// =================================================================================================
// eModbus: Copyright 2020 by Michael Harwerth, Bert Melis and the contributors to eModbus
//               MIT license - see license.md for details
// =================================================================================================

#ifndef _TELNETLOGASYNC_H
#define _TELNETLOGASYNC_H
// Include Arduino.h to make Print and Serial known
#include <Arduino.h>
#include <vector>
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>

using std::vector;

class TelnetLog : public Print {
public:
  TelnetLog(uint16_t port, uint8_t maxClients);
  ~TelnetLog();
  void begin(const char *label);
  void end();
  inline bool isActive() { return (TL_Client.size() ? true : false); };
  size_t write(uint8_t c);
  size_t write(const uint8_t *buffer, size_t size);

protected:
    // Telnet definitions
    uint8_t TL_maxClients;
    AsyncServer *TL_Server;
    std::vector<AsyncClient *> TL_Client;
    char myLabel[64];
    static void handleNewClient(void *srv, AsyncClient *client);
    static void handleDisconnect(void *srv, AsyncClient *client);
    static void handleData(void *srv, AsyncClient* client, void *data, size_t len);
};
#endif
