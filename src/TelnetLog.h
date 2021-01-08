// =================================================================================================
// eModbus: Copyright 2020 by Michael Harwerth, Bert Melis and the contributors to eModbus
//               MIT license - see license.md for details
// =================================================================================================

// Include Arduino.h to make Print and Serial known
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>

#define USE_TEMPLATES 1

class TelnetLog : public Print {
public:
  TelnetLog(uint16_t port, uint8_t maxClients);
  ~TelnetLog();
  void begin();
  void end();
  void update();
  size_t write(uint8_t c);

#if defined(USE_TEMPLATES)
  // Wrapper for all write() variants in Print
  template <typename... Args>
  size_t write(Args&&... args) { // NOLINT
    size_t len = 0;
    usedTemplate = 1;
    // Loop over clients
    for (uint8_t i = 0; i < TL_maxClients; ++i) {
      // Is it active?
      if (TL_Client[i] || TL_Client[i].connected()) {
        // Yes. print out line
        len = TL_Client[i].write(std::forward<Args>(args) ...);
        TL_Client[i].flush();
      }
    }
    return len;
  }

  // Wrapper for all print() variants in Print
  template <typename... Args>
  size_t print(Args&&... args) { // NOLINT
    size_t len = 0;
    usedTemplate = 2;
    // Loop over clients
    for (uint8_t i = 0; i < TL_maxClients; ++i) {
      // Is it active?
      if (TL_Client[i] || TL_Client[i].connected()) {
        // Yes. print out line
        len = TL_Client[i].print(std::forward<Args>(args) ...);
        TL_Client[i].flush();
      }
    }
    return len;
  }

  // Wrapper for all println() variants in Print
  template <typename... Args>
  size_t println(Args&&... args) { // NOLINT
    size_t len = 0;
    usedTemplate = 3;
    // Loop over clients
    for (uint8_t i = 0; i < TL_maxClients; ++i) {
      // Is it active?
      if (TL_Client[i] || TL_Client[i].connected()) {
        // Yes. print out line
        len = TL_Client[i].println(std::forward<Args>(args) ...);
        TL_Client[i].flush();
      }
    }
    return len;
  }

  // Wrapper for all printf() variants in Print
  template <typename... Args>
  size_t printf(Args&&... args) { // NOLINT
    size_t len = 0;
    usedTemplate = 4;
    // Loop over clients
    for (uint8_t i = 0; i < TL_maxClients; ++i) {
      // Is it active?
      if (TL_Client[i] || TL_Client[i].connected()) {
        // Yes. print out line
        len = TL_Client[i].printf(std::forward<Args>(args) ...);
        TL_Client[i].flush();
      }
    }
    return len;
  }
  #endif

  size_t printf(const char *format, ...) {
    size_t len = 0;
    usedTemplate = 6;
    // Loop over clients
    for (uint8_t i = 0; i < TL_maxClients; ++i) {
      // Is it active?
      if (TL_Client[i] || TL_Client[i].connected()) {
        // Yes. print out line
        va_list argptr;
        va_start(argptr, format);
        len = TL_Client[i].printf(format, argptr);
        va_end(argptr);
      }
    }
    return len;
  }

  uint8_t usedTemplate;

protected:
    // Telnet definitions
    bool TL_ConnectionEstablished; // Flag for successfully handled connection
    uint8_t TL_maxClients;
    WiFiServer *TL_Server;
    WiFiClient *TL_Client;
};

