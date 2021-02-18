// =================================================================================================
// eModbus: Copyright 2020 by Michael Harwerth, Bert Melis and the contributors to eModbus
//               MIT license - see license.md for details
// =================================================================================================

// Include Arduino.h to make Print and Serial known
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>

class TelnetLog : public Print {
public:
  TelnetLog(uint16_t port, uint8_t maxClients);
  ~TelnetLog();
  void begin(const char *label);
  void end();
  void update();
  inline bool isActive() { return telnetActive; };
  size_t write(uint8_t c);
  size_t write(const uint8_t *buffer, size_t size);

protected:
    // Telnet definitions
    bool TL_ConnectionEstablished; // Flag for successfully handled connection
    uint8_t TL_maxClients;
    WiFiServer *TL_Server;
    WiFiClient *TL_Client;
    bool telnetActive;
    char myLabel[64];
};

