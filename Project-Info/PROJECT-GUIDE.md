# Makload5000 PRO | Specs, wiring and instructions

This is the current Arduino Nano version. Detailed test results, build photos and STL enclosure files have not yet been supplied.

**Specs:** 0-15 V input design, 0-3 A set current, up to 45 W theoretical (not confirmed continuous), 16x2 I2C LCD, encoder, Nano USB HTML dashboard, shunt feedback. Current calibration is stored in Nano EEPROM; voltage calibration in the dashboard affects the browser display only.

**PCB note:** The only editable KiCad file is an earlier schematic and has **not** been updated to match the new Nano build. No verified PCB file has been provided.

**Images and box:** Photos and STL files can go in this Project-Info folder when supplied.

---

# Nano wiring and circuits

## Nano pin map

| Nano | Connection |
|---|---|
| A0 | Shunt positive sense (0.100 V/A nominal; shunt negative to common GND) |
| A1 | Input voltage sensing network |
| A4 | I2C LCD SDA |
| A5 | I2C LCD SCL |
| D4 | Rotary encoder CLK |
| D5 | Rotary encoder DT |
| D6 | Rotary encoder SW |
| D9 | PWM-to-voltage converter input |
| 5V | 5 V LCD and encoder |
| GND | Common circuit ground |

## 1. PWM to filtered control voltage

```text
Nano D9 --- 39 kOhm ---+------ LM358 noninverting (+) input
                       |
                     10 kOhm
                       |
                      GND

At LM358 input node: 10 uF capacitor to GND
Capacitor positive to node, negative to GND.
```

With a 5 V Nano: full PWM scale about 1.02 V. Software intentionally limits PWM to approximately 0.7 V. The LM358 output connects to the MOSFET gate through the original gate resistor. The op-amp feedback circuit must be wired according to the actual power stage, not guessed from this signal-only drawing.

## 2. Voltage measurement: 0-15 V

```text
Load input + --- 220 kOhm ---+--- 1 kOhm --- A1
                             |               |
                           10 kOhm         100 nF
                             |               |
Load input - / GND ----------+---------------+
```

The 220k/10k divider scales 15 V to ~0.652 V; code uses the Nano's nominal internal 1.1 V reference and divider ratio 23. Real ADC reference and resistor tolerances affect accuracy. This is **not** an isolated measurement and is only suitable for an agreed common-ground positive source. Protect against incorrect polarity and transients.

## 3. Shunt measurement

```text
Shunt positive ---- A0 (direct only after checking fault voltages)
Shunt negative ---- GND
```

Nominal values: 1 A -> 0.100 V, 2 A -> 0.200 V, 3 A -> 0.300 V. The safer recommended production version uses 4.7k in series with A0 and 100 nF from A0 to GND. The 1.1 V ADC reference is **not** input overvoltage protection.

## 4. LCD and rotary encoder

LCD backpack: GND->GND, VCC->5V, SDA->A4, SCL->A5. Default I2C address 0x27; some modules use 0x3F.

Encoder KY-040: CLK->D4, DT->D5, SW->D6, +->5V, GND->GND.

## Parts for Nano measurement and PWM circuits only

- 39k: 1
- 220k: 1
- 10k: 2
- 1k: 1
- 10uF electrolytic: 1
- 100nF ceramic: 1 (voltage A1); add another for protected A0
- Optional recommended 4.7k: 1 for A0

Resistors can be 1/4 W in these signal paths. The separate power stage, MOSFET, op-amp, shunt, cooling and protection parts are not included in this short list.


---

# Makload5000 PRO | How to use

## 1. Prepare and upload

1. Build the measurement networks exactly as documented in the wiring section below. Check polarities and common ground.
2. In Arduino IDE install `LiquidCrystal_I2C` by Library Manager.
3. Select **Arduino Nano** and the correct COM port. For older clones, try **ATmega328P (Old Bootloader)** if uploading fails.
4. Open [Makload5000_PRO_Nano.ino](../Makload5000_PRO_Nano.ino) and upload. Arduino output D9 should be zero on startup.
5. Keep the electronic-load input disconnected until the LCD, encoder and zero-output tests pass.

## 2. LCD home page

Top row: **S** means current setpoint; **M** means current measured from shunt. ON/OFF/TRIP indicates state.

Bottom row: measured input **V** and calculated load **W**.

The load boots **OFF** regardless of the saved setpoint.

## 3. Rotary encoder

- Turn knob on home page: adjust target current.
- Short press from home: enter main menu.
- Turn knob in menu: move between options.
- Short press: select.
- Hold for about 0.75 seconds on home: toggle load ON/OFF.
- Hold in menus: return toward previous screen/home.

### Main menu

- **Load power**: manually enable/disable load. Load will not enable when setpoint is zero or safety trip is active.
- **Settings**: open adjustment preferences.
- **Device information**: version and limits.
- **Clear trip**: reset a software trip, leaving load OFF.
- **Back**: return to home.

### Settings

- **Current step**: cycles 0.01 A, 0.05 A, 0.10 A per encoder click.
- **Max current**: adjustable cap between 0.5 and 3.0 A.
- **Backlight**: always ON or automatic timeout around 30 seconds.
- **Save setpoint**: remember selected current between reboots. Device still boots OFF.
- **Reset settings**: restores standard preferences.
- **Back**: main menu.

## 4. Computer dashboard

1. Use desktop Chrome or Edge. Open [Makload5000_PRO_Classic_Dashboard.html](../Makload5000_PRO_Classic_Dashboard.html) from your computer.
2. Close Arduino IDE Serial Monitor so USB port is free.
3. Click **Connect Arduino**, select the Nano, and connect at **115200 baud**.
4. View current and voltage gauges and live setpoint/power readings.
5. Select a low setpoint, click Apply, and click **Load ON** only after connecting a properly limited supply.
6. Use **Load OFF** to disable, and **Clear Trip** only after the fault is corrected.
7. Developer menu shows raw telemetry, sample count and serial commands.

### Calibration

**Current:** use a trusted external current meter or a reliable lab power supply. Set the load near 1 A, then enter the **actual** external current reading and select the point 1 capture. Repeat near 2 A and 3 A. The dashboard sends `CAL 1 <amps>`, `CAL 2 <amps>`, `CAL 3 <amps>` to the Nano. Nano stores shunt-voltage-to-current points in EEPROM and regulates based on them. `GETCAL` reads points; `CALRESET` erases them. Do not claim precision until verified.

**Voltage:** apply a known positive DC input voltage and enter the accurate multimeter result. Capture voltage calibration updates the HTML gauge correction stored in that browser, **not** the Nano's overvoltage trip. Firmware voltage calibration constant remains separate. You must verify raw voltage displayed on the Nano before trusting protection.

## 5. First power test

Start with a low input voltage, a current-limited source, and a small target current. Check A0 and A1 with a multimeter, the D9 filter output voltage, and actual heat dissipated in the MOSFET and shunt. Do not leave the load unattended. Never assume 45 W continuous capacity. Software protection is not a substitute for hardware safe-operating-area, thermal, fuse and reverse-polarity protections.

## 6. Serial protocol

Firmware sends:
```text
DATA,set_A,measured_A,shunt_V,control_V,pwm,enabled,tripped
VDATA,input_V,power_W
CALDATA,valid1,shunt1,amps1,valid2,shunt2,amps2,valid3,shunt3,amps3
```
Commands: `SET 1.00`, `ON`, `OFF`, `CLEAR`, `CAL 1 0.970`, `GETCAL`, `CALRESET`.

## 7. Troubleshooting

- LCD blank: run I2C scanner; verify A4 SDA/A5 SCL, 5 V and contrast. Use 0x3F instead of 0x27 if needed.
- Encoder inverted: swap CLK and DT (after powering off).
- HTML not connecting: use Chrome/Edge; close Serial Monitor; replug Nano and select correct COM port.
- No live voltage: check A1 divider, shared ground and VDATA frames.
- Wrong current: perform calibration with trusted meter, check 1.1 V reference and shunt wiring.

