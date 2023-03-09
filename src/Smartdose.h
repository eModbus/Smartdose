// Firmware for "Gosund SP1", "Maxcio W-DE 004", "Nous A1T" and "Sonoff S26"-type smart sockets.
// Copyright 2020-2023 by miq1@gmx.de
//
// ========== Definitions =================
// Defines for the type of device
// Supported devices:
#define GOSUND_SP1 1
#define MAXCIO 2
#define SONOFF_S26 3
#define NOUS_A1T 4
// Set the device to be used
// Default is a Maxcio device with minimum functionality
#ifndef DEVICETYPE
#define DEVICETYPE MAXCIO
#endif

// Enable telnet server (port 23) for monitor outputs: 1=yes, 0=no
#ifndef TELNET_LOG
#define TELNET_LOG 0
#endif

// Enable Modbus server for monitoring runtime data (energy values available with a GOSUND_SP1 device only!): 1=yes, 0=no
#ifndef MODBUS_SERVER
#define MODBUS_SERVER 0
#endif

// Disable Fauxmo for Alexa ignorance ;)
#ifndef FAUXMO_ACTIVE
#define FAUXMO_ACTIVE 0
#endif

// Disable (debugging) event tracking
#ifndef EVENT_TRACKING
#define EVENT_TRACKING 0
#endif

// Enable timer functions: 1=yes, 0=no
#ifndef TIMERS
#define TIMERS 0
#endif
// Timers will need the Modbus server to work
#if TIMERS == 1 || EVENT_TRACKING == 1
#undef MODBUS_SERVER
#define MODBUS_SERVER 1
#endif

// Library includes
#include <Arduino.h>
#include <ArduinoOTA.h>
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#if FAUXMO_ACTIVE == 1
#include "fauxmoESP.h"
#endif
#include <EEPROM.h>
#include <time.h>
#include "Blinker.h"
#include "Buttoner.h"
#if TELNET_LOG == 1
#include "TelnetLogAsync.h"
#include "Logging.h"
#endif
#if MODBUS_SERVER == 1
#include "ModbusServerTCPasync.h"
#endif
#if EVENT_TRACKING == 1
#include "RingBuf.h"
#endif

// GPIO definitions
#if DEVICETYPE == GOSUND_SP1
#define RED_LED 13
#define BLUE_LED 1
#define RELAY 14
#define BUTTON 3
#define SIGNAL_LED RED_LED
#define POWER_LED BLUE_LED
// Energy monitor GPIOs
#define SEL_PIN 12
#define CF_PIN 4
#define CF1_PIN 5 
// Energy monitor settings
#define HASPOWERMETER 1
#define HIGH_PULSE 38
#endif
#if DEVICETYPE == MAXCIO
// MAXCIO W-DE 004
#define LED 13
#define RELAY 14
#define BUTTON 1
#define SIGNAL_LED LED
#define POWER_LED LED
#endif
#if DEVICETYPE == SONOFF_S26
// Sonoff S26 (R2)
#define LED 13
#define RELAY 12
#define BUTTON 0
#define SIGNAL_LED LED
#endif
#if DEVICETYPE == NOUS_A1T
#define LED 13
#define RELAY 14
#define BUTTON 0
#define SIGNAL_LED LED
#define POWER_LED LED
// Energy monitor GPIOs
#define SEL_PIN 12
#define CF_PIN 4
#define CF1_PIN 5 
// Energy monitor settings
#define HASPOWERMETER 1
#define HIGH_PULSE 38
#endif

// Disable power meter functions if not explicitly set before
#ifndef HASPOWERMETER
#define HASPOWERMETER 0
#endif

// Time between (energy) monitor updates in ms
#define UPDATE_TIME 5000

// Time between timer checks - must be below 1 minute to not let pass a timer unnoticed!
#define TIMER_UPDATE_INTERVAL 40000

// NTP definitions
#ifndef MY_NTP_SERVER
#define MY_NTP_SERVER "pool.ntp.org"
#endif
#ifndef MY_TZ
#define MY_TZ "CET"
#endif

// ================= No user definable values below this line ===================

// Operations modes
#define RUN 1
#define CONFIG 2

// maximum length of configuration parameters
#define PARMLEN 64

// Config flags
#define CONF_DEFAULT_ON 0x0001
#define CONF_MASK       0x0001
#define CONF_HAS_POWER  0x8000
#define CONF_HAS_TELNET 0x4000
#define CONF_HAS_MODBUS 0x2000
#define CONF_HAS_FAUXMO 0x1000
#define CONF_TIMERS     0x0800

// Blink patterns for the different modes
// Wait for initial button press
#define KNOBBLINK 0x3333
// CONFIG mode:
#define CONFIGBLINK 0xCCC0
// Wifi connect mode:
#define WIFIBLINK 0xFF00

// Serial output while config mode is up
#define CONFIG_TEST_OUTPUT 0

#if TIMERS == 1
// EEPROM offsets etc. for timer data
constexpr uint16_t O_TIMERS = 16 + 4 * PARMLEN;
#define ACTIVEMASK 0x80
#define DAYMASK    0x7F
#define ONMASK     0x01
#endif
// Number of timers is needed in any case (memory layout)
#define NUM_TIMERS 16

// Struct for timers
struct Timer_t {
  uint8_t activeDays;         // Bit 0..6: days of week, bit 7: active/inactive flag
  uint8_t onOff;              // Bit 0: 1=timer switches on, 0=switches off
  uint8_t hour;               // HH24 hour of switching time
  uint8_t minute;             // MM minutes of switching time
  Timer_t() :
    activeDays(0),
    onOff(0),
    hour(0),
    minute(0) { }
};

#if HASPOWERMETER == 1
// EEPROM offset to auto power off data
constexpr uint16_t O_AUTO_PO = 16 + 4 * PARMLEN + NUM_TIMERS * sizeof(Timer_t);
#endif

// Forward declarations
bool Debouncer(bool raw);
void SetState(uint8_t device_id, const char * device_name, bool state, uint8_t value);
void SetState_F(uint8_t device_id, const char * device_name, bool state, uint8_t value);
void wifiSetup(const char *hostname);
void handleRoot();
void handleSave();
void handleRestart();
void handleNotFound();

#if HASPOWERMETER == 1
  unsigned long int getFrequency();
  void updateEnergy();
#endif

// Number of event slots
const uint8_t MAXEVENT(40);

#if MODBUS_SERVER == 1
  // Highest addressable Modbus register
  // 8 basic data
  // 14 power measure data
  // NUM_TIMERS * 2 timer data
  // MAXEVENT event slots + 1 slot count
  // 2 auto power off control values
  constexpr uint16_t MAXWORD(22 + NUM_TIMERS * 2 + MAXEVENT + 1 + 2);

  ModbusMessage FC03(ModbusMessage request);
  ModbusMessage FC06(ModbusMessage request);
  #if HASPOWERMETER == 1
    ModbusMessage FC43(ModbusMessage request);
  #endif
  #if TIMERS == 1
    ModbusMessage FC10(ModbusMessage request);
  #endif
#endif

// The following are used only for power meter devices, but will be initialized always for EEPROM
struct Measure {
  double measured;            // Observed value
  float factor;               // correction factor
  Measure() : measured(0.0), factor(1.0) {}
};

#define VOLTAGE 0
#define CURRENT 1
#define POWER   2

// TimeCount: class to hold time passed
class TimeCount {
public:
  TimeCount() : interval_(0), counter_(0) { }
  void start(uint32_t interval) {
    interval_ = interval;
    ticksPerHour_ = 3600000 / interval;
    ticksPerMinute_ = 60000 / interval;
  }
  void count() {
    if (interval_) counter_++;
  }
  uint16_t getHour() {
    return interval_ ? (uint16_t)(counter_ / ticksPerHour_) : 0;
  }
  uint8_t getMinute() {
    return interval_ ? (uint8_t)((counter_ / ticksPerMinute_) % 60) : 0;
  }
  uint8_t getSecond() {
    return interval_ ? (uint8_t)(((counter_ * interval_) / 1000) % 60) : 0;
  }
  void reset() {
    counter_ = 0;
  }
protected:
  uint32_t interval_;
  uint32_t counter_;
  uint32_t ticksPerHour_;
  uint32_t ticksPerMinute_;
};

// Time between reads of the runtime data monitor im milliseconds
// Less than 2000ms may end up in stalling the device!
constexpr unsigned int update_interval = (UPDATE_TIME >= 2000) ? UPDATE_TIME : 2000;

#if TELNET_LOG == 1
// Init Telnet logging: port 23, max. 2 concurrent clients, max. 3kB buffer
TelnetLog tl(23, 2, 3000);
#endif

#if EVENT_TRACKING == 1
// Define the event types
enum S_EVENT : uint8_t  { 
  NO_EVENT=0, DATE_CHANGE,
  BOOT_DATE, BOOT_TIME, 
  DEFAULT_ON,
  BUTTON_ON, BUTTON_OFF,
  MODBUS_ON, MODBUS_OFF,
  TIMER_ON, TIMER_OFF,
  FAUXMO_ON, FAUXMO_OFF,
  WIFI_DISCONN, WIFI_CONN, WIFI_LOST,
  AUTOOFF,
};
const char *eventname[] = { 
  "no event", "date change", "boot date", "boot time", "default on",
  "button on", "button off", 
  "Modbus on", "Modbus off", 
  "timer on", "timer off", 
  "Fauxmo on", "Fauxmo off", 
  "WiFi disconn", "WiFi connected", "WiFi lost",
  "Low power auto off",
};
#define EVENT(x) registerEvent(x)

// Allocate event buffer
RingBuf<uint16_t> events(MAXEVENT);

// registerEvent: append another event to the buffer
void registerEvent(S_EVENT ev) {
  // We will need date and/or time
  time_t now = time(NULL);
  tm tm;
  localtime_r(&now, &tm);           // update the structure tm with the current time
  uint16_t eventWord = NO_EVENT << 11;
  uint8_t hi = 0;
  uint8_t lo = 0;
  
 // Set the event word
  if (ev == BOOT_DATE || ev == DATE_CHANGE) {
    // Need the date
    hi = tm.tm_mday & 0x1F;
    lo = (tm.tm_mon + 1) & 0x3F;
  } else {
    // Need the time
    hi = tm.tm_hour & 0x1F;
    lo = tm.tm_min & 0x3F;
  }
  eventWord = ((ev & 0x1F) << 11) | (hi << 6) | lo;

  // Prevent duplicates - last event must differ
  if (events[events.size() - 1] != eventWord) {
    // Push the word
    events.push_back(eventWord);
  }

#if TELNET_LOG == 1
  // Log event
  tl.printf("Event: %-20s %02d%c%02d\n", eventname[ev], hi, (ev == BOOT_DATE || ev == DATE_CHANGE) ? '.' : ':', lo);
#endif
}
#else
#define EVENT(x)
#endif 
