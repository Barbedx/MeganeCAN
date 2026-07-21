# ESP32-WROVER-IB — footprint + symbol specification

Target: custom project library entries `meganecan:ESP32-WROVER-IB` (symbol) and
`meganecan:ESP32-WROVER-IB` (footprint, in `meganecan.pretty/`).
KiCad 10.0.4. There is **no** WROVER symbol or footprint in the official KiCad 10 libraries
(`RF_Module` ships only WROOM-32/32D/32E/32U/32UE, C3-WROOM-02, S2-MINI, S2-WROVER,
S3-WROOM-1/1U/2, C6-MINI). This part must be drawn by hand.

## Source of truth

Espressif **ESP32-WROVER-B & ESP32-WROVER-IB Datasheet v2.2**
(<https://documentation.espressif.com/esp32-wrover-b_datasheet_en.pdf>), specifically:

- §3 Table 3 — Pin Definitions (38 pins + EPAD)
- §9.1 Figure 8 (WROVER-B) / **Figure 9 (WROVER-IB)** — Module Dimensions
- §10.1 **Figure 11 — Recommended PCB Land Pattern** (shared for -B and -IB)

All geometry below was extracted from the **vector objects of Figure 11**, not from OCR of the
drawing: the 38 land pads are real rectangles in the PDF content stream, so pitch, pad size,
row separation and edge offsets are exact, not inferred. Cross-checked against every dimension
label in the figure (1.27 / 0.9 / 1.5 / 17.5 / 22.86 / 7.44 / 1.1 / 6.22 / 3.7 / 7.5 / 16.16 /
18 / 31.4 / 0.5) and against the module Bottom View in Figure 9 (0.46 / 16.16 / 7.5 / 3.5).

Figure 11 is a **top view of the host PCB land pattern**, so its left/right map directly onto
the KiCad F.Cu footprint with no mirroring. -IB is electrically and mechanically identical to -B
except the antenna: -B has the on-module PCB antenna, -IB has an IPEX/U.FL connector. Same PCB,
same 38 pads, same land pattern.

---

## 1. Mechanical summary

| Item | Value |
|---|---|
| Body | 18.00 ±0.15 (X) × 31.40 ±0.15 (Y) × 3.30 ±0.15 mm (H) |
| Pads | 38 castellated, **19 per long side, no bottom row** |
| Pitch | 1.27 mm |
| Land pad size | 1.50 mm (X, radial) × 0.90 mm (Y, along the row) |
| Land pad row spacing | 17.50 mm centre-to-centre → **X = ±8.75 mm** |
| Pad overhang outside module outline | 0.50 mm (pad spans 0.5 mm out / 1.0 mm in) |
| Pad row span | 22.86 mm (= 18 × 1.27) |
| Antenna-end edge → pin 1 centre | 7.44 mm |
| Far edge → pin 19 centre | 1.10 mm |
| Antenna area (from antenna-end edge) | 6.22 mm × full 18 mm width |
| Module EPAD (pin 39) | 3.50 × 3.50 mm |
| Recommended EPAD land array | 3.70 × 3.70 mm overall |
| EPAD centre | 7.50 mm from the left (pin 1..19) edge, 16.16 mm from the far edge |
| EPAD offset from body centre | X = −1.50 mm, Y = −0.46 mm (toward the antenna) |

Consistency check: 7.44 + 22.86 + 1.10 = 31.40 ✔

### Pin 1 orientation

Pins **1 and 38 are both GND and both sit at the antenna end**, adjacent to each other.
Pins 19 and 20 sit at the far (connector) end. Numbering runs 1→19 **down the left side**
starting at the antenna end, then 20→38 **up the right side** from the far end back to the
antenna end. (Figure 11 labels "1"/"38" at the antenna end and "19"/"20" at the far end.)

---

## 2. Footprint origin convention

**Origin (0,0) = geometric centre of the 18 × 31.4 mm body.**
+Y points away from the antenna; the antenna end is at Y = −15.70.

This deliberately differs from KiCad's `ESP32-WROOM-32.kicad_mod`, whose origin sits near the
centre of the side pad rows (its body runs Y −15.74 … +9.76, i.e. the origin is *not* the body
centre). Body-centre origin is chosen here because it makes the WROVER numbers symmetric and
keeps the antenna keep-out arithmetic trivial.

---

## 3. Pad centre coordinates (mm, module origin = body centre)

All 38 signal pads: `smd rect`, size **1.5 × 0.9**, rotation **0**, layers `F.Cu F.Mask F.Paste`.
(Both columns use the same rotation — the pad is 1.5 mm wide in X for both sides, so no `90`
rotation is needed anywhere. This is unlike WROOM-32, whose bottom-row pads are rotated 90°.)

### Left column — X = −8.75, pins 1…19 (pin 1 at the antenna end)

| Pin | Y | Pin | Y | Pin | Y | Pin | Y |
|---|---|---|---|---|---|---|---|
| 1 | −8.26 | 6 | −1.91 | 11 | 4.44 | 16 | 10.79 |
| 2 | −6.99 | 7 | −0.64 | 12 | 5.71 | 17 | 12.06 |
| 3 | −5.72 | 8 | 0.63 | 13 | 6.98 | 18 | 13.33 |
| 4 | −4.45 | 9 | 1.90 | 14 | 8.25 | 19 | 14.60 |
| 5 | −3.18 | 10 | 3.17 | 15 | 9.52 | | |

Formula: `Y(n) = -8.26 + (n-1) * 1.27`, n = 1…19.

### Right column — X = +8.75, pins 20…38 (pin 20 at the far end)

| Pin | Y | Pin | Y | Pin | Y | Pin | Y |
|---|---|---|---|---|---|---|---|
| 20 | 14.60 | 25 | 8.25 | 30 | 1.90 | 35 | −4.45 |
| 21 | 13.33 | 26 | 6.98 | 31 | 0.63 | 36 | −5.72 |
| 22 | 12.06 | 27 | 5.71 | 32 | −0.64 | 37 | −6.99 |
| 23 | 10.79 | 28 | 4.44 | 33 | −1.91 | 38 | −8.26 |
| 24 | 9.52 | 29 | 3.17 | 34 | −3.18 | | |

Formula: `Y(n) = 14.60 - (n-20) * 1.27`, n = 20…38.

### Pad 39 — EPAD (thermal / ground)

- Centre **(−1.50, −0.46)**.
- Module's own EPAD is 3.50 × 3.50 mm; Espressif's recommended land is a 3 × 3 array of
  0.90 × 0.90 mm copper squares on a 1.40 mm grid (overall extent 3.70 × 3.70 mm) with thermal
  vias interleaved between the squares.
- **Recommended implementation for this hand-assembled 2-layer board:** one solid
  `smd rect` 3.5 × 3.5 at (−1.50, −0.46) on `F.Cu F.Mask` with `zone_connect 2`, plus 4 thermal
  vias (0.60 land / 0.30 drill, `pad_prop_heatsink`) at X = −1.50 ± 0.90, Y = −0.46 ± 0.90.
  Do **not** put paste on the full 3.5 mm square if hand-soldering — the module is placed by hand
  and a large paste square floats it. Paste is optional; the datasheet explicitly states
  *"Soldering Pad 39 to the ground of the base board is not a must."*
  If reflowing, use a 3 × 3 paste grid of 1.05 × 1.05 squares on a 1.40 mm pitch (≈65% coverage).

---

## 4. Graphic layers

### F.Fab (0.10 mm)

Body outline with a 1.0 mm chamfer at the pin-1 corner:

```
(-8.00,-15.70) → ( 9.00,-15.70)
( 9.00,-15.70) → ( 9.00, 15.70)
( 9.00, 15.70) → (-9.00, 15.70)
(-9.00, 15.70) → (-9.00,-14.70)
(-9.00,-14.70) → (-8.00,-15.70)      # chamfer = pin-1 indicator
```

Plus `fp_text user "${REFERENCE}"` at (0, 0), size 1×1, and the `Value` property on F.Fab at
(0, 17.5).

### F.SilkS (0.12 mm) — body edge offset +0.12 → ±9.12 / ±15.82

Silk may only run at X = ±9.12 where there are no pads. Pads occupy Y −8.71 … +15.05 at that X.

```
(-9.12,-15.82) → ( 9.12,-15.82)      # antenna-end edge
(-9.12,-15.82) → (-9.12, -8.96)      # left, antenna region only
( 9.12,-15.82) → ( 9.12, -8.96)      # right, antenna region only
(-9.12, 15.82) → ( 9.12, 15.82)      # far edge
(-9.12, 15.30) → (-9.12, 15.82)      # short corner stub
( 9.12, 15.30) → ( 9.12, 15.82)      # short corner stub
```

Pin-1 marker: filled `fp_poly` triangle, pts (−9.62,−8.96) (−9.12,−8.96) (−9.12,−9.46).

Reference designator on F.SilkS at (−10.9, 8.0) rotation 90.

### F.CrtYd (0.05 mm)

Rectangle (−9.75, −15.95) → (+9.75, +15.95).
(Pads reach X = ±9.50, body reaches Y = ±15.70; +0.25 clearance on the larger of the two.)

### Antenna keep-out rule area

`zone` on all copper layers with `keepout` (tracks / vias / pads / copperpour / footprints all
`not_allowed`), polygon:

```
(-24.0, -9.48) (24.0, -9.48) (24.0, -30.48) (-24.0, -30.48)
```

Y = −9.48 is the antenna-area boundary (−15.70 + 6.22). The zone covers the antenna area itself
plus ~14.8 mm beyond the module edge, matching the arms KiCad's WROOM-32 footprint draws.

**-IB caveat:** the -IB has no on-module PCB antenna, so this keep-out is *not* an RF requirement
for our variant — it is inherited from the shared Figure 11. For this board it can be relaxed to
a mechanical clearance for the IPEX connector and its pigtail. Keep the zone in the footprint
(harmless, documents intent) but expect to override/delete it in the layout if board area is
tight. Do not put the WROVER's own decoupling under the antenna area regardless.

### 3D model

None available in the KiCad 10 libraries. Leave the `model` block out, or point at a
locally-downloaded STEP. Espressif publishes STEP files with the datasheet source files.

---

## 5. Full 38-pin table (+ EPAD)

Verified against Table 3 of the v2.2 datasheet. **GPIO16 and GPIO17 are consumed internally by
the PSRAM on WROVER modules and are NOT brought out** — pads 27 and 28, which are IO16 and IO17
on WROOM-32, are `NC1` / `NC2` here.

| Pin | Name | Type | Function / notes |
|---:|---|---|---|
| 1 | GND | power_in | Ground (antenna end) |
| 2 | 3V3 | power_in | 3.0–3.6 V supply |
| 3 | EN | input | Module enable, active high. **Needs 10k pull-up to 3V3** |
| 4 | SENSOR_VP | input | GPIO36, ADC1_CH0, RTC_GPIO0 — **input only** |
| 5 | SENSOR_VN | input | GPIO39, ADC1_CH3, RTC_GPIO3 — **input only** |
| 6 | IO34 | input | GPIO34, ADC1_CH6, RTC_GPIO4 — **input only** |
| 7 | IO35 | input | GPIO35, ADC1_CH7, RTC_GPIO5 — **input only** |
| 8 | IO32 | bidi | GPIO32, XTAL_32K_P, ADC1_CH4, TOUCH9 |
| 9 | IO33 | bidi | GPIO33, XTAL_32K_N, ADC1_CH5, TOUCH8 |
| 10 | IO25 | bidi | GPIO25, DAC_1, ADC2_CH8, RTC_GPIO6, EMAC_RXD0 |
| 11 | IO26 | bidi | GPIO26, DAC_2, ADC2_CH9, RTC_GPIO7, EMAC_RXD1 |
| 12 | IO27 | bidi | GPIO27, ADC2_CH7, TOUCH7, RTC_GPIO17, EMAC_RX_DV |
| 13 | IO14 | bidi | GPIO14, ADC2_CH6, TOUCH6, MTMS, HSPICLK, HS2_CLK |
| 14 | IO12 | bidi | GPIO12, ADC2_CH5, TOUCH5, MTDI, HSPIQ — **strap: must be LOW at reset, leave floating, NO pull-up** |
| 15 | GND | power_in | Ground |
| 16 | IO13 | bidi | GPIO13, ADC2_CH4, TOUCH4, MTCK, HSPID, HS2_DATA3 |
| 17 | SHD/SD2 | bidi | GPIO9 — **internal SPI flash/PSRAM, do not use** |
| 18 | SWP/SD3 | bidi | GPIO10 — **internal SPI flash/PSRAM, do not use** |
| 19 | SCS/CMD | bidi | GPIO11 — **internal SPI flash, do not use** |
| 20 | SCK/CLK | bidi | GPIO6 — **internal SPI flash, do not use** |
| 21 | SDO/SD0 | bidi | GPIO7 — **internal SPI flash, do not use** |
| 22 | SDI/SD1 | bidi | GPIO8 — **internal SPI flash, do not use** |
| 23 | IO15 | bidi | GPIO15, ADC2_CH3, TOUCH3, MTDO, HSPICS0 — strap (bootlog silence) |
| 24 | IO2 | bidi | GPIO2, ADC2_CH2, TOUCH2, HSPIWP — **strap: must be LOW/floating at reset**, safe for a status LED to GND |
| 25 | IO0 | bidi | GPIO0, ADC2_CH1, TOUCH1, CLK_OUT1 — **boot strap: 10k pull-up + button to GND** |
| 26 | IO4 | bidi | GPIO4, ADC2_CH0, TOUCH0, HSPIHD, HS2_DATA1 |
| 27 | NC1 | no_connect | *WROOM-32 has IO16 here.* GPIO16 = PSRAM CS on WROVER — **not exposed** |
| 28 | NC2 | no_connect | *WROOM-32 has IO17 here.* GPIO17 = PSRAM CLK on WROVER — **not exposed** |
| 29 | IO5 | bidi | GPIO5, VSPICS0, EMAC_RX_CLK — strap (drives at boot) |
| 30 | IO18 | bidi | GPIO18, VSPICLK, HS1_DATA7 |
| 31 | IO19 | bidi | GPIO19, VSPIQ, U0CTS, EMAC_TXD0 |
| 32 | NC | no_connect | Not connected (also NC on WROOM-32) |
| 33 | IO21 | bidi | GPIO21, VSPIHD, EMAC_TX_EN |
| 34 | RXD0 | bidi | GPIO3, U0RXD, CLK_OUT2 |
| 35 | TXD0 | bidi | GPIO1, U0TXD, CLK_OUT3, EMAC_RXD2 |
| 36 | IO22 | bidi | GPIO22, VSPIWP, U0RTS, EMAC_TXD1 |
| 37 | IO23 | bidi | GPIO23, VSPID, HS1_STROBE |
| 38 | GND | power_in | Ground (antenna end) |
| 39 | GND (EPAD) | power_in | Thermal pad. Grounding is recommended but not mandatory |

Usable GPIO set on this module (flash pins and NC removed):
`0, 2, 4, 5, 12, 13, 14, 15, 18, 19, 21, 22, 23, 25, 26, 27, 32, 33` (bidirectional) and
`34, 35, 36, 39` (input-only). GPIO16/17 do **not** exist on the WROVER.

### Cross-check against the MeganeCAN GW firmware

| Firmware signal | GPIO | WROVER pin | OK |
|---|---|---|---|
| Vehicle CAN RX (`gw_main.cpp`) | 21 | 33 | ✔ |
| Vehicle CAN TX | 22 | 36 | ✔ |
| HU/canbox UART1 TX | 25 | 10 | ✔ |
| HU/canbox UART1 RX | 26 | 11 | ✔ |
| Link UART2 TX (`GwLink.cpp`) → C3 GPIO20 | 18 | 30 | ✔ |
| Link UART2 RX ← C3 GPIO21 | 19 | 31 | ✔ |
| ACC ignition sense (47k/10k divider) | 34 | 6 | ✔ input-only, ADC1_CH6 — correct choice |
| Status LED | 2 | 24 | ✔ safe strap (LED to GND) |
| Boot button | 0 | 25 | ✔ |
| Reset button | EN | 3 | ✔ |
| USB-serial flash jig (J_JIG) | TXD0/RXD0 | 35 / 34 | ✔ |
| J_EXP spares 4,5,13,14,23,27,32,33 | — | 26, 29, 16, 13, 37, 12, 8, 9 | ✔ all exist |

No conflicts. GPIO12 (pin 14) must stay floating — do not route it to J_EXP.

---

## 6. Differences vs the official KiCad `RF_Module:ESP32-WROOM-32` footprint

Read `C:\Program Files\KiCad\10.0\share\kicad\footprints\RF_Module.pretty\ESP32-WROOM-32.kicad_mod`
for the KiCad 10 s-expression syntax (`(version 20260206)`, `(generator "pcbnew")`,
`(generator_version "10.0")`, `(attr smd)`, `(duplicate_pad_numbers_are_jumpers no)`, per-object
`(uuid …)`). Copy the syntax; **do not** copy the geometry. What differs:

| | ESP32-WROOM-32 | ESP32-WROVER-B / -IB |
|---|---|---|
| **Pad arrangement** | **14 left (1–14) + 10 bottom (15–24) + 14 right (25–38)** | **19 left (1–19) + 19 right (20–38), no bottom row** |
| Pad rotation | bottom row pads rotated 90° | all pads rotation 0 |
| Body | 18.0 × 25.5 × 3.1 mm | 18.0 × 31.4 × 3.3 mm |
| Pitch | 1.27 | 1.27 (same) |
| Pad size | 1.5 × 0.9 | 1.5 × 0.9 (same) |
| Side row X | ±8.75 | ±8.75 (same) |
| Side row Y range (its own origin) | −8.25 … +8.26 | −8.26 … +14.60 (body-centre origin) |
| Bottom row | Y = +9.51, X −5.71 … +5.72 | none |
| Antenna area depth | ≈6.0 mm | 6.22 mm |
| Keep-out zone | (−24,−9.8)…(24,−30.74) | (−24,−9.48)…(24,−30.48) |
| EPAD | 4.2 × 4.2 at (−0.68, −0.91) | 3.5 × 3.5 (land array 3.7 × 3.7) at (−1.50, −0.46) |
| Pin 27 | IO16 | **NC1** (PSRAM) |
| Pin 28 | IO17 | **NC2** (PSRAM) |
| Origin | near side-pad-row centre, not body centre | body centre |
| Pins 4/5 names | SENSOR_VP / SENSOR_VN | same |
| 3D model | `RF_Module.3dshapes/ESP32-WROOM-32.step` | none shipped |

The one change that will silently ruin the board if missed is the **pad arrangement**: a WROVER
is a two-row 19+19 part. Do not start from the WROOM-32 footprint and stretch it.

Note also that `RF_Module:ESP32-S2-WROVER` has the same 18 × 31.4 body but is a **42-pin,
1.5 mm-pitch** part — it is *not* a usable mechanical donor either.

---

## 7. Symbol specification (`meganecan:ESP32-WROVER-IB`)

Single unit, single body style. Grid 2.54 mm. Body rectangle **(−15.24, −25.40) → (15.24, 33.02)**,
`stroke width 0.254`, `fill type background`. Pin length 5.08, name/number text 1.27.

Properties:
- `Reference` = `U`, at (−15.24, 36.83), justify left
- `Value` = `ESP32-WROVER-IB`, at (1.27, 36.83), justify left
- `Footprint` = `meganecan:ESP32-WROVER-IB`, hidden
- `Datasheet` = `https://documentation.espressif.com/esp32-wrover-b_datasheet_en.pdf`, hidden
- `Description` = `RF Module, ESP32-D0WD SoC, 8 MB PSRAM, Wi-Fi 802.11b/g/n, Bluetooth/BLE, 3.0-3.6V, IPEX antenna connector, SMD`, hidden
- `ki_keywords` = `RF Radio ESP ESP32 Espressif WROVER PSRAM WiFi BT BLE IPEX`
- `ki_fp_filters` = `ESP32?WROVER*`

### Left side — X = −20.32, rotation 0

| Y | Pin | Name | Type |
|---|---|---|---|
| 30.48 | 3 | EN | input |
| 25.40 | 4 | SENSOR_VP | input |
| 22.86 | 5 | SENSOR_VN | input |
| 20.32 | 6 | IO34 | input |
| 17.78 | 7 | IO35 | input |
| 12.70 | 17 | SHD/SD2 | bidirectional |
| 10.16 | 18 | SWP/SD3 | bidirectional |
| 7.62 | 19 | SCS/CMD | bidirectional |
| 5.08 | 20 | SCK/CLK | bidirectional |
| 2.54 | 21 | SDO/SD0 | bidirectional |
| 0.00 | 22 | SDI/SD1 | bidirectional |
| −5.08 | 27 | NC1 | no_connect |
| −7.62 | 28 | NC2 | no_connect |
| −10.16 | 32 | NC | no_connect |

### Right side — X = +20.32, rotation 180

| Y | Pin | Name | Type |
|---|---|---|---|
| 30.48 | 25 | IO0 | bidirectional |
| 27.94 | 24 | IO2 | bidirectional |
| 25.40 | 26 | IO4 | bidirectional |
| 22.86 | 29 | IO5 | bidirectional |
| 20.32 | 14 | IO12 | bidirectional |
| 17.78 | 16 | IO13 | bidirectional |
| 15.24 | 13 | IO14 | bidirectional |
| 12.70 | 23 | IO15 | bidirectional |
| 10.16 | 30 | IO18 | bidirectional |
| 7.62 | 31 | IO19 | bidirectional |
| 5.08 | 33 | IO21 | bidirectional |
| 2.54 | 36 | IO22 | bidirectional |
| 0.00 | 37 | IO23 | bidirectional |
| −2.54 | 10 | IO25 | bidirectional |
| −5.08 | 11 | IO26 | bidirectional |
| −7.62 | 12 | IO27 | bidirectional |
| −10.16 | 8 | IO32 | bidirectional |
| −12.70 | 9 | IO33 | bidirectional |
| −17.78 | 35 | TXD0 | bidirectional |
| −20.32 | 34 | RXD0 | bidirectional |

### Power — top and bottom

| X | Y | Rot | Pin | Name | Type |
|---|---|---|---|---|---|
| 0.00 | 38.10 | 270 | 2 | 3V3 | power_in |
| −7.62 | −30.48 | 90 | 1 | GND | power_in |
| −2.54 | −30.48 | 90 | 15 | GND | power_in |
| 2.54 | −30.48 | 90 | 38 | GND | power_in |
| 7.62 | −30.48 | 90 | 39 | GND | power_in |

All four GND pins are drawn individually (not stacked/hidden) so the schematic shows every
ground the module actually needs. If ERC complains about four `power_in` pins with no driver,
that is the normal "power flag" case, not a symbol defect.

---

## 8. Machine-readable spec

```json
{
  "part": "ESP32-WROVER-IB",
  "variant_note": "-IB = -B with IPEX/U.FL antenna connector; identical PCB, pads and land pattern",
  "datasheet": "https://documentation.espressif.com/esp32-wrover-b_datasheet_en.pdf",
  "datasheet_version": "v2.2",
  "geometry_source": "Figure 11 Recommended PCB Land Pattern (vector extraction) + Figure 9 Module Dimensions",
  "units": "mm",
  "origin": "body geometric center; +Y away from antenna; antenna end at Y=-15.70",
  "body": { "size_x": 18.0, "size_y": 31.4, "height": 3.3, "tol": 0.15 },
  "pads": {
    "count": 38,
    "arrangement": "19 left + 19 right, no bottom row",
    "pitch": 1.27,
    "land_size_x": 1.5,
    "land_size_y": 0.9,
    "row_spacing_x": 17.5,
    "row_center_x": 8.75,
    "overhang_outside_body": 0.5,
    "row_span_y": 22.86,
    "antenna_edge_to_pin1_center": 7.44,
    "far_edge_to_pin19_center": 1.1,
    "shape": "rect",
    "rotation": 0,
    "layers": ["F.Cu", "F.Mask", "F.Paste"]
  },
  "pad_coordinates": [
    {"n":1,"x":-8.75,"y":-8.26},{"n":2,"x":-8.75,"y":-6.99},{"n":3,"x":-8.75,"y":-5.72},
    {"n":4,"x":-8.75,"y":-4.45},{"n":5,"x":-8.75,"y":-3.18},{"n":6,"x":-8.75,"y":-1.91},
    {"n":7,"x":-8.75,"y":-0.64},{"n":8,"x":-8.75,"y":0.63},{"n":9,"x":-8.75,"y":1.90},
    {"n":10,"x":-8.75,"y":3.17},{"n":11,"x":-8.75,"y":4.44},{"n":12,"x":-8.75,"y":5.71},
    {"n":13,"x":-8.75,"y":6.98},{"n":14,"x":-8.75,"y":8.25},{"n":15,"x":-8.75,"y":9.52},
    {"n":16,"x":-8.75,"y":10.79},{"n":17,"x":-8.75,"y":12.06},{"n":18,"x":-8.75,"y":13.33},
    {"n":19,"x":-8.75,"y":14.60},
    {"n":20,"x":8.75,"y":14.60},{"n":21,"x":8.75,"y":13.33},{"n":22,"x":8.75,"y":12.06},
    {"n":23,"x":8.75,"y":10.79},{"n":24,"x":8.75,"y":9.52},{"n":25,"x":8.75,"y":8.25},
    {"n":26,"x":8.75,"y":6.98},{"n":27,"x":8.75,"y":5.71},{"n":28,"x":8.75,"y":4.44},
    {"n":29,"x":8.75,"y":3.17},{"n":30,"x":8.75,"y":1.90},{"n":31,"x":8.75,"y":0.63},
    {"n":32,"x":8.75,"y":-0.64},{"n":33,"x":8.75,"y":-1.91},{"n":34,"x":8.75,"y":-3.18},
    {"n":35,"x":8.75,"y":-4.45},{"n":36,"x":8.75,"y":-5.72},{"n":37,"x":8.75,"y":-6.99},
    {"n":38,"x":8.75,"y":-8.26}
  ],
  "epad": {
    "number": 39,
    "module_pad_size": [3.5, 3.5],
    "recommended_land_extent": [3.7, 3.7],
    "center": [-1.5, -0.46],
    "center_from_left_edge": 7.5,
    "center_from_far_edge": 16.16,
    "datasheet_land": "3x3 array of 0.9x0.9 copper squares on 1.4 mm grid with interleaved thermal vias",
    "recommended_impl": {
      "pad": {"shape":"rect","size":[3.5,3.5],"layers":["F.Cu","F.Mask"],"zone_connect":2},
      "vias": {"count":4,"land":0.6,"drill":0.3,"positions":[[-2.4,-1.36],[-0.6,-1.36],[-2.4,0.44],[-0.6,0.44]]},
      "paste_optional": true
    },
    "mandatory": false
  },
  "antenna_area": { "depth_from_antenna_edge": 6.22, "y_boundary": -9.48, "width": 18.0 },
  "keepout_zone": { "polygon": [[-24.0,-9.48],[24.0,-9.48],[24.0,-30.48],[-24.0,-30.48]],
                    "note": "inherited from shared -B/-IB figure; not an RF requirement for -IB (external antenna)" },
  "courtyard": { "x": [-9.75, 9.75], "y": [-15.95, 15.95] },
  "fab_outline": [[-8.0,-15.7],[9.0,-15.7],[9.0,15.7],[-9.0,15.7],[-9.0,-14.7],[-8.0,-15.7]],
  "silk_segments": [
    [[-9.12,-15.82],[9.12,-15.82]],
    [[-9.12,-15.82],[-9.12,-8.96]],
    [[9.12,-15.82],[9.12,-8.96]],
    [[-9.12,15.82],[9.12,15.82]],
    [[-9.12,15.30],[-9.12,15.82]],
    [[9.12,15.30],[9.12,15.82]]
  ],
  "silk_pin1_triangle": [[-9.62,-8.96],[-9.12,-8.96],[-9.12,-9.46]],
  "pins": [
    {"n":1,"name":"GND","type":"power_in","gpio":null},
    {"n":2,"name":"3V3","type":"power_in","gpio":null},
    {"n":3,"name":"EN","type":"input","gpio":null},
    {"n":4,"name":"SENSOR_VP","type":"input","gpio":36,"input_only":true},
    {"n":5,"name":"SENSOR_VN","type":"input","gpio":39,"input_only":true},
    {"n":6,"name":"IO34","type":"input","gpio":34,"input_only":true},
    {"n":7,"name":"IO35","type":"input","gpio":35,"input_only":true},
    {"n":8,"name":"IO32","type":"bidirectional","gpio":32},
    {"n":9,"name":"IO33","type":"bidirectional","gpio":33},
    {"n":10,"name":"IO25","type":"bidirectional","gpio":25},
    {"n":11,"name":"IO26","type":"bidirectional","gpio":26},
    {"n":12,"name":"IO27","type":"bidirectional","gpio":27},
    {"n":13,"name":"IO14","type":"bidirectional","gpio":14},
    {"n":14,"name":"IO12","type":"bidirectional","gpio":12,"strap":"must be low at reset; leave floating"},
    {"n":15,"name":"GND","type":"power_in","gpio":null},
    {"n":16,"name":"IO13","type":"bidirectional","gpio":13},
    {"n":17,"name":"SHD/SD2","type":"bidirectional","gpio":9,"reserved":"internal flash/PSRAM"},
    {"n":18,"name":"SWP/SD3","type":"bidirectional","gpio":10,"reserved":"internal flash/PSRAM"},
    {"n":19,"name":"SCS/CMD","type":"bidirectional","gpio":11,"reserved":"internal flash"},
    {"n":20,"name":"SCK/CLK","type":"bidirectional","gpio":6,"reserved":"internal flash"},
    {"n":21,"name":"SDO/SD0","type":"bidirectional","gpio":7,"reserved":"internal flash"},
    {"n":22,"name":"SDI/SD1","type":"bidirectional","gpio":8,"reserved":"internal flash"},
    {"n":23,"name":"IO15","type":"bidirectional","gpio":15,"strap":"boot log silence"},
    {"n":24,"name":"IO2","type":"bidirectional","gpio":2,"strap":"low/floating at reset; LED-to-GND safe"},
    {"n":25,"name":"IO0","type":"bidirectional","gpio":0,"strap":"10k pullup + boot button to GND"},
    {"n":26,"name":"IO4","type":"bidirectional","gpio":4},
    {"n":27,"name":"NC1","type":"no_connect","gpio":null,"wroom32_equivalent":"IO16","reason":"GPIO16 used by PSRAM"},
    {"n":28,"name":"NC2","type":"no_connect","gpio":null,"wroom32_equivalent":"IO17","reason":"GPIO17 used by PSRAM"},
    {"n":29,"name":"IO5","type":"bidirectional","gpio":5,"strap":"drives at boot"},
    {"n":30,"name":"IO18","type":"bidirectional","gpio":18},
    {"n":31,"name":"IO19","type":"bidirectional","gpio":19},
    {"n":32,"name":"NC","type":"no_connect","gpio":null},
    {"n":33,"name":"IO21","type":"bidirectional","gpio":21},
    {"n":34,"name":"RXD0","type":"bidirectional","gpio":3},
    {"n":35,"name":"TXD0","type":"bidirectional","gpio":1},
    {"n":36,"name":"IO22","type":"bidirectional","gpio":22},
    {"n":37,"name":"IO23","type":"bidirectional","gpio":23},
    {"n":38,"name":"GND","type":"power_in","gpio":null},
    {"n":39,"name":"GND","type":"power_in","gpio":null,"epad":true}
  ],
  "diff_vs_kicad_wroom32_footprint": {
    "pad_arrangement": "WROOM 14+10+14 with a bottom row; WROVER 19+19 with none",
    "bottom_row_rotation": "WROOM bottom pads rotated 90; WROVER has no rotated pads",
    "body_y": "25.5 -> 31.4",
    "body_height": "3.1 -> 3.3",
    "epad": "4.2x4.2 @ (-0.68,-0.91) -> 3.5x3.5 @ (-1.5,-0.46)",
    "antenna_depth": "~6.0 -> 6.22",
    "pin27": "IO16 -> NC1",
    "pin28": "IO17 -> NC2",
    "origin": "WROOM origin is near the side-pad-row center, not the body center; this spec uses the body center",
    "unchanged": ["pitch 1.27", "pad 1.5x0.9", "side row X +/-8.75", "pin1 at antenna end", "pins 1/38 GND"]
  }
}
```

---

## 9. Known uncertainties

1. **`1.08` on the Bottom View (Figure 9)** — an unattributed dimension next to `38 x 0.45`
   (castellation half-hole, 0.9 mm diameter) and `38 x 0.85` (module-side pad land length).
   It most likely dimensions the module's own pad/castellation geometry. It does **not** affect
   the land pattern, which Espressif specifies directly in Figure 11 (1.5 × 0.9 @ ±8.75), so this
   is documented for completeness only.
2. **EPAD size 3.5 vs 3.7** — 3.5 × 3.5 is read from the Figure 9 Bottom View (the module's own
   pad); 3.7 × 3.7 is the overall extent of the recommended land array in Figure 11. Both are
   used above in the roles stated. If you want a single number, 3.5 is the conservative one.
3. **EPAD thermal-via pattern** — Figure 11 draws 9 copper squares (0.9 mm, 1.4 mm grid) plus 12
   small circles at the grid midpoints. Reading the circles as via drills gives an implausibly
   small ~0.25 mm; they are more likely a not-to-scale via symbol. The recommended
   implementation in §3 substitutes a conventional solid pad + 4 vias, which is fine because
   grounding the EPAD is optional per the datasheet.
4. **-IB IPEX connector position and height** — Figure 9 carries `-IB`-only dimensions `2.12`
   and `23.05` that were not unambiguously attributed; the side view also shows `3.5` where the
   module body is `3.3`. Before finalising the enclosure/stack-up, confirm the U.FL connector's
   XY position and the mated-cable height directly from Figure 9 (allow ~6.5 mm total height for
   a mated pigtail). This does not affect the footprint.
5. **Antenna keep-out for -IB** — the shared Figure 11 keep-out is drawn for the PCB-antenna -B
   variant. Treated here as advisory for -IB (see §4). Confirm against datasheet §10.2 if the
   layout ends up needing that area.
6. **No 3D model** — none is available from the KiCad libraries; a STEP must be pulled from
   Espressif if a 3D check is wanted.
7. **NRND** — the datasheet is watermarked *"Not Recommended For New Designs"*. The module is
   still widely available and is what this project already has in hand; noted so it is not a
   surprise later.
