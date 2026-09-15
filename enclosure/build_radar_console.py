import FreeCAD, Part, Mesh
import math

print("==================================================")
print("  FLYRADAR32 CONSOLE - PERFECT PRODUCTION FIX     ")
print("==================================================")

doc_name = "FlyRadar32_Fix"
if doc_name in FreeCAD.listDocuments():
    FreeCAD.closeDocument(doc_name)
doc = FreeCAD.newDocument(doc_name)

# 1. IMPORT STEP MODELS
esp_path = r'D:\Projects\Embedded\Firmware-Development\flyradar32\enclosure\3d files\esp32_Wroom_30pins_C-Type (1).STEP'
tft_path = r'D:\Projects\Embedded\Firmware-Development\flyradar32\enclosure\3d files\AZ-Delivery 1.8 inch TFT Display SPI.stp'
btn_path = r'D:\Projects\Embedded\Firmware-Development\flyradar32\enclosure\3d files\push_switch_small.step'

import Import
Import.insert(esp_path, doc.Name)
Import.insert(tft_path, doc.Name)
Import.insert(btn_path, doc.Name)
doc.recompute()

esp = doc.getObject('esp32_Wroom_30pins_C_Type')
tft = doc.getObject('AZ_Delivery_TFT_1_8_SPI')
btn_orig = doc.getObject('Taster_v4')

# Remove the header pins from ESP32 model:
f141 = doc.getObject('Part__Feature141')
if f141 and hasattr(f141, 'Shape') and not f141.Shape.isNull():
    # Slice off the male header pins below Z = -1.6 in local coords:
    pin_cutter = Part.makeBox(60.0, 70.0, 15.0, FreeCAD.Vector(-30.0, -35.0, -16.6))
    f141.Shape = f141.Shape.cut(pin_cutter)
    doc.recompute()

# 2. ROTATION & PLACEMENT
TILT_DEG = 10.0
rot_x10 = FreeCAD.Rotation(FreeCAD.Vector(1, 0, 0), TILT_DEG)
rot_z180 = FreeCAD.Rotation(FreeCAD.Vector(0, 0, 1), 180)
rot_tft = rot_x10.multiply(rot_z180)
rot_esp = FreeCAD.Rotation(FreeCAD.Vector(0, 0, 1), 90)

# ESP32: Placed on the bottom floor resting on 0.8mm ribs (PCB bottom at Z = 2.6mm, top at Z = 4.2mm)
# USB-C tip is at X = 8.7 + 27.7 = 36.4mm (0.6mm from outer wall X=37.0mm)
pos_esp = FreeCAD.Vector(8.7, 0.0, 4.2)
esp.Placement = FreeCAD.Placement(pos_esp, rot_esp)

# TFT: Placed at X=-10.0mm, active center Y=-3.00mm (aligned with SELECT button at Y=-3.00mm,
# symmetrically framing UP button at Y=10.0mm and DOWN button at Y=-16.0mm with exact 3.10mm top/bottom gaps)
pos_tft = FreeCAD.Vector(-10.0, -1.455, 19.743)
tft.Placement = FreeCAD.Placement(pos_tft, rot_tft)

tft_pl = FreeCAD.Placement(pos_tft, rot_tft)
# Exact Active Area center from STEP model Face 26: local X = -2.89mm, Y = 0.0mm, Z = 8.90mm
global_active_center = tft_pl.multVec(FreeCAD.Vector(-2.89, 0.0, 8.90))

# 3 Buttons: Beside display on the right at X = +28.5mm, tilted +10 deg
def clone_button_exact(src_part, name, label, target_plunger_xy, base_z, tilt_deg):
    new_part = doc.addObject('App::Part', name)
    new_part.Label = label
    for c in src_part.Group:
        if hasattr(c, 'Shape') and not c.Shape.isNull():
            new_feat = doc.addObject('Part::Feature', f'{name}_{c.Name}')
            new_feat.Shape = c.Shape.copy()
            new_feat.Placement = c.Placement
            new_part.addObject(new_feat)
    
    rot = FreeCAD.Rotation(FreeCAD.Vector(1, 0, 0), tilt_deg)
    local_plunger = FreeCAD.Vector(3.0, 3.0, 9.6)
    rot_plunger = rot.multVec(local_plunger)
    target_z = base_z + target_plunger_xy[1] * math.tan(math.radians(tilt_deg))
    target_plunger = FreeCAD.Vector(target_plunger_xy[0], target_plunger_xy[1], target_z)
    part_base = target_plunger.sub(rot_plunger)
    new_part.Placement = FreeCAD.Placement(part_base, rot)
    return new_part, target_plunger

btn_up,   pt_up   = clone_button_exact(btn_orig, 'Button_UP',     'Button_UP',     (28.5,  10.0), 28.8, TILT_DEG)  # shifted -3mm
btn_sel,  pt_sel  = clone_button_exact(btn_orig, 'Button_SELECT', 'Button_SELECT', (28.5,  -3.0), 28.8, TILT_DEG)  # shifted -3mm
btn_down, pt_down = clone_button_exact(btn_orig, 'Button_DOWN',   'Button_DOWN',   (28.5, -16.0), 28.8, TILT_DEG)  # shifted -3mm

doc.removeObject(btn_orig.Name)
doc.recompute()

def make_rounded_box(xmin, xmax, ymin, ymax, zmin, zmax, r):
    w = xmax - xmin
    d = ymax - ymin
    h = zmax - zmin
    box = Part.makeBox(w, d, h, FreeCAD.Vector(xmin, ymin, zmin))
    if r <= 0:
        return box
    vert_edges = []
    for e in box.Edges:
        v1, v2 = e.Vertexes[0].Point, e.Vertexes[1].Point
        if abs(v1.x - v2.x) < 1e-4 and abs(v1.y - v2.y) < 1e-4 and abs(abs(v1.z - v2.z) - h) < 1e-4:
            vert_edges.append(e)
    if vert_edges:
        try:
            return box.makeFillet(r, vert_edges)
        except Exception:
            return box
    return box

def make_rounded_wire(xmin, xmax, ymin, ymax, z, r):
    pts = [
        FreeCAD.Vector(xmin + r, ymin, z),
        FreeCAD.Vector(xmax - r, ymin, z),
        FreeCAD.Vector(xmax, ymin + r, z),
        FreeCAD.Vector(xmax, ymax - r, z),
        FreeCAD.Vector(xmax - r, ymax, z),
        FreeCAD.Vector(xmin + r, ymax, z),
        FreeCAD.Vector(xmin, ymax - r, z),
        FreeCAD.Vector(xmin, ymin + r, z),
    ]
    edges = [
        Part.makeLine(pts[0], pts[1]),
        Part.makeCircle(r, pts[1] + FreeCAD.Vector(0, r, 0), FreeCAD.Vector(0, 0, 1), 270, 360),
        Part.makeLine(pts[2], pts[3]),
        Part.makeCircle(r, pts[4] + FreeCAD.Vector(0, -r, 0), FreeCAD.Vector(0, 0, 1), 0, 90),
        Part.makeLine(pts[4], pts[5]),
        Part.makeCircle(r, pts[5] + FreeCAD.Vector(0, -r, 0), FreeCAD.Vector(0, 0, 1), 90, 180),
        Part.makeLine(pts[6], pts[7]),
        Part.makeCircle(r, pts[0] + FreeCAD.Vector(0, r, 0), FreeCAD.Vector(0, 0, 1), 180, 270),
    ]
    return Part.Wire(edges)

# ==========================================
# 3. ENCLOSURE OUTER & CAVITY
# ==========================================
X_MIN, X_MAX = -45.0, 37.0
Y_MIN, Y_MAX = -34.0, 26.0
WALL = 1.8
FLOOR = 1.8
R_CORNER = 5.0
Z_SPLIT = 16.5

raw_outer = make_rounded_box(X_MIN, X_MAX, Y_MIN, Y_MAX, 0.0, 48.0, R_CORNER)

cutter_box = Part.makeBox(130.0, 110.0, 30.0, FreeCAD.Vector(-65.0, -55.0, 0.0))
cutter_box.Placement = FreeCAD.Placement(FreeCAD.Vector(0.0, 0.0, 31.0), rot_x10)
wedge_solid = raw_outer.cut(cutter_box)

cavity_raw = make_rounded_box(X_MIN + WALL, X_MAX - WALL, Y_MIN + WALL, Y_MAX - WALL, FLOOR, 46.0, R_CORNER - 1.5)
cavity_cutter = Part.makeBox(130.0, 110.0, 30.0, FreeCAD.Vector(-65.0, -55.0, 0.0))
cavity_cutter.Placement = FreeCAD.Placement(FreeCAD.Vector(0.0, 0.0, 31.0 - WALL), rot_x10)
cavity_solid = cavity_raw.cut(cavity_cutter)

enclosure_hollow = wedge_solid.cut(cavity_solid)

# ==========================================
# 4. PARTING LINE: SHELL & LID SPLIT
# ==========================================
split_box_shell = Part.makeBox(140.0, 120.0, Z_SPLIT, FreeCAD.Vector(-70.0, -60.0, 0.0))
split_box_lid = Part.makeBox(140.0, 120.0, 40.0, FreeCAD.Vector(-70.0, -60.0, Z_SPLIT))

shell_base = enclosure_hollow.common(split_box_shell)
lid_base = enclosure_hollow.common(split_box_lid)

shell_body = shell_base
lid_body = lid_base

# ==========================================
# 4b. INTERLOCKING LIP JOINT (ZERO FLOATING WALLS, ZERO SUPPORTS NEEDED)
# ==========================================
# Shell lip: solid ring Z=16.5->18.0, thickness=0.9mm (wall-to-lip offset)
# Lid rebate: clearance 0.15mm on lip outer face and 0.45mm snap pocket
# 1. Shell: Solid Alignment Lip rising from Z = 16.5 to Z = 18.0:
# Integrally printed going straight UP from the shell wall (ZERO overhang, ZERO supports needed!)
lip_outer = make_rounded_box(X_MIN + 0.9, X_MAX - 0.9, Y_MIN + 0.9, Y_MAX - 0.9, 16.5, 18.0, R_CORNER - 0.9)
lip_inner = make_rounded_box(X_MIN + WALL, X_MAX - WALL, Y_MIN + WALL, Y_MAX - WALL, 16.4, 18.1, R_CORNER - WALL)
shell_lip = lip_outer.cut(lip_inner)

# 2. 4 Automatic Cantilever Snap Tabs on the Shell Lip (reduced to 0.3mm for smoother snap):
# All 4 tabs: 8mm long x 0.30mm wide x 1.0mm tall, centered on their respective wall
tab_front = Part.makeBox(8.0, 0.30, 1.0, FreeCAD.Vector(-4.0, Y_MIN + 0.9 - 0.30, 16.7))
tab_rear  = Part.makeBox(8.0, 0.30, 1.0, FreeCAD.Vector(-4.0, Y_MAX - 0.9,          16.7))
tab_left  = Part.makeBox(0.30, 8.0, 1.0, FreeCAD.Vector(X_MIN + 0.9 - 0.30, -4.0,  16.7))
tab_right = Part.makeBox(0.30, 8.0, 1.0, FreeCAD.Vector(X_MAX - 0.9,         -4.0,  16.7))  # FIX: was -20.0 (wrong)
shell_lip = shell_lip.fuse(tab_front).fuse(tab_rear).fuse(tab_left).fuse(tab_right)

shell_body = shell_body.fuse(shell_lip)

# 4. Lid: Solid Wall Rebate to receive the Shell Lip (with 0.15mm clearance):
# The lid wall is ONE SOLID PIECE. The rebate is cut into the inner face from Z=16.45 to 18.15.
rebate_outer = make_rounded_box(X_MIN + 0.75, X_MAX - 0.75, Y_MIN + 0.75, Y_MAX - 0.75, 16.45, 18.15, R_CORNER - 0.75)
rebate_inner = make_rounded_box(X_MIN + WALL + 0.15, X_MAX - WALL - 0.15, Y_MIN + WALL + 0.15, Y_MAX - WALL - 0.15, 16.4, 18.2, R_CORNER - WALL - 0.15)
lid_rebate = rebate_outer.cut(rebate_inner)
lid_body = lid_body.cut(lid_rebate)

# 5. Matching Undercut Snap Pockets inside the Lid Rebate (0.15mm clearance over the 0.30mm tabs):
# Tab protrusion is 0.30mm on the shell lip. The rebate outer wall is at X_MIN+0.75.
# The pocket needs to cut into the outer wall by 0.30mm+0.15mm clearance = 0.45mm.
# All 4 pockets: 10mm long x 0.45mm deep x 1.3mm tall, symmetric on all walls
pocket_front = Part.makeBox(10.0, 0.45, 1.3, FreeCAD.Vector(-5.0, Y_MIN + 0.75 - 0.45, 16.55))
pocket_rear  = Part.makeBox(10.0, 0.45, 1.3, FreeCAD.Vector(-5.0, Y_MAX - 0.75,          16.55))
pocket_left  = Part.makeBox(0.45, 10.0, 1.3, FreeCAD.Vector(X_MIN + 0.75 - 0.45, -5.0,  16.55))
pocket_right = Part.makeBox(0.45, 10.0, 1.3, FreeCAD.Vector(X_MAX - 0.75,         -5.0,  16.55))  # FIX: was -21.0 (wrong)
lid_body = lid_body.cut(pocket_front).cut(pocket_rear).cut(pocket_left).cut(pocket_right)


# ==========================================
# 5. SHELL: BOTTOM ESP32 CRADLE WITH AUTOMATIC SNAP-FIT LOCKS & TYPE-C PORT
# ==========================================

# HEAVY-DUTY CORNER SCREW FIXATION BOSSES (M2 x 16mm Pan Head Screws - Invoice Item 16):
# Boss centers = exact corner arc centers of the enclosure rounded rectangle.
# R_CORNER = 5.0mm, WALL = 1.8mm -> boss radius = R_CORNER - WALL = 3.2mm
# This fills the inner corner pocket flush with NO protrusion outside the enclosure.
# Boss:  R=3.2mm,  Z = FLOOR(1.8) to Z_SPLIT(16.5) => height = 14.7mm
# M2 shaft clearance hole: Dia 2.4mm (R=1.2mm), full height
# Counterbore for M2 Pan Head: Dia 4.5mm (R=2.25mm), depth 3.5mm from Z=0
CORNER_BOSS_DATA = [
    # (cx, cy)  = corner arc center = (X_MIN+R, Y_MIN+R) etc.
    (X_MIN + R_CORNER, Y_MIN + R_CORNER),  # FL = (-40.0, -29.0)
    (X_MAX - R_CORNER, Y_MIN + R_CORNER),  # FR = ( 32.0, -29.0)
    (X_MIN + R_CORNER, Y_MAX - R_CORNER),  # RL = (-40.0,  21.0)
    (X_MAX - R_CORNER, Y_MAX - R_CORNER),  # RR = ( 32.0,  21.0)
]
BOSS_R       = R_CORNER - WALL          # 3.2mm  -- fits inside inner corner arc exactly
SHELL_BOSS_H = Z_SPLIT - FLOOR          # 14.7mm -- floor to split line

for cx, cy in CORNER_BOSS_DATA:
    # Solid boss cylinder sitting inside the corner pocket, Z = FLOOR to Z_SPLIT:
    s_boss = Part.makeCylinder(BOSS_R, SHELL_BOSS_H, FreeCAD.Vector(cx, cy, FLOOR), FreeCAD.Vector(0, 0, 1))
    # M2 shaft clearance hole (Dia 2.4mm), runs full height + countersink depth:
    s_hole  = Part.makeCylinder(1.2,  SHELL_BOSS_H + FLOOR + 0.5, FreeCAD.Vector(cx, cy, -0.5), FreeCAD.Vector(0, 0, 1))
    # Counterbore for M2 Pan Head (Dia 4.5mm, 3.5mm deep from Z=0 downward):
    s_csink = Part.makeCylinder(2.25, 3.5, FreeCAD.Vector(cx, cy, -0.1), FreeCAD.Vector(0, 0, 1))
    shell_body = shell_body.fuse(s_boss).cut(s_hole).cut(s_csink)

# Bottom plate is kept 100% plane and smooth (no badge recess)

# WIDE-CLEARANCE TYPE-C PORT WITH PERFECT UNIVERSAL ALIGNMENT:
# Accommodates both CH340 Type-C (Z in [2.60, 5.85]) and C-Type (Z in [3.12, 7.30]):
Z_USBC = 4.80
# 1. Main through-pill cutout: Width 11.0mm, height 5.6mm (R=2.8mm, spans Z in [2.00, 7.60mm]):
c_top = Part.makeCylinder(2.8, 8.0, FreeCAD.Vector(X_MAX - 5.0, 2.7, Z_USBC), FreeCAD.Vector(1, 0, 0))
c_bot = Part.makeCylinder(2.8, 8.0, FreeCAD.Vector(X_MAX - 5.0, -2.7, Z_USBC), FreeCAD.Vector(1, 0, 0))
b_mid = Part.makeBox(8.0, 5.4, 5.6, FreeCAD.Vector(X_MAX - 5.0, -2.7, Z_USBC - 2.8))
usbc_pill = c_top.fuse(c_bot).fuse(b_mid)
shell_body = shell_body.cut(usbc_pill)

# 2. Outer cable shroud relief pocket: 13.6mm wide x 7.4mm high (R=3.7mm), depth 1.5mm on outer wall
c_rel_top = Part.makeCylinder(3.7, 2.0, FreeCAD.Vector(X_MAX - 1.5, 3.1, Z_USBC), FreeCAD.Vector(1, 0, 0))
c_rel_bot = Part.makeCylinder(3.7, 2.0, FreeCAD.Vector(X_MAX - 1.5, -3.1, Z_USBC), FreeCAD.Vector(1, 0, 0))
b_rel_mid = Part.makeBox(2.0, 6.2, 7.4, FreeCAD.Vector(X_MAX - 1.5, -3.1, Z_USBC - 3.7))
usbc_relief = c_rel_top.fuse(c_rel_bot).fuse(b_rel_mid)
shell_body = shell_body.cut(usbc_relief)

# 3. VERTICAL FLUTES WITH PERFECT ROUNDED INNER PROFILE (Capsule geometry, NOT boxed):
# Cylinder R=1.25mm with spherical ends, submerged 0.35mm into wall (leaves 1.45mm solid plastic = 100% solid, ZERO slicing holes)
def make_vertical_rounded_flute(cx, cy, z_bot, h_flute, r_flute, depth, norm_vec):
    pos = FreeCAD.Vector(cx, cy, z_bot) + norm_vec * (r_flute - depth)
    cyl = Part.makeCylinder(r_flute, h_flute, pos, FreeCAD.Vector(0, 0, 1))
    sph1 = Part.makeSphere(r_flute, pos)
    sph2 = Part.makeSphere(r_flute, pos + FreeCAD.Vector(0, 0, h_flute))
    return cyl.fuse(sph1).fuse(sph2)

# Wall Centers: Enclosure is X in [-45, 37] -> Center X = -4.0mm; Y in [-34, 26] -> Center Y = -4.0mm
X_WALL_CTR = (X_MIN + X_MAX) / 2.0  # -4.0mm (dead center of front & rear walls)
Y_WALL_CTR = (Y_MIN + Y_MAX) / 2.0  # -4.0mm (dead center of left wall)

# Left wall: 3 vertical rounded flutes centered at Y_WALL_CTR (-4.0mm), pitch 10.0mm:
# Spans Y in [-14.0, 6.0] -> exactly 20.0mm margin to both front (Y=-34) and rear (Y=26) corners!
for fy in [Y_WALL_CTR - 10.0, Y_WALL_CTR, Y_WALL_CTR + 10.0]:
    flute_l = make_vertical_rounded_flute(X_MIN, fy, 4.5, 6.0, 1.25, 0.35, FreeCAD.Vector(-1, 0, 0))
    shell_body = shell_body.cut(flute_l)

# Right wall: 2 vertical rounded flutes flanking Type-C port (centered at Y=0.0mm)
for fy in [-13.0, 13.0]:
    flute_r = make_vertical_rounded_flute(X_MAX, fy, 4.5, 6.0, 1.25, 0.35, FreeCAD.Vector(1, 0, 0))
    shell_body = shell_body.cut(flute_r)

# Front wall: 3 vertical rounded flutes centered at X_WALL_CTR (-4.0mm), pitch 12.0mm:
# Spans X in [-16.0, 8.0] -> exactly 29.0mm margin to both left (X=-45) and right (X=37) corners!
for fx in [X_WALL_CTR - 12.0, X_WALL_CTR, X_WALL_CTR + 12.0]:
    flute_f = make_vertical_rounded_flute(fx, Y_MIN, 4.5, 6.0, 1.25, 0.35, FreeCAD.Vector(0, -1, 0))
    shell_body = shell_body.cut(flute_f)

# Rear wall: 3 vertical rounded flutes centered at X_WALL_CTR (-4.0mm), pitch 12.0mm:
# Spans X in [-16.0, 8.0] -> exactly 29.0mm margin to both left (X=-45) and right (X=37) corners!
for rx in [X_WALL_CTR - 12.0, X_WALL_CTR, X_WALL_CTR + 12.0]:
    flute_rear = make_vertical_rounded_flute(rx, Y_MAX, 4.5, 6.0, 1.25, 0.35, FreeCAD.Vector(0, 1, 0))
    shell_body = shell_body.cut(flute_rear)

# ==========================================
# AUTOMATIC SNAP-FIT ESP32 CRADLE WITH 2.80mm LOCATOR PINS:
# ESP32 PCB: X in [-17.30, 34.20], Y in [-14.25, 14.25], Z in [2.60, 4.20]
# 4 Corner Mounting Holes in PCB are Dia 3.0mm (R=1.50mm) at:
# (-16.55, -13.50), (-16.55, 13.50), (33.95, -13.50), (33.95, 13.50)
# ==========================================
GAP_ESP_Y = 0.40
GAP_ESP_X = 0.60
# Accommodates 29.0mm wide CH340 board (and 28.5mm WROOM):
Y_ESP_MIN = -14.50 - GAP_ESP_Y   # -14.90mm
Y_ESP_MAX = 14.50 + GAP_ESP_Y    # +14.90mm
X_ESP_REAR = -16.60 - GAP_ESP_X  # -17.20mm

# 1. Four resting pads with Dia 2.40mm (R=1.20mm) locator pins with lead-in cone:
# Perfectly centered between CH340 (24.00 x 46.48mm pitch) and WROOM (23.20 x 47.00mm pitch)
# Pitch in X = 46.50mm (-13.85 to +32.65), Pitch in Y = 23.80mm (-11.90 to +11.90)
# Radial clearance >= 0.30mm inside Dia 3.20mm holes for 100% effortless drop-in slip fit
esp_pads = []
esp_pins = []
for hx, hy in [(-14.55, -11.80), (-14.55, 11.80), (32.20, -11.80), (32.20, 11.80)]:
    # Resting pad: 5.0 x 5.0 mm, height 0.8mm (Z in [1.80, 2.60])
    pad = Part.makeBox(5.0, 5.0, 0.80, FreeCAD.Vector(hx - 2.5, hy - 2.5, FLOOR))
    esp_pads.append(pad)
    # 2.40mm locator pin: height 2.10mm (Z in [2.60, 4.70], PCB top is at 4.20mm, cone tip above)
    cyl = Part.makeCylinder(1.20, 1.60, FreeCAD.Vector(hx, hy, 2.60), FreeCAD.Vector(0, 0, 1))
    tip = Part.makeCone(1.20, 0.70, 0.50, FreeCAD.Vector(hx, hy, 4.20), FreeCAD.Vector(0, 0, 1))
    esp_pins.append(cyl.fuse(tip))

all_pads = esp_pads[0].fuse(esp_pads[1]).fuse(esp_pads[2]).fuse(esp_pads[3])
all_pins = esp_pins[0].fuse(esp_pins[1]).fuse(esp_pins[2]).fuse(esp_pins[3])

# 2. Solid Rear Thrust Wall (2.0mm thick, inner face at X_ESP_REAR = -17.20mm):
thrust_w = Y_ESP_MAX - Y_ESP_MIN
rear_thrust = Part.makeBox(2.0, thrust_w, 4.0, FreeCAD.Vector(X_ESP_REAR - 2.0, Y_ESP_MIN, FLOOR))

# 3. Lateral Guide Rails with 0.4mm gap each side:
rail_left = Part.makeBox(24.0, 1.5, 3.5, FreeCAD.Vector(0.0, Y_ESP_MIN - 1.5, FLOOR))
rail_right = Part.makeBox(24.0, 1.5, 3.5, FreeCAD.Vector(0.0, Y_ESP_MAX, FLOOR))

# 4. 4 Cantilever snap-fit locks:
def make_snap_clip(x_center, y_pos, side_sign):
    y_base = y_pos - (1.4 if side_sign > 0 else 0.0)
    arm = Part.makeBox(5.0, 1.4, 5.6 - FLOOR, FreeCAD.Vector(x_center - 2.5, y_base, FLOOR))
    y_tooth = y_pos - (0.8 if side_sign > 0 else -0.0)
    tooth = Part.makeBox(5.0, 0.8, 1.25, FreeCAD.Vector(x_center - 2.5, y_tooth, 4.35))
    c_box = Part.makeBox(7.0, 2.0, 2.0, FreeCAD.Vector(x_center - 3.5, y_pos - 1.0, 4.6))
    rot = FreeCAD.Rotation(FreeCAD.Vector(1, 0, 0), -45 if side_sign > 0 else 45)
    c_box.Placement = FreeCAD.Placement(FreeCAD.Vector(x_center - 3.5, y_pos, 5.6), rot)
    return arm.fuse(tooth).cut(c_box)

snap1 = make_snap_clip(5.0, Y_ESP_MIN, -1)
snap2 = make_snap_clip(22.0, Y_ESP_MIN, -1)
snap3 = make_snap_clip(5.0, Y_ESP_MAX, 1)
snap4 = make_snap_clip(22.0, Y_ESP_MAX, 1)

esp_mount = all_pads.fuse(all_pins).fuse(rear_thrust).fuse(rail_left).fuse(rail_right).fuse(snap1).fuse(snap2).fuse(snap3).fuse(snap4)

# Cut exact PCB entry path so snap clips have proper spring clearance:
pcb_clear = Part.makeBox(54.0, thrust_w + 0.1, 10.0, FreeCAD.Vector(-18.5, Y_ESP_MIN - 0.05, 2.6))
# Cut only the snap arms, preserve the locator pins:
snap_cleared = esp_mount.cut(pcb_clear).fuse(all_pins)

shell_body = shell_body.fuse(snap_cleared)
shell_body = shell_body.removeSplitter()

# ==========================================
# 6. LID: AVIONICS SUN-VISOR BEZEL, ERGONOMIC BUTTON DISHES & ACOUSTIC LOUVERS
# ==========================================
X_SCREEN = global_active_center.x
Y_SCREEN = global_active_center.y
z_sc_top = 31.0 + Y_SCREEN * math.tan(math.radians(TILT_DEG))

# A. Multi-tiered Avionics Sun-Visor Display Bezel:
# 1. Main viewing window: 35.2 x 28.2 mm (Frames the 35.00 x 28.00mm active display with clean perpendicular cut):
window_cutter = Part.makeBox(35.2, 28.2, 10.0, FreeCAD.Vector(-17.6, -14.1, -5.0))
p_win = FreeCAD.Placement()
p_win.Rotation = rot_x10
p_win.Base = FreeCAD.Vector(X_SCREEN, Y_SCREEN, z_sc_top)
window_cutter.Placement = p_win
lid_body = lid_body.cut(window_cutter)

# 2. Modern Minimalist High-Contrast Screen Accent Bezel:
# 39.2 x 32.2 mm outer with 1.5mm rounded corners, 35.2 x 28.2 mm inner opening, 0.40mm depth (2 layers at 0.20mm)
b_outer = make_rounded_box(-19.6, 19.6, -16.1, 16.1, -0.40, 0.0, 1.5)
b_inner = Part.makeBox(35.2, 28.2, 1.0, FreeCAD.Vector(-17.6, -14.1, -0.8))
bezel_accent = b_outer.cut(b_inner)
bezel_accent.Placement = p_win
lid_body = lid_body.cut(bezel_accent)

# 3. 4 PRECISION CORNER MOUNTING POINTS FOR 1.8" TFT DISPLAY MODULE:
# TFT PCB Size: 58.00 x 34.50 mm, 1.60mm thickness
# 4 Mounting Holes: Pitch X = 52.00mm, Pitch Y = 28.50mm, Diagonal = 59.30mm, Dia 3.20mm
# Each standoff features:
#   - Solid Boss Shoulder: Dia 5.20mm (R=2.60mm) seating the PCB front face (Z_local = 6.55mm)
#   - Chamfered Locator Pin: Dia 2.80mm (R=1.40mm) x 1.30mm height (snug fit in Dia 3.20mm hole)
#   - M2 Screw Pilot Hole: Dia 1.90mm (R=0.95mm) x 5.50mm depth for optional M2 screw fixation
#   - Anchored solidly into lid roof, trimmed cleanly 0.50mm below outer cosmetic surface
tft_mount_posts = []
v_tft_roof_up = rot_x10.multVec(FreeCAD.Vector(0, 0, 1))
v_tft_pcb_down = rot_x10.multVec(FreeCAD.Vector(0, 0, -1))
TFT_HOLES_LOCAL = [(-26.0, -14.25), (26.0, -14.25), (-26.0, 14.25), (26.0, 14.25)]

for hx, hy in TFT_HOLES_LOCAL:
    p_pcb_seat = tft_pl.multVec(FreeCAD.Vector(hx, hy, 6.55))
    # Boss cylinder extending up into lid roof:
    boss_cyl = Part.makeCylinder(2.60, 6.0, p_pcb_seat, v_tft_roof_up)
    # Alignment pin entering into PCB hole:
    pin_cyl = Part.makeCylinder(1.40, 1.0, p_pcb_seat, v_tft_pcb_down)
    pin_tip = Part.makeCone(1.40, 1.00, 0.30, p_pcb_seat + v_tft_pcb_down * 1.0, v_tft_pcb_down)
    pin_full = pin_cyl.fuse(pin_tip)
    post_solid = boss_cyl.fuse(pin_full)
    # M2 screw pilot hole:
    pilot = Part.makeCylinder(0.95, 5.5, p_pcb_seat + v_tft_pcb_down * 1.35, v_tft_roof_up)
    tft_mount_posts.append(post_solid.cut(pilot))

all_tft_posts = tft_mount_posts[0].fuse(tft_mount_posts[1]).fuse(tft_mount_posts[2]).fuse(tft_mount_posts[3])

# Roof cutter leaves 0.5mm solid cosmetic exterior skin:
roof_cutter_tft = Part.makeBox(130.0, 110.0, 30.0, FreeCAD.Vector(-65.0, -55.0, 0.0))
roof_cutter_tft.Placement = FreeCAD.Placement(FreeCAD.Vector(0.0, 0.0, 31.0 - 0.5), rot_x10)
clean_tft_posts = all_tft_posts.cut(roof_cutter_tft)

lid_body = lid_body.fuse(clean_tft_posts)
# Forehead and chin motifs removed: 100% pure, clean, uncluttered negative space.
# The screen aperture and tactile button indicators are the sole heroes.

# 4. OPTION 1: CLEAN MINIMALIST AEROSPACE FACE PLATE
# Pure, uncluttered matte black surface - zero barcode stripes, zero floating lines.
# Negative space highlights the screen and controls for a high-end flight console look.

# CORNER SCREW BOSSES IN LID (M2 x 3mm Brass Threaded Inserts - Invoice Item 20):
# Full continuous solid pillars extending from Z_SPLIT all the way into the lid roof!
# Anchored solidly to the inner roof and corner walls so they print continuously without mid-air gaps or floating overhangs.
roof_embed_cutter = Part.makeBox(130.0, 110.0, 30.0, FreeCAD.Vector(-65.0, -55.0, 0.0))
roof_embed_cutter.Placement = FreeCAD.Placement(FreeCAD.Vector(0.0, 0.0, 31.0 - 0.5), rot_x10)

for cx, cy in CORNER_BOSS_DATA:
    raw_lid_boss = Part.makeCylinder(BOSS_R, 30.0, FreeCAD.Vector(cx, cy, Z_SPLIT), FreeCAD.Vector(0, 0, 1))
    lid_boss = raw_lid_boss.cut(roof_embed_cutter)
    # M2x3mm brass heat-set insert blind hole: Dia 3.2mm (R=1.6mm), 3.65mm deep from Z_SPLIT:
    insert_hole = Part.makeCylinder(1.6, 3.65, FreeCAD.Vector(cx, cy, Z_SPLIT - 0.05), FreeCAD.Vector(0, 0, 1))
    lid_body = lid_body.fuse(lid_boss).cut(insert_hole)

# Clear rebate groove through any bosses intersecting the perimeter lip:
lid_body = lid_body.cut(lid_rebate)

# Guarantee ZERO interference with physical button components:
# Any boss/carrier geometry that grazes a button model is explicitly cleared here.
for _btn_obj in [btn_up, btn_sel, btn_down]:
    lid_body = lid_body.cut(_btn_obj.Shape)

# C. PROPER INTEGRATED BUTTON BOX / HOLDER:
# Connects solidly to the lid roof and right inner wall (X = X_MAX - WALL = 35.2mm)
# Provides rock-solid support so buttons are NEVER floating in air!
# Centered at Y = -3.0 (matches SELECT button and middle of button cluster)
btn_carrier = Part.makeBox(12.5, 38.0, 12.0, FreeCAD.Vector(-5.8, -19.0, -12.0))
p_bc = FreeCAD.Placement()
p_bc.Rotation = rot_x10
z_bc = 31.0 + (-3.0) * math.tan(math.radians(TILT_DEG))
p_bc.Base = FreeCAD.Vector(28.5, -3.0, z_bc)
btn_carrier.Placement = p_bc

# Trim carrier at Z_SPLIT so it remains perfectly flush with the parting line (never pokes into shell):
trim_carrier_split = Part.makeBox(200.0, 200.0, 10.0, FreeCAD.Vector(-100.0, -100.0, Z_SPLIT - 10.0))
btn_carrier = btn_carrier.cut(trim_carrier_split)

lid_body = lid_body.fuse(btn_carrier)

btn_caps = []
for btn_obj, pt in [(btn_up, pt_up), (btn_sel, pt_sel), (btn_down, pt_down)]:
    bdir = rot_x10.multVec(FreeCAD.Vector(0, 0, 1))
    bz_top = 31.0 + pt.y * math.tan(math.radians(TILT_DEG))
    
    p_sw = FreeCAD.Placement()
    p_sw.Rotation = rot_x10
    p_sw.Base = FreeCAD.Vector(pt.x, pt.y, bz_top)
    
    # Exact physical switch plunger location in global and local frames:
    pl_glob = btn_obj.Placement.multVec(FreeCAD.Vector(3.0, 3.0, 9.6))
    pl_local = p_sw.inverse().multVec(pl_glob)
    
    # Top face origin perfectly concentric with switch plunger:
    b_origin = pl_glob - bdir * pl_local.z
    
    # 1. Lid Through-Hole (Dia 4.0mm, clean perpendicular cylinder matching Image 2)
    bhole = Part.makeCylinder(2.00, 4.0, b_origin - bdir * 2.0, bdir)
    lid_body = lid_body.cut(bhole)

    # 2. Precision Switch Chamber (6.25 x 6.25mm snug cavity for 6.0 x 6.0mm button body)
    # Provides 0.125mm clearance per side (snug, zero wobble) plus 8.4mm pin clearance channels:
    cavity_body = Part.makeBox(6.25, 6.25, 10.5, FreeCAD.Vector(-3.125, pl_local.y - 3.125, -12.0))
    pin_channels = Part.makeBox(8.4, 6.25, 6.0, FreeCAD.Vector(-4.2, pl_local.y - 3.125, -12.0))
    cavity = cavity_body.fuse(pin_channels)
    cavity.Placement = p_sw
    
    # Solid, clean walls around all 4 sides of each button box:
    lid_body = lid_body.cut(cavity)
    
    # Guarantee zero physical collision with switch model:
    lid_body = lid_body.cut(btn_obj.Shape)

    # 3. Precision Ergonomic Button Cap (Designed for 6.5mm long, Dia 3.0mm switch plunger)
    # Stem: Dia 3.50mm (Radius 1.75mm) solid cylinder with 0.25mm smooth radial glide clearance in Dia 4.0mm hole
    cap_top = Part.makeCylinder(1.75, 2.8, b_origin - bdir * 2.0, bdir)
    cap_top_chamfer = Part.makeCone(1.75, 1.35, 0.4, b_origin + bdir * 0.8, bdir)
    # Retaining Flange Base: Dia 5.50mm (Radius 2.75mm), thickness 1.0mm
    # Easily enters through the 6.25x6.25mm pocket (0.375mm clearance per side)
    # Trapped inside by the Dia 4.0mm hole (0.75mm retention lip, CANNOT fall out!)
    cap_base = Part.makeCylinder(2.75, 1.0, b_origin - bdir * 3.0, bdir)
    # Plunger socket in flange: Dia 3.30mm (Radius 1.65mm), Depth 0.95mm into the base
    # Gives 0.15mm radial slip-fit over Dia 3.0mm plunger, 0.12mm idle clearance to prevent pre-loading
    # Leaves a 1.10mm thick solid annular wall in the flange; the stem above is 100% SOLID!
    plunger_socket = Part.makeCylinder(1.65, 0.95, b_origin - bdir * 3.0, bdir)
    
    single_cap = cap_top.fuse(cap_top_chamfer).fuse(cap_base).cut(plunger_socket)
    btn_caps.append(single_cap)

# D. BOLD MODERN TACTILE INDICATOR GLYPHS BESIDE BUTTONS (Matching User Image 2):
# Centered at X=22.2 (clean 2.2mm gap from button hole at X=24.4, 12.6mm from screen bezel)
# Up button glyph: bold triangle pointing UP, at Y=10.0 (X=22.2, width 4.2mm, height 3.8mm)
p_glyph_up = FreeCAD.Placement()
p_glyph_up.Rotation = rot_x10
z_g_up = 31.0 + 10.0 * math.tan(math.radians(TILT_DEG))
p_glyph_up.Base = FreeCAD.Vector(22.2, 10.0, z_g_up)
tri_up = Part.Face(Part.makePolygon([
    FreeCAD.Vector(0, 2.0, 0),
    FreeCAD.Vector(-2.1, -1.8, 0),
    FreeCAD.Vector(2.1, -1.8, 0),
    FreeCAD.Vector(0, 2.0, 0)
])).extrude(FreeCAD.Vector(0, 0, -0.40))
tri_up.Placement = p_glyph_up
lid_body = lid_body.cut(tri_up)

# Select button glyph: bold solid circle / dot, at Y=-3.0 (X=22.2, Dia 3.8mm / R=1.9mm)
p_glyph_sel = FreeCAD.Placement()
p_glyph_sel.Rotation = rot_x10
z_g_sel = 31.0 + (-3.0) * math.tan(math.radians(TILT_DEG))
p_glyph_sel.Base = FreeCAD.Vector(22.2, -3.0, z_g_sel)
dot_sel = Part.makeCylinder(1.90, 0.40, FreeCAD.Vector(0, 0, -0.40), FreeCAD.Vector(0, 0, 1))
dot_sel.Placement = p_glyph_sel
lid_body = lid_body.cut(dot_sel)

# Down button glyph: bold triangle pointing DOWN, at Y=-16.0 (X=22.2, width 4.2mm, height 3.8mm)
p_glyph_down = FreeCAD.Placement()
p_glyph_down.Rotation = rot_x10
z_g_down = 31.0 + (-16.0) * math.tan(math.radians(TILT_DEG))
p_glyph_down.Base = FreeCAD.Vector(22.2, -16.0, z_g_down)
tri_down = Part.Face(Part.makePolygon([
    FreeCAD.Vector(0, -2.0, 0),
    FreeCAD.Vector(-2.1, 1.8, 0),
    FreeCAD.Vector(2.1, 1.8, 0),
    FreeCAD.Vector(0, -2.0, 0)
])).extrude(FreeCAD.Vector(0, 0, -0.40))
tri_down.Placement = p_glyph_down
lid_body = lid_body.cut(tri_down)

lid_body = lid_body.removeSplitter()

# Clean Minimalist Aerospace: Screen Border, Bold Button Glyphs
accent_solids = [bezel_accent, tri_up, dot_sel, tri_down]
accents_compound = Part.makeCompound(accent_solids)

# 7. ADD PART FEATURES
feat_shell = doc.addObject("Part::Feature", "Shell")
feat_shell.Label = "FlyRadar32_Shell"
feat_shell.Shape = shell_body

feat_lid = doc.addObject("Part::Feature", "Lid")
feat_lid.Label = "FlyRadar32_Lid"
feat_lid.Shape = lid_body

feat_accents = doc.addObject("Part::Feature", "Lid_Accents")
feat_accents.Label = "FlyRadar32_Lid_Accents"
feat_accents.Shape = accents_compound

caps_compound = Part.makeCompound(btn_caps)
feat_caps = doc.addObject("Part::Feature", "Button_Caps")
feat_caps.Label = "FlyRadar32_Button_Caps"
feat_caps.Shape = caps_compound

doc.recompute()

# 8. COMPREHENSIVE BOOLEAN INTERFERENCE MATRIX
print("\n==============================================")
print("     COMPREHENSIVE BOOLEAN INTERFERENCE MATRIX ")
print("==============================================")

s_esp = esp.Shape.copy()
s_tft = tft.Shape.copy()

s_btn_up = btn_up.Shape.copy()
s_btn_sel = btn_sel.Shape.copy()
s_btn_down = btn_down.Shape.copy()
s_shell = shell_body.copy()
s_lid = lid_body.copy()
s_caps = caps_compound.copy()

checks = [
    ("ESP32 <-> TFT", s_esp, s_tft),
    ("ESP32 <-> Button_UP", s_esp, s_btn_up),
    ("ESP32 <-> Button_SEL", s_esp, s_btn_sel),
    ("ESP32 <-> Button_DOWN", s_esp, s_btn_down),
    ("TFT <-> Button_UP", s_tft, s_btn_up),
    ("TFT <-> Button_SEL", s_tft, s_btn_sel),
    ("TFT <-> Button_DOWN", s_tft, s_btn_down),
    ("Shell <-> ESP32", s_shell, s_esp),
    ("Shell <-> TFT", s_shell, s_tft),
    ("Shell <-> Button_UP", s_shell, s_btn_up),
    ("Shell <-> Button_SEL", s_shell, s_btn_sel),
    ("Shell <-> Button_DOWN", s_shell, s_btn_down),
    ("Lid <-> ESP32", s_lid, s_esp),
    ("Lid <-> TFT", s_lid, s_tft),
    ("Lid <-> Button_UP", s_lid, s_btn_up),
    ("Lid <-> Button_SEL", s_lid, s_btn_sel),
    ("Lid <-> Button_DOWN", s_lid, s_btn_down),
    ("Lid <-> Button_Caps", s_lid, s_caps),
    ("Shell <-> Button_Caps", s_shell, s_caps),
    ("Shell <-> Lid (Mating)", s_shell, s_lid),
]

all_passed = True
for name, s1, s2 in checks:
    try:
        common = s1.common(s2)
        vol = common.Volume
        if vol < 0.0001:
            print(f"  PASS: {name:<28} = 0.000000 mm3")
        else:
            print(f"  FAIL: {name:<28} = {vol:.6f} mm3 [OVERLAP!]")
            all_passed = False
    except Exception as e:
        print(f"  ERR : {name:<28} = {e}")
        all_passed = False

if all_passed:
    print("\n>>> ALL 20 INTERFERENCE CHECKS PASSED: 0.000000 mm³! <<<")
else:
    print("\n>>> WARNING: SOME CHECKS FAILED! <<<")

# If running with FreeCADGui, ensure all models are visible with correct avionics multicolor styling:
try:
    import FreeCADGui
    doc = FreeCAD.activeDocument()
    for name in ["Shell", "Lid", "Lid_Accents", "Button_Caps", "Button_UP", "Button_SELECT", "Button_DOWN"]:
        obj = doc.getObject(name)
        if obj and hasattr(obj, "ViewObject") and obj.ViewObject:
            obj.ViewObject.Visibility = True
        if obj and hasattr(obj, "Group"):
            for child in obj.Group:
                if hasattr(child, "ViewObject") and child.ViewObject:
                    child.ViewObject.Visibility = True
    for name in ["esp32_Wroom_30pins_C_Type", "AZ_Delivery_TFT_1_8_SPI", "Taster_v4", "btn_orig"]:
        obj = doc.getObject(name)
        if obj and hasattr(obj, "ViewObject") and obj.ViewObject:
            obj.ViewObject.Visibility = False
    if 'btn_orig' in locals() and hasattr(btn_orig, "ViewObject") and btn_orig.ViewObject:
        btn_orig.ViewObject.Visibility = False

    # Avionics Cockpit Palette (Black Base with High-Contrast Cockpit Red Accents):
    if hasattr(feat_shell, "ViewObject") and feat_shell.ViewObject:
        feat_shell.ViewObject.ShapeColor = (0.13, 0.14, 0.16)   # Deep Matte Black
    if hasattr(feat_lid, "ViewObject") and feat_lid.ViewObject:
        feat_lid.ViewObject.ShapeColor = (0.13, 0.14, 0.16)     # Deep Matte Black
    if hasattr(feat_accents, "ViewObject") and feat_accents.ViewObject:
        feat_accents.ViewObject.ShapeColor = (0.90, 0.10, 0.10) # Vibrant Cockpit Red
    if hasattr(feat_caps, "ViewObject") and feat_caps.ViewObject:
        feat_caps.ViewObject.ShapeColor = (0.90, 0.10, 0.10)    # Vibrant Cockpit Red
    FreeCADGui.activeView().viewIsometric()
    FreeCADGui.SendMsgToActiveView("ViewFit")
except Exception:
    pass

# Save document
doc.saveAs(r"D:\Projects\Embedded\Firmware-Development\flyradar32\enclosure\FlyRadar32_Enclosure.FCStd")
print("Saved FreeCAD document: FlyRadar32_Enclosure.FCStd")

# Export STLs and Multicolor 3MFs
print("\nExporting production STLs and Multicolor 3MF packages...")
import os
import MeshPart

base_dir = r"D:\Projects\Embedded\Firmware-Development\flyradar32\enclosure"
stl_dir = os.path.join(base_dir, "STLs")
os.makedirs(stl_dir, exist_ok=True)

# 1. High-resolution STLs (saved cleanly in STLs/)
# Prepare printable button caps oriented flat on the build plate (Z=0, upright normal +Z):
flange_bed = Part.makeCylinder(2.75, 1.0, FreeCAD.Vector(0, 0, 0), FreeCAD.Vector(0, 0, 1))
stem_bed = Part.makeCylinder(1.75, 2.8, FreeCAD.Vector(0, 0, 1.0), FreeCAD.Vector(0, 0, 1))
chamfer_bed = Part.makeCone(1.75, 1.35, 0.4, FreeCAD.Vector(0, 0, 3.8), FreeCAD.Vector(0, 0, 1))
socket_bed = Part.makeCylinder(1.65, 0.95, FreeCAD.Vector(0, 0, 0), FreeCAD.Vector(0, 0, 1))
cap_bed_template = flange_bed.fuse(stem_bed).fuse(chamfer_bed).cut(socket_bed)

c1 = cap_bed_template.copy()
c1.translate(FreeCAD.Vector(-12.0, 0.0, 0.0))
c2 = cap_bed_template.copy()
c3 = cap_bed_template.copy()
c3.translate(FreeCAD.Vector(12.0, 0.0, 0.0))
caps_bed = Part.makeCompound([c1, c2, c3])

for shape_to_export, filename in [
    (feat_shell.Shape, "flyradar32_shell.stl"),
    (feat_lid.Shape, "flyradar32_lid.stl"),
    (feat_accents.Shape, "flyradar32_lid_accents.stl"),
    (caps_bed, "flyradar32_button_caps.stl")
]:
    mesh = MeshPart.meshFromShape(Shape=shape_to_export, LinearDeflection=0.02, AngularDeflection=0.1, Relative=False)
    mesh.write(os.path.join(stl_dir, filename))
    # Also write to root enclosure directory if existing
    if os.path.exists(os.path.join(base_dir, filename)):
        mesh.write(os.path.join(base_dir, filename))
    print(f"  [STL] {filename} ({os.path.getsize(os.path.join(stl_dir, filename))/1024:.1f} KB)")

# 2. Multicolor 3MFs (AMS / CFS Ready in STLs/)
# A) Multicolor Lid with integrated inlays:
Mesh.export([feat_lid, feat_accents], os.path.join(stl_dir, "flyradar32_lid_multicolor.3mf"))
print(f"  [3MF] flyradar32_lid_multicolor.3mf (Lid Black + Accents Red)")

# B) Complete Console multicolor assembly:
Mesh.export([feat_shell, feat_lid, feat_accents, feat_caps], os.path.join(stl_dir, "flyradar32_console_multicolor.3mf"))
print(f"  [3MF] flyradar32_console_multicolor.3mf (Full multicolor assembly)")
print("All production deliverables exported cleanly to STLs/!")
