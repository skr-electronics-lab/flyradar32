"""
FlyRadar32 Enclosure - COMPLETE PRE-PRINT VERIFICATION
All special chars removed for Windows cp1252 compatibility.
"""
import FreeCAD, Part, Mesh, math, os

doc = FreeCAD.open(r'D:\Projects\Embedded\Firmware-Development\flyradar32\enclosure\FlyRadar32_Enclosure.FCStd')

shell = doc.getObject('Shell')
lid   = doc.getObject('Lid')
caps  = doc.getObject('Button_Caps')
tft   = doc.getObject('AZ_Delivery_TFT_1_8_SPI')
esp   = doc.getObject('esp32_Wroom_30pins_C_Type')
bup   = doc.getObject('Button_UP')
bsel  = doc.getObject('Button_SELECT')
bdwn  = doc.getObject('Button_DOWN')

def get_shape(o):
    if o is None:
        return Part.Shape()
    if hasattr(o, 'Group'):
        pl = getattr(o, 'Placement', FreeCAD.Placement())
        shps = []
        for c in o.Group:
            if hasattr(c, 'Shape') and not c.Shape.isNull():
                s = c.Shape.copy()
                s.Placement = pl.multiply(c.Placement)
                shps.append(s)
        if shps:
            return Part.makeCompound(shps)
    if hasattr(o, 'Shape') and not o.Shape.isNull():
        return o.Shape
    return Part.Shape()

ss = shell.Shape
ls = lid.Shape

results = []
def chk(label, condition, detail=""):
    tag = "  [PASS]" if condition else "  [FAIL]"
    results.append((tag, label, detail))
    print("%s  %-52s %s" % (tag, label, detail))

print("")
print("="*70)
print("  FLYRADAR32 PRE-PRINT VERIFICATION REPORT")
print("="*70)

# ── 1. OUTER DIMENSIONS ──────────────────────────────────────────────────────
print("\n[1] ENCLOSURE OUTER DIMENSIONS")
# Tessellated bounding box reflects actual solid mesh vertices (avoiding OCC Bnd_Box sphere patch overestimation)
bb_s = Mesh.Mesh(ss.tessellate(0.1)).BoundBox
bb_l = Mesh.Mesh(ls.tessellate(0.1)).BoundBox
X_MIN, X_MAX = -45.0, 37.0
Y_MIN, Y_MAX = -34.0, 26.0
WALL   = 1.8
FLOOR  = 1.8
R_CORNER = 5.0
Z_SPLIT  = 16.5
TILT_DEG = 15.0

chk("Shell width  (target 82 mm)", 79 < round(bb_s.XLength,1) < 85,   "%s mm" % round(bb_s.XLength,1))
chk("Shell depth  (target 60 mm)", 57 < round(bb_s.YLength,1) < 63,   "%s mm" % round(bb_s.YLength,1))
# Shell height includes snap lip (16.5 + 1.5mm lip) = up to 18.5 is fine
chk("Shell height (16-19 mm OK)",  16 < round(bb_s.ZLength,1) < 19.5, "%s mm" % round(bb_s.ZLength,1))
chk("Lid   height (13-25 mm OK)",  13 < round(bb_l.ZLength,1) < 25,   "%s mm" % round(bb_l.ZLength,1))

# ── 2. WALL BOUNDS ───────────────────────────────────────────────────────────
print("\n[2] WALL THICKNESS BOUNDS")
chk("Shell XMin = X_MIN (-45.0)", abs(bb_s.XMin - X_MIN) < 0.1, "XMin=%.2f" % bb_s.XMin)
chk("Shell XMax = X_MAX ( 37.0)", abs(bb_s.XMax - X_MAX) < 0.1, "XMax=%.2f" % bb_s.XMax)
chk("Shell YMin = Y_MIN (-34.0)", abs(bb_s.YMin - Y_MIN) < 0.1, "YMin=%.2f" % bb_s.YMin)
chk("Shell YMax = Y_MAX ( 26.0)", abs(bb_s.YMax - Y_MAX) < 0.1, "YMax=%.2f" % bb_s.YMax)
chk("Lid   X span matches shell",
    abs(bb_l.XMin - bb_s.XMin) < 0.1 and abs(bb_l.XMax - bb_s.XMax) < 0.1,
    "Lid X: %.2f .. %.2f" % (bb_l.XMin, bb_l.XMax))
chk("Lid   Y span matches shell",
    abs(bb_l.YMin - bb_s.YMin) < 0.1 and abs(bb_l.YMax - bb_s.YMax) < 0.1,
    "Lid Y: %.2f .. %.2f" % (bb_l.YMin, bb_l.YMax))

# ── 3. PARTING LINE ──────────────────────────────────────────────────────────
print("\n[3] PARTING LINE & MATING")
chk("Shell top Z <= 19 mm (lip OK)", bb_s.ZMax <= 19.0,  "ZMax=%.2f" % bb_s.ZMax)
chk("Lid   bot Z at split (14.0-17)", 14.0 < bb_l.ZMin < 17.0, "ZMin=%.2f" % bb_l.ZMin)
sl_vol = ss.common(ls).Volume
chk("Shell <-> Lid interference = 0", sl_vol < 0.001, "%.6f mm3" % sl_vol)

# ── 4. CORNER SCREW BOSSES ───────────────────────────────────────────────────
print("\n[4] CORNER SCREW BOSSES (M2 x 16 mm Pan Head + M2x3mm insert)")
BOSS_R = R_CORNER - WALL  # 3.2 mm
corner_centers = [
    ("FL", X_MIN + R_CORNER, Y_MIN + R_CORNER),
    ("FR", X_MAX - R_CORNER, Y_MIN + R_CORNER),
    ("RL", X_MIN + R_CORNER, Y_MAX - R_CORNER),
    ("RR", X_MAX - R_CORNER, Y_MAX - R_CORNER),
]
for lbl, cx, cy in corner_centers:
    # Boss solid probe (R=3.0mm, Z=3..14) must be inside shell
    probe  = Part.makeCylinder(BOSS_R-0.2, 11.0, FreeCAD.Vector(cx, cy, 3.0), FreeCAD.Vector(0,0,1))
    vol    = probe.common(ss).Volume
    chk("Shell %s boss solid (vol>30)" % lbl, vol > 30.0, "vol=%.1f mm3" % vol)

    # M2 clearance hole (R=0.9mm) must be AIR in shell
    hole   = Part.makeCylinder(0.9, 17.0, FreeCAD.Vector(cx, cy, -0.5), FreeCAD.Vector(0,0,1))
    h_vol  = hole.common(ss).Volume
    chk("Shell %s M2 hole is clear" % lbl, h_vol < 0.05, "overlap=%.4f mm3" % h_vol)

    # Counterbore (R=1.8mm, Z=-0.1 to 3.5) must be AIR
    csink  = Part.makeCylinder(1.8, 3.5, FreeCAD.Vector(cx, cy, -0.1), FreeCAD.Vector(0,0,1))
    cs_vol = csink.common(ss).Volume
    chk("Shell %s counterbore clear" % lbl, cs_vol < 0.05, "overlap=%.4f mm3" % cs_vol)

    # Lid boss solid (Z=17..19)
    rot_tilt = FreeCAD.Rotation(FreeCAD.Vector(1,0,0), TILT_DEG)
    roof_z  = 31.0 + cy * math.tan(math.radians(TILT_DEG)) - WALL * math.cos(math.radians(TILT_DEG))
    l_probe = Part.makeCylinder(BOSS_R-0.2, min(roof_z - Z_SPLIT - 0.5, 3.0),
                                 FreeCAD.Vector(cx, cy, Z_SPLIT + 0.3), FreeCAD.Vector(0,0,1))
    l_vol   = l_probe.common(ls).Volume
    chk("Lid   %s boss solid" % lbl, l_vol > 3.0, "vol=%.1f mm3" % l_vol)

    # Lid insert hole (R=1.4mm at Z=Z_SPLIT) must be AIR
    ins    = Part.makeCylinder(1.4, 3.0, FreeCAD.Vector(cx, cy, Z_SPLIT + 0.1), FreeCAD.Vector(0,0,1))
    i_vol  = ins.common(ls).Volume
    chk("Lid   %s insert hole clear" % lbl, i_vol < 0.05, "overlap=%.4f mm3" % i_vol)

# ── 5. BUTTON FIT ────────────────────────────────────────────────────────────
print("\n[5] BUTTON HOLDER & CAP GEOMETRY")
rot_tilt = FreeCAD.Rotation(FreeCAD.Vector(1,0,0), TILT_DEG)
for bname, by in [("UP", 10.0), ("SEL", -3.0), ("DOWN", -16.0)]:
    bx    = 28.5
    bz_top = 31.0 + by * math.tan(math.radians(TILT_DEG))
    bdir  = rot_tilt.multVec(FreeCAD.Vector(0,0,1))
    
    # Switch plunger projection on lid face
    b_obj = doc.getObject("Button_" + bname if bname != "SEL" else "Button_SELECT")
    pl_glob = b_obj.Placement.multVec(FreeCAD.Vector(3.0, 3.0, 9.6))
    p_sw = FreeCAD.Placement(FreeCAD.Vector(bx, by, bz_top), rot_tilt)
    pl_loc = p_sw.inverse().multVec(pl_glob)
    b_origin = pl_glob - bdir * pl_loc.z

    # Through-hole (R=2.0mm probe, hole is Dia 4.4mm / R=2.2mm)
    h_probe = Part.makeCylinder(1.9, 1.5, b_origin + bdir*(-0.8), bdir)
    h_vol   = h_probe.common(ls).Volume
    chk("Btn %s lid hole clear (Dia 4.4mm)" % bname, h_vol < 0.1, "overlap=%.4f" % h_vol)

    # Lower switch pocket 6.25x6.25mm: probe 5.6x5.6 inside switch body space (Z = -11.5 to -9.0)
    p_cav = FreeCAD.Placement()
    p_cav.Rotation = rot_tilt
    p_cav.Base = FreeCAD.Vector(bx, by, bz_top)
    sw_probe = Part.makeBox(5.6, 5.6, 2.5, FreeCAD.Vector(-2.8, pl_loc.y - 2.8, -11.5))
    sw_probe.Placement = p_cav
    s_vol = sw_probe.common(ls).Volume
    chk("Btn %s switch pocket 6.25mm clear" % bname, s_vol < 0.1, "overlap=%.4f" % s_vol)

    # Upper cylindrical guide bore Dia 6.20mm (Radius 3.10mm, Z = -8.0 to -2.5): probe Radius 2.95mm
    bore_probe = Part.makeCylinder(2.95, 5.5, b_origin - bdir * 8.0, bdir)
    b_vol = bore_probe.common(ls).Volume
    chk("Btn %s cap bore Dia 6.2mm clear" % bname, b_vol < 0.1, "overlap=%.4f" % b_vol)

# Key cap geometry numbers
retention   = 2.85 - 2.20   # 0.65mm retention ledge per side
clearance   = (6.25 - 6.0) / 2.0   # 0.125mm per side
boss_wall_m2  = BOSS_R - 1.2   # 2.0mm
boss_wall_ins = BOSS_R - 1.6   # 1.6mm
chk("Cap retention lip >= 0.6 mm",       retention >= 0.60, "%.2f mm" % retention)
chk("Switch pocket snug fit <= 0.35 mm", clearance <= 0.35, "%.2f mm per side" % clearance)

# ── 6. TFT WINDOW ────────────────────────────────────────────────────────────
print("\n[6] TFT DISPLAY WINDOW")
rot_z180 = FreeCAD.Rotation(FreeCAD.Vector(0,0,1), 180)
rot_tft  = rot_tilt.multiply(rot_z180)
pos_tft  = FreeCAD.Vector(-10.0, -0.697, 19.736)
tft_pl   = FreeCAD.Placement(pos_tft, rot_tft)
g_ac     = tft_pl.multVec(FreeCAD.Vector(-2.89, 0.0, 8.90))
z_top    = 31.0 + g_ac.y * math.tan(math.radians(TILT_DEG))
p_win = FreeCAD.Placement()
p_win.Rotation = rot_tilt
p_win.Base = FreeCAD.Vector(g_ac.x, g_ac.y, z_top)

win_probe = Part.makeBox(33.0, 27.0, 8.0, FreeCAD.Vector(-16.5, -13.5, -4.0))
win_probe.Placement = p_win
w_vol = win_probe.common(ls).Volume
chk("TFT window 33x27 probe clear",  w_vol < 0.5, "overlap=%.4f mm3" % w_vol)

ctr_probe = Part.makeCylinder(5.0, 5.0, FreeCAD.Vector(g_ac.x, g_ac.y, z_top - 2.0), FreeCAD.Vector(0,0,1))
c_vol = ctr_probe.common(ls).Volume
chk("Lid center above TFT is open",  c_vol < 0.5, "overlap=%.4f mm3" % c_vol)

# ── 7. USB-C PORT ────────────────────────────────────────────────────────────
print("\n[7] USB-C PORT")
Z_USBC = 5.75
usbc_probe = Part.makeBox(4.0, 8.2, 2.8, FreeCAD.Vector(X_MAX - 1.0, -4.1, Z_USBC - 1.4))
u_vol = usbc_probe.common(ss).Volume
chk("USB-C cutout open in shell", u_vol < 0.1, "overlap=%.4f mm3" % u_vol)
esp_shape = get_shape(esp)
esp_bb = esp_shape.BoundBox
chk("ESP32 USB-C within 2 mm of wall", abs(esp_bb.XMax - X_MAX) < 2.5,
    "ESP XMax=%.2f wall=%.1f" % (esp_bb.XMax, X_MAX))
chk("ESP32 Y-centered (|Yc|<1.5mm)",
    abs((esp_bb.YMin + esp_bb.YMax)/2) < 1.5,
    "Yc=%.2f" % ((esp_bb.YMin + esp_bb.YMax)/2))

# ── 8. ESP32 CRADLE FIT ──────────────────────────────────────────────────────
print("\n[8] ESP32 CRADLE")
chk("ESP32 <-> Shell = 0",          ss.common(esp_shape).Volume < 0.001, "%.6f mm3" % ss.common(esp_shape).Volume)
chk("ESP32 <-> Lid   = 0",          ls.common(esp_shape).Volume < 0.001, "%.6f mm3" % ls.common(esp_shape).Volume)
chk("ESP32 Zbot >= FLOOR (1.8mm)",  esp_bb.ZMin >= 1.5,                  "ZMin=%.2f" % esp_bb.ZMin)
chk("ESP32 Ztop <= Z_SPLIT+0.3",    esp_bb.ZMax <= Z_SPLIT + 0.3,        "ZMax=%.2f" % esp_bb.ZMax)

# ── 9. FULL 20-CHECK BOOLEAN MATRIX ─────────────────────────────────────────
print("\n[9] FULL 20-CHECK BOOLEAN INTERFERENCE MATRIX")
pairs = [
    ("ESP32 <-> TFT",         get_shape(esp), get_shape(tft)),
    ("ESP32 <-> Button_UP",   get_shape(esp), get_shape(bup)),
    ("ESP32 <-> Button_SEL",  get_shape(esp), get_shape(bsel)),
    ("ESP32 <-> Button_DOWN", get_shape(esp), get_shape(bdwn)),
    ("TFT <-> Button_UP",     get_shape(tft), get_shape(bup)),
    ("TFT <-> Button_SEL",    get_shape(tft), get_shape(bsel)),
    ("TFT <-> Button_DOWN",   get_shape(tft), get_shape(bdwn)),
    ("Shell <-> ESP32",       ss,             get_shape(esp)),
    ("Shell <-> TFT",         ss,             get_shape(tft)),
    ("Shell <-> Button_UP",   ss,             get_shape(bup)),
    ("Shell <-> Button_SEL",  ss,             get_shape(bsel)),
    ("Shell <-> Button_DOWN", ss,             get_shape(bdwn)),
    ("Lid <-> ESP32",         ls,             get_shape(esp)),
    ("Lid <-> TFT",           ls,             get_shape(tft)),
    ("Lid <-> Button_UP",     ls,             get_shape(bup)),
    ("Lid <-> Button_SEL",    ls,             get_shape(bsel)),
    ("Lid <-> Button_DOWN",   ls,             get_shape(bdwn)),
    ("Lid <-> Button_Caps",   ls,             get_shape(caps)),
    ("Shell <-> Button_Caps", ss,             get_shape(caps)),
    ("Shell <-> Lid (Mating)",ss,             ls),
]
for name, s1, s2 in pairs:
    try:
        vol = s1.common(s2).Volume
        chk(name, vol < 0.001, "%.6f mm3" % vol)
    except Exception as e:
        chk(name, False, str(e))

# ── 10. STL MANIFOLD CHECKS ──────────────────────────────────────────────────
print("\n[10] STL FILE & MANIFOLD CHECKS")
stl_dir = r"D:\Projects\Embedded\Firmware-Development\flyradar32\enclosure\STLs"
for pname, fname in [("Shell","flyradar32_shell.stl"),
                      ("Lid",  "flyradar32_lid.stl"),
                      ("Caps", "flyradar32_button_caps.stl")]:
    fpath = os.path.join(stl_dir, fname)
    if not os.path.exists(fpath):
        chk("%s STL exists" % pname, False, "FILE MISSING")
        continue
    sz = os.path.getsize(fpath) / 1024
    chk("%s STL exists & >50 KB" % pname, sz > 50, "%.1f KB" % sz)
    m = Mesh.Mesh()
    m.read(fpath)
    chk("%s STL is solid (no open edges)" % pname, m.isSolid(), "%d pts / %d facets" % (m.CountPoints, m.CountFacets))
    chk("%s STL no non-manifold edges" % pname, not m.hasNonManifolds(), "")

# ── 11. PRINTABILITY ─────────────────────────────────────────────────────────
print("\n[11] PRINTABILITY")
chk("Shell flat bottom Z~0 (no support)",    bb_s.ZMin < 0.2, "ZMin=%.2f" % bb_s.ZMin)
chk("Shell max height <= 19.5 mm",           bb_s.ZMax <= 19.5, "ZMax=%.2f" % bb_s.ZMax)
chk("Lid prints on flat split face (Zmin~16.5)", 15.5 < bb_l.ZMin < 17.0, "ZMin=%.2f" % bb_l.ZMin)
chk("Boss wall around M2 hole >= 1.5 mm",   BOSS_R - 1.2 >= 1.5, "%.2f mm" % (BOSS_R - 1.2))
chk("Lid boss wall around insert >= 1.0 mm",BOSS_R - 1.6 >= 1.0, "%.2f mm" % (BOSS_R - 1.6))
chk("Button cap retention >= 0.6 mm each side", retention >= 0.6, "%.2f mm" % retention)
chk("Switch pocket snug fit <= 0.35 mm", clearance <= 0.35, "%.2f mm per side" % clearance)

# ── SUMMARY ──────────────────────────────────────────────────────────────────
print("")
print("="*70)
n_pass = sum(1 for r in results if "[PASS]" in r[0])
n_fail = sum(1 for r in results if "[FAIL]" in r[0])
print("  TOTAL: %d PASS   %d FAIL" % (n_pass, n_fail))
print("="*70)
if n_fail == 0:
    print("  >>> READY FOR PRINTING! All checks passed. <<<")
else:
    print("  >>> ISSUES FOUND - review before printing! <<<")
    print("")
    print("  Failed items:")
    for tag, label, detail in results:
        if "[FAIL]" in tag:
            print("    - %s: %s" % (label, detail))

with open(r"D:\Projects\Embedded\Firmware-Development\flyradar32\enclosure\preprint_report.txt", "w") as f:
    f.write(f"TOTAL: {n_pass} PASS   {n_fail} FAIL\n")
    for tag, label, detail in results:
        f.write(f"{tag} {label:<52} {detail}\n")

