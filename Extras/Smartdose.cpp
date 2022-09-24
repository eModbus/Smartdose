// Copyright 2021 Michael Harwerth - miq1 AT gmx DOT de

#include <iostream>
#include <iomanip>
#include <regex>
#include "Logging.h"
#include "ModbusClientTCP.h"
#include "parseTarget.h"

using std::cout;
using std::endl;
using std::printf;
using std::hex;
using std::dec;

// Data structure for basic Smartdose data
struct SDbasic {
  uint16_t onState;
  uint16_t flags;
  struct {
    uint16_t hours;
    uint8_t minutes;
    uint8_t seconds;
  } uptime;
  struct {
    uint16_t hours;
    uint8_t minutes;
    uint8_t seconds;
  } statetime;
  struct {
    uint16_t hours;
    uint8_t minutes;
    uint8_t seconds;
  } ontime;
} basicData;

// Data structure for additional data on Gosund devices
struct SDadvanced {
  float accW;
  float factorV;
  float factorA;
  float factorW;
  float watts;
  float volts;
  float amps;
} advancedData;

// Data structure for timers
struct SDtimers {
  uint8_t activeDays;
  uint8_t onOff;
  uint8_t hour;
  uint8_t minute;
} timerData[16];

// Commands understood
const char *cmds[] = { "INFO", "ON", "OFF", "DEFAULT", "EVERY", "RESET", "FACTOR", "TIMER", "_X_END" };
enum CMDS : uint8_t { INFO = 0, SW_ON, SW_OFF, DEFLT, EVRY, RST_CNT, FCTR, TIMR, X_END };

void handleError(Error error, uint32_t token) 
{
  // ModbusError wraps the error code and provides a readable error message for it
  ModbusError me(error);
  cout << "Error response: " << (int)me << " - " << (const char *)me << " at " << token << endl;
}

void usage(const char *msg) {
  cout << msg << endl;
  cout << "Usage: Smartdose host[:port[:serverID]]] [cmd [cmd_parms]]" << endl;
  cout << "  cmd: ";
  for (uint8_t c = 0; c < X_END; c++) {
    if (c) cout << " | ";
    cout << cmds[c];
  }
  cout << endl;
  cout << "  DEFAULT ON|OFF" << endl;
  cout << "  EVERY <seconds>" << endl;
  cout << "  FACTOR [V|A|W [<factor>]]" << endl;
  cout << "  TIMER <n> [<arg> [<arg> [...]]]" << endl;
  cout << "    n: 1..16" << endl;
  cout << "    arg: ACTIVE|INACTIVE|ON|OFF|DAILY|WORKDAYS|WEEKEND|<day>|<hh24>:<mm>|CLEAR" << endl;
  cout << "    day: SUN|MON|TUE|WED|THU|FRI|SAT" << endl;
  cout << "    hh24: 0..23" << endl;
  cout << "    mm: 0..59" << endl;
}

void printTimer(uint8_t tnum, SDtimers& t) {
  char buf[128];

  snprintf(buf, 128, "Timer %2d: %3s %3s %02d:%02d", 
    (unsigned int)tnum,
    t.activeDays & 0x80 ? "ACT" : " ",
    t.onOff & 0x01 ? "ON" : "OFF",
    t.hour,
    t.minute
  );
  if (t.activeDays & 0x01) { strncat(buf, " SUN", 127); }
  if (t.activeDays & 0x02) { strncat(buf, " MON", 127); }
  if (t.activeDays & 0x04) { strncat(buf, " TUE", 127); }
  if (t.activeDays & 0x08) { strncat(buf, " WED", 127); }
  if (t.activeDays & 0x10) { strncat(buf, " THU", 127); }
  if (t.activeDays & 0x20) { strncat(buf, " FRI", 127); }
  if (t.activeDays & 0x40) { strncat(buf, " SAT", 127); }
  cout << buf << endl;
}

// ============= main =============
int main(int argc, char **argv) {
  // Target host parameters
  IPAddress targetIP = NIL_ADDR;
  uint16_t targetPort = 502;
  uint8_t targetServer = 1;
  char buf[128];

  // Define a TCP client
  Client cl;
  cl.setNoDelay(true);

  // Check calling arguments.
  // Arg1 is mandatory, a host name or IP address, optionally followed by ":port number",
  // again optionally followed by ":server ID".
  if (argc < 2) {
    usage("At least one argument needed!\n");
    return -1;
  }

  if (int rc = parseTarget(argv[1], targetIP, targetPort, targetServer)) {
    usage("Target descriptor invalid!");
    return rc;
  }

  cout << "Using " << string(targetIP) << ":" << targetPort << ":" << (unsigned int)targetServer << endl;

  // Next shall be a command word. Omission is like INFO
  uint8_t cmd = X_END;
  if (argc > 2) {
    for (uint8_t c = INFO; c < X_END; c++) {
      if (strncasecmp(argv[2], cmds[c], strlen(cmds[c])) == 0) {
        cmd = c;
        break;
      }
    }
    if (cmd == X_END) {
      usage("Invalid command!");
      return -1;
    }
  } else {
    cmd = INFO;
  }
    
  // Define a Modbus client using the TCP client
  ModbusClientTCP MBclient(cl);

  // Set up ModbusTCP client.
  // Set message timeout to 2000ms and interval between requests to the same host to 200ms
  MBclient.setTimeout(2000, 200);
  // Start ModbusTCP background task
  MBclient.begin();

  // Set Modbus TCP server address and port number
  MBclient.setTarget(targetIP, targetPort);

  switch (cmd) {
// --------- Get and print out state data ------------------
  case INFO:
  case EVRY:
    {
      unsigned int interval = 0;
      unsigned int loopCnt = 0;

      if (cmd == EVRY) {
        if (argc > 3) {
          interval = atoi(argv[3]);
        }
        if (interval == 0) {
          usage("EVERY needs an interval > 0s");
          return -1;
        }
      }

      do {
        // Issue a request
        uint16_t addr = 1;
        uint16_t words = 8;
        uint16_t offs = 3;

        ModbusMessage response = MBclient.syncRequest(1, targetServer, READ_HOLD_REGISTER, addr, words);
        Error err = response.getError();
        if (err!=SUCCESS) {
          handleError(err, 1);
        } else {
          offs = 3;
          offs = response.get(offs, basicData.onState);
          offs = response.get(offs, basicData.flags);
          offs = response.get(offs, basicData.uptime.hours);
          offs = response.get(offs, basicData.uptime.minutes);
          offs = response.get(offs, basicData.uptime.seconds);
          offs = response.get(offs, basicData.statetime.hours);
          offs = response.get(offs, basicData.statetime.minutes);
          offs = response.get(offs, basicData.statetime.seconds);
          offs = response.get(offs, basicData.ontime.hours);
          offs = response.get(offs, basicData.ontime.minutes);
          offs = response.get(offs, basicData.ontime.seconds);

          // Print out results
          if (loopCnt == 0) {
            if (basicData.flags & 0x8000) cout << "Gosund device| ";
            if (basicData.flags & 0x4000) cout << "Telnet server| ";
            if (basicData.flags & 0x2000) cout << "Modbus server| ";
            if (basicData.flags & 0x1000) cout << "Fauxmo server (Alexa)| ";
            if (basicData.flags & 0x0800) cout << "Timers| ";
            if (basicData.flags & 0x0001) cout << "Default: ON" << endl;
            else                          cout << "Default: OFF" << endl;
          
            snprintf(buf, 128, "Running since %4u:%02u:%02u", 
                          (unsigned int)basicData.uptime.hours, 
                        (unsigned int)basicData.uptime.minutes, 
                        (unsigned int)basicData.uptime.seconds);
            cout << buf << endl;

            snprintf(buf, 128, "ON time       %4u:%02u:%02u", 
                          (unsigned int)basicData.ontime.hours, 
                          (unsigned int)basicData.ontime.minutes, 
                          (unsigned int)basicData.ontime.seconds);
            cout << buf << endl;

            snprintf(buf, 128, "%-3s (%3d) for %4u:%02u:%02u", 
                          (basicData.onState ? "ON" : "OFF"),
                        (unsigned int)basicData.onState,
                        (unsigned int)basicData.statetime.hours, 
                        (unsigned int)basicData.statetime.minutes, 
                        (unsigned int)basicData.statetime.seconds);
            cout << buf << endl;
          } else {
            if (loopCnt % 24 == 1) {
              snprintf(buf, 128, "Loop:   Run time     ON time  now      since        kWh           W           V           A");
              cout << buf << endl;
            }
            snprintf(buf, 128, "%4u: %4u:%02u:%02u  %4u:%02u:%02u  %-3s %4u:%02u:%02u ",
              loopCnt,
              (unsigned int)basicData.uptime.hours, 
              (unsigned int)basicData.uptime.minutes, 
              (unsigned int)basicData.uptime.seconds,
              (unsigned int)basicData.ontime.hours, 
              (unsigned int)basicData.ontime.minutes, 
              (unsigned int)basicData.ontime.seconds,
              (basicData.onState ? "ON" : "OFF"),
              (unsigned int)basicData.statetime.hours, 
              (unsigned int)basicData.statetime.minutes, 
              (unsigned int)basicData.statetime.seconds);
            cout << buf;
          }
        }

        if (basicData.flags & 0x8000) {
          addr = 9; 
          words = 14;
          offs = 3;
          response = MBclient.syncRequest(2, targetServer, READ_HOLD_REGISTER, addr, words);
          err = response.getError();
          if (err!=SUCCESS) {
            handleError(err, 2);
          } else {
            offs = response.get(offs, advancedData.accW);
            offs = response.get(offs, advancedData.factorV);
            offs = response.get(offs, advancedData.factorA);
            offs = response.get(offs, advancedData.factorW);
            offs = response.get(offs, advancedData.volts);
            offs = response.get(offs, advancedData.amps);
            offs = response.get(offs, advancedData.watts);

            if (loopCnt == 0) {
              snprintf(buf, 128, "accumulated   %10.2f kWh", advancedData.accW / 1000.0);
              cout << buf << endl;
              snprintf(buf, 128, "Power         %10.2f W", advancedData.watts);
              cout << buf << endl;
              snprintf(buf, 128, "Voltage       %10.2f V", advancedData.volts);
              cout << buf << endl;
              snprintf(buf, 128, "Current       %10.2f A", advancedData.amps);
            } else {
              snprintf(buf, 128, "%10.2f  %10.2f  %10.2f  %10.2f",
                advancedData.accW / 1000.0,
                advancedData.watts,
                advancedData.volts,
                advancedData.amps);
            }
            cout << buf << endl;
          }
        }
        if (basicData.flags & 0x0800) {
          addr = 23; 
          words = 32;
          offs = 3;
          response = MBclient.syncRequest(3, targetServer, READ_HOLD_REGISTER, addr, words);
          err = response.getError();
          if (err!=SUCCESS) {
            handleError(err, 3);
          } else {
            for (uint8_t i = 0; i < 16; i++) {
              offs = response.get(offs, timerData[i].activeDays);
              offs = response.get(offs, timerData[i].onOff);
              offs = response.get(offs, timerData[i].hour);
              offs = response.get(offs, timerData[i].minute);
            }
           
            if (loopCnt == 0) {
              for (uint8_t i = 0; i < 16; i++) {
                printTimer(i + 1, timerData[i]);
              }
            } else {
            }
          }
        } else {
          if (interval) {
            cout << endl;
          }
        }
        sleep(interval);
        loopCnt++;
      } while (interval);
    }
    break;
// --------- Switch ON ------------------
  case SW_ON:
    {
      // Write 255 to addr 1
      uint16_t addr = 1;

      ModbusMessage response = MBclient.syncRequest(4, targetServer, WRITE_HOLD_REGISTER, addr, 255);
      Error err = response.getError();
      if (err!=SUCCESS) {
        handleError(err, 4);
      }
    }
    break;
// --------- Switch OFF ------------------
  case SW_OFF:
    {
      // Write 0 to addr 1
      uint16_t addr = 1;

      ModbusMessage response = MBclient.syncRequest(5, targetServer, WRITE_HOLD_REGISTER, addr, 0);
      Error err = response.getError();
      if (err!=SUCCESS) {
        handleError(err, 5);
      }
    }
    break;
// --------- Default switch state ------------------
  case DEFLT:
    {
      // Check state argument
      uint8_t onOff = 99;
      if (argc > 3) {
        if (strncasecmp(argv[3], "ON", 2) == 0) {
          onOff = 1;
        } else if (strncasecmp(argv[3], "OFF", 3) == 0) {
          onOff = 0;
        }
      } 
      if (onOff == 99) {
        usage("DEFAULT requires ON or OFF!");
        return -1;
      }
      
      // Read flag register
      uint16_t addr = 2;
      uint16_t words = 1;
      uint16_t offs = 3;

      ModbusMessage response = MBclient.syncRequest(6, targetServer, READ_HOLD_REGISTER, addr, words);
      Error err = response.getError();
      if (err!=SUCCESS) {
        handleError(err, 6);
      } else {
        offs = response.get(offs, basicData.flags);

        // Read current default
        // Do nothing if state matches command argument
        if (basicData.flags & 0x0001) {
          if (onOff == 1) onOff = 99;
        } else {
          if (onOff == 0) onOff = 99;
        }

        // Something to be done?
        if (onOff != 99) {
          // Yes. Write flag register
          basicData.flags &= 0xFFFE;
          basicData.flags |= onOff;

          response = MBclient.syncRequest(7, targetServer, WRITE_HOLD_REGISTER, addr, basicData.flags);
          err = response.getError();
          if (err!=SUCCESS) {
            handleError(err, 7);
            cout << "DEFAULT " << (onOff ? "ON" : "OFF") << " was unsuccessful." << endl;
          }
        }
      }
    }
    break;
// --------- Reset power counter ------------------
  case RST_CNT:
    {
      // Read flag register
      uint16_t addr = 2;
      uint16_t words = 1;
      uint16_t offs = 3;

      ModbusMessage response = MBclient.syncRequest(8, targetServer, READ_HOLD_REGISTER, addr, words);
      Error err = response.getError();
      if (err!=SUCCESS) {
        handleError(err, 8);
      } else {
        offs = response.get(offs, basicData.flags);
        if (!(basicData.flags & 0x8000)) {
          usage("RESET is only for Gosund devices!");
          return -1;
        }

        addr = 9;
        response = MBclient.syncRequest(9, targetServer, WRITE_HOLD_REGISTER, addr, 0);
        err = response.getError();
        if (err!=SUCCESS) {
          handleError(err, 9);
          cout << "RESET was unsuccessful." << endl;
        }
      }
    }
    break;
// --------- Adjust meter values ------------------
  case FCTR:
    {
      // Read flag register
      uint16_t addr = 2;
      uint16_t words = 1;
      uint16_t offs = 3;

      ModbusMessage response = MBclient.syncRequest(10, targetServer, READ_HOLD_REGISTER, addr, words);
      Error err = response.getError();
      if (err!=SUCCESS) {
        handleError(err, 10);
      } else {
        offs = response.get(offs, basicData.flags);
        if (!(basicData.flags & 0x8000)) {
          usage("FACTOR is only for Gosund devices!");
          return -1;
        }

        // Output only
        if (argc < 4) {
          addr = 9; 
          words = 14;
          offs = 3;
          response = MBclient.syncRequest(11, targetServer, READ_HOLD_REGISTER, addr, words);
          err = response.getError();
          if (err!=SUCCESS) {
            handleError(err, 11);
          } else {
            offs = response.get(offs, advancedData.accW);
            offs = response.get(offs, advancedData.factorV);
            offs = response.get(offs, advancedData.factorA);
            offs = response.get(offs, advancedData.factorW);
            offs = response.get(offs, advancedData.volts);
            offs = response.get(offs, advancedData.amps);
            offs = response.get(offs, advancedData.watts);

            cout << "Correction factors:" << endl;
            snprintf(buf, 128, "V: %10.5f", advancedData.factorV);
            cout << buf << endl;
            snprintf(buf, 128, "A: %10.5f", advancedData.factorA);
            cout << buf << endl;
            snprintf(buf, 128, "W: %10.5f", advancedData.factorW);
            cout << buf << endl;
            
            return 0;
          }
        } else {
          uint8_t type = 99;
          switch (*argv[3]) {
          case 'V':
            type = 0;
            break;
          case 'A':
            type = 1;
            break;
          case 'W':
            type = 2;
            break;
          }
          if (type == 99) {
            usage("FACTOR needs a unit (V/A/W)!");
            return -1;
          }

          float f = 1.0;
          if (argc > 4) {
            f = atof(argv[4]);
          }

          ModbusMessage facMsg(targetServer, USER_DEFINED_43);
          facMsg.add(type, f);

          response = MBclient.syncRequest(facMsg, (uint32_t)13);
          err = response.getError();
          if (err!=SUCCESS) {
            handleError(err,13);
          } 
        }
      }
    }
    break;
// --------- Set timer parameters -----------------
  case TIMR:
    {
      // Check arguments
      uint8_t subcmd = 99;
      uint8_t tim = 99;
      if (argc > 3) {
//      Get timer number
        tim = atoi(argv[3]);
        if (tim < 1 || tim > 16) {
          usage("TIMER number must be 1..16!");
          return -1;
        }
//      We have one. Now we need to read the timer data
        tim--;
        subcmd = 1;  // info for now.

        // Issue a request for the flag word
        uint16_t addr = 2;
        uint16_t words = 1;
        uint16_t offs = 3;

        ModbusMessage response = MBclient.syncRequest(14, targetServer, READ_HOLD_REGISTER, addr, words);
        Error err = response.getError();
        if (err!=SUCCESS) {
          handleError(err, 14);
        } else {
          offs = response.get(offs, basicData.flags);
        }
//      Do we have timers at all?
        if (basicData.flags & 0x0800) {
//        Yes. Read timer data
          addr = 23 + tim * 2;
          words = 2;
          offs = 3;
          response = MBclient.syncRequest(15, targetServer, READ_HOLD_REGISTER, addr, words);
          err = response.getError();
          if (err!=SUCCESS) {
            handleError(err, 15);
          } else {
            offs = response.get(offs, timerData[0].activeDays);
            offs = response.get(offs, timerData[0].onOff);
            offs = response.get(offs, timerData[0].hour);
            offs = response.get(offs, timerData[0].minute);
          }
          uint16_t nextArg = 4;
//        Do we have at least another parameter?
          if (argc > 4) {
//          Yes. Check all remaining
            while (argc > nextArg) {
//            ON?
              if (strncasecmp(argv[nextArg], "ON", 2) == 0) {
                timerData[0].onOff |= 0x01;
//            OFF?
              } else if (strncasecmp(argv[nextArg], "OFF", 3) == 0) {
                timerData[0].onOff &= 0xFE;
//            ACTIVE?
              } else if (strncasecmp(argv[nextArg], "ACTIVE", 6) == 0) {
                timerData[0].activeDays |= 0x80;
//            INACTIVE?
              } else if (strncasecmp(argv[nextArg], "INACTIVE", 8) == 0) {
                timerData[0].activeDays &= 0x7F;
//            SUNday?
              } else if (strncasecmp(argv[nextArg], "SUN", 3) == 0) {
                timerData[0].activeDays |= 0x01;
//            MONday?
              } else if (strncasecmp(argv[nextArg], "MON", 3) == 0) {
                timerData[0].activeDays |= 0x02;
//            TUEsday?
              } else if (strncasecmp(argv[nextArg], "TUE", 3) == 0) {
                timerData[0].activeDays |= 0x04;
//            WEDnesday?
              } else if (strncasecmp(argv[nextArg], "WED", 3) == 0) {
                timerData[0].activeDays |= 0x08;
//            THUrsday?
              } else if (strncasecmp(argv[nextArg], "THU", 3) == 0) {
                timerData[0].activeDays |= 0x10;
//            FRIday?
              } else if (strncasecmp(argv[nextArg], "FRI", 3) == 0) {
                timerData[0].activeDays |= 0x20;
//            SATurday?
              } else if (strncasecmp(argv[nextArg], "SAT", 3) == 0) {
                timerData[0].activeDays |= 0x40;
//            WORKday?
              } else if (strncasecmp(argv[nextArg], "WORK", 4) == 0) {
                timerData[0].activeDays |= 0x3E;
//            WEEKEND?
              } else if (strncasecmp(argv[nextArg], "WEEKEND", 7) == 0) {
                timerData[0].activeDays |= 0x41;
//            DAILY?
              } else if (strncasecmp(argv[nextArg], "DAILY", 5) == 0) {
                timerData[0].activeDays |= 0x7F;
//            CLEAR?
              } else if (strncasecmp(argv[nextArg], "CLEAR", 5) == 0) {
                timerData[0].activeDays = 0;
                timerData[0].onOff = 0;
                timerData[0].hour = 0;
                timerData[0].minute = 0;
//            HH24:MM?
              } else if (*argv[nextArg] >= '0' && *argv[nextArg] <= '9') {
                char *cp = argv[nextArg];
                uint16_t hh = 0;
                uint16_t mm = 0;
//              Get hours value first
                while (*cp>= '0' && *cp <= '9') {
                  hh *= 10;
                  hh += (*cp - '0');
                  cp++;
                }
//              Valid hour?
                if (hh < 24) {
//                Yes, check separator
                  if (*cp == ':') {
//                  Is OK, get minutes value
                    cp++;
                    while (*cp>= '0' && *cp <= '9') {
                      mm *= 10;
                      mm += (*cp - '0');
                      cp++;
                    }
//                  Valid minute?
                    if (mm < 60) {
//                    Yes, use time
                      timerData[0].hour = hh;
                      timerData[0].minute = mm;
                    } else {
//                    No, minute is invalid
                      usage("Minute must be 0..59!");
                      return -1;
                    }
                  } else {
//                  No, separator not found
                    usage("Time must be given as HH:MM!");
                    return -1;
                  }
                } else {
//                No, minute is invalid
                  usage("Hour must be 0..23!");
                  return -1;
                }
//            Unknown parameter.
              } else {
                snprintf(buf, 128, "Invalid TIMER parameter '%s'!", argv[nextArg]);
                usage(buf);
                return -1;
              }
              nextArg++;
            }
//          All fine here, now use the collected data
            addr = 23 + tim * 2;
            words = 2;
//          Write data
            ModbusMessage request;
            request.add(targetServer, WRITE_MULT_REGISTERS, 
              addr, 
              words, 
              (uint8_t)(words * 2),
              timerData[0].activeDays,
              timerData[0].onOff,
              timerData[0].hour,
              timerData[0].minute
              );
            response = MBclient.syncRequest(request, (uint32_t)16);
            err = response.getError();
            if (err!=SUCCESS) {
              handleError(err, 16);
            } else {
//            Read back timer data
              offs = 3;
              response = MBclient.syncRequest(17, targetServer, READ_HOLD_REGISTER, addr, words);
              err = response.getError();
              if (err!=SUCCESS) {
                handleError(err, 17);
              } else {
                offs = response.get(offs, timerData[0].activeDays);
                offs = response.get(offs, timerData[0].onOff);
                offs = response.get(offs, timerData[0].hour);
                offs = response.get(offs, timerData[0].minute);
              }
            }
          } 
          printTimer(tim + 1, timerData[0]);
        } else {
          usage("TIMER: device has no timer function!");
          return -1;
        }
      } 
      if (subcmd == 99) {
        usage("TIMER requires a timer number at least!");
        return -1;
      }
    }
    break;
  default:
    usage("MAYNOTHAPPEN error?!?");
    return -2;
  }

  return 0;
}

