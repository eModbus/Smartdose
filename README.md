## Firmware for Gosund SP1 and Maxcio W-DE004 smart plugs

This is a standalone firmware for the named smart plugs/sockets, supporting:
- Philips Hue hub V1 emulation (courtesy of [github.com/vintlabs/fauxmoESP](https://github.com/vintlabs/fauxmoESP), TCP port 80) to enable Alexa speech control
- Telnet monitor for plug operations (TCP port 23)
- OTA flashing of new firmware
- Modbus server (TCP port 502) for
  - run time statictics
  - power metering (Gosund SP1 only)

No app, no cloud service, no data transfers outside your home network

### First-time use
After flashing the firmware to a device, it will be uninitialized. If you will plug it in, it will be in configuration mode.
**Note**: to later get into configuration mode again, press the button within the first 3 seconds after plugging the device. 
To indicate this time, the signal LED will continuously flash in short intervals.

### Configuration
The signal LED will flash three times, then pause a beat, then start again to signal configuration mode has been activated.
To get to the configuration page, you need to connect to the temporary WiFi network the device has set up.
The network is named (SSID is) "Socket_XXXXXX", with "XXXXXX" as the last 6 hexadecimal digits of the device's chip ID.
There is no password necessary.
Once you have made a successful connection to that network and received an IP address, open your browser to [http://192.168.4.1]().
Now the configuration page will be displayed:


### Modbus register reference
The Modbus server running on the smart plus will give out internal values as "holding registers", hence function code 0x03 READ_HOLD_REGISTER can be used to retrieve these values.
Register addresses 1 to 8 are available on any device, regardless of type, whereas registers 9 and onward are only valid for Gosund SP1 devices.

| Register | Contents                        | write enabled? |
|----------|---------------------------------|----------------|
| 1        | State of switch. 0=OFF, 1-255=ON| Yes            |
| 2        | Control flags:                  | Yes (0x0001)   |
|          | 0x0001 - default ON after boot  |                |
|          | 0x2000 - Modbus server enabled  |                |
|          | 0x4000 - Telnet server enabled  |                |
|          | 0x8000 - is a Gosund SP1 device |                |
| 3        | Hours since boot                |                |
| 4        | Minutes/seconds since boot 0xMMSS|               |
| 5        | Hours in current state ON or OFF|                |
| 6        | Min/sec in current state        |                |
| 7        | Hours of ON state since boot    |                |
| 8        | Min/sec of ON state since boot  |                |
|----------|---------------------------------|----------------|
| 9, 10    | accumulated power consumption(W)| Yes (0 value)  |
| 11, 12   | Voltage correction factor       | (Yes, see below)|
| 13, 14   | Current correction factor       | (Yes, see below)|
| 15, 16   | Power correction factor         | (Yes, see below)|
| 17, 18   | Current power consumption (W)   |                |
| 19, 20   | Current voltage level (V)       |                |
| 21, 22   | Current current level (A)       |                |

**Note**: all measurement values are sent as an IEEE754 float number in MSB-first byte sequence. The 4 bytes of that float will use two consecutive registers.

The registers marked as write enabled can be set with the 0x06 WRITE_HOLD_REGISTER function code. 
- Writing registers 1 and 9 will have immediate effect.
- To alter the Control flags register persistently, after writing to it the function code 0x42 USER_DEFINED_42 has to be sent to copy the written data into the EEPROM.
- The correction factors are modified as follows: 
  - use function code 0x43 USER_DEFINED_43 to send an observed value.
    The first byte has to be one of 0=voltage, 1=current or 2=power, followed by a 4-byte IEEE754 float value with the observed measurement.
  - repeated observed values will be used to calculate the average correction factor. This factor will be used immediately for feedback.
  - leaving out the observed measurement value from the request message will reset the factor to 1.0 again.
- To make the corretion factors persistent, the 0x42 USER_DEFINED_42 function code has to be sent as seen above.
