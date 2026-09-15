# FlyRadar32 Desktop Radar Console Enclosure

Production-ready, dual-shell 3D printable console enclosure for the **FlyRadar32** ADS-B / Flight Radar Ground Station.

---

## 1. Mechanical Architecture & Logic

### A. Lid & Precision Modern Aviation Bezel (Exact User Reference)
- **Ergonomic Desktop Tilt**: Front face angled at $10.0^\circ$ for optimal desktop viewing angle.
- **Clean Perpendicular Viewing Aperture**: Razor-sharp $35.2 \times 28.2\text{ mm}$ aperture without interior bevel steps or unsupported horizontal overhangs, guaranteeing clean face-down printing on textured PEI sheets.
- **Clear Window Area (Zero Screw Standoffs)**: Internal display cavity is 100% clear of obstructive screw pillars.
- **Sleek Screen Bezel**: High-contrast Cockpit Red border ($39.2 \times 32.2\text{ mm}$ outer, $1.5\text{ mm}$ corner radius) providing a uniform $2.0\text{ mm}$ bezel framing the display flush on the face ($0.40\text{ mm}$ depth).
- **Pure Clean Minimalist Faceplate (100% Negative Space)**: All floating lines, stripes, barcodes, and motifs completely removed. A smooth, pristine matte surface puts all visual focus on the screen aperture and tactile controls.
- **Full-Height Solid Corner Pillars**: Corner screw bosses extend 100% continuously from the split plane into the roof, anchored solidly to inner corner walls with zero mid-air gaps.
- **100% Continuous Alignment Skirt**: Full perimeter interlocking lip between lid and shell with zero cutouts or steps.

### B. Tactile Button Control Bank & Anti-Fallout Keycaps
- **Exact Push Switch Chamber ($6.25 \times 6.25\text{ mm}$)**:
  - Designed specifically for the user's $6.0 \times 6.0\text{ mm}$, $3.8\text{ mm}$ tall physical tactile switches.
  - Snug $0.125\text{ mm}$ clearance per side: zero rattle, zero tilting, and zero loose wobble.
  - Lateral $8.4\text{ mm}$ pin channels for through-hole solder pin clearance.
- **Anti-Fallout Keycap Solution**:
  - **Bottom Retaining Flange**: $\varnothing 5.50\text{ mm}$ diameter, $1.0\text{ mm}$ thickness. Easily drops through the $6.25 \times 6.25\text{ mm}$ switch pocket from the inside during assembly ($5.50 < 6.25$).
  - **Lid Through-Hole**: $\varnothing 4.00\text{ mm}$ circular hole on top face. Because the flange is $\varnothing 5.50\text{ mm}$, it overlaps the hole by $0.75\text{ mm}$ all around ($1.50\text{ mm}$ total diameter overlap). The cap **CANNOT pop out** through the top face!
  - **Gliding Top Stem**: $\varnothing 3.60\text{ mm}$ diameter with $0.3\text{ mm}$ chamfer. Glides inside the $\varnothing 4.00\text{ mm}$ hole with $0.20\text{ mm}$ radial clearance for effortless, smooth actuation.
  - **Deep Internal Plunger Socket**: $\varnothing 3.30\text{ mm}$ diameter, **$4.60\text{ mm}$ deep**! Custom-tailored to receive the user's $5.5\text{ mm}$ long tapered plunger ($\varnothing 3.5\text{ mm}$ base, $\varnothing 3.0\text{ mm}$ top) with solid concentric alignment and $0.8\text{ mm}$ switch actuation stroke.
- **Enlarged High-Visibility Tactile Glyphs**: Bold $\blacktriangle$ (UP, $4.2 \times 3.8\text{ mm}$), $\bullet$ (SEL, $\varnothing 3.8\text{ mm}$), and $\blacktriangledown$ (DOWN, $4.2 \times 3.8\text{ mm}$) indicators inlaid flush next to the button keycaps ($X = 22.2\text{ mm}$) with $0.40\text{ mm}$ depth for crisp multicolor slicing.

### C. Bottom Shell, Vertical Rounded Wall Flutes & ESP32 Locator Pins
- **100% Plane Bottom Plate**: Flat continuous bottom face with 4 precision M2 pan-head screw counterbores (badge recess completely eliminated).
- **Vertical Flutes with Smooth Rounded Concave Inner Cut (Non-Boxed)**:
  - Cylindrical concave grooves ($R = 1.25\text{ mm}$) with spherical rounded ends.
  - Cut depth is strictly **$0.35\text{ mm}$** into $1.80\text{ mm}$ outer walls, leaving **$1.45\text{ mm}$ of 100% solid plastic wall** behind every cut (>3 solid print perimeters with a 0.4mm nozzle, eliminating thin-wall slicing slits and sharp corners).
  - **Left wall**: 3 vertical rounded flutes.
  - **Right wall**: 2 vertical rounded flutes flanking the Type-C port.
  - **Front wall**: 3 vertical rounded flutes.
  - **Rear wall**: 3 vertical rounded flutes.
- **ESP32 4-Corner $\varnothing 2.80\text{ mm}$ Locator Sticks**:
  - Four precision vertical locator pins ($\varnothing 2.80\text{ mm}$, height $2.20\text{ mm}$) rise directly from the $0.8\text{ mm}$ floor resting pads at `(-16.55, ±13.50)` and `(33.95, ±13.50)`.
  - Enter the ESP32 board's four $\varnothing 3.00\text{ mm}$ corner mounting holes with $0.10\text{ mm}$ tolerance, locking the board in place with sub-millimeter precision.
  - Conical chamfer tips ($0.40\text{ mm}$) for smooth, guided drop-in installation.
- **Anti-Push Thrust Wall**: Solid $2.2\text{ mm}$ thick vertical backstop at $X = -17.8\text{ mm}$ absorbing 100% of USB-C cable insertion force without screws.
- **Side Support Rails & Snap Clips**: Lateral alignment rails and cantilever clips holding the PCB securely against the floor pads.
- **Precision Type-C Port**: Wide clearance pill cutout perfectly aligned with the ESP32 connector.

---

## 2. Clean Production Structure

```
enclosure/
├── FlyRadar32_Enclosure.FCStd       ← Master FreeCAD parametric CAD model
├── build_radar_console.py           ← Parametric Python build script
├── preprint_verify.py               ← 86-point QA validation script (All 86 PASS)
├── DOC-20260811-WA0006.pdf          ← Fastener reference invoice
├── README.md                        ← Project documentation
├── 3d files/                        ← Reference component STEP models
└── STLs/                            ← 3D PRINT FILES (STL + 3MF)
    ├── flyradar32_shell.stl         ← Bottom shell (Print in Black)
    ├── flyradar32_lid.stl           ← Top lid (Print in Black)
    ├── flyradar32_lid_accents.stl   ← Red screen border, accent bar, button glyphs (Print in Red)
    ├── flyradar32_button_caps.stl   ← 3x button keycaps (Print in Red)
    ├── flyradar32_lid_multicolor.3mf← Pre-aligned multicolor lid for Bambu/OrcaSlicer AMS
    └── flyradar32_console_multicolor.3mf ← Complete 4-part multi-color console assembly
```

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
