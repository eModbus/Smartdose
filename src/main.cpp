// Firmware for "Gosund SP1" and "Maxcio W-DE 004"-type smart sockets.
// Copyright 2020 by miq1@gmx.de
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
//   Config data permanently stored in LittleFS

// ========== Definitions =================
// Defines for the type of device
// Supported devices:
#define GOSUND_SP1 1
#define MAXCIO 2
// Set the device to be used
#define DEVICETYPE GOSUND_SP1

// Enable telnet server (port 23) for monitor outputs: 1=yes, 0=no
#define TELNET_LOG 1

// Enable Modbus server for energy monitor (available with a GOSUND_SP1 device only!): 1=yes, 0=no
#if DEVICETYPE == GOSUND_SP1
#define MODBUS_SERVER 1
#endif

// Library includes
#include <Arduino.h>
#include <ArduinoOTA.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include "fauxmoESP.h"
#include "LittleFS.h"
#if TELNET_LOG == 1
#include "TelnetLog.h"
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
// Energy monitor GPIOs
#define SEL_PIN 12
#define CF_PIN 4
#define CF1_PIN 5 
// Energy monitor settings
#define UPDATE_TIME 5000
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

// ================= No user definable values below this line ===================

// Operations modes
#define RUN 1
#define CONFIG 2

// maximum length of configuration parameters
#define PARMLEN 64

// Blink patterns for the different modes
// Wait for initial button press
#define KNOBBLINK 0x3333
// CONFIG mode:
#define CONFIGBLINK 0xCCC0
// Wifi connect mode:
#define WIFIBLINK 0xFF00

// File paths in LittleFS
const char SSIDFILE[] = "/conf/SSID";
const char PWDFILE[]  =  "/conf/PWD";
const char DEVFILE[]  =  "/conf/DEV";
const char OPWDFILE[] = "/conf/OTA";

// Some forward declarations
bool getOut(char *target, const char *readname);
bool fillIn(char *target, String arg, const char *writename);
void handleSave();
void handleRestart();
void handleRoot();
bool Debouncer(bool raw);
void SetState(uint8_t device_id, const char * device_name, bool state, uint8_t value);
void wifiSetup();
#if DEVICETYPE == GOSUND_SP1
void updateEnergy();
#if MODBUS_SERVER == 1
ModbusMessage FC03(ModbusMessage request);
ModbusMessage FC06(ModbusMessage request);
#endif
#endif

fauxmoESP fauxmo;             // create Philips Hue lookalike
bool Testschalter;            // Relay state
uint8_t dimValue;             // Hue dimmer value
ESP8266WebServer server(80);  // Web server on port 80
uint8_t mode;                 // Operations mode, RUN or CONFIG

#if DEVICETYPE == GOSUND_SP1
// Time between reads of the energy monitor im milliseconds
constexpr unsigned int update_interval = (UPDATE_TIME >= 2000) ? UPDATE_TIME : 2000;
// Number of intervals since system boot
unsigned long tickCount = 0;
// Read energy values
double volt = 0.0;                // Last read voltage value
double amps = 0.0;                // Last read current value
double watt = 0.0;                // Last read power value
// Uptime values
uint32_t hours = 0;
uint16_t minutes = 0;
uint16_t seconds = 0;
// Spent Watt hours (Wh) since system boot
double accumulatedWatts = 0.0;
// Calculation helpers for hours and minutes since system boot
constexpr unsigned int ticksPerHour = 3600000 / update_interval;
constexpr unsigned int ticksPerMinute = 60000 / update_interval;
#endif

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
TelnetLog tl(23, 2);
#endif

// Blinker: helper class to maintain blinking patterns for the LED
class Blinker {
public:
  // Constructor: takes GPIO of LED to handle
  explicit Blinker(uint8_t port) :
    B_port(port) {}
  
  // start: in interval steps, loop over blinking pattern
  uint32_t start(uint32_t interval, uint16_t pattern) {
    B_interval = interval;
    B_pattern = pattern;
    B_lastTick = millis();
    return B_lastTick + B_interval;
  }

  // stop: stop blinking
  void stop() {
    B_lastTick = 0;
    B_interval = 0;
    B_pattern = 0;
    B_counter = 0;
    digitalWrite(B_port, HIGH);
  }

  // update: check if the blinking pattern needs to be advanced a step
  void update() {
    // Do we have a valid interval?
    if (B_interval) {
      // Yes. Has it passed?
      if (millis() - B_lastTick > B_interval) {
        // Yes. do some blinkenlights!
        int state = digitalRead(B_port);            // get the current state of the LED pin
        int bite = ((1 << B_counter) & B_pattern);  // get the current bit of the blink pattern
        if (bite) { // if HIGH
          if (state) digitalWrite(B_port, LOW);     // switch LED ON, if was OFF
        } else { // if LOW
          if (!state) digitalWrite(B_port, HIGH);   // switch LED OFF, if was ON
        }
        B_counter++;                                // advance one bit
        B_counter &= 0xf;                           // catch overflow - counter runs 0-15
        B_lastTick = millis();
      }
    }
  }

protected:
  uint8_t B_counter;       // Number of bit currently processed
  uint8_t B_port;          // GPIO of the LED
  uint32_t B_lastTick;     // Last interval start time
  uint32_t B_interval;     // Length of interval in milliseconds
  uint16_t B_pattern;      // 16-bit blinking pattern
};

// SIGNAL_LED is the blinking one
Blinker SignalLed(SIGNAL_LED);

// -----------------------------------------------------------------------------
// Setup WiFi in RUN mode
// -----------------------------------------------------------------------------
void wifiSetup(const char *hostname) {
  // Start WiFi connection blinking pattern
  SignalLed.start(100, WIFIBLINK);
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
    digitalWrite(POWER_LED, LOW);
    digitalWrite(RELAY, HIGH);
  } else { // OFF
#if TELNET_LOG == 1
    LOG_I("Switch OFF\n");
#endif
    Testschalter = false;
    digitalWrite(POWER_LED, HIGH);
    digitalWrite(RELAY, LOW);
  }
  dimValue = value;
}

#if MODBUS_SERVER == 1
// -----------------------------------------------------------------------------
// FC03. React on Modbus read request
// -----------------------------------------------------------------------------
ModbusMessage FC03(ModbusMessage request) {
  static constexpr uint16_t maxMemory = 
    2 +                          // On/OFF state
    sizeof(hours) +              // hours since boot
    sizeof(minutes) +            // minutes ~
    sizeof(seconds) +            // seconds ~
    sizeof(watt) +               // W
    sizeof(accumulatedWatts) +   // Wh
    sizeof(volt) +               // V
    sizeof(amps)                 // A
    ; // NOLINT
  static ModbusMessage memory(maxMemory); // Temporary data storage
  ModbusMessage response;          // returned response message

  uint16_t address = 0;
  uint16_t words = 0;

  // Get start address and length for read
  request.get(2, address);
  request.get(4, words);

  // Valid?
  if (address && words && ((address + words - 1) < (maxMemory / 2)) && (words < 126)) {
    // Yes, both okay. Set up temporary memory
    // Delete previous content
    memory.clear();

    // Fill in current values
    memory.add((uint16_t)(Testschalter ? dimValue : 0));
    memory.add(hours);
    memory.add(minutes);
    memory.add(seconds);
    memory.add(watt);
    memory.add(accumulatedWatts);
    memory.add(volt);
    memory.add(amps);
    // set up response
    response.add(request.getServerID(), request.getFunctionCode(), (uint8_t)(words * 2));
    response.add(memory.data() + address - 1, words * 2);
  } else {
    // No, memory violation. Return error
    response.setError(request.getServerID(), request.getFunctionCode(), ILLEGAL_DATA_ADDRESS);
  }
  return response;
}

// -----------------------------------------------------------------------------
// FC06. Switch socket on or off 
// -----------------------------------------------------------------------------
ModbusMessage FC06(ModbusMessage request) {
  ModbusMessage response;
  uint16_t address = 0;
  uint16_t value = 0;

  request.get(2, address);
  request.get(4, value);

  // Address valid?
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
  } else {
    // No, memory violation. Return error
    response.setError(request.getServerID(), request.getFunctionCode(), ILLEGAL_DATA_ADDRESS);
  }
  return response;
}
#endif

// -----------------------------------------------------------------------------
// Setup. Find out which mode to run and initialize objects
// -----------------------------------------------------------------------------
void setup() {
  File f;

  Serial.begin(115200);
  Serial.println();
  Serial.println("_OK_");

  // Define GPIO input/output direction
  pinMode(SIGNAL_LED, OUTPUT);
  pinMode(POWER_LED, OUTPUT);
  pinMode(RELAY, OUTPUT);
  pinMode(BUTTON, INPUT);

  // initially switch to OFF
  digitalWrite(RELAY, LOW);        // Relay OFF
  digitalWrite(SIGNAL_LED, HIGH);  // LED OFF
  digitalWrite(POWER_LED, HIGH);   // LED OFF

#if TELNET_LOG == 1
  // Init telnet server
  tl.begin();
#endif

  // Open LittleFS.
  bool rc = LittleFS.begin();

  // Open failed or no formating registered?
  if (!rc || !LittleFS.exists("/fmtOK.txt")) {
    // YES. Light red LED and format LittleFS
    digitalWrite(SIGNAL_LED, LOW);
    LittleFS.format();

    // Now register format to do it only once
    f = LittleFS.open("/fmtOK.txt", "w");
    f.print("Format OK");
    f.close();

    // stop LED
    digitalWrite(SIGNAL_LED, HIGH);
  }

  // attempt to read all four configuration values. Count each successful read.
  uint8_t confcnt = 0;
  if (getOut(C_SSID, SSIDFILE)) confcnt++;
  if (getOut(C_PWD, PWDFILE)) confcnt++;
  if (getOut(DEVNAME, DEVFILE)) confcnt++;
  if (getOut(O_PWD, OPWDFILE)) confcnt++;

  // Determine operation mode - normally RUN
  mode = RUN;

  // we will wait 3s for button presses to deliberately enter CONFIG mode
  SignalLed.start(100, KNOBBLINK);
  uint32_t t0 = millis();

  while (millis() - t0 <= 3000) {
    SignalLed.update();
    // Button pressed?
    if (Debouncer(digitalRead(BUTTON))) {
      // YES. Force CONFIG mode and leave. 
      confcnt = 0;
      break;
    }
  }
  SignalLed.stop();

  // CONFIG mode required?
  if (confcnt<4) {
    // YES. Switch to CONFIG and start special blink pattern,
    mode = CONFIG;
    SignalLed.start(100, CONFIGBLINK);

    // Set up access point mode. First, create AP SSID from flash ID,
    char APssid[64];
    strcpy(APssid, "Socket_XXXXXX");
    long id = ESP.getChipId();
    for (uint8_t i=6; i; i--) {
      char c = (id & 0xf);
      if (c>9) { c -= 10; c += 'A'; }
      else    c += '0';
      APssid[6+i] = c;
      id >>= 4;
    }

    // Start AP.
    WiFi.softAP(APssid, "Maelstrom");

    // Register URLs for functions on web page.
    server.on("/", handleRoot);         // Main page
    server.on("/reset", handleRestart); // Reset button on page
    server.on("/save", handleSave);     // Save button on page

    // Start web server.
    server.begin();
  } else {
    // NO, RUN mode.
    // Connect. Will not terminate if not successful!!!
    wifiSetup(DEVNAME);

    digitalWrite(SIGNAL_LED, HIGH);   // make sure LED is OFF
    digitalWrite(POWER_LED, HIGH);    // make sure LED is OFF
    Testschalter = false;             // Assume relay is OFF

    // Fauxmo setup.
    fauxmo.createServer(true);        // Start server
    fauxmo.setPort(80);               // use HTML port 80
    fauxmo.enable(true);              // get visible.
    fauxmo.addDevice(DEVNAME);        // Set Hue name
    fauxmo.onSetState(SetState);      // link to switch callback
    fauxmo.setState(DEVNAME, false, 255);      // set OFF state

    // ArduinoOTA setup
    ArduinoOTA.setHostname(DEVNAME);  // Set OTA host name
    ArduinoOTA.setPassword((const char *)O_PWD);  // Set OTA password
    ArduinoOTA.begin();               // start OTA scan

#if DEVICETYPE == GOSUND_SP1
    // Configure energy meter GPIOs
    pinMode(CF_PIN, INPUT);
    pinMode(CF1_PIN, INPUT);
    pinMode(SEL_PIN, OUTPUT);

    tickCount = 0;
    accumulatedWatts = 0.0;

#if MODBUS_SERVER == 1
    // Register server functions to read and write data
    MBserver.registerWorker(1, READ_HOLD_REGISTER, &FC03);
    MBserver.registerWorker(1, WRITE_HOLD_REGISTER, &FC06);
    // Init and start Modbus server:
    // Listen on port 502 (MODBUS standard), maximum 2 clients, 2s timeout
    MBserver.start(502, 2, 2000);
#endif
#endif
  }
#if TELNET_LOG == 1
  LOGDEVICE = &tl;
  MBUlogLvl = LOG_LEVEL_VERBOSE;
  LOG_V("setup() finished.\n");
#endif
}

// -----------------------------------------------------------------------------
// Debounce routine for button. Gets digitalRead() as input.
// -----------------------------------------------------------------------------
bool Debouncer(bool raw) {
  static uint16_t State = 0;
  State = (State<<1)|(!raw)|0xE000;
  return(State==0xF000);
}

#if DEVICETYPE == GOSUND_SP1
void updateEnergy() {
  static unsigned long last = millis();    // time of last read
  static bool select = false;              // Toggle for voltage/current
  unsigned long int cf;                    // CF read value (power pulse length)
  unsigned long int cf1;                   // CF1 read value (voltage/current pulse length)

  // New read due?
  if ((millis() - last) > update_interval) {
    // Yes. Read pulse lengths. Note: current reading may take a long time (up to 2.5 seconds)
    cf1 = pulseIn(CF1_PIN, LOW, 1000000);   // Read voltage or current
    cf  = pulseIn(CF_PIN, LOW, 1000000);     // Read power

    // Calculate watts according the BL 0937 specs
    //              Vref^2       cycle length       v-specs     resistors
    // watt = cf ? (1483524.0 / (cf + HIGH_PULSE) / 1721506.0 * 2001000.0) : 0.0;
    watt = cf ? (1724380.585/(cf + HIGH_PULSE)) : 0.0;
    accumulatedWatts += watt * (millis() - last) / 3600000.0;

    // Did we read current?
    if (select) {
      // Yes. Calculate amps according to specs
      //               Vref         cycle length        v-specs   shunt
      // amps = cf1 ? (1218000.0 / (cf1 + HIGH_PULSE) / 94638.0 * 1000.0) : 0.0;
      amps = cf1 ? (12870.0/(cf1 + HIGH_PULSE)) : 0.0;
      // Toggle SEL pin to read the other value next time around
      digitalWrite(SEL_PIN, HIGH);
      select = false;
    } else {
      // No. Calculate volts according to specs
      //               Vref         cycle length        v-specs   resistors
      // volt = cf1 ? (1218000.0 / (cf1 + HIGH_PULSE) / 15397.0 * 2001.0) : 0.0;
      volt = cf1 ? (158299.656/(cf1 + HIGH_PULSE)) : 0.0;
      // Toggle SEL pin to read the other value next time around
      digitalWrite(SEL_PIN, LOW);
      select = true;
    }

    hours = tickCount / ticksPerHour;
    minutes = (tickCount / ticksPerMinute) % 60;
    seconds = ((tickCount * update_interval) / 1000) % 60;
#if TELNET_LOG == 1
    // Write a data line to the telnet client(s), if any
    tl.printf("%06d:%02d:%02d %c %6.2fV %8.2fW %5.2fA %8.2fWh\n", 
      hours,
      minutes, 
      seconds,
      select ? 'V' : 'A', 
      volt, 
      watt, 
      amps, 
      accumulatedWatts); 
#endif
    tickCount++;
    last = millis();
  }
}
#endif 

// -----------------------------------------------------------------------------
// Main loop
// -----------------------------------------------------------------------------
void loop() {
  // Check for OTA update requests
  ArduinoOTA.handle();

#if TELNET_LOG == 1
  // Handle telnet connections
  tl.update();
#endif

  // Update blinking LED, if any
  SignalLed.update();

  // RUN mode?
  if (mode == RUN) {
    // YES. Check Hue and button requests.
    fauxmo.handle();

    // If button pressed, toggle Relay/LED
    if (Debouncer(digitalRead(BUTTON))) {
      SetState(0, DEVNAME, !Testschalter, 255);
    }

#if DEVICETYPE == GOSUND_SP1
    // Read energy meter.
    updateEnergy();
#endif

  } else {
    // NO. CONFIG mode - listen to web requests only.
    server.handleClient();
  }
}

// -----------------------------------------------------------------------------
// handleRoot - bring out configuration page
// -----------------------------------------------------------------------------
void handleRoot() {
  // Setup HTML with embedded values from LittleFS - if any.
  String page = "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"utf-8\">" \
    "<title>Smart socket setup</title>" \
    "<style type=\"text/css\">" \
    "label { display: block; width: 200px; font-size: small; }" \
    "legend { font-weight: bold; }" \
    "input.button { width: 10em;  height: 3em; font-weight: bold; }" \
    "table {border: none;}" \
    "</style></head><body><h1>Socket setup</h1>" \
    "<form><fieldset style=\"background-color:#FFEFD5\"><legend>WiFi network</legend>" \
    "<label for=\"ssid\">SSID</label><input type=\"text\" id=\"ssid\" name=\"ssid\" maxlength=32 size=40 required value=\"" 
    + String(C_SSID) + // merge in home network SSID
    "\"><br/>" \
    "<label for=\"pwd\">Password</label><input type=\"password\" id=\"pwd\" name=\"pwd\" maxlength=32 size=40 value=\""
    + String(C_PWD) + // merge in home network password
    "\">" \
    "</fieldset><p/><fieldset style=\"background-color:#DCDCDC\"><legend>Device settings</legend>" \
    "<label for=\"device\">Device name</label><input type=\"text\" id=\"device\" name=\"device\" maxlength=32 size=40 pattern=\"[A-Za-z0-9_-]+\" required value=\""
    + String(DEVNAME) +  // merge in WeMo/OTA device name
    "\"><br/>" \
    "<label for=\"otapwd\">OTA Password</label><input type=\"text\" id=\"otapwd\" name=\"otapwd\" maxlength=32 size=40 value=\""
    + String(O_PWD) +  // merge in OTA password
    "\">" \
    "</fieldset><p/>" \
    "<input type=\"submit\" value=\"Save\" name=\"send\" formaction=\"/save\" class=\"button\" style=\"color:black;background-color:#32CD32\">" \
    "</form><p/><table><tr><td>ESP ID</td><td>"
    + String(ESP.getFlashChipId(), HEX) + 
    "</td></tr><tr><td>Speed</td><td>" 
    + String(ESP.getFlashChipSpeed()) + 
    "</td></tr><tr><td>Flash size</td><td>" 
    + String(ESP.getFlashChipRealSize()) + 
    "</td></tr><tr><td>Flash mode</td><td>" 
    + String(ESP.getFlashChipMode()) + 
    "</td></tr></table><p/><form>" \
    "<input type=\"submit\" value=\"Reset\" name=\"send\" formaction=\"/reset\" class=\"button\" style=\"color:white;background-color:#FF4500\">" \
    "</form></body></html>";

  // Send out page
  server.send(200, "text/html", page);
}

// -----------------------------------------------------------------------------
// handleRestart - reset device on web page request (user pressed Reset button)
// -----------------------------------------------------------------------------
void handleRestart() {
  ESP.restart();
}

// -----------------------------------------------------------------------------
// handleSave - get user input from web page and store in LittleFS and config parameters
// -----------------------------------------------------------------------------
void handleSave() {
  fillIn(C_SSID, server.arg("ssid"), SSIDFILE);
  fillIn(C_PWD, server.arg("pwd"), PWDFILE);
  fillIn(DEVNAME, server.arg("device"), DEVFILE);
  fillIn(O_PWD, server.arg("otapwd"), OPWDFILE);

  // re-display configuration web page
  handleRoot();
}

// -----------------------------------------------------------------------------
// fillIn - saves String arg into LittleFS and configuration parameter
// -----------------------------------------------------------------------------
bool fillIn(char *target, String arg, const char *writename) {
  File f;

  // No value? Bail out!
  if (!arg) return false;

  // get length of given parameter
  uint8_t len = arg.length();

  // Copy to parameter char array
  arg.toCharArray(target, PARMLEN);

  // Also write into LittleFS 
  f = LittleFS.open(writename, "w");
  if (f) {
    f.write((uint8_t *)target, len);
    f.close();
  }
  return true;
}

// -----------------------------------------------------------------------------
// getOut - read configuration parameter from LittleFS and fill in char array
// -----------------------------------------------------------------------------
bool getOut(char *target, const char *readname) {
  File f;
  
  *target = 0;                    // assume no data
  
  // Open LittleFS path
  f = LittleFS.open(readname, "r");
  if (f) { // valid file?
    // YES. get data length and read bytes into char array.
    uint8_t len = f.size();
    f.read((uint8_t *)target, len);
    f.close();

    // terminate char array with 0x00
    target[len] = 0;

    // leave OK
    return true;
  }

  // NO, no file found. Bail out!
  return false;
}

