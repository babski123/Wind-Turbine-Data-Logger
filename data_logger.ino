#include <LiquidCrystal_I2C.h>
#include <Wire.h>
#include <Arduino.h>
#include <DS3231.h>
#include <SPI.h>
#include <SD.h>
#include <HX711.h>
#include <EmonLib.h>

#define BTN_PIN 2                  // Button Pin
#define PIS 3                      // Photointerrupter Pin
#define PULSES_PER_REVOLUTION 1    // Number of blades
#define CS 4                       // SD card Chip select Pin
#define calibration_factor 1980.0  // HX711 Calibration Factor
#define DOUT 7                     // HX711 Data pin
#define CLK 6                      // HX711 Clock pin
#define SCT_PIN A1                 //SCT-013-30A pin

LiquidCrystal_I2C lcd(0x27, 16, 2);  // LCD config
DS3231 myRTC;
HX711 scale;
EnergyMonitor emon1;

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

void setup() {
  Serial.begin(9600);
  Wire.begin();
  lcdInit();
  rtcInit();
  sdCardInit();
  scaleInit();
  emonInit();

  pinMode(BTN_PIN, INPUT_PULLUP);                                    // Use internal pull-up for the button
  pinMode(PIS, INPUT_PULLUP);                                        // Assuming the photo interrupter outputs LOW when interrupted
  attachInterrupt(digitalPinToInterrupt(PIS), countPulse, FALLING);  // Trigger on falling edge (light blocked)
}

void loop() {
  readBtn();
  stateMachine();
  logData(readRPM(), readTorque(), readVoltage(), readCurrent(), readVoltage());
  delay(100);
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
        updateLcd("V:      I:     ", "P:       ");
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
      lcd.setCursor(2, 0);
      printValue(readVoltage());
      lcd.setCursor(10, 0);
      printValue(readCurrent());
      lcd.setCursor(2, 1);
      printValue(calculatePower(readVoltage(), readCurrent()));
      break;
    case 1:
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
      break;
    case 2:
      int currentTorque;
      currentTorque = readTorque();
      if (prevTorque != currentTorque) {
        lcd.setCursor(0, 1);
        lcd.print("      ");
        lcd.setCursor(0, 1);
        lcd.print(currentTorque);
      } else {
        lcd.setCursor(0, 1);
        lcd.print(currentTorque);
      }
      lcd.print(" Nm");
      prevTorque = currentTorque;
      break;
    case 3:
      lcd.setCursor(9, 0);
      printValue(readVoltage());
      lcd.print(" V");
      break;
    case 4:
      lcd.setCursor(9, 0);
      printValue(readCurrent());
      lcd.print(" A");
      break;
    case 5:
      lcd.setCursor(7, 0);
      printValue(calculatePower(readVoltage(), readCurrent()));
      lcd.print(" W");
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
  lcd.print(value, 2);  // Print with 2 decimal places
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
  return rpm;
}

int readTorque() {
  // Your code to read torque
  int t = scale.get_units();
  if (t < 0) {
    return 0;
  } else {
    return t;
  }
}

float readVoltage() {
  // Your code to read voltage
  return 12.34;
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

void logData(int rpm, int torque, float voltage, float current, float power) {
  // we log data only if isLogging = true
  if (isLogging) {
    unsigned long currentTime = millis();
    if (currentTime - lastLogTime >= logInterval) {
      lastLogTime = currentTime;

      if (!SD.exists("data.csv")) {
        File dataFile = SD.open("data.csv", FILE_WRITE);
        if (dataFile) {
          dataFile.println("Timestamp,RPM,Torque(Nm),Voltage(V),Current(A),Power(W)");
          dataFile.close();
          Serial.println("Headers written to data.csv");
        } else {
          Serial.println("Error creating/opening data.csv for headers");
          return;
        }
      }

      File dataFile = SD.open("data.csv", FILE_WRITE);
      if (dataFile) {
        dataFile.print(getDateAndTime(true));
        dataFile.print(" ");
        dataFile.print(getDateAndTime(false));
        dataFile.print(",");
        dataFile.print(rpm);
        dataFile.print(",");
        dataFile.print(torque);
        dataFile.print(",");
        dataFile.print(voltage);
        dataFile.print(",");
        dataFile.print(current);
        dataFile.print(",");
        dataFile.println(power);

        dataFile.close();
        Serial.println("Data logged to data.csv");
      } else {
        Serial.println("Error opening data.csv for writing");
      }
    }
  }
}