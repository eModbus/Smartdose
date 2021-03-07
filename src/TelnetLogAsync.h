// =================================================================================================
// eModbus: Copyright 2020 by Michael Harwerth, Bert Melis and the contributors to eModbus
//               MIT license - see license.md for details
// =================================================================================================

#ifndef _TELNETLOGASYNC_H
#define _TELNETLOGASYNC_H
// Include Arduino.h to make Print and Serial known
#include <Arduino.h>
#include <vector>
#include "RingBuf.h"

#ifdef ESP8266
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#elif defined(ESP32)
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#else
#error "TelnetLogAsync requires an ESP8266 or ESP32 to run."
#endif


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
  inline unsigned int getActiveClients() { return TL_Client.size(); }

protected:
    struct ClientList {
      AsyncClient *client;
      RingBuf<uint8_t> *buffer;
      ClientList(size_t bufSize, AsyncClient *c) {
        buffer = new RingBuf<uint8_t>(bufSize);
        client = c;
      }
      ~ClientList() {
        if (client) {
          client->close(true);
          client->stop();
          delete client;
        }
        if (buffer) delete buffer;
      }
    };
    // Telnet definitions
    uint8_t TL_maxClients;
    AsyncServer *TL_Server;
    std::vector<ClientList *> TL_Client;
    char myLabel[64];
    static void handleNewClient(void *srv, AsyncClient *client);
    static void handleDisconnect(void *srv, AsyncClient *client);
    static void handlePoll(void *srv, AsyncClient *client);
    static void handleAck(void *srv, AsyncClient *client, size_t len, uint32_t aTime);
    static void handleData(void *srv, AsyncClient* client, void *data, size_t len);
    static void sendBytes(TelnetLog *server, AsyncClient *client);
};
#endif
