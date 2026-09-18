# FlyRadar32 Desktop Radar Console Enclosure

Production-ready, dual-shell 3D printable console enclosure for the **FlyRadar32** ADS-B / Flight Radar Ground Station.

---

## 1. Mechanical Architecture & Logic

### A. Lid & Precision Modern Aviation Bezel
- **Ergonomic 15° Desktop Tilt**: Front face angled at $15.0^\circ$ for an optimal desktop instrument viewing angle.
- **Clean Perpendicular Viewing Aperture**: Razor-sharp $35.2 \times 28.2\text{ mm}$ aperture without interior bevel steps or unsupported horizontal overhangs, guaranteeing clean face-down printing on textured PEI sheets.
- **Clear Window Area (Zero Screw Standoffs)**: Internal display cavity is 100% clear of obstructive screw pillars.
- **Sleek Cockpit Red Screen Bezel**: High-contrast Cockpit Red border ($39.2 \times 32.2\text{ mm}$ outer, $1.5\text{ mm}$ corner radius) providing a uniform $2.0\text{ mm}$ bezel framing the display flush on the face ($0.40\text{ mm}$ depth).
- **Precision Aerospace Datum Accent**: A crisp $1.0\text{ mm}$ wide red datum line running at $X = 13.5\text{ mm}$ between the radar screen and the control bank, creating an authentic avionics MFD panel aesthetic without visual clutter.
- **Full-Height Solid Corner Pillars**: Corner screw bosses extend 100% continuously from the split plane into the roof, anchored solidly to inner corner walls with zero mid-air gaps.
- **Internally Reinforced Snap Locks (Zero Outer Wall Holes)**:
  - The lid features 4 internal reinforcement pads ($3.8\text{ mm}$ local wall thickness).
  - The $0.35\text{ mm}$ snap undercuts are cut 100% internally into these reinforcement pads, leaving a full, solid **$> 1.8\text{ mm}$ exterior wall** with zero outer slots or blowouts.
  - Matches 4 internal snap tabs on the shell lip for a tight, flush, rattle-free closure.

### B. Tactile Button Control Bank & Anti-Fallout Keycaps
- **Exact Push Switch Chamber ($6.25 \times 6.25\text{ mm}$)**:
  - Designed specifically for $6.0 \times 6.0\text{ mm}$, $3.8\text{ mm}$ tall physical tactile switches.
  - Snug $0.125\text{ mm}$ clearance per side: zero rattle, zero tilting, and zero loose wobble.
  - Lateral $8.4\text{ mm}$ pin channels for through-hole solder pin clearance.
- **Anti-Fallout Keycap Solution**:
  - **Bottom Retaining Flange**: $\varnothing 5.50\text{ mm}$ diameter, $1.0\text{ mm}$ thickness. Easily drops through the $6.25 \times 6.25\text{ mm}$ switch pocket from the inside during assembly ($5.50 < 6.25$).
  - **Lid Through-Hole**: $\varnothing 4.00\text{ mm}$ circular hole on top face. Because the flange is $\varnothing 5.50\text{ mm}$, it overlaps the hole by $0.75\text{ mm}$ all around ($1.50\text{ mm}$ total diameter overlap). The cap **CANNOT pop out** through the top face.
  - **Gliding Top Stem**: $\varnothing 3.50\text{ mm}$ diameter with $0.4\text{ mm}$ chamfer. Glides inside the $\varnothing 4.00\text{ mm}$ hole with $0.25\text{ mm}$ radial clearance for effortless actuation.
  - **Internal Plunger Socket**: $\varnothing 3.30\text{ mm}$ diameter, $0.95\text{ mm}$ deep into the flange base, perfectly engaging the switch plunger with $0.8\text{ mm}$ stroke.
- **Enlarged High-Visibility Tactile Glyphs**: Bold $\blacktriangle$ (UP, $4.2 \times 3.8\text{ mm}$), $\bullet$ (SEL, $\varnothing 3.8\text{ mm}$), and $\blacktriangledown$ (DOWN, $4.2 \times 3.8\text{ mm}$) indicators inlaid flush next to the button keycaps ($X = 22.2\text{ mm}$) with $0.40\text{ mm}$ depth for crisp multicolor slicing.

### C. Bottom Shell & ESP32 Cradle
- **100% Smooth Planar Outer Walls**: All exterior flutes/grooves removed for clean, modern aesthetics and zero overhang bridging/sagging print defects.
- **100% Plane Bottom Plate**: Flat continuous bottom face with 4 precision M2 pan-head screw counterbores.
- **ESP32 4-Corner $\varnothing 2.40\text{ mm}$ Locator Pins**:
  - Four vertical locator pins ($\varnothing 2.40\text{ mm}$, height $1.50\text{ mm}$ + $0.50\text{ mm}$ lead-in cone) rise directly from the $0.8\text{ mm}$ floor resting pads at `(+32.00, ±11.85)` and `(-14.65, ±11.85)`.
  - Enter the ESP32 board's four $\varnothing 3.20\text{ mm}$ corner mounting holes with generous $>0.20\text{ mm}$ radial clearance, perfectly fitting both CH340 and WROOM boards.
- **Anti-Push Thrust Wall**: Solid vertical backstop at $X = -18.0\text{ mm}$ absorbing 100% of USB-C cable insertion force.
- **Side Support Rails**: $30.0\text{ mm}$ internal width ($Y \in [-15.0, +15.0]\text{ mm}$) for zero-friction drop-in installation of the $29.0\text{ mm}$ board.
- **Right-Sized Type-C Port**:
  - Stadium pill cutout: **$11.20\text{ mm}$ wide $\times 5.00\text{ mm}$ high** ($R = 2.50\text{ mm}$), centered at $Z = 5.80\text{ mm}$.
  - Snugly frames standard Type-C plugs ($10.5 \times 5.8\text{ mm}$) while completely concealing the internal PCB (only $1.0\text{ mm}$ upper gap, $3.30\text{ mm}$ solid base beneath the port).

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
    ├── flyradar32_lid_accents.stl   ← Red screen border, datum bar, button glyphs (Print in Red)
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
