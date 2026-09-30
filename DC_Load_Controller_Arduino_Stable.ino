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

const uint8_t CURRENT_PIN = A0;
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
}

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
      constrain(amps, 0.0, MAX_SET_CURRENT_A);

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
// SETUP / LOOP
// =====================================================
void setup() {
  pinMode(PWM_PIN, OUTPUT);
  analogWrite(PWM_PIN, 0);

  analogReference(INTERNAL);

  Serial.begin(115200);

  delay(100);
  analogRead(CURRENT_PIN);

  loadCalibration();

  lastControlMs = millis();

  Serial.println(F("READY"));
  sendCalibration();
}

void loop() {
  readSerial();
  controlCurrent();
  sendTelemetry();
}
