## Linux control program
The ``Smartdose.cpp`` file is intended to build a ``Smartdose`` control program for the Smartdose socket firmware.
It purely is built upon the Modbus server the firmware provides, so to use it you need to have a ``#define MODBUS_SERVER 1`` in the code.
To build it you will need the ``libeModbus`` library that can be found in the Linux examples section on [the eModbus Github repository](https://github.com/eModbus/eModbus).

I have built it successfully on lUbuntu 20.04 and 22.04 and on Raspbian.

### Usage
Running ``Smartdose`` without parameters will give you this usage hint:
```
At least one argument needed!

Usage: Smartdose host[:port[:serverID]]] [cmd [cmd_parms]]
  cmd: INFO | ON | OFF | DEFAULT | EVERY | RESET | ADJUST | AUTOOFF | EVENTS | TIMER
  DEFAULT ON|OFF
  EVERY <seconds>
  ADJUST [V|A|W [<measured value>]]
  AUTOOFF <milliamps> <cycles>
  TIMER <n> [<arg> [<arg> [...]]]
    n: 1..16
    arg: ACTIVE|INACTIVE|ON|OFF|DAILY|WORKDAYS|WEEKEND|<day>|<hh24>:<mm>|CLEAR
    day: SUN|MON|TUE|WED|THU|FRI|SAT
    hh24: 0..23
    mm: 0..59

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
On devices with power meter, in addition to the basic switch data the power meter data is printed.
If ``TIMERS`` is activated, a list of the 16 timers will be added.
```
micha@LinuxBox:~/MBtools$ Smartdose Gosund03
Using 192.168.178.52:502:1
Power meter| Telnet server| Modbus server| Fauxmo server (Alexa)| Timers| Default: ON
Running since    0:40:25
ON time          0:23:40
ON  (255) for    0:40:25
accumulated         0.00 kWh
Power               0.00 W
Voltage           230.72 V
Current             0.00 A
Auto power OFF  0.00 A for 0 turns
Timer  1: ACT  ON 18:05 SUN MON TUE WED THU FRI SAT
Timer  2: ACT OFF 21:50 SUN MON TUE WED THU FRI SAT
Timer  3:     OFF 00:00
Timer  4:     OFF 00:00
Timer  5:     OFF 00:00
Timer  6:     OFF 00:00
Timer  7:     OFF 23:59 SUN SAT
Timer  8:     OFF 00:00
Timer  9:     OFF 00:00
Timer 10:     OFF 00:00
Timer 11: ACT  ON 07:30 WED
Timer 12:     OFF 00:00
Timer 13:     OFF 00:00
Timer 14:     OFF 00:00
Timer 15:     OFF 00:00
Timer 16:     OFF 00:00
```
The first line gives the full descriptor the program has built from the entered target description.
The next lists the attributes the device has set - of course the Modbus server is listed here - else nothing would be read!
The next three lines give the switch statistics - run time since last reboot, time spent in ON state and the time the current state (``ON`` in the example) is active.

Please note that with power meter devices the last two are different, since the ``ON time`` only is counted if power consumption is measured, while the current state is counted for the time the switch being electrically ON or OFF.
Maxcio and Sonoff devices have no power meter, hence will have both times identical.

The next five lines are shown for power meter devices only and are showing the current values of the power meter.

Finally the 16 timers are listed. In the example, only timers 1, 2, 7 and 11 have been programmed, timer 7 is set inactive.

#### EVERY n
EVERY is basically the same as INFO, but does repeatedly request and display the target's data.
Timer data are not printed again.
The ``n`` gives the number of seconds between data requests and may not be 0. 
A value of at least 5 is sensible to not overload the devices with requests.

Example output for a Gosund device again:
```
micha@LinuxBox:~/Smartdose$ ./Smartdose Gosund04 EVERY 10
Using 192.168.178.54:502:1
Power meter | Telnet server | Modbus server | Default: ON
Running since   48:05:50
ON time          0:00:50
OFF (  0) for   27:00:05
accumulated         0.01 kWh
Power               0.00 W
Voltage           231.64 V
Current             0.00 A
Auto power OFF  0.00 A for 0 turns
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
Power meter devices are accumulating the consumed power in a register.
To start from scratch, with the RESET command you can have this register set to zero again.

#### ADJUST, ADJUST V|A|W [measured_value]
The ADJUST command is used to read or set the internal measurement correction factors the power meter devices are maintaining.
Used without any additional parameter, ADJUST will print out the current correction factor values:
```
micha@LinuxBox:~/Smartdose$ ./Smartdose Gosund02 ADJUST
Using 192.168.178.51:502:1
Correction factors:
V:    1.02000
A:    1.75000
W:    1.04000
```
If you would like to change these factors, you will have to name the value you are going to correct - ``V`` for voltage, ``A`` for current and ``W`` for power measurements.

Without any further value, the command will reset the respective correction factor to 1.0:
```
micha@LinuxBox:~/Smartdose$ ./Smartdose Gosund02 ADJUST W 
Using 192.168.178.51:502:1
micha@LinuxBox:~/Smartdose$ ./Smartdose Gosund02 ADJUST
Using 192.168.178.51:502:1
Correction factors:
V:    1.02000
A:    1.75000
W:    1.00000
```

To set a specific factor, you have to add a measured value behind the category letter:
```
micha@LinuxBox:~/Smartdose$ ./Smartdose Gosund02 ADJUST W 62.4
Using 192.168.178.51:502:1
micha@LinuxBox:~/Smartdose$ ./Smartdose Gosund02 ADJUST
Using 192.168.178.51:502:1
Correction factors:
V:    1.02000
A:    1.75000
W:    1.04000
```

These changes are permanent and will have immediate effect.

#### EVENTS
If event tracking has been activated (by ``-DEVENT_TRACKING=1``), the ``EVENTS`` command will list all events recorded so far, but only up to the configured number of events back.
The default is 40. Output is a list of events in readable form:
```
micha@LinuxBox:~$ Smartdose Gosund03 events
Using 192.168.178.52:502:1
40 event slots found.
boot date            27.09
boot time            16:48
default on           16:48
button off           16:49
Modbus on            16:49
```

#### AUTOOFF <milliams> <cycles>
The power meter devices can be instructed to automatically switch off if the current was below a threshold for a given time.
Reasoning behind that function is: if you have a power consuming target connected, but wish to have it completely off after it has done its work,
in most cases the current will drop in idle state.

Smartdose devices with a power meter will track the current if it is below ``milliamps`` mA for at least ``cycles`` measurements.
The latter translates to a time, as measurements are taken every 5s by default.

A command like 
```
micha@LinuxBox:~$ Smartdose pool autooff 100 12
Using 192.168.178.42:502:1
```
will order the Smartdose to switch off, if the current was below 100mA for one minute (12 * 5 = 60 seconds).

Setting one or both parameters to zero will disable the feature.

#### TIMER <n>
The ``TIMER`` command is the way to display and alter the timers' programs.
``TIMER`` will need further sub-parameters to control its actions. The number ``n`` of the timer to be handled is mandatory in any case.
``n`` can be in the range 1..16 only.

If only a timer number is given, ``Smartdose`` will just print out the current data of that timer.
```
micha@LinuxBox:~/MBtools$ Smartdose Gosund03 Timer 11
Using 192.168.178.52:502:1
Timer 11: ACT  ON 07:30 WED
```

##### Sub-parameters
All further parameters to ``TIMER`` are processed left to right. A parameter further to the right will overwrite a previous parameter in case both are affecting the same data.
F.i. using ``CLEAR`` after having set days, time etc. will discard all these data!

###### ON, OFF
This parameter defines which type of switch the timer will trigger - socket to on or off.

###### ACTIVE, INACTIVE
A completely programmed timer can be deactivated. Its data is kept, but the timer will not fire until it is activated again.

###### CLEAR
To completely erase all data of a timer and deactivate it, the ``CLEAR`` sub-parameter is used.
It may be advisable to use a ``CLEAR`` as a first sub-parameter when setting up a new timer - to get rid of potential remains of a previous programming.

###### Day parameters DAILY, WORKDAYS, WEEKEND, SUNDAY, MONDAY etc.
Timers may be restricted to certain days of week. The days may be named individually in several independent parameters, or a group word can be used.
``DAILY`` will set all days of the week, whereas ``WEEKEND`` will set Saturday and Sunday only and ``WORKDAYS`` will set the complementary days Monday to Friday.
Group words and single days can be combined, like ``WEEKEND WEDNESDAY FRIDAY`` if needed.

###### Trigger time HH:MM
The time to have the timer trigger a switch is given as a 24-hour hours value, a colon separator and a minute value 0..59.
No blanks may be used in between!

##### Examples
To have your porch light switch on every day at 8pm and off again at 1:30am in the morning, you will want to program two timers:
```
micha@LinuxBox:~/MBtools$ Smartdose Gosund03 Timer 1 clear daily on 20:00 active
Using 192.168.178.52:502:1
Timer  1: ACT  ON 20:00 SUN MON TUE WED THU FRI SAT
micha@LinuxBox:~/MBtools$ Smartdose Gosund03 Timer 2 clear daily off 1:30 active
Using 192.168.178.52:502:1
Timer  2: ACT OFF 01:30 SUN MON TUE WED THU FRI SAT
```
Note the use of ``CLEAR`` before the programming proper.

During the week you may like to have your working room heated before you get there:
```
micha@LinuxBox:~/MBtools$ Smartdose RadiatorWRoom Timer 9 clear workdays on 7:15 active
Using 192.168.178.96:502:1
Timer  9: ACT  ON 07:15 MON TUE WED THU FRI
```
(You are supposed to switch it off when you leave...)
But hey, we can put a safety OFF in case you forgot:
```
micha@LinuxBox:~/MBtools$ Smartdose RadiatorWRoom Timer 10 clear workdays off 22:00 active
Using 192.168.178.96:502:1
Timer 10: ACT  OFF 22:00 MON TUE WED THU FRI
```