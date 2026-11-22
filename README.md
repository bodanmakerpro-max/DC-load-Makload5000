# Makload5000 PRO | Arduino Nano DC Electronic Load

Makload5000 PRO is a DIY adjustable DC electronic load. The current version uses an **Arduino Nano**, **16x2 I2C LCD**, **rotary encoder**, **USB HTML dashboard**, shunt-current feedback, and a voltage sensing divider.

**Electrical design target:** 0-15 V DC input and 0-3 A commanded load current. 15 V x 3 A is 45 W theoretical heat dissipation. That is NOT a validated continuous rating. MOSFET safe operating area, cooling, shunt dissipation, wiring and protections must be checked. Use a current-limited supply for initial tests.

## Project folders

| Folder | Contents |
| --- | --- |
| [KiCad](KiCad/) | Existing editable KiCad schematic (legacy design, not yet updated to match Nano wiring) |
| [Schematics](Schematics/) | Updated Nano wiring, resistor networks and component values |
| [Code-and-Software](Code-and-Software/) | Nano firmware and classic HTML dashboard |
| [How-to-Use](How-to-Use/) | Startup, LCD menus, dashboard connection, calibration and testing |
| [STL-Files](STL-Files/) | Enclosure instructions and placeholder for the new 3D-printed case files |

**Start here:** [How-to-Use/USER-GUIDE.md](How-to-Use/USER-GUIDE.md).

## Current control features

- Standalone Nano control through rotary encoder and 16x2 display
- Current setpoint, measured current, input voltage, load power, running/OFF/trip status
- Settings: current step, max setpoint, LCD backlight, saved setpoint, reset settings
- Closed-loop PWM adjustment on D9; shunt voltage measurement on A0
- Three-point current calibration stored in Arduino EEPROM
- Chrome/Edge USB dashboard with current/voltage gauges, developer menu and calibration
- Browser-only voltage display calibration (does not change firmware trip threshold)

## Safety and project status

This is experimental hardware and software, **not a certified electronic load**. No independent temperature sensor or hardware current limiter is included in this revision. The 15 V / 3 A boundary alone does not prove 45 W continuous capability.

The firmware and dashboard have not been verified as an integrated build on your physical hardware. The KiCad schematic in `KiCad/` is an older editable project and must **not** be treated as an updated production-ready PCB. Enclosure STL models have not been supplied yet.

Do not power a 5 V Nano directly from the load's 15 V input. Keep the LM358 supply arrangement as designed and tie the analog grounds correctly. A direct A0 connection is particularly vulnerable to faults; a series protection resistor and filter are recommended.

## Source and license

This repository keeps its original [LICENSE](LICENSE). The previous root-level HTML and firmware files are retired in favor of the organized PRO version. Git history retains earlier versions.
