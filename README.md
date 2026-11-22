# Makload5000 PRO

DIY variable DC electronic load, now using an **Arduino Nano**, 16x2 I2C LCD, rotary encoder and a USB HTML dashboard.

**Start here:** [Project info, specs, wiring and instructions](Project-Info/PROJECT-GUIDE.md).

## Files

- [Arduino Nano firmware](Makload5000_PRO_Nano.ino)
- [HTML dashboard with gauges and calibration](Makload5000_PRO_Classic_Dashboard.html)
- [KiCad editable schematic](KiCad/Petnica%20DC%20load.kicad_sch) (older circuit, not yet revised for Nano)
- [Project-Info](Project-Info/) - specs, wiring, menus, instructions, and future photos/STLs
- [Old-Version](Old-Version/) - archived original code, dashboard and README

**Limits:** 15 V DC input and 3 A commanded current. 45 W is theoretical and not a tested continuous rating. Use a heatsink and current-limited bench supply, and verify the MOSFET safe operating area. Software safety trips do not replace hardware protection.

**Status:** Nano firmware and HTML dashboard have not yet been jointly validated on physical hardware. The updated Nano PCB, enclosure models and project photos have not yet been supplied.

[LICENSE](LICENSE)
