// Modbus bridge device V3
// Copyright 2020 by miq1@gmx.de

#include "Blinker.h"

// Constructor: takes GPIO of LED to handle
Blinker::Blinker(uint8_t port, bool onState) :
  B_port(port),
  B_onState(onState) {
  pinMode(port, OUTPUT);
  stop();
}
  
// start: in interval steps, loop over blinking pattern
uint32_t Blinker::start(uint16_t pattern, uint32_t interval) {
  B_interval = interval;
  B_pattern = pattern;
  B_pLength = 16;
  // Shift B_pattern left until the first '1' bit is found
  while (B_pLength && !(B_pattern & 0x8000)) {
    B_pattern <<= 1;
    B_pLength --;
  }
  B_pWork = B_pattern;
  B_counter = 0;
  B_lastTick = millis();
  return B_lastTick + B_interval;
}

// stop: stop blinking
void Blinker::stop() {
  B_lastTick = 0;
  B_interval = 0;
  B_pattern = 0;
  B_counter = 0;
  digitalWrite(B_port, !B_onState);
}

// update: check if the blinking pattern needs to be advanced a step
void Blinker::update() {
  // Do we have a valid interval?
  if (B_interval) {
    // Yes. Has it passed?
    if (millis() - B_lastTick > B_interval) {
      // Yes. get the current state of the LED pin
      bool state = digitalRead(B_port);
      // Does the pattern require an ON?
      if (B_pWork & 0x8000) {             
        // Yes. switch LED ON, if was OFF
        if (state != B_onState) digitalWrite(B_port, B_onState);
      } else { 
        // No, OFF requested. switch LED OFF, if was ON
        if (state == B_onState) digitalWrite(B_port, !B_onState);
      }
      // Advance pattern
      B_pWork <<= 1;
      // Overflow?
      if (++B_counter == B_pLength) {
        // Yes. Start again with the pattern
        B_counter = 0;
        B_pWork = B_pattern;
      }
      B_lastTick = millis();
    }
  }
}
