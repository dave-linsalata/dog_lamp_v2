#include <Wire.h>
#include <Adafruit_NeoPixel.h>
#include <DS3232RTC.h>  // https://github.com/JChristensen/DS3232RTC
#include <Streaming.h>  // https://github.com/janelia-arduino/Streaming

// RTC setup
constexpr uint8_t RTC_INT_PIN{ 5 };  // RTC provides an alarm signal on this pin
DS3232RTC myRTC;
char timestamp[32];  // current time from the RTC in text format

enum alarmPhases {
  NIGHTLIGHT,
  GETTINGREADY,
  DAYTIME
};
enum alarmPhases alarmState;

// Define timing for each alarm
#define DAYOFWEEKCHECK_HOUR 12
#define DAYOFWEEKCHECK_MINUTE 11
#define WEEKDAY_WAKEUP_HOUR 12
#define WEEKDAY_WAKEUP_MINUTE 12  // +1
#define WEEKEND_WAKEUP_HOUR 12
#define WEEKEND_WAKEUP_MINUTE 12  // +1
#define TURN_OFF_MINUTES 1
#define NIGHTLIGHT_ON_HOUR 12
#define NIGHTLIGHT_ON_MINUTE 14  // +3

// NeoPixel Setup
#define LED_PIN 6       // Which pin on the Arduino is connected to the NeoPixels?
#define LED_COUNT 64    // How many NeoPixels are attached to the Arduino?
#define BRIGHTNESS 200  // Set BRIGHTNESS to about 1/5 (max = 255); Example had it set to 50

// Declare our NeoPixel strip object:
Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRBW + NEO_KHZ800);  // Adjust to GRBW per forum, but order is RGBW in setting
// Argument 1 = Number of pixels in NeoPixel strip
// Argument 2 = Arduino pin number (most are valid)
// Argument 3 = Pixel type flags, add together as needed:
//   NEO_KHZ800  800 KHz bitstream (most NeoPixel products w/WS2812 LEDs)
//   NEO_KHZ400  400 KHz (classic 'v1' (not v2) FLORA pixels, WS2811 drivers)
//   NEO_GRB     Pixels are wired for GRB bitstream (most NeoPixel products)
//   NEO_RGB     Pixels are wired for RGB bitstream (v1 FLORA pixels, not v2)
//   NEO_RGBW    Pixels are wired for RGBW bitstream (NeoPixel RGBW products)


void setup() {
  Serial.begin(115200);
  // while the serial stream is not open, do nothing:
  while (!Serial)
    ;
  delay(1000);
  Serial.println("DEBUG: After serial begin");
  // Serial << F("\n" __FILE__ " " __DATE__ " " __TIME__ "\n");
  pinMode(RTC_INT_PIN, INPUT_PULLUP);

  // RTC setup
  myRTC.begin();
  setSyncProvider(myRTC.get);  // the function to get the time from the RTC
  if (timeStatus() != timeSet)
    Serial.println("Unable to sync with the RTC");
  else
    Serial.println("RTC has set the system time");

  // initialize the alarms to known values, clear the alarm flags, clear the alarm interrupt flags
  myRTC.setAlarm(DS3232RTC::ALM1_MATCH_DATE, 0, 0, 0, 1);  // Last digit (1) implies date, but I assume it's fine since I switch it later)
  myRTC.setAlarm(DS3232RTC::ALM2_MATCH_DATE, 0, 0, 0, 1);
  myRTC.alarm(DS3232RTC::ALARM_1);
  myRTC.alarm(DS3232RTC::ALARM_2);
  myRTC.alarmInterrupt(DS3232RTC::ALARM_1, false);
  myRTC.alarmInterrupt(DS3232RTC::ALARM_2, false);
  myRTC.squareWave(DS3232RTC::SQWAVE_NONE);

  // print the current time
  time_t t = myRTC.get();
  formatTime(timestamp, t);
  Serial << millis() << F(" Current RTC time ") << timestamp << endl;

  // Set the proper status. Set it to previous status and let changeColor() run
  Serial << millis() << F(" Setting proper status") << endl;
  if (weekday() == 1 || weekday() == 7) {  // Is a weekend (Day of the week (1-7), Sunday is day 1)
    Serial << millis() << F(" DEBUG:Setting weekend or weekday") << endl;

    if (hour() < WEEKEND_WAKEUP_HOUR || hour() >= NIGHTLIGHT_ON_HOUR) {  // Want this to end up at nightlight, so set to daytime
      alarmState = DAYTIME;
    } else {
      if (hour() == WEEKEND_WAKEUP_HOUR) {  // Shortcutting by not worrying about getting ready timer.
        alarmState = NIGHTLIGHT;
      } else {
        alarmState = GETTINGREADY;
      }
    }
  } else {                                                               // Is a weekday
    if (hour() < WEEKDAY_WAKEUP_HOUR || hour() >= NIGHTLIGHT_ON_HOUR) {  // Want this to end up at nightlight, so set to daytime
      alarmState = DAYTIME;
    } else {
      if (hour() == WEEKDAY_WAKEUP_HOUR) {  // Shortcutting by not worrying about getting ready timer.
        alarmState = NIGHTLIGHT;
      } else {
        alarmState = GETTINGREADY;
      }
    }
  }
  changeColor();
  Serial.print("Current status: ");
  Serial.print(alarmState);
  Serial.println();

  // Set alarm 1, which (later) checks for day of week and then sets alarm 2 appropraitely
  Serial << millis() << F(" DEBUG:Setting alarm 1 for day of week check") << endl;
  myRTC.setAlarm(DS3232RTC::ALM1_MATCH_HOURS, DAYOFWEEKCHECK_MINUTE, DAYOFWEEKCHECK_HOUR, 0);  // 1-18 changed date from 1 to 0
  myRTC.alarm(DS3232RTC::ALARM_1);                                                             // ensure RTC interrupt flag is cleared
  myRTC.alarmInterrupt(DS3232RTC::ALARM_1, true);

  // Set alarm 2 to a default weekday alarm
  Serial << millis() << F(" DEBUG:Setting alarm 2") << endl;
  myRTC.setAlarm(DS3232RTC::ALM2_MATCH_HOURS, WEEKDAY_WAKEUP_MINUTE, WEEKDAY_WAKEUP_HOUR, 0);
  myRTC.alarm(DS3232RTC::ALARM_2);  // ensure RTC interrupt flag is cleared
  myRTC.alarmInterrupt(DS3232RTC::ALARM_2, true);

  // NeoPixel setup
  Serial << millis() << F(" DEBUG:Neopixel initializing") << endl;
  strip.begin();  // INITIALIZE NeoPixel strip object (REQUIRED)
  strip.show();   // Turn OFF all pixels ASAP
  strip.setBrightness(BRIGHTNESS);
}

void loop() {

  digitalClockDisplay();
  Serial.print("Current status: ");
  Serial.print(alarmState);
  Serial.println();
  delay(1000);

  // check to see if the INT/SQW pin is low, i.e. an alarm has occurred
  if (!digitalRead(RTC_INT_PIN)) {
      Serial << millis() << F(" DEBUG:Some alarm went off; checking which") << endl;
    formatTime(timestamp, myRTC.get());     // get current RTC time
    if (myRTC.alarm(DS3232RTC::ALARM_1)) {  // If Alarm 1 has gone off, reset the alarm flag and check to see the day of the week
      Serial << millis() << F(" Alarm 1 went off at ") << timestamp << endl;

      // Set Alarm 2
      setAlarm2();

      // if (weekday() == 1 || weekday() == 7) {                                                        // Is a weekend (Day of the week (1-7), Sunday is day 1)
      //   myRTC.setAlarm(DS3232RTC::ALM2_MATCH_HOURS, WEEKEND_WAKEUP_MINUTE, WEEKEND_WAKEUP_HOUR, 1);  // Set alarm 2 to weekend wakeup time
      //   myRTC.alarm(DS3232RTC::ALARM_2);                                                             // ensure RTC interrupt flag is cleared
      //   myRTC.alarmInterrupt(DS3232RTC::ALARM_2, true);
      // } else {                                                                                       // Is a weekday
      //   myRTC.setAlarm(DS3232RTC::ALM2_MATCH_HOURS, WEEKDAY_WAKEUP_MINUTE, WEEKDAY_WAKEUP_HOUR, 1);  // Set alarm 2 to weekday wakeup time
      //   myRTC.alarm(DS3232RTC::ALARM_2);                                                             // ensure RTC interrupt flag is cleared
      //   myRTC.alarmInterrupt(DS3232RTC::ALARM_2, true);

      //   Serial.print("Alarm 2 SET to ");
      //   Serial.print(WEEKDAY_WAKEUP_HOUR);
      //   Serial.print(":");
      //   Serial.print(WEEKDAY_WAKEUP_MINUTE);
      //   Serial.println();
      // }

    } else if (myRTC.alarm(DS3232RTC::ALARM_2)) {  // Must be alarm 2, so change the color and then advance the state
                                                   // If state = nightlight, turn off the nightlight, turn on the wake up light, set state = wakeup, set alarm 2 = 8am
                                                   // If state = wakeup, turn off the wake up light, set state = daytime, set alarm 2 = 7pm
                                                   // If state = daytime, turn on nightlight, set state = nightlight, set alarm 2 = 7am

      Serial << millis() << F(" Alarm 2 went off at ") << timestamp << endl;

      Serial.print("The alarmState currently is: ");
      Serial.print(alarmState);
      Serial.println();

      // MAY NEED TO MOVE COLOR STUFF INTO HERE SINCE IT'S SKIPPING NIGHTLIGHT (edit: don't think so)


      if (alarmState == NIGHTLIGHT) {  // Set to turn off TURN_OFF_MINUTES minutes after turning on
        time_t t2 = myRTC.get();
        tmElements_t tm;
        breakTime(t2 + (60 * TURN_OFF_MINUTES), tm);
        tm.Second = 0;
        time_t a1 = makeTime(tm);
        // time_t a2 = a1 + 60;
        myRTC.setAlarm(DS3232RTC::ALM2_MATCH_HOURS, minute(a1), hour(a1), 1);
        myRTC.alarm(DS3232RTC::ALARM_2);  // ensure RTC interrupt flag is cleared
        myRTC.alarmInterrupt(DS3232RTC::ALARM_2, true);

        Serial.print("Now moving to GETTING READY state; Alarm 2 RESET to ");
        Serial.print(hour(a1));
        Serial.print(":");
        Serial.print(minute(a1));
        Serial.println();

      } else if (alarmState == GETTINGREADY) {  // Set the light to come back on at NIGHTLIGHT_ON_HOUR and NIGHTLIGHT_ON_MINUTE
        myRTC.setAlarm(DS3232RTC::ALM2_MATCH_HOURS, NIGHTLIGHT_ON_MINUTE, NIGHTLIGHT_ON_HOUR, 1);
        myRTC.alarm(DS3232RTC::ALARM_2);  // ensure RTC interrupt flag is cleared
        myRTC.alarmInterrupt(DS3232RTC::ALARM_2, true);

        Serial.print("Now moving to DAYTIME state; Alarm 2 RESET to ");
        Serial.print(NIGHTLIGHT_ON_HOUR);
        Serial.print(":");
        Serial.print(NIGHTLIGHT_ON_MINUTE);
        Serial.println();


      } else if (alarmState == DAYTIME) {  // (default setting) set the wakeup alarm to trigger at 7am
        myRTC.setAlarm(DS3232RTC::ALM2_MATCH_HOURS, 0, 7, 1);
        myRTC.alarm(DS3232RTC::ALARM_2);  // ensure RTC interrupt flag is cleared
        myRTC.alarmInterrupt(DS3232RTC::ALARM_2, true);

        Serial.println("Now moving to NIGHTLIGHT state; Alarm 2 RESET to 7:00 default");
      }
      changeColor();
    }
  }
}

void setAlarm2() {
  if (weekday() == 1 || weekday() == 7) {                                                        // Is a weekend (Day of the week (1-7), Sunday is day 1)
    myRTC.setAlarm(DS3232RTC::ALM2_MATCH_HOURS, WEEKEND_WAKEUP_MINUTE, WEEKEND_WAKEUP_HOUR, 1);  // Set alarm 2 to weekend wakeup time
    myRTC.alarm(DS3232RTC::ALARM_2);                                                             // ensure RTC interrupt flag is cleared
    myRTC.alarmInterrupt(DS3232RTC::ALARM_2, true);
  } else {                                                                                       // Is a weekday
    myRTC.setAlarm(DS3232RTC::ALM2_MATCH_HOURS, WEEKDAY_WAKEUP_MINUTE, WEEKDAY_WAKEUP_HOUR, 1);  // Set alarm 2 to weekday wakeup time
    myRTC.alarm(DS3232RTC::ALARM_2);                                                             // ensure RTC interrupt flag is cleared
    myRTC.alarmInterrupt(DS3232RTC::ALARM_2, true);

    Serial.print("Alarm 2 SET to ");
    Serial.print(WEEKDAY_WAKEUP_HOUR);
    Serial.print(":");
    Serial.print(WEEKDAY_WAKEUP_MINUTE);
    Serial.println();
  }
}


void changeColor() {
  Serial.println("In start of changecolor");
  if (alarmState == NIGHTLIGHT) {  // Switch to Wake Up Light
    Serial.println("In NIGHTLIGHT");
    // Fill entire strip with nothing
    strip.fill(strip.Color(0, 0, 0, 0));
    strip.show();

    // Fill entire strip with green
    strip.fill(strip.Color(0, 35, 0, 0));
    strip.show();

    alarmState = GETTINGREADY;
    return;

  } else if (alarmState == GETTINGREADY) {  // Switch to daytime
    Serial.println("In GETTINGREADY");
    // Fill entire strip with nothing
    strip.fill(strip.Color(0, 0, 0, 0));
    strip.show();

    alarmState = DAYTIME;
    return;

  } else if (alarmState == DAYTIME) {
    Serial.println("In DAYTIME");
    // Fill entire strip with nothing
    strip.fill(strip.Color(0, 0, 0, 0));
    strip.show();

    // Fill entire strip with purple
    strip.fill(strip.Color(7, 0, 13, 0));
    strip.show();

    alarmState = NIGHTLIGHT;
    return;
  }
}


void digitalClockDisplay() {
  // digital clock display of the time
  Serial.print(hour());
  printDigits(minute());
  printDigits(second());
  Serial.print(' ');
  Serial.print(day());
  Serial.print(' ');
  Serial.print(month());
  Serial.print(' ');
  Serial.print(year());
  Serial.println();
}

void printDigits(int digits) {
  // utility function for digital clock display: prints preceding colon and leading 0
  Serial.print(':');
  if (digits < 10)
    Serial.print('0');
  Serial.print(digits);
}



// format a time_t value, return the formatted string in buf (must be at least 25 bytes)
void formatTime(char *buf, time_t t) {
  char m[4];  // temporary storage for month string (DateStrings.cpp uses shared buffer)
  strcpy(m, monthShortStr(month(t)));
  sprintf(buf, "%.2d:%.2d:%.2d %s %.2d %s %d",
          hour(t), minute(t), second(t), dayShortStr(weekday(t)), day(t), m, year(t));
}