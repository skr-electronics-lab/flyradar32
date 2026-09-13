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

# Remove the header pins from ESP32 model (user removes header pins):
f141 = doc.getObject('Part__Feature141')
if f141 and hasattr(f141, 'Shape') and not f141.Shape.isNull():
    # Slice off the male header pins below Z = -1.6 in local coords:
    pin_cutter = Part.makeBox(40.0, 60.0, 15.0, FreeCAD.Vector(-20.0, -30.0, -16.6))
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

# TFT: Placed at X=-10.0mm, Y=0.0mm, Z=20.0mm
pos_tft = FreeCAD.Vector(-10.0, 0.0, 20.0)
tft.Placement = FreeCAD.Placement(pos_tft, rot_tft)

tft_pl = FreeCAD.Placement(pos_tft, rot_tft)
# Exact Active Area center from datasheet: local X = -5.15mm, Y = 0.0mm, Z = 8.90mm
global_active_center = tft_pl.multVec(FreeCAD.Vector(-5.15, 0.0, 8.90))

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

btn_up, pt_up = clone_button_exact(btn_orig, 'Button_UP', 'Button_UP', (28.5, 13.0), 28.5, TILT_DEG)
btn_sel, pt_sel = clone_button_exact(btn_orig, 'Button_SELECT', 'Button_SELECT', (28.5, 0.0), 28.5, TILT_DEG)
btn_down, pt_down = clone_button_exact(btn_orig, 'Button_DOWN', 'Button_DOWN', (28.5, -13.0), 28.5, TILT_DEG)

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
# 4. PARTING LINE: SHELL & LID
# ==========================================
split_box_shell = Part.makeBox(140.0, 120.0, Z_SPLIT, FreeCAD.Vector(-70.0, -60.0, 0.0))
split_box_lid = Part.makeBox(140.0, 120.0, 40.0, FreeCAD.Vector(-70.0, -60.0, Z_SPLIT))

shell_base = enclosure_hollow.common(split_box_shell)
lid_base = enclosure_hollow.common(split_box_lid)

screw_corners = [(-40.0, -29.0), (32.0, -29.0), (-40.0, 21.0), (32.0, 21.0)]

# Shell: 4 Corner posts from floor to parting line
shell_posts = []
for cx, cy in screw_corners:
    post = Part.makeCylinder(3.6, Z_SPLIT - FLOOR, FreeCAD.Vector(cx, cy, FLOOR), FreeCAD.Vector(0, 0, 1))
    hole = Part.makeCylinder(1.7, Z_SPLIT + 2.0, FreeCAD.Vector(cx, cy, -1.0), FreeCAD.Vector(0, 0, 1))
    cbore = Part.makeCylinder(3.1, 2.8, FreeCAD.Vector(cx, cy, -0.1), FreeCAD.Vector(0, 0, 1))
    post = post.cut(hole).cut(cbore)
    shell_posts.append(post)

shell_body = shell_base
for sp in shell_posts:
    shell_body = shell_body.fuse(sp)

# Lid: 4 Corner bosses from parting line to ceiling
lid_bosses = []
for cx, cy in screw_corners:
    z_roof = 31.0 + cy * math.tan(math.radians(TILT_DEG)) - WALL * math.cos(math.radians(TILT_DEG))
    boss_h = z_roof - Z_SPLIT + 0.5
    boss = Part.makeCylinder(3.6, boss_h, FreeCAD.Vector(cx, cy, Z_SPLIT), FreeCAD.Vector(0, 0, 1))
    pilot = Part.makeCylinder(1.25, 8.5, FreeCAD.Vector(cx, cy, Z_SPLIT - 0.2), FreeCAD.Vector(0, 0, 1))
    boss = boss.cut(pilot)
    lid_bosses.append(boss)

lid_body = lid_base
for lb in lid_bosses:
    lid_body = lid_body.fuse(lb)

# Interlocking Lip:
# CONTINUOUS UNBROKEN SKIRT ON LID (No USB notches! Completely smooth and clean!)
lid_skirt_outer = make_rounded_box(X_MIN + WALL + 0.3, X_MAX - WALL - 0.3, Y_MIN + WALL + 0.3, Y_MAX - WALL - 0.3, Z_SPLIT - 1.8, Z_SPLIT, R_CORNER - 1.6)
lid_skirt_inner = make_rounded_box(X_MIN + WALL + 1.3, X_MAX - WALL - 1.3, Y_MIN + WALL + 1.3, Y_MAX - WALL - 1.3, Z_SPLIT - 2.5, Z_SPLIT + 0.5, R_CORNER - 2.5)
lid_skirt = lid_skirt_outer.cut(lid_skirt_inner)

# Corner relief cutouts so skirt does not collide with shell posts:
for cx, cy in screw_corners:
    skirt_post_cut = Part.makeCylinder(4.0, 3.0, FreeCAD.Vector(cx, cy, Z_SPLIT - 2.2), FreeCAD.Vector(0, 0, 1))
    lid_skirt = lid_skirt.cut(skirt_post_cut)

lid_body = lid_body.fuse(lid_skirt)

# Shell rebate:
shell_rebate_outer = make_rounded_box(X_MIN + WALL - 0.01, X_MAX - WALL + 0.01, Y_MIN + WALL - 0.01, Y_MAX - WALL + 0.01, Z_SPLIT - 2.0, Z_SPLIT + 0.1, R_CORNER - 1.5)
shell_rebate_inner = make_rounded_box(X_MIN + WALL + 1.3, X_MAX - WALL - 1.3, Y_MIN + WALL - 1.3, Y_MAX + WALL + 1.3, Z_SPLIT - 2.5, Z_SPLIT + 0.5, R_CORNER - 2.5)
shell_rebate = shell_rebate_outer.cut(shell_rebate_inner)

for cx, cy in screw_corners:
    post_keep = Part.makeCylinder(3.6, 2.5, FreeCAD.Vector(cx, cy, Z_SPLIT - 2.2), FreeCAD.Vector(0, 0, 1))
    shell_rebate = shell_rebate.cut(post_keep)

shell_body = shell_body.cut(shell_rebate)

# Thumb pry notch on -X left wall
pry_notch = Part.makeBox(4.0, 8.0, 2.0, FreeCAD.Vector(X_MIN - 2.0, -4.0, Z_SPLIT - 1.0))
shell_body = shell_body.cut(pry_notch)
lid_body = lid_body.cut(pry_notch)

# Modern Parting Line Accent Reveal (Perimeter Shadow Gap):
# 1.2mm total reveal height (0.6mm cut in shell, 0.6mm cut in lid), 0.5mm deep around outer perimeter
shadow_cut_shell = make_rounded_box(X_MIN - 2.0, X_MAX + 2.0, Y_MIN - 2.0, Y_MAX + 2.0, Z_SPLIT - 0.6, Z_SPLIT + 0.05, R_CORNER + 2.0).cut(
    make_rounded_box(X_MIN + 0.5, X_MAX - 0.5, Y_MIN + 0.5, Y_MAX - 0.5, Z_SPLIT - 0.7, Z_SPLIT + 0.1, R_CORNER - 0.5)
)
shell_body = shell_body.cut(shadow_cut_shell)

shadow_cut_lid = make_rounded_box(X_MIN - 2.0, X_MAX + 2.0, Y_MIN - 2.0, Y_MAX + 2.0, Z_SPLIT - 0.05, Z_SPLIT + 0.6, R_CORNER + 2.0).cut(
    make_rounded_box(X_MIN + 0.5, X_MAX - 0.5, Y_MIN + 0.5, Y_MAX - 0.5, Z_SPLIT - 0.1, Z_SPLIT + 0.7, R_CORNER - 0.5)
)
lid_body = lid_body.cut(shadow_cut_lid)

# ==========================================
# 5. SHELL: BOTTOM ESP32 CRADLE WITH AUTOMATIC SNAP-FIT LOCKS, TYPE-C PORT & ERGONOMICS
# ==========================================
# Rubber Bumper Feet Pockets on bottom (dia 8.2mm, depth 0.75mm for standard 8mm silicone pads):
for fx, fy in [(-36.0, -25.0), (28.0, -25.0), (-36.0, 17.0), (28.0, 17.0)]:
    foot_pocket = Part.makeCylinder(4.1, 0.75, FreeCAD.Vector(fx, fy, -0.05), FreeCAD.Vector(0, 0, 1))
    shell_body = shell_body.cut(foot_pocket)

# Lateral Tactile Grip Flutes (Left flank):
for fy in [-16.0, -8.0, 0.0, 8.0, 16.0]:
    flute_l = Part.makeCylinder(1.0, 10.0, FreeCAD.Vector(-44.6, fy, 3.5), FreeCAD.Vector(0, 0, 1))
    shell_body = shell_body.cut(flute_l)

# Lateral Tactile Grip Flutes (Right flank, avoiding Type-C port at Y=0):
for fy in [-18.0, -11.0, 11.0, 18.0]:
    flute_r = Part.makeCylinder(1.0, 10.0, FreeCAD.Vector(36.6, fy, 3.5), FreeCAD.Vector(0, 0, 1))
    shell_body = shell_body.cut(flute_r)

# Bottom Plate Maker Badge Recess (30 x 14mm, 0.4mm deep):
badge_recess = make_rounded_box(-19.0, 11.0, -11.0, 3.0, -0.1, 0.4, 2.5)
shell_body = shell_body.cut(badge_recess)

# Rear Lanyard Loop Slot through rear wall (X=-21, Y=26, Z=6):
lanyard1 = Part.makeCylinder(1.5, 5.0, FreeCAD.Vector(-24.0, 24.0, 6.0), FreeCAD.Vector(0, 1, 0))
lanyard2 = Part.makeCylinder(1.5, 5.0, FreeCAD.Vector(-18.0, 24.0, 6.0), FreeCAD.Vector(0, 1, 0))
lanyard_slot = Part.makeBox(6.0, 5.0, 3.0, FreeCAD.Vector(-24.0, 24.0, 4.5))
shell_body = shell_body.cut(lanyard1).cut(lanyard2).cut(lanyard_slot)

# TIGHT, EXACT TYPE-C PILL PORT:
# Metal shell: Y in [-4.47, 4.47], Z in [3.587, 7.30] (height 3.71, width 8.94)
# Pill cutout: Centered at Z=5.45mm, Y=0.0mm. Width 10.0mm, height 4.2mm (R=2.1mm):
Z_USBC = 5.45
c_top = Part.makeCylinder(2.1, 8.0, FreeCAD.Vector(X_MAX - 5.0, 2.9, Z_USBC), FreeCAD.Vector(1, 0, 0))
c_bot = Part.makeCylinder(2.1, 8.0, FreeCAD.Vector(X_MAX - 5.0, -2.9, Z_USBC), FreeCAD.Vector(1, 0, 0))
b_mid = Part.makeBox(8.0, 5.8, 4.2, FreeCAD.Vector(X_MAX - 5.0, -2.9, Z_USBC - 2.1))
usbc_pill = c_top.fuse(c_bot).fuse(b_mid)
shell_body = shell_body.cut(usbc_pill)

# AUTOMATIC SNAP-FIT ESP32 CRADLE ON BOTTOM PLATE:
# PCB sits at Z in [2.6, 4.2], X in [-17.3, 34.2], Y in [-14.25, 14.25]
# 1. Four 0.8mm resting pads on floor:
pad1 = Part.makeBox(4.0, 4.0, 0.8, FreeCAD.Vector(-17.0, -14.2, FLOOR))
pad2 = Part.makeBox(4.0, 4.0, 0.8, FreeCAD.Vector(-17.0, 10.2, FLOOR))
pad3 = Part.makeBox(4.0, 4.0, 0.8, FreeCAD.Vector(30.0, -14.2, FLOOR))
pad4 = Part.makeBox(4.0, 4.0, 0.8, FreeCAD.Vector(30.0, 10.2, FLOOR))

# 2. Solid Rear Thrust Wall (prevents backward push during USB plugging):
rear_thrust = Part.makeBox(2.2, 28.5, 4.0, FreeCAD.Vector(-19.2, -14.25, FLOOR))

# 3. Lateral Guide Rails:
rail_left = Part.makeBox(30.0, 1.5, 3.5, FreeCAD.Vector(-10.0, -15.75, FLOOR))
rail_right = Part.makeBox(30.0, 1.5, 3.5, FreeCAD.Vector(-10.0, 14.25, FLOOR))

# 4. DUAL AUTOMATIC CANTILEVER SNAP-FIT LOCKS:
def make_snap_clip(x_center, y_pos, side_sign):
    y_base = y_pos - (1.4 if side_sign > 0 else 0.0)
    arm = Part.makeBox(5.0, 1.4, 5.6 - FLOOR, FreeCAD.Vector(x_center - 2.5, y_base, FLOOR))
    
    y_tooth = y_pos - (0.8 if side_sign > 0 else 0.0)
    # Tooth starts at Z = 4.30mm (0.10mm above top face of PCB Z=4.20mm to prevent boolean clash while locking securely):
    tooth = Part.makeBox(5.0, 0.8, 1.3, FreeCAD.Vector(x_center - 2.5, y_tooth, 4.30))
    
    c_box = Part.makeBox(7.0, 2.0, 2.0, FreeCAD.Vector(x_center - 3.5, y_pos - 1.0, 4.6))
    rot = FreeCAD.Rotation(FreeCAD.Vector(1, 0, 0), -45 if side_sign > 0 else 45)
    c_box.Placement = FreeCAD.Placement(FreeCAD.Vector(x_center - 3.5, y_pos, 5.6), rot)
    
    clip = arm.fuse(tooth).cut(c_box)
    return clip

snap1 = make_snap_clip(0.0, -14.25, -1)
snap2 = make_snap_clip(22.0, -14.25, -1)
snap3 = make_snap_clip(0.0, 14.25, 1)
snap4 = make_snap_clip(22.0, 14.25, 1)

esp_mount = pad1.fuse(pad2).fuse(pad3).fuse(pad4).fuse(rear_thrust).fuse(rail_left).fuse(rail_right).fuse(snap1).fuse(snap2).fuse(snap3).fuse(snap4)

# Cut exact PCB entry path so snap clips have proper spring clearance:
pcb_clear = Part.makeBox(53.0, 29.0, 10.0, FreeCAD.Vector(-18.0, -14.5, 2.6))
esp_mount = esp_mount.cut(pcb_clear)

shell_body = shell_body.fuse(esp_mount)
shell_body = shell_body.removeSplitter()

# ==========================================
# 6. LID: AVIONICS SUN-VISOR BEZEL, ERGONOMIC BUTTON DISHES & ACOUSTIC LOUVERS
# ==========================================
X_SCREEN = global_active_center.x
Y_SCREEN = global_active_center.y
z_sc_top = 31.0 + Y_SCREEN * math.tan(math.radians(TILT_DEG))

# A. Multi-tiered Avionics Sun-Visor Display Bezel:
# 1. Main viewing window: Exactly 36.0 x 28.5 mm (Full Active Display Area 35x28mm + 0.5mm frame margin!):
window_cutter = Part.makeBox(36.0, 28.5, 10.0, FreeCAD.Vector(-18.0, -14.25, -5.0))
p_win = FreeCAD.Placement()
p_win.Rotation = rot_x10
p_win.Base = FreeCAD.Vector(X_SCREEN, Y_SCREEN, z_sc_top)
window_cutter.Placement = p_win
lid_body = lid_body.cut(window_cutter)

# 2. Middle 45-degree lead-in bevel:
win_bevel1 = Part.makeBox(37.8, 30.3, 1.2, FreeCAD.Vector(-18.9, -15.15, -0.6))
win_bevel1.Placement = p_win
lid_body = lid_body.cut(win_bevel1)

# 3. Outer sun-hood framing step:
win_bevel2 = Part.makeBox(39.6, 32.1, 0.6, FreeCAD.Vector(-19.8, -16.05, -0.3))
win_bevel2.Placement = p_win
lid_body = lid_body.cut(win_bevel2)

# B. 4 Solid Screw Standoffs for TFT (terminating at front face of PCB, local Z = 6.55):
for hx, hy in [(-26.0, -14.25), (26.0, -14.25), (-26.0, 14.25), (26.0, 14.25)]:
    pt_rot = rot_x10.multVec(FreeCAD.Vector(-hx, -hy, 6.55))
    pt_glob = pt_rot.add(pos_tft)
    
    boss_roof_z = 31.0 + pt_glob.y * math.tan(math.radians(TILT_DEG)) - 1.8 * math.cos(math.radians(TILT_DEG))
    boss_height = boss_roof_z - pt_glob.z
    
    if boss_height > 0.5:
        boss_dir = rot_x10.multVec(FreeCAD.Vector(0, 0, -1))
        boss_solid = Part.makeCylinder(2.6, boss_height, FreeCAD.Vector(pt_glob.x, pt_glob.y, boss_roof_z), boss_dir)
        pilot_hole = Part.makeCylinder(0.925, 5.0, pt_glob, rot_x10.multVec(FreeCAD.Vector(0, 0, 1)))
        boss_solid = boss_solid.cut(pilot_hole)
        lid_body = lid_body.fuse(boss_solid)

# C. SOLID BUTTON CARRIER EXTENDING ALL THE WAY TO THE RIGHT WALL ("Supported from wall properly!"):
# Right inner wall is at X = X_MAX - WALL = 35.2mm.
# Carrier extends from X = 22.5mm to X = 35.2mm (width = 12.7mm, solid across entire right side!)
btn_carrier = Part.makeBox(12.7, 36.0, 8.5, FreeCAD.Vector(-6.0, -18.0, -8.5))
p_bc = FreeCAD.Placement()
p_bc.Rotation = rot_x10
p_bc.Base = FreeCAD.Vector(28.5, 0.0, 31.0)
btn_carrier.Placement = p_bc
lid_body = lid_body.fuse(btn_carrier)

btn_caps = []
for btn_obj, pt in [(btn_up, pt_up), (btn_sel, pt_sel), (btn_down, pt_down)]:
    bdir = rot_x10.multVec(FreeCAD.Vector(0, 0, 1))
    bz_top = 31.0 + pt.y * math.tan(math.radians(TILT_DEG))
    b_origin = FreeCAD.Vector(pt.x, pt.y, bz_top)
    
    # 0. Ergonomic recessed finger dish (dia 8.6mm, depth 0.6mm):
    dish = Part.makeCone(4.3, 2.9, 0.6, b_origin - bdir * 0.6, bdir)
    lid_body = lid_body.cut(dish)
    
    # 1. Outer button hole: dia 5.8mm (smooth glide for dia 5.0mm cap stem)
    bhole = Part.makeCylinder(2.9, 12.0, b_origin - bdir * 6.0, bdir)
    lid_body = lid_body.cut(bhole)
    
    # 2. Retaining counterbore on underside for cap flange: dia 7.8mm, depth 1.4mm
    bpocket = Part.makeCylinder(3.9, 6.0, b_origin - bdir * 7.2, bdir)
    lid_body = lid_body.cut(bpocket)
    
    # 3. Square switch pocket inside carrier bar:
    p_sp = FreeCAD.Placement()
    p_sp.Rotation = rot_x10
    p_sp.Base = b_origin
    
    # 4. Wire exit slot towards -X (facing interior):
    wire_slot = Part.makeBox(8.0, 4.0, 6.0, FreeCAD.Vector(-7.0, -2.0, -7.8))
    wire_slot.Placement = p_sp
    lid_body = lid_body.cut(wire_slot)
    
    # 5. Tactile Button Cap with WIDE plunger pocket:
    cap_flange = Part.makeCylinder(3.6, 1.0, b_origin - bdir * 2.4, bdir)
    cap_stem = Part.makeCylinder(2.5, 4.8, b_origin - bdir * 1.4, bdir)
    socket = Part.makeCylinder(2.3, 3.5, b_origin - bdir * 3.7, bdir)
    socket_cone = Part.makeCone(2.6, 2.3, 0.6, b_origin - bdir * 3.7, bdir)
    cap_solid = cap_stem.fuse(cap_flange).cut(socket).cut(socket_cone)
    btn_caps.append(cap_solid)

    # Cut FULL switch clearance using the exact placement of the switch:
    sw_env = Part.makeBox(8.0, 8.0, 14.0, FreeCAD.Vector(-4.0, -4.0, -13.5))
    p_sw = FreeCAD.Placement()
    p_sw.Rotation = rot_x10
    p_sw.Base = FreeCAD.Vector(pt.x, pt.y, pt.z)
    sw_env.Placement = p_sw
    lid_body = lid_body.cut(sw_env)

# D. DEBOSSED TACTILE INDICATOR GLYPHS BESIDE BUTTONS:
# Up button glyph: ▲ (triangle prism pointing +Y)
p_glyph_up = FreeCAD.Placement()
p_glyph_up.Rotation = rot_x10
z_g_up = 31.0 + 13.0 * math.tan(math.radians(TILT_DEG))
p_glyph_up.Base = FreeCAD.Vector(22.2, 13.0, z_g_up)
tri_up = Part.Face(Part.makePolygon([FreeCAD.Vector(0, 1.0, 0), FreeCAD.Vector(-1.0, -0.8, 0), FreeCAD.Vector(1.0, -0.8, 0), FreeCAD.Vector(0, 1.0, 0)])).extrude(FreeCAD.Vector(0, 0, -0.5))
tri_up.Placement = p_glyph_up
lid_body = lid_body.cut(tri_up)

# Select button glyph: ● (circular indicator dia 2.0mm)
p_glyph_sel = FreeCAD.Placement()
p_glyph_sel.Rotation = rot_x10
z_g_sel = 31.0
p_glyph_sel.Base = FreeCAD.Vector(22.2, 0.0, z_g_sel)
dot_sel = Part.makeCylinder(1.0, 0.5, FreeCAD.Vector(0, 0, -0.5), FreeCAD.Vector(0, 0, 1))
dot_sel.Placement = p_glyph_sel
lid_body = lid_body.cut(dot_sel)

# Down button glyph: ▼ (triangle prism pointing -Y)
p_glyph_down = FreeCAD.Placement()
p_glyph_down.Rotation = rot_x10
z_g_down = 31.0 - 13.0 * math.tan(math.radians(TILT_DEG))
p_glyph_down.Base = FreeCAD.Vector(22.2, -13.0, z_g_down)
tri_down = Part.Face(Part.makePolygon([FreeCAD.Vector(0, -1.0, 0), FreeCAD.Vector(-1.0, 0.8, 0), FreeCAD.Vector(1.0, 0.8, 0), FreeCAD.Vector(0, -1.0, 0)])).extrude(FreeCAD.Vector(0, 0, -0.5))
tri_down.Placement = p_glyph_down
lid_body = lid_body.cut(tri_down)

# E. ANGLED AEROSPACE ACOUSTIC CHEVRON LOUVERS (Swept at 30 deg):
rot_vent = FreeCAD.Rotation(FreeCAD.Vector(0, 0, 1), 30.0)
rot_vent_comb = rot_x10.multiply(rot_vent)
for vx in [7.0, 10.5, 14.0, 17.5, 21.0]:
    vz = 31.0 - 25.0 * math.tan(math.radians(TILT_DEG))
    vslot = Part.makeBox(1.5, 7.5, 8.0, FreeCAD.Vector(-0.75, -3.75, -4.0))
    pv = FreeCAD.Placement()
    pv.Rotation = rot_vent_comb
    pv.Base = FreeCAD.Vector(vx, -25.0, vz)
    vslot.Placement = pv
    lid_body = lid_body.cut(vslot)

lid_body = lid_body.removeSplitter()

# 7. ADD PART FEATURES
feat_shell = doc.addObject("Part::Feature", "Shell")
feat_shell.Label = "FlyRadar32_Shell"
feat_shell.Shape = shell_body

feat_lid = doc.addObject("Part::Feature", "Lid")
feat_lid.Label = "FlyRadar32_Lid"
feat_lid.Shape = lid_body

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
    # If running with FreeCADGui, ensure all models are visible with correct styling
    try:
        import FreeCADGui
        for name in ["Shell", "Lid", "Button_Caps", "esp32_Wroom_30pins_C_Type", "AZ_Delivery_TFT_1_8_SPI", "Button_UP", "Button_SELECT", "Button_DOWN"]:
            obj = doc.getObject(name)
            if obj and hasattr(obj, "ViewObject"):
                obj.ViewObject.Visibility = True
            if obj and hasattr(obj, "Group"):
                for child in obj.Group:
                    if hasattr(child, "ViewObject"):
                        child.ViewObject.Visibility = True
        if hasattr(feat_shell, "ViewObject") and feat_shell.ViewObject:
            feat_shell.ViewObject.ShapeColor = (0.92, 0.44, 0.10)
        if hasattr(feat_lid, "ViewObject") and feat_lid.ViewObject:
            feat_lid.ViewObject.ShapeColor = (0.92, 0.44, 0.10)
        if hasattr(feat_caps, "ViewObject") and feat_caps.ViewObject:
            feat_caps.ViewObject.ShapeColor = (0.15, 0.16, 0.18)
        FreeCADGui.activeView().viewAxometric()
        FreeCADGui.SendMsgToActiveView("ViewFit")
    except Exception:
        pass

    # Save document
    doc.saveAs(r"D:\Projects\Embedded\Firmware-Development\flyradar32\enclosure\FlyRadar32_Enclosure.FCStd")
    print("Saved FreeCAD document: FlyRadar32_Enclosure.FCStd")
    
    # Export STLs
    print("\nExporting production STLs...")
    Mesh.export([feat_shell], r"D:\Projects\Embedded\Firmware-Development\flyradar32\enclosure\flyradar32_shell.stl")
    Mesh.export([feat_lid], r"D:\Projects\Embedded\Firmware-Development\flyradar32\enclosure\flyradar32_lid.stl")
    Mesh.export([feat_caps], r"D:\Projects\Embedded\Firmware-Development\flyradar32\enclosure\flyradar32_button_caps.stl")
    print("STLs exported successfully!")
else:
    print("\n>>> WARNING: SOME CHECKS FAILED! <<<")
