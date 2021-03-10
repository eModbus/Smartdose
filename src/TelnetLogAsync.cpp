// =================================================================================================
// eModbus: Copyright 2020 by Michael Harwerth, Bert Melis and the contributors to eModbus
//               MIT license - see license.md for details
// =================================================================================================
#include "TelnetLogAsync.h"

TelnetLog::TelnetLog(uint16_t p, uint8_t mc, size_t rbSize) {
  TL_maxClients = mc;
  TL_Server = new AsyncServer(p);
  myRBsize = rbSize;
  TL_Client.clear();
  TL_Server->onClient(&handleNewClient, (void *)this);
}

TelnetLog::~TelnetLog() {
  delete TL_Server;
  for (auto it : TL_Client) {
    delete it;
  }
  TL_Client.clear();
  std::vector<ClientList *>().swap(TL_Client);
}

void TelnetLog::begin(const char * label) {
  strncpy(myLabel, label, sizeof(myLabel));
  TL_Server->begin();
  TL_Server->setNoDelay(true);
}

void TelnetLog::end() {
  TL_Server->end();
  for (auto it : TL_Client) {
    delete it;
  }
  TL_Client.clear();
}

size_t TelnetLog::write(uint8_t c) {
  // Loop over clients
  for (auto cl : TL_Client) {
    if (cl->client->connected()) { // } && client->canSend()) {
      cl->buffer->push_back(c);
    }
  }
  return 1;
}

size_t TelnetLog::write(const uint8_t *buffer, size_t len) {
  // Loop over clients
  for (auto cl : TL_Client) {
    if (cl->client->connected()) { // } && client->canSend()) {
      cl->buffer->push_back(buffer, len);
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
    ClientList *c = new ClientList(s->myRBsize, newClient);
    s->TL_Client.push_back(c);
	
    // register events
    newClient->onData(&handleData, srv);
    newClient->onPoll(&handlePoll, srv);
    newClient->onAck(&handleAck, srv);
    newClient->onDisconnect(&handleDisconnect, srv);

    snprintf(buffer, 80, "Welcome to '%s'!\n", s->myLabel);
    newClient->add(buffer, strlen(buffer));
        
    snprintf(buffer, 80, "Millis since start: %ul\n", (uint32_t)millis());
    newClient->add(buffer, strlen(buffer));
        
    snprintf(buffer, 80, "Free heap RAM: %d\n", ESP.getFreeHeap());
    newClient->add(buffer, strlen(buffer));

    snprintf(buffer, 80, "Server IP: %d.%d.%d.%d\n", WiFi.localIP()[0], WiFi.localIP()[1], WiFi.localIP()[2], WiFi.localIP()[3]);
    newClient->add(buffer, strlen(buffer));

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
    if ((*it)->client == c) {
      delete (*it);
      s->TL_Client.erase(it);
      it = s->TL_Client.end();
    } else {
      it++;
    }
  }
}

void TelnetLog::handleData(void *srv, AsyncClient* client, void *data, size_t len) {
  // Do nothing for now, ignore data
}

void TelnetLog::sendBytes(TelnetLog *s, AsyncClient *client) {
  if (client->connected()) {
    size_t numBytes = client->space();
    if (numBytes) {
      for (auto it : s->TL_Client) {
        if (it->client == client) {
          size_t numSend = it->buffer->size();
          if (numSend && client->canSend()) {
            if (numSend <= numBytes) {
              client->write((const char *)it->buffer->data(), numSend, ASYNC_WRITE_FLAG_COPY);
              it->buffer->pop(numSend);
            } else {
              client->write((const char *)it->buffer->data(), numBytes, ASYNC_WRITE_FLAG_COPY);
              it->buffer->pop(numBytes);
            }
            break;
          }
          break;
        }
      }
    }
  }

}

void TelnetLog::handlePoll(void *srv, AsyncClient *client) {
  TelnetLog *s = reinterpret_cast<TelnetLog *>(srv);
  sendBytes(s, client);
}

void TelnetLog::handleAck(void *srv, AsyncClient *client, size_t len, uint32_t aTime) {
  TelnetLog *s = reinterpret_cast<TelnetLog *>(srv);
  sendBytes(s, client);
}

