// =================================================================================================
// eModbus: Copyright 2020 by Michael Harwerth, Bert Melis and the contributors to eModbus
//               MIT license - see license.md for details
// =================================================================================================
#include "TelnetLog.h"

TelnetLog::TelnetLog(uint16_t p, uint8_t mc) {
  TL_maxClients = mc;
  TL_ConnectionEstablished = false;
  TL_Server = new WiFiServer(p);
  TL_Client = new WiFiClient[mc];
}

TelnetLog::~TelnetLog() {
  delete TL_Server;
  delete[] TL_Client;
}

void TelnetLog::begin(const char * label) {
  strncpy(myLabel, label, 64);
  TL_Server->begin();
  TL_Server->setNoDelay(true);
}

void TelnetLog::end() {
  // Loop over clients
  for (uint8_t i = 0; i < TL_maxClients; ++i) {
    // Is it active?
    if (TL_Client[i] || TL_Client[i].connected()) {
      // Yes. Disconnect it.
      TL_Client[i].stop();
    }
  }
  TL_Server->close();
  TL_Server->stop();
}

size_t TelnetLog::write(uint8_t c) {
    size_t len = 0;
  // Loop over clients
  for (uint8_t i = 0; i < TL_maxClients; ++i) {
    // Is it active?
    if (TL_Client[i] || TL_Client[i].connected()) {
      // Yes. print out line
      len = TL_Client[i].write(c);
    }
  }
  return len;
}

size_t TelnetLog::write(const uint8_t *buffer, size_t len) {
  // Loop over clients
  for (uint8_t i = 0; i < TL_maxClients; ++i) {
    // Is it active?
    if (TL_Client[i] || TL_Client[i].connected()) {
      // Yes. print out line
      len = TL_Client[i].write(buffer, len);
      TL_Client[i].flush();
    }
  }
  return len;
}

void TelnetLog::update() {
  telnetActive = false;
  // Cleanup disconnected session
  for (uint8_t i = 0; i < TL_maxClients; i++) {
    // Client in use?
    if (TL_Client[i]) {
      // Yes. Connected?
      if (!TL_Client[i].connected()) {
        // No, stop it.
        TL_Client[i].stop();
      } else {
        // Yes. Discard any input!
        while (TL_Client[i].available()) TL_Client[i].read();
        telnetActive = true;
      }
    }
  }
  
  // Check new client connections
  if (TL_Server->hasClient()) {
    TL_ConnectionEstablished = false; // Set to false
    
    for (uint8_t i = 0; i < TL_maxClients; i++) {
      // find free socket
      if (!TL_Client[i]) {
        TL_Client[i] = TL_Server->available(); 
        
        TL_Client[i].flush();  // clear input buffer, else you get strange characters
        TL_Client[i].print("Welcome to '");
        TL_Client[i].print(myLabel);
        TL_Client[i].println("'!");
        
        TL_Client[i].print("Millis since start: ");
        TL_Client[i].println(millis());
        
        TL_Client[i].print("Free Heap RAM: ");
        TL_Client[i].println(ESP.getFreeHeap());

        TL_Client[i].print("Server IP: ");
        TL_Client[i].println(WiFi.localIP());
  
        TL_Client[i].println("----------------------------------------------------------------");
        
        TL_ConnectionEstablished = true; 
        
        break;
      }
    }

    if (TL_ConnectionEstablished == false) {
      TL_Server->available().stop();
    }
  }
}
