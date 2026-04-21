# Housing design — `housing.scad`

Enclosure for a 0420T solenoid + ESP32-C3 Super Mini. When the
solenoid fires, a connection pillar lifts the M2 threads on its top
by 4 mm, moving whatever is screwed onto them (the Claude model).

## Parts

| Part     | Role |
| -------- | ---- |
| `base`   | Floor plate, solenoid bracket on −X, pin-header rails, bolt-head counterbores, sticker pockets. |
| `lid`    | Square cup. Pillar hole on top, USB-C pill on +X, three self-tap bosses inside. |
| `pillar` | Captive rod between plunger tip and the M2 thread. |

Render a specific part with `part = "base" | "lid" | "pillar" | "all"`.

## Hardware

| Item                     | Value          |
| ------------------------ | -------------- |
| Solenoid body (W × D × H)| 11.2 × 12.2 × 20.5 mm |
| Plunger                  | Ø 4 mm, 12.8 mm below / 4.5 mm above at rest, 4 mm stroke (up) |
| Solenoid M2 mount        | 6 × 10 rect on the 11.2 × 20.5 face, thread depth ≤ 1.5 mm |
| PCB (L × W × t)          | 22.8 × 17.9 × 1 mm, component side DOWN, +X edge flush with wall |
| USB-C receptacle         | 8.94 × 3.26 mm (IEC 62680-1-3), protrudes 1.5 mm past PCB +X edge |
| Pin headers              | 2 rows × 8 pins, 2.54 mm pitch, 15 mm between rows, 6 mm pin length |

## Connection pillar

Stepped through-hole cylinder; Ø 7.5 mm, 21 mm tall.

| Section    | Z range  | Inner Ø | Notes |
| ---------- | -------- | ------- | ----- |
| Cup        | 0 .. 3   | 4.3 mm  | Wraps the plunger, 2 mm engagement at rest |
| Bolt clear | 3 .. 18  | 3.9 mm  | M2 head (Ø 3.8) passes freely |
| Tap pilot  | 18 .. 21 | 2.0 mm  | M2 self-taps |

An M2 × ≥ 21 bolt is driven threads-first from the cup side; the
exposed thread length above the pillar top is what the model screws
onto. At rest the pillar body protrudes 15 mm above the lid top;
at full stroke, 19 mm.

## Lid-base bolting

Two M2 × 5.8 mm self-tapping bolts (head-up-from-below) at the −X/±Y
corners. Each boss is inset 5 mm from both walls so its Ø 4 mm
counterbore stays inside the Ø 10.5 mm sticker pocket. The +X side
is left free (no fastener near the USB port).

| Feature               | Value            |
| --------------------- | ---------------- |
| Clearance hole (base) | Ø 2.2 mm         |
| Head counterbore      | Ø 4.0 × 2.5 mm (M2 head fully buried; 1.5 mm material above it) |
| Boss outer Ø          | 6 mm             |
| Boss height           | 4 mm             |
| Tap pilot             | Ø 2.0 mm         |

With `base_t = 4`, a 5.8 mm bolt leaves 4.3 mm of shank past the
plate — fully engaged in the corner bosses.

### Corner boss printability

When the lid prints upside-down (closed top on bed), each corner
boss is supported by a single **30° conical ramp** hulled from the
wall corner above down to the full boss top outline. The M2 pilot
is drilled through the ramp so the bolt entry stays clear.

## Solenoid bracket

Flush with the −X inner wall (`bolt_clear = 0`), 6.8 mm thick:

    bracket_t = sol_bolt_len − sol_thread_depth + m2_cb_depth
              = 5.8 − 1.5 + 2.5 = 6.8 mm

A 5.8 mm M2 seats in a Ø 4.0 × 2.5 mm counterbore on the back face
and threads exactly 1.5 mm into the solenoid. Bracket is 12 mm wide
in Y, so the pin-header rows at Y = ±7.5 clear it with 1 mm gap.
No gussets.

## Anti-slip stickers

Four Ø 10.5 × 1 mm pockets on the base-plate bottom, offset 1 mm
from the plate edge. The two −X bolt counterbores sit inside their
matching pockets with ~1.2 mm radial margin.

## Stackup

Inner 30 × 30 mm, outer 33 × 33 mm (`wall = 1.5`). Base plate 4 mm.
Lid is a cup that drops over the base (`fit_gap = 0.4`, 0.2 mm per
side). Z = 0 is the bottom of both parts.

```
pillar top @max   =  75.3   (= pillar_z1 + stroke)             +Z
pillar top @rest  =  71.3   ← 15 mm above lid outer top
lid outer top     =  56.3
lid inner ceiling =  54.3
plunger tip @rest =  52.3
pillar bottom     =  50.3   (2 mm plunger engagement)
solenoid top      =  47.8
solenoid bottom   =  27.3   (1 mm plunger clearance above PCB)
plunger bot @rest =  14.5
PCB top           =  13.5
PCB bottom        =  12.5
USB-C bottom face =   9.24
base plate top    =   4.0
base plate bottom =   0
lid bottom (open) =   0
```

The lid's top hole is centred on the plunger axis `(sol_cx, sol_cy)
= (−2.1, 0)` — the lid is NOT symmetric in X. The USB-C pill on the
+X wall is centred at `(y, z) = (0, 10.87)`, 9.54 × 3.86 mm
(receptacle + 0.6 mm tolerance).

## Assembly

1. Solder two 1×8 pin headers to the ESP32 (pins pointing up, away
   from USB-C).
2. Seat the headers into the base-plate rails; PCB ends up 8.5 mm
   above the plate.
3. Bolt the solenoid to the bracket with two M2 × 5.8 mm from the
   −X side (heads buried in the back-face counterbores).
4. Drop the pillar onto the plunger tip (cup down) and drive a long
   M2 (≥ 21 mm) threads-first through the pillar until the desired
   length sticks out the top.
5. Lower the lid over the base — pillar enters the 7.9 mm top hole,
   USB-C enters the pill opening, bosses land on the two −X bolt
   holes.
6. Drive two M2 × 5.8 mm up from the base-plate bottom into the
   bosses.
7. Stick the four Ø 10.5 mm pads into the bottom pockets.
8. Screw the Claude model onto the exposed pillar threads.

## Print orientation

- **base**: right-side-up (floor on bed).
- **lid**: upside-down (closed top on bed). Corner bosses self-support
  via their 30° ramps.
- **pillar**: either end down.
