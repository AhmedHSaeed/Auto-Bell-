#include <LiquidCrystal_I2C.h>  // I2C LCD Library
#include <EEPROM.h>
#include <ThreeWire.h>          // DS1302
#include <RtcDS1302.h>

// LCD I2C configuration
LiquidCrystal_I2C lcd(0x27, 16, 2);  // Try 0x3F if 0x27 doesn't work

// DS1302 RTC pins
#define RTC_CLK 5    // Clock pin
#define RTC_DAT 4    // Data pin  
#define RTC_RST 2    // Reset pin

// Button pins
#define bt_set  A0
#define bt_next A1
#define bt_up   A2
#define bt_down A3

// Relay and Buzzer pins
#define relay 8      // Relay for bell (school bell)
#define buzzer 6     // Buzzer for button feedback

// LED indicators
#define led_active 9    // Green LED - System active
#define led_bell   10   // Red LED - Bell ringing

// Create DS1302 object
ThreeWire myWire(RTC_DAT, RTC_CLK, RTC_RST);
RtcDS1302<ThreeWire> Rtc(myWire);

// EEPROM Structure
#define EEPROM_INIT_FLAG 0
#define BELL_DURATION_ADDR 10
#define WEEKEND_DAY_ADDR 11
#define ALARM_COUNT_ADDR 12
#define ALARM_DATA_START 50

// Constants
#define MAX_ALARMS 30
#define MAX_BELL_DURATION 99
#define MIN_BELL_DURATION 1

// Variables
int hh = 0, mm = 0, ss = 0, set_day = 0;
int bell_duration = 3;
int weekend = 6;
int current_alarm = 1;
int alarm_hour = 8;
int alarm_minute = 0;
int total_alarms = 0;

int setMode = 0;
int field = 0;
int next_bell_hour = 0;
int next_bell_minute = 0;
bool next_bell_found = false;
bool rtc_valid = true;
bool show_cursor = false;
unsigned long cursor_blink_time = 0;

unsigned long last_button_time = 0;
unsigned long last_rtc_update = 0;
unsigned long last_display_update = 0;

// ======== FIXED RELAY LOGIC ========
bool bellActive = false;
unsigned long bellStart = 0;
int lastBellMinute = -1;
// ===================================

// Alarm tracking
bool alarms_triggered_today[MAX_ALARMS] = {false};
int last_checked_day = -1;

// Day names - FIXED: Proper order for DS1302
// DS1302: 1=Sunday, 2=Monday, 3=Tuesday, 4=Wednesday, 5=Thursday, 6=Friday, 7=Saturday
String day_names[] = {"", "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

void setup() {
  Serial.begin(9600);
  Serial.println(F("Auto Bell"));
  
  // Initialize LEDs
  pinMode(led_active, OUTPUT);
  pinMode(led_bell, OUTPUT);
  digitalWrite(led_active, HIGH);
  digitalWrite(led_bell, LOW);
  
  // Initialize LCD
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("Auto Bell"));
  lcd.setCursor(0, 1);
  lcd.print(F("Top Students"));
  
  
  // Initialize RTC
  Rtc.Begin();
  
  if (!Rtc.GetIsRunning()) {
    Serial.println(F("RTC not running, starting..."));
    Rtc.SetIsRunning(true);
  }
  
  if (Rtc.GetIsWriteProtected()) {
    Rtc.SetIsWriteProtected(false);
  }
  
  // Set RTC to compile time if needed
  RtcDateTime compiled = RtcDateTime(__DATE__, __TIME__);
  RtcDateTime now = Rtc.GetDateTime();
  
  if (!now.IsValid() || now.Year() < 2023) {
    Serial.println(F("RTC not set or invalid. Setting to compile time."));
    Rtc.SetDateTime(compiled);
  }
  
  // Initialize buttons
  pinMode(bt_set, INPUT_PULLUP);
  pinMode(bt_next, INPUT_PULLUP);
  pinMode(bt_up, INPUT_PULLUP);
  pinMode(bt_down, INPUT_PULLUP);
  
  // Initialize relay and buzzer
  pinMode(relay, OUTPUT);
  pinMode(buzzer, OUTPUT);
  digitalWrite(relay, LOW);
  digitalWrite(buzzer, LOW);
  
  // Initialize EEPROM
  initializeEEPROM();
  
  // Read settings
  bell_duration = EEPROM.read(BELL_DURATION_ADDR);
  weekend = EEPROM.read(WEEKEND_DAY_ADDR);
  total_alarms = EEPROM.read(ALARM_COUNT_ADDR);
  
  // Validate settings
  if (bell_duration < MIN_BELL_DURATION || bell_duration > MAX_BELL_DURATION) {
    bell_duration = 3;
    EEPROM.write(BELL_DURATION_ADDR, bell_duration);
  }
  
  if (weekend < 1 || weekend > 7) {
    weekend = 6;
    EEPROM.write(WEEKEND_DAY_ADDR, weekend);
  }
  
  if (total_alarms > MAX_ALARMS) {
    total_alarms = 0;
    EEPROM.write(ALARM_COUNT_ADDR, total_alarms);
  }
  
  delay(1500);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("System Ready"));
  lcd.setCursor(0, 1);
  lcd.print(F("Alarms: "));
  lcd.print(total_alarms);
  
  buttonBeep();
  delay(1000);
  lcd.clear();
  
  // Get initial time and day
  updateTimeFromRTC();
  last_checked_day = set_day;
  findNextBell();
  
  Serial.print(F("Current day: "));
  Serial.println(day_names[set_day]);
  
  quickBellTest();
  
  Serial.print(F("Bell duration: "));
  Serial.print(bell_duration);
  Serial.println(F(" seconds"));
}

void loop() {
  unsigned long current_millis = millis();
  
  // Update RTC time every second
  if (current_millis - last_rtc_update >= 1000) {
    last_rtc_update = current_millis;
    updateTimeFromRTC();
    
    // Check for alarms (only on non-weekend days)
    if (!isWeekend(set_day)) {
      checkForAlarms();
    }
  }
  
  // ======== FIXED RELAY CONTROL ========
  // ---- START BELL (moment of match only) ----
  if (!bellActive && !isWeekend(set_day)) {
    for (int i = 0; i < total_alarms; i++) {
      int check_hour, check_minute;
      readAlarm(i, check_hour, check_minute);
      
      if (check_hour == hh && check_minute == mm && 
          !alarms_triggered_today[i] && lastBellMinute != mm) {
        
        // Mark alarm as triggered
        alarms_triggered_today[i] = true;
        lastBellMinute = mm;
        
        // Activate bell
        digitalWrite(relay, HIGH);
        digitalWrite(led_bell, HIGH);
        bellStart = current_millis;
        bellActive = true;
        
        Serial.print(F("Bell #"));
        Serial.print(i + 1);
        Serial.print(F(" at "));
        Serial.print(hh);
        Serial.print(F(":"));
        if (mm < 10) Serial.print("0");
        Serial.print(mm);
        Serial.print(F(" for "));
        Serial.print(bell_duration);
        Serial.println(F(" seconds"));
        
        // Audible feedback
        digitalWrite(buzzer, HIGH);
        delay(200);
        digitalWrite(buzzer, LOW);
        
        // Update next bell
        findNextBell();
        
        break; // Only trigger one alarm
      }
    }
  }
  
  // ---- STOP BELL AFTER DURATION ----
  if (bellActive && current_millis - bellStart >= (unsigned long)(bell_duration * 1000)) {
    digitalWrite(relay, LOW);
    digitalWrite(led_bell, LOW);
    bellActive = false;
    
    Serial.print(F("Bell OFF after "));
    Serial.print(bell_duration);
    Serial.println(F(" seconds"));
    
    // Small delay to ensure relay is off
    delay(50);
  }
  // ===================================
  
  // Handle buttons
  handleButtons();
  
  // Handle cursor blinking in settings modes
  if (setMode > 0) {
    if (current_millis - cursor_blink_time >= 500) {
      cursor_blink_time = current_millis;
      show_cursor = !show_cursor;
    }
  } else {
    show_cursor = false;
  }
  
  // Update display
  if (current_millis - last_display_update >= 250) {
    updateDisplay();
    last_display_update = current_millis;
  }
  
  delay(10);
}

// FIXED: Correct day of week calculation for DS1302
void updateTimeFromRTC() {
  RtcDateTime now = Rtc.GetDateTime();
  
  if (now.IsValid()) {
    int old_ss = ss;
    int old_mm = mm;
    int old_hh = hh;
    int old_day = set_day;
    
    ss = now.Second();
    mm = now.Minute();
    hh = now.Hour();
    
    // FIXED: DS1302 has built-in day of week function
    int dow = now.DayOfWeek();  // Get day of week from RTC (0-6)
    
    // Convert to our format: 1=Sunday, 2=Monday, ..., 7=Saturday
    set_day = dow + 1;
    
    // Reset triggered alarms if day changed
    if (old_day != set_day) {
      for (int i = 0; i < MAX_ALARMS; i++) {
        alarms_triggered_today[i] = false;
      }
      lastBellMinute = -1;
      Serial.println(F("Day changed - reset alarms"));
    }
    
    rtc_valid = true;
  } else {
    rtc_valid = false;
    Serial.println(F("RTC time invalid!"));
  }
}

// Simplified - relay logic is now in main loop
void checkForAlarms() {
  // This function is now just for finding next bell
  // Actual bell triggering is in the main loop with the fixed logic
}

void handleButtons() {
  static bool btn_set_prev = HIGH;
  static bool btn_next_prev = HIGH;
  static bool btn_up_prev = HIGH;
  static bool btn_down_prev = HIGH;
  
  bool btn_set = digitalRead(bt_set);
  bool btn_next = digitalRead(bt_next);
  bool btn_up = digitalRead(bt_up);
  bool btn_down = digitalRead(bt_down);
  
  unsigned long current_millis = millis();
  
  // SET button - Mode selection
  if (btn_set == LOW && btn_set_prev == HIGH && (current_millis - last_button_time > 300)) {
    buttonBeep();
    last_button_time = current_millis;
    
    setMode = (setMode + 1) % 5;
    field = 0;
    lcd.noBlink();
    
    // Initialize alarm mode
    if (setMode == 3) {
      current_alarm = 1;
      if (total_alarms > 0) {
        readAlarm(0, alarm_hour, alarm_minute);
      } else {
        alarm_hour = 8;
        alarm_minute = 0;
      }
    }
    
    lcd.clear();
  }
  btn_set_prev = btn_set;
  
  // NEXT button
  if (btn_next == LOW && btn_next_prev == HIGH && (current_millis - last_button_time > 300)) {
    buttonBeep();
    last_button_time = current_millis;
    
    if (setMode == 1) {
      field = (field + 1) % 2;
    } 
    else if (setMode == 3) {
      field = (field + 1) % 3;
      
      if (field == 0) {
        // Save current alarm
        saveAlarm(current_alarm - 1, alarm_hour, alarm_minute);
        
        // Move to next alarm
        current_alarm++;
        if (current_alarm > MAX_ALARMS) {
          current_alarm = 1;
        }
        
        // Update total if needed
        if (current_alarm > total_alarms) {
          total_alarms = current_alarm;
          EEPROM.write(ALARM_COUNT_ADDR, total_alarms);
        }
        
        // Read next alarm
        if (current_alarm <= total_alarms) {
          readAlarm(current_alarm - 1, alarm_hour, alarm_minute);
        } else {
          alarm_hour = 8;
          alarm_minute = 0;
        }
        
        // Show confirmation
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print(F("Alarm Saved"));
        lcd.setCursor(0, 1);
        lcd.print(F("Alarm #"));
        lcd.print(current_alarm);
        delay(800);
        lcd.clear();
        
        // Update next bell
        findNextBell();
      }
    }
  }
  btn_next_prev = btn_next;
  
  // UP button - MANUAL DAY SETTING
  if (btn_up == LOW && btn_up_prev == HIGH && (current_millis - last_button_time > 200)) {
    buttonBeep();
    last_button_time = current_millis;
    
    switch (setMode) {
      case 0: // In normal mode, press UP to manually advance day (for testing)
        set_day++;
        if (set_day > 7) set_day = 1;
        Serial.print(F("Manual day change to: "));
        Serial.println(day_names[set_day]);
        findNextBell();
        break;
        
      case 1: // Set Time
        if (field == 0) {
          hh = (hh + 1) % 24;
        } else {
          mm = (mm + 1) % 60;
        }
        setRTCTime();
        findNextBell();
        break;
        
      case 2: // Set Bell Duration
        bell_duration++;
        if (bell_duration > MAX_BELL_DURATION) bell_duration = MIN_BELL_DURATION;
        EEPROM.write(BELL_DURATION_ADDR, bell_duration);
        Serial.print(F("Bell duration: "));
        Serial.print(bell_duration);
        Serial.println(F("s"));
        break;
        
      case 3: // Set Alarm
        if (field == 0) {
          current_alarm++;
          if (current_alarm > MAX_ALARMS) current_alarm = 1;
          if (current_alarm <= total_alarms) {
            readAlarm(current_alarm - 1, alarm_hour, alarm_minute);
          } else {
            alarm_hour = 8;
            alarm_minute = 0;
          }
        } else if (field == 1) {
          alarm_hour = (alarm_hour + 1) % 24;
        } else if (field == 2) {
          alarm_minute = (alarm_minute + 1) % 60;
        }
        break;
        
      case 4: // Set Weekend
        weekend++;
        if (weekend > 7) weekend = 1;
        EEPROM.write(WEEKEND_DAY_ADDR, weekend);
        findNextBell();
        break;
    }
    
    delay(150);
  }
  btn_up_prev = btn_up;
  
  // DOWN button - MANUAL DAY SETTING
  if (btn_down == LOW && btn_down_prev == HIGH && (current_millis - last_button_time > 200)) {
    buttonBeep();
    last_button_time = current_millis;
    
    switch (setMode) {
      case 0: // In normal mode, press DOWN to manually go back day (for testing)
        set_day--;
        if (set_day < 1) set_day = 7;
        Serial.print(F("Manual day change to: "));
        Serial.println(day_names[set_day]);
        findNextBell();
        break;
        
      case 1: // Set Time
        if (field == 0) {
          hh--;
          if (hh < 0) hh = 23;
        } else {
          mm--;
          if (mm < 0) mm = 59;
        }
        setRTCTime();
        findNextBell();
        break;
        
      case 2: // Set Bell Duration
        bell_duration--;
        if (bell_duration < MIN_BELL_DURATION) bell_duration = MAX_BELL_DURATION;
        EEPROM.write(BELL_DURATION_ADDR, bell_duration);
        Serial.print(F("Bell duration: "));
        Serial.print(bell_duration);
        Serial.println(F("s"));
        break;
        
      case 3: // Set Alarm
        if (field == 0) {
          current_alarm--;
          if (current_alarm < 1) current_alarm = MAX_ALARMS;
          if (current_alarm <= total_alarms) {
            readAlarm(current_alarm - 1, alarm_hour, alarm_minute);
          } else {
            alarm_hour = 8;
            alarm_minute = 0;
          }
        } else if (field == 1) {
          alarm_hour--;
          if (alarm_hour < 0) alarm_hour = 23;
        } else if (field == 2) {
          alarm_minute--;
          if (alarm_minute < 0) alarm_minute = 59;
        }
        break;
        
      case 4: // Set Weekend
        weekend--;
        if (weekend < 1) weekend = 7;
        EEPROM.write(WEEKEND_DAY_ADDR, weekend);
        findNextBell();
        break;
    }
    
    delay(150);
  }
  btn_down_prev = btn_down;
  
  // RESET ALARMS: Press and hold SET + NEXT for 5 seconds
  static unsigned long reset_start_time = 0;
  if (btn_set == LOW && btn_next == LOW) {
    if (reset_start_time == 0) {
      reset_start_time = current_millis;
    } else if (current_millis - reset_start_time > 5000) {
      resetAllAlarms();
      reset_start_time = 0;
    }
  } else {
    reset_start_time = 0;
  }
}

void updateDisplay() {
  lcd.noBlink();  // Turn off blink initially
  
  if (setMode == 0) {
    // Normal display mode
    lcd.setCursor(0, 0);
    
    // Day and Weekend indicator
    lcd.print(day_names[set_day]);
    if (isWeekend(set_day)) {
      lcd.print(" W");
    } else {
      lcd.print("  ");
    }
    
    // Time
    lcd.setCursor(8, 0);
    if (hh < 10) lcd.print("0");
    lcd.print(hh);
    lcd.print(":");
    if (mm < 10) lcd.print("0");
    lcd.print(mm);
    lcd.print(":");
    if (ss < 10) lcd.print("0");
    lcd.print(ss);
    
    // Line 2: Status
    lcd.setCursor(0, 1);
    if (bellActive) {
      unsigned long elapsed = millis() - bellStart;
      unsigned long remaining = bell_duration - (elapsed / 1000);
      if (remaining < 0) remaining = 0;
      lcd.print("BELL:");
      lcd.print(remaining);
      lcd.print("s    ");
    } else {
      lcd.print("Next:");
      if (next_bell_found) {
        if (next_bell_hour < 10) lcd.print("0");
        lcd.print(next_bell_hour);
        lcd.print(":");
        if (next_bell_minute < 10) lcd.print("0");
        lcd.print(next_bell_minute);
        lcd.print("   ");
      } else {
        lcd.print("--:--   ");
      }
    }
  } 
  else if (setMode == 1) {
    // Set Time
    lcd.setCursor(0, 0);
    lcd.print("SET TIME       ");
    
    lcd.setCursor(0, 1);
    
    // Hour
    if (field == 0 && show_cursor) {
      lcd.print("__");
    } else {
      if (hh < 10) lcd.print("0");
      lcd.print(hh);
    }
    
    lcd.print(":");
    
    // Minute
    if (field == 1 && show_cursor) {
      lcd.print("__");
    } else {
      if (mm < 10) lcd.print("0");
      lcd.print(mm);
    }
    
    lcd.print(":");
    if (ss < 10) lcd.print("0");
    lcd.print(ss);
    
    // Show cursor position
    if (show_cursor) {
      lcd.setCursor(field * 3, 1);
      lcd.blink();
    }
  }
  else if (setMode == 2) {
    // Set Bell Duration
    lcd.setCursor(0, 0);
    lcd.print("BELL DURATION  ");
    lcd.setCursor(0, 1);
    lcd.print("Seconds: ");
    
    if (show_cursor) {
      lcd.print("   ");
      lcd.setCursor(8, 1);
      lcd.blink();
    } else {
      lcd.print(bell_duration);
      if (bell_duration < 10) lcd.print(" ");
      if (bell_duration < 100) lcd.print(" ");
    }
  }
  else if (setMode == 3) {
    // Set Alarms
    lcd.setCursor(0, 0);
    lcd.print("Alarm#");
    lcd.print(current_alarm);
    lcd.print("/");
    lcd.print(total_alarms);
    lcd.print("    ");
    
    lcd.setCursor(0, 1);
    
    if (field == 0) {
      if (show_cursor) {
        lcd.print("No: __");
      } else {
        lcd.print("No: ");
        if (current_alarm < 10) lcd.print("0");
        lcd.print(current_alarm);
      }
    } 
    else if (field == 1) {
      if (show_cursor) {
        lcd.print("Hr: __");
      } else {
        lcd.print("Hr: ");
        if (alarm_hour < 10) lcd.print("0");
        lcd.print(alarm_hour);
      }
    } 
    else if (field == 2) {
      if (show_cursor) {
        lcd.print("Mn: __");
      } else {
        lcd.print("Mn: ");
        if (alarm_minute < 10) lcd.print("0");
        lcd.print(alarm_minute);
      }
    }
    
    // Show full time on right
    lcd.setCursor(10, 1);
    if (alarm_hour < 10) lcd.print("0");
    lcd.print(alarm_hour);
    lcd.print(":");
    if (alarm_minute < 10) lcd.print("0");
    lcd.print(alarm_minute);
    
    // Show cursor
    if (show_cursor) {
      lcd.setCursor(4, 1);
      lcd.blink();
    }
  }
  else if (setMode == 4) {
    // Set Weekend
    lcd.setCursor(0, 0);
    lcd.print("SET WEEKEND    ");
    lcd.setCursor(0, 1);
    
    if (show_cursor) {
      if (weekend == 6) {
        lcd.print("Fri&Sat   ");
      } else {
        lcd.print(day_names[weekend]);
        lcd.print("       ");
      }
      lcd.setCursor(0, 1);
      lcd.blink();
    } else {
      if (weekend == 6) {
        lcd.print("Fri&Sat   ");
      } else {
        lcd.print(day_names[weekend]);
        lcd.print("       ");
      }
    }
  }
}

void initializeEEPROM() {
  if (EEPROM.read(EEPROM_INIT_FLAG) != 0xAA) {
    Serial.println(F("Initializing EEPROM..."));
    
    EEPROM.write(EEPROM_INIT_FLAG, 0xAA);
    EEPROM.write(BELL_DURATION_ADDR, 30);
    EEPROM.write(WEEKEND_DAY_ADDR, 6);
    EEPROM.write(ALARM_COUNT_ADDR, 0);
    
    for (int i = 0; i < MAX_ALARMS * 2; i++) {
      EEPROM.write(ALARM_DATA_START + i, 0);
    }
    
    Serial.println(F("EEPROM initialized"));
  }
}

void saveAlarm(int index, int hour, int minute) {
  if (index < 0 || index >= MAX_ALARMS) return;
  
  int address = ALARM_DATA_START + (index * 2);
  EEPROM.write(address, hour);
  EEPROM.write(address + 1, minute);
  
  if (index >= total_alarms) {
    total_alarms = index + 1;
    EEPROM.write(ALARM_COUNT_ADDR, total_alarms);
  }
}

void readAlarm(int index, int &hour, int &minute) {
  if (index < 0 || index >= total_alarms) {
    hour = 0;
    minute = 0;
    return;
  }
  
  int address = ALARM_DATA_START + (index * 2);
  hour = EEPROM.read(address);
  minute = EEPROM.read(address + 1);
}

void findNextBell() {
  next_bell_found = false;
  
  // Check if today is weekend - NO BELLS ON WEEKENDS
  if (isWeekend(set_day)) {
    Serial.print(F("Today is "));
    Serial.print(day_names[set_day]);
    Serial.println(F(" (weekend) - no bells"));
    return;
  }
  
  if (total_alarms == 0) {
    return;
  }
  
  // Check all alarms to find the next one
  for (int i = 0; i < total_alarms; i++) {
    int check_hour, check_minute;
    readAlarm(i, check_hour, check_minute);
    
    // Check if this alarm is in future OR if it's current time but not triggered
    bool is_future = (check_hour > hh) || (check_hour == hh && check_minute > mm);
    bool is_current_untriggered = (check_hour == hh && check_minute == mm && !alarms_triggered_today[i]);
    
    if (is_future || is_current_untriggered) {
      next_bell_hour = check_hour;
      next_bell_minute = check_minute;
      next_bell_found = true;
      
      Serial.print(F("Next bell: "));
      Serial.print(next_bell_hour);
      Serial.print(F(":"));
      if (next_bell_minute < 10) Serial.print("0");
      Serial.println(next_bell_minute);
      return;
    }
  }
  
  // If no more alarms today, check first alarm tomorrow
  if (total_alarms > 0) {
    readAlarm(0, next_bell_hour, next_bell_minute);
    next_bell_found = true;
    Serial.println(F("Next bell is first alarm tomorrow"));
  }
}

bool isWeekend(int day) {
  // Proper weekend checking
  if (weekend == 6) {
    // Friday = 6, Saturday = 7 are weekends
    return (day == 6 || day == 7);
  } else {
    // Only the selected day is weekend
    return (day == weekend);
  }
}

// FIXED: Set RTC with correct day of week
void setRTCTime() {
  RtcDateTime now = Rtc.GetDateTime();
  
  // Create new time with current date but updated time
  RtcDateTime new_time(now.Year(), now.Month(), now.Day(), hh, mm, ss);
  
  // IMPORTANT: The day of week is automatically calculated by RTC library
  // when we create RtcDateTime object
  Rtc.SetDateTime(new_time);
  
  Serial.print(F("RTC set to: "));
  Serial.print(hh);
  Serial.print(F(":"));
  Serial.print(mm);
  Serial.print(F(":"));
  Serial.print(ss);
  Serial.print(F(" on "));
  Serial.println(day_names[set_day]);
}

void buttonBeep() {
  digitalWrite(buzzer, HIGH);
  delay(20);
  digitalWrite(buzzer, LOW);
}

void quickBellTest() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("Testing..."));
  
  // Ensure relay is off
  digitalWrite(relay, LOW);
  delay(100);
  
  // Turn on relay
  digitalWrite(relay, HIGH);
  digitalWrite(led_bell, HIGH);
  digitalWrite(buzzer, HIGH);
  delay(200);
  digitalWrite(buzzer, LOW);
  
  // Keep relay on for 3 seconds
  delay(3000);
  
  // Turn off relay
  digitalWrite(relay, LOW);
  digitalWrite(led_bell, LOW);
  
  lcd.clear();
}

void resetAllAlarms() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("RESETTING ALL"));
  lcd.setCursor(0, 1);
  lcd.print(F("ALARMS..."));
  
  total_alarms = 0;
  EEPROM.write(ALARM_COUNT_ADDR, 0);
  
  for (int i = 0; i < MAX_ALARMS * 2; i++) {
    EEPROM.write(ALARM_DATA_START + i, 0);
  }
  
  // Reset triggered alarms
  for (int i = 0; i < MAX_ALARMS; i++) {
    alarms_triggered_today[i] = false;
  }
  
  // Reset bell tracking
  bellActive = false;
  lastBellMinute = -1;
  digitalWrite(relay, LOW);
  digitalWrite(led_bell, LOW);
  
  // Feedback
  for (int i = 0; i < 3; i++) {
    digitalWrite(buzzer, HIGH);
    digitalWrite(led_bell, HIGH);
    delay(150);
    digitalWrite(buzzer, LOW);
    digitalWrite(led_bell, LOW);
    delay(150);
  }
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("ALL ALARMS"));
  lcd.setCursor(0, 1);
  lcd.print(F("RESET COMPLETE"));
  
  delay(1500);
  lcd.clear();
  
  findNextBell();
}