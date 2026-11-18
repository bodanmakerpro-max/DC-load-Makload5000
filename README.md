# Makload5000 - Computer-Controlled and Standalone Variable DC Load

## ! READ ME BEFORE USE !

Makload5000 is a variable DC electronic load for testing low-voltage DC power supplies, batteries, converters and similar DC sources.

The load can now be controlled in two ways:

1. From the existing HTML computer dashboard through USB.
2. Directly from the device using the small 0.96-inch SSD1306 128x64 OLED and rotary encoder.

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

## 0.96-inch OLED wiring

The standalone display is the same small OLED type used in the RFID/ESP32 project:

- **SSD1306**
- **128x64 pixels**
- **I2C**
- Usually address **0x3C**
- 4 pins: **GND, VCC, SCL, SDA**

On a 5 V Pro Micro:

~~~text
OLED pin      Pro Micro
-----------------------
GND           GND
VCC           5V
SDA           D2
SCL           D3
~~~

D2 and D3 are the Pro Micro hardware I2C pins.

The firmware uses the **U8g2** library and the SSD1306 128x64 hardware-I2C driver.

## Rotary encoder wiring

Because D2 and D3 are now used by the OLED, the encoder uses D4, D5 and D6:

~~~text
Encoder pin   Pro Micro
-----------------------
CLK           D4
DT            D5
SW            D6
VCC           5V
GND           GND
~~~

D9 remains reserved for PWM control and A0 remains reserved for shunt measurement.

## Local standalone controls

You do not need to keep a computer connected during normal use after calibration.

### Home screen

The OLED home screen shows the actual current, selected current and load state. It looks roughly like:

~~~text
MAKLOAD5000          RUN
------------------------
        1.25 A

SET 1.50A   PRESS=MENU
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

If the computer changes the setpoint, the OLED updates automatically.

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
2. The OLED should show MAKLOAD5000 during startup.
3. Connect the DC source being tested.
4. Make sure polarity is correct.
5. Rotate the encoder to choose the current.
6. Press the encoder to enter the menu.
7. Select LOAD ON.
8. Watch the measured current on the OLED.
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
