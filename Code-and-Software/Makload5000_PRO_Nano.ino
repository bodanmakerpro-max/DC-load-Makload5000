/*
 Makload5000 PRO Nano v2.1
 Wiring: I2C 16x2 SDA=A4 SCL=A5, VCC=5V, GND=GND
 Encoder CLK=D4 DT=D5 SW=D6, + =5V, GND=GND
 Shunt + -> A0 (direct sense; protection resistor recommended), shunt - -> GND
 Load input + -> 220k -> node -> 1k -> A1
                                 node -> 10k -> GND
                                 node -> 100nF -> GND
 PWM D9 -> 39k -> LM358 noninverting input node
                    node -> 10k -> GND
                    node -> 10uF (+ at node) -> GND
 Control and load negatives MUST share GND. Use a separate 9V
 supply for original LM358 circuit, not Nano 5V. No negative voltages
 on analog pins. Maximum rated LOAD INPUT 15V, 3A. Monitor MOSFET
 dissipation and temperature; software alone cannot guarantee safety.
 LCD I2C address 0x27; switch to 0x3F if needed.
*/
/*
  DC Electronic Load Controller
  OLD feedback behavior + FIXED 3-point calibration

  This keeps the earlier feedback controller:
    - 150 ms control interval
    - 0.020 A deadband
    - PWM correction steps 1 / 2 / 3 / 5
    - no 10-reading moving-average controller

  Calibration:
    CAL 1 0.940
    CAL 2 1.930
    CAL 3 2.910

  Each CAL command saves:
    raw shunt voltage at that moment -> real current from lab supply

  IMPORTANT:
  The calibration command parser does NOT use sscanf("%f").
  It uses strtok() + atof(), which is much more reliable on AVR Arduinos.

  Wiring:
    Shunt + -> A0
    Shunt - -> Arduino GND

    D9 PWM -> 39k -> control node -> LM358 + input
                         |
                         +---- 10k ---- GND
                         |
                         +---- 10uF --- GND
                              + toward control node

    Arduino GND must be common with load GND.
*/

#include <Arduino.h>
#include <EEPROM.h>
#include <math.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

const uint8_t CURRENT_PIN = A0;
const uint8_t VOLTAGE_PIN = A1;
// Input divider: 220k (top) and 10k (bottom), plus 100nF to GND.
// Both ADC channels use Nano INTERNAL 1.1 V reference.
const float VOLTAGE_R_TOP = 220000.0f;
const float VOLTAGE_R_BOTTOM = 10000.0f;
const float VOLTAGE_CAL_FACTOR = 1.000f; // Adjust against multimeter
const float MAX_INPUT_V = 15.0f;
const float OVERVOLT_TRIP_V = 15.5f;

const uint8_t PWM_PIN = 9;

// =============================
// LIMITS
// =============================
const float MAX_SET_CURRENT_A = 3.00;
const float HARD_TRIP_CURRENT_A = 3.40;

// Fallback before calibration
const float DEFAULT_SHUNT_VOLTS_PER_AMP = 0.100;

// =============================
// ADC REFERENCE
// =============================
#if defined(__AVR_ATmega32U4__)
float ADC_REFERENCE_V = 2.560;   // Leonardo / Pro Micro
#else
float ADC_REFERENCE_V = 1.100;   // Pro Mini / Uno / Nano
#endif

// =============================
// PWM DIVIDER
// D9 -> 39k -> node -> 10k -> GND
// =============================
const float PWM_HIGH_VOLTAGE = 5.000;
const float R_TOP = 39000.0;
const float R_BOTTOM = 10000.0;

const float PWM_FULL_SCALE_V =
  PWM_HIGH_VOLTAGE * (R_BOTTOM / (R_TOP + R_BOTTOM));

const float MAX_CONTROL_V = 0.700;

const int MAX_PWM =
  (int)((MAX_CONTROL_V / PWM_FULL_SCALE_V) * 255.0 + 0.5);

// =============================
// OLD CONTROLLER SETTINGS
// =============================
const unsigned long CONTROL_PERIOD_MS = 150;
const float CURRENT_DEADBAND_A = 0.020;

const float ERROR_BIG_A = 0.50;
const float ERROR_MED_A = 0.20;
const float ERROR_SMALL_A = 0.06;

// =============================
// CALIBRATION STORAGE
// =============================
struct CalibrationPoint {
  float shuntVoltage;
  float actualCurrent;
  uint8_t valid;
};

struct CalibrationStore {
  uint32_t magic;
  CalibrationPoint point[3];
};

const uint32_t EEPROM_MAGIC = 0xDC360006;
CalibrationStore cal;

// =============================
// STATE
// =============================
float requestedCurrentA = 0.0;
float measuredCurrentA = 0.0;
float shuntVoltageV = 0.0;
float controlVoltageV = 0.0;
float loadInputV = 0.0;
float loadPowerW = 0.0;

bool loadEnabled = false;
bool safetyTrip = false;

int pwmValue = 0;
uint8_t overCurrentSamples = 0;

unsigned long lastControlMs = 0;
unsigned long lastTelemetryMs = 0;

char commandBuffer[80];
uint8_t commandIndex = 0;

// =====================================================
// EEPROM
// =====================================================
void clearCalibration() {
  cal.magic = EEPROM_MAGIC;

  for (uint8_t i = 0; i < 3; i++) {
    cal.point[i].shuntVoltage = 0.0;
    cal.point[i].actualCurrent = 0.0;
    cal.point[i].valid = 0;
  }

  EEPROM.put(0, cal);
}

void loadCalibration() {
  EEPROM.get(0, cal);

  if (cal.magic != EEPROM_MAGIC) {
    clearCalibration();
  }
}

void saveCalibration() {
  cal.magic = EEPROM_MAGIC;
  EEPROM.put(0, cal);
}

// =====================================================
// SHUNT READING
// Same style as old code: average ADC samples once.
// =====================================================
float readShuntVoltage() {
  const uint8_t SAMPLES = 48;
  unsigned long total = 0;

  for (uint8_t i = 0; i < SAMPLES; i++) {
    total += analogRead(CURRENT_PIN);
  }

  float adc = total / (float)SAMPLES;

  return adc * ADC_REFERENCE_V / 1023.0;
}

// =====================================================
// CALIBRATION MATH
// =====================================================
uint8_t getCalibrationPoints(CalibrationPoint *points) {
  uint8_t count = 0;

  for (uint8_t i = 0; i < 3; i++) {
    if (cal.point[i].valid &&
        cal.point[i].shuntVoltage > 0.0001 &&
        cal.point[i].actualCurrent > 0.0001) {
      points[count++] = cal.point[i];
    }
  }

  // Sort by shunt voltage
  for (uint8_t i = 0; i < count; i++) {
    for (uint8_t j = i + 1; j < count; j++) {
      if (points[j].shuntVoltage < points[i].shuntVoltage) {
        CalibrationPoint t = points[i];
        points[i] = points[j];
        points[j] = t;
      }
    }
  }

  return count;
}

float interpolateCurrent(float voltage,
                         float v1, float a1,
                         float v2, float a2) {
  if (fabs(v2 - v1) < 0.000001) {
    return a1;
  }

  return a1 +
         (voltage - v1) *
         (a2 - a1) /
         (v2 - v1);
}

float getActualCurrent(float voltage) {
  CalibrationPoint p[3];
  uint8_t count = getCalibrationPoints(p);

  // No calibration yet
  if (count == 0) {
    return voltage / DEFAULT_SHUNT_VOLTS_PER_AMP;
  }

  // One point: scale through zero
  if (count == 1) {
    return voltage * p[0].actualCurrent / p[0].shuntVoltage;
  }

  // Below first calibration point
  if (voltage <= p[0].shuntVoltage) {
    return interpolateCurrent(
      voltage,
      0.0, 0.0,
      p[0].shuntVoltage, p[0].actualCurrent
    );
  }

  // Between points
  for (uint8_t i = 0; i < count - 1; i++) {
    if (voltage <= p[i + 1].shuntVoltage) {
      return interpolateCurrent(
        voltage,
        p[i].shuntVoltage, p[i].actualCurrent,
        p[i + 1].shuntVoltage, p[i + 1].actualCurrent
      );
    }
  }

  // Above highest point
  return interpolateCurrent(
    voltage,
    p[count - 2].shuntVoltage, p[count - 2].actualCurrent,
    p[count - 1].shuntVoltage, p[count - 1].actualCurrent
  );
}

float readInputVoltage() {
  // Allow settling from the previous A0 ADC channel before averaging.
  analogRead(VOLTAGE_PIN);
  unsigned long sum = 0;
  for (uint8_t i = 0; i < 32; i++) sum += analogRead(VOLTAGE_PIN);
  float pinV = (sum / 32.0f) * ADC_REFERENCE_V / 1023.0f;
  float measuredV = pinV * (VOLTAGE_R_TOP + VOLTAGE_R_BOTTOM) /
                    VOLTAGE_R_BOTTOM * VOLTAGE_CAL_FACTOR;
  return measuredV < 0.0f ? 0.0f : measuredV;
}

void readCurrent() {
  shuntVoltageV = readShuntVoltage();
  measuredCurrentA = getActualCurrent(shuntVoltageV);
  loadInputV = readInputVoltage();
  loadPowerW = loadInputV * measuredCurrentA;

  if (measuredCurrentA < 0.0) {
    measuredCurrentA = 0.0;
  }
}

// =====================================================
// PWM
// =====================================================
void setPWM(int value) {
  if (!loadEnabled || safetyTrip) {
    value = 0;
  }

  value = constrain(value, 0, MAX_PWM);
  pwmValue = value;

  analogWrite(PWM_PIN, pwmValue);

  controlVoltageV =
    (pwmValue / 255.0) * PWM_FULL_SCALE_V;
}

// =====================================================
// SAFETY
// =====================================================
void tripLoad() {
  safetyTrip = true;
  loadEnabled = false;

  pwmValue = 0;
  analogWrite(PWM_PIN, 0);
  controlVoltageV = 0.0;
}

// =====================================================
// OLD CLOSED LOOP
// =====================================================
void controlCurrent() {
  unsigned long now = millis();

  if (now - lastControlMs < CONTROL_PERIOD_MS) {
    return;
  }

  lastControlMs = now;

  readCurrent();

  // Secondary software protection. Do not rely on this for overvoltage safety.
  if (loadInputV > OVERVOLT_TRIP_V) {
    tripLoad();
    return;
  }

  if (measuredCurrentA >= HARD_TRIP_CURRENT_A) {
    if (overCurrentSamples < 255) {
      overCurrentSamples++;
    }

    if (overCurrentSamples >= 3) {
      tripLoad();
      return;
    }
  } else {
    overCurrentSamples = 0;
  }

  if (!loadEnabled || safetyTrip) {
    setPWM(0);
    return;
  }

  if (requestedCurrentA <= 0.001) {
    setPWM(0);
    return;
  }

  float error = requestedCurrentA - measuredCurrentA;

  if (fabs(error) <= CURRENT_DEADBAND_A) {
    return;
  }

  int step = 1;
  float absError = fabs(error);

  if (absError >= ERROR_BIG_A) {
    step = 5;
  }
  else if (absError >= ERROR_MED_A) {
    step = 3;
  }
  else if (absError >= ERROR_SMALL_A) {
    step = 2;
  }

  if (error > 0.0) {
    setPWM(pwmValue + step);
  }
  else {
    setPWM(pwmValue - step);
  }
}

// =====================================================
// FIXED CALIBRATION CAPTURE
// =====================================================
bool captureCalibration(uint8_t point, float realCurrent) {
  if (point < 1 || point > 3) {
    return false;
  }

  if (realCurrent < 0.05 || realCurrent > 5.0) {
    return false;
  }

  // Capture a stable average of the shunt voltage.
  // This ONLY affects calibration capture, not the controller behavior.
  float sum = 0.0;

  for (uint8_t i = 0; i < 10; i++) {
    sum += readShuntVoltage();
    delay(15);
  }

  float capturedV = sum / 10.0;

  if (capturedV < 0.001) {
    return false;
  }

  uint8_t index = point - 1;

  cal.point[index].shuntVoltage = capturedV;
  cal.point[index].actualCurrent = realCurrent;
  cal.point[index].valid = 1;

  saveCalibration();

  // Re-read using the newly saved curve
  readCurrent();

  return true;
}

// =====================================================
// SERIAL OUTPUT
// =====================================================
void sendCalibration() {
  // CALDATA,valid1,V1,A1,valid2,V2,A2,valid3,V3,A3
  Serial.print(F("CALDATA"));

  for (uint8_t i = 0; i < 3; i++) {
    Serial.print(',');
    Serial.print(cal.point[i].valid ? 1 : 0);

    Serial.print(',');
    Serial.print(cal.point[i].shuntVoltage, 5);

    Serial.print(',');
    Serial.print(cal.point[i].actualCurrent, 4);
  }

  Serial.println();
}

void sendTelemetry() {
  unsigned long now = millis();

  if (now - lastTelemetryMs < 200) {
    return;
  }

  lastTelemetryMs = now;

  // DATA,set,current,shunt,control,pwm,enabled,tripped
  Serial.print(F("DATA,"));
  Serial.print(requestedCurrentA, 3);
  Serial.print(',');
  Serial.print(measuredCurrentA, 3);
  Serial.print(',');
  Serial.print(shuntVoltageV, 5);
  Serial.print(',');
  Serial.print(controlVoltageV, 4);
  Serial.print(',');
  Serial.print(pwmValue);
  Serial.print(',');
  Serial.print(loadEnabled ? 1 : 0);
  Serial.print(',');
  Serial.println(safetyTrip ? 1 : 0);
  // Additional message, existing DATA packet remains unchanged for HTML UI.
  Serial.print(F("VDATA,"));
  Serial.print(loadInputV, 3);
  Serial.print(',');
  Serial.println(loadPowerW, 3);
}

// User preferences are separate from the existing calibration at EEPROM address 0.
const int SETTINGS_ADDR = 96;
const uint32_t SETTINGS_MAGIC = 0x4D4C5032UL;
struct UserSettings {
  uint32_t magic;
  uint8_t stepIndex;       // 0=0.01A, 1=0.05A, 2=0.10A
  uint16_t maxCurrentCentiA;
  uint8_t backlightMode;   // 0=always on, 1=auto 30s
  uint8_t rememberSetpoint;
  uint16_t savedSetpointCentiA;
};
UserSettings settings;

void noteSetpointChanged();

// =====================================================
// FIXED COMMAND PARSER
// =====================================================
void processCommand(char *cmd) {
  while (*cmd == ' ') {
    cmd++;
  }

  if (strncmp(cmd, "SET ", 4) == 0) {
    float amps = atof(cmd + 4);

    requestedCurrentA =
      constrain(amps, 0.0, settings.maxCurrentCentiA / 100.0f);
    noteSetpointChanged();

    if (requestedCurrentA <= 0.001) {
      setPWM(0);
    }

    Serial.print(F("ACK,SET,"));
    Serial.println(requestedCurrentA, 3);
    return;
  }

  if (strcmp(cmd, "ON") == 0) {
    if (!safetyTrip) {
      loadEnabled = true;

      // Old behavior: start from 0 and climb to setpoint
      pwmValue = 0;
      analogWrite(PWM_PIN, 0);
      controlVoltageV = 0.0;

      Serial.println(F("ACK,ON"));
    }
    return;
  }

  if (strcmp(cmd, "OFF") == 0) {
    loadEnabled = false;
    setPWM(0);

    Serial.println(F("ACK,OFF"));
    return;
  }

  if (strcmp(cmd, "CLEAR") == 0) {
    safetyTrip = false;
    overCurrentSamples = 0;
    loadEnabled = false;

    pwmValue = 0;
    analogWrite(PWM_PIN, 0);
    controlVoltageV = 0.0;

    Serial.println(F("ACK,CLEAR"));
    return;
  }

  // -------------------------------------------------
  // FIXED CAL COMMAND PARSING
  //
  // Example:
  // CAL 2 1.930
  //
  // strtok + atof avoids AVR sscanf float problems.
  // -------------------------------------------------
  if (strncmp(cmd, "CAL ", 4) == 0) {
    char temp[80];
    strncpy(temp, cmd, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';

    char *token = strtok(temp, " "); // CAL
    token = strtok(NULL, " ");       // point number

    if (token == NULL) {
      Serial.println(F("CALRESULT,0,ERROR,BAD_POINT"));
      return;
    }

    int point = atoi(token);

    token = strtok(NULL, " ");       // real current

    if (token == NULL) {
      Serial.print(F("CALRESULT,"));
      Serial.print(point);
      Serial.println(F(",ERROR,BAD_CURRENT"));
      return;
    }

    float realCurrent = atof(token);

    bool ok = captureCalibration(
      (uint8_t)point,
      realCurrent
    );

    Serial.print(F("CALRESULT,"));
    Serial.print(point);
    Serial.print(',');

    if (ok) {
      Serial.print(F("OK,"));
      Serial.print(cal.point[point - 1].shuntVoltage, 5);
      Serial.print(',');
      Serial.println(cal.point[point - 1].actualCurrent, 4);
    }
    else {
      Serial.println(F("ERROR,CAPTURE_FAILED"));
    }

    sendCalibration();
    return;
  }

  if (strcmp(cmd, "GETCAL") == 0) {
    sendCalibration();
    return;
  }

  if (strcmp(cmd, "CALRESET") == 0) {
    clearCalibration();

    Serial.println(F("CALRESET,OK"));
    sendCalibration();
    return;
  }

  Serial.print(F("ERROR,UNKNOWN_COMMAND,"));
  Serial.println(cmd);
}

void readSerial() {
  while (Serial.available()) {
    char c = Serial.read();

    if (c == '\n' || c == '\r') {
      if (commandIndex > 0) {
        commandBuffer[commandIndex] = '\0';
        processCommand(commandBuffer);
        commandIndex = 0;
      }
    }
    else if (commandIndex < sizeof(commandBuffer) - 1) {
      commandBuffer[commandIndex++] = c;
    }
  }
}

// =====================================================
// STANDALONE 16x2 I2C LCD + ROTARY ENCODER
// Uno/Nano: LCD SDA=A4, SCL=A5; encoder CLK=D4, DT=D5, SW=D6
// The original serial dashboard still works without changes.
// =====================================================
LiquidCrystal_I2C lcd(0x27, 16, 2); // Change to 0x3F if needed
const uint8_t ENC_CLK = 4;
const uint8_t ENC_DT  = 5;
const uint8_t ENC_SW  = 6;

const uint16_t STEP_CENTIA[] = {1, 5, 10};

enum Screen : uint8_t { HOME, MAIN_MENU, SETTINGS_MENU, INFO_SCREEN };
Screen screen = HOME;
uint8_t mainItem = 0;
uint8_t settingItem = 0;
uint8_t infoPage = 0;
uint8_t lastEncoderAB = 0;
int8_t encoderAccumulator = 0;
bool rawButtonPressed = false, stableButtonPressed = false;
bool longPressHandled = false;
unsigned long lastButtonTransition = 0, buttonDownAt = 0;
unsigned long lastInputMs = 0, lastLcdUpdateMs = 0;
unsigned long lastSetpointChangeMs = 0;
bool dirtySetpoint = false;
bool lightOn = true;

void defaultSettings() {
  settings.magic = SETTINGS_MAGIC;
  settings.stepIndex = 1;
  settings.maxCurrentCentiA = 300;
  settings.backlightMode = 0;
  settings.rememberSetpoint = 1;
  settings.savedSetpointCentiA = 0;
}

void saveSettings() { EEPROM.put(SETTINGS_ADDR, settings); }

void loadSettings() {
  EEPROM.get(SETTINGS_ADDR, settings);
  if (settings.magic != SETTINGS_MAGIC ||
      settings.stepIndex > 2 ||
      settings.maxCurrentCentiA < 10 || settings.maxCurrentCentiA > 300 ||
      settings.backlightMode > 1 || settings.rememberSetpoint > 1 ||
      settings.savedSetpointCentiA > 300) {
    defaultSettings();
    saveSettings();
  }
  requestedCurrentA = settings.rememberSetpoint ?
    min(settings.savedSetpointCentiA, settings.maxCurrentCentiA) / 100.0f : 0.0f;
  loadEnabled = false; // Never restore load ON after power loss
}

void powerOff() {
  loadEnabled = false;
  setPWM(0);
}

void togglePower() {
  if (loadEnabled) {
    powerOff();
  } else if (!safetyTrip && requestedCurrentA > 0.001f) {
    // Reset output so the closed-loop control ramps up from zero.
    pwmValue = 0;
    analogWrite(PWM_PIN, 0);
    controlVoltageV = 0.0;
    loadEnabled = true;
  }
}

void noteSetpointChanged() {
  lastSetpointChangeMs = millis();
  dirtySetpoint = true;
}

void setLocalCurrentCentiA(int value) {
  value = constrain(value, 0, (int)settings.maxCurrentCentiA);
  requestedCurrentA = value / 100.0f;
  if (value == 0) setPWM(0);
  noteSetpointChanged();
}

void inputActivity() {
  lastInputMs = millis();
  if (!lightOn) { lcd.backlight(); lightOn = true; }
  lastLcdUpdateMs = 0;
}

void encoderMove(int8_t dir) {
  inputActivity();
  if (screen == HOME) {
    int hundredths = (int)(requestedCurrentA * 100.0f + 0.5f);
    setLocalCurrentCentiA(hundredths + dir * STEP_CENTIA[settings.stepIndex]);
  } else if (screen == MAIN_MENU) {
    mainItem = (mainItem + 5 + dir) % 5;
  } else if (screen == SETTINGS_MENU) {
    settingItem = (settingItem + 6 + dir) % 6;
  } else if (screen == INFO_SCREEN) {
    infoPage = (infoPage + 2 + dir) % 2;
  }
}

void handleShortPress() {
  inputActivity();
  if (screen == HOME) {
    screen = MAIN_MENU;
    mainItem = 0;
  } else if (screen == MAIN_MENU) {
    switch (mainItem) {
      case 0: togglePower(); break;
      case 1: screen = SETTINGS_MENU; settingItem = 0; break;
      case 2: screen = INFO_SCREEN; infoPage = 0; break;
      case 3:
        if (safetyTrip) {
          safetyTrip = false;
          overCurrentSamples = 0;
          powerOff();
        }
        break;
      case 4: screen = HOME; break;
    }
  } else if (screen == SETTINGS_MENU) {
    switch (settingItem) {
      case 0: settings.stepIndex = (settings.stepIndex + 1) % 3; break;
      case 1:
        // Cycle 0.5, 1.0, 1.5, 2.0, 2.5, 3.0 A.
        settings.maxCurrentCentiA += 50;
        if (settings.maxCurrentCentiA > 300) settings.maxCurrentCentiA = 50;
        if (requestedCurrentA > settings.maxCurrentCentiA / 100.0f)
          setLocalCurrentCentiA(settings.maxCurrentCentiA);
        break;
      case 2: settings.backlightMode ^= 1; break;
      case 3:
        settings.rememberSetpoint ^= 1;
        if (settings.rememberSetpoint)
          settings.savedSetpointCentiA = (uint16_t)(requestedCurrentA * 100.0f + 0.5f);
        break;
      case 4:
        defaultSettings();
        // Resetting UI settings does not touch current calibration.
        noteSetpointChanged();
        break;
      case 5: screen = MAIN_MENU; mainItem = 1; break;
    }
    saveSettings();
  } else {
    screen = MAIN_MENU;
    mainItem = 2;
  }
}

void handleLongPress() {
  inputActivity();
  if (screen == HOME) togglePower();
  else if (screen == SETTINGS_MENU || screen == INFO_SCREEN) screen = MAIN_MENU;
  else screen = HOME;
}

void serviceEncoder() {
  static const int8_t quadrature[16] = {
     0,-1, 1, 0,  1, 0, 0,-1,
    -1, 0, 0, 1,  0, 1,-1, 0
  };
  uint8_t ab = (digitalRead(ENC_CLK) << 1) | digitalRead(ENC_DT);
  if (ab != lastEncoderAB) {
    encoderAccumulator += quadrature[(lastEncoderAB << 2) | ab];
    lastEncoderAB = ab;
    if (encoderAccumulator >= 4) { encoderAccumulator = 0; encoderMove(1); }
    if (encoderAccumulator <= -4) { encoderAccumulator = 0; encoderMove(-1); }
  }

  unsigned long now = millis();
  bool pressed = digitalRead(ENC_SW) == LOW;
  if (pressed != rawButtonPressed) {
    rawButtonPressed = pressed;
    lastButtonTransition = now;
  }
  if (now - lastButtonTransition >= 35 && stableButtonPressed != rawButtonPressed) {
    stableButtonPressed = rawButtonPressed;
    if (stableButtonPressed) {
      buttonDownAt = now;
      longPressHandled = false;
    } else if (!longPressHandled) {
      handleShortPress();
    }
  }
  if (stableButtonPressed && !longPressHandled && now - buttonDownAt >= 750) {
    longPressHandled = true;
    handleLongPress();
  }
}

void lcdLine(uint8_t row, const char *msg) {
  lcd.setCursor(0, row);
  uint8_t i = 0;
  while (msg[i] && i < 16) lcd.print(msg[i++]);
  while (i++ < 16) lcd.print(' ');
}

const char *mainTitles[] = {"Load power", "Settings", "Device info", "Clear trip", "Back"};
const char *settingTitles[] = {"Current step", "Max current", "Backlight", "Save setpoint", "Reset settings", "Back"};

void serviceLcd() {
  unsigned long now = millis();
  if (lastLcdUpdateMs && now - lastLcdUpdateMs < 200) return;
  lastLcdUpdateMs = now ? now : 1;
  char line[17];
  if (screen == HOME) {
    // S=set current, M=measured current. Bottom=load voltage and watts.
    lcd.setCursor(0, 0);
    lcd.print('S'); lcd.print(requestedCurrentA, 2);
    lcd.print(F(" M")); lcd.print(measuredCurrentA, 2);
    lcd.print(' ');
    if (safetyTrip) lcd.print(F("TRIP"));
    else if (loadEnabled) lcd.print(F("ON  "));
    else lcd.print(F("OFF "));
    lcd.setCursor(0, 1);
    lcd.print(loadInputV, 2); lcd.print(F("V "));
    lcd.print(loadPowerW, 2); lcd.print(F("W     "));
  } else if (screen == MAIN_MENU) {
    snprintf(line, sizeof(line), ">%s", mainTitles[mainItem]);
    lcdLine(0, line);
    switch (mainItem) {
      case 0: lcdLine(1, safetyTrip ? "Trip: clear first" : (loadEnabled ? "Press: turn OFF" : "Press: turn ON")); break;
      case 1: lcdLine(1, "Press to open"); break;
      case 2: lcdLine(1, "Firmware / limits"); break;
      case 3: lcdLine(1, safetyTrip ? "Press to reset" : "No active trip"); break;
      case 4: lcdLine(1, "Return to home"); break;
    }
  } else if (screen == SETTINGS_MENU) {
    snprintf(line, sizeof(line), ">%s", settingTitles[settingItem]);
    lcdLine(0, line);
    switch (settingItem) {
      case 0: snprintf(line, sizeof(line), "Step: %u mA", STEP_CENTIA[settings.stepIndex] * 10); break;
      case 1: snprintf(line, sizeof(line), "Limit: %u.%02u A", settings.maxCurrentCentiA/100, settings.maxCurrentCentiA%100); break;
      case 2: strcpy(line, settings.backlightMode ? "Auto off: 30 sec" : "Always ON"); break;
      case 3: strcpy(line, settings.rememberSetpoint ? "Remember: YES" : "Remember: NO"); break;
      case 4: strcpy(line, "Press to reset"); break;
      default: strcpy(line, "Return to menu"); break;
    }
    lcdLine(1, line);
  } else {
    if (infoPage == 0) {
      lcdLine(0, "Makload5000 PRO");
      lcdLine(1, "Nano firmware 2.1");
    } else {
      lcdLine(0, "Max 15V / 3A");
      lcdLine(1, "3-point cal / PWM");
    }
  }
  if (settings.backlightMode && !stableButtonPressed &&
      now - lastInputMs > 30000UL && lightOn) {
    lcd.noBacklight();
    lightOn = false;
  }
}

void servicePreferences() {
  if (!dirtySetpoint || millis() - lastSetpointChangeMs < 1800UL) return;
  dirtySetpoint = false;
  if (!settings.rememberSetpoint) return;
  uint16_t hundredths = (uint16_t)(requestedCurrentA * 100.0f + 0.5f);
  if (settings.savedSetpointCentiA != hundredths) {
    settings.savedSetpointCentiA = hundredths;
    saveSettings(); // EEPROM.put updates only changed bytes
  }
}

void setup() {
  pinMode(PWM_PIN, OUTPUT);
  analogWrite(PWM_PIN, 0);
  analogReference(INTERNAL); // ATmega328P internal 1.1 V for A0 and A1
  Serial.begin(115200);
  delay(100);
  analogRead(CURRENT_PIN);
  loadCalibration();
  loadSettings();

  pinMode(ENC_CLK, INPUT_PULLUP);
  pinMode(ENC_DT, INPUT_PULLUP);
  pinMode(ENC_SW, INPUT_PULLUP);
  lastEncoderAB = (digitalRead(ENC_CLK) << 1) | digitalRead(ENC_DT);
  rawButtonPressed = digitalRead(ENC_SW) == LOW;
  stableButtonPressed = rawButtonPressed;

  lcd.init();
  lcd.backlight();
  lcdLine(0, "Makload5000 PRO");
  lcdLine(1, "Starting - OFF");
  lastInputMs = millis();
  lastControlMs = millis();
  Serial.println(F("READY"));
  sendCalibration();
}

void loop() {
  readSerial();
  serviceEncoder();
  controlCurrent();
  sendTelemetry();
  servicePreferences();
  serviceLcd();
}