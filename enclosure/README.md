# FlyRadar32 Desktop Radar Console Enclosure

Production-grade, snap-locking desktop instrument console for the FlyRadar32 standalone ADS-B ground station.

---

## Overview

The enclosure is engineered specifically for FDM additive manufacturing. It houses an ESP32-WROOM-32 development board (30-pin, micro-USB or USB-C), a 1.8-inch ST7735 SPI TFT display module, and three 6x6mm tactile switches without requiring glue or external fasteners for closure.

The assembly consists of three printable components:

1. **Bottom Shell (`flyradar32_shell.stl`)**: Chassis base with internal ESP32 cradle rails, corner alignment pins, solid anti-push USB thrust wall, and internal snap-locking tabs.
2. **Bezel Lid (`flyradar32_lid.stl`)**: Ergonomic 15-degree desktop viewing faceplate, perimeter display frame, internal snap-fit receptacles, and switch actuator guides.
3. **Button Keycaps (`flyradar32_button_caps.stl`)**: Anti-fallout tactile keycaps with retaining flanges and 0.25 mm radial gliding clearance.

Master parametric source: `FlyRadar32_Enclosure.FCStd` (FreeCAD 0.21 / 1.0 compatible).

---

## Component Specifications

| File | Description | Material | Infill | Supports |
|------|-------------|----------|--------|----------|
| `flyradar32_shell.stl` | Bottom chassis with USB port and board rails | PETG or PLA+ | 20% Gyroid | None |
| `flyradar32_lid.stl` | Top faceplate with 15-degree instrument tilt | PETG or PLA+ | 20% Gyroid | None |
| `flyradar32_button_caps.stl` | 3x tactile actuator keycaps with retention lips | PETG or PLA+ | 100% Solid | None |

---

## Recommended Print Settings

All parts are designed to be printed directly on a flat build plate without supports or brim:

- **Layer Height:** 0.20 mm (0.16 mm recommended for keycaps)
- **Line Width:** 0.40 mm
- **Perimeters / Wall Loops:** 3 (minimum 1.2 mm wall thickness)
- **Top / Bottom Solid Layers:** 4 top, 4 bottom
- **Infill:** 20% Gyroid or Grid
- **Bed Temperature:** 60 deg C (PLA) / 75 deg C (PETG)
- **Nozzle Temperature:** Per filament manufacturer specifications
- **Orientation:**
  - `flyradar32_shell.stl`: Print flat with bottom exterior surface facing down.
  - `flyradar32_lid.stl`: Print flat with top face down on textured PEI or smooth sheet.
  - `flyradar32_button_caps.stl`: Print with flange base down.

---

## Mechanical Tolerances and Clearances

- **Display Aperture:** 35.20 mm x 28.20 mm viewing window, recessed with zero perimeter obstruction.
- **ESP32 Cradle:** Four 2.40 mm vertical locator pins matching 3.20 mm PCB mounting holes with 0.40 mm clearance.
- **USB-C Port Cutout:** 11.20 mm x 5.00 mm stadium profile, fully enclosing standard cable overmolds.
- **Snap Joints:** 0.30 mm positive engagement on 4 internal reinforcement ribs with zero exterior wall blowouts.
- **Keycaps:** 5.50 mm base retention flange engaging 4.00 mm lid apertures (1.50 mm overlap prevents keycaps from falling out through the front).

---

## Assembly Instructions

1. **Keycaps Installation:**
   - Drop the three button keycaps into the underside of the lid through the switch chambers before mounting electronics. The wide retaining flanges prevent them from falling forward.
2. **Display Mounting:**
   - Align the 1.8-inch ST7735 TFT module into the display locating frame on the underside of the lid.
3. **ESP32 Installation:**
   - Place the ESP32 board into the bottom shell, sliding the USB port into the rear cutout. Press down firmly so the four chassis locator pins engage the board corner mounting holes.
4. **Wiring Connection:**
   - Connect the pre-soldered display and switch flying leads to the ESP32 GPIO headers as defined in the main project pinout table.
5. **Enclosure Closure:**
   - Align the front lip of the lid with the shell and press down along the perimeter until the snap-fit ribs engage with an audible click.
