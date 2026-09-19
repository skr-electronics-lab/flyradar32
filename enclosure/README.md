# FlyRadar32 3D Printable Enclosure

Snap-fit desktop console enclosure for the FlyRadar32 standalone ADS-B ground station.

---

<div align="center">

![FlyRadar32 3D Printable Console](../assets/enclosure_hero.png)

</div>

---

## Printable Files

The complete enclosure consists of three printable STL files designed for zero-support FDM printing:

| File | Description | Material | Infill | Supports |
|------|-------------|----------|--------|----------|
| [`flyradar32_shell.stl`](flyradar32_shell.stl) | Bottom chassis with ESP32 mounting rails & USB port | PLA / PETG | 20% | None |
| [`flyradar32_lid.stl`](flyradar32_lid.stl) | Top bezel with 15° desktop tilt & display frame | PLA / PETG | 20% | None |
| [`flyradar32_button_caps.stl`](flyradar32_button_caps.stl) | 3x tactile button actuator keycaps | PLA / PETG | 100% | None |

Master CAD Source: [`FlyRadar32_Enclosure.FCStd`](FlyRadar32_Enclosure.FCStd) (FreeCAD).

---

## Hardware Fit & Assembly

<div align="center">

| Internal Seating | Transparent CAD Assembly |
|:---:|:---:|
| ![Internal Seating](../assets/enclosure_internal.png) | ![Transparent Assembly](../assets/enclosure_transparent.png) |

</div>

### Quick Print Settings

- **Layer Height:** 0.20 mm (0.16 mm for keycaps)
- **Perimeters / Walls:** 3
- **Infill:** 20% Gyroid or Grid
- **Supports:** None needed (all overhangs <= 45°)
- **Orientation:**
  - `flyradar32_shell.stl`: Flat on bottom
  - `flyradar32_lid.stl`: Face-down on print bed
  - `flyradar32_button_caps.stl`: Flange down

### Assembly

1. Drop the 3 button keycaps into the underside of the lid through the switch holes (the retaining flanges keep them from falling out).
2. Seat the 1.8" ST7735 display into the lid frame.
3. Slide the ESP32 into the bottom shell rails and push onto the 4 alignment pins.
4. Snap the lid onto the bottom shell — the internal snap clips lock firmly without screws or glue.
