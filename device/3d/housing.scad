/*
 * claude-knock housing for 0420T solenoid + ESP32-C3 Super Mini.
 * See DESIGN.md.
 *
 * part = "base" | "lid" | "pillar" | "all"
 */

part = "all";
$fn = 64;

/* ---------- 0420T solenoid ----------
 * Plunger is a rigid rod that passes through the body and sticks out
 * BOTH ends. The top tip (Ø 4 mm) drives the connection pillar.
 * When energised the plunger moves UP by sol_stroke.
 */
sol_w = 11.2;                 // along the mounting face short side
sol_d = 12.2;                 // perpendicular to the mounting face
sol_h = 20.5;                 // body height
sol_shaft_dia       = 4;
sol_stroke          = 4;
sol_shaft_rest_up   = 4.5;    // plunger TOP tip above body, at rest
sol_shaft_rest_down = 12.8;   // plunger BOTTOM tip below body, at rest
// (max-up = 8.5; max-down = 8.8 — both follow from rest + stroke)
sol_mount_hole   = 2.2;
sol_mount_rect_w = 6;
sol_mount_rect_h = 10;
sol_bolt_len     = 5.8;       // M2 under-head length (shortest available bolt)
sol_thread_depth = 1.5;       // thread engagement into solenoid body

/* ---------- ESP32-C3 Super Mini ---------- */
pcb_l = 22.8;                 // long side  (along X)
pcb_w = 17.9;                 // short side (along Y)
pcb_t = 1;
usbc_w = 8.94;                // USB-C female shell width  (IEC 62680-1-3)
usbc_h = 3.26;                // USB-C female shell height (IEC 62680-1-3)
usbc_protrude = 1.5;
pin_n = 8;
pin_pitch = 2.54;
pin_row_gap = 15;
pin_from_usb = 1.5;
pin_from_other = 3.5;
pin_hole_d = 1.4;             // rail pin hole over 0.64 sq pin

/* ---------- Housing ---------- */
inner = 30;                   // square inner cross-section
wall  = 1.5;
outer = inner + 2*wall;
base_t = 4;                   // 2.5 mm counterbore + 1.5 mm head support layer
lid_top_t = 2;
fit_gap = 0.4;
bolt_clear = 0;               // bracket back flush with -X inner wall

/* ---------- Pin-header rails ---------- */
rail_w       = 5;             // rail width (Y)
pin_depth    = 6;             // pin length below plastic = rail height
rail_h       = pin_depth;     // rail exactly as tall as the pins
plastic_h    = 2.5;           // pin header plastic strip height

/* ---------- Pillar ---------- */
pillar_od         = 7.5;
pillar_cup_id     = 4.3;
pillar_cup_depth  = 3;
pillar_bolt_clear = 3.9;
pillar_tap_pilot  = 2.0;
pillar_tap_len    = 3;
pillar_total      = 21;
pillar_engage     = 2;

lid_top_hole_d   = pillar_od + 0.4;
pillar_above_lid = 3;

/* ---------- Lid-base boss bolts (M2, vertical, 3-point) ---------- */
boss_od      = 6;             // boss post outer diameter
boss_tap_d   = 2.0;           // M2 self-tap pilot diameter
boss_tap_len = 4;             // boss height; matches USB-C clearance limit
m2_cb_depth  = 2.5;          // counterbore depth in base plate (> M2 head 2 mm)
m2_cb_d      = 4.0;          // counterbore diameter
m2_clr_d     = 2.2;          // M2 clearance hole diameter

/* ---------- Anti-slip sticker recesses ---------- */
sticker_d      = 10.5;       // sticker disc diameter
sticker_depth  = 1;          // recess depth into base plate bottom
sticker_margin = 0.5;        // margin from plate edge to sticker disc

/* ======================================================== */
/* Derived positions                                          */
/* ======================================================== */

base_bot_z = 0;
base_top_z = base_t;

/* Rails rise from the base-plate top. Plastic rests on the rail top;
 * PCB rests on top of the plastic.                                   */
rail_top_z    = base_top_z + rail_h;
plastic_top_z = rail_top_z + plastic_h;
pcb_bot_z     = plastic_top_z;
pcb_top_z     = pcb_bot_z + pcb_t;

/* USB-C connector is mounted on the PCB bottom face and points +X. */
usbc_center_z = pcb_bot_z - usbc_h/2;
usbc_center_y = 0;

/* PCB +X edge flush with the inner +X wall. */
pcb_x_edge_plus = inner/2;
pcb_center_x = pcb_x_edge_plus - pcb_l/2;
pcb_center_y = 0;

/* Solenoid is raised so the plunger BOTTOM tip (12.8 mm below body at
 * rest) clears the PCB top by 1 mm.                                  */
sol_z0 = pcb_top_z + sol_shaft_rest_down + 1;
sol_z1 = sol_z0 + sol_h;
sol_cz = (sol_z0 + sol_z1)/2;

/* Bracket on -X: back flush with inner wall.
 * Thickness set by bolt geometry: head bottoms on counterbore floor,
 * shank travels (bracket_t - m2_cb_depth) through remaining bracket,
 * then sol_thread_depth into the solenoid. With sol_bolt_len = total
 * under-head length, (bracket_t - m2_cb_depth) + sol_thread_depth =
 * sol_bolt_len  →  bracket_t = sol_bolt_len - sol_thread_depth + m2_cb_depth
 * = 5.8 - 1.5 + 2.5 = 6.8 mm.                                         */
bracket_t      = sol_bolt_len - sol_thread_depth + m2_cb_depth;
bracket_back_x = -inner/2 + bolt_clear;
sol_face_x     = bracket_back_x + bracket_t;
sol_cx = sol_face_x + sol_d/2;
sol_cy = 0;

bracket_y  = 12;              // narrow enough to clear corner bosses by 1 mm
bracket_z0 = base_top_z + 5;   // raised 5 mm above base plate
bracket_z1 = sol_z1 + 2;

/* Plunger Z positions (at rest and max stroke). */
plunger_top_rest = sol_z1 + sol_shaft_rest_up;
plunger_top_max  = sol_z1 + sol_shaft_rest_up + sol_stroke;
plunger_bot_rest = sol_z0 - sol_shaft_rest_down;
plunger_bot_max  = sol_z0 - sol_shaft_rest_down + sol_stroke;

/* Pillar — cup engages the plunger TOP at rest. */
pillar_z0 = plunger_top_rest - pillar_engage;
pillar_z1 = pillar_z0 + pillar_total;

/* Lid — top just high enough that the pillar cup bottom stays inside
 * the hole at full stroke (pillar_z0 + sol_stroke + 2 mm margin).   */
lid_bot_z  = base_bot_z;
lid_top_z  = pillar_z0 + sol_stroke + 2;
lid_ceil_z = lid_top_z - lid_top_t;

/* Boss bolt XY positions. Two -X corner bosses; the +X side is left
 * open (no USB-side fastener). Corner bosses inset so the M2
 * counterbore stays inside the sticker recess.                       */
corner_boss_inset = 4;        // inward enough so the boss clears the PCB corner
sticker_c         = (inner - fit_gap)/2 - sticker_d/2 - sticker_margin;
boss_pts = [
    [-inner/2 + corner_boss_inset,   inner/2 - corner_boss_inset     ],
    [-inner/2 + corner_boss_inset, -(inner/2 - corner_boss_inset)    ]
];

function pin_x(i) = pcb_x_edge_plus - pin_from_usb - i*pin_pitch;

/* ======================================================== */
/* Modules                                                    */
/* ======================================================== */

// Pill-shaped USB-C cut through the +X wall.
module usbc_hole() {
    r_short = (usbc_h + 0.6) / 2;
    dy      = (usbc_w - usbc_h) / 2;
    cut_d   = wall + 0.4;
    translate([inner/2 - 0.2, 0, usbc_center_z])
        rotate([0, 90, 0])
            hull()
                for (sy = [-1, 1])
                    translate([0, sy*dy, 0])
                        cylinder(r=r_short, h=cut_d);
}

// One pin-header rail with 8 blind holes drilled from the top.
module pin_rail(row_y) {
    rail_len = (pin_n - 1) * pin_pitch + 2.54;   // = plastic strip length
    rail_cx  = (pin_x(0) + pin_x(pin_n-1)) / 2;
    difference() {
        translate([rail_cx - rail_len/2, row_y - rail_w/2, base_top_z])
            cube([rail_len, rail_w, rail_h]);
        for (i = [0:pin_n-1])
            translate([pin_x(i), row_y,
                       base_top_z + rail_h - pin_depth])
                cylinder(d=pin_hole_d, h=pin_depth + 0.2);
    }
}

// Base plate (closed floor) + solenoid bracket + two pin rails.
module base_part() {
    plate = inner - fit_gap;
    difference() {
        union() {
            // closed floor
            translate([-plate/2, -plate/2, base_bot_z])
                cube([plate, plate, base_t]);
            // bracket wall — starts at base plate top for structural connection
            translate([bracket_back_x, -bracket_y/2, base_top_z])
                cube([bracket_t, bracket_y, bracket_z1 - base_top_z]);
            // pin header rails (one per row)
            pin_rail( pin_row_gap/2);
            pin_rail(-pin_row_gap/2);
        }
        // M2 holes through the bracket (diagonal corners of the
        // 6 x 10 mm rectangle on the solenoid mount face)
        for (pos = [[-sol_mount_rect_w/2,  sol_mount_rect_h/2],
                    [ sol_mount_rect_w/2, -sol_mount_rect_h/2]]) {
            // thread clearance through full bracket thickness
            translate([bracket_back_x - 0.1, pos[0], sol_cz + pos[1]])
                rotate([0, 90, 0])
                    cylinder(d=sol_mount_hole, h=bracket_t + 0.2);
            // counterbore for M2 socket cap head (Ø 4 mm, fully recessed)
            translate([bracket_back_x - 0.1, pos[0], sol_cz + pos[1]])
                rotate([0, 90, 0])
                    cylinder(d=m2_cb_d, h=m2_cb_depth + 0.1);
        }
        // Boss bolt holes: M2 clearance through base plate + counterbore from bottom
        for (pt = boss_pts) {
            // clearance hole through full plate thickness
            translate([pt[0], pt[1], base_bot_z - 0.1])
                cylinder(d=m2_clr_d, h=base_t + 0.2);
            // counterbore for M2 head recessed in base plate bottom
            translate([pt[0], pt[1], base_bot_z - 0.1])
                cylinder(d=m2_cb_d, h=m2_cb_depth + 0.1);
        }
        // Anti-slip sticker recesses (four corners, carved from bottom face)
        for (sx = [-1, 1])
            for (sy = [-1, 1])
                translate([sx*sticker_c, sy*sticker_c, base_bot_z - 0.01])
                    cylinder(d=sticker_d, h=sticker_depth + 0.01);
    }
}

// Square cup lid: open bottom, pillar hole on top, USB-C pill on +X wall,
// boss posts for M2 lid-base bolts.
module lid_part() {
    h = lid_top_z - lid_bot_z;
    union() {
        // Outer shell with inner cavity and feature holes
        difference() {
            translate([-outer/2, -outer/2, lid_bot_z])
                cube([outer, outer, h]);
            translate([-inner/2, -inner/2, lid_bot_z - 0.1])
                cube([inner, inner, h - lid_top_t + 0.1]);
            translate([sol_cx, sol_cy, lid_ceil_z - 0.1])
                cylinder(d=lid_top_hole_d, h=lid_top_t + 0.2);
            usbc_hole();
        }
        // Boss posts — fused to inner walls, self-tapping pilot for M2
        boss_corner( 1);
        boss_corner(-1);
    }
}

// Corner boss: only the cavity-facing corner is a quarter arc; the
// other two sides fuse flat to the two walls meeting at the corner.
// Inset to keep the M2 counterbore inside the sticker recess.
// Single 45° conical ramp from the inner wall corner down to the full
// boss top outline (slabs + cylinder) eliminates overhangs everywhere
// when the lid is printed upside-down (closed top on bed).
// sign_y = +1 for +Y corner, -1 for -Y corner.
module boss_corner(sign_y) {
    cx = -inner/2 + corner_boss_inset;
    cy = sign_y * (inner/2 - corner_boss_inset);
    ry_lo = sign_y > 0 ? cy : -inner/2;
    boss_top_z = base_t + boss_tap_len;
    // Apex height set so even the farthest boss-outline point
    // (cylinder tip on the wall-corner diagonal) sits at ≥ 45°.
    ramp_reach = corner_boss_inset * sqrt(2) + boss_od/2;
    ramp_top_z = boss_top_z + ramp_reach * tan(30);  // 30° slope from horizontal
    apex_y     = sign_y > 0 ? inner/2 - 0.01 : -inner/2;
    difference() {
        union() {
            // slab along -X wall
            translate([-inner/2, cy - boss_od/2, base_t])
                cube([corner_boss_inset, boss_od, boss_tap_len]);
            // slab along ±Y wall
            translate([-inner/2, ry_lo, base_t])
                cube([corner_boss_inset + boss_od/2,
                      corner_boss_inset, boss_tap_len]);
            // cylinder; cavity-facing quadrant is the quarter arc
            translate([cx, cy, base_t])
                cylinder(d=boss_od, h=boss_tap_len);
            // Conical ramp: hull from boss-top outline up to wall corner
            hull() {
                translate([0, 0, boss_top_z])
                    linear_extrude(height=0.01) {
                        translate([-inner/2, cy - boss_od/2])
                            square([corner_boss_inset, boss_od]);
                        translate([-inner/2, ry_lo])
                            square([corner_boss_inset + boss_od/2,
                                    corner_boss_inset]);
                        translate([cx, cy])
                            circle(d=boss_od);
                    }
                translate([-inner/2, apex_y, ramp_top_z - 0.01])
                    cube([0.01, 0.01, 0.01]);
            }
        }
        // Pilot extends through the ramp so the bolt entry stays clear
        translate([cx, cy, base_t - 0.1])
            cylinder(d=boss_tap_d, h=ramp_top_z - base_t + 0.2);
    }
}

// Connection pillar (origin at cup opening / pillar bottom).
module pillar_part() {
    difference() {
        cylinder(d=pillar_od, h=pillar_total);
        translate([0, 0, -0.1])
            cylinder(d=pillar_cup_id, h=pillar_cup_depth + 0.1);
        translate([0, 0, pillar_cup_depth])
            cylinder(d=pillar_bolt_clear,
                     h=pillar_total - pillar_cup_depth
                       - pillar_tap_len + 0.1);
        translate([0, 0, pillar_total - pillar_tap_len])
            cylinder(d=pillar_tap_pilot,
                     h=pillar_tap_len + 0.2);
    }
}

/* -------- Preview mocks -------- */

module mock_solenoid() {
    color("silver") {
        // body
        translate([sol_cx - sol_d/2, sol_cy - sol_w/2, sol_z0])
            cube([sol_d, sol_w, sol_h]);
        // TOP plunger tip (Ø 4 mm rod above body, shown at REST)
        translate([sol_cx, sol_cy, sol_z1])
            cylinder(d=sol_shaft_dia, h=sol_shaft_rest_up);
        // BOTTOM plunger protrusion (below body, at REST)
        translate([sol_cx, sol_cy, sol_z0 - sol_shaft_rest_down])
            cylinder(d=sol_shaft_dia, h=sol_shaft_rest_down);
    }
}

module mock_pcb() {
    color("darkgreen") {
        translate([pcb_center_x - pcb_l/2,
                   pcb_center_y - pcb_w/2, pcb_bot_z])
            cube([pcb_l, pcb_w, pcb_t]);
        // USB-C connector — pill cross-section matching real shell
        translate([pcb_x_edge_plus, 0, pcb_bot_z - usbc_h/2])
            rotate([0, 90, 0])
                hull()
                    for (sy = [-1, 1])
                        translate([0, sy*(usbc_w - usbc_h)/2, 0])
                            cylinder(r=usbc_h/2, h=usbc_protrude);
    }
}

/* ======================================================== */
/* Render                                                     */
/* ======================================================== */

if (part == "base") {
    base_part();
} else if (part == "lid") {
    lid_part();
} else if (part == "pillar") {
    pillar_part();
} else {
    base_part();
    mock_pcb();
    mock_solenoid();
    translate([sol_cx, sol_cy, pillar_z0])
        color("steelblue") pillar_part();
    color("lightblue", 0.9)
        difference() {
            lid_part();
            translate([-outer, 0, lid_bot_z - 1])
                cube([outer*2, outer, lid_top_z - lid_bot_z + 2]);
            // top cut-off so corner bosses and ramps are visible
            translate([-outer, -outer,
                       base_t + boss_tap_len +
                       (corner_boss_inset*sqrt(2) + boss_od/2)*tan(30) + 2])
                cube([outer*2, outer*2, lid_top_z]);
        }
}
