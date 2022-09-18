## Firmware for Gosund SP1, Sonoff S26 (R2) and Maxcio W-DE004 smart plugs

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
The password is "Maelstrom" unless you have changed it in the source.
Once you have made a successful connection to that network and received an IP address, open your browser to [http://192.168.4.1]().
Now the configuration page will be displayed:

<img src="/ConfigPage.png" width="50%">

Fill in your WiFi network SSID and password. Then give the device a unique name it will be using as host name, OTA name etc.
Finally choose an OTA password to secure your future OTA firmware transfers.

Clicking on ``Save`` will store your data in EEPROM.

Finally click on ``Reset`` to restart the device and apply your configurations.

### Usage
Plug in the device, then  wait some seconds for it to settle. The signal LED will flash in quick succession first to allow you to press the button for configuration mode.
After that, the device will try to connect to your WiFi network with the data you gave at the configuration. This is indicated by a slow blink of the signal LED:
Finally a short blink will signal it has read the configuration data and is ready.

#### Manual switching
Press the device's button firmly for a short time, then release it to switch the socket on and off. The power LED will light in ON state.

#### Telnet monitor
(This of course will only apply if you configured ``TELNET_LOG 1`` in the source code!)

Use a terminal program (like the recommended ``putty`` on Windows) to connect to the device on TCP port 23. You may use the device name you configured, or the device's IP address. The terminal program should be set to "Add a CR to every LF" to have clean line breaks.
Terminal output will be like
```
Welcome to 'Socket_F9AD5B'!
Millis since start: 78116166
Free Heap RAM: 29640
Server IP: 192.168.178.54
----------------------------------------------------------------
 ON for    21:41:30   Run time    21:41:30    ON time    17:50:35
   | 230.00 V|     0.00 W|  0.01 A|  9482.34 Wh|
 ON for    21:41:35   Run time    21:41:35    ON time    17:50:40
   | 230.17 V|     0.00 W|  0.01 A|  9482.34 Wh|
 ON for    21:41:40   Run time    21:41:40    ON time    17:50:40
   | 230.17 V|     0.00 W|  0.00 A|  9482.34 Wh|
```

**Note**: the Telnet output is read-only, you may not enter any command here!

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

Since the Gosund built-in meters are somewhat inaccurate, you may modify the measured results with a constant factor at least.
So if f.i. the voltage is off by about 3%, you can set a voltage correction factor of 1.03 to have that adjusted.
You will need a good meter to measure the values at the smart plug outlet. 

**Please be careful not to touch any powered parts to avoid electrical shock - rather leave all as is if you do not exactly know what you are doing!**

The correction factors are modified as follows: 
- use function code 0x43 USER_DEFINED_43 to send a correction factor.
  The first byte has to be one of 0=voltage, 1=current or 2=power, followed by a 4-byte IEEE754 float value with the factor.
