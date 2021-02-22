// =================================================================================================
// eModbus: Copyright 2020 by Michael Harwerth, Bert Melis and the contributors to eModbus
//               MIT license - see license.md for details
// =================================================================================================
#include "TelnetLogAsync.h"

TelnetLog::TelnetLog(uint16_t p, uint8_t mc) {
  TL_maxClients = mc;
  TL_Server = new AsyncServer(p);
  TL_Client.clear();
  TL_Server->onClient(&handleNewClient, (void *)this);
}

TelnetLog::~TelnetLog() {
  delete TL_Server;
  // Loop over clients
  for (auto it : TL_Client) {
    it->stop();
    it->close(true);
    delete it;
  }
  std::vector<AsyncClient *>().swap(TL_Client);
}

void TelnetLog::begin(const char * label) {
  strncpy(myLabel, label, sizeof(myLabel));
  TL_Server->begin();
  TL_Server->setNoDelay(true);
}

void TelnetLog::end() {
  // Loop over clients
  for (auto client : TL_Client) {
    client->stop();
    client->close();
    delete client;
  }
  TL_Server->end();
}

size_t TelnetLog::write(uint8_t c) {
    uint8_t buf[2] =  { c, 0 };
  // Loop over clients
  for (auto client : TL_Client) {
    if (client->connected()) { // } && client->canSend()) {
      while (!client->canSend()) yield();
      client->write((const char *)buf, 1);
    }
  }
  return 1;
}

size_t TelnetLog::write(const uint8_t *buffer, size_t len) {
  // Loop over clients
  for (auto client : TL_Client) {
    if (client->connected()) { // } && client->canSend()) {
      while (!client->canSend()) yield();
      client->write((const char *)buffer, len);
    }
  }
  return len;
}

void TelnetLog::handleNewClient(void *srv, AsyncClient* newClient) {
  char buffer[80];
  TelnetLog *s = reinterpret_cast<TelnetLog *>(srv);

  // Space left?
  if (s->TL_Client.size() < s->TL_maxClients) {
    // add to list
    s->TL_Client.push_back(newClient);
	
    // register events
    newClient->onData(&handleData, srv);
    newClient->onDisconnect(&handleDisconnect, srv);

    snprintf(buffer, 80, "Welcome to'%s'!\n", s->myLabel);
    newClient->add(buffer, strlen(buffer));
    newClient->send();
        
    snprintf(buffer, 80, "Millis since start: %ul\n", (uint32_t)millis());
    newClient->add(buffer, strlen(buffer));
    newClient->send();
        
    snprintf(buffer, 80, "Free heap RAM: %ul\n", ESP.getFreeHeap());
    newClient->add(buffer, strlen(buffer));
    newClient->send();

    snprintf(buffer, 80, "Server IP: %d.%d.%d.%d\n", WiFi.localIP()[0], WiFi.localIP()[1], WiFi.localIP()[2], WiFi.localIP()[3]);
    newClient->add(buffer, strlen(buffer));
    newClient->send();

    memset(buffer, '-', 80);
    buffer[78] = '\n';
    buffer[79] = 0;
    newClient->add(buffer, strlen(buffer));
    newClient->send();
  } else {
    // No, maximum number of clients reached
    newClient->close(true);
    newClient->stop();
    delete newClient;
  }
}

void TelnetLog::handleDisconnect(void *srv, AsyncClient *c) {
  TelnetLog *s = reinterpret_cast<TelnetLog *>(srv);
  for (auto it = s->TL_Client.begin(); it != s->TL_Client.end();) {
    if (*it == c) {
      s->TL_Client.erase(it);
      c->close(true);
      delete c;
      it = s->TL_Client.end();
    } else {
      it++;
    }
  }
}

void TelnetLog::handleData(void *srv, AsyncClient* client, void *data, size_t len) {
  // Do nothing for now, ignore data
}
