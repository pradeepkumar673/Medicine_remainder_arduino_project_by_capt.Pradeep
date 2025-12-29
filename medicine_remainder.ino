/*
 * SMART MEDICINE REMINDER SYSTEM - ENHANCED VERSION
 * 
 * Features:
 * - Shows "BOX REFILLED" message when empty box is filled again
 * - Clear visual feedback for refills
 * - Buzzer stops immediately on refill
 * - Shows "Medicine Time Soon" when within 30 minutes of scheduled dose
 * - Improved display notifications with alternating display
 * - Enhanced user experience
 */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <RTClib.h>

// ===================== HARDWARE CONSTANTS =====================
// Pin definitions
const uint8_t PIN_IR_BOX_A     = 2;
const uint8_t PIN_IR_BOX_B     = 4;
const uint8_t PIN_IR_BOX_C     = 6;
const uint8_t PIN_LED_BOX_A    = 3;
const uint8_t PIN_LED_BOX_B    = 5;
const uint8_t PIN_LED_BOX_C    = 7;
const uint8_t PIN_BUZZER       = 8;
const uint8_t PIN_BTN_MENU     = 9;
const uint8_t PIN_BTN_SELECT   = 10;

// Timing constants
const uint16_t DEBOUNCE_DELAY_MS = 50;
const uint16_t LONG_PRESS_MS = 1000;
const uint16_t LED_BLINK_INTERVAL_MS = 500;
const uint16_t BUZZER_ON_TIME_MS = 500;      // Buzzer ON time for continuous beep
const uint16_t BUZZER_OFF_TIME_MS = 500;     // Buzzer OFF time for continuous beep
const uint16_t ALERT_REFRESH_MS = 1000;
const uint8_t SCHEDULE_WINDOW_MINUTES = 5;
const uint8_t REMINDER_WINDOW_MINUTES = 30;  // Show "Medicine Soon" 30 minutes before
const uint16_t REFILL_DISPLAY_TIME_MS = 3000; // Show refill message for 3 seconds
const uint16_t REMINDER_DISPLAY_CYCLE_MS = 5000; // Cycle between reminder and normal display every 5 seconds

// Display modes
enum DisplayMode {
  DISPLAY_MODE_TIME,
  DISPLAY_MODE_SCHEDULE,
  DISPLAY_MODE_STATUS,
  DISPLAY_MODE_COUNT
};

// Alert types
enum AlertType {
  ALERT_NONE,
  ALERT_EMPTY_BOX,
  ALERT_MEDICINE_TIME,
  ALERT_MEDICINE_SOON,
  ALERT_BOX_REFILLED  // NEW: For refill notifications
};

// ===================== DATA STRUCTURES =====================
struct MedicineBox {
  const char* name;
  const uint8_t scheduleTimes[3];  // Up to 3 doses per day (0 = no dose)
  uint8_t irPin;
  uint8_t ledPin;
  
  // State variables
  bool isEmpty;
  bool isAlertActive;      // For empty box alerts
  bool isScheduleAlert;    // For medicine time alerts (within schedule window)
  bool isReminderActive;   // For medicine soon alerts (30 minutes before)
  bool wasScheduleTriggeredToday[3];
  
  // NEW: Refill detection
  bool wasJustRefilled;
  unsigned long refillDisplayStartTime;
  
  // LED blinking control
  bool ledState;
  unsigned long lastBlinkTime;
  
  // For reminder display
  uint8_t nextDoseHour;
  uint8_t nextDoseMinute;
};

// Initialize LCD
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Initialize RTC
RTC_DS3231 rtc;

// ===================== GLOBAL VARIABLES =====================
MedicineBox boxes[3] = {
  {
    "Paracetamol",
    {8, 14, 20},  // 8AM, 2PM, 8PM
    PIN_IR_BOX_A,
    PIN_LED_BOX_A,
    false, false, false, false, {false, false, false},
    false, 0,  // wasJustRefilled, refillDisplayStartTime
    false, 0,
    0, 0
  },
  {
    "Amoxicillin", 
    {9, 21, 0},   // 9AM, 9PM, no third dose
    PIN_IR_BOX_B,
    PIN_LED_BOX_B,
    false, false, false, false, {false, false, false},
    false, 0,
    false, 0,
    0, 0
  },
  {
    "Metformin",
    {8, 13, 19},  // 8AM, 1PM, 7PM
    PIN_IR_BOX_C,
    PIN_LED_BOX_C,
    false, false, false, false, {false, false, false},
    false, 0,
    false, 0,
    0, 0
  }
};

// System state
DisplayMode currentDisplayMode = DISPLAY_MODE_TIME;
uint8_t lastCheckedMinute = 255;
uint8_t lastCheckedDay = 255;
bool isAnyAlertActive = false;
bool isBuzzerMuted = false;  // Flag to track if buzzer is manually muted
unsigned long lastDisplayUpdate = 0;
unsigned long lastBuzzerToggle = 0;
bool buzzerState = false;
bool isShowingRefillMessage = false;  // NEW: Track if showing refill message
unsigned long refillMessageEndTime = 0;  // NEW: When to stop showing refill message

// NEW: For alternating reminder display
bool showReminderScreen = false;  // Toggles between reminder and normal screen
unsigned long lastDisplayToggleTime = 0;

// Button handling
struct ButtonState {
  uint8_t pin;
  bool lastState;
  bool currentState;
  unsigned long lastDebounceTime;
  unsigned long pressStartTime;
  bool isPressed;
  bool wasLongPress;
};

ButtonState btnMenu = {PIN_BTN_MENU, HIGH, HIGH, 0, 0, false, false};
ButtonState btnSelect = {PIN_BTN_SELECT, HIGH, HIGH, 0, 0, false, false};

// ===================== SETUP FUNCTION =====================
void setup() {
  Serial.begin(9600);
  Serial.println(F("Enhanced Medicine Reminder - Starting..."));
  
  initializePins();
  initializeLCD();
  initializeRTC();
  
  displayWelcomeSequence();
  Serial.println(F("System initialization complete."));
}

void initializePins() {
  pinMode(PIN_IR_BOX_A, INPUT);
  pinMode(PIN_IR_BOX_B, INPUT);
  pinMode(PIN_IR_BOX_C, INPUT);
  pinMode(PIN_BTN_MENU, INPUT_PULLUP);
  pinMode(PIN_BTN_SELECT, INPUT_PULLUP);
  
  pinMode(PIN_LED_BOX_A, OUTPUT);
  pinMode(PIN_LED_BOX_B, OUTPUT);
  pinMode(PIN_LED_BOX_C, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  
  digitalWrite(PIN_LED_BOX_A, LOW);
  digitalWrite(PIN_LED_BOX_B, LOW);
  digitalWrite(PIN_LED_BOX_C, LOW);
  digitalWrite(PIN_BUZZER, LOW);
}

void initializeLCD() {
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("Initializing..."));
}

void initializeRTC() {
  if (!rtc.begin()) {
    lcd.clear();
    lcd.print(F("RTC Error!"));
    Serial.println(F("ERROR: RTC initialization failed"));
    while (true);
  }
  
  if (rtc.lostPower()) {
    lcd.clear();
    lcd.print(F("Setting RTC..."));
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    delay(1000);
    Serial.println(F("RTC was reset to compile time"));
  }
}

// ===================== MAIN LOOP =====================
void loop() {
  DateTime now = rtc.now();
  
  updateButton(&btnMenu);
  updateButton(&btnSelect);
  
  handleButtonActions(now);
  
  checkForDailyReset(now);
  
  checkBoxStatus();  // Now detects both empty and refilled states
  checkMedicineSchedule(now);
  checkMedicineReminders(now);
  
  handleAlerts();
  handleBuzzer();
  
  updateDisplay(now);
  
  delay(50); // Small delay for stability
}

// ===================== BUTTON HANDLING =====================
void updateButton(ButtonState* btn) {
  bool reading = digitalRead(btn->pin);
  
  if (reading != btn->lastState) {
    btn->lastDebounceTime = millis();
  }
  
  if ((millis() - btn->lastDebounceTime) > DEBOUNCE_DELAY_MS) {
    if (reading != btn->currentState) {
      btn->currentState = reading;
      
      if (btn->currentState == LOW) {
        btn->isPressed = true;
        btn->pressStartTime = millis();
        btn->wasLongPress = false;
      } else {
        btn->isPressed = false;
      }
    }
  }
  
  btn->lastState = reading;
  
  if (btn->isPressed && !btn->wasLongPress) {
    if ((millis() - btn->pressStartTime) > LONG_PRESS_MS) {
      btn->wasLongPress = true;
    }
  }
}

void handleButtonActions(const DateTime& now) {
  // Menu button - short press cycles display modes
  if (!btnMenu.isPressed && btnMenu.currentState == HIGH && 
      btnMenu.lastState == LOW && !btnMenu.wasLongPress) {
    currentDisplayMode = static_cast<DisplayMode>((currentDisplayMode + 1) % DISPLAY_MODE_COUNT);
    forceDisplayUpdate();
    Serial.print(F("Display mode changed to: "));
    Serial.println(currentDisplayMode);
  }
  
  // Select button handling
  if (!btnSelect.isPressed && btnSelect.currentState == HIGH && 
      btnSelect.lastState == LOW) {
    
    if (btnSelect.wasLongPress) {
      handleLongPressReset();
    } else {
      acknowledgeAllAlerts();
    }
  }
}

void handleLongPressReset() {
  bool anyReset = false;
  
  for (uint8_t i = 0; i < 3; i++) {
    if (boxes[i].isAlertActive && !digitalRead(boxes[i].irPin)) {
      boxes[i].isAlertActive = false;
      boxes[i].isEmpty = false;
      boxes[i].wasJustRefilled = true;
      boxes[i].refillDisplayStartTime = millis();
      digitalWrite(boxes[i].ledPin, LOW);
      anyReset = true;
      
      Serial.print(F("Box "));
      Serial.print(static_cast<char>('A' + i));
      Serial.println(F(" alert reset (refilled)"));
    }
  }
  
  if (anyReset) {
    isShowingRefillMessage = true;
    refillMessageEndTime = millis() + REFILL_DISPLAY_TIME_MS;
    
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("Refill detected"));
    lcd.setCursor(0, 1);
    lcd.print(F("Alerts cleared"));
    delay(1500);
    forceDisplayUpdate();
  }
}

void acknowledgeAllAlerts() {
  bool hadAlerts = false;
  
  // Stop buzzer by setting mute flag
  isBuzzerMuted = true;
  digitalWrite(PIN_BUZZER, LOW);
  
  // Clear schedule alerts and reminders
  for (uint8_t i = 0; i < 3; i++) {
    if (boxes[i].isScheduleAlert || boxes[i].isReminderActive) {
      hadAlerts = true;
      boxes[i].isScheduleAlert = false;
      boxes[i].isReminderActive = false;
    }
    // Don't clear empty box alerts - they need refill or long press
  }
  
  // Reset reminder display toggle
  showReminderScreen = false;
  
  if (hadAlerts) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("Alerts silenced"));
    lcd.setCursor(0, 1);
    lcd.print(F("Press menu"));
    delay(1500);
    forceDisplayUpdate();
    Serial.println(F("All alerts acknowledged - Buzzer muted"));
  }
}

// ===================== MEDICINE LOGIC =====================
void checkBoxStatus() {
  for (uint8_t i = 0; i < 3; i++) {
    bool sensorReading = (digitalRead(boxes[i].irPin) == HIGH);
    
    // Detect when box becomes empty
    if (sensorReading && !boxes[i].isEmpty) {
      boxes[i].isEmpty = true;
      boxes[i].isAlertActive = true;
      boxes[i].wasJustRefilled = false; // Reset refill flag
      
      // Reset buzzer mute when new empty alert occurs
      isBuzzerMuted = false;
      
      Serial.print(F("ALERT: Box "));
      Serial.print(static_cast<char>('A' + i));
      Serial.print(F(" ("));
      Serial.print(boxes[i].name);
      Serial.println(F(") is EMPTY!"));
      
      forceDisplayUpdate();
    }
    
    // NEW: Detect when box is refilled (sensor goes LOW)
    if (!sensorReading && boxes[i].isEmpty) {
      boxes[i].isEmpty = false;
      boxes[i].isAlertActive = false;
      boxes[i].wasJustRefilled = true;
      boxes[i].refillDisplayStartTime = millis();
      
      // Stop buzzer immediately on refill
      isBuzzerMuted = true;
      digitalWrite(PIN_BUZZER, LOW);
      
      // Turn off LED
      digitalWrite(boxes[i].ledPin, LOW);
      
      // Set refill message display
      isShowingRefillMessage = true;
      refillMessageEndTime = millis() + REFILL_DISPLAY_TIME_MS;
      
      Serial.print(F("SUCCESS: Box "));
      Serial.print(static_cast<char>('A' + i));
      Serial.print(F(" ("));
      Serial.print(boxes[i].name);
      Serial.println(F(") has been REFILLED!"));
      
      forceDisplayUpdate();
    }
    
    // Turn off LED if no alerts and not just refilled
    if (!boxes[i].isAlertActive && !boxes[i].isScheduleAlert && 
        !boxes[i].isReminderActive && !boxes[i].wasJustRefilled) {
      digitalWrite(boxes[i].ledPin, LOW);
    }
  }
}

void checkMedicineSchedule(const DateTime& now) {
  if (now.minute() == lastCheckedMinute) {
    return;
  }
  
  lastCheckedMinute = now.minute();
  uint8_t currentHour = now.hour();
  uint8_t currentMinute = now.minute();
  
  for (uint8_t boxIdx = 0; boxIdx < 3; boxIdx++) {
    MedicineBox* box = &boxes[boxIdx];
    
    for (uint8_t doseIdx = 0; doseIdx < 3; doseIdx++) {
      uint8_t scheduledHour = box->scheduleTimes[doseIdx];
      
      if (scheduledHour == 0) continue;
      
      // Check if it's time for this dose (within schedule window)
      if (currentHour == scheduledHour && 
          currentMinute < SCHEDULE_WINDOW_MINUTES) {
        
        if (!box->wasScheduleTriggeredToday[doseIdx]) {
          box->isScheduleAlert = true;
          box->wasScheduleTriggeredToday[doseIdx] = true;
          
          // Reset buzzer mute when new schedule alert occurs
          isBuzzerMuted = false;
          
          Serial.print(F("SCHEDULE: Time for "));
          Serial.print(box->name);
          Serial.print(F(" (Box "));
          Serial.print(static_cast<char>('A' + boxIdx));
          Serial.println(F(")"));
          
          forceDisplayUpdate();
        }
      }
    }
  }
}

void checkMedicineReminders(const DateTime& now) {
  uint8_t currentHour = now.hour();
  uint8_t currentMinute = now.minute();
  uint16_t currentTotalMinutes = currentHour * 60 + currentMinute;
  
  // Reset all reminders first
  for (uint8_t i = 0; i < 3; i++) {
    boxes[i].isReminderActive = false;
  }
  
  // Check for upcoming medicines (within reminder window)
  for (uint8_t boxIdx = 0; boxIdx < 3; boxIdx++) {
    MedicineBox* box = &boxes[boxIdx];
    bool foundUpcoming = false;
    
    for (uint8_t doseIdx = 0; doseIdx < 3; doseIdx++) {
      uint8_t scheduledHour = box->scheduleTimes[doseIdx];
      
      if (scheduledHour == 0) continue;
      
      // Skip if already triggered today
      if (box->wasScheduleTriggeredToday[doseIdx]) continue;
      
      uint16_t scheduledTotalMinutes = scheduledHour * 60;
      
      // Check if medicine is coming up within reminder window
      if (scheduledTotalMinutes > currentTotalMinutes && 
          (scheduledTotalMinutes - currentTotalMinutes) <= REMINDER_WINDOW_MINUTES) {
        
        box->isReminderActive = true;
        box->nextDoseHour = scheduledHour;
        box->nextDoseMinute = 0; // Assuming doses are on the hour
        
        foundUpcoming = true;
        break; // Only show one upcoming dose per box
      }
      
      // Handle overnight schedules
      uint16_t scheduledTotalMinutesTomorrow = scheduledTotalMinutes + (24 * 60);
      if (scheduledTotalMinutesTomorrow > currentTotalMinutes && 
          (scheduledTotalMinutesTomorrow - currentTotalMinutes) <= REMINDER_WINDOW_MINUTES) {
        
        box->isReminderActive = true;
        box->nextDoseHour = scheduledHour;
        box->nextDoseMinute = 0;
        
        foundUpcoming = true;
        break;
      }
    }
  }
}

void checkForDailyReset(const DateTime& now) {
  if (now.day() != lastCheckedDay) {
    lastCheckedDay = now.day();
    
    for (uint8_t i = 0; i < 3; i++) {
      for (uint8_t j = 0; j < 3; j++) {
        boxes[i].wasScheduleTriggeredToday[j] = false;
      }
    }
    
    // Reset buzzer mute at midnight
    isBuzzerMuted = false;
    
    Serial.println(F("Daily schedule reset."));
  }
}

// ===================== ALERT HANDLING =====================
void handleAlerts() {
  unsigned long currentMillis = millis();
  isAnyAlertActive = false;
  
  // NEW: Check if refill message display time has expired
  if (isShowingRefillMessage && currentMillis > refillMessageEndTime) {
    isShowingRefillMessage = false;
    // Clear refilled flags
    for (uint8_t i = 0; i < 3; i++) {
      boxes[i].wasJustRefilled = false;
    }
    forceDisplayUpdate();
  }
  
  // Check for any active alerts
  for (uint8_t i = 0; i < 3; i++) {
    if (boxes[i].wasJustRefilled) {
      // Solid LED for refilled boxes (positive feedback)
      digitalWrite(boxes[i].ledPin, HIGH);
      continue;
    }
    
    if (boxes[i].isAlertActive || boxes[i].isScheduleAlert) {
      isAnyAlertActive = true;
      
      // Handle LED blinking for high priority alerts
      if (currentMillis - boxes[i].lastBlinkTime > LED_BLINK_INTERVAL_MS) {
        boxes[i].ledState = !boxes[i].ledState;
        digitalWrite(boxes[i].ledPin, boxes[i].ledState ? HIGH : LOW);
        boxes[i].lastBlinkTime = currentMillis;
      }
    } else if (boxes[i].isReminderActive) {
      // For reminders, use slower blink (solid with occasional blink)
      if (currentMillis - boxes[i].lastBlinkTime > (LED_BLINK_INTERVAL_MS * 2)) {
        boxes[i].ledState = !boxes[i].ledState;
        digitalWrite(boxes[i].ledPin, boxes[i].ledState ? HIGH : LOW);
        boxes[i].lastBlinkTime = currentMillis;
      }
    } else {
      // No alert - ensure LED is off
      digitalWrite(boxes[i].ledPin, LOW);
    }
  }
}

void handleBuzzer() {
  // Don't buzz if muted or no high priority alerts
  if (isBuzzerMuted || !isAnyAlertActive) {
    digitalWrite(PIN_BUZZER, LOW);
    return;
  }
  
  unsigned long currentMillis = millis();
  
  // Continuous beep pattern (500ms ON, 500ms OFF)
  if (buzzerState) {
    if (currentMillis - lastBuzzerToggle > BUZZER_ON_TIME_MS) {
      buzzerState = false;
      digitalWrite(PIN_BUZZER, LOW);
      lastBuzzerToggle = currentMillis;
    }
  } else {
    if (currentMillis - lastBuzzerToggle > BUZZER_OFF_TIME_MS) {
      buzzerState = true;
      digitalWrite(PIN_BUZZER, HIGH);
      lastBuzzerToggle = currentMillis;
    }
  }
}

// ===================== DISPLAY FUNCTIONS =====================
void updateDisplay(const DateTime& now) {
  unsigned long currentMillis = millis();
  
  if (currentMillis - lastDisplayUpdate < ALERT_REFRESH_MS && lastDisplayUpdate != 0) {
    return;
  }
  
  lastDisplayUpdate = currentMillis;
  
  // NEW: Check if we should show refill message
  if (isShowingRefillMessage) {
    displayRefillMessage();
    return;
  }
  
  AlertType highestAlert = getHighestPriorityAlert();
  
  switch (highestAlert) {
    case ALERT_EMPTY_BOX:
      displayEmptyBoxAlert();
      break;
    case ALERT_MEDICINE_TIME:
      displayMedicineTimeAlert();
      break;
    case ALERT_MEDICINE_SOON:
      // NEW: Alternate between reminder and normal screen
      if (currentMillis - lastDisplayToggleTime > REMINDER_DISPLAY_CYCLE_MS) {
        showReminderScreen = !showReminderScreen;
        lastDisplayToggleTime = currentMillis;
        forceDisplayUpdate();
      }
      
      if (showReminderScreen) {
        displayMedicineSoonAlert();
      } else {
        displayNormalScreen(now);
      }
      break;
    case ALERT_NONE:
      displayNormalScreen(now);
      break;
  }
}

AlertType getHighestPriorityAlert() {
  // Check for empty box alerts (highest priority)
  for (uint8_t i = 0; i < 3; i++) {
    if (boxes[i].isAlertActive) {
      return ALERT_EMPTY_BOX;
    }
  }
  
  // Check for schedule alerts (medium priority)
  for (uint8_t i = 0; i < 3; i++) {
    if (boxes[i].isScheduleAlert) {
      return ALERT_MEDICINE_TIME;
    }
  }
  
  // Check for reminders (low priority)
  for (uint8_t i = 0; i < 3; i++) {
    if (boxes[i].isReminderActive) {
      return ALERT_MEDICINE_SOON;
    }
  }
  
  return ALERT_NONE;
}

// NEW: Display refill message
void displayRefillMessage() {
  // Find which box was just refilled
  for (uint8_t i = 0; i < 3; i++) {
    if (boxes[i].wasJustRefilled) {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print(F("BOX REFILLED!"));
      
      lcd.setCursor(0, 1);
      lcd.print(F("Box "));
      lcd.print(static_cast<char>('A' + i));
      lcd.print(F(": "));
      
      String name = boxes[i].name;
      if (name.length() > 10) {
        name = name.substring(0, 10);
      }
      lcd.print(name);
      return;
    }
  }
  
  // If multiple boxes refilled
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("BOXES REFILLED"));
  lcd.setCursor(0, 1);
  lcd.print(F("All good now!"));
}

void displayEmptyBoxAlert() {
  for (uint8_t i = 0; i < 3; i++) {
    if (boxes[i].isAlertActive) {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print(F("! EMPTY BOX "));
      lcd.print(static_cast<char>('A' + i));
      lcd.print(F(" !"));
      
      lcd.setCursor(0, 1);
      lcd.print(F("Refill: "));
      
      String name = boxes[i].name;
      if (name.length() > 8) {
        name = name.substring(0, 8);
      }
      lcd.print(name);
      lcd.print(F("      "));
      
      return;
    }
  }
}

void displayMedicineTimeAlert() {
  for (uint8_t i = 0; i < 3; i++) {
    if (boxes[i].isScheduleAlert) {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print(F("MEDICINE TIME!"));
      
      lcd.setCursor(0, 1);
      lcd.print(F("Take "));
      
      String name = boxes[i].name;
      if (name.length() > 11) {
        name = name.substring(0, 11);
      }
      lcd.print(name);
      
      return;
    }
  }
}

void displayMedicineSoonAlert() {
  // Find the first medicine that's coming up soon
  for (uint8_t i = 0; i < 3; i++) {
    if (boxes[i].isReminderActive) {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print(F("Medicine Soon:"));
      
      lcd.setCursor(0, 1);
      lcd.print(boxes[i].name);
      lcd.print(F(" @ "));
      lcd.print(boxes[i].nextDoseHour < 10 ? F("0") : F(""));
      lcd.print(boxes[i].nextDoseHour);
      lcd.print(F(":00"));
      
      return;
    }
  }
}

void displayNormalScreen(const DateTime& now) {
  char timeBuffer[17];
  char dateBuffer[17];
  
  switch (currentDisplayMode) {
    case DISPLAY_MODE_TIME:
      // Format time with leading zeros
      snprintf(timeBuffer, sizeof(timeBuffer), "Time: %02d:%02d:%02d", 
               now.hour(), now.minute(), now.second());
      snprintf(dateBuffer, sizeof(dateBuffer), "Date: %02d/%02d/%02d", 
               now.day(), now.month(), now.year() % 100);
      
      lcd.setCursor(0, 0);
      lcd.print(timeBuffer);
      lcd.setCursor(0, 1);
      lcd.print(dateBuffer);
      
      // Add alert indicator if any alert is active
      if (isAnyAlertActive) {
        lcd.setCursor(15, 0);
        lcd.print(F("!"));
      }
      break;
      
    case DISPLAY_MODE_SCHEDULE:
      displayNextMedicine(now);
      break;
      
    case DISPLAY_MODE_STATUS:
      displayBoxStatus();
      break;
  }
}

void displayNextMedicine(const DateTime& now) {
  uint8_t currentHour = now.hour();
  uint8_t currentMinute = now.minute();
  uint16_t currentTotalMinutes = currentHour * 60 + currentMinute;
  uint16_t nextTotalMinutes = 24 * 60;
  uint8_t nextBoxIdx = 255;
  
  // Find the next scheduled dose across all boxes
  for (uint8_t boxIdx = 0; boxIdx < 3; boxIdx++) {
    for (uint8_t doseIdx = 0; doseIdx < 3; doseIdx++) {
      uint8_t scheduledHour = boxes[boxIdx].scheduleTimes[doseIdx];
      if (scheduledHour == 0) continue;
      
      // Skip if already taken today
      if (boxes[boxIdx].wasScheduleTriggeredToday[doseIdx]) continue;
      
      uint16_t scheduledTotalMinutes = scheduledHour * 60;
      
      // Handle overnight schedules
      if (scheduledTotalMinutes < currentTotalMinutes) {
        scheduledTotalMinutes += 24 * 60;
      }
      
      if (scheduledTotalMinutes < nextTotalMinutes) {
        nextTotalMinutes = scheduledTotalMinutes;
        nextBoxIdx = boxIdx;
      }
    }
  }
  
  lcd.setCursor(0, 0);
  lcd.print(F("Next medicine:"));
  lcd.setCursor(0, 1);
  
  if (nextBoxIdx == 255) {
    lcd.print(F("None today    "));
  } else {
    uint16_t minutesUntil = nextTotalMinutes - currentTotalMinutes;
    uint8_t hoursUntil = minutesUntil / 60;
    uint8_t minsUntil = minutesUntil % 60;
    
    lcd.print(static_cast<char>('A' + nextBoxIdx));
    lcd.print(F(": "));
    
    String name = boxes[nextBoxIdx].name;
    if (name.length() > 6) {
      name = name.substring(0, 6);
    }
    lcd.print(name);
    
    lcd.print(F(" in "));
    if (hoursUntil > 0) {
      lcd.print(hoursUntil);
      lcd.print(F("h"));
    }
    if (minsUntil > 0) {
      if (hoursUntil > 0) lcd.print(F(" "));
      lcd.print(minsUntil);
      lcd.print(F("m"));
    }
  }
}

void displayBoxStatus() {
  lcd.setCursor(0, 0);
  lcd.print(F("Box Status:"));
  
  lcd.setCursor(0, 1);
  for (uint8_t i = 0; i < 3; i++) {
    lcd.print(static_cast<char>('A' + i));
    lcd.print(F(":"));
    
    if (boxes[i].wasJustRefilled) {
      lcd.print(F("R ")); // Recently Refilled (solid LED)
    } else if (boxes[i].isAlertActive) {
      lcd.print(F("E ")); // Empty
    } else if (boxes[i].isScheduleAlert) {
      lcd.print(F("T ")); // Time to take
    } else if (boxes[i].isReminderActive) {
      lcd.print(F("S ")); // Soon
    } else {
      lcd.print(F("F ")); // Full/OK
    }
  }
  
  // Add buzzer status
  lcd.setCursor(12, 1);
  if (isBuzzerMuted) {
    lcd.print(F("MUT"));
  } else if (isAnyAlertActive) {
    lcd.print(F("BZZ"));
  } else {
    lcd.print(F("   "));
  }
}

void forceDisplayUpdate() {
  lastDisplayUpdate = 0;
}

// ===================== UTILITY FUNCTIONS =====================
void displayWelcomeSequence() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("Enhanced Med"));
  lcd.setCursor(0, 1);
  lcd.print(F("Reminder v3.0"));
  delay(1500);
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("Buzzer: Continuous"));
  lcd.setCursor(0, 1);
  lcd.print(F("Until refill/ack"));
  delay(2000);
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("Reminder: Flashes"));
  lcd.setCursor(0, 1);
  lcd.print(F("every 5 seconds"));
  delay(2000);
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("System Ready"));
  
  DateTime now = rtc.now();
  lcd.setCursor(0, 1);
  lcd.print(now.hour() < 10 ? F("0") : F(""));
  lcd.print(now.hour());
  lcd.print(F(":"));
  lcd.print(now.minute() < 10 ? F("0") : F(""));
  lcd.print(now.minute());
  lcd.print(F(":"));
  lcd.print(now.second() < 10 ? F("0") : F(""));
  lcd.print(now.second());
  
  delay(2000);
  lcd.clear();
}

// Utility function to set RTC time (call once in setup if needed)
void setRTCTime() {
  // Uncomment and adjust before uploading once
  // rtc.adjust(DateTime(2025, 12, 15, 10, 30, 0));
}
