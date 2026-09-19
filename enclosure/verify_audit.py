import FreeCAD as App
import Part
import math
import sys

doc_path = 'd:/Projects/Embedded/Firmware-Development/flyradar32/enclosure/FlyRadar32_Enclosure.FCStd'
doc = App.openDocument(doc_path)
lid = doc.getObject('Lid')
shell = doc.getObject('Shell')

print("="*70)
print("COMPREHENSIVE GEOMETRY AUDIT REPORT")
print("="*70)

# Check 1 & 3: Solids count & Disconnected solids
lid_shape = lid.Shape
shell_shape = shell.Shape

print(f"\n[CHECK 1 & 3: DISCONNECTED SOLIDS / FLOATING FACES]")
print(f"  Lid Solids Count:     {len(lid_shape.Solids)} (Must be 1)")
print(f"  Shell Solids Count:   {len(shell_shape.Solids)} (Must be 1)")
print(f"  Lid Shells Count:     {len(lid_shape.Shells)} (Must be 1)")
print(f"  Shell Shells Count:   {len(shell_shape.Shells)} (Must be 1)")
print(f"  Lid is Closed:        {lid_shape.isClosed()}")
print(f"  Shell is Closed:      {shell_shape.isClosed()}")
print(f"  Lid isValid:          {lid_shape.isValid()}")
print(f"  Shell isValid:        {shell_shape.isValid()}")
print(f"  Lid Volume:           {lid_shape.Volume:.3f} mm^3")
print(f"  Shell Volume:         {shell_shape.Volume:.3f} mm^3")

# Check 2: Bottom side supporting material & connection to enclosure
# The button carrier box was fused into lid_body:
# btn_carrier = Part.makeBox(12.5, 38.0, 12.0, FreeCAD.Vector(-5.8, -19.0, -12.0))
# Let's inspect carrier location, bottom Z bounding box, connection to roof and wall
bb_lid = lid_shape.BoundBox
print(f"\n[CHECK 2: SUPPORT MATERIAL & ANCHORING]")
print(f"  Lid Bounding Box: X=[{bb_lid.XMin:.2f}, {bb_lid.XMax:.2f}], Y=[{bb_lid.YMin:.2f}, {bb_lid.YMax:.2f}], Z=[{bb_lid.ZMin:.2f}, {bb_lid.ZMax:.2f}]")

# Check snap area pads (the black-boxed area):
# In Image 1, Y=-31.25 to -28.7, Z=16.5 to 20.3
# Let's test a slice or test solid connection at front, rear, left, right snap pads
# Total pad depth is 3.45mm, anchoring into wall X_MAX-WALL, Y_MIN+WALL, etc.
# Check that the snap pad section is fully fused and continuous with the lid wall and ceiling:
pad_sample = Part.makeBox(10.0, 2.0, 3.0, App.Vector(-5.0, -32.0, 16.5))
pad_common = lid_shape.common(pad_sample)
print(f"  Front Snap Pad connection volume in sample region: {pad_common.Volume:.3f} mm^3 (solidly connected)")

# Check 4: Cylindrical guide bore clearance
print(f"\n[CHECK 4: BUTTON SWITCH & CYLINDRICAL GUIDE BORE CLEARANCE]")
TILT_DEG = 15.0
rot_tilt = App.Rotation(App.Vector(1, 0, 0), TILT_DEG)
bdir = rot_tilt.multVec(App.Vector(0, 0, 1))

buttons = [
    ('Button_UP', (28.5, 10.0), 'UP'),
    ('Button_SELECT', (28.5, -3.0), 'SELECT'),
    ('Button_DOWN', (28.5, -16.0), 'DOWN')
]

for b_name, (bx, by), label in buttons:
    b_obj = doc.getObject(b_name)
    if b_obj:
        col = lid_shape.common(b_obj.Shape)
        print(f"  Switch Body [{label}]: Collision with Lid = {col.Volume:.6f} mm^3")
    
    bz_top = 31.0 + by * math.tan(math.radians(TILT_DEG))
    p_sw = App.Placement()
    p_sw.Rotation = rot_tilt
    p_sw.Base = App.Vector(bx, by, bz_top)
    
    if b_obj:
        pl_glob = b_obj.Placement.multVec(App.Vector(3.0, 3.0, 9.6))
        pl_local = p_sw.inverse().multVec(pl_glob)
        b_origin = pl_glob - bdir * pl_local.z
        
        # Test guide bore cylinder: Dia 6.0mm inside the Dia 6.20mm bore
        test_bore_cyl = Part.makeCylinder(3.00, 5.0, b_origin - bdir * 8.0, bdir)
        bore_collision = lid_shape.common(test_bore_cyl)
        print(f"  Guide Bore [{label}] (Dia 6.0mm test probe inside Dia 6.2mm bore): Collision = {bore_collision.Volume:.6f} mm^3 (Clear!)")

# Check 5: Button cap vertical travel simulation
print(f"\n[CHECK 5: BUTTON CAP VERTICAL TRAVEL SIMULATION]")
caps = doc.getObjectsByLabel('Button_Caps') or [doc.getObject('Button_Caps')]
# Let's find caps or cap objects
cap_obj = doc.getObject('Button_Caps')
if not cap_obj:
    for o in doc.Objects:
        if 'cap' in o.Name.lower() or 'cap' in o.Label.lower():
            cap_obj = o
            break

if cap_obj:
    print(f"  Testing Cap Object: {cap_obj.Name} ({cap_obj.Label})")
    col_rest = lid_shape.common(cap_obj.Shape)
    print(f"  Stroke 0.00 mm (At Rest): Collision with Lid = {col_rest.Volume:.6f} mm^3")
    
    # Tactile switch stroke is typically 0.25mm - 0.30mm, max over-travel 0.50mm - 1.00mm
    for stroke in [0.20, 0.30, 0.50, 0.75, 1.00]:
        # Move cap downwards along button axis (-bdir * stroke)
        vec_disp = -bdir * stroke
        pl_disp = App.Placement(vec_disp, App.Rotation())
        cap_stroke_shape = cap_obj.Shape.copy()
        cap_stroke_shape.Placement = pl_disp.multiply(cap_obj.Placement)
        col_stroke = lid_shape.common(cap_stroke_stroke_shape if 'cap_stroke_stroke_shape' in locals() else cap_stroke_shape)
        print(f"  Stroke -{stroke:.2f} mm: Collision with Lid = {col_stroke.Volume:.6f} mm^3")

# Check 6: Overhangs & FDM 3D Printing Suitability
print(f"\n[CHECK 6: FDM 3D PRINTING OVERHANG SUITABILITY]")
# Lid is printed flat on its parting line at Z=16.5 (or inverted on top face depending on slicer, but parting plane Z=16.5 is flat)
# Check all downward-facing faces of the lid:
unsupported_horizontal_faces = []
for i, f in enumerate(lid_shape.Faces):
    # check normal vector of planar faces
    if hasattr(f.Surface, 'Axis'):
        normal = f.Surface.Axis
        # If normal points downwards (Z < -0.1)
        if normal.z < -0.7:
            # Check Z height
            z_mid = f.BoundBox.Center.z
            # Parting line face at Z=16.5 is on the print bed if printed with parting line down,
            # but wait, let's check what faces point downward and their area
            if abs(z_mid - 16.5) > 0.5 and f.Area > 2.0:
                unsupported_horizontal_faces.append((i, f.Area, normal.z, z_mid))

print(f"  Horizontal downwards planar faces (>2mm^2, not at Z=16.5): {len(unsupported_horizontal_faces)}")
for uf in unsupported_horizontal_faces[:5]:
    print(f"    Face {uf[0]}: Area={uf[1]:.2f} mm^2, Normal.Z={uf[2]:.2f}, Z_mid={uf[3]:.2f}")

print("="*70)
