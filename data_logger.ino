#include <LiquidCrystal_I2C.h>
#include <Wire.h>
#include <Arduino.h>
#include <DS3231.h>
#include <SPI.h>
#include <SD.h>
#include <HX711.h>
#include <EmonLib.h>
#include <ZMPT101B.h>

#define BTN_PIN 2                  // Button Pin
#define PIS 3                      // Photointerrupter Pin
#define PULSES_PER_REVOLUTION 1    // Number of blades
#define CS 4                       // SD card Chip select Pin
#define calibration_factor 1980.0  // HX711 Calibration Factor
#define DOUT 7                     // HX711 Data pin
#define CLK 6                      // HX711 Clock pin
#define SCT_PIN A1                 //SCT-013-30A pin
#define SENSITIVITY 1137.5f        //ZMPT101B SENSITIVITY
#define VOLT_SENSOR_PIN A0         // ZMPT101B PIN

LiquidCrystal_I2C lcd(0x27, 16, 2);  // LCD config
DS3231 myRTC;
HX711 scale;
EnergyMonitor emon1;
ZMPT101B voltageSensor(VOLT_SENSOR_PIN, 60.0);  // 60hz - change as needed

unsigned long lastLogTime = 0;
const unsigned long logInterval = 5000;  // Log data every 5000 milliseconds (5 seconds)
int dataLogCountdown = 11;               // the countdown before logging starts
unsigned long countdownTimer = 0;        // the timer for countdown
bool isLogging = false;                  // flag if logger is active

// 12 hour mode
bool CenturyBit;
bool h12 = true;
bool hPM;

/*
LCD States:
0 = Voltage, Current, Power
1 = RPM
2 = TORQUE
3 = Voltage Only
4 = Current Only
5 = Power Only
*/
int lcdState = -1;
int btnState = 0;
int lstBtnState = 0;
int prevLcdState = -1;  // Initialize with a value different from any valid state

volatile unsigned long pulseCount = 0;
volatile unsigned long lastPulseTime = 0;
unsigned int rpm = 0;
int prevTorque = 999;
int prevRpm = -1;
const float armLength = 90.0;  // Arm length in millimeters to measure torque

unsigned long lastSensorsUpdate = 0;

void setup() {
  Serial.begin(9600);
  Wire.begin();
  lcdInit();
  rtcInit();
  sdCardInit();
  scaleInit();
  emonInit();
  voltSensorInit();

  pinMode(BTN_PIN, INPUT_PULLUP);                                    // Use internal pull-up for the button
  pinMode(PIS, INPUT_PULLUP);                                        // Assuming the photo interrupter outputs LOW when interrupted
  attachInterrupt(digitalPinToInterrupt(PIS), countPulse, FALLING);  // Trigger on falling edge (light blocked)
}

void loop() {
  readBtn();
  stateMachine();
  logData();
  delay(100);
}

void voltSensorInit() {
  voltageSensor.setSensitivity(SENSITIVITY);
}

void emonInit() {
  emon1.current(SCT_PIN, 85.1);
}

void scaleInit() {
  scale.begin(DOUT, CLK);
  scale.set_scale(calibration_factor);
  scale.tare();
}

void sdCardInit() {
  SD.begin(CS);
}

void rtcInit() {
  myRTC.setClockMode(h12);
  /*
  // default secs
  byte theSecs = 0;
  // default mins
  byte theMins = 21;
  // default hr
  byte theHr = 23;
  // Day of the Week
  byte theDoW = 1;
  // day of the month
  byte theDate = 14;
  // month
  byte theMonth = 4;
  // year
  byte theYear = 25;
  myRTC.setSecond(theSecs);
  myRTC.setMinute(theMins);
  myRTC.setHour(theHr);
  myRTC.setDoW(theDoW);
  myRTC.setDate(theDate);
  myRTC.setMonth(theMonth);
  myRTC.setYear(theYear);
  */
}

void lcdInit() {
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Data Logger");
  lcd.setCursor(0, 1);
  lcd.print("Press Button ...");
  prevLcdState = lcdState;  // Initialize prevLcdState after initial display
}

void updateLcd(String firstRow, String secondRow) {
  lcd.clear();

  // Truncate if necessary
  if (firstRow.length() > 16) {
    firstRow = firstRow.substring(0, 16);
  }
  if (secondRow.length() > 16) {
    secondRow = secondRow.substring(0, 16);
  }

  delay(100);
  lcd.setCursor(0, 0);
  lcd.print(firstRow);
  lcd.setCursor(0, 1);
  lcd.print(secondRow);
}

void readBtn() {
  btnState = digitalRead(BTN_PIN);

  if (btnState != lstBtnState) {
    if (btnState == LOW) {  // Button press is usually LOW with pull-up
      if (lcdState == 7) {
        lcdState = 0;
        isLogging = false;      // stop logging
        dataLogCountdown = 11;  // reset dataLogger countdown
      } else {
        lcdState++;
      }
    }
  }
  lstBtnState = btnState;
}

void stateMachine() {
  if (lcdState != prevLcdState) {
    switch (lcdState) {
      case 0:
        updateLcd("", "");
        break;
      case 1:
        updateLcd("RPM:        ", "     ");
        break;
      case 2:
        updateLcd("TORQUE:       ", "");
        break;
      case 3:
        updateLcd("Voltage:      ", "");
        break;
      case 4:
        updateLcd("Current:      ", "");
        break;
      case 5:
        updateLcd("Power:        ", "");
        break;
      case 6:
        updateLcd("", "");
        break;
      case 7:
        updateLcd("Logging Data in", "10s ...");
        break;
    }
    prevLcdState = lcdState;
  }

  // Update sensor values
  switch (lcdState) {
    case 0:
      if (millis() - lastSensorsUpdate > 1000) {
        lcd.setCursor(0, 0);
        printValueWithUnit(readVoltage(), "V");
        lcd.setCursor(8, 0);
        printValueWithUnit(readCurrent(), "A");
        lcd.setCursor(0, 1);
        printValueWithUnit(calculatePower(readVoltage(), readCurrent()), "W");
        lastSensorsUpdate = millis();
      }
      break;
    case 1:
      if (millis() - lastSensorsUpdate > 1000) {
        int currentRpm;
        currentRpm = readRPM();
        if (prevRpm != currentRpm) {
          lcd.setCursor(0, 1);
          lcd.print("      ");
          lcd.setCursor(0, 1);
          lcd.print(readRPM());
        } else {
          lcd.setCursor(0, 1);
          lcd.print(readRPM());
        }
        prevRpm = currentRpm;
        lastSensorsUpdate = millis();
      }
      break;
    case 2:
      int currentTorque;
      currentTorque = readTorque();
      if (prevTorque != currentTorque) {
        lcd.setCursor(0, 1);
        lcd.print("       ");
        lcd.setCursor(0, 1);
        lcd.print(currentTorque);
      } else {
        lcd.setCursor(0, 1);
        lcd.print(currentTorque);
      }
      lcd.print(" Nmm");
      prevTorque = currentTorque;
      break;
    case 3:
      if (millis() - lastSensorsUpdate > 1000) {
        lcd.setCursor(0, 1);
        printValueWithUnit(readVoltage(), "V");
        lastSensorsUpdate = millis();
      }
      break;
    case 4:
      if (millis() - lastSensorsUpdate > 1000) {
        lcd.setCursor(0, 1);
        printValueWithUnit(readCurrent(), "A");
        lastSensorsUpdate = millis();
      }
      break;
    case 5:
      if (millis() - lastSensorsUpdate > 1000) {
        lcd.setCursor(0, 1);
        printValueWithUnit(calculatePower(readVoltage(), readCurrent()), "W");
        lastSensorsUpdate = millis();
      }
      break;
    case 6:
      lcd.setCursor(0, 0);
      lcd.print(getDateAndTime(true));
      lcd.setCursor(0, 1);
      lcd.print(getDateAndTime(false));
      break;
    case 7:
      if (millis() - countdownTimer >= 1000 && dataLogCountdown > 0) {
        countdownTimer = millis();
        dataLogCountdown--;
        lcd.setCursor(0, 1);
        lcd.print("  ");
        lcd.setCursor(0, 1);
        lcd.print(dataLogCountdown);
      }
      if (dataLogCountdown <= 0 && !isLogging) {
        isLogging = true;  // start logging after countdown
        updateLcd("Data Logger", "Is Running");
      }
      break;
  }
}

// Helper function to print values (can be customized for formatting)
void printValue(float value) {
  char buffer[7];                // 6 characters + null terminator
  dtostrf(value, 6, 2, buffer);  // width=6, 2 decimal places
  lcd.print(buffer);
}

void printValueWithUnit(float value, const char* unit) {
  char buffer[10];
  dtostrf(value, 6, 2, buffer);  // width=6, precision=2
  lcd.print(buffer);
  lcd.print(unit);
}




// Helper function for power calculation (you might have a direct power sensor)
float calculatePower(float v, float i) {
  return v * i;
}

void countPulse() {
  pulseCount++;
  lastPulseTime = micros();  // Record the time of the last pulse
}

int readRPM() {
  static unsigned long lastTime = 0;
  static unsigned long previousPulseCount = 0;

  unsigned long currentTime = millis();
  unsigned long timeInterval = currentTime - lastTime;

  // Debounce and ensure a reasonable time has passed since the last calculation
  if (timeInterval >= 100) {  // Update RPM every 100 milliseconds (adjust as needed)
    cli();                    // Disable interrupts temporarily for atomic read
    unsigned long currentPulseCount = pulseCount;
    sei();  // Re-enable interrupts

    unsigned long pulseDifference = currentPulseCount - previousPulseCount;

    // Calculate RPM:
    // (Number of pulses in the interval / Time interval in seconds) * 60 seconds/minute
    rpm = (pulseDifference * 1000UL * 60UL) / (timeInterval * PULSES_PER_REVOLUTION);

    previousPulseCount = currentPulseCount;
    lastTime = currentTime;
  }
  return rpm / 3;
}

int readTorque() {
  // Read force in grams from the load cell
  float grams = scale.get_units();

  // Ignore positive values
  if (grams > 0) {
    return 0;
  }

  // Constants
  const float g = 9.81;   // Acceleration due to gravity (m/s^2)
  const float armLength = 50.0; // Example arm length in mm - make sure this is your actual value

  // Convert grams to kg
  float mass_kg = grams / 1000.0;

  // Calculate force in Newtons (will be negative since grams is negative)
  float force_N = mass_kg * g;

  // Torque in N·mm = Force (N) × Arm length (mm) (will be negative)
  float torque_Nmm = force_N * armLength;

  // Return the absolute value as an integer
  return (int)abs(torque_Nmm);
}


float readVoltage() {
  // Your code to read voltage
  return voltageSensor.getRmsVoltage();
}

float readCurrent() {
  // Your code to read current
  return emon1.calcIrms(1480);
  //return 0.15;
}

String getDateAndTime(bool returnDate) {
  if (returnDate) {
    String dateString = "";
    dateString += "20";
    dateString += myRTC.getYear();
    dateString += "-";
    if (myRTC.getMonth(CenturyBit) < 10) dateString += "0";
    dateString += myRTC.getMonth(CenturyBit);
    dateString += "-";
    if (myRTC.getDate() < 10) dateString += "0";
    dateString += myRTC.getDate();
    return dateString;
  } else {
    String timeString = "";
    int hour = myRTC.getHour(h12, hPM);
    int minute = myRTC.getMinute();
    int second = myRTC.getSecond();

    if (hour < 10) timeString += "0";
    timeString += hour;
    timeString += ":";
    if (minute < 10) timeString += "0";
    timeString += minute;
    timeString += ":";
    if (second < 10) timeString += "0";
    timeString += second;

    if (h12 == true) {  // 12-hour mode
      if (hPM == true) {
        timeString += " PM";
      } else {
        timeString += " AM";
      }
    }
    return timeString;
  }
}

void logData() {
  int _rpm;
  int _torque;
  float _voltage;
  float _current;
  float _power;
  // we log data only if isLogging = true
  if (isLogging) {
    _rpm = readRPM();
    _torque = readTorque();
    _voltage = readVoltage();
    _current = readCurrent();
    _power = _voltage * _current;

    unsigned long currentTime = millis();
    if (currentTime - lastLogTime >= logInterval) {
      lastLogTime = currentTime;

      if (!SD.exists("data.csv")) {
        File dataFile = SD.open("data.csv", FILE_WRITE);
        if (dataFile) {
          dataFile.println("Timestamp,RPM,Torque(Nmm),Voltage(V),Current(A),Power(W)");
          dataFile.close();
        } else {
          updateLcd("SD Card Error", "Please Restart");
          return;
        }
      }

      File dataFile = SD.open("data.csv", FILE_WRITE);
      if (dataFile) {
        dataFile.print(getDateAndTime(true));
        dataFile.print(" ");
        dataFile.print(getDateAndTime(false));
        dataFile.print(",");
        dataFile.print(_rpm);
        dataFile.print(",");
        dataFile.print(_torque);
        dataFile.print(",");
        dataFile.print(_voltage);
        dataFile.print(",");
        dataFile.print(_current);
        dataFile.print(",");
        dataFile.println(_power);

        dataFile.close();
      } else {
        updateLcd("SD Card Error", "Please Restart");
      }
    }
  }
}