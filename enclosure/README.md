# FlyRadar32 Desktop Radar Console Enclosure

Production-ready, dual-shell 3D printable console enclosure for the **FlyRadar32** ADS-B / Flight Radar Ground Station.

---

## 1. Mechanical Architecture & Logic

### A. Lid & Display Retaining Bezel
- **Ergonomic Desktop Tilt**: Front face angled at $10.0^\circ$ for desktop visibility.
- **Active Glass Framing Window**: Clean, sharp front aperture ($36.0 \times 28.5\text{ mm}$) centered at local $(-5.15, 0.0)\text{ mm}$ exactly conforming to the $35.00 \times 28.00\text{ mm}$ active pixel matrix from the technical drawing.
- **Stepped Bezel Pocket (Inside)**: Recessed pocket that acts as a front retaining lip. The glass module sits against this lip and cannot push through the front panel.
- **4 Monolithic Screw Standoffs**: 4 solid cylindrical bosses with pilot holes for M2 self-tapping screws clamping the display against the bezel.
- **100% Continuous Alignment Skirt**: Full perimeter interlocking lip between lid and shell with zero cutouts or steps.

### B. Tactile Button Carrier (Beside Display)
- **Monolithic Wall-Supported Shelf**: Solid carrier bar fused directly into the right enclosure wall ($X \in [22.5, 35.2]\text{ mm}$), eliminating detached/floating walls.
- **Precision Switch Pockets**: 3 switch cavities ($6.20 \times 6.20 \times 3.60\text{ mm}$) with lower wire pass-through channels and retention lips.
- **Tactile Keycaps**: Stepped plunger caps with anti-fallout shoulder rings trapped under the lid bezel.

### C. Screwless ESP32 Plastic Snap-Fit Cradle (Bottom Floor)
- **Pins Desoldered**: ESP32 sits directly onto bottom floor support ribs at $Z = 2.60\text{ mm}$ (no tall standoffs needed).
- **Automatic Cantilever Snap Locks**: 4 snap clips with $45^\circ$ lead-in ramps that deflect outward during insertion and click securely over the PCB top face ($Z = 4.20\text{ mm}$).
- **Anti-Push Thrust Wall**: Solid $2.2\text{ mm}$ thick vertical backstop at $X = -19.2\text{ mm}$ absorbing 100% of USB-C cable insertion force without screws.
- **Precision Type-C Cutout**: Tight $10.0 \times 4.2\text{ mm}$ ($R = 2.1\text{ mm}$) pill cutout centered at $Z = 5.45\text{ mm}$ on the side wall with uniform $0.25\text{ mm}$ cable clearance.
- **Rubber Feet**: 4 circular pockets ($\varnothing 8.0\text{ mm} \times 0.65\text{ mm}$) for non-slip silicone bumper pads.

---

## 2. Production Files

| File | Description | Material / Infill |
| :--- | :--- | :--- |
| [`flyradar32_lid.stl`](file:///D:/Projects/Embedded/Firmware-Development/flyradar32/enclosure/flyradar32_lid.stl) | Console top lid with display bezel, screw standoffs & button cages | PLA/PETG, 0.16–0.20mm, 20% gyroid |
| [`flyradar32_shell.stl`](file:///D:/Projects/Embedded/Firmware-Development/flyradar32/enclosure/flyradar32_shell.stl) | Bottom shell with ESP32 cradle, thrust wall & USB-C cutout | PLA/PETG, 0.20mm, 20% gyroid |
| [`flyradar32_button_caps.stl`](file:///D:/Projects/Embedded/Firmware-Development/flyradar32/enclosure/flyradar32_button_caps.stl) | 3 tactile orange button caps with captive flange | PLA/PETG (Orange), 0.12mm, 100% infill |
| [`FlyRadar32_Enclosure.FCStd`](file:///D:/Projects/Embedded/Firmware-Development/flyradar32/enclosure/FlyRadar32_Enclosure.FCStd) | Master parametric CAD document | FreeCAD 1.1+ |
| [`build_radar_console.py`](file:///D:/Projects/Embedded/Firmware-Development/flyradar32/enclosure/build_radar_console.py) | Parametric build script with 20/20 boolean collision checks | Python 3 + FreeCAD |

---

## 3. Interference Matrix Verification

All 20 physical intersection checks between components, shell, lid, and buttons evaluate to **strictly $0.000000\text{ mm}^3$**:

```
  PASS: ESP32 <-> TFT                = 0.000000 mm3
  PASS: ESP32 <-> Button_UP          = 0.000000 mm3
  PASS: ESP32 <-> Button_SEL         = 0.000000 mm3
  PASS: ESP32 <-> Button_DOWN        = 0.000000 mm3
  PASS: TFT <-> Button_UP            = 0.000000 mm3
  PASS: TFT <-> Button_SEL           = 0.000000 mm3
  PASS: TFT <-> Button_DOWN          = 0.000000 mm3
  PASS: Shell <-> ESP32              = 0.000000 mm3
  PASS: Shell <-> TFT                = 0.000000 mm3
  PASS: Shell <-> Button_UP          = 0.000000 mm3
  PASS: Shell <-> Button_SEL         = 0.000000 mm3
  PASS: Shell <-> Button_DOWN        = 0.000000 mm3
  PASS: Lid <-> ESP32                = 0.000000 mm3
  PASS: Lid <-> TFT                  = 0.000000 mm3
  PASS: Lid <-> Button_UP            = 0.000000 mm3
  PASS: Lid <-> Button_SEL           = 0.000000 mm3
  PASS: Lid <-> Button_DOWN          = 0.000000 mm3
  PASS: Lid <-> Button_Caps          = 0.000000 mm3
  PASS: Shell <-> Button_Caps        = 0.000000 mm3
  PASS: Shell <-> Lid (Mating)       = 0.000000 mm3
```
