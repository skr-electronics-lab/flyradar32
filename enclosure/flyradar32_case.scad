// =============================================================================
// FlyRadar32 Desktop ATC Radar Enclosure
// Designed for: SKR Electronics Lab / FlyRadar32
// Hardware: ESP32 DevKit + 1.8" ST7735 SPI TFT (128x160) + 3x Tactile Buttons
// Author: SKR Electronics Lab (Pair-programmed with Antigravity)
// Language: OpenSCAD
// =============================================================================

/* [Render Mode] */
// Select which component to preview or export
part = "assembly"; // [assembly:Full 3D Assembly Preview, top:Top Bezel (Printable), bottom:Bottom Chassis (Printable), buttons:Set of 3 Buttons, plate:Print Bed Layout (All Parts)]

/* [Enclosure Dimensions] */
case_width        = 96.0;   // Overall enclosure width (X)
case_depth        = 74.0;   // Overall enclosure depth (Y)
case_height_front = 24.0;   // Front edge height (Z)
case_height_back  = 40.0;   // Rear edge height (Z) -> Ergonomic ~12.2 deg desk viewing tilt
wall_thickness    = 2.4;    // Sturdy wall thickness (6 perimeters with 0.4mm nozzle)
corner_radius     = 5.0;    // Exterior smooth corner radius
lip_height        = 2.0;    // Interlocking alignment lip between top & bottom

/* [Display ST7735 Dimensions] */
disp_pcb_w        = 58.5;   // ST7735 PCB width (X)
disp_pcb_h        = 34.5;   // ST7735 PCB height (Y)
disp_pcb_th       = 1.6;    // PCB thickness
disp_view_w       = 37.0;   // Visible screen aperture width (X)
disp_view_h       = 30.0;   // Visible screen aperture height (Y)
disp_glass_th     = 2.6;    // Glass + frame thickness
disp_hole_dx      = 53.0;   // Corner mounting hole pitch (X)
disp_hole_dy      = 29.5;   // Corner mounting hole pitch (Y)
disp_hole_dia     = 2.2;    // Hole diameter for M2 screws
disp_offset_x     = -11.5;  // Shift left to leave space for button column on right
disp_offset_y     = 2.0;    // Slightly above center

/* [Button Controls] */
btn_hole_dia      = 7.2;    // Hole in bezel for button plunger
btn_cap_dia       = 6.4;    // Button plunger diameter (0.8mm clearance)
btn_flange_dia    = 9.6;    // Retention flange diameter (cannot fall out)
btn_height        = 8.5;    // Total button height
btn_spacing_y     = 11.5;   // Vertical spacing between buttons
btn_col_x         = 31.0;   // X position of button column

/* [ESP32 DevKit Mounting] */
esp_pcb_w         = 28.5;   // ESP32 width
esp_pcb_l         = 52.5;   // ESP32 length
esp_pcb_th        = 1.6;    // ESP32 PCB thickness
esp_hole_dx       = 23.0;   // Standoff spacing X
esp_hole_dy       = 46.5;   // Standoff spacing Y
usb_cutout_w      = 13.0;   // USB plug access width
usb_cutout_h      = 8.0;    // USB plug access height

/* [Assembly Fasteners] */
case_screw_dia    = 3.2;    // M3 clearance hole
case_boss_dia     = 7.5;    // Screw boss outer diameter
case_insert_dia   = 4.2;    // M3 heat-set insert hole (or thread bite)
case_screw_dx     = 83.0;   // Corner screw hole pitch X
case_screw_dy     = 61.0;   // Corner screw hole pitch Y

/* [Derived Math] */
face_mid_z        = (case_height_front + case_height_back) / 2;
tilt_angle        = atan2(case_height_back - case_height_front, case_depth);
split_z           = (case_height_front + case_height_back) * 0.42;

/* [Quality / Resolution] */
$fn = 64;

// -----------------------------------------------------------------------------
// HELPER MODULES
// -----------------------------------------------------------------------------

// Rounded box centered in XY, resting on Z=0
module rounded_box(w, d, h, r) {
    hull() {
        for (x = [-w/2 + r, w/2 - r]) {
            for (y = [-d/2 + r, d/2 - r]) {
                translate([x, y, 0])
                    cylinder(r=r, h=h);
            }
        }
    }
}

// Wedge-shaped rounded enclosure body with inclined top face
module enclosure_hull(w, d, h_front, h_back, r) {
    hull() {
        for (x = [-w/2 + r, w/2 - r]) {
            translate([x, -d/2 + r, 0])
                cylinder(r=r, h=h_front);
            translate([x, d/2 - r, 0])
                cylinder(r=r, h=h_back);
        }
    }
}

// Coordinate system aligned with the tilted front face
module on_top_face() {
    translate([0, 0, face_mid_z])
        rotate([tilt_angle, 0, 0])
            children();
}

// -----------------------------------------------------------------------------
// TOP BEZEL MODULE (Clean front face, blind interior screw bosses)
// -----------------------------------------------------------------------------
module top_bezel() {
    difference() {
        union() {
            // Main exterior shell
            difference() {
                enclosure_hull(case_width, case_depth, case_height_front, case_height_back, corner_radius);
                
                // Cut away bottom chassis portion
                translate([0, 0, -split_z])
                    cube([case_width + 20, case_depth + 20, split_z * 2], center=true);

                // Hollow out interior cavity
                translate([0, 0, -0.2])
                    enclosure_hull(
                        case_width - wall_thickness * 2,
                        case_depth - wall_thickness * 2,
                        case_height_front - wall_thickness,
                        case_height_back - wall_thickness,
                        max(0.5, corner_radius - wall_thickness)
                    );
            }

            // ST7735 Internal Mounting Standoffs (under the top face)
            on_top_face() {
                translate([disp_offset_x, disp_offset_y, -wall_thickness]) {
                    for (sx = [-disp_hole_dx/2, disp_hole_dx/2]) {
                        for (sy = [-disp_hole_dy/2, disp_hole_dy/2]) {
                            translate([sx, sy, -5.5])
                                cylinder(d=5.5, h=5.5);
                        }
                    }
                }
            }

            // Corner Assembly Screw Bosses (Blind from top — screws enter from bottom!)
            for (sx = [-case_screw_dx/2, case_screw_dx/2]) {
                for (sy = [-case_screw_dy/2, case_screw_dy/2]) {
                    post_top_z = (sy < 0) ? (case_height_front - wall_thickness) : (case_height_back - wall_thickness);
                    translate([sx, sy, split_z])
                        cylinder(d=case_boss_dia, h=post_top_z - split_z);
                }
            }

            // Interlocking alignment tongue around perimeter
            difference() {
                translate([0, 0, split_z - lip_height])
                    rounded_box(
                        case_width - wall_thickness * 1.1,
                        case_depth - wall_thickness * 1.1,
                        lip_height + 0.1,
                        corner_radius - wall_thickness * 0.5
                    );
                translate([0, 0, split_z - lip_height - 0.5])
                    rounded_box(
                        case_width - wall_thickness * 2.2,
                        case_depth - wall_thickness * 2.2,
                        lip_height + 1.5,
                        max(0.5, corner_radius - wall_thickness)
                    );
            }
        }

        // --- CUTOUTS ON TOP FACE ---

        // 1. ST7735 Display Window
        on_top_face() {
            translate([disp_offset_x, disp_offset_y, 0]) {
                // Viewport aperture through the front face
                translate([0, 0, -10])
                    cube([disp_view_w, disp_view_h, 30], center=true);

                // 45 degree aesthetic outer chamfer around screen window
                translate([0, 0, 0.2])
                    hull() {
                        cube([disp_view_w, disp_view_h, 0.1], center=true);
                        translate([0, 0, 2.0])
                            cube([disp_view_w + 3.6, disp_view_h + 3.6, 0.1], center=true);
                    }

                // Internal glass seating pocket
                translate([0, 0, -wall_thickness - 0.5])
                    cube([disp_pcb_w + 1.2, disp_pcb_h + 1.2, 4.0], center=true);

                // Screw holes for ST7735 PCB corners (M2 self-tapping)
                for (sx = [-disp_hole_dx/2, disp_hole_dx/2]) {
                    for (sy = [-disp_hole_dy/2, disp_hole_dy/2]) {
                        translate([sx, sy, -12])
                            cylinder(d=disp_hole_dia, h=15);
                    }
                }
            }

            // 2. Button Holes (UP, SELECT, DOWN) on Right Column
            translate([btn_col_x, 0, 0]) {
                for (i = [-1, 0, 1]) {
                    translate([0, i * btn_spacing_y, 0]) {
                        // Through-hole for button plunger
                        translate([0, 0, -10])
                            cylinder(d=btn_hole_dia, h=30);

                        // Chamfer on top face
                        translate([0, 0, 0.2])
                            cylinder(d1=btn_hole_dia, d2=btn_hole_dia + 1.6, h=1.0);

                        // Internal recess for retention flange
                        translate([0, 0, -wall_thickness - 3.0])
                            cylinder(d=btn_flange_dia + 0.8, h=5.0);
                    }
                }

                // Button Labels Debossed into Face (0.6mm deep)
                translate([0, 1 * btn_spacing_y + 6.0, -0.6])
                    linear_extrude(height=1.0)
                        text("▲", size=3.6, halign="center", valign="center", font="Liberation Sans:style=Bold");

                translate([0, 0 * btn_spacing_y + 5.2, -0.6])
                    linear_extrude(height=1.0)
                        text("SEL", size=2.3, halign="center", valign="center", font="Liberation Sans:style=Bold");

                translate([0, -1 * btn_spacing_y + 6.0, -0.6])
                    linear_extrude(height=1.0)
                        text("▼", size=3.6, halign="center", valign="center", font="Liberation Sans:style=Bold");
            }

            // 3. Branding Debossed on Bottom Chin
            translate([disp_offset_x, -case_depth/2 + 7.5, -0.6]) {
                linear_extrude(height=1.0)
                    text("FLYRADAR 32", size=4.0, halign="center", valign="center", font="Liberation Sans:style=Bold");
                translate([0, -4.5, 0])
                    linear_extrude(height=0.8)
                        text("SKR ELECTRONICS LAB", size=2.3, halign="center", valign="center", font="Liberation Sans:style=Bold");
            }
        }

        // 4. Blind Screw Holes inside Top Bosses (M3 heat-set insert or 2.8mm tap hole)
        for (sx = [-case_screw_dx/2, case_screw_dx/2]) {
            for (sy = [-case_screw_dy/2, case_screw_dy/2]) {
                translate([sx, sy, split_z - 0.1])
                    cylinder(d=case_insert_dia, h=10.0);
            }
        }
    }
}

// -----------------------------------------------------------------------------
// BOTTOM CHASSIS MODULE
// -----------------------------------------------------------------------------
module bottom_case() {
    difference() {
        union() {
            // Main bottom tub
            difference() {
                intersection() {
                    enclosure_hull(case_width, case_depth, case_height_front, case_height_back, corner_radius);
                    translate([0, 0, split_z / 2])
                        cube([case_width + 20, case_depth + 20, split_z], center=true);
                }

                // Inner cavity
                translate([0, 0, wall_thickness])
                    rounded_box(
                        case_width - wall_thickness * 2,
                        case_depth - wall_thickness * 2,
                        split_z + 2,
                        max(0.5, corner_radius - wall_thickness)
                    );
            }

            // 4 Corner Screw Bosses
            for (sx = [-case_screw_dx/2, case_screw_dx/2]) {
                for (sy = [-case_screw_dy/2, case_screw_dy/2]) {
                    translate([sx, sy, 0])
                        cylinder(d=case_boss_dia, h=split_z);
                }
            }

            // ESP32 Dev Board Cradle
            translate([0, -2.0, 0]) {
                for (sx = [-esp_hole_dx/2, esp_hole_dx/2]) {
                    for (sy = [-esp_hole_dy/2, esp_hole_dy/2]) {
                        translate([sx, sy, 0])
                            cylinder(d=6.0, h=wall_thickness + 4.5);
                    }
                }
                // Retention Side Guides for ESP32
                for (sx = [-esp_pcb_w/2 - 1.2, esp_pcb_w/2 + 1.2]) {
                    translate([sx, 0, wall_thickness])
                        cube([1.4, esp_pcb_l * 0.6, 7.5], center=true);
                }
            }
        }

        // --- CUTOUTS ON BOTTOM ---

        // 1. Countersunk M3 Screw Pass-Through Holes (screws enter from bottom)
        for (sx = [-case_screw_dx/2, case_screw_dx/2]) {
            for (sy = [-case_screw_dy/2, case_screw_dy/2]) {
                translate([sx, sy, -0.1])
                    cylinder(d=case_screw_dia, h=split_z + 1.0);
                // Countersink cone for M3 screw head flush fit
                translate([sx, sy, -0.1])
                    cylinder(d1=6.8, d2=case_screw_dia, h=2.5);
            }
        }

        // 2. ESP32 Mounting Screw Pilot Holes
        translate([0, -2.0, 0]) {
            for (sx = [-esp_hole_dx/2, esp_hole_dx/2]) {
                for (sy = [-esp_hole_dy/2, esp_hole_dy/2]) {
                    translate([sx, sy, wall_thickness])
                        cylinder(d=2.2, h=10);
                }
            }
        }

        // 3. Rear USB Power Port Cutout
        translate([0, case_depth/2, wall_thickness + 6.5]) {
            hull() {
                translate([-usb_cutout_w/2 + 2, 0, 0])
                    rotate([90, 0, 0])
                        cylinder(r=2, h=10, center=true);
                translate([usb_cutout_w/2 - 2, 0, 0])
                    rotate([90, 0, 0])
                        cylinder(r=2, h=10, center=true);
                translate([-usb_cutout_w/2 + 2, 0, usb_cutout_h - 4])
                    rotate([90, 0, 0])
                        cylinder(r=2, h=10, center=true);
                translate([usb_cutout_w/2 - 2, 0, usb_cutout_h - 4])
                    rotate([90, 0, 0])
                        cylinder(r=2, h=10, center=true);
            }
        }

        // 4. Passive Air Ventilation Louvers (Bottom Base)
        for (vx = [-28 : 7 : 28]) {
            translate([vx, -2.0, -0.1])
                rounded_box(3.0, 36.0, wall_thickness + 0.5, 1.2);
        }

        // 5. Rear Ventilation Louvers
        for (vx = [-24 : 8 : 24]) {
            if (abs(vx) > 8) {
                translate([vx, case_depth/2, wall_thickness + 7.0])
                    rotate([90, 0, 0])
                        rounded_box(3.0, 7.5, wall_thickness * 2, 1.2);
            }
        }

        // 6. Recesses for 4x Rubber Feet (8mm diameter pads)
        for (sx = [-case_width/2 + 10, case_width/2 - 10]) {
            for (sy = [-case_depth/2 + 10, case_depth/2 - 10]) {
                translate([sx, sy, -0.1])
                    cylinder(d=8.5, h=0.8);
            }
        }
    }
}

// -----------------------------------------------------------------------------
// TACTILE BUTTON CAP MODULE
// -----------------------------------------------------------------------------
module button_cap() {
    union() {
        // Bottom retention flange (stays inside bezel)
        cylinder(d=btn_flange_dia, h=1.6);

        // Plunger shaft protruding through front bezel
        translate([0, 0, 1.6])
            cylinder(d=btn_cap_dia, h=btn_height - 1.6);

        // Tactile concave dish on top face for finger grip
        difference() {
            translate([0, 0, btn_height])
                cylinder(d=btn_cap_dia, h=0.01);
            translate([0, 0, btn_height + 5.8])
                sphere(r=6.0);
        }

        // Internal switch actuator depression stem
        translate([0, 0, -1.2])
            cylinder(d=3.2, h=1.4);
    }
}

// -----------------------------------------------------------------------------
// ASSEMBLY / HARDWARE MOCKUP PREVIEW
// -----------------------------------------------------------------------------
module mock_display() {
    color([0.15, 0.45, 0.85, 0.8]) // Blue PCB
        cube([disp_pcb_w, disp_pcb_h, disp_pcb_th], center=true);
    color([0.05, 0.05, 0.05, 0.95]) // Black LCD Glass
        translate([0, 0, disp_pcb_th/2 + 1.2])
            cube([disp_view_w + 3, disp_view_h + 3, 2.0], center=true);
    color([0.0, 0.9, 0.2, 0.9]) // Radar Active Green Sweep
        translate([0, 0, disp_pcb_th/2 + 2.25])
            cube([disp_view_w, disp_view_h, 0.1], center=true);
}

module mock_esp32() {
    color([0.1, 0.1, 0.1, 0.9]) // Matte Black ESP32 PCB
        cube([esp_pcb_w, esp_pcb_l, esp_pcb_th], center=true);
    color([0.8, 0.8, 0.85]) // Metal RF Shield Can
        translate([0, 6, esp_pcb_th/2 + 1.6])
            cube([18, 25, 3.0], center=true);
    color([0.7, 0.7, 0.75]) // Micro USB Port
        translate([0, esp_pcb_l/2 - 1.5, esp_pcb_th/2 + 1.5])
            cube([7.5, 6.0, 3.0], center=true);
}

// -----------------------------------------------------------------------------
// SELECTOR / DISPATCH
// -----------------------------------------------------------------------------
if (part == "assembly") {
    // Bottom Base (Dark Graphite)
    color([0.18, 0.20, 0.22, 0.95])
        bottom_case();

    // Top Bezel (Aviation Cockpit Slate / Dark Navy)
    color([0.24, 0.27, 0.30, 0.90])
        top_bezel();

    // Buttons (Tactical Aviation Orange / Amber)
    color([1.0, 0.45, 0.0, 1.0]) {
        on_top_face() {
            translate([btn_col_x, 0, 0]) {
                for (i = [-1, 0, 1]) {
                    translate([0, i * btn_spacing_y, -wall_thickness + 1.2])
                        button_cap();
                }
            }
        }
    }

    // Hardware Mockups inside case
    on_top_face() {
        translate([disp_offset_x, disp_offset_y, -wall_thickness - disp_glass_th - disp_pcb_th/2])
            mock_display();
    }

    translate([0, -2.0, wall_thickness + 4.5 + esp_pcb_th/2])
        mock_esp32();

} else if (part == "top") {
    // Oriented for FDM 3D printing (flat bottom edge down)
    translate([0, 0, -split_z])
        top_bezel();

} else if (part == "bottom") {
    // Oriented flat on base for clean 3D printing without supports
    bottom_case();

} else if (part == "buttons") {
    // 3 Button caps arranged for printing
    for (i = [0:2]) {
        translate([i * (btn_flange_dia + 4), 0, 0])
            button_cap();
    }

} else if (part == "plate") {
    // Complete 3D printing build plate layout
    translate([-case_width/2 - 4, 0, 0])
        bottom_case();

    translate([case_width/2 + 4, 0, -split_z])
        top_bezel();

    for (i = [0:2]) {
        translate([0, -case_depth/2 - 12 + i * 11, 0])
            button_cap();
    }
}
