// Blinker
// Copyright 2020 by miq1@gmx.de

// Blinker is a class to apply arbitrary blinking patterns to a LED.
// Blinker patterns are 16-bit uint16_t bit maps, where a '1' means 'LED ON'
// and a '0' is 'LED OFF'. Sequences of '1' or '0' are treated as a longer
// stable period.
// NOTE: leading '0' in the pattern are ignored - the pattern will start at the first '1'!
// The time length of 1 bit is given by the interval parameter to start().
// Example:
//   start(100, 0xF0F0);
// will turn the LED ON for 400ms, switch it off again for 400ms and repeat that
// 'F' is 4 bits '1' in a row ==> 4 * 100 = 400
// NOTE: the update() call must be done in shorter intervals as those in the 
// start() call for Blinker to be able to cleanly maintain the requested pattern!

#ifndef _BLINKER_H
#define _BLINKER_H

#include <Arduino.h>

#define BLINKER_DEFAULT 250
#define BLINKER_PATTERN 0xF000

// Blinker: helper class to maintain blinking patterns for the LED
class Blinker {
public:
  // Constructor: takes GPIO of LED to handle
  explicit Blinker(uint8_t port, bool onState = HIGH);
  
  // start: in interval steps, loop over blinking pattern
  uint32_t start(uint16_t pattern = BLINKER_PATTERN, uint32_t interval = BLINKER_DEFAULT);

  // stop: stop blinking
  void stop();

  // update: check if the blinking pattern needs to be advanced a step
  void update();

protected:
  uint8_t  B_counter;      // Number of bit currently processed
  uint8_t  B_port;         // GPIO of the LED
  uint16_t B_pattern;      // 16-bit blinking pattern
  uint16_t B_pWork;        // 16-bit work pattern
  uint8_t  B_pLength;      // used length of the blinking pattern
  uint32_t B_lastTick;     // Last interval start time
  uint32_t B_interval;     // Length of interval in milliseconds
  bool     B_onState;      // Pin state to switch the LED ON
};
#endif
