# -*- coding: utf-8 -*-
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
TILT_DEG = 15.0
rot_tilt = FreeCAD.Rotation(FreeCAD.Vector(1, 0, 0), TILT_DEG)
rot_z180 = FreeCAD.Rotation(FreeCAD.Vector(0, 0, 1), 180)
rot_tft = rot_tilt.multiply(rot_z180)
rot_esp = FreeCAD.Rotation(FreeCAD.Vector(0, 0, 1), 90)

# ESP32: Placed on the bottom floor resting on 0.8mm ribs (PCB bottom at Z = 2.6mm, top at Z = 4.2mm)
# USB-C tip is at X = 8.7 + 27.7 = 36.4mm (0.6mm from outer wall X=37.0mm)
pos_esp = FreeCAD.Vector(8.7, 0.0, 4.2)
esp.Placement = FreeCAD.Placement(pos_esp, rot_esp)

# TFT: Placed at X=-10.0mm, active center Y=-3.00mm (aligned with SELECT button at Y=-3.00mm,
# symmetrically framing UP button at Y=10.0mm and DOWN button at Y=-16.0mm with exact 3.10mm top/bottom gaps)
pos_tft = FreeCAD.Vector(-10.0, -0.697, 19.736)
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
cutter_box.Placement = FreeCAD.Placement(FreeCAD.Vector(0.0, 0.0, 31.0), rot_tilt)
wedge_solid = raw_outer.cut(cutter_box)

cavity_raw = make_rounded_box(X_MIN + WALL, X_MAX - WALL, Y_MIN + WALL, Y_MAX - WALL, FLOOR, 46.0, R_CORNER - 1.5)
cavity_cutter = Part.makeBox(130.0, 110.0, 30.0, FreeCAD.Vector(-65.0, -55.0, 0.0))
cavity_cutter.Placement = FreeCAD.Placement(FreeCAD.Vector(0.0, 0.0, 31.0 - WALL), rot_tilt)
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
# Shell lip: solid ring Z=16.5->18.0, thickness=1.05mm, outer ledge=1.55mm
# Lid rebate: outer wall=1.40mm, clearance 0.15mm on lip outer face and snap pocket
LID_OUTER_WALL   = 1.40
SHELL_LEDGE      = LID_OUTER_WALL + 0.15        # 1.55mm
LIP_THICKNESS    = 1.05                         # 1.05mm
SHELL_LIP_INNER  = SHELL_LEDGE + LIP_THICKNESS  # 2.60mm
LID_REBATE_INNER = SHELL_LIP_INNER + 0.15       # 2.75mm

# 1. Shell: Solid Alignment Lip rising from Z = 16.5 to Z = 18.75 (taller, easy for slicer to resolve):
lip_outer = make_rounded_box(X_MIN + SHELL_LEDGE, X_MAX - SHELL_LEDGE, Y_MIN + SHELL_LEDGE, Y_MAX - SHELL_LEDGE, 16.5, 18.75, R_CORNER - SHELL_LEDGE)
lip_inner = make_rounded_box(X_MIN + SHELL_LIP_INNER, X_MAX - SHELL_LIP_INNER, Y_MIN + SHELL_LIP_INNER, Y_MAX - SHELL_LIP_INNER, 16.4, 18.85, R_CORNER - SHELL_LIP_INNER)
shell_lip = lip_outer.cut(lip_inner)

# Supporting collar under shell lip from Z=14.5 to Z=16.5 (supports inner face solidly):
collar_outer = make_rounded_box(X_MIN + WALL, X_MAX - WALL, Y_MIN + WALL, Y_MAX - WALL, 14.5, 16.5, R_CORNER - WALL)
collar_inner = make_rounded_box(X_MIN + SHELL_LIP_INNER, X_MAX - SHELL_LIP_INNER, Y_MIN + SHELL_LIP_INNER, Y_MAX - SHELL_LIP_INNER, 14.4, 16.6, R_CORNER - SHELL_LIP_INNER)
shell_collar = collar_outer.cut(collar_inner)
shell_body = shell_body.fuse(shell_collar)

# ============================================================
# 2. THREE AUTO-LOCK CANTILEVER SNAP TABS (FRONT, REAR, LEFT)
# Taller snap profile (Z=16.60 to 18.80) with self-supporting ramps for easy FDM slicing.
# Right wall (button carrier) has ZERO snap tabs to guarantee 100% solid button walls!
# ============================================================
TAB_W    = 10.0  # tab width
TAB_P    = 0.65  # solid snap ramp protrusion (mm) - increased for real retention preload
Z_BOT    = 16.60 # bottom base of support ramp on inner lip face
Z_MBOT   = 17.10 # top of support ramp / bottom of vertical locking shoulder (lowered for 0.95mm shoulder)
Z_MTOP   = 18.05 # top of vertical locking shoulder / bottom of lead-in ramp
Z_TOP    = 18.80 # top tip of lead-in ramp

def _ramp_tab_front(cx, W, y_wall, P):
    pts = [
        FreeCAD.Vector(cx - W/2, y_wall,     Z_BOT),
        FreeCAD.Vector(cx - W/2, y_wall + P, Z_MBOT),
        FreeCAD.Vector(cx - W/2, y_wall + P, Z_MTOP),
        FreeCAD.Vector(cx - W/2, y_wall,     Z_TOP),
        FreeCAD.Vector(cx - W/2, y_wall,     Z_BOT),
    ]
    return Part.Face(Part.makePolygon(pts)).extrude(FreeCAD.Vector(W, 0, 0))

tab_front = _ramp_tab_front(0.0, TAB_W, Y_MIN + SHELL_LIP_INNER, TAB_P)

def _ramp_tab_rear(cx, W, y_wall, P):
    pts = [
        FreeCAD.Vector(cx - W/2, y_wall,     Z_BOT),
        FreeCAD.Vector(cx - W/2, y_wall - P, Z_MBOT),
        FreeCAD.Vector(cx - W/2, y_wall - P, Z_MTOP),
        FreeCAD.Vector(cx - W/2, y_wall,     Z_TOP),
        FreeCAD.Vector(cx - W/2, y_wall,     Z_BOT),
    ]
    return Part.Face(Part.makePolygon(pts)).extrude(FreeCAD.Vector(W, 0, 0))

tab_rear = _ramp_tab_rear(0.0, TAB_W, Y_MAX - SHELL_LIP_INNER, TAB_P)

def _ramp_tab_left(cy, W, x_wall, P):
    pts = [
        FreeCAD.Vector(x_wall,     cy - W/2, Z_BOT),
        FreeCAD.Vector(x_wall + P, cy - W/2, Z_MBOT),
        FreeCAD.Vector(x_wall + P, cy - W/2, Z_MTOP),
        FreeCAD.Vector(x_wall,     cy - W/2, Z_TOP),
        FreeCAD.Vector(x_wall,     cy - W/2, Z_BOT),
    ]
    return Part.Face(Part.makePolygon(pts)).extrude(FreeCAD.Vector(0, W, 0))

tab_left = _ramp_tab_left(0.0, TAB_W, X_MIN + SHELL_LIP_INNER, TAB_P)

# Shell lip fuses 3 tabs (Front, Rear, Left). Right lip is 100% smooth and continuous!
shell_lip = shell_lip.fuse(tab_front).fuse(tab_rear).fuse(tab_left)
shell_body = shell_body.fuse(shell_lip)

# 3. Lid: Internal Reinforcement Pads with Downward Interlocking Tongues (Z=14.50 to Roof):
# Extends 2.0mm downward past Z_SPLIT (16.5 -> 14.5) into the shell cavity as an interlocking guide tongue!
PAD_W           = 16.0
TOTAL_PAD_DEPTH = (LID_REBATE_INNER + 3.00) - WALL  # 3.95mm deep from nominal wall
TONGUE_DOWN     = 2.00                              # 2.0mm extension past parting plane
Z_PAD_BOT       = Z_SPLIT - TONGUE_DOWN             # 14.50mm
Y_F_PAD         = Y_MIN + LID_REBATE_INNER
Y_R_PAD         = Y_MAX - LID_REBATE_INNER
X_L_PAD         = X_MIN + LID_REBATE_INNER

# Front pad + 45-deg top ramp + downward tongue (integrally anchored to lid wall, roof, and interlocking tongue):
pts_f = [
    FreeCAD.Vector(-PAD_W/2, Y_MIN + LID_REBATE_INNER,                   Z_PAD_BOT),
    FreeCAD.Vector(-PAD_W/2, Y_MIN + WALL + TOTAL_PAD_DEPTH - 0.5,       Z_PAD_BOT),
    FreeCAD.Vector(-PAD_W/2, Y_MIN + WALL + TOTAL_PAD_DEPTH,             Z_PAD_BOT + 0.5),
    FreeCAD.Vector(-PAD_W/2, Y_MIN + WALL + TOTAL_PAD_DEPTH,             20.0),
    FreeCAD.Vector(-PAD_W/2, Y_MIN + WALL,                               20.0 + TOTAL_PAD_DEPTH),
    FreeCAD.Vector(-PAD_W/2, Y_MIN + WALL,                               16.5),
    FreeCAD.Vector(-PAD_W/2, Y_MIN + LID_REBATE_INNER,                   16.5),
    FreeCAD.Vector(-PAD_W/2, Y_MIN + LID_REBATE_INNER,                   Z_PAD_BOT),
]
pad_front = Part.Face(Part.makePolygon(pts_f)).extrude(FreeCAD.Vector(PAD_W, 0, 0))

# Rear pad + 45-deg top ramp + downward tongue:
pts_r = [
    FreeCAD.Vector(-PAD_W/2, Y_MAX - LID_REBATE_INNER,                   Z_PAD_BOT),
    FreeCAD.Vector(-PAD_W/2, Y_MAX - WALL - TOTAL_PAD_DEPTH + 0.5,       Z_PAD_BOT),
    FreeCAD.Vector(-PAD_W/2, Y_MAX - WALL - TOTAL_PAD_DEPTH,             Z_PAD_BOT + 0.5),
    FreeCAD.Vector(-PAD_W/2, Y_MAX - WALL - TOTAL_PAD_DEPTH,             22.0),
    FreeCAD.Vector(-PAD_W/2, Y_MAX - WALL,                               22.0 + TOTAL_PAD_DEPTH),
    FreeCAD.Vector(-PAD_W/2, Y_MAX - WALL,                               16.5),
    FreeCAD.Vector(-PAD_W/2, Y_MAX - LID_REBATE_INNER,                   16.5),
    FreeCAD.Vector(-PAD_W/2, Y_MAX - LID_REBATE_INNER,                   Z_PAD_BOT),
]
pad_rear = Part.Face(Part.makePolygon(pts_r)).extrude(FreeCAD.Vector(PAD_W, 0, 0))

# Left pad + 45-deg top ramp + downward tongue:
pts_l = [
    FreeCAD.Vector(X_MIN + LID_REBATE_INNER,                   -PAD_W/2, Z_PAD_BOT),
    FreeCAD.Vector(X_MIN + WALL + TOTAL_PAD_DEPTH - 0.5,       -PAD_W/2, Z_PAD_BOT),
    FreeCAD.Vector(X_MIN + WALL + TOTAL_PAD_DEPTH,             -PAD_W/2, Z_PAD_BOT + 0.5),
    FreeCAD.Vector(X_MIN + WALL + TOTAL_PAD_DEPTH,             -PAD_W/2, 22.0),
    FreeCAD.Vector(X_MIN + WALL,                               -PAD_W/2, 22.0 + TOTAL_PAD_DEPTH),
    FreeCAD.Vector(X_MIN + WALL,                               -PAD_W/2, 16.5),
    FreeCAD.Vector(X_MIN + LID_REBATE_INNER,                   -PAD_W/2, 16.5),
    FreeCAD.Vector(X_MIN + LID_REBATE_INNER,                   -PAD_W/2, Z_PAD_BOT),
]
pad_left = Part.Face(Part.makePolygon(pts_l)).extrude(FreeCAD.Vector(0, PAD_W, 0))

lid_body = lid_body.fuse(pad_front).fuse(pad_rear).fuse(pad_left)
lid_body = lid_body.cut(cutter_box)

# 4. Lid: Solid Wall Rebate to receive the Shell Lip:
rebate_outer = make_rounded_box(X_MIN + LID_OUTER_WALL, X_MAX - LID_OUTER_WALL, Y_MIN + LID_OUTER_WALL, Y_MAX - LID_OUTER_WALL, 16.45, 19.00, R_CORNER - LID_OUTER_WALL)
rebate_inner = make_rounded_box(X_MIN + LID_REBATE_INNER, X_MAX - LID_REBATE_INNER, Y_MIN + LID_REBATE_INNER, Y_MAX - LID_REBATE_INNER, 16.40, 19.05, R_CORNER - LID_REBATE_INNER)
lid_rebate = rebate_outer.cut(rebate_inner)
lid_body = lid_body.cut(lid_rebate)

# ============================================================
# 5. MATING SNAP POCKETS (LID) - BLIND CATCH RECESSES
# Correct snap design: only cuts the INNER catch recess (y_reb -> y_reb+D).
# NO outer slot - solid pad wall on outer side locks the shell tab in Y direction.
# Shell tab deflects against solid pad face during insertion, snaps into blind recess.
# ============================================================
POCKET_W = 11.0  # pocket length along wall
POCKET_D = 0.55  # catch pocket depth (shell tab protrudes 0.10mm more = 0.10mm preload)
PZ_BOT   = Z_BOT   # entry chamfer start  (16.60) - matches shell tab bottom
PZ_MBOT  = 17.10   # bottom of vertical locking pocket (matches Z_MBOT exactly)
PZ_MTOP  = 18.05   # top of vertical pocket recess (matches Z_MTOP exactly)
PZ_TOP   = 18.85   # top of lead-in chamfer (Z_TOP + 0.05mm clearance)

def _snap_pocket_front(cx, W, y_reb, D):
    # Blind pocket with matched entry chamfer on INNER side only.
    # Outer side (y < y_reb) stays SOLID - solid backing wall locks lid in Y direction.
    # Entry chamfer (Z_BOT->Z_MBOT): clears the shell tab lead-in ramp as lid descends.
    # Locking recess (Z_MBOT->Z_MTOP): tab snaps into blind pocket - Y-locked.
    # Top chamfer (Z_MTOP->Z_TOP): lead-in for smooth snap-in feel.
    pts = [
        FreeCAD.Vector(cx - W/2, y_reb,     PZ_BOT),   # entry start (flush)
        FreeCAD.Vector(cx - W/2, y_reb + D, PZ_MBOT),  # entry ramp end = locking floor
        FreeCAD.Vector(cx - W/2, y_reb + D, PZ_MTOP),  # locking back wall
        FreeCAD.Vector(cx - W/2, y_reb,     PZ_TOP),   # top lead-in tip
        FreeCAD.Vector(cx - W/2, y_reb,     PZ_BOT),   # back to start
    ]
    return Part.Face(Part.makePolygon(pts)).extrude(FreeCAD.Vector(W, 0, 0))

pocket_front = _snap_pocket_front(0.0, POCKET_W, Y_F_PAD, POCKET_D)

def _snap_pocket_rear(cx, W, y_reb, D):
    pts = [
        FreeCAD.Vector(cx - W/2, y_reb,     PZ_BOT),
        FreeCAD.Vector(cx - W/2, y_reb - D, PZ_MBOT),
        FreeCAD.Vector(cx - W/2, y_reb - D, PZ_MTOP),
        FreeCAD.Vector(cx - W/2, y_reb,     PZ_TOP),
        FreeCAD.Vector(cx - W/2, y_reb,     PZ_BOT),
    ]
    return Part.Face(Part.makePolygon(pts)).extrude(FreeCAD.Vector(W, 0, 0))

pocket_rear = _snap_pocket_rear(0.0, POCKET_W, Y_R_PAD, POCKET_D)

def _snap_pocket_left(cy, W, x_reb, D):
    pts = [
        FreeCAD.Vector(x_reb,     cy - W/2, PZ_BOT),
        FreeCAD.Vector(x_reb + D, cy - W/2, PZ_MBOT),
        FreeCAD.Vector(x_reb + D, cy - W/2, PZ_MTOP),
        FreeCAD.Vector(x_reb,     cy - W/2, PZ_TOP),
        FreeCAD.Vector(x_reb,     cy - W/2, PZ_BOT),
    ]
    return Part.Face(Part.makePolygon(pts)).extrude(FreeCAD.Vector(0, W, 0))

pocket_left = _snap_pocket_left(0.0, POCKET_W, X_L_PAD, POCKET_D)

# Cut only 3 pockets (Front, Rear, Left). Right wall (button side) has ZERO cuts!
lid_body = lid_body.cut(pocket_front).cut(pocket_rear).cut(pocket_left)


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

# PRECISION TYPE-C PORT (Centered at Y=0, Z=5.75mm, Width=10.00mm, Height=4.40mm, R=2.20mm):
# Snugly and elegantly frames the ESP32 Type-C connector with balanced 0.65-0.90mm clearance all around
# Eliminates tight top edge while guaranteeing 0.000000 mm³ interference!
Z_USBC = 5.75
W_USBC = 10.00
H_USBC = 4.40
R_USBC = H_USBC / 2.0  # 2.20mm
DY_USBC = (W_USBC / 2.0) - R_USBC  # 5.00 - 2.20 = 2.80mm
c_top = Part.makeCylinder(R_USBC, 8.0, FreeCAD.Vector(X_MAX - 5.0,  DY_USBC, Z_USBC), FreeCAD.Vector(1, 0, 0))
c_bot = Part.makeCylinder(R_USBC, 8.0, FreeCAD.Vector(X_MAX - 5.0, -DY_USBC, Z_USBC), FreeCAD.Vector(1, 0, 0))
b_mid = Part.makeBox(8.0, 2 * DY_USBC, 2 * R_USBC, FreeCAD.Vector(X_MAX - 5.0, -DY_USBC, Z_USBC - R_USBC))
usbc_pill = c_top.fuse(c_bot).fuse(b_mid)
shell_body = shell_body.cut(usbc_pill)

# ==========================================
# AUTOMATIC SNAP-FIT ESP32 CRADLE WITH 2.80mm LOCATOR PINS:
# ESP32 PCB: X in [-17.30, 34.20], Y in [-14.25, 14.25], Z in [2.60, 4.20]
# 4 Corner Mounting Holes in PCB are Dia 3.0mm (R=1.50mm) at:
# (-16.55, -13.50), (-16.55, 13.50), (33.95, -13.50), (33.95, 13.50)
# ==========================================
GAP_ESP_Y = 0.40
GAP_ESP_X = 0.60
# Accommodates both 29.0mm CH340 board and 28.5mm WROOM board with 0.5mm clearance:
Y_ESP_MIN = -15.00  # Lateral guide rail inner face (Y = -15.00mm)
Y_ESP_MAX = 15.00   # Lateral guide rail inner face (Y = +15.00mm)
X_ESP_REAR = -18.00 # Rear thrust wall inner face (X = -18.00mm)

# 1. Four resting pads with Dia 2.40mm (R=1.20mm) locator pins:
# Perfectly centered for CH340 (24.00 x 46.48mm pitch) and WROOM (23.20 x 47.00mm pitch)
# Front pins: X = +32.00mm, Y = +-11.85mm
# Rear pins:  X = -14.65mm, Y = +-11.85mm
# Pitch in X = 46.65mm, Pitch in Y = 23.70mm
# Conical lead-in tip (R=1.20 -> 0.65mm) for effortless drop-in slip fit inside Dia 3.20mm holes
esp_pads = []
esp_pins = []
for hx, hy in [(-14.65, -11.85), (-14.65, 11.85), (32.00, -11.85), (32.00, 11.85)]:
    # Resting pad: 5.0 x 5.0 mm, height 0.8mm (Z in [1.80, 2.60])
    pad = Part.makeBox(5.0, 5.0, 0.80, FreeCAD.Vector(hx - 2.5, hy - 2.5, FLOOR))
    esp_pads.append(pad)
    # 2.40mm locator pin: height 1.80mm straight (matching 1.60mm PCB) + 0.60mm conical guide tip (Z in [2.60, 5.00])
    cyl = Part.makeCylinder(1.20, 1.80, FreeCAD.Vector(hx, hy, 2.60), FreeCAD.Vector(0, 0, 1))
    tip = Part.makeCone(1.20, 0.60, 0.60, FreeCAD.Vector(hx, hy, 4.40), FreeCAD.Vector(0, 0, 1))
    esp_pins.append(cyl.fuse(tip))

all_pads = esp_pads[0].fuse(esp_pads[1]).fuse(esp_pads[2]).fuse(esp_pads[3])
all_pins = esp_pins[0].fuse(esp_pins[1]).fuse(esp_pins[2]).fuse(esp_pins[3])

# 2. Solid Rear Thrust Wall (2.0mm thick, inner face at X_ESP_REAR = -18.00mm):
thrust_w = Y_ESP_MAX - Y_ESP_MIN
rear_thrust = Part.makeBox(2.0, thrust_w, 4.0, FreeCAD.Vector(X_ESP_REAR - 2.0, Y_ESP_MIN, FLOOR))

# 3. Lateral Guide Rails with 0.5mm gap each side (total width 30.0mm):
rail_left = Part.makeBox(24.0, 1.5, 3.5, FreeCAD.Vector(0.0, Y_ESP_MIN - 1.5, FLOOR))
rail_right = Part.makeBox(24.0, 1.5, 3.5, FreeCAD.Vector(0.0, Y_ESP_MAX, FLOOR))

snap_cleared = all_pads.fuse(all_pins).fuse(rear_thrust).fuse(rail_left).fuse(rail_right)

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
p_win.Rotation = rot_tilt
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
v_tft_roof_up = rot_tilt.multVec(FreeCAD.Vector(0, 0, 1))
v_tft_pcb_down = rot_tilt.multVec(FreeCAD.Vector(0, 0, -1))
TFT_HOLES_LOCAL = [(-26.0, -14.25), (26.0, -14.25), (-26.0, 14.25), (26.0, 14.25)]

for hx, hy in TFT_HOLES_LOCAL:
    p_pcb_seat = tft_pl.multVec(FreeCAD.Vector(hx, hy, 6.55))
    # Boss cylinder extending up into lid roof:
    boss_cyl = Part.makeCylinder(2.60, 6.0, p_pcb_seat, v_tft_roof_up)
    # Alignment pin entering into PCB hole:
    # Straight height 1.80mm (matching 1.62mm PCB) + 0.60mm conical guide tip (total 2.40mm)
    pin_cyl = Part.makeCylinder(1.40, 1.80, p_pcb_seat, v_tft_pcb_down)
    pin_tip = Part.makeCone(1.40, 0.80, 0.60, p_pcb_seat + v_tft_pcb_down * 1.80, v_tft_pcb_down)
    pin_full = pin_cyl.fuse(pin_tip)
    post_solid = boss_cyl.fuse(pin_full)
    # M2 screw pilot hole (total length 5.40mm: 2.40mm through pin + 3.00mm into boss):
    pilot = Part.makeCylinder(0.95, 5.40, p_pcb_seat + v_tft_pcb_down * 2.40, v_tft_roof_up)
    tft_mount_posts.append(post_solid.cut(pilot))

all_tft_posts = tft_mount_posts[0].fuse(tft_mount_posts[1]).fuse(tft_mount_posts[2]).fuse(tft_mount_posts[3])

# Roof cutter leaves 0.5mm solid cosmetic exterior skin:
roof_cutter_tft = Part.makeBox(130.0, 110.0, 30.0, FreeCAD.Vector(-65.0, -55.0, 0.0))
roof_cutter_tft.Placement = FreeCAD.Placement(FreeCAD.Vector(0.0, 0.0, 31.0 - 0.5), rot_tilt)
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
roof_embed_cutter.Placement = FreeCAD.Placement(FreeCAD.Vector(0.0, 0.0, 31.0 - 0.5), rot_tilt)

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
p_bc.Rotation = rot_tilt
z_bc = 31.0 + (-3.0) * math.tan(math.radians(TILT_DEG))
p_bc.Base = FreeCAD.Vector(28.5, -3.0, z_bc)
btn_carrier.Placement = p_bc

# Trim carrier at Z_SPLIT so it remains perfectly flush with the parting line (never pokes into shell):
trim_carrier_split = Part.makeBox(200.0, 200.0, 10.0, FreeCAD.Vector(-100.0, -100.0, Z_SPLIT - 10.0))
btn_carrier = btn_carrier.cut(trim_carrier_split)

lid_body = lid_body.fuse(btn_carrier)

btn_caps = []
btn_origins = []
for btn_obj, pt in [(btn_up, pt_up), (btn_sel, pt_sel), (btn_down, pt_down)]:
    bdir = rot_tilt.multVec(FreeCAD.Vector(0, 0, 1))
    bz_top = 31.0 + pt.y * math.tan(math.radians(TILT_DEG))
    
    p_sw = FreeCAD.Placement()
    p_sw.Rotation = rot_tilt
    p_sw.Base = FreeCAD.Vector(pt.x, pt.y, bz_top)
    
    # Exact physical switch plunger location in global and local frames:
    pl_glob = btn_obj.Placement.multVec(FreeCAD.Vector(3.0, 3.0, 9.6))
    pl_local = p_sw.inverse().multVec(pl_glob)
    
    # Top face origin perfectly concentric with switch plunger:
    b_origin = pl_glob - bdir * pl_local.z
    btn_origins.append(b_origin)
    
    # 1. Lid Through-Hole (Dia 4.0mm, clean perpendicular cylinder matching original printed lid)
    bhole = Part.makeCylinder(2.00, 4.0, b_origin - bdir * 2.0, bdir)
    lid_body = lid_body.cut(bhole)

    # 2. Precision Switch Chamber with Solid Corner Support:
    # Lower square pocket for 6.0 x 6.0mm switch body (Z = -12.0 to -8.45):
    sw_pocket = Part.makeBox(6.25, 6.25, 3.55, FreeCAD.Vector(-3.125, pl_local.y - 3.125, -12.0))
    pin_channels = Part.makeBox(8.0, 6.25, 1.5, FreeCAD.Vector(-4.0, pl_local.y - 3.125, -12.0))
    sw_cavity = sw_pocket.fuse(pin_channels)
    sw_cavity.Placement = p_sw

    # Upper cylindrical guide bore for button cap (Dia 6.20mm / Radius 3.10mm, Z = -8.45 to -2.00):
    # Fills the 4 corner gaps with 100% solid enclosure material down to the switch corner rivets!
    # Provides 0.20mm smooth clearance around the Dia 5.80mm flange and 0.40mm around Dia 5.40mm body.
    cap_bore = Part.makeCylinder(3.10, 6.45, b_origin - bdir * 8.45, bdir)
    cavity = sw_cavity.fuse(cap_bore)
    
    # Solid, clean walls around all 4 sides of each button box:
    lid_body = lid_body.cut(cavity)
    
    # Guarantee zero physical collision with switch model:
    lid_body = lid_body.cut(btn_obj.Shape)

    # 3. Precision Ergonomic Reinforced Button Cap (+1.5mm Top Height, 100% Solid Stem Core, Drop-in Fit for Printed Lid):
    # Stem: Dia 3.50mm (Radius 1.75mm) solid cylinder with 0.25mm smooth radial glide clearance in Dia 4.0mm hole
    # Extended by +1.50mm for superior tactile reach and thumb ergonomics (Z = -2.00 to +2.30, length 4.30mm)
    cap_top = Part.makeCylinder(1.75, 4.30, b_origin - bdir * 2.0, bdir)
    cap_top_chamfer = Part.makeCone(1.75, 1.35, 0.40, b_origin + bdir * 2.30, bdir)
    # Structural Neck Fillet (R=1.95 at Z=-2.00 to R=1.75 at Z=-1.75, length 0.25mm):
    # Eliminates 90-deg notch stress riser, perfectly fits inside already-printed Dia 4.0mm hole with 0.05mm clearance!
    neck_fillet = Part.makeCone(1.95, 1.75, 0.25, b_origin - bdir * 2.00, bdir)
    # Retaining Flange Base: Dia 5.80mm (Radius 2.90mm), thickness 1.0mm (Z = -2.00 to -3.00)
    # Trapped inside by the Dia 4.0mm hole (0.90mm retention lip, CANNOT fall out through lid!)
    cap_base = Part.makeCylinder(2.90, 1.0, b_origin - bdir * 3.0, bdir)
    # Guide Sleeve Body: Dia 5.40mm (Radius 2.70mm), extending from -3.00mm down to -5.40mm (length 2.40mm)
    # Extra lower 3.00mm sleeve (Z = -5.40 to -8.40) cut off to provide 3.025mm travel clearance above switch body!
    # Slides smoothly inside the Dia 6.20mm guide bore with zero wobble and full actuation stroke.
    cap_sleeve = Part.makeCylinder(2.70, 2.40, b_origin - bdir * 5.40, bdir)
    # Conical Plunger Socket: Depth 3.30mm (from -5.40mm up to -2.10mm, ceiling entirely inside flange):
    # Plunger tip is at -2.125mm; above -2.10mm, the entire stem and neck are 100% SOLID PLASTIC!
    plunger_socket = Part.makeCone(1.95, 1.55, 3.30, b_origin - bdir * 5.40, bdir)
    
    single_cap = cap_top.fuse(cap_top_chamfer).fuse(neck_fillet).fuse(cap_base).fuse(cap_sleeve).cut(plunger_socket)
    btn_caps.append(single_cap)

# D. EXACT AVIONICS ACCENT DESIGN - Matching user reference photo (media_1789732270260.jpg):
# 1. SCREEN BEZEL: Clean high-contrast rectangular bezel (zero notches or dots at display side)
# 2. ABOVE SCREEN: Horizontal bars flanking a center diamond (─── ◆ ─── targeting reticle)
# 3. BESIDE BUTTONS: Proper UP (▲), SELECT (●), DOWN (▼) icons exactly level with buttons
# 4. BOTTOM-RIGHT: 5 parallel diagonal hash marks angled at 45 deg

# === ACCENT 1: Horizontal dashes + center diamond ABOVE the screen ===
Y_above = Y_SCREEN + 16.1 + 3.5   # 3.5mm above bezel top
p_top_accent = FreeCAD.Placement()
p_top_accent.Rotation = rot_tilt
z_top_acc = 31.0 + Y_above * math.tan(math.radians(TILT_DEG))
p_top_accent.Base = FreeCAD.Vector(X_SCREEN, Y_above, z_top_acc)

# Left dash: from X=-16.0 to X=-2.8, height 1.1mm, depth 0.4mm
dash_left = Part.makeBox(13.2, 1.1, 0.40, FreeCAD.Vector(-16.0, -0.55, -0.40))
dash_left.Placement = p_top_accent
lid_body = lid_body.cut(dash_left)

# Right dash: from X=+2.8 to X=+16.0, height 1.1mm, depth 0.4mm
dash_right = Part.makeBox(13.2, 1.1, 0.40, FreeCAD.Vector(2.8, -0.55, -0.40))
dash_right.Placement = p_top_accent
lid_body = lid_body.cut(dash_right)

# Center diamond (◆): 2.2 x 2.2mm rotated 45 deg, depth 0.4mm
diamond_center = Part.makeBox(2.2, 2.2, 0.40, FreeCAD.Vector(-1.1, -1.1, -0.40))
diamond_center.rotate(FreeCAD.Vector(0, 0, 0), FreeCAD.Vector(0, 0, 1), 45)
diamond_center.Placement = p_top_accent.multiply(diamond_center.Placement)
lid_body = lid_body.cut(diamond_center)

# === ACCENT 2: Proper Avionics Button Indicator Icons (▲ UP, ● SELECT, ▼ DOWN) ===
# Positioned EXACTLY beside each button hole (Zero vertical offset, identical faceplate Y and Z)
# Shifted in -X by DX = 5.8mm on the faceplate surface
depth_icon = 0.40
w_tri, h_tri = 3.2, 2.8

# 1. UP Button Icon (▲)
p_up_icon = FreeCAD.Placement()
p_up_icon.Rotation = rot_tilt
p_up_icon.Base = btn_origins[0] + FreeCAD.Vector(-5.8, 0.0, 0.0)

pts_up = [
    FreeCAD.Vector(0.0,       h_tri/2, -depth_icon),
    FreeCAD.Vector(w_tri/2,  -h_tri/2, -depth_icon),
    FreeCAD.Vector(-w_tri/2, -h_tri/2, -depth_icon),
    FreeCAD.Vector(0.0,       h_tri/2, -depth_icon)
]
icon_up = Part.Face(Part.makePolygon(pts_up)).extrude(FreeCAD.Vector(0, 0, depth_icon))
icon_up.Placement = p_up_icon
lid_body = lid_body.cut(icon_up)

# 2. SELECT Button Icon (● Bold Circular Indicator, Dia 3.0mm)
p_sel_icon = FreeCAD.Placement()
p_sel_icon.Rotation = rot_tilt
p_sel_icon.Base = btn_origins[1] + FreeCAD.Vector(-5.8, 0.0, 0.0)

icon_sel = Part.makeCylinder(1.5, depth_icon, FreeCAD.Vector(0, 0, -depth_icon), FreeCAD.Vector(0, 0, 1))
icon_sel.Placement = p_sel_icon
lid_body = lid_body.cut(icon_sel)

# 3. DOWN Button Icon (▼)
p_down_icon = FreeCAD.Placement()
p_down_icon.Rotation = rot_tilt
p_down_icon.Base = btn_origins[2] + FreeCAD.Vector(-5.8, 0.0, 0.0)

pts_down = [
    FreeCAD.Vector(0.0,      -h_tri/2, -depth_icon),
    FreeCAD.Vector(w_tri/2,    h_tri/2, -depth_icon),
    FreeCAD.Vector(-w_tri/2,   h_tri/2, -depth_icon),
    FreeCAD.Vector(0.0,      -h_tri/2, -depth_icon)
]
icon_down = Part.Face(Part.makePolygon(pts_down)).extrude(FreeCAD.Vector(0, 0, depth_icon))
icon_down.Placement = p_down_icon
lid_body = lid_body.cut(icon_down)

button_icons = [icon_up, icon_sel, icon_down]

# === ACCENT 3: Diagonal hash marks at BOTTOM-RIGHT corner ===
# 5 parallel slashes, 45-deg angle, positioned in the lower-right field below screen and buttons
# Moved downward and spaced out cleanly as requested:
HASH_Y_BASE = -26.5    # moved down side
HASH_X_BASE = 9.0      # left edge of hash area  
HASH_N = 5             # number of hash marks
HASH_SEP = 3.2         # increased space between sticks (gap = 2.1mm)
HASH_W = 1.1           # width of each slash bar
HASH_L = 7.0           # length of each slash bar
HASH_ANGLE = 45.0      # angle in degrees from horizontal

hash_shapes = []
for i in range(HASH_N):
    hx = HASH_X_BASE + i * HASH_SEP
    hy = HASH_Y_BASE
    z_h = 31.0 + hy * math.tan(math.radians(TILT_DEG))
    p_h = FreeCAD.Placement()
    p_h.Rotation = rot_tilt.multiply(FreeCAD.Rotation(FreeCAD.Vector(0, 0, 1), HASH_ANGLE))
    p_h.Base = FreeCAD.Vector(hx, hy, z_h)
    slash = Part.makeBox(HASH_W, HASH_L, 0.40, FreeCAD.Vector(-HASH_W/2, 0.0, -0.40))
    slash.Placement = p_h
    hash_shapes.append(slash)
    lid_body = lid_body.cut(slash)

# Re-cut lid rebate to ensure button carrier has 100% zero interference at split line:
lid_body = lid_body.cut(lid_rebate)

lid_body = lid_body.removeSplitter()

# Clean Aerospace Accents: Screen Bezel + Header Reticle + Proper Button Icons + Diagonal Hash marks
accent_solids = [bezel_accent, dash_left, diamond_center, dash_right] + button_icons + hash_shapes
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
    print("\n>>> ALL 20 INTERFERENCE CHECKS PASSED: 0.000000 mmÂ³! <<<")
else:
    print("\n>>> WARNING: SOME CHECKS FAILED! <<<")

# If running with FreeCADGui, ensure all models are visible with correct avionics multicolor styling:
try:
    import FreeCADGui
    doc = FreeCAD.activeDocument()
    # Show ALL components (enclosure + internal electronics hardware):
    all_visible = [
        "Shell", "Lid", "Lid_Accents", "Button_Caps",
        "esp32_Wroom_30pins_C_Type", "AZ_Delivery_TFT_1_8_SPI",
        "Button_UP", "Button_SELECT", "Button_DOWN"
    ]
    for name in all_visible:
        obj = doc.getObject(name)
        if obj and hasattr(obj, "ViewObject") and obj.ViewObject:
            obj.ViewObject.Visibility = True
        if obj and hasattr(obj, "Group"):
            for child in obj.Group:
                if hasattr(child, "ViewObject") and child.ViewObject:
                    child.ViewObject.Visibility = True

    # Avionics Cockpit Palette (Black Base with High-Contrast Cockpit Red Accents):
    if hasattr(feat_shell, "ViewObject") and feat_shell.ViewObject:
        feat_shell.ViewObject.ShapeColor = (0.13, 0.14, 0.16)   # Deep Matte Black
    if hasattr(feat_lid, "ViewObject") and feat_lid.ViewObject:
        feat_lid.ViewObject.ShapeColor = (0.13, 0.14, 0.16)     # Deep Matte Black
        feat_lid.ViewObject.Transparency = 55                   # Semi-transparent to inspect internal fit
    if hasattr(feat_accents, "ViewObject") and feat_accents.ViewObject:
        feat_accents.ViewObject.ShapeColor = (0.90, 0.10, 0.10) # Vibrant Cockpit Red
    if hasattr(feat_caps, "ViewObject") and feat_caps.ViewObject:
        feat_caps.ViewObject.ShapeColor = (0.90, 0.10, 0.10)    # Vibrant Cockpit Red
        feat_caps.ViewObject.Transparency = 75                  # Transparent view to inspect button switch inside
    FreeCADGui.activeView().viewDimetric()
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
# Total height = 2.40 (sleeve) + 1.00 (flange) + 4.30 (stem) + 0.40 (top chamfer) = 8.10 mm
sleeve_bed = Part.makeCylinder(2.70, 2.40, FreeCAD.Vector(0, 0, 0), FreeCAD.Vector(0, 0, 1))
flange_bed = Part.makeCylinder(2.90, 1.00, FreeCAD.Vector(0, 0, 2.40), FreeCAD.Vector(0, 0, 1))
fillet_bed = Part.makeCone(1.95, 1.75, 0.25, FreeCAD.Vector(0, 0, 3.40), FreeCAD.Vector(0, 0, 1))
stem_bed = Part.makeCylinder(1.75, 4.30, FreeCAD.Vector(0, 0, 3.40), FreeCAD.Vector(0, 0, 1))
chamfer_bed = Part.makeCone(1.75, 1.35, 0.40, FreeCAD.Vector(0, 0, 7.70), FreeCAD.Vector(0, 0, 1))
socket_bed = Part.makeCone(1.95, 1.55, 3.30, FreeCAD.Vector(0, 0, 0), FreeCAD.Vector(0, 0, 1))
cap_bed_template = sleeve_bed.fuse(flange_bed).fuse(fillet_bed).fuse(stem_bed).fuse(chamfer_bed).cut(socket_bed)

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
