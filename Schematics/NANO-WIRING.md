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
