## Linux control program
The ``Smartdose.zip`` file contains all source files, plus a ``Makefile`` to build a ``Smartdose`` control program for the Smartdose socket firmware.
It purely is built upon the Modbus server the firmware provides, so to use it you need to have a ``#define MODBUS_SERVER 1`` in the code.

### Usage
Running ``Smartdose`` without parameters will give you this usage hint:
```
At least one argument needed!

Usage: Smartdose host[:port[:serverID]]] [cmd [cmd_parms]]
  cmd: INFO | ON | OFF | DEFAULT | EVERY | RESET | FACTOR
  DEFAULT ON|OFF
  EVERY <seconds>
  FACTOR [V|A|W [<factor>]]
```
Whenever something is not understood by the program, or if some Modbus error is received, the program will terminate after putting out a respective message.

The very first and required parameter is the socket you will want to control.
It may be an IP address or the device name you have assigned in the configuration of the socket (if you are in the same network as the socket, of course).
Optionally you may append a port number, starting with a colon ``:``, should you have changed the default 502 port in the firmware source.
If you have added a port number, you additionally may add a Modbus server ID, if you have changed the default 1 to something else - again separated by a colon.

So all of these examples are valid target descriptions:
```
192.168.4.22
LivingRoom
192.168.178.75:502:2
downlight:6244
vent:51202:211
```
The program will put out an error response for invalid descriptors.

Next comes a command. If you omit it, ``INFO`` will be taken as the default.

#### INFO
This command reads out all Modbus registers the sockets are providing and will print the contents in interpreted form.
On Gosund devices, in addition to the basic switch data the power meter data is printed.
```
micha@LinuxBox:~/Smartdose$ ./Smartdose Gosund02
Using 192.168.178.51:502:1
Gosund device Modbus server Default: ON
Running since   24:12:20
ON time         11:06:45
ON  (255) for   24:12:15
accumulated         0.00 kWh
Power               0.00 W
Voltage           230.88 V
Current             0.02 A
```
The first line gives the full descriptor the program has built from the entered target description.
The next lists the attributes the device has set - of course the Modbus server is listed here - else nothing would be read!
The next three lines give the switch statistics - run time since last reboot, time spent in ON state and the time the current state (``ON`` in the example) is active.

Please note that with Gosund devices the last two are different, since the ``ON time`` only is counted if power consumption is measured, while the current state is counted for the time the switch being electrically ON or OFF.
Maxcio devices have no power meter, hence will have both times identical.

The final four lines are shown for Gosund devices only and are showing the current values of the power meter.

#### EVERY n
EVERY is basically the same as INFO, but does repeatedly request and display the target's data.
The ``n`` gives the number of seconds between data requests and may not be 0. 
A value of at least 5 is sensible to not overload the devices with requests.

Example output for a Gosund device again:
```
micha@LinuxBox:~/Smartdose$ ./Smartdose Gosund04 EVERY 10
Using 192.168.178.54:502:1
Gosund device Telnet server Modbus server Default: ON
Running since   48:05:50
ON time          0:00:50
OFF (  0) for   27:00:05
accumulated         0.01 kWh
Power               0.00 W
Voltage           231.64 V
Current             0.00 A
Loop:   Run time     ON time  now      since        kWh           W           V           A
   1:   48:06:00     0:00:50  OFF   27:00:15       0.01        0.00      231.47        0.00
   2:   48:06:10     0:00:50  OFF   27:00:25       0.01        0.00      231.47        0.00
   3:   48:06:25     0:00:50  OFF   27:00:40       0.01        0.00      231.47        0.00
   4:   48:06:35     0:00:50  OFF   27:00:50       0.01        0.00      231.31        0.00
```

After an initial output identical to that of INFO subsequent responses are printed as one-liners.

#### ON and OFF
These two commands simply do what their names tell: they will switch the device into ON or OFF state, respectively.

#### DEFAULT ON|OFF
The DEFAULT command takes one argument out of ON or OFF and will set the boot switch state accordingly.
A device set to DEFAULT ON will switch the power on upon a reboot or power loss.

#### RESET
Gosund devices are accumulating the consumed power in a register.
To start from scratch, with the RESET command you can have this register set to zero again.

#### FACTOR, FACTOR V|A|W [value]
The FACTOR command is used to read or set the internal measurement correction factors the Gosund devices are maintaining.
Used without any additional parameter, FACTOR will print out the current correction factor values:
```
micha@LinuxBox:~/Smartdose$ ./Smartdose Gosund02 FACTOR
Using 192.168.178.51:502:1
Correction factors:
V:    1.02000
A:    1.75000
W:    1.04000
```
If you would like to change these factors, you will have to name the value you are going to correct - ``V`` for voltage, ``A`` for current and ``W`` for power measurements.

Without any further value, the command will reset the respective correction factor to 1.0:
```
micha@LinuxBox:~/Smartdose$ ./Smartdose Gosund02 FACTOR W 
Using 192.168.178.51:502:1
micha@LinuxBox:~/Smartdose$ ./Smartdose Gosund02 FACTOR
Using 192.168.178.51:502:1
Correction factors:
V:    1.02000
A:    1.75000
W:    1.00000
```

To set a specific value, you have to add it behind the category letter:
```
micha@LinuxBox:~/Smartdose$ ./Smartdose Gosund02 FACTOR W 1.04
Using 192.168.178.51:502:1
micha@LinuxBox:~/Smartdose$ ./Smartdose Gosund02 FACTOR
Using 192.168.178.51:502:1
Correction factors:
V:    1.02000
A:    1.75000
W:    1.04000
```

These changes are permanent and will have immediate effect.
