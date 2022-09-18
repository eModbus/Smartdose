// Firmware for "Gosund SP1", "Maxcio W-DE 004" and "Sonoff S26"-type smart sockets.
// Copyright 2020-2022 by miq1@gmx.de
//
// Features:
// * Imitates a Philips Hue V1 hub and a lamp (courtesy of the fauxmoESP library: https://github.com/vintlabs/fauxmoESP
// * ON/OFF by pressing the button on the socket
// * ON state signalled by steady power LED
// * OTA-Update from Arduino IDE or Platformio
// * Config mode - started automatically on freshly setup devices or by pressing the button within 3s from system reset
//   WIFI access point SSID: "Socket_XXXXXX" with XXXXXX=ESP flash ID, password "Maelstrom"
//   Web page on http://192.168.4.1 for configuration
//   Configurable values:
//   * SSID for home network
//   * Password for home network
//   * Device ID for Hue, network host and OTA names
//   * OTA password
//   Config data permanently stored in EEPROM

// ========== Definitions =================
// Defines for the type of device
// Supported devices:
#define GOSUND_SP1 1
#define MAXCIO 2
#define SONOFF_S26 3
// Set the device to be used
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
#ifndef FAUXMO_ON
#define FAUXMO_ON 0
#endif

// Library includes
#include <Arduino.h>
#include <ArduinoOTA.h>
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#if FAUXMO_ON == 1
#include "fauxmoESP.h"
#endif
#include <EEPROM.h>
#include <time.h>
#include "Blinker.h"
#include "Buttoner.h"
#if TELNET_LOG == 1
#include "TelnetLogAsync.h"
#define LOCAL_LOG_LEVEL LOG_LEVEL_VERBOSE
#include "Logging.h"
#endif
#if MODBUS_SERVER == 1
#include "ModbusServerTCPasync.h"
#endif

// GPIO definitions
#if DEVICETYPE == GOSUND_SP1
#define RED_LED 13
#define BLUE_LED 1
#define RELAY 14
#define BUTTON 3
#define SIGNAL_LED RED_LED
#define POWER_LED BLUE_LED
#define MAXWORD 22

// Energy monitor GPIOs
#define SEL_PIN 12
#define CF_PIN 4
#define CF1_PIN 5 

// Energy monitor settings
#define HIGH_PULSE 38
#endif
#if DEVICETYPE == MAXCIO
// MAXCIO W-DE 004
#define LED 13
#define RELAY 14
#define BUTTON 1
#define SIGNAL_LED LED
#define POWER_LED LED
#define MAXWORD 8
#endif
#if DEVICETYPE == SONOFF_S26
// Sonoff S26 (R2)
#define LED 13
#define RELAY 12
#define BUTTON 0
#define SIGNAL_LED LED
#define MAXWORD 8
#endif

// Time between (energy) monitor updates in ms
#define UPDATE_TIME 5000

// NTP definitions
#ifndef MY_NTP_SERVER
#define MY_NTP_SERVER "fritz.box"
#endif
#ifndef MY_TZ
#define MY_TZ "CET-1CEST-2,M3.5.0/2:00,M10.5.0/3:00"
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
#define CONF_IS_GOSUND  0x8000
#define CONF_HAS_TELNET 0x4000
#define CONF_HAS_MODBUS 0x2000
#define CONF_HAS_FAUXMO 0x1000

// Blink patterns for the different modes
// Wait for initial button press
#define KNOBBLINK 0x3333
// CONFIG mode:
#define CONFIGBLINK 0xCCC0
// Wifi connect mode:
#define WIFIBLINK 0xFF00

// Serial output while config mode is up
#define CONFIG_TEST_OUTPUT 0

// Some forward declarations
bool Debouncer(bool raw);
void SetState(uint8_t device_id, const char * device_name, bool state, uint8_t value);
void wifiSetup();
void handleRoot();
void handleSave();
void handleRestart();
void handleNotFound();

#if DEVICETYPE == GOSUND_SP1
unsigned long int getFrequency();
void updateEnergy();
unsigned long int highPulse = HIGH_PULSE;
#endif

#if MODBUS_SERVER == 1
ModbusMessage FC03(ModbusMessage request);
ModbusMessage FC06(ModbusMessage request);
#if DEVICETYPE == GOSUND_SP1
ModbusMessage FC43(ModbusMessage request);
#endif
#endif

#if FAUXMO_ON == 1
fauxmoESP fauxmo;             // create Philips Hue lookalike
#endif
bool Testschalter;            // Relay state
uint8_t dimValue;             // Hue dimmer value
ESP8266WebServer server(80);    // Web server on port 80
uint8_t mode;                 // Operations mode, RUN or CONFIG
IPAddress myIP;               // local IP address
char APssid[64];              // Access point ID
uint16_t configFlags;         // 16 configuration flags
                              // 0x0001 : switch ON on boot

// The following are used only for GOSUND SP1, but will be initialized always for EEPROM
struct Measure {
  double measured;            // Observed value
  float factor;               // correction factor
  Measure() : measured(0.0), factor(1.0) {}
};

#define VOLTAGE 0
#define CURRENT 1
#define POWER   2
Measure measures[3];

#if DEVICETYPE == GOSUND_SP1
// Spent Watt hours (Wh) since system boot
double accumulatedWatts = 0.0;
// Counters and interrupt functions to sample meter frequency
volatile unsigned long int CF1tick = 0;
void IRAM_ATTR CF1Tick() { CF1tick++; }
volatile unsigned long int CF_tick = 0;
void IRAM_ATTR CF_Tick() { CF_tick++; }
#endif

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
// Number of intervals since system boot
unsigned long tickCount = 0;
// Time counters
TimeCount upTime;
TimeCount stateTime;
TimeCount onTime;

#if MODBUS_SERVER == 1
ModbusServerTCPasync MBserver;
ModbusMessage Response;
#endif

// char arrays for configuration parameters
char C_SSID[PARMLEN];
char C_PWD[PARMLEN];
char DEVNAME[PARMLEN];
char O_PWD[PARMLEN];

#if TELNET_LOG == 1
// Init Telnet logging: port 23, max. 2 concurrent clients, max. 3kB buffer
TelnetLog tl(23, 2, 3000);
#endif

// SIGNAL_LED is the blinking one
Blinker SignalLed(SIGNAL_LED, LOW);

// Button to watch
Buttoner button(BUTTON, LOW);

// -----------------------------------------------------------------------------
// Setup WiFi in RUN mode
// -----------------------------------------------------------------------------
void wifiSetup(const char *hostname) {
  // Start WiFi connection blinking pattern
  SignalLed.start(WIFIBLINK, 100);
  // Set WIFI module to STA mode
  WiFi.mode(WIFI_STA);

  // If we already have a device name, use it as hostname as well
  if (*hostname) {
    WiFi.hostname(hostname);
  }

  // Connect
  WiFi.begin(C_SSID, C_PWD);

  // Wait for connection. ==> We will hang here in RUN mode forever without a WiFi!
  while (WiFi.status() != WL_CONNECTED) {
    SignalLed.update();
    delay(50);
  }

  myIP = WiFi.localIP();
  
  // Start mDNS service
  if (*hostname) {
    MDNS.begin(hostname);
  }

  // Connected! Stop blinking
  SignalLed.stop();
}

// -----------------------------------------------------------------------------
// Change state of device ON<-->OFF
// -----------------------------------------------------------------------------
void SetState(uint8_t device_id, const char * device_name, bool state, uint8_t value) {
  if (state) { // ON
#if TELNET_LOG == 1
    LOG_I("Switch ON\n");
#endif
    Testschalter = true;
#if defined(POWER_LED)
    digitalWrite(POWER_LED, LOW);
#endif
    digitalWrite(RELAY, HIGH);
  } else { // OFF
#if TELNET_LOG == 1
    LOG_I("Switch OFF\n");
#endif
    Testschalter = false;
#if defined(POWER_LED)
    digitalWrite(POWER_LED, HIGH);
#endif
    digitalWrite(RELAY, LOW);
  }
  stateTime.reset();
  dimValue = value;
}

#if MODBUS_SERVER == 1
// -----------------------------------------------------------------------------
// FC03. React on Modbus read request
// -----------------------------------------------------------------------------
ModbusMessage FC03(ModbusMessage request) {
  static constexpr uint16_t maxMemory = 
    2                            // On/OFF state
    + 2                          // hours since boot
    + 1                          // minutes ~
    + 1                          // seconds ~
    + 2                          // hours in current state (ON/OFF)
    + 1                          // minutes ~
    + 1                          // seconds ~
    + 2                          // ON hours since boot
    + 1                          // minutes ~
    + 1                          // seconds ~
    // Following only for GOSUND_SP1
#if DEVICETYPE == GOSUND_SP1
    + 4                          // Wh
    + 4                          // W
    + 4                          // V
    + 4                          // A
#endif
    ; // NOLINT
  static ModbusMessage memory(maxMemory); // Temporary data storage
  ModbusMessage response;          // returned response message

  uint16_t address = 0;
  uint16_t words = 0;

  // Get start address and length for read
  request.get(2, address);
  request.get(4, words);

  // Valid?
  if (address && words && ((address + words - 1) <= MAXWORD) && (words < 126)) {
    // Yes, both okay. Set up temporary memory
    // Delete previous content
    memory.clear();

    // Fill in current values
    memory.add((uint16_t)(Testschalter ? dimValue : 0));
    uint16_t showFlags = configFlags & CONF_MASK;
#if DEVICETYPE == GOSUND_SP1
    showFlags |= CONF_IS_GOSUND;
#endif
#if TELNET_LOG == 1
    showFlags |= CONF_HAS_TELNET;
#endif
#if MODBUS_SERVER == 1
    showFlags |= CONF_HAS_MODBUS;
#endif
#if FAUXMO_ON == 1
    showFlags |= CONF_HAS_FAUXMO;
#endif
    memory.add(showFlags);
    memory.add(upTime.getHour());
    memory.add(upTime.getMinute());
    memory.add(upTime.getSecond());
    memory.add(stateTime.getHour());
    memory.add(stateTime.getMinute());
    memory.add(stateTime.getSecond());
    memory.add(onTime.getHour());
    memory.add(onTime.getMinute());
    memory.add(onTime.getSecond());
    // Following only for GOSUND_SP1
#if DEVICETYPE == GOSUND_SP1
    memory.add((float)accumulatedWatts);
    memory.add(measures[VOLTAGE].factor);
    memory.add(measures[CURRENT].factor);
    memory.add(measures[POWER].factor);
    memory.add((float)measures[VOLTAGE].measured);
    memory.add((float)measures[CURRENT].measured);
    memory.add((float)measures[POWER].measured);
#endif
    // set up response
    response.add(request.getServerID(), request.getFunctionCode(), (uint8_t)(words * 2));
    response.add(memory.data() + (address - 1) * 2, words * 2);
  } else {
    // No, memory violation. Return error
    response.setError(request.getServerID(), request.getFunctionCode(), ILLEGAL_DATA_ADDRESS);
  }
  return response;
}

// -----------------------------------------------------------------------------
// FC06. Switch socket on or off or change config values
// -----------------------------------------------------------------------------
ModbusMessage FC06(ModbusMessage request) {
  ModbusMessage response;
  uint16_t address = 0;
  uint16_t value = 0;

  request.get(2, address);
  request.get(4, value);

#if TELNET_LOG == 1
  LOG_D("Write %d: %d\n", address, value);
#endif

  // Address valid? Switch trigger on 1
  if (address == 1) {
    // Yes. Data in valid range?
    if (value < 256) {
      // Yes. switch socket
      SetState(0, DEVNAME, (value ? true : false), (uint8_t)value);
      response = ECHO_RESPONSE;
    } else {
      // No, illegal data value
      response.setError(request.getServerID(), request.getFunctionCode(), ILLEGAL_DATA_VALUE);
    }
  // Also okay: 2 - flag word
  } else if (address == 2) {
    // Write to EEPROM
    configFlags = value & CONF_MASK;
    EEPROM.put(2, value & CONF_MASK);
    EEPROM.commit();
    response = ECHO_RESPONSE;
#if DEVICETYPE == GOSUND_SP1
  // On the GOSUND_SP1 we may reset the accumulated power consumption on word 9
  } else if (address == 9) {
    // Value is zero?
    if (value == 0) {
      // Yes. Reset the counter
      accumulatedWatts = 0.0;
      response = ECHO_RESPONSE;
    } else {
      // No, illegal data value
      response.setError(request.getServerID(), request.getFunctionCode(), ILLEGAL_DATA_VALUE);
    }
#endif
  } else {
    // No, memory violation. Return error
    response.setError(request.getServerID(), request.getFunctionCode(), ILLEGAL_DATA_ADDRESS);
  }
  return response;
}

#if DEVICETYPE == GOSUND_SP1
// -----------------------------------------------------------------------------
// FC43. Power meter adjustment
// -----------------------------------------------------------------------------
ModbusMessage FC43(ModbusMessage request) {
  ModbusMessage response;
  uint8_t type = 0;           // 0:volts, 1:amps, 2:watts
  float value = 0.0;          // Real value sent in message

  // Read the type byte
  request.get(2, type);
  request.get(3, value);

#if TELNET_LOG == 1
  LOG_D("FC43 got type=%d, value=%f\n", (unsigned int)type, value);
#endif

  // Set default response
  response.setError(request.getServerID(), request.getFunctionCode(), SUCCESS);

  // Is it a valid type?
  if (type >=0 && type <=2) {
      // Yes. Write it.
      measures[type].factor = value;
      EEPROM.put(4 + 4 * type, value);
      EEPROM.commit();
  } else {
    response.setError(request.getServerID(), request.getFunctionCode(), ILLEGAL_DATA_VALUE);
  }
  return response;
}
#endif

#endif

// -----------------------------------------------------------------------------
// Setup. Find out which mode to run and initialize objects
// -----------------------------------------------------------------------------
void setup() {
  uint8_t confcnt = 0;     // count necessary config variables

  // Define GPIO input/output direction
  pinMode(SIGNAL_LED, OUTPUT);
#if defined(POWER_LED)
  pinMode(POWER_LED, OUTPUT);
#endif
  pinMode(RELAY, OUTPUT);

  // initially switch to OFF
  digitalWrite(RELAY, LOW);        // Relay OFF
  digitalWrite(SIGNAL_LED, HIGH);  // LED OFF
#if defined(POWER_LED)
  digitalWrite(POWER_LED, HIGH);   // LED OFF
#endif

  // Determine operation mode - normally RUN
  mode = RUN;

  // Configure EEPROM
  // EEPROM layout:
  //   0 : uint16_t magic value
  //   2 : uint16_t flag word
  //   4 : float32 Volts adjustment factor (Gosund SP1)
  //   8 : float32 Amperes adjustment factor (Gosund SP1)
  //  12 : float32 Watts adjustment factor (Gosund SP1)
  //  16 : char[PARMLEN] SSID
  //  16 + PARMLEN : char[PARMLEN] PASS
  //  16 + 2 * PARMLEN : char[PARMLEN] DEVICENAME
  //  16 + 3 * PARMLEN : char[PARMLEN] OTA_PWD
  EEPROM.begin(512);

  // Read magic value
  uint16_t magic;
  EEPROM.get(0, magic);

  // Is it valid (EEPROM was initialized before)?
  if (magic != 0x4711) {
    // No, we need to do it first.
    // Init control values
    EEPROM.put(2, (uint16_t)0x0000);    // flags
    EEPROM.put(4, measures[VOLTAGE].factor);          // Adjustment factor volts
    EEPROM.put(8, measures[CURRENT].factor);          // Adjustment factor amperes
    EEPROM.put(12, measures[POWER].factor);           // Adjustment factor watts
    // Init char variable space
    for (uint16_t i = 16; i < 512; ++i) {
      EEPROM.write(i, 0);
    }
    // Put in magic value to stamp EEPROM valid
    EEPROM.put(0, 0x4711);
    // Write data
    EEPROM.commit();
  } else {
    // Magic value is okay - read config data
    EEPROM.get(2, configFlags);
    EEPROM.get(4, measures[VOLTAGE].factor);          // Adjustment factor volts
    EEPROM.get(8, measures[CURRENT].factor);          // Adjustment factor amperes
    EEPROM.get(12, measures[POWER].factor);           // Adjustment factor watts
    uint16_t addr = 16;
    strncpy(C_SSID, (const char *)EEPROM.getConstDataPtr() + addr, PARMLEN);
    if (EEPROM[addr]) confcnt++;
    addr += PARMLEN;
    strncpy(C_PWD, (const char *)EEPROM.getConstDataPtr() + addr, PARMLEN);
    if (EEPROM[addr]) confcnt++;
    addr += PARMLEN;
    strncpy(DEVNAME, (const char *)EEPROM.getConstDataPtr() + addr, PARMLEN);
    if (EEPROM[addr]) confcnt++;
    addr += PARMLEN;
    strncpy(O_PWD, (const char *)EEPROM.getConstDataPtr() + addr, PARMLEN);
    if (EEPROM[addr]) confcnt++;
  }

  // we will wait 3s for button presses to deliberately enter CONFIG mode
  SignalLed.start(KNOBBLINK, 100);
  uint32_t t0 = millis();

  while (millis() - t0 <= 3000) {
    SignalLed.update();
    button.update();
    // Button pressed?
    if (button.getEvent() != BE_NONE) {
      // YES. Force CONFIG mode and leave. 
      confcnt = 0;
      break;
    }
  }
  SignalLed.stop();

  // Start NTP
  configTime(MY_TZ, MY_NTP_SERVER); 

  // Create AP SSID from flash ID,
  strcpy(APssid, "Socket_XXXXXX");
  long id = ESP.getChipId();
  for (uint8_t i = 6; i; i--) {
    char c = (id & 0xf);
    if (c > 9) { c -= 10; c += 'A'; }
    else    c += '0';
    APssid[6 + i] = c;
    id >>= 4;
  }

  // CONFIG mode required?
  if (confcnt<4) {
    // YES. Switch to CONFIG and start special blink pattern,
    mode = CONFIG;
    SignalLed.start(CONFIGBLINK, 100);
#if CONFIG_TEST_OUTPUT == 1
    Serial.begin(115200);
    Serial.println();
    Serial.println("__OK__");
#endif

    // Start AP.
    WiFi.softAP(APssid, "Maelstrom");

    // Register URLs for functions on web page.
    server.on("/", handleRoot);         // Main page
    server.on("/reset", handleRestart); // Reset button on page
    server.on("/save", handleSave);     // Save button on page
    server.onNotFound(handleNotFound);  // Illegal request

    // Start web server.
    server.begin();
  } else {
    // NO, RUN mode.
    // Connect. Will not terminate if not successful!!!
    wifiSetup(DEVNAME);

    digitalWrite(SIGNAL_LED, HIGH);   // make sure LED is OFF
#if defined(POWER_LED)
    digitalWrite(POWER_LED, HIGH);    // make sure LED is OFF
#endif
    Testschalter = false;             // Assume relay is OFF

#if FAUXMO_ON == 1
    // Fauxmo setup.
    fauxmo.createServer(true);        // Start server
    fauxmo.setPort(80);               // use HTML port 80
    fauxmo.enable(true);              // get visible.
    fauxmo.addDevice(DEVNAME);        // Set Hue name
    fauxmo.onSetState(SetState);      // link to switch callback
    fauxmo.setState(DEVNAME, false, (uint8_t)255);      // set OFF state
#endif

    // ArduinoOTA setup
    ArduinoOTA.setHostname(DEVNAME);  // Set OTA host name
    ArduinoOTA.setPassword((const char *)O_PWD);  // Set OTA password
    ArduinoOTA.begin();               // start OTA scan

#if DEVICETYPE == GOSUND_SP1
    // Configure energy meter GPIOs
    pinMode(CF_PIN, INPUT_PULLUP);
    pinMode(CF1_PIN, INPUT_PULLUP);
    pinMode(SEL_PIN, OUTPUT);
    digitalWrite(SEL_PIN, HIGH);

    accumulatedWatts = 0.0;

    attachInterrupt(digitalPinToInterrupt(CF1_PIN), CF1Tick, RISING);
    attachInterrupt(digitalPinToInterrupt(CF_PIN), CF_Tick, RISING);

#endif

#if MODBUS_SERVER == 1
    // Register server functions to read and write data
    MBserver.registerWorker(1, READ_HOLD_REGISTER, &FC03);
    MBserver.registerWorker(1, WRITE_HOLD_REGISTER, &FC06);
#if DEVICETYPE == GOSUND_SP1
    MBserver.registerWorker(1, USER_DEFINED_43, &FC43);
#endif
    // Init and start Modbus server:
    // Listen on port 502 (MODBUS standard), maximum 2 clients, 2s timeout
    MBserver.start(502, 2, 2000);
#endif
    
    upTime.start(update_interval);
    stateTime.start(update_interval);
    onTime.start(update_interval);
  }
#if TELNET_LOG == 1
  // Init telnet server
  MBUlogLvl = LOG_LEVEL_VERBOSE;
  LOGDEVICE = &tl;
  char buffer[64];

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
  snprintf(buffer, 64, "%s (%s)", DEVNAME, APssid);
#pragma GCC diagnostic pop

  tl.begin(buffer);
#endif

  // Default ON?
  if (configFlags & 0x0001) {
    SetState(0, DEVNAME, true, 255);
  }
}

#if DEVICETYPE == GOSUND_SP1
// getFrequency: blocking function to sample power meter data
void getFrequency(unsigned long int& cf, unsigned long int& cf1) {
  // Disable interrupts
  cli();
  // Init counters
  CF_tick = 0;
  CF1tick = 0;
  // Enable interrupts
  sei();
  // count for 1000 msec
  delay(1000);
  // Disable interrupts
  cli();
  // save counters
  cf = CF_tick;
  cf1 = CF1tick;
  // Enable interrupts
  sei();
}

void updateEnergy() {
  static bool select = false;              // Toggle for voltage/current
  unsigned long int cf;                    // CF read value (power pulse length)
  unsigned long int cf1;                   // CF1 read value (voltage/current pulse length)

  // Read pulse lengths. Note: reading takes 1s blocking time!
  getFrequency(cf, cf1);

  // Calculate watts according the BL 0937 specs
  measures[POWER].measured = cf ? (cf * 1.218 * 1.218 * 2.0) / 1.721506 * measures[POWER].factor : 0.0;

  // Did we read current?
  if (select) {
    // Yes. Calculate amps according to specs
    measures[CURRENT].measured = cf1 ? ((cf1 * 1.218) / 94638.0 * 1000.0) * measures[CURRENT].factor : 0.0;
    // Toggle SEL pin to read the other value next time around
    digitalWrite(SEL_PIN, HIGH);
    select = false;
  } else {
    // No. Calculate volts according to specs
    measures[VOLTAGE].measured = cf1 ? ((cf1 * 1.218) / 15397.0 * 2001.0) * measures[VOLTAGE].factor : 0.0;
    // Toggle SEL pin to read the other value next time around
    digitalWrite(SEL_PIN, LOW);
    select = true;
  }
}
#endif 

// -----------------------------------------------------------------------------
// Main loop
// -----------------------------------------------------------------------------
void loop() {
#if TELNET_LOG == 1
  static uint8_t oneTime = 8;
#endif

  static uint32_t last = millis();
  // Check for OTA update requests
  ArduinoOTA.handle();

  // Update blinking LED, if any
  SignalLed.update();

  // Update button state
  button.update();

  // RUN mode?
  if (mode == RUN) {
#if FAUXMO_ON == 1
    // YES. Check Hue requests.
    fauxmo.handle();
#endif
    
    // Keep mDNS running
    MDNS.update();

    // If button pressed, toggle Relay/LED
    if (button.getEvent() != BE_NONE) {
      SetState(0, DEVNAME, !Testschalter, 255);
    }

    // New read due?
    if ((millis() - last) > update_interval) {
#if TELNET_LOG == 1
      if (oneTime) {
        oneTime--;
        if (!oneTime) {
          HEXDUMP_V("EEPROM", EEPROM.getConstDataPtr(), EEPROM.length());
        }
      }
#endif
#if DEVICETYPE == GOSUND_SP1
      unsigned long int calcLast = millis() - last;
#endif
      last = millis();
#if DEVICETYPE == GOSUND_SP1
      // Read energy meter.
      updateEnergy();
      accumulatedWatts += measures[POWER].measured * calcLast / 3600000.0;
#endif
      // Count up timers
      upTime.count();
      stateTime.count();
      // onTime only counted for switch state == ON
      // GOSUND_SP1 devices additionally will watch current to state ON
      if (Testschalter) { 
#if DEVICETYPE == GOSUND_SP1
        if (measures[CURRENT].measured > 0.0)
#endif
        onTime.count(); 
      }

      // Check WiFi connection
      if (WiFi.status() != WL_CONNECTED) {
        // No connection - reconnect
        WiFi.disconnect();
        WiFi.persistent(false);
        WiFi.mode(WIFI_OFF);
        WiFi.mode(WIFI_STA);
        WiFi.begin(C_SSID, C_PWD);
        delay(100);
      }

#if TELNET_LOG == 1
      // Output only if a client is connected
      if (tl.isActive()) {
        time_t now = time(NULL);
        tm tm;
        localtime_r(&now, &tm);           // update the structure tm with the current time
        // Write data to the telnet client(s), if any
        tl.printf("%02d:%02d:%02d %3s %d:%02d:%02d | Run %d:%02d:%02d | ON %d:%02d:%02d\n",
          tm.tm_hour,
          tm.tm_min,
          tm.tm_sec,
          Testschalter ? "ON" : "OFF",
          stateTime.getHour(),
          stateTime.getMinute(),
          stateTime.getSecond(),
          upTime.getHour(),
          upTime.getMinute(),
          upTime.getSecond(),
          onTime.getHour(),
          onTime.getMinute(),
          onTime.getSecond());
#if DEVICETYPE == GOSUND_SP1
        tl.printf("   | %6.2f V| %8.2f W| %5.2f A| %8.2f Wh|\n", 
          measures[VOLTAGE].measured, 
          measures[POWER].measured, 
          measures[CURRENT].measured, 
          accumulatedWatts); 
#endif
      }
#endif
    }
  // No, config mode. Keep the web server active
  } else {
    server.handleClient();
  }
}

// -----------------------------------------------------------------------------
// handleRoot - bring out configuration page
// -----------------------------------------------------------------------------
void handleRoot() {
#if CONFIG_TEST_OUTPUT == 1
  Serial.println("root request");
#endif
  // Setup HTML with embedded values from EEPROM - if any.
  PGM_P HTTP_HEAD  = PSTR("<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"utf-8\">" \
    "<title>Smart socket setup</title>" \
    "<style type=\"text/css\">" \
    "label { display: block; width: 200px; font-size: small; }" \
    "legend { font-weight: bold; }" \
    "input.button { width: 10em;  height: 3em; font-weight: bold; }" \
    "table {border: none;}" \
    "</style></head><body><h1>Socket setup</h1>" \
    "<form><fieldset style=\"background-color:#FFEFD5\"><legend>WiFi network</legend>" \
    "<label for=\"ssid\">SSID</label><input type=\"text\" id=\"ssid\" name=\"ssid\" maxlength=32 size=40 required value=\"");
  String page = FPSTR(HTTP_HEAD)
    + String(C_SSID); // merge in home network SSID
  PGM_P M1 = PSTR("\"><br/>" \
    "<label for=\"pwd\">Password</label><input type=\"password\" id=\"pwd\" name=\"pwd\" maxlength=32 size=40 value=\"");
  page += FPSTR(M1)
    + String(C_PWD); // merge in home network password
  PGM_P M2 = PSTR("\">" \
    "</fieldset><p/><fieldset style=\"background-color:#DCDCDC\"><legend>Device settings</legend>" \
    "<label for=\"device\">Device name</label><input type=\"text\" id=\"device\" name=\"device\" maxlength=32 size=40 pattern=\"[A-Za-z0-9_-]+\" required value=\"");
  page += FPSTR(M2) + String(DEVNAME);  // merge in WeMo/OTA device name
  PGM_P M3 = PSTR("\"><br/>" \
    "<label for=\"otapwd\">OTA Password</label><input type=\"text\" id=\"otapwd\" name=\"otapwd\" maxlength=32 size=40 value=\"");
  page += FPSTR(M3) + String(O_PWD);  // merge in OTA password
  PGM_P M4 = PSTR("\">" \
    "</fieldset><p/>" \
    "<input type=\"submit\" value=\"Save\" name=\"send\" formaction=\"/save\" class=\"button\" style=\"color:black;background-color:#32CD32\">" \
    "</form><p/><table><tr><td>ESP ID</td><td>");
  page += FPSTR(M4) + String(ESP.getFlashChipId(), HEX);
  PGM_P M5 = PSTR("</td></tr><tr><td>Speed</td><td>" );
  page += FPSTR(M5) + String(ESP.getFlashChipSpeed());
  PGM_P M6 = PSTR("</td></tr><tr><td>Flash size</td><td>");
  page += FPSTR(M6) + String(ESP.getFlashChipRealSize());
  PGM_P M7 =PSTR("</td></tr><tr><td>Flash mode</td><td>");
  page += FPSTR(M7) + String(ESP.getFlashChipMode());
  PGM_P HTTP_TAIL = PSTR("</td></tr></table><p/><form>" \
    "<input type=\"submit\" value=\"Reset\" name=\"send\" formaction=\"/reset\" class=\"button\" style=\"color:white;background-color:#FF4500\">" \
    "</form></body></html>");
  page += FPSTR(HTTP_TAIL);

  // Send out page
  server.send(200, "text/html", page);
}

// -----------------------------------------------------------------------------
// handleRestart - reset device on web page request (user pressed Reset button)
// -----------------------------------------------------------------------------
void handleRestart() {
#if CONFIG_TEST_OUTPUT == 1
  Serial.println("restart request");
#endif
  ESP.restart();
}

// -----------------------------------------------------------------------------
// handleSave - get user input from web page and store in EEPROM and config parameters
// -----------------------------------------------------------------------------
void handleSave() {
#if CONFIG_TEST_OUTPUT == 1
  Serial.println("save request");
#endif
  // Get user inputs
  String m;
  m = server.arg("ssid");
  m.toCharArray(C_SSID, PARMLEN);
  m = server.arg("pwd");
  m.toCharArray(C_PWD, PARMLEN);
  m = server.arg("device");
  m.toCharArray(DEVNAME, PARMLEN);
  m = server.arg("otapwd");
  m.toCharArray(O_PWD, PARMLEN);

  // Write to EEPROM
  uint16_t addr = 16;
  strncpy((char *)EEPROM.getDataPtr() + addr, C_SSID, PARMLEN);
  addr += PARMLEN;
  strncpy((char *)EEPROM.getDataPtr() + addr, C_PWD, PARMLEN);
  addr += PARMLEN;
  strncpy((char *)EEPROM.getDataPtr() + addr, DEVNAME, PARMLEN);
  addr += PARMLEN;
  strncpy((char *)EEPROM.getDataPtr() + addr, O_PWD, PARMLEN);
  EEPROM.commit();

  // re-display configuration web page
  handleRoot();
}

void handleNotFound() {
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++) { message += " " + server.argName(i) + ": " + server.arg(i) + "\n"; }
  server.send(404, "text/plain", message);
#if CONFIG_TEST_OUTPUT == 1
  Serial.println("illegal request");
  Serial.println(message);
#endif
}
