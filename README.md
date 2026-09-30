Makload5000 – Computer-Controlled Variable DC Load
! READ ME BEFORE USE !
Makload5000 is a computer-controlled variable DC electronic load designed for testing low-voltage DC power supplies, batteries, converters, and similar DC sources.
The load is controlled entirely through a computer using the HTML control dashboard.
There is no local control interface on this version.

Electrical Limits
Maximum input voltage: 15 V DC
Maximum load current: 3 A
Maximum theoretical input power: 45 W

Control electronics supply: 9 V DC from a separate power source
Do not exceed 15 V or 3 A.

What You Need
To use the electronic load you need:
1. The Makload5000 electronic load.
2. A separate 9 V DC supply for the control electronics.
3. A computer connected to the Arduino through USB.
4. The HTML control dashboard from the GitHub repository.
5. A source to test, with a maximum of 15 V.
6. An external accurate current meter or laboratory power supply for calibration.
How the Project Works
The electronic load uses a power MOSFET as a controllable load.
The current path is approximately:
DC INPUT +
    |
    |
 MOSFET
    |
    |
 SHUNT
    |
DC INPUT -

The voltage across the shunt resistor is proportional to the load current.
The current-sense scale is approximately:
0.100 V = 1 A
0.200 V = 2 A
0.300 V = 3 A

The Arduino measures the shunt voltage and sends the measured current to the computer.
The computer dashboard sends the requested current to the Arduino.
The Arduino then adjusts its PWM output until the measured current reaches the requested current.
Arduino to Op-Amp Control
The Arduino PWM signal is converted into an approximately 0–1 V analog control signal before entering the LM358.
Arduino D9 ---- 39kΩ ----+---- LM358 + input
                         |
                         +---- 10kΩ ---- GND
                         |
                         +---- 10µF ---- GND


The 39 kΩ and 10 kΩ resistors form a voltage divider.
The 10 µF capacitor smooths the PWM into a DC control voltage.
The LM358 output drives the MOSFET gate through a resistor:
LM358 output ---- 100Ω ---- MOSFET gate

Shunt Measurement
Shunt + ---- 4.7kΩ ----+---- Arduino A0
                        |
                      100nF
                        |
                       GND

Shunt - --------------- Arduino GND

The 4.7 kΩ resistor limits current into the Arduino analog input if a voltage spike occurs.
The 100 nF ceramic capacitor filters high-frequency noise and short spikes.

Computer Control
All user controls are handled through the computer dashboard.
The software allows the user to:
- Set the required load current.
- Turn the load ON.
- Turn the load OFF.
- Monitor measured current.
- Monitor shunt voltage.
- View the Arduino control output.
- Calibrate the current measurement.
- Clear an over-current trip.
- View debug and serial information.

Calibration Is Required Before Accurate Use
The load must be calibrated in the software before accurate measurements are expected.
Calibration compensates for:
- Shunt resistor tolerance.
- Arduino ADC reference tolerance.
- Wiring resistance.
- Component tolerances.
- Measurement error.
Use a reliable external ammeter or a laboratory power supply with an accurate current display.
Calibration Procedure
1. Connect the electronic load to a suitable laboratory power supply.
2. Connect the Arduino to the computer.
3. Open the HTML control dashboard.
4. Connect the dashboard to the Arduino.
5. Start at a low current.
6. Calibrate approximately at 1 A, 2 A, and 3 A.
7. At each calibration point, enter the current that the external instrument actually measures.
Example:
Load approximately 1 A
Lab supply reads 0.97 A
Enter: 0.970 A

Load approximately 2 A
Lab supply reads 1.94 A
Enter: 1.940 A

Load approximately 3 A
Lab supply reads 2.91 A
Enter: 2.910 A

Do not enter exactly 1.000 A, 2.000 A, and 3.000 A unless the external meter actually reads those values.
No Voltage Monitoring

Use a multimeter or laboratory power supply to monitor the input voltage separately.
Basic Operating Procedure
1. Connect the separate 9 V supply to the electronic load control electronics.
2. Connect the Arduino to the computer through USB.
3. Open the HTML control dashboard.
4. Connect the dashboard to the Arduino.
5. Connect the DC source being tested.
6. Make sure polarity is correct.
7. Set a low current first.
8. Turn the load ON.
9. Confirm the measured current is reasonable.
10. Increase the current slowly as required.
