# Makload5000 - Computer-Controlled and Standalone Variable DC Load

## ! READ ME BEFORE USE !

Makload5000 is a variable DC electronic load for testing low-voltage DC power supplies, batteries, converters and similar DC sources.

The load can now be controlled in two ways:

1. From the existing HTML computer dashboard through USB.
2. Directly from the device using a 16x2 LCD and rotary encoder.

The HTML dashboard is still supported and has not been removed.

## Electrical limits

- Maximum input voltage: **15 V DC**
- Maximum load current: **3 A**
- Maximum theoretical input power: **45 W**
- Control electronics supply: **9 V DC from a separate power source**
- Arduino board: **5 V Pro Micro / ATmega32U4**

Do not exceed 15 V or 3 A.

The real continuous power limit depends on MOSFET Safe Operating Area, heatsink size, airflow and temperature.

## Main load connections

### Shunt current measurement

~~~text
Shunt + ---- 4.7kΩ ----+---- Arduino A0
                        |
                      100nF
                        |
                       GND

Shunt - --------------- Arduino GND
~~~

The 4.7 kΩ resistor limits current into the Arduino analog input if a spike occurs.

The 100 nF capacitor filters high-frequency noise and short spikes.

The shunt scale is approximately:

~~~text
0.100 V = 1 A
0.200 V = 2 A
0.300 V = 3 A
~~~

### Arduino PWM to LM358

~~~text
Arduino D9 ---- 39kΩ ----+---- LM358 + input
                         |
                         +---- 10kΩ ---- GND
                         |
                         +---- 10µF ---- GND
~~~

For the 10 µF electrolytic capacitor:

~~~text
+ terminal -> control node / LM358 + input
- terminal -> GND
~~~

The LM358 output drives the MOSFET gate through a 100 Ω resistor:

~~~text
LM358 output ---- 100Ω ---- MOSFET gate
~~~

## 16x2 LCD wiring

This version uses the same type of normal 16x2 parallel LCD used in the earlier RFID project. It is not an I2C LCD.

~~~text
LCD pin       Pro Micro
-------------------------
VSS           GND
VDD           5V
VO            middle pin of 10k contrast potentiometer
RS            D4
RW            GND
E             D5
D4            D6
D5            D7
D6            D8
D7            D10
A / LED+      5V
K / LED-      GND
~~~

For the contrast potentiometer:

~~~text
5V ---- outer pin
GND --- outer pin
VO ---- middle pin
~~~

If your LCD module does not already contain a backlight resistor, add a suitable series resistor for the backlight.

## Rotary encoder wiring

For a KY-040 style rotary encoder:

~~~text
Encoder pin   Pro Micro
-------------------------
CLK           D2
DT            D3
SW            D15
+ / VCC       5V
GND           GND
~~~

D9 remains reserved for PWM control and A0 remains reserved for shunt measurement.

## Local standalone controls

You do not need to keep a computer connected during normal use after calibration.

### Home screen

The LCD displays something like:

~~~text
DRAW 1.25A
SET  1.50A RUN
~~~

- DRAW = measured current from the shunt
- SET = requested current
- RUN = load active
- OFF = load disabled
- TRIP = over-current trip active

### Rotary encoder

On the home screen:

- Rotate clockwise or counter-clockwise to change the current setpoint.
- Current changes in **0.05 A steps**.
- Press the built-in encoder button to open the menu.

### Menu

Rotate to navigate and press to select.

Menu items:

~~~text
LOAD ON / LOAD OFF
SHUNT INFO
CONTROL INFO
CLEAR TRIP
BACK
~~~

### Shunt info

Shows the measured shunt voltage and calculated current.

### Control info

Shows the control voltage and PWM value being sent toward the LM358.

## Computer control

The existing HTML dashboard still works through USB Serial at **115200 baud**.

The computer dashboard can:

- Set load current.
- Turn load ON and OFF.
- Monitor measured current.
- Monitor shunt voltage.
- View PWM and control voltage.
- Run the 3-point calibration.
- Clear a trip.
- View serial/debug information.

Local LCD/encoder control and HTML control can both be used with the same firmware.

If the computer changes the setpoint, the LCD updates automatically.

If the rotary encoder changes the setpoint, the new value is used by the same closed-loop controller.

## Calibration

Calibration should still be done through the HTML dashboard using an accurate external current meter or laboratory power supply.

Calibration is stored in EEPROM, so after calibration the Arduino remembers it even when the computer is disconnected.

Recommended calibration points are approximately:

~~~text
1 A
2 A
3 A
~~~

Enter the current that the external instrument actually measures.

Example:

~~~text
Load approximately 1 A
Lab supply reads 0.97 A
Enter 0.970 A

Load approximately 2 A
Lab supply reads 1.94 A
Enter 1.940 A

Load approximately 3 A
Lab supply reads 2.91 A
Enter 2.910 A
~~~

Do not enter exactly 1.000 A, 2.000 A and 3.000 A unless the external meter really reads those values.

## No input-voltage monitoring

This version does not measure the voltage of the device being tested.

Use a multimeter or laboratory power supply to monitor input voltage separately.

## Basic standalone operating procedure

1. Power the load control electronics from the separate 9 V supply.
2. The LCD should show MAKLOAD5000 during startup.
3. Connect the DC source being tested.
4. Make sure polarity is correct.
5. Rotate the encoder to choose the current.
6. Press the encoder to enter the menu.
7. Select LOAD ON.
8. Watch the measured current on the LCD.
9. Use LOAD OFF when finished.

Start at a low current and increase slowly.

## Safety

Do not exceed:

~~~text
15 V
3 A
~~~

At 15 V and 3 A the load could be asked to dissipate up to 45 W.

That does not mean the MOSFET can safely dissipate 45 W continuously.

MOSFET Safe Operating Area, junction temperature, heatsink temperature and cooling must all be considered.

Use an adequate heatsink and do not operate the electronic load unattended at high power.

## Repository

Project source, HTML dashboard, Arduino firmware, KiCad schematic and documentation:

https://github.com/bodanmakerpro-max/DC-load-Makload5000
