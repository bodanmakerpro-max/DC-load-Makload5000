/*
  Makload5000 DC Electronic Load Controller
  Stable PC control + standalone 16x2 LCD / rotary encoder control

  Board target:
    5 V Arduino Pro Micro / ATmega32U4

  Existing load connections kept:
    Shunt + -> A0
    Shunt - -> Arduino GND
    D9 PWM -> 39k -> control node -> LM358 + input
                         |
                         +---- 10k ---- GND
                         |
                         +---- 10uF --- GND
                              + toward control node

  16x2 PARALLEL LCD:
    RS -> D4
    E  -> D5
    D4 -> D6
    D5 -> D7
    D6 -> D8
    D7 -> D10
    RW -> GND
    VSS -> GND
    VDD -> 5V
    VO -> middle pin of 10k contrast potentiometer
    LCD backlight A -> 5V (through resistor if your LCD board needs one)
    LCD backlight K -> GND

  ROTARY ENCODER:
    CLK -> D2
    DT  -> D3
    SW  -> D15
    GND -> GND
    VCC -> 5V for a KY-040 module

  Local controls:
    HOME:
      Rotate encoder = change current setpoint in 0.05 A steps
      Press encoder  = open menu

    MENU:
      Rotate = move through menu
      Press  = select

    Menu:
      LOAD ON/OFF
      SHUNT INFO
      CONTROL INFO
      CLEAR TRIP
      BACK

  The HTML dashboard remains fully compatible.
  Calibration remains available through the HTML dashboard and is saved in EEPROM.
*/

#include <Arduino.h>
#include <EEPROM.h>
#include <math.h>
#include <LiquidCrystal.h>

// =====================================================
// LOAD PINS
// =====================================================
const uint8_t CURRENT_PIN = A0;
const uint8_t PWM_PIN = 9;

// =====================================================
// LCD PINS - 16x2 parallel, 4-bit mode
// =====================================================
const uint8_t LCD_RS = 4;
const uint8_t LCD_EN = 5;
const uint8_t LCD_D4 = 6;
const uint8_t LCD_D5 = 7;
const uint8_t LCD_D6 = 8;
const uint8_t LCD_D7 = 10;

LiquidCrystal lcd(
  LCD_RS,
  LCD_EN,
  LCD_D4,
  LCD_D5,
  LCD_D6,
  LCD_D7
);

// =====================================================
// ROTARY ENCODER PINS
// =====================================================
const uint8_t ENC_CLK = 2;
const uint8_t ENC_DT  = 3;
const uint8_t ENC_SW  = 15;

// Set true if your encoder turns backwards.
const bool ENCODER_REVERSED = false;

const float LOCAL_SET_STEP_A = 0.05;

// =====================================================
// LIMITS
// =====================================================
const float MAX_SET_CURRENT_A = 3.00;
const float HARD_TRIP_CURRENT_A = 3.40;

// Fallback before calibration
const float DEFAULT_SHUNT_VOLTS_PER_AMP = 0.100;

// =====================================================
// ADC REFERENCE
// =====================================================
#if defined(__AVR_ATmega32U4__)
float ADC_REFERENCE_V = 2.560;   // Leonardo / Pro Micro
#else
float ADC_REFERENCE_V = 1.100;   // Pro Mini / Uno / Nano
#endif

// =====================================================
// PWM DIVIDER
// D9 -> 39k -> node -> 10k -> GND
//               |
//               +---- 10uF ---- GND
// =====================================================
const float PWM_HIGH_VOLTAGE = 5.000;
const float R_TOP = 39000.0;
const float R_BOTTOM = 10000.0;

const float PWM_FULL_SCALE_V =
  PWM_HIGH_VOLTAGE * (R_BOTTOM / (R_TOP + R_BOTTOM));

// Keep the old stable controller limit unchanged.
const float MAX_CONTROL_V = 0.700;

const int MAX_PWM =
  (int)((MAX_CONTROL_V / PWM_FULL_SCALE_V) * 255.0 + 0.5);

// =====================================================
// OLD STABLE CONTROLLER SETTINGS
// =====================================================
const unsigned long CONTROL_PERIOD_MS = 150;
const float CURRENT_DEADBAND_A = 0.020;

const float ERROR_BIG_A = 0.50;
const float ERROR_MED_A = 0.20;
const float ERROR_SMALL_A = 0.06;

// =====================================================
// CALIBRATION STORAGE
// =====================================================
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

// =====================================================
// LOAD STATE
// =====================================================
float requestedCurrentA = 0.0;
float measuredCurrentA = 0.0;
float shuntVoltageV = 0.0;
float controlVoltageV = 0.0;

bool loadEnabled = false;
bool safetyTrip = false;

int pwmValue = 0;
uint8_t overCurrentSamples = 0;

unsigned long lastControlMs = 0;
unsigned long lastTelemetryMs = 0;

char commandBuffer[80];
uint8_t commandIndex = 0;

// =====================================================
// LOCAL UI STATE
// =====================================================
enum UiScreen {
  UI_HOME,
  UI_MENU,
  UI_SHUNT_INFO,
  UI_CONTROL_INFO
};

UiScreen uiScreen = UI_HOME;

const uint8_t MENU_COUNT = 5;
uint8_t menuIndex = 0;

int lastEncoderClk = HIGH;
unsigned long lastEncoderMs = 0;

bool lastButtonReading = HIGH;
bool stableButtonState = HIGH;
unsigned long lastButtonChangeMs = 0;

unsigned long lastLcdMs = 0;

const unsigned long LCD_REFRESH_MS = 180;
const unsigned long BUTTON_DEBOUNCE_MS = 25;
const unsigned long ENCODER_DEBOUNCE_MS = 2;

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

float interpolateCurrent(
  float voltage,
  float v1,
  float a1,
  float v2,
  float a2
) {
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

  if (count == 0) {
    return voltage / DEFAULT_SHUNT_VOLTS_PER_AMP;
  }

  if (count == 1) {
    return voltage * p[0].actualCurrent / p[0].shuntVoltage;
  }

  if (voltage <= p[0].shuntVoltage) {
    return interpolateCurrent(
      voltage,
      0.0, 0.0,
      p[0].shuntVoltage, p[0].actualCurrent
    );
  }

  for (uint8_t i = 0; i < count - 1; i++) {
    if (voltage <= p[i + 1].shuntVoltage) {
      return interpolateCurrent(
        voltage,
        p[i].shuntVoltage, p[i].actualCurrent,
        p[i + 1].shuntVoltage, p[i + 1].actualCurrent
      );
    }
  }

  return interpolateCurrent(
    voltage,
    p[count - 2].shuntVoltage, p[count - 2].actualCurrent,
    p[count - 1].shuntVoltage, p[count - 1].actualCurrent
  );
}

void readCurrent() {
  shuntVoltageV = readShuntVoltage();
  measuredCurrentA = getActualCurrent(shuntVoltageV);

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
// LOAD ACTIONS
// Used by BOTH the HTML dashboard and local encoder.
// =====================================================
void enableLoad() {
  if (safetyTrip) {
    return;
  }

  loadEnabled = true;

  // Same old behavior: begin at zero and climb.
  pwmValue = 0;
  analogWrite(PWM_PIN, 0);
  controlVoltageV = 0.0;
}

void disableLoad() {
  loadEnabled = false;
  setPWM(0);
}

void clearTrip() {
  safetyTrip = false;
  overCurrentSamples = 0;
  loadEnabled = false;

  pwmValue = 0;
  analogWrite(PWM_PIN, 0);
  controlVoltageV = 0.0;
}

void tripLoad() {
  safetyTrip = true;
  loadEnabled = false;

  pwmValue = 0;
  analogWrite(PWM_PIN, 0);
  controlVoltageV = 0.0;
}

// =====================================================
// OLD STABLE CLOSED LOOP
// =====================================================
void controlCurrent() {
  unsigned long now = millis();

  if (now - lastControlMs < CONTROL_PERIOD_MS) {
    return;
  }

  lastControlMs = now;

  readCurrent();

  if (measuredCurrentA >= HARD_TRIP_CURRENT_A) {
    if (overCurrentSamples < 255) {
      overCurrentSamples++;
    }

    if (overCurrentSamples >= 3) {
      tripLoad();
      return;
    }
  }
  else {
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
// CALIBRATION CAPTURE
// =====================================================
bool captureCalibration(uint8_t point, float realCurrent) {
  if (point < 1 || point > 3) {
    return false;
  }

  if (realCurrent < 0.05 || realCurrent > 5.0) {
    return false;
  }

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
  readCurrent();

  return true;
}

// =====================================================
// SERIAL OUTPUT - unchanged format for HTML V6
// =====================================================
void sendCalibration() {
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
}

// =====================================================
// SERIAL COMMAND PARSER - compatible with HTML V6
// =====================================================
void processCommand(char *cmd) {
  while (*cmd == ' ') {
    cmd++;
  }

  if (strncmp(cmd, "SET ", 4) == 0) {
    float amps = atof(cmd + 4);

    requestedCurrentA =
      constrain(amps, 0.0, MAX_SET_CURRENT_A);

    if (requestedCurrentA <= 0.001) {
      setPWM(0);
    }

    Serial.print(F("ACK,SET,"));
    Serial.println(requestedCurrentA, 3);
    return;
  }

  if (strcmp(cmd, "ON") == 0) {
    enableLoad();

    if (loadEnabled) {
      Serial.println(F("ACK,ON"));
    }
    else {
      Serial.println(F("ERROR,TRIPPED"));
    }
    return;
  }

  if (strcmp(cmd, "OFF") == 0) {
    disableLoad();
    Serial.println(F("ACK,OFF"));
    return;
  }

  if (strcmp(cmd, "CLEAR") == 0) {
    clearTrip();
    Serial.println(F("ACK,CLEAR"));
    return;
  }

  if (strncmp(cmd, "CAL ", 4) == 0) {
    char temp[80];
    strncpy(temp, cmd, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';

    char *token = strtok(temp, " ");
    token = strtok(NULL, " ");

    if (token == NULL) {
      Serial.println(F("CALRESULT,0,ERROR,BAD_POINT"));
      return;
    }

    int point = atoi(token);

    token = strtok(NULL, " ");

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
// ROTARY ENCODER
// =====================================================
int readEncoderStep() {
  int clk = digitalRead(ENC_CLK);
  int step = 0;

  if (clk != lastEncoderClk) {
    unsigned long now = millis();

    if (clk == LOW && (now - lastEncoderMs) >= ENCODER_DEBOUNCE_MS) {
      lastEncoderMs = now;

      // On the falling CLK edge:
      // DT HIGH = one direction, DT LOW = the other.
      step = (digitalRead(ENC_DT) == HIGH) ? 1 : -1;

      if (ENCODER_REVERSED) {
        step = -step;
      }
    }

    lastEncoderClk = clk;
  }

  return step;
}

bool encoderButtonPressed() {
  bool reading = digitalRead(ENC_SW);

  if (reading != lastButtonReading) {
    lastButtonReading = reading;
    lastButtonChangeMs = millis();
  }

  if ((millis() - lastButtonChangeMs) >= BUTTON_DEBOUNCE_MS) {
    if (reading != stableButtonState) {
      stableButtonState = reading;

      if (stableButtonState == LOW) {
        return true;
      }
    }
  }

  return false;
}

// =====================================================
// LOCAL UI ACTIONS
// =====================================================
void handleLocalControls() {
  int step = readEncoderStep();

  if (step != 0) {
    if (uiScreen == UI_HOME) {
      requestedCurrentA += step * LOCAL_SET_STEP_A;

      requestedCurrentA =
        constrain(requestedCurrentA, 0.0, MAX_SET_CURRENT_A);

      // Round to exact 0.05 A steps.
      requestedCurrentA =
        round(requestedCurrentA / LOCAL_SET_STEP_A) * LOCAL_SET_STEP_A;

      if (requestedCurrentA <= 0.001) {
        requestedCurrentA = 0.0;
        setPWM(0);
      }
    }
    else if (uiScreen == UI_MENU) {
      int next = (int)menuIndex + step;

      if (next < 0) {
        next = MENU_COUNT - 1;
      }
      else if (next >= MENU_COUNT) {
        next = 0;
      }

      menuIndex = (uint8_t)next;
    }

    // Refresh LCD immediately after rotation.
    lastLcdMs = 0;
  }

  if (!encoderButtonPressed()) {
    return;
  }

  if (uiScreen == UI_HOME) {
    uiScreen = UI_MENU;
    menuIndex = 0;
  }

  else if (uiScreen == UI_MENU) {
    switch (menuIndex) {
      case 0:
        if (loadEnabled) {
          disableLoad();
        }
        else {
          enableLoad();
        }
        uiScreen = UI_HOME;
        break;

      case 1:
        uiScreen = UI_SHUNT_INFO;
        break;

      case 2:
        uiScreen = UI_CONTROL_INFO;
        break;

      case 3:
        clearTrip();
        uiScreen = UI_HOME;
        break;

      case 4:
      default:
        uiScreen = UI_HOME;
        break;
    }
  }

  else {
    // Any info screen: click returns to menu.
    uiScreen = UI_MENU;
  }

  lastLcdMs = 0;
}

// =====================================================
// LCD HELPERS
// =====================================================
void clearLcdRow(uint8_t row) {
  lcd.setCursor(0, row);
  lcd.print(F("                "));
  lcd.setCursor(0, row);
}

void drawHomeScreen() {
  clearLcdRow(0);
  lcd.print(F("DRAW "));
  lcd.print(measuredCurrentA, 2);
  lcd.print(F("A"));

  clearLcdRow(1);
  lcd.print(F("SET "));
  lcd.print(requestedCurrentA, 2);
  lcd.print(F("A "));

  if (safetyTrip) {
    lcd.print(F("TRIP"));
  }
  else if (loadEnabled) {
    lcd.print(F("RUN"));
  }
  else {
    lcd.print(F("OFF"));
  }
}

void drawMenuScreen() {
  clearLcdRow(0);
  lcd.print(F("MENU "));
  lcd.print(menuIndex + 1);
  lcd.print('/');
  lcd.print(MENU_COUNT);

  clearLcdRow(1);
  lcd.print('>');

  switch (menuIndex) {
    case 0:
      if (loadEnabled) {
        lcd.print(F("LOAD OFF"));
      }
      else {
        lcd.print(F("LOAD ON"));
      }
      break;

    case 1:
      lcd.print(F("SHUNT INFO"));
      break;

    case 2:
      lcd.print(F("CONTROL INFO"));
      break;

    case 3:
      lcd.print(F("CLEAR TRIP"));
      break;

    case 4:
    default:
      lcd.print(F("BACK"));
      break;
  }
}

void drawShuntInfoScreen() {
  clearLcdRow(0);
  lcd.print(F("SHUNT "));
  lcd.print(shuntVoltageV, 3);
  lcd.print(F("V"));

  clearLcdRow(1);
  lcd.print(F("CURR  "));
  lcd.print(measuredCurrentA, 2);
  lcd.print(F("A"));
}

void drawControlInfoScreen() {
  clearLcdRow(0);
  lcd.print(F("CTRL "));
  lcd.print(controlVoltageV, 3);
  lcd.print(F("V"));

  clearLcdRow(1);
  lcd.print(F("PWM "));
  lcd.print(pwmValue);
  lcd.print(F("/255"));
}

void updateLcd() {
  unsigned long now = millis();

  if (lastLcdMs != 0 && (now - lastLcdMs) < LCD_REFRESH_MS) {
    return;
  }

  lastLcdMs = now;

  switch (uiScreen) {
    case UI_HOME:
      drawHomeScreen();
      break;

    case UI_MENU:
      drawMenuScreen();
      break;

    case UI_SHUNT_INFO:
      drawShuntInfoScreen();
      break;

    case UI_CONTROL_INFO:
      drawControlInfoScreen();
      break;
  }
}

// =====================================================
// SETUP / LOOP
// =====================================================
void setup() {
  pinMode(PWM_PIN, OUTPUT);
  analogWrite(PWM_PIN, 0);

  pinMode(ENC_CLK, INPUT_PULLUP);
  pinMode(ENC_DT, INPUT_PULLUP);
  pinMode(ENC_SW, INPUT_PULLUP);

  lastEncoderClk = digitalRead(ENC_CLK);
  lastButtonReading = digitalRead(ENC_SW);
  stableButtonState = lastButtonReading;

  lcd.begin(16, 2);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("MAKLOAD5000"));
  lcd.setCursor(0, 1);
  lcd.print(F("Starting..."));

  analogReference(INTERNAL);

  Serial.begin(115200);

  delay(300);
  analogRead(CURRENT_PIN);

  loadCalibration();

  readCurrent();

  lastControlMs = millis();

  Serial.println(F("READY"));
  sendCalibration();

  uiScreen = UI_HOME;
  lastLcdMs = 0;
}

void loop() {
  readSerial();

  handleLocalControls();

  controlCurrent();

  updateLcd();

  sendTelemetry();
}
