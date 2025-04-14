#include <LiquidCrystal_I2C.h>
#include <Wire.h>

#define PIS 3      // Photointerrupter Pin
#define BTN_PIN 2  // Button Pin

LiquidCrystal_I2C lcd(0x27, 16, 2);  // LCD config

/*
LCD States:
0 = Voltage, Current, Power
1 = RPM
2 = TORQUE
3 = Voltage Only
4 = Current Only
5 = Power Only
*/
int lcdState = 0;
int btnState = 0;
int lstBtnState = 0;
int prevLcdState = -1;  // Initialize with a value different from any valid state

void setup() {
  Serial.begin(9600);
  lcdInit();
  pinMode(BTN_PIN, INPUT_PULLUP);  // Use internal pull-up for the button
}

void loop() {
  readBtn();
  stateMachine();
  delay(50);
}

void lcdInit() {
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Wind Turbine");
  lcd.setCursor(0, 1);
  lcd.print("Data Logger");
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
      if (lcdState == 5) {
        lcdState = 0;
      } else {
        lcdState++;
      }
      Serial.println("on");
      Serial.print("LCD State: ");
      Serial.println(lcdState);
    } else {
      Serial.println("off");
    }
  }
  lstBtnState = btnState;
}

void stateMachine() {
  if (lcdState != prevLcdState) {
    switch (lcdState) {
      case 0:
        updateLcd("V:     I:     ", "P:       ");
        break;
      case 1:
        updateLcd("RPM:        ", "");
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
    }
    prevLcdState = lcdState;
  }

  // Update sensor values
  switch (lcdState) {
    case 0:
      lcd.setCursor(3, 0);
      printValue(readVoltage());
      lcd.setCursor(9, 0);
      printValue(readCurrent());
      lcd.setCursor(3, 1);
      printValue(calculatePower(readVoltage(), readCurrent()));
      break;
    case 1:
      lcd.setCursor(5, 0);
      printValue(readRPM());
      lcd.print(" RPM");
      break;
    case 2:
      lcd.setCursor(8, 0);
      printValue(readTorque());
      lcd.print(" Nm");  // Example unit
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

int readRPM() {
  // Your code to read RPM
  return 1234;
}

float readTorque() {
  // Your code to read torque
  return 5.67;
}

float readVoltage() {
  // Your code to read voltage
  return 12.34;
}

float readCurrent() {
  // Your code to read current
  return 2.10;
}