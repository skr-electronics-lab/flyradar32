# FlyRadar32 3D Printable Desktop Enclosure

A custom, avionics-inspired desktop ATC radar console enclosure designed specifically for the **FlyRadar32** ground station.

![FlyRadar32 Enclosure 3D Hero Preview](preview_hero.png)

---

## 🧰 Hardware & Compatibility

| Component | Specification | Notes |
|---|---|---|
| **Microcontroller** | ESP32 DevKit V1 (30-pin CH340 / CP2102) | Snug friction cradle with side retention guides + rear USB port access |
| **Display** | AZ-Delivery 1.8" ST7735 128×160 SPI TFT | Exact AP242 CAD calibrated: 52.0×28.5mm hole pitch, -0.8mm glass offset |
| **Buttons** | 3× 6×6mm tactile pushbuttons (`push_switch_small`) | UP (▲), SELECT (SEL), DOWN (▼) with captive printable actuator caps |
| **Button Carrier** | Dual M2 interior standoff bosses (17.5mm half-pitch) | For mounting button perfboard or carrier bracket behind bezel |
| **Fasteners** | 4× M3 × 16mm screws | Bottom countersunk (no visible screws on front face) |
| **Thread Inserts** | 4× M3 heat-set brass inserts (OD ~4.2mm) | *Optional* — screws can also self-tap directly into top bosses |
| **Display Screws**| 4× M2 × 4mm or M2.5 × 6mm self-tapping | Secures ST7735 PCB into 5.8mm OD bosses |
| **Desk Feet** | 4× 8mm rubber adhesive bumpers | Recessed circular slots in bottom chassis base |

---

## 📐 Design Features

- **Ergonomic Desktop Incline**: Sleek 9° inclined face for natural, comfortable viewing on your desk while seated.
- **Pristine Avionics Face**: Completely clean front panel with **zero exposed screws**; clean debossed `FLYRADAR 32` and `SKR ELECTRONICS LAB` branding.
- **Low-Profile Tactile Buttons**: Sleek 6.6mm disc button caps protruding only 1.8mm above the bezel face, with captive retention flanges underneath so they can never fall out.
- **Flush Display Fit**: Precision-beveled aperture with an internal pocket that seats the ST7735 glass flush against the front bezel with minimal border.
- **Passive Thermal Management**: Dual convective cooling louvers along the bottom base and rear exhaust keep the ESP32 Wi-Fi radio cool during 24/7 flight tracking.
- **Clean Cable Access**: Rear oval USB cutout accommodates standard Micro-USB and USB-C cable heads without binding.
- **Interlocking Mating Lip**: Perimeter tongue-and-groove joint between top and bottom halves prevents light bleed and ensures rigid alignment.

---

## 🖨️ 3D Printing Guidelines

![Print Bed Layout](preview_plate.png)

### Recommended Slicer Settings
- **Material**: PETG, PLA, or ABS/ASA (Matte Black, Dark Navy, or Slate Grey recommended).
- **Layer Height**: `0.20 mm` (or `0.16 mm` for ultra-smooth bevels).
- **Perimeters / Walls**: `4 to 6 walls` (for solid structural rigidity around screw bosses).
- **Infill**: `20%` (Gyroid or Grid).
- **Supports**:
  - `bottom_case`: **No supports required** (prints flat on base).
  - `button_caps`: **No supports required** (prints flat on flange).
  - `top_bezel`: Minimal tree supports under the internal hollow cavity.

---

## 💻 OpenSCAD Customization

The `.scad` model is 100% parametric. Open [`flyradar32_case.scad`](flyradar32_case.scad) in OpenSCAD and use the **Customizer** panel:

- `part = "assembly"` — 3D color-coded preview with hardware mockups.
- `part = "top"` — Top bezel oriented for export.
- `part = "bottom"` — Bottom chassis oriented for export.
- `part = "buttons"` — Set of 3 button caps.
- `part = "plate"` — Complete print bed layout with all parts arranged for a single print job.

### Exporting STLs via Command Line
```powershell
# Export Bottom Chassis
& "C:\Program Files\OpenSCAD\openscad.com" -o flyradar32_bottom.stl -D 'part=\"bottom\"' flyradar32_case.scad

# Export Top Bezel
& "C:\Program Files\OpenSCAD\openscad.com" -o flyradar32_top.stl -D 'part=\"top\"' flyradar32_case.scad

# Export Button Caps
& "C:\Program Files\OpenSCAD\openscad.com" -o flyradar32_buttons.stl -D 'part=\"buttons\"' flyradar32_case.scad
```
