"""
FlyRadar32 Enclosure - Render Component Fit Views
Generates high-resolution close-up renders showing how ESP32, TFT display, and buttons fit.
"""
import sys, os
import FreeCAD
import FreeCADGui

FreeCADGui.showMainWindow()
doc = FreeCAD.open(r"D:\Projects\Embedded\Firmware-Development\flyradar32\enclosure\FlyRadar32_Enclosure.FCStd")
out_dir = r"C:\Users\PC\.gemini\antigravity-ide\brain\b8fd7814-afac-45f2-9c7c-ff268bbf3cbc\scratch"
os.makedirs(out_dir, exist_ok=True)

view = FreeCADGui.activeView()

def set_vis(name, vis=True):
    obj = doc.getObject(name)
    if not obj:
        return
    if hasattr(obj, "ViewObject") and obj.ViewObject:
        obj.ViewObject.Visibility = vis
    if hasattr(obj, "Group"):
        for child in obj.Group:
            if hasattr(child, "ViewObject") and child.ViewObject:
                child.ViewObject.Visibility = vis

def set_transparency(name, trans=0):
    obj = doc.getObject(name)
    if obj and hasattr(obj, "ViewObject") and obj.ViewObject:
        obj.ViewObject.Transparency = trans

# Color styling:
def set_color(name, rgb):
    obj = doc.getObject(name)
    if obj and hasattr(obj, "ViewObject") and obj.ViewObject:
        obj.ViewObject.ShapeColor = rgb

set_color("Shell", (0.13, 0.14, 0.16))
set_color("Lid", (0.13, 0.14, 0.16))
set_color("Lid_Accents", (0.90, 0.10, 0.10))
set_color("Button_Caps", (0.90, 0.10, 0.10))

# ----------------------------------------------------
# VIEW 1: SHELL + ESP32 SEATED IN CRADLE (CLOSE-UP)
# ----------------------------------------------------
print("Rendering View 1: Shell + ESP32...")
set_vis("Shell", True)
set_vis("esp32_Wroom_30pins_C_Type", True)
set_vis("Lid", False)
set_vis("Lid_Accents", False)
set_vis("Button_Caps", False)
set_vis("AZ_Delivery_TFT_1_8_SPI", False)
set_vis("Button_UP", False)
set_vis("Button_SELECT", False)
set_vis("Button_DOWN", False)

view.setCameraType("Perspective")
view.setCameraOrientation(FreeCAD.Rotation(FreeCAD.Vector(0.55, -0.40, -0.73), 130))
view.fitAll()
view.zoomIn()
view.zoomIn()
view.saveImage(os.path.join(out_dir, "fit_1_shell_esp32_cradle.png"), 1600, 1000, "Current")

# ----------------------------------------------------
# VIEW 2: LID UNDERSIDE + TFT DISPLAY + BUTTONS (CLOSE-UP)
# ----------------------------------------------------
print("Rendering View 2: Lid Underside + TFT + Buttons...")
set_vis("Shell", False)
set_vis("esp32_Wroom_30pins_C_Type", False)
set_vis("Lid", True)
set_transparency("Lid", 0)
set_vis("AZ_Delivery_TFT_1_8_SPI", True)
set_vis("Button_UP", True)
set_vis("Button_SELECT", True)
set_vis("Button_DOWN", True)
set_vis("Button_Caps", True)
set_vis("Lid_Accents", False)

view.setCameraType("Perspective")
view.setCameraOrientation(FreeCAD.Rotation(FreeCAD.Vector(-0.45, 0.45, 0.77), -135))
view.fitAll()
view.zoomIn()
view.zoomIn()
view.saveImage(os.path.join(out_dir, "fit_2_lid_tft_buttons_underside.png"), 1600, 1000, "Current")

# ----------------------------------------------------
# VIEW 3: FULL ASSEMBLY WITH SEMI-TRANSPARENT LID (INTERIOR FIT)
# ----------------------------------------------------
print("Rendering View 3: Full Assembly (Transparent Lid)...")
set_vis("Shell", True)
set_vis("esp32_Wroom_30pins_C_Type", True)
set_vis("AZ_Delivery_TFT_1_8_SPI", True)
set_vis("Button_UP", True)
set_vis("Button_SELECT", True)
set_vis("Button_DOWN", True)
set_vis("Button_Caps", True)
set_vis("Lid_Accents", True)
set_vis("Lid", True)
set_transparency("Lid", 65)

view.setCameraType("Orthographic")
view.viewDimetric()
view.fitAll()
view.zoomIn()
view.saveImage(os.path.join(out_dir, "fit_3_full_assembly_transparent.png"), 1600, 1000, "Current")

# ----------------------------------------------------
# VIEW 4: EXTERIOR COSMETIC CLOSE-UP (SOLID LID)
# ----------------------------------------------------
print("Rendering View 4: Exterior Close-up (Solid Lid)...")
set_transparency("Lid", 0)
view.setCameraType("Perspective")
view.setCameraOrientation(FreeCAD.Rotation(FreeCAD.Vector(-0.40, 0.45, 0.80), 85))
view.fitAll()
view.zoomIn()
view.saveImage(os.path.join(out_dir, "fit_4_faceplate_controls_closeup.png"), 1600, 1000, "Current")

# Leave document ready for FreeCAD GUI user with all parts visible and transparent lid:
set_transparency("Lid", 55)
set_vis("Shell", True)
set_vis("esp32_Wroom_30pins_C_Type", True)
set_vis("AZ_Delivery_TFT_1_8_SPI", True)
set_vis("Button_UP", True)
set_vis("Button_SELECT", True)
set_vis("Button_DOWN", True)
set_vis("Button_Caps", True)
set_vis("Lid_Accents", True)
set_vis("Lid", True)
doc.save()

print("All fit renders generated successfully!")
sys.exit(0)
