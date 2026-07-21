# MeganeCAN GW — Power Section Detail Design

Board: `hardware/meganecan-gw` (JLCPCB 2-layer, hand-assembled).
Loads: ESP32-WROVER-IB (3V3), ESP32-C3 SuperMini daughterboard (5V pin, own LDO),
2× SN65HVD230 CAN transceivers (3V3).
Source: automotive 12V (BAT + ACC + GND screw terminal from the radio harness).

> **Headline:** the two-stage tree specified in ARCHITECTURE-V2 CR-06
> (Mini-360 → 5V → AMS1117-3.3 → 3V3) **does not close thermally or on current**
> at automotive ambient. §6 has the numbers, §7 the single recommended fix.
> §1–§5 document the as-specified design so the delta is auditable.

---

## 1. Mini-360 (MP2307) buck module — mechanical + footprint

### 1.1 Web-verified facts

| Item | Value | Confidence |
|---|---|---|
| PCB outline | **17.0 × 11.0 × 4.0 mm** (some vendors quote 17.9 × 12.0) | Medium — vendor listings agree on 17×11, no drawing |
| Terminals | 4 plated pads at the **board corners**, silkscreen `IN+ IN- OUT+ OUT-` | High |
| Input range | 4.75 – 23 V | High |
| Output | 1 – 17 V adjustable (multiturn pot) | High |
| Rated Iout | 1.8 A "max" (3 A peak claimed by sellers) | High (as *claimed*) |
| **Measured Iout** | **trips/cycles at ≥750 mA open-air**; practical safe limit ~500 mA | High — independent bench test (Gough Lui) |
| Quiescent | up to 60 mA | High |
| **Pad coordinates** | **NOT PUBLISHED** | — |

**Critical sourcing finding:** there is no dimensional drawing for this module anywhere.
The most detailed independent teardown explicitly notes the mounting
*"holes don't seem to line up in mils or mm"* with *"no accurate dimensions online."*
Any 4-pad footprint published as exact coordinates is someone's caliper reading of
*their* unit, and units vary between the many unbranded factories that clone this board.

Therefore the footprint below is specified as **PROVISIONAL-nominal + error budget**,
not as a datasheet transcription. Two mitigations are baked in.

### 1.2 Footprint `MINI360_4pad_Socket` (project lib `meganecan.pretty`)

Nominal pad-centre rectangle derived from a 17.0 × 11.0 outline with corner pads
inset ~1.4 mm to centre: **14.2 mm × 8.2 mm**, origin at module centre.

| Pad | Net | X (mm) | Y (mm) | Drill | Pad Ø | Shape |
|---|---|---|---|---|---|---|
| 1 | `VIN_SW` (IN+) | **−7.10** | **−4.10** | 2.20 | 3.60 | rect (pin-1 marker) |
| 2 | `GND`   (IN−) | **−7.10** | **+4.10** | 2.20 | 3.60 | circle |
| 3 | `V5_BUCK` (OUT+) | **+7.10** | **−4.10** | 2.20 | 3.60 | circle |
| 4 | `GND`   (OUT−) | **+7.10** | **+4.10** | 2.20 | 3.60 | circle |

- Courtyard: 19.0 × 13.0 mm rect (`F.CrtYd`, 0.05 line).
- Fab outline: 17.0 × 11.0 mm (`F.Fab`); silk only on the two short edges so the
  module body can overhang without covering silk.
- **Error budget:** 2.20 mm drill against a ~1.0 mm module pin absorbs **±0.6 mm**
  of positional error per pad. This is deliberate and is why the drill looks huge.
- Mount with 4 short lengths of 0.8 mm solid wire or cut-down 2.54 header pins
  soldered through both boards (the module has no header — you make the pins).

**Mitigation A (required before fab):** measure the actual module with calipers and
patch the four X/Y values. This is a 2-minute edit and removes all remaining risk.

**Mitigation B (belt-and-braces, keep it):** place `J_BUCK`, a
`PinHeader_1x04_P2.54mm_Vertical` footprint, on the *same four nets*
(`VIN_SW, GND, V5_BUCK, GND`) beside the module pocket. If the pad rectangle is
wrong, the module goes in as a 4-wire pigtail into that header and the board still
works. Cost: 4 holes.

---

## 2. Protection chain (12V input) — concrete parts

Order, from the `J_PWR` screw terminal inward:

| Ref | Part | Package / Footprint | LCSC | Why this one |
|---|---|---|---|---|
| `F1` | **Littelfuse 1812L110/16 polyfuse**, Ihold 1.1 A / Itrip 2.2 A, 16 V | `Fuse:Fuse_1812_4532Metric_Pad1.30x3.40mm_HandSolder` | C2830175 | Self-resetting; no fuse to lose in a dashboard. 1812 is comfortably hand-solderable. 16 V rating is fine downstream of the TVS. |
| `D1` | **SS56** Schottky 60 V / 5 A, series reverse-polarity (was SS54/40 V — only ~1 V of margin against the 38.9 V TVS clamp) | `Diode_SMD:D_SMA-SMB_Universal_Handsoldering` | C15879 | **Pick SMA (DO-214AC), not DO-201 THT** — the universal SMA/SMB handsolder pad is oversized, easy with an iron, and keeps the input loop tight for EMC. THT DO-201 buys nothing here. Vf ~0.55 V @ 1 A. |
| `TVS1` | **SMBJ24A** unidirectional, Vbr 26.7 V, Vclamp 38.9 V, 600 W | `Diode_SMD:D_SMB_Handsoldering` | C2687123 | Placed **after** D1 so it clamps the rail the bucks actually see. 24 V standoff clears 12 V nominal + 14.4 V charging + normal alternator ripple; clamps ISO 7637 pulses. |
| `C1` | 100 µF / 25 V alu electrolytic | `Capacitor_THT:CP_Radial_D8.0mm_P3.50mm` | C3009 | Bulk / load-dump energy reservoir. THT electrolytic = trivial to hand-fit, and 25 V gives margin under the TVS clamp… **see note.** |
| `C2` | 100 nF / 50 V X7R | `Capacitor_SMD:C_0805_2012Metric_Pad1.18x1.45mm_HandSolder` | C1590 | HF bypass next to C1. |

> **Note on C1 voltage rating:** SMBJ24A clamps at **38.9 V** peak. A 25 V electrolytic
> is *below* the clamp voltage. For a millisecond-scale transient this is survivable
> (electrolytics tolerate brief overvoltage), but the correct part is a
> **100 µF / 50 V** (`CP_Radial_D10.0mm_P5.00mm`, LCSC C3013). Use the 50 V part —
> it is the same price and removes the only latent-failure item in the chain.

---

## 3. AMS1117-3.3 as specified (documented, then superseded)

- Symbol **verified present**: `Regulator_Linear:AMS1117-3.3` at line 2602 of
  `C:\Program Files\KiCad\10.0\share\kicad\symbols\Regulator_Linear.kicad_sym`.
- Footprint: `Package_TO_SOT_SMD:SOT-223-3_TabPin2` (tab = GND on AMS1117).
- Input cap `C3`: 10 µF / 16 V X7R, `C_0805_2012Metric_Pad1.18x1.45mm_HandSolder`.
- Output cap `C4`: 22 µF / 10 V X7R, `C_1210_3225Metric_Pad1.33x2.70mm_HandSolder`
  (a 22 µF X7R in 0805 derates ~60% at 3.3 V bias — use 1210).
- Bulk at WROVER `C5`: **220 µF / 6.3 V** low-ESR, `CP_Radial_D6.3mm_P2.50mm`
  (LCSC C3020), placed **within 10 mm of the WROVER 3V3 pads** per ARCHITECTURE-V2 §3.
- Plus 100 nF 0805 at each WROVER 3V3 pad and each transceiver VCC.

This regulator is **removed** by the §7 recommendation. The 220 µF bulk, the 22 µF,
and all the 100 nF decoupling stay exactly as above — they belong to the 3V3 rail,
not to the regulator that feeds it.

---

## 4. Backfeed Schottky, BAT/ACC jumper, ACC sense — exact net topology

Read as a net list. `-->` is a directed component; `==` is a plain net tie.

```
J_PWR.1 (BAT)  --> JP1 pad 3 (B)                   [3-pad SELECTOR, factory bridge 1-2]
J_PWR.2 (ACC)  --> JP1 pad 1 (A)
J_PWR.3 (GND)  == NET:GND

JP1 pad 2 (C)  == NET:V12_SEL                       [exactly ONE source; never both]
NET:V12_SEL    --> F1 (polyfuse 1.1A)  --> NET:V12_F
NET:V12_F      --> TVS1 (SMBJ24A) --> NET:GND       [cathode=V12_F, anode=GND]
NET:V12_F      --> D1 (SS56, anode=V12_F, cathode=V12_P)  --> NET:V12_P
NET:V12_P      == C1 (100uF/50V) + C2 (100nF) + C17 (10uF/50V) + C18 (100nF) --> NET:GND

--- ACC ignition sense (divider on the PROTECTED rail) ---
NET:V12_P      --> R1 (47k, 0805) --> NET:ACC_SENSE
NET:ACC_SENSE  --> R2 (10k, 0805) --> NET:GND
NET:ACC_SENSE  --> C6 (100nF, 0805) --> NET:GND     [debounce / ESD]
NET:ACC_SENSE  == WROVER.GPIO34                      [input-only, no pullup]
   ratio 10/(47+10) = 0.1754  ->  12.0V=2.11V  14.4V=2.53V  16V=2.81V  OK (<3.3V)
   worst case at TVS clamp 38.9V -> 6.82V : GPIO34 ESD diodes + 47k limit to
   ~75uA for the transient duration. Acceptable. Do NOT lower R1.

--- 5V generation + USB backfeed isolation ---
NET:V12_P      --> U_BUCK1.IN+ (Mini-360 pad 1)
U_BUCK1.IN-    == NET:GND
U_BUCK1.OUT+   == NET:V5_BUCK                        [pot trimmed to 5.00V]
U_BUCK1.OUT-   == NET:GND
NET:V5_BUCK    --> D2 (SS34, anode=V5_BUCK, cathode=V5) --> NET:V5
NET:V5         == C7 (22uF/10V 1210) --> NET:GND
NET:V5         == C3_SUPERMINI.5V                    [USB backfeeds NET:V5 here]

   D2 rationale: with USB plugged into the C3, its 5V pin drives NET:V5 to ~5.1V.
   D2 blocks that from reaching U_BUCK1.OUT+, so the buck's output stage and the
   USB host never fight. Reverse leakage of SS34 ~0.5mA @ 40V, negligible.
   Cost: Vf ~0.45V at 350mA, so NET:V5 = 4.55V when buck-fed. The C3 SuperMini
   LDO needs >= ~3.6V in for 3.3V out -- 4.55V is fine.
```

**Solder-jumper JP1 semantics (revised — it is a selector, not a tie):** the factory
state bridges pads 1-2, so the board runs from ACC and is fully dead with the ignition
off. To run permanently from BAT, **cut the 1-2 bridge first**, then bridge 2-3.

The earlier 2-pad version of JP1 could not do this. It shorted `J_PWR.1` to `J_PWR.2`,
so closing it did not select BAT — it *paralleled* BAT onto ACC, with two consequences
neither this document nor DESIGN.md previously acknowledged:

1. Permanent 12 V was driven **back out of the ACC terminal** onto the vehicle's
   accessory conductor, holding every other accessory on that circuit energised with
   the key out and backfeeding the ignition-switch contacts. The documented downside
   was only the parasitic drain on *this* board; the outward-facing hazard was not
   mentioned anywhere.
2. The BAT→ACC bridge was **upstream of F1**, so nothing on this board limited the
   current in that path — a solder slip or a wiring error there is a battery short
   through a hand-soldered blob, with only the vehicle-side fuses in the loop.

The 3-pad selector removes both: pad 2 is the sole path to F1, and BAT and ACC are
never electrically joined regardless of how the jumper is bridged.

The drain warning still applies to the BAT selection: the Mini-360's 60 mA quiescent
draw is ~1.4 Ah/day and will flatten a car battery in under a week. Silkscreened
`ACC<->BAT SEL` / `CUT 1-2 FIRST` next to JP1.

**TVS position (revised):** `TVS1` now sits on `V12_F`, i.e. **between F1 and D1**,
not on `V12_P` behind D1. Behind D1 it could only clamp positive excursions: an
ISO 7637-2 pulse-1 negative transient reverse-biases the series Schottky, the TVS
cannot conduct, and the full negative excursion lands across a diode rated 40 V —
which avalanches and typically fails *short*, silently removing reverse-polarity
protection. It also forced every clamp ampere back through F1's PTC resistance and
D1. Connector → fuse → TVS → series-block is the conventional order and is what the
board now implements. D1 is upsized to **SS56 (60 V)** and the catch diode to
**SS36 (60 V)** for margin over the 38.9 V clamp.

---

## 5. Current budget

### 3V3 rail
| Load | Peak | Sustained |
|---|---|---|
| ESP32-WROVER-IB, WiFi TX | 500 mA | ~240 mA (TX duty) / 80 mA idle |
| SN65HVD230 #1 (vehicle CAN) | 70 mA dominant | 10 mA recessive |
| SN65HVD230 #2 (multimedia CAN) | 70 mA dominant | 10 mA recessive |
| Power + status LEDs (2 × 1k) | 6 mA | 6 mA |
| **3V3 total** | **~646 mA** | **~270 mA** |

### 5V rail
| Load | Peak | Sustained |
|---|---|---|
| ESP32-C3 SuperMini (via its own LDO) | 350 mA | ~120 mA |
| **5V total** | **~350 mA** | **~120 mA** |

### As-specified tree — both stages fail

**AMS1117-3.3 dissipation:**
```
P = (5.0 - 3.3) x I
   @ 500 mA  -> 0.85 W
   @ 646 mA  -> 1.10 W   (real peak)
```
SOT-223 θJA on a 2-layer board with a realistic ~1 cm² pour ≈ **80 °C/W**
(the 61 °C/W in the datasheet assumes 1 in² of 2 oz copper, which this board
does not have to spare).

```
ΔT @ 0.85 W = 68 °C     ΔT @ 1.10 W = 88 °C
Automotive ambient behind a radio head unit, summer: 60-70 °C
Tj = 70 + 68 = 138 °C   ...  Tj(max) = 125 °C     ** OUT OF SPEC **
```
This is not "marginal." At full WiFi TX in a hot car the AMS1117 exceeds its
absolute-maximum junction temperature and will thermally fold back — which
browns out the WROVER mid-transmission, i.e. an intermittent field failure that
is very hard to diagnose.

**Mini-360 loading:**
```
I(buck) = I(5V rail) + I(AMS1117 in)  =  350 mA + 646 mA  = ~1.00 A peak
                                          120 mA + 270 mA = ~0.39 A sustained
Measured Mini-360 trip point, open air: >= 750 mA        ** FAILS AT PEAK **
```
Sustained is inside the envelope, but every WiFi TX burst pokes past the trip
point, and there is no thermal headroom left for 70 °C ambient.

---

## 6. Root cause

The failure is structural, not a component-choice mistake: **a linear regulator is
being asked to bridge 5 V → 3.3 V for a 650 mA load.** 1.7 V × 0.65 A is 1.1 W that
has to leave a SOT-223 in a sealed automotive enclosure, and it also forces the
upstream buck to carry the 3V3 rail's *full* current at 5 V instead of at 12 V.
Both problems disappear together if the 3V3 rail is generated directly from 12 V.

---

## 7. RECOMMENDATION (one change)

**Delete the AMS1117-3.3 and the 5V→3V3 path entirely. Generate 3V3 straight from
the protected 12 V rail with an on-board LM2596S-3.3, and demote the Mini-360 to
feeding only the C3 SuperMini.**

```
NET:V12_P --> U_BUCK1 (Mini-360, 5V)  --> D2(SS34) --> NET:V5  --> C3 SuperMini 5V pin
NET:V12_P --> U2 (LM2596S-3.3, 3A)               --> NET:V3V3 --> WROVER + 2x SN65HVD230
```

Why this and not the alternatives:

- **vs. two AMS1117s in parallel-ish (split loads):** halves the *per-package*
  dissipation but not the total 1.1 W, still needs the buck to source ~1 A at 5 V,
  and load-sharing between two linear regulators is not something you can rely on.
  It treats the symptom.
- **vs. one 3 A buck straight to 3.3 V for everything:** attractive, but the C3
  SuperMini's `3V3` pin is its own LDO's *output*. Driving it externally while USB
  is plugged means two hard 3.3 V sources in contention, and the obvious fix
  (a series Schottky) lands the C3 at ~3.0 V — right on its minimum. Keeping the
  C3 on 5 V preserves **exactly one** backfeed path, the documented one, already
  handled by D2. Don't create a second.

**Resulting numbers:**

| | Before | After |
|---|---|---|
| Linear dissipation | 1.10 W in SOT-223 | **0 W** |
| Mini-360 peak load | 1.00 A (trips) | **0.35 A** (well inside limit) |
| 3V3 supply peak headroom | none | **3 A rated vs 0.65 A → 4.6×** |
| Worst-case Tj (70 °C amb) | 138 °C (fail) | LM2596S ~0.8 W, TO-263 + pour ≈ 35 °C/W → **98 °C** |

**LM2596S-3.3 support circuit** (fixed-output part — no feedback divider to get
wrong, no pot to vibrate out of trim, 40 V max input suits automotive):

| Ref | Part | Footprint | LCSC |
|---|---|---|---|
| `U2` | LM2596S-3.3 | `Package_TO_SOT_SMD:TO-263-5_TabPin3` (sym `Regulator_Switching:LM2596S-3.3`, **verified** line 35820) | C347421 |
| `L1` | 33 µH / 3 A shielded power inductor | `Inductor_SMD:L_12x12mm_H8mm` | C169305 |
| `D3` | SS34 catch diode (40 V / 3 A) | `Diode_SMD:D_SMA-SMB_Universal_Handsoldering` | C8678 |
| `C8` | 100 µF / 50 V alu, Cin | `Capacitor_THT:CP_Radial_D10.0mm_P5.00mm` | C3013 |
| `C9` | 220 µF / 6.3 V low-ESR, Cout | `Capacitor_THT:CP_Radial_D6.3mm_P2.50mm` | C3020 |

`C9` **is** the 220 µF bulk that ARCHITECTURE-V2 §3 requires at the WROVER —
place it at the WROVER 3V3 pads and route the LM2596 output through it, so it
serves as both the switcher's output cap and the module's bulk. One part, two jobs.
Keep the 100 nF 0805 at each WROVER 3V3 pad and each transceiver VCC regardless.

**Layout notes for the switcher (150 kHz — the one place this board can bite).**
Refdes in the built board are `U5` (regulator), `D6` (catch diode), `C3` (output/bulk),
`C2`+`C17` (input ceramics); the `U2`/`D3`/`C8`/`C9` labels above are from an earlier
revision of this document.

- Keep the `U5.OUT → L1 → C3` loop and the `D6` return loop physically tight;
  D6's cathode goes to the switch node, anode to the *same* ground pour region
  as the input caps' negative terminals.
- Pour a copper area of ≥ 2 cm² on the TO-263 tab, stitched to the bottom pour
  with ≥ 8 thermal vias.
- Route the switch node as a short fat trace, and keep the CAN transceiver pairs
  and the WROVER antenna keep-out away from it.

**As built, measured from the board file:**

| | Before | After |
|---|---|---|
| Cin ceramic → U5.VIN | 11.5 mm (C2 was the only ceramic on the node) | **4.4 mm** (C2 100 nF) + 8.8 mm (C17 10 µF, new) |
| D6 cathode → U5.SW | 18.8 mm | **5.1 mm** |
| Switch node copper | 22.5 mm, **split across both layers**, incl. a 16 mm 1.5 mm-wide slot cut through the bottom GND pour | **24 mm, entirely F.Cu, zero vias** |
| Thermal vias in the tab | **0** | **12** (0.6/0.3 mm) |
| PS1 local ceramic | none | **C18 100 nF at 4.4 mm** |

The honest residual: `L1` is a 12 × 12 mm part and `U5` is a TO-263 whose pin row and
tab together span 16.7 mm, so the switch node cannot get below ~19 mm from the SW pin
to L1 without a smaller inductor. What matters most — the Cin → VIN → SW → D6 → GND
commutation loop, the high-di/dt one — is now ~25 mm of perimeter instead of >70 mm,
and the highest-dv/dt node on the board is no longer a slot in the reference plane.

---

## 8. Machine-readable component + net data

```json
{
  "schema": "meganecan-gw.power.v1",
  "revision": "2026-07-20",
  "status": "as-recommended (section 7 applied); AMS1117 branch retained in section 3 for audit only",
  "rails": [
    { "net": "V12_P", "nominal_v": 12.0, "range_v": [9.0, 16.0], "clamp_v": 38.9, "source": "ACC via F1/D1/TVS1" },
    { "net": "V5",    "nominal_v": 4.55, "range_v": [4.4, 5.2],  "peak_ma": 350, "sustained_ma": 120, "source": "U_BUCK1 via D2, or USB backfeed" },
    { "net": "V3V3",  "nominal_v": 3.30, "range_v": [3.2, 3.4],  "peak_ma": 646, "sustained_ma": 270, "source": "U2 LM2596S-3.3" }
  ],
  "components": [
    { "ref": "J_PWR", "value": "3-pin 5.08mm screw terminal", "desc": "BAT / ACC / GND",
      "footprint": "TerminalBlock_Phoenix:TerminalBlock_Phoenix_MKDS-1,5-3-5.08_1x03_P5.08mm_Horizontal", "lcsc": null },
    { "ref": "JP1", "value": "solder jumper", "desc": "BAT->ACC tie, DEFAULT OPEN (60mA Iq would flatten the battery)",
      "footprint": "Jumper:SolderJumper-2_P1.3mm_Open_RoundedPad1.0x1.5mm", "lcsc": null },
    { "ref": "F1", "value": "1812L110/16", "desc": "polyfuse Ihold 1.1A / Itrip 2.2A",
      "footprint": "Fuse:Fuse_1812_4532Metric_Pad1.30x3.40mm_HandSolder", "lcsc": "C2830175" },
    { "ref": "D1", "value": "SS56", "desc": "series reverse-polarity Schottky 60V/5A, SMA (DO-214AC)",
      "footprint": "Diode_SMD:D_SMA-SMB_Universal_Handsoldering", "symbol": "Device:D_Schottky", "lcsc": "" },
    { "ref": "TVS1", "value": "SMBJ24A", "desc": "unidir TVS, Vbr 26.7V, Vclamp 38.9V, 600W, SMB",
      "footprint": "Diode_SMD:D_SMB_Handsoldering", "symbol": "Device:D_TVS", "lcsc": "C2687123" },
    { "ref": "C1", "value": "100uF/50V", "desc": "input bulk; 50V chosen to exceed the 38.9V TVS clamp",
      "footprint": "Capacitor_THT:CP_Radial_D10.0mm_P5.00mm", "lcsc": "C3013" },
    { "ref": "C2", "value": "100nF/50V X7R", "footprint": "Capacitor_SMD:C_0805_2012Metric_Pad1.18x1.45mm_HandSolder", "lcsc": "C1590" },
    { "ref": "R1", "value": "47k 1% 0805", "desc": "ACC sense divider high side",
      "footprint": "Resistor_SMD:R_0805_2012Metric_Pad1.20x1.40mm_HandSolder", "lcsc": "C17714" },
    { "ref": "R2", "value": "10k 1% 0805", "desc": "ACC sense divider low side",
      "footprint": "Resistor_SMD:R_0805_2012Metric_Pad1.20x1.40mm_HandSolder", "lcsc": "C17414" },
    { "ref": "C6", "value": "100nF/50V X7R", "desc": "ACC_SENSE debounce/ESD",
      "footprint": "Capacitor_SMD:C_0805_2012Metric_Pad1.18x1.45mm_HandSolder", "lcsc": "C1590" },
    { "ref": "U_BUCK1", "value": "Mini-360 (MP2307)", "desc": "socketed module, pot trimmed to 5.00V, feeds C3 only (~350mA)",
      "footprint": "meganecan:MINI360_4pad_Socket", "lcsc": null,
      "warning": "pad coordinates PROVISIONAL - verify with calipers before fab" },
    { "ref": "J_BUCK", "value": "1x04 2.54mm header", "desc": "fallback pigtail for U_BUCK1, same 4 nets",
      "footprint": "Connector_PinHeader_2.54mm:PinHeader_1x04_P2.54mm_Vertical", "lcsc": null },
    { "ref": "D2", "value": "SS34", "desc": "blocks C3 USB 5V backfeed from fighting U_BUCK1 output",
      "footprint": "Diode_SMD:D_SMA-SMB_Universal_Handsoldering", "symbol": "Diode:SS34", "lcsc": "C8678" },
    { "ref": "C7", "value": "22uF/10V X7R 1210", "desc": "V5 rail decoupling",
      "footprint": "Capacitor_SMD:C_1210_3225Metric_Pad1.33x2.70mm_HandSolder", "lcsc": "C29823" },
    { "ref": "U2", "value": "LM2596S-3.3", "desc": "12V->3.3V 3A fixed switcher, replaces AMS1117",
      "footprint": "Package_TO_SOT_SMD:TO-263-5_TabPin3", "symbol": "Regulator_Switching:LM2596S-3.3", "lcsc": "C347421" },
    { "ref": "L1", "value": "33uH/3A shielded", "footprint": "Inductor_SMD:L_12x12mm_H8mm", "lcsc": "C169305" },
    { "ref": "D3", "value": "SS34", "desc": "LM2596 catch diode",
      "footprint": "Diode_SMD:D_SMA-SMB_Universal_Handsoldering", "symbol": "Diode:SS34", "lcsc": "C8678" },
    { "ref": "C8", "value": "100uF/50V", "desc": "LM2596 Cin",
      "footprint": "Capacitor_THT:CP_Radial_D10.0mm_P5.00mm", "lcsc": "C3013" },
    { "ref": "C9", "value": "220uF/6.3V low-ESR", "desc": "LM2596 Cout AND the ARCHITECTURE-V2 WROVER bulk - place at WROVER 3V3 pads",
      "footprint": "Capacitor_THT:CP_Radial_D6.3mm_P2.50mm", "lcsc": "C3020" },
    { "ref": "C10-C13", "value": "100nF/50V X7R", "desc": "one per WROVER 3V3 pad and per SN65HVD230 VCC",
      "footprint": "Capacitor_SMD:C_0805_2012Metric_Pad1.18x1.45mm_HandSolder", "lcsc": "C1590" },
    { "ref": "D_PWR", "value": "LED green 0805 + R 1k", "desc": "3V3 power indicator",
      "footprint": "LED_SMD:LED_0805_2012Metric_Pad1.15x1.40mm_HandSolder", "lcsc": "C2286" }
  ],
  "removed_from_spec": [
    { "ref": "U1", "value": "AMS1117-3.3", "reason": "1.10W in SOT-223 -> Tj 138C at 70C ambient, exceeds 125C max",
      "symbol_verified_at": "Regulator_Linear.kicad_sym:2602", "footprint": "Package_TO_SOT_SMD:SOT-223-3_TabPin2" }
  ],
  "nets": [
    { "name": "GND",       "members": ["J_PWR.3","TVS1.A","C1.-","C2.2","R2.2","C6.2","U_BUCK1.2","U_BUCK1.4","J_BUCK.2","J_BUCK.4","C7.2","U2.GND","U2.TAB","D3.A","C8.-","C9.-","WROVER.GND","C3.GND","CAN1.GND","CAN2.GND"] },
    { "name": "ACC_RAW",   "members": ["J_PWR.2","JP1.B","F1.1"] },
    { "name": "BAT_RAW",   "members": ["J_PWR.1","JP1.A"] },
    { "name": "V12_F",     "members": ["F1.2","D1.A"] },
    { "name": "V12_P",     "members": ["D1.K","TVS1.K","C1.+","C2.1","R1.1","U_BUCK1.1","J_BUCK.1","U2.VIN","C8.+"] },
    { "name": "ACC_SENSE", "members": ["R1.2","R2.1","C6.1","WROVER.GPIO34"] },
    { "name": "V5_BUCK",   "members": ["U_BUCK1.3","J_BUCK.3","D2.A"] },
    { "name": "V5",        "members": ["D2.K","C7.1","C3_SUPERMINI.5V"] },
    { "name": "SW",        "members": ["U2.OUT","L1.1","D3.K"] },
    { "name": "V3V3",      "members": ["L1.2","C9.+","U2.FB","C10.1","C11.1","C12.1","C13.1","WROVER.3V3","CAN1.VCC","CAN2.VCC","D_PWR.R"] }
  ],
  "budget": {
    "v3v3_peak_ma": 646, "v3v3_sustained_ma": 270,
    "v5_peak_ma": 350,  "v5_sustained_ma": 120,
    "v12_sustained_ma_at_14v": 105,
    "ams1117_dissipation_w_at_peak": 1.10,
    "ams1117_tj_at_70c_ambient": 138,
    "lm2596_dissipation_w_at_peak": 0.80,
    "lm2596_tj_at_70c_ambient": 98,
    "mini360_measured_trip_ma": 750,
    "mini360_load_after_change_ma": 350
  },
  "open_actions": [
    "Caliper-measure a physical Mini-360 and patch the 4 pad coordinates in meganecan:MINI360_4pad_Socket",
    "DONE - silkscreen legend added for J1/J2/J3/J4/J5/J7, JP1 (ACC<->BAT SEL, CUT 1-2 FIRST), JP2-JP5 (BENCH ONLY), PS1 trim note, IO2 LED note, PS1/J7 populate-one note",
    "Source the parts introduced in this revision (empty LCSC fields): D1 SS56, D6 SS36, D7/D8 NUP2105L, R5/R6 0R 0805, R8 220R 0805, R16 470R 0805, C17 10uF/50V X7R 1210",
    "Confirm Mini-360 silkscreen corner orientation (IN+/IN- vs OUT+/OUT-) against the footprint pin-1 marker"
  ]
}
```
