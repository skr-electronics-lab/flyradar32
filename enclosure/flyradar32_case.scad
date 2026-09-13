// =============================================================================
// FlyRadar32 Desktop ATC Radar Console Enclosure
// Designed for: SKR Electronics Lab / FlyRadar32
// Hardware: ESP32 DevKit V1 + AZ-Delivery 1.8" ST7735 SPI TFT + 3x Tactile Buttons
// Style: Modern Low-Profile Avionics Desktop Console
// Language: OpenSCAD
// =============================================================================

/* [Render Mode] */
// Select which component to preview or export
part = "assembly"; // [assembly:Full 3D Assembly Preview, top:Top Bezel (Printable), bottom:Bottom Chassis (Printable), buttons:Set of 3 Buttons, plate:Print Bed Layout (All Parts)]

/* [Enclosure Dimensions] */
case_width        = 104.0;  // Overall enclosure width (X)
case_depth        = 76.0;   // Overall enclosure depth (Y)
case_height_front = 18.0;   // Front edge height (Z) - sleek low profile
case_height_back  = 30.0;   // Rear edge height (Z) - comfortable 9° desktop incline
wall_th           = 2.0;    // Structural wall thickness (5 perimeters with 0.4mm nozzle)
corner_r          = 6.0;    // Exterior smooth corner fillet
split_h           = 10.0;   // Bottom chassis rim height
lip_h             = 1.8;    // Perimeter alignment lip

/* [Display ST7735 AZ-Delivery Exact CAD Measurements] */
disp_pcb_w        = 58.0;   // Exact PCB width (X) from AP242 CAD
disp_pcb_h        = 34.5;   // Exact PCB height (Y) from AP242 CAD
disp_pcb_th       = 1.6;    // PCB thickness
disp_glass_w      = 43.7;   // Outer glass/frame width
disp_glass_h      = 34.5;   // Outer glass/frame height
disp_glass_th     = 2.4;    // Glass assembly thickness above PCB
disp_glass_ox     = -0.8;   // Glass center X offset relative to PCB center
disp_view_w       = 36.0;   // Active viewable TFT screen width
disp_view_h       = 29.0;   // Active viewable TFT screen height
disp_hole_dx      = 52.0;   // Exact corner mounting hole pitch X
disp_hole_dy      = 28.5;   // Exact corner mounting hole pitch Y
disp_hole_dia     = 2.4;    // Pilot hole diameter for M2/M2.5 screws
disp_x            = -13.5;  // Shift display left to balance with buttons on right
disp_y            = 2.5;    // Center vertically on bezel face

/* [Button Controls] */
btn_col_x         = 33.0;   // X position of button column
btn_spacing_y     = 11.5;   // Vertical spacing between buttons
btn_cap_dia       = 6.6;    // Button plunger diameter (sleek low profile)
btn_hole_dia      = 7.4;    // Bezel cutout diameter (0.4mm radial clearance)
btn_flange_dia    = 9.6;    // Retention flange diameter (cannot fall out)
btn_flange_th     = 1.2;    // Flange thickness
btn_protrusion    = 1.8;    // Height button sticks out above bezel face (sleek!)
btn_total_h       = wall_th + btn_protrusion + btn_flange_th; // 5.0mm total
btn_boss_dy       = 17.5;   // Spacing for internal button PCB standoffs

/* [ESP32 DevKit V1 30-Pin Mounting] */
esp_pcb_w         = 28.5;   // ESP32 width
esp_pcb_l         = 52.5;   // ESP32 length
esp_pcb_th        = 1.6;    // ESP32 PCB thickness
esp_hole_dx       = 23.0;   // Standoff spacing X
esp_hole_dy       = 46.5;   // Standoff spacing Y
usb_cutout_w      = 12.0;   // USB plug access width
usb_cutout_h      = 7.0;    // USB plug access height

/* [Enclosure Assembly Screws] */
case_boss_dia     = 7.0;    // Outer diameter of corner screw posts
case_screw_dia    = 3.2;    // M3 clearance hole
case_insert_dia   = 4.0;    // M3 heat-set insert hole (or self-tap bite)
case_screw_dx     = case_width - 15.0; // 89.0mm
case_screw_dy     = case_depth - 15.0; // 61.0mm

/* [Derived Math] */
tilt_angle = atan2(case_height_back - case_height_front, case_depth); // ~8.97 deg
face_mid_z = (case_height_front + case_height_back) / 2;

/* [Quality] */
$fn = 64;

// -----------------------------------------------------------------------------
// GEOMETRY PRIMITIVES
// -----------------------------------------------------------------------------

// 2D rounded rectangle centered at origin
module rounded_rect_2d(w, d, r) {
    hull() {
        for (x = [-w/2 + r, w/2 - r]) {
            for (y = [-d/2 + r, d/2 - r]) {
                translate([x, y])
                    circle(r=r);
            }
        }
    }
}

// 3D rounded prism extruded along Z
module rounded_prism(w, d, h, r) {
    linear_extrude(height=h)
        rounded_rect_2d(w, d, r);
}

// Wedge-shaped enclosure solid body
module enclosure_raw_solid() {
    hull() {
        for (x = [-case_width/2 + corner_r, case_width/2 - corner_r]) {
            // Front low edge
            translate([x, -case_depth/2 + corner_r, 0])
                cylinder(r=corner_r, h=case_height_front);
            // Rear high edge
            translate([x, case_depth/2 - corner_r, 0])
                cylinder(r=corner_r, h=case_height_back);
        }
    }
}

// Transform children to align with the inclined top face
module on_top_face() {
    translate([0, 0, face_mid_z])
        rotate([tilt_angle, 0, 0])
            children();
}

// -----------------------------------------------------------------------------
// TOP BEZEL COMPONENT (Pristine face, zero exterior screw holes)
// -----------------------------------------------------------------------------
module top_bezel() {
    difference() {
        union() {
            // Outer shell above split line
            difference() {
                enclosure_raw_solid();

                // Cut away bottom chassis portion
                translate([0, 0, -50])
                    cube([case_width + 40, case_depth + 40, 50 + split_h], center=true);

                // Hollow out interior cavity
                translate([0, 0, -1])
                    hull() {
                        for (x = [-case_width/2 + corner_r, case_width/2 - corner_r]) {
                            translate([x, -case_depth/2 + corner_r, 0])
                                cylinder(r=max(0.5, corner_r - wall_th), h=case_height_front - wall_th);
                            translate([x, case_depth/2 - corner_r, 0])
                                cylinder(r=max(0.5, corner_r - wall_th), h=case_height_back - wall_th);
                        }
                    }
            }

            // ST7735 Internal Mounting Bosses (under top face)
            on_top_face() {
                translate([disp_x, disp_y, -wall_th]) {
                    for (sx = [-disp_hole_dx/2, disp_hole_dx/2]) {
                        for (sy = [-disp_hole_dy/2, disp_hole_dy/2]) {
                            translate([sx, sy, -5.0])
                                cylinder(d=5.8, h=5.0);
                        }
                    }
                }

                // Button PCB Carrier Standoff Bosses
                translate([btn_col_x, 0, -wall_th]) {
                    for (sy = [-btn_boss_dy, btn_boss_dy]) {
                        translate([0, sy, -5.0])
                            cylinder(d=5.0, h=5.0);
                    }
                }
            }

            // 4 Corner Screw Bosses (Strictly internal — stopped 2.5mm below top face!)
            for (sx = [-case_screw_dx/2, case_screw_dx/2]) {
                for (sy = [-case_screw_dy/2, case_screw_dy/2]) {
                    // Safe top limit so boss NEVER reaches top face
                    safe_top_z = face_mid_z + sy * tan(tilt_angle) - wall_th - 1.5;
                    translate([sx, sy, split_h])
                        cylinder(d=case_boss_dia, h=max(4.0, safe_top_z - split_h));
                }
            }

            // Interlocking alignment tongue (protrudes downward into bottom chassis)
            difference() {
                translate([0, 0, split_h - lip_h])
                    rounded_prism(
                        case_width - wall_th * 1.2,
                        case_depth - wall_th * 1.2,
                        lip_h + 0.1,
                        corner_r - wall_th * 0.6
                    );
                translate([0, 0, split_h - lip_h - 0.5])
                    rounded_prism(
                        case_width - wall_th * 2.2,
                        case_depth - wall_th * 2.2,
                        lip_h + 1.5,
                        max(0.5, corner_r - wall_th)
                    );
            }
        }

        // --- CUTOUTS ON TOP BEZEL ---

        on_top_face() {
            // 1. ST7735 Display Aperture & Pockets
            translate([disp_x, disp_y, 0]) {
                // Main screen viewport cutout through front face
                translate([disp_glass_ox, 0, -10])
                    cube([disp_view_w, disp_view_h, 30], center=true);

                // Subtle 45-degree aesthetic bevel around screen window
                translate([disp_glass_ox, 0, 0.1])
                    hull() {
                        cube([disp_view_w, disp_view_h, 0.1], center=true);
                        translate([0, 0, 1.2])
                            cube([disp_view_w + 2.4, disp_view_h + 2.4, 0.1], center=true);
                    }

                // Internal glass seating pocket (flush fit for glass frame)
                translate([disp_glass_ox, 0, -wall_th - disp_glass_th/2])
                    cube([disp_glass_w + 0.8, disp_glass_h + 0.8, disp_glass_th + 0.2], center=true);

                // Internal PCB relief pocket
                translate([0, 0, -wall_th - disp_glass_th - disp_pcb_th/2])
                    cube([disp_pcb_w + 1.0, disp_pcb_h + 1.0, disp_pcb_th + 1.0], center=true);

                // M2/M2.5 self-tapping pilot holes in display standoffs
                for (sx = [-disp_hole_dx/2, disp_hole_dx/2]) {
                    for (sy = [-disp_hole_dy/2, disp_hole_dy/2]) {
                        translate([sx, sy, -12])
                            cylinder(d=disp_hole_dia, h=10);
                    }
                }
            }

            // 2. Button Holes on Right Column
            translate([btn_col_x, 0, 0]) {
                for (i = [-1, 0, 1]) {
                    translate([0, i * btn_spacing_y, 0]) {
                        // Through-hole for button plunger
                        translate([0, 0, -10])
                            cylinder(d=btn_hole_dia, h=30);

                        // Chamfer on top edge
                        translate([0, 0, 0.1])
                            cylinder(d1=btn_hole_dia, d2=btn_hole_dia + 1.0, h=0.8);

                        // Internal recess for retention flange
                        translate([0, 0, -wall_th - 2.0])
                            cylinder(d=btn_flange_dia + 0.8, h=3.5);
                    }
                }

                // Button Pilot holes for PCB bosses
                for (sy = [-btn_boss_dy, btn_boss_dy]) {
                    translate([0, sy, -10])
                        cylinder(d=2.2, h=8);
                }

                // Tactile Button Function Debossed Icons (0.4mm deep)
                translate([0, 1 * btn_spacing_y + 5.5, -0.4])
                    linear_extrude(height=0.6)
                        text("▲", size=3.0, halign="center", valign="center", font="Liberation Sans:style=Bold");

                translate([0, 0 * btn_spacing_y + 4.8, -0.4])
                    linear_extrude(height=0.6)
                        text("SEL", size=2.0, halign="center", valign="center", font="Liberation Sans:style=Bold");

                translate([0, -1 * btn_spacing_y + 5.5, -0.4])
                    linear_extrude(height=0.6)
                        text("▼", size=3.0, halign="center", valign="center", font="Liberation Sans:style=Bold");
            }

            // 3. Clean Console Branding on Lower Chin
            translate([0, -case_depth/2 + 6.5, -0.4]) {
                linear_extrude(height=0.6)
                    text("FLYRADAR 32", size=3.5, halign="center", valign="center", font="Liberation Sans:style=Bold");
                translate([0, -3.8, 0])
                    linear_extrude(height=0.5)
                        text("SKR ELECTRONICS LAB", size=1.8, halign="center", valign="center", font="Liberation Sans");
            }
        }

        // 4. Blind Screw Holes inside Top Bosses (M3 heat-set insert or 2.8mm tap)
        for (sx = [-case_screw_dx/2, case_screw_dx/2]) {
            for (sy = [-case_screw_dy/2, case_screw_dy/2]) {
                translate([sx, sy, split_h - 0.1])
                    cylinder(d=case_insert_dia, h=8.0);
            }
        }
    }
}

// -----------------------------------------------------------------------------
// BOTTOM CHASSIS COMPONENT (Flat base, ESP32 cradle, ventilation, feet)
// -----------------------------------------------------------------------------
module bottom_case() {
    difference() {
        union() {
            // Main bottom tub
            difference() {
                intersection() {
                    enclosure_raw_solid();
                    translate([0, 0, split_h / 2])
                        cube([case_width + 40, case_depth + 40, split_h], center=true);
                }

                // Inner tub cavity
                translate([0, 0, wall_th])
                    rounded_prism(
                        case_width - wall_th * 2,
                        case_depth - wall_th * 2,
                        split_h + 2,
                        max(0.5, corner_r - wall_th)
                    );
            }

            // 4 Corner Screw Bosses
            for (sx = [-case_screw_dx/2, case_screw_dx/2]) {
                for (sy = [-case_screw_dy/2, case_screw_dy/2]) {
                    translate([sx, sy, 0])
                        cylinder(d=case_boss_dia, h=split_h);
                }
            }

            // ESP32 DevKit V1 Cradle (centered in base)
            translate([0, -1.0, 0]) {
                // 4 Corner Standoff Posts
                for (sx = [-esp_hole_dx/2, esp_hole_dx/2]) {
                    for (sy = [-esp_hole_dy/2, esp_hole_dy/2]) {
                        translate([sx, sy, 0])
                            cylinder(d=5.5, h=wall_th + 3.5);
                    }
                }
                // Snug Side Retention Guides for ESP32 PCB
                for (sx = [-esp_pcb_w/2 - 1.0, esp_pcb_w/2 + 1.0]) {
                    translate([sx, 0, wall_th])
                        cube([1.2, esp_pcb_l * 0.6, 6.0], center=true);
                }
            }
        }

        // --- CUTOUTS ON BOTTOM CHASSIS ---

        // 1. Countersunk M3 Screw Pass-Through Holes (screws enter from bottom!)
        for (sx = [-case_screw_dx/2, case_screw_dx/2]) {
            for (sy = [-case_screw_dy/2, case_screw_dy/2]) {
                translate([sx, sy, -0.1])
                    cylinder(d=case_screw_dia, h=split_h + 1.0);
                // Countersink cone for M3 screw head flush fit
                translate([sx, sy, -0.1])
                    cylinder(d1=6.8, d2=case_screw_dia, h=2.4);
            }
        }

        // 2. ESP32 Mounting Screw Pilot Holes
        translate([0, -1.0, 0]) {
            for (sx = [-esp_hole_dx/2, esp_hole_dx/2]) {
                for (sy = [-esp_hole_dy/2, esp_hole_dy/2]) {
                    translate([sx, sy, wall_th])
                        cylinder(d=2.2, h=8);
                }
            }
        }

        // 3. Rear USB Cable Cutout (Smooth oval receptacle)
        translate([0, case_depth/2, wall_th + 5.0]) {
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

        // 4. Passive Thermal Air Ventilation Louvers (Bottom Base)
        for (vx = [-28 : 7 : 28]) {
            translate([vx, -1.0, -0.1])
                rounded_prism(2.8, 32.0, wall_th + 0.5, 1.0);
        }

        // 5. Rear Ventilation Louvers
        for (vx = [-26 : 8 : 26]) {
            if (abs(vx) > 8) {
                translate([vx, case_depth/2, wall_th + 5.5])
                    rotate([90, 0, 0])
                        rounded_prism(2.8, 6.0, wall_th * 2.5, 1.0);
            }
        }

        // 6. Recessed Pockets for 4x Rubber Desk Feet (8mm pads)
        for (sx = [-case_width/2 + 10, case_width/2 - 10]) {
            for (sy = [-case_depth/2 + 10, case_depth/2 - 10]) {
                translate([sx, sy, -0.1])
                    cylinder(d=8.5, h=0.8);
            }
        }
    }
}

// -----------------------------------------------------------------------------
// SLEEK LOW-PROFILE BUTTON CAP COMPONENT
// -----------------------------------------------------------------------------
module button_cap() {
    union() {
        // Bottom retention flange (captive inside bezel)
        cylinder(d=btn_flange_dia, h=btn_flange_th);

        // Plunger shaft protruding through bezel
        translate([0, 0, btn_flange_th])
            cylinder(d=btn_cap_dia, h=wall_th + btn_protrusion - 0.4);

        // Subtle top chamfer & ergonomic tactile concave dish
        translate([0, 0, btn_flange_th + wall_th + btn_protrusion - 0.4]) {
            cylinder(d1=btn_cap_dia, d2=btn_cap_dia - 0.8, h=0.4);
            difference() {
                cylinder(d=btn_cap_dia - 0.8, h=0.01);
                translate([0, 0, 4.8])
                    sphere(r=5.0);
            }
        }

        // Internal switch depression stem
        translate([0, 0, -1.0])
            cylinder(d=3.2, h=1.0);
    }
}

// -----------------------------------------------------------------------------
// ACCURATE HARDWARE MOCKUPS FOR 3D ASSEMBLY PREVIEW
// -----------------------------------------------------------------------------
module mock_display() {
    // Red FR4 PCB
    difference() {
        color([0.80, 0.15, 0.18, 0.95])
            cube([disp_pcb_w, disp_pcb_h, disp_pcb_th], center=true);

        // 4 Corner M3 Mounting Holes (Exact CAD Pitch 52.0 x 28.5 mm)
        for (sx = [-disp_hole_dx/2, disp_hole_dx/2]) {
            for (sy = [-disp_hole_dy/2, disp_hole_dy/2]) {
                translate([sx, sy, 0])
                    cylinder(d=3.2, h=disp_pcb_th + 0.2, center=true);
            }
        }
    }

    // Outer Glass / Metal Bezel Frame (43.7 x 34.5 mm, offset X = -0.8mm)
    translate([disp_glass_ox, 0, disp_pcb_th/2 + disp_glass_th/2]) {
        color([0.12, 0.12, 0.15, 0.98])
            cube([disp_glass_w, disp_glass_h, disp_glass_th], center=true);

        // Active TFT Screen Area (36.0 x 29.0 mm)
        translate([0, 0, disp_glass_th/2 + 0.05]) {
            color([0.03, 0.05, 0.04, 1.0])
                cube([disp_view_w, disp_view_h, 0.05], center=true);

            // Illuminated Radar Sweep Graphic
            color([0.0, 1.0, 0.35, 1.0]) {
                cube([disp_view_w * 0.9, 0.8, 0.1], center=true);
                cylinder(d=12.0, h=0.1, center=true);
            }
        }
    }

    // 8-Pin Header at X = +26.5 mm
    color([0.85, 0.75, 0.2])
        translate([26.5, 0, -disp_pcb_th/2 - 2.5])
            cube([2.54, 20.32, 5.0], center=true);
}

module mock_tactile_switch() {
    color([0.2, 0.2, 0.22])
        cube([6.0, 6.0, 3.5], center=true);
    color([0.75, 0.75, 0.8])
        translate([0, 0, 1.8])
            cube([5.8, 5.8, 0.2], center=true);
    color([0.1, 0.1, 0.1])
        translate([0, 0, 2.8])
            cylinder(d=3.2, h=2.0, center=true);
}

module mock_esp32() {
    // ESP32 DevKit V1 PCB
    color([0.1, 0.12, 0.14, 0.95])
        cube([esp_pcb_w, esp_pcb_l, esp_pcb_th], center=true);

    // Metal RF Shield Can (ESP-WROOM-32)
    color([0.82, 0.82, 0.85])
        translate([0, 6.0, esp_pcb_th/2 + 1.5])
            cube([18.0, 25.0, 3.0], center=true);

    // Micro USB Port
    color([0.75, 0.78, 0.82])
        translate([0, esp_pcb_l/2 - 1.0, esp_pcb_th/2 + 1.5])
            cube([7.5, 6.0, 3.0], center=true);

    // Dual 15-Pin Headers
    for (hx = [-11.43, 11.43]) {
        color([0.15, 0.15, 0.15])
            translate([hx, 0, -esp_pcb_th/2 - 2.5])
                cube([2.54, 38.1, 5.0], center=true);
    }
}

// -----------------------------------------------------------------------------
// SELECTOR / DISPATCH
// -----------------------------------------------------------------------------
if (part == "assembly") {
    // Bottom Chassis (Graphite Black)
    color([0.15, 0.16, 0.18, 0.98])
        bottom_case();

    // Top Bezel (Matte Cockpit Charcoal / Slate)
    color([0.22, 0.24, 0.27, 0.92])
        top_bezel();

    // Low-Profile Tactical Buttons (Aviation Amber)
    color([1.0, 0.42, 0.05, 1.0]) {
        on_top_face() {
            translate([btn_col_x, 0, 0]) {
                for (i = [-1, 0, 1]) {
                    translate([0, i * btn_spacing_y, -wall_th - btn_flange_th])
                        button_cap();
                }
            }
        }
    }

    // Hardware Mockups inside case
    on_top_face() {
        translate([disp_x, disp_y, -wall_th - disp_glass_th - disp_pcb_th/2])
            mock_display();

        translate([btn_col_x, 0, 0]) {
            for (i = [-1, 0, 1]) {
                translate([0, i * btn_spacing_y, -wall_th - 4.5])
                    mock_tactile_switch();
            }
        }
    }

    translate([0, -1.0, wall_th + 3.5 + esp_pcb_th/2])
        mock_esp32();

} else if (part == "top") {
    // Oriented for printing: flat bottom edge down
    translate([0, 0, -split_h])
        top_bezel();

} else if (part == "bottom") {
    // Flat on base: ZERO supports required
    bottom_case();

} else if (part == "buttons") {
    // 3 sleek button caps arranged flat on flanges
    for (i = [-1, 0, 1]) {
        translate([i * (btn_flange_dia + 4.0), 0, 0])
            button_cap();
    }

} else if (part == "plate") {
    // Complete single-print-job build plate layout
    translate([-case_width/2 - 6, 0, 0])
        bottom_case();

    translate([case_width/2 + 6, 0, -split_h])
        top_bezel();

    for (i = [-1, 0, 1]) {
        translate([0, -case_depth/2 - 12 + i * 12, 0])
            button_cap();
    }

} else if (part == "esp32") {
    mock_esp32();

} else if (part == "switch") {
    mock_tactile_switch();
}

