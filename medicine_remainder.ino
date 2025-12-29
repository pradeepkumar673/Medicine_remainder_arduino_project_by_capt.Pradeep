#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <RTClib.h>
LiquidCrystal_I2C lcd(0x27, 16, 2);
RTC_DS3231 rtc;
#define IR_BOX_A 2
#define IR_BOX_B 4
#define IR_BOX_C 6
#define LED_BOX_A 3
#define LED_BOX_B 5
#define LED_BOX_C 7
#define BUZZER 8
#define BTN_MENU 9
#define BTN_SELECT 10
String medicineNames[] = {"Paracetamol", "Amoxicillin", "Metformin"};
int scheduleTimes[][3] = {
  {8, 14, 20},   // Box A: 8AM, 2PM, 8PM
  {9, 21, 0},    // Box B: 9AM, 9PM, 0 = no dose
  {8, 13, 19}    // Box C: 8AM, 1PM, 7PM
};
bool boxEmpty[] = {false, false, false};
bool boxAlert[] = {false, false, false};
bool scheduleAlert[] = {false, false, false};
int lastMinute = -1;
unsigned long lastBuzzerTime = 0;
bool buzzerState = false;
int displayMode = 0;  
bool alertActive = false;
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50;

void setup() {
  Serial.begin(9600);
  pinMode(IR_BOX_A, INPUT);
  pinMode(IR_BOX_B, INPUT);
  pinMode(IR_BOX_C, INPUT);
  pinMode(LED_BOX_A, OUTPUT);
  pinMode(LED_BOX_B, OUTPUT);
  pinMode(LED_BOX_C, OUTPUT);
  pinMode(BUZZER, OUTPUT);
  pinMode(BTN_MENU, INPUT_PULLUP);
  pinMode(BTN_SELECT, INPUT_PULLUP);
  lcd.init();
  lcd.backlight();
  lcd.clear();
  if (!rtc.begin()) {
    lcd.print("RTC Error!");
    while (1);
  }
  
  if (rtc.lostPower()) {
    lcd.print("RTC Setting...");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    delay(1000);
  }
  
  displayWelcome();
}

void loop() {
  DateTime now = rtc.now();
  
  checkBoxStatus();
  
  if (now.minute() != lastMinute) {
    lastMinute = now.minute();
    checkMedicineSchedule(now);
  }
  handleAlerts();
  updateDisplay(now);
  handleButtons();
  
  delay(100);
}

void checkBoxStatus() {
  bool statusA = digitalRead(IR_BOX_A) == HIGH;
  bool statusB = digitalRead(IR_BOX_B) == HIGH;
  bool statusC = digitalRead(IR_BOX_C) == HIGH;
  if (statusA && !boxEmpty[0]) {
    boxEmpty[0] = true;
    boxAlert[0] = true;
    logEvent("Box A EMPTY: Paracetamol");
  }
  
  if (statusB && !boxEmpty[1]) {
    boxEmpty[1] = true;
    boxAlert[1] = true;
    logEvent("Box B EMPTY: Amoxicillin");
  }
  
  if (statusC && !boxEmpty[2]) {
    boxEmpty[2] = true;
    boxAlert[2] = true;
    logEvent("Box C EMPTY: Metformin");
  }
  digitalWrite(LED_BOX_A, boxAlert[0] ? HIGH : LOW);
  digitalWrite(LED_BOX_B, boxAlert[1] ? HIGH : LOW);
  digitalWrite(LED_BOX_C, boxAlert[2] ? HIGH : LOW);
}

void checkMedicineSchedule(DateTime now) {
  int currentHour = now.hour();
  int currentMinute = now.minute();
  for (int box = 0; box < 3; box++) {
    bool scheduleTriggered = false;
    for (int dose = 0; dose < 3; dose++) {
      int scheduledHour = scheduleTimes[box][dose];
      if (scheduledHour == 0 && dose == 2) continue;
      if (currentHour == scheduledHour && currentMinute >= 0 && currentMinute <= 5) {
        scheduleTriggered = true;
        break;
      }
    }
    if (scheduleTriggered && !scheduleAlert[box]) {
      scheduleAlert[box] = true;
      String message = "Time for " + medicineNames[box];
      logEvent(message);
    }
  }
}

void handleAlerts() {
  bool anyAlert = false;
  
  for (int i = 0; i < 3; i++) {
    if (boxAlert[i] || scheduleAlert[i]) {
      anyAlert = true;
      break;
    }
  }
  if (anyAlert) {
    unsigned long currentMillis = millis();
    if (currentMillis - lastBuzzerTime >= 1000) {
      buzzerState = !buzzerState;
      digitalWrite(BUZZER, buzzerState ? HIGH : LOW);
      lastBuzzerTime = currentMillis;
      alertActive = true;
    }
  } else {
    digitalWrite(BUZZER, LOW);
    alertActive = false;
  }
}

void updateDisplay(DateTime now) {
  static int lastDisplayUpdate = -1;
  if (millis() - lastDisplayUpdate > 1000 || lastDisplayUpdate == -1) {
    lastDisplayUpdate = millis();
    lcd.clear();
    for (int i = 0; i < 3; i++) {
      if (boxAlert[i]) {
        displayBoxEmptyAlert(i);
        return;
      }
    }
    for (int i = 0; i < 3; i++) {
      if (scheduleAlert[i]) {
        displayScheduleAlert(i);
        return;
      }
    }
    
    switch (displayMode) {
      case 0: // Current time
        displayCurrentTime(now);
        break;
      case 1: // Medicine schedule
        displayNextSchedule(now);
        break;
      case 2: // Box status
        displayBoxStatus();
        break;
    }
  }
}

void displayBoxEmptyAlert(int boxIndex) {
  lcd.setCursor(0, 0);
  lcd.print("! ALERT ! Box ");
  lcd.print(char('A' + boxIndex));
  lcd.print(" Empty");
  
  lcd.setCursor(0, 1);
  lcd.print("Refill: ");
  lcd.print(medicineNames[boxIndex]);
}

void displayScheduleAlert(int boxIndex) {
  lcd.setCursor(0, 0);
  lcd.print("Medicine Time!");
  
  lcd.setCursor(0, 1);
  lcd.print("Take ");
  lcd.print(medicineNames[boxIndex]);
  lcd.print("    ");
}

void displayCurrentTime(DateTime now) {
  lcd.setCursor(0, 0);
  lcd.print("Time: ");
  lcd.print(now.hour() < 10 ? "0" : "");
  lcd.print(now.hour());
  lcd.print(":");
  lcd.print(now.minute() < 10 ? "0" : "");
  lcd.print(now.minute());
  lcd.print(":");
  lcd.print(now.second() < 10 ? "0" : "");
  lcd.print(now.second());
  
  lcd.setCursor(0, 1);
  lcd.print("Date: ");
  lcd.print(now.day());
  lcd.print("/");
  lcd.print(now.month());
  lcd.print("/");
  lcd.print(now.year() % 100);
}

void displayNextSchedule(DateTime now) {
  lcd.setCursor(0, 0);
  lcd.print("Next Medicine:");
  
  // Find next schedule
  int nextHour = 24;
  int nextBox = -1;
  
  for (int box = 0; box < 3; box++) {
    for (int dose = 0; dose < 3; dose++) {
      int scheduledHour = scheduleTimes[box][dose];
      if (scheduledHour > now.hour() && scheduledHour < nextHour) {
        nextHour = scheduledHour;
        nextBox = box;
      }
    }
  }
  
  lcd.setCursor(0, 1);
  if (nextBox != -1) {
    lcd.print(char('A' + nextBox));
    lcd.print(": ");
    lcd.print(medicineNames[nextBox]);
    lcd.print(" @ ");
    lcd.print(nextHour);
    lcd.print(":00");
  } else {
    lcd.print("No more today");
  }
}

void displayBoxStatus() {
  lcd.setCursor(0, 0);
  lcd.print("Box Status:");
  
  lcd.setCursor(0, 1);
  for (int i = 0; i < 3; i++) {
    lcd.print(boxEmpty[i] ? "E" : "F");
    if (i < 2) lcd.print(" ");
  }
  lcd.print(" A:");
  lcd.print(medicineNames[0].substring(0, 3));
  lcd.print(" ");
}

void handleButtons() {
  static int lastButtonStateMenu = HIGH;
  static int lastButtonStateSelect = HIGH;
  int currentButtonStateMenu = digitalRead(BTN_MENU);
  int currentButtonStateSelect = digitalRead(BTN_SELECT);
  if (currentButtonStateMenu == LOW && lastButtonStateMenu == HIGH) {
    delay(50); // Debounce
    if (digitalRead(BTN_MENU) == LOW) {
      displayMode = (displayMode + 1) % 3;
      lcd.clear();
    }
  }
  lastButtonStateMenu = currentButtonStateMenu;
  
  if (currentButtonStateSelect == LOW && lastButtonStateSelect == HIGH) {
    delay(50); // Debounce
    if (digitalRead(BTN_SELECT) == LOW) {
      acknowledgeAlerts();
    }
  }
  lastButtonStateSelect = currentButtonStateSelect;
}

void acknowledgeAlerts() {
  for (int i = 0; i < 3; i++) {
    boxAlert[i] = false;
    scheduleAlert[i] = false;
    digitalWrite(LED_BOX_A + i, LOW);
  }
  
  digitalWrite(BUZZER, LOW);
  alertActive = false;
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Alert Acknowledged");
  delay(2000);
}

void displayWelcome() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Medicine Reminder");
  lcd.setCursor(0, 1);
  lcd.print("System v1.0");
  delay(2000);
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Box A: Paracetamol");
  lcd.setCursor(0, 1);
  lcd.print("Box B: Amoxicillin");
  delay(2000);
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Box C: Metformin");
  lcd.setCursor(0, 1);
  lcd.print("System Ready!");
  delay(2000);
}

void logEvent(String message) {
  DateTime now = rtc.now();
  Serial.print("[");
  Serial.print(now.hour());
  Serial.print(":");
  Serial.print(now.minute());
  Serial.print(":");
  Serial.print(now.second());
  Serial.print("] ");
  Serial.println(message);
}

// RTC timer one time run pandrathuku (run once)
void setRTCTime() {
  // incase time thapa irundichina, uncomment the below one 
  // rtc.adjust(DateTime(2025, 12, 15, 10, 30, 0));
}