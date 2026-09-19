import FreeCAD as App
import Part

doc = App.openDocument('d:/Projects/Embedded/Firmware-Development/flyradar32/enclosure/FlyRadar32_Enclosure.FCStd')

# Let's test all components with the 3 tongues
t_front = Part.makeBox(16.0, 3.0, 2.0, App.Vector(-8.0, -34.0 + 2.75, 14.5))
t_rear  = Part.makeBox(16.0, 3.0, 2.0, App.Vector(-8.0,  26.0 - 2.75 - 3.0, 14.5))
t_left  = Part.makeBox(3.0, 16.0, 2.0, App.Vector(-45.0 + 2.75, -8.0, 14.5))
tongues = t_front.fuse(t_rear).fuse(t_left)

for o in doc.Objects:
    if hasattr(o, 'Shape') and o.Name not in ['Lid', 'Shell']:
        col = o.Shape.common(tongues)
        if col.Volume > 0.001:
            print(f"Collision with {o.Name}: {col.Volume:.3f}")

print("Tongue interference test completed!")
