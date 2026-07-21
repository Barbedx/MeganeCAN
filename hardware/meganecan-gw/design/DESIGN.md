# MeganeCAN GW — Canonical Board Design

Board: `hardware/meganecan-gw`. JLCPCB 2-layer, hand-assembled.
Machine-readable twin of this document: **`design.json`** (that file is the build input;
this one explains it).

Reconciles `wrover-ib.md` + `c3-supermini.md` + `power.md` + `netlist.md` into one design.
Where those four disagreed, the resolution and its reason are in §7.

**64 components · 50 nets · 3 custom library parts.**

---

## 1. What the board is

A dual-MCU automotive gateway that sits between the Mégane II vehicle CAN bus, the OEM
multimedia CAN bus, and a DUDU7 Android head unit.

| Role | Part | Owns |
|---|---|---|
| **GW brain** | ESP32-WROVER-IB (soldered module) | vehicle CAN, DUDU7 canbox UART, BLE, WiFi/web |
| **DISP co-processor** | ESP32-C3 SuperMini (socketed) | multimedia CAN, AFFA3 display drivers, USB bench proxy |

They talk over a 460800 8N1 LinkProto UART, cross-wired on the PCB.

---

## 2. Power tree

**The headline decision: there is no linear regulator on this board.** See §7.1.

```
 J1.1 BAT ─────────> JP1.3 ─┐
                            │   JP1 = 3-pad SOLDER-JUMPER SOURCE SELECTOR
 J1.2 ACC ─────────> JP1.1 ─┤   (pads 1-2 bridged from the factory = ACC)
                            │   Exactly ONE source reaches JP1.2 — BAT can
                          JP1.2  never be driven back out of the ACC terminal.
                            │
                         V12_SEL
                            │
              F1  1812L110/16 polyfuse   (placed hard against J1: ~9 mm of
              (Ihold 1.1 A / Itrip 2.2 A) 1.5 mm unfused copper, nothing else)
                            │
                         V12_FUSED
                            ├── D2 SMBJ24A TVS ──> GND
                            │   (Vbr 26.7 V, clamp 38.9 V, 600 W)
                            │   *** UPSTREAM of D1: a negative ISO 7637-2
                            │   pulse-1 transient is clamped here instead of
                            │   being impressed across the series Schottky ***
                            │
              D1  SS56 Schottky 60 V, series
              (reverse-polarity protection; 60 V for margin over the 38.9 V clamp)
                            │
                            ├── C1 100 µF/50 V ──> GND
                            ├── C2 100 nF ───────> GND   (at U5.VIN, ~4 mm)
                            ├── C17 10 µF/50 V ──> GND   (at U5.VIN, ~5 mm)
                            ├── C18 100 nF ──────> GND   (at PS1.IN+, ~4 mm)
                            │
                         V12_PROT  (9–16 V nominal)
                          ┌─────────────────────────────┼─────────────────────────────┐
                          │                             │                             │
          ┌───────────────┴──────────┐    ┌─────────────┴──────────┐   ┌──────────────┴────────────┐
          │ R1 47 k                  │    │ PS1  Mini-360 (MP2307) │   │ U5  LM2596S-3.3           │
          │   │                      │    │ pot trimmed to 5.00 V  │   │ 3 A fixed switcher, 150 kHz│
          │ ACC_SENSE ── C6 100 nF ─GND   │ C16 100 µF/50 V = Cin  │   │ ~ON/OFF (pin 5) tied GND  │
          │   │                      │    └─────────┬──────────────┘   └───────┬───────────────────┘
          │ R2 10 k ─> GND           │              │ +5V_BUCK                 │ SW_LM2596
          └───┼──────────────────────┘              │                          │
              │                                 D3 SS34                    L1 33 µH/3 A
        U1.IO34 (pad 6, input-only)          (blocks USB backfeed)        D6 SS36 catch ─> GND
        12.0 V -> 2.11 V                         │                            │
        14.4 V -> 2.53 V                         ├── C7  22 µF/10 V 1210      ├── C3 220 µF/6.3 V low-ESR
        16.0 V -> 2.81 V   (< 3.3 V OK)          ├── C14 47 µF/10 V           │   (= LM2596 Cout AND the
                                                 ├── J6.2 (EXP header)        │      WROVER bulk — one part,
                                                 │                            │      two jobs; place it AT
                                                 └── U2.1  C3 SuperMini 5V    │      the WROVER 3V3 pads)
                                                     ▲                        │
                                                     │ USB backfeeds here     └──> +3V3 rail
                                                     │ (documented bench           ├── U1 WROVER (pad 2)
                                                     │  maintenance path)          ├── U3 SN65HVD230 (vehicle CAN)
                                                                                   ├── U4 SN65HVD230 (MM CAN)
                                                                                   ├── R3 10 k -> EN pull-up
                                                                                   ├── R4 10 k -> IO0 pull-up
                                                                                   ├── R7 1 k -> D4 power LED
                                                                                   ├── J5.1 (JIG), J6.1 (EXP)
                                                                                   └── C4 10 µF, C5/C8..C13 100 nF
```

`C3` sits **9.8 mm** from the WROVER's 3V3 pad (U1 pad 2) and the LM2596 output is routed
*through* it on 1.0 mm copper — see §7.6. `R3` (EN pull-up) and `C15` (EN filter) moved to
**3.3 mm** of U1 pad 3, and the EN net no longer leaves the module: `R16` (470 Ω) isolates the
`J5`/`J6` header stubs on a separate `EN_EXT` node so a cable cannot inject onto the reset pin.

### 2.1 Current budget

| Rail | Peak | Sustained | Source capability | Headroom |
|---|---|---|---|---|
| **+3V3** | 646 mA | 270 mA | LM2596S-3.3 @ 3 A | **4.6×** |
| **+5V** | 350 mA | 120 mA | Mini-360, measured trip ≥ 750 mA | **2.1×** |
| **12 V in** | — | ~105 mA @ 14 V | F1 Ihold 1.1 A | 10× |

+3V3 peak = WROVER WiFi TX 500 mA + 2 × SN65HVD230 dominant 70 mA + LEDs 6 mA.

### 2.2 Thermals

`U5` LM2596S-3.3 dissipates ~0.80 W at the 646 mA peak. TO-263 tab on ≥ 2 cm² of pour
with ≥ 8 thermal vias ≈ 35 °C/W → **Tj ≈ 98 °C at 70 °C ambient**, inside the 125 °C limit.

---

## 3. Connector pinouts

### J1 — PWR (3-pin 5.08 mm screw terminal)
| Pin | Signal | Note |
|---|---|---|
| 1 | BAT | 12 V permanent, from the radio harness. Reaches the board only if `JP1` is re-bridged 2–3 |
| 2 | ACC | 12 V ignition-switched — the normal power source, selected by `JP1`'s factory 1–2 bridge |
| 3 | GND | chassis / harness ground |

`JP1` is a **3-pad selector**, not a 2-pad tie: pad 2 (the common) is the only path to `F1`, so
BAT and ACC are never bridged to each other. Both wires may be landed permanently; selecting BAT
does not drive 12 V back out of the ACC terminal onto the vehicle's accessory circuit. Silkscreen
next to it reads `ACC<->BAT SEL / CUT 1-2 FIRST`.

### J2 — VCAN (3-pin 5.08 mm screw terminal) — vehicle CAN, 500 k
| Pin | Signal |
|---|---|
| 1 | CANH |
| 2 | CANL |
| 3 | GND |

### J3 — MMCAN (3-pin 5.08 mm screw terminal) — multimedia CAN, 500 k
| Pin | Signal |
|---|---|
| 1 | CANH |
| 2 | CANL |
| 3 | GND |

### J4 — HU (3-pin JST-XH 2.50 mm) — DUDU7 canbox UART, 38400 8N1
| Pin | Signal | Direction |
|---|---|---|
| 1 | `GW_TX_HU_RX` | GW **TX** (WROVER IO25) → head-unit RX |
| 2 | `GW_RX_HU_TX` | GW **RX** (WROVER IO26) ← head-unit TX |
| 3 | GND | |

> The nets were called `HU_TX`/`HU_RX` in earlier revisions. That reads to an installer as
> "the head unit's TX", i.e. the opposite wire. The names now spell out both ends, and the
> silkscreen at J4 reads `GWTX GWRX GND`.

> Cross-over is done **on this board**. Wire J4 straight through to the head unit;
> do not cross the cable as well.

### J5 — JIG (1×6 pin header 2.54 mm) — WROVER USB-serial flash jig, CR-09
| Pin | Signal |
|---|---|
| 1 | +3V3 |
| 2 | TXD0 (WROVER pad 35, GPIO1) |
| 3 | RXD0 (WROVER pad 34, GPIO3) |
| 4 | EN |
| 5 | IO0 |
| 6 | GND |

Standard auto-reset order: a CP2102/CH340 jig with DTR→EN and RTS→IO0 drives this directly.

### J6 — EXP (1×12 pin header 2.54 mm) — expansion, CR-10
| Pin | Signal | | Pin | Signal |
|---|---|---|---|---|
| 1 | +3V3 | | 7 | IO13 |
| 2 | +5V | | 8 | IO14 |
| 3 | GND | | 9 | IO23 |
| 4 | EN | | 10 | IO27 |
| 5 | IO4 | | 11 | IO32 |
| 6 | IO5 | | 12 | IO33 |

IO13/IO14 are HSPI-capable; IO32/IO33 are generic. **IO12 is deliberately absent** — it is
the flash-voltage strap and must float (see §5).

### J7 — BUCK-ALT (1×4 pin header 2.54 mm) — Mini-360 fallback
| Pin | Signal |
|---|---|
| 1 | V12_PROT (= PS1 IN+) |
| 2 | GND (= PS1 IN−) |
| 3 | +5V_BUCK (= PS1 OUT+) |
| 4 | GND (= PS1 OUT−) |

Insurance, not a feature — see §7.4.

### PS1 — Mini-360 module socket
| Pad | Silk | Net |
|---|---|---|
| 1 | IN+ | V12_PROT |
| 2 | IN− | GND |
| 3 | OUT+ | +5V_BUCK |
| 4 | OUT− | GND |

### U2 — ESP32-C3 SuperMini socket (2 × 1×8 female headers, 15.24 mm apart)
USB-C end at −Y. Left row pads 1–8 (nearest USB first), right row pads 9–16.

| Pad | Pin | Net | | Pad | Pin | Net |
|---|---|---|---|---|---|---|
| 1 | 5V | **+5V** | | 9 | GPIO21 | **LINK_GW_RX** |
| 2 | GND | **GND** | | 10 | GPIO20 | **LINK_GW_TX** |
| 3 | 3V3 | *NC — do not connect* | | 11 | GPIO10 | — |
| 4 | GPIO0 | — | | 12 | GPIO9 | — (BOOT) |
| 5 | GPIO1 | — | | 13 | GPIO8 | — (LED) |
| 6 | GPIO2 | — | | 14 | GPIO7 | — |
| 7 | GPIO3 | **MCAN_RX** | | 15 | GPIO6 | — |
| 8 | GPIO4 | **MCAN_TX** | | 16 | GPIO5 | — |

---

## 4. Jumper table

| Ref | Name | Default | Changing it means | Why the default |
|---|---|---|---|---|
| **JP1** | SRC-SEL | **1–2 bridged (ACC)** | Cut 1–2, bridge 2–3 → the board runs from permanent BAT | The Mini-360's quiescent draw is up to **60 mA ≈ 1.4 Ah/day**; on BAT it flattens a car battery in under a week. ACC = zero parasitic drain with the ignition off. It is a *selector*, so BAT is never bridged onto the ACC harness wire. Silkscreen: `ACC<->BAT SEL`, `CUT 1-2 FIRST` |
| **JP2** | VCAN-TERM-H | **OPEN** | With **JP4**, inserts R9/R10 60.4 Ω split termination + C12 on the vehicle CAN | In the car we *tap an already-terminated bus* (ARCH §3.3). Adding a third terminator corrupts it. Close only on the bench, where the board would otherwise be the only node and bus-off. Silkscreen: `BENCH ONLY` |
| **JP4** | VCAN-TERM-L | **OPEN** | The low leg of the same network | See below |
| **JP3** | MMCAN-TERM-H | **OPEN** | Same, for the multimedia CAN | Same rule |
| **JP5** | MMCAN-TERM-L | **OPEN** | The low leg of the same network | See below |

Split termination = 60.4 Ω + 60.4 Ω in series (120.8 Ω nominal) with C12/C13 100 nF from the
midpoint to GND, for common-mode filtering.

**Both legs are jumpered.** With a jumper in the H leg only, opening it left CANL permanently
loaded by 60.4 Ω in series with 100 nF to ground — about 63 Ω to AC ground at 500 kbit/s on one
line while the other saw an open circuit. That is a single-ended half-termination: it loads
CANL's recessive-to-dominant edges asymmetrically and converts differential drive into
common-mode current, which is exactly what split termination exists to prevent — and it shipped
in the *in-car* configuration, the one that is open by default. `JP4`/`JP5` lift the whole R-C-R
network off both lines. Close H and L together or neither.

---

## 5. WROVER straps and boot

| Strap | Pad | Circuit | Requirement met |
|---|---|---|---|
| **EN** | 3 | R3 10 k → +3V3, SW1 tactile → GND, C15 100 nF → GND, R16 470 Ω → `EN_EXT` (J5.4/J6.4) | Pull-up + reset button + RC power-on delay, with the header stubs isolated |
| **IO0** | 25 | R4 10 k → +3V3, SW2 tactile → GND | High = normal boot; hold SW2 during reset = download mode |
| **IO12** | 14 | **NOTHING. Not on any net.** | MTDI selects flash voltage; a pull-up bricks boot. No pull-up, no test point, no J6 pin, no pour stitch |
| **IO2** | 24 | D5 blue LED anode; cathode → R8 220 Ω → GND | Must be low/floating at reset. An LED **to GND** satisfies this |
| **IO15** | 23 | unconnected | Boot-log silence strap; floating = normal |
| **IO5** | 29 | on J6 EXP | Drives at boot — anything attached to J6.6 must tolerate that |

> **IO2 warning for future revisions:** the status LED must stay wired **to GND**. If it is
> ever changed to a pull-up-driven indicator, the board loses download mode. Silkscreen a note.

**GPIO16/17 do not exist on a WROVER.** They are consumed internally by the PSRAM; module pads
27 and 28 are NC1/NC2, declared no-connect in the footprint and absent from every net. Never
route to them.

---

## 6. Signal map (firmware ↔ pad)

Verified against `src/gw/gw_main.cpp`, `src/gw/GwLink.cpp`, `src/disp/DispLink.cpp`,
`src/disp/disp_main.cpp`.

| Function | MCU | GPIO | Pad | Net |
|---|---|---|---|---|
| Vehicle CAN RX | WROVER | 21 | 33 | VCAN_RX → U3.4 (R) |
| Vehicle CAN TX | WROVER | 22 | 36 | VCAN_TX → U3.1 (D) |
| Canbox UART TX | WROVER | 25 | 10 | GW_TX_HU_RX → J4.1 |
| Canbox UART RX | WROVER | 26 | 11 | GW_RX_HU_TX → J4.2 |
| Link UART2 TX | WROVER | 18 | 30 | LINK_GW_TX → C3 GPIO20 (pad 10) |
| Link UART2 RX | WROVER | 19 | 31 | LINK_GW_RX ← R13 1 k ← `LINK_GW_RX_C3` ← C3 GPIO21 (pad 9) |
| ACC ignition sense | WROVER | 34 | 6 | ACC_SENSE (input-only, ADC1_CH6) |
| Status LED | WROVER | 2 | 24 | LED_STAT |
| MM CAN RX | C3 | 3 | 7 | `MCAN_RX_C3` → R15 1 k → MCAN_RX → U4.4 (R) |
| MM CAN TX | C3 | 4 | 8 | `MCAN_TX_C3` → R14 1 k → MCAN_TX → U4.1 (D) |
| Link UART TX | C3 | 21 | 9 | → WROVER IO19 |
| Link UART RX | C3 | 20 | 10 | ← WROVER IO18 |

The link is **cross-wired on the PCB**: GW TX (IO18) → C3 RX (GPIO20), C3 TX (GPIO21) →
GW RX (IO19).

Both transceivers have `Rs` (pin 8) tied to GND through **0 Ω** (R5/R6) — high-speed mode.
The original 10 k gave ~15 V/µs, i.e. ~130 ns per 2 V differential transition; added to the
transmitter and receiver delays that is ~300 ns of loop delay against a 2 µs bit time, which
eats a large slice of the TWAI propagation segment for no benefit on a short tap stub. The 0805
pads are kept, so slope control can be restored by fitting 10 k if an EMC problem ever appears.
`Vref` (pin 5) on both is left open.

**Every C3 output that faces the 3V3 domain has a 1 k series resistor** (R13 on `LINK_GW_RX`,
R14 on `MCAN_TX`, R15 on `MCAN_RX`). See §7.3 — without them the C3 back-powers a dead 3V3 rail
through those pins' ESD clamp diodes. 1 k is harmless at 460800 baud and 500 kbit/s into the
few-pF loads involved.

---

## 7. Decisions and why

### 7.1 No linear regulator — LM2596S-3.3 replaces the AMS1117-3.3

The original CR-06 tree was `Mini-360 (12→5) → AMS1117-3.3 (5→3.3)`. **It does not close,
on two independent counts:**

- **Thermal.** 1.7 V × 0.646 A = **1.10 W** in a SOT-223. With a realistic ~1 cm² pour on a
  2-layer board, θJA ≈ 80 °C/W → ΔT = 88 °C. Behind a radio head unit in summer, ambient is
  60–70 °C, so **Tj = 138 °C against a 125 °C maximum**. Not marginal — out of spec. It would
  thermally fold back mid-WiFi-transmission and brown out the WROVER: an intermittent field
  failure that is miserable to diagnose.
- **Current.** The buck would have to carry the 3V3 rail's full current *at 5 V*:
  350 mA + 646 mA = **~1.00 A peak**, against a Mini-360 whose independently measured trip
  point is ~750 mA. Every WiFi burst pokes past it.

The root cause is structural, not a part-choice error: **a linear regulator was being asked to
bridge 5 V → 3.3 V for a 650 mA load.** Both problems vanish together if the 3V3 rail is
switched directly from 12 V, because the conversion loss stops being heat and the buck stops
carrying the 3V3 load.

| | Before | After |
|---|---|---|
| Linear dissipation | 1.10 W in SOT-223 | **0 W** |
| Mini-360 peak load | 1.00 A (trips) | **0.35 A** |
| 3V3 headroom | none | **3 A rated vs 0.646 A** |
| Worst-case Tj @ 70 °C | 138 °C (**fail**) | **98 °C** |

LM2596S-3.3 specifically: fixed output (no feedback divider to get wrong, no trim pot to
vibrate out of adjustment), 40 V max input suits automotive transients, TO-263 is
hand-solderable, and the KiCad symbol/footprint pair exists and is verified.

**Layout is the one place this bites** — it is a 150 kHz switcher:
- Keep the `U5.OUT → L1 → C3` loop and the `D6` return loop physically tight.
- D6's cathode goes to the switch node; its anode returns to the *same* ground pour region as
  the input capacitors' negative terminals.
- Pour ≥ 2 cm² on the TO-263 tab, stitched to the bottom pour with ≥ 8 thermal vias.
- Route the switch node as a short fat trace; keep it away from both CAN pairs and the
  WROVER antenna area.

**What the first layout actually did, and what it does now.** The first pass violated every one
of those bullets, which matters because §2.2's 98 °C junction figure and the whole case for
dropping the AMS1117 are predicated on them:

| | First layout | Now |
|---|---|---|
| Nearest ceramic on V12_PROT → U5.VIN | 11.5 mm (C2, the only one) | **4.4 mm** (C2) + 8.8 mm (C17 10 µF, new) |
| D6 cathode → U5.SW | 18.8 mm | **5.1 mm** |
| D6 anode → U5 GND pin | — | **6.4 mm**, same pour region as C2/C17 grounds |
| Switch node | 22.5 mm across **both layers**, incl. a 16 mm 1.5 mm-wide run cut straight through the bottom GND pour | **24 mm, entirely F.Cu, zero vias** |
| Thermal vias in the TO-263 tab | **0** | **12** |
| PS1 local ceramic | none | **C18 100 nF at 4.4 mm** |

The switch node threads the 2.15 mm gap between U5's pin row and its tab at 1.0 mm width, which
is why it is 1.0 mm there and 1.5 mm on the D6 branch.

**Residual, stated honestly:** `L1` is 12 × 12 mm and `U5`'s pin row plus tab span 16.7 mm, so
the SW pin cannot get closer than ~16 mm to L1 without a physically smaller inductor. That leg
carries continuous inductor current, not the commutation di/dt, and it is now a single-layer
1.0 mm trace rather than a slot in the reference plane. The loop that does matter —
Cin → VIN → SW → D6 → GND → Cin — is roughly 25 mm of perimeter instead of the 70 mm-plus it
was.

### 7.2 Rejected: one 3 A buck straight to 3.3 V for everything

Attractive — it deletes the Mini-360 entirely. Rejected because the C3 SuperMini's `3V3` pin is
its **own LDO's output**. Driving it externally while USB is plugged in means two hard 3.3 V
sources in contention, and the obvious fix (a series Schottky) lands the C3 at ~3.0 V, right on
its minimum.

Keeping the C3 on 5 V preserves **exactly one** backfeed path — the documented USB bench
maintenance path — already handled by D3. Don't create a second one.

### 7.3 The C3's 3V3 pin is not connected. Its 5V pin is.

`U2.3` appears in no net. This is load-bearing, not an oversight; see 7.2.

`U2.1` (5V) is bidirectional **by design**: plugging USB into the C3 on the bench backfeeds
the +5V rail, which is the documented way to power the board for maintenance. D3 (SS34) blocks
that current from reaching the Mini-360's output stage, so the buck and the USB host never
fight. Cost: Vf ≈ 0.45 V at 350 mA, so +5V sits at ~4.55 V when buck-fed — comfortably above
the C3 LDO's ~3.6 V minimum.

With USB attached but no 12 V the 3V3 rail is unpowered and the C3 alone runs. That closes
netlist.md's open question 5 (which worried that a bench USB port would have to carry the whole
3V3 rail through the C3's little LDO — under this tree, it never does).

**It is not automatically safe, though, and an earlier revision of this section was wrong to say
so.** Three C3 pins face silicon on the 3V3 rail with no isolation:

| Net | C3 pin | 3V3-domain end |
|---|---|---|
| `LINK_GW_RX` | GPIO21 — a **driven output** | WROVER IO19 |
| `MCAN_TX` | GPIO4 — a **driven output** | U4 (SN65HVD230) D input |
| `MCAN_RX` | GPIO3 — an input | U4 R output |

`DispLink.cpp` brings UART1 up and `disp_main.cpp` brings TWAI up inside `setup()`; UART TX idles
high and TWAI TX idles recessive (high). So within milliseconds of USB power the C3 is holding
two 3.3 V logic highs against pins whose VDD is at 0 V. Their ESD clamp diodes forward-bias and
charge the whole 3V3 rail — including the ≥ 220 µF bulk — to roughly Vout − Vf ≈ 2.7 V, sourced
through ESD structures rated for transients, not DC. 2.7 V is also above the ESP32's brown-out
threshold, so the WROVER half-wakes into an undefined state instead of staying cleanly off.
The same transient exists on every 12 V power-up, because `PS1` (+5V) and `U5` (+3V3) have
independent soft-starts and there is no sequencing between them.

**Fix, and it is in the netlist now:** `R13`/`R14`/`R15`, 1 kΩ in series at the C3 end of each of
those three nets. That caps the injected current at ~2.7 mA per pin — within any ESD diode's
continuous rating — and is electrically free at 460800 baud and 500 kbit/s. (`MCAN_RX` strictly
does not need it, since the unpowered transceiver's R output is high-Z; it is fitted anyway so
the three link/CAN nets are treated identically.)

### 7.4 Mini-360 footprint is provisional, and J7 is the insurance

There is **no dimensional drawing for the Mini-360 anywhere.** The most detailed independent
teardown says outright that the mounting holes *"don't seem to line up in mils or mm"* with
*"no accurate dimensions online."* Any 4-pad footprint published as exact coordinates is
somebody's caliper reading of *their* unit, and these boards come from many unbranded factories.

Two mitigations, both baked in:

- **A (do before fab):** caliper-measure the actual module and patch the four X/Y values. Two
  minutes, removes the risk entirely.
- **B (kept regardless):** `J7`, a 1×4 header on the *same four nets*, sits beside the module
  pocket. If the pad rectangle is wrong, the module goes in as a 4-wire pigtail and the board
  still works. Cost: four holes.

The 2.20 mm drill against a ~1.0 mm module pin also absorbs ±0.6 mm of positional error per
pad. The huge drill is deliberate.

### 7.5 ACC sense taps the protected rail

`R1` (47 k) connects to **V12_PROT**, i.e. after the fuse, the reverse-polarity Schottky and
the TVS — not to the raw input. The divider therefore never sees an unclamped automotive
transient.

Ratio 10/(47+10) = 0.1754: 12.0 V → 2.11 V, 14.4 V → 2.53 V, 16 V → 2.81 V, all under 3.3 V.
At the 38.9 V TVS clamp the node would reach 6.82 V, where GPIO34's ESD diodes plus the 47 k
limit current to ~75 µA for the transient's duration — acceptable. **Do not lower R1.**

*Known caveat:* V12_PROT is downstream of JP1, so if JP1 is re-bridged 2–3 (BAT) the divider
reports battery rather than ignition and ignition sense becomes meaningless. Benign today — ACC
is the factory selection and the firmware does not read IO34 yet. The alternative (tapping J1.2
pre-fuse for a true ACC read) was rejected: it puts an unprotected node on a GPIO.

### 7.6 C3 does two jobs

The 220 µF low-ESR electrolytic is simultaneously the LM2596's output capacitor **and** the
≥ 220 µF WROVER bulk that ARCHITECTURE-V2 §3 requires within 10 mm of the WROVER 3V3 pads.
Place it at the WROVER and route the regulator output *through* it. One part, two jobs.

**As laid out:** `C3` at (51.5, 37.5), U1 pad 2 at (59.25, 31.51) — **9.8 mm**, inside the
limit (and `C5` 100 nF at 4.1 mm, `C4` 10 µF at 7.2 mm for the HF end). The regulator output leaves `L1`, runs to `C3` on 1.0 mm copper and continues from `C3`
to U1 pad 2; the WROVER is no longer fed through a 0.4 mm trace from the far side of the board.
Making that fit required moving `C4`/`C5` (the WROVER HF decoupling, still 3–5 mm from the pad)
and the `R1`/`R2`/`C6` ACC-sense divider a few millimetres — none of them are placement-critical
in the way `C3` is.

The 100 nF at each WROVER 3V3 pad and each transceiver VCC stay regardless — they are the
rail's decoupling, not the regulator's.

### 7.7 Small part choices

- **C1 is 100 µF/50 V, not 25 V.** The SMBJ24A clamps at **38.9 V**; a 25 V electrolytic sits
  below the clamp. Survivable for a millisecond transient, but the 50 V part costs the same and
  removes the only latent-failure item in the protection chain.
- **F1 is a 1812 SMD polyfuse**, not radial THT. Self-resetting (no fuse to lose inside a
  dashboard), hand-solderable at 1812, and it keeps the input loop tight for EMC.
- **D1 is SMA, not DO-201 THT.** The universal SMA/SMB handsolder pad is oversized and easy
  with an iron; a THT package buys nothing here and worsens the loop.
- **C7 is 1210, not 0805.** A 22 µF X7R in 0805 derates ~60 % at bias — an 0805 part would
  deliver ~9 µF.
- **D1 is SS56 (60 V) and D6 is SS36 (60 V), not the 40 V SS54/SS34.** The same reasoning that
  put a 50 V electrolytic on C1 applies to the diodes: the SMBJ24A clamps at **38.9 V**, and D6
  in particular sees the full input voltage in reverse every time the LM2596's switch turns on —
  plus switch-node ringing on top. 40 V parts leave ~1 V of margin against a clamped transient.
  60 V parts are the same package at the same price. `Diode:SS56` does not exist in KiCad 10, so
  D1 uses the generic `Device:D_Schottky` with value `SS56`; D6 does the same with `SS36`
  (`Diode:SS36` exists but D6 is kept on the generic symbol for consistency with D1).
- **R8 is 220 Ω, not 1 k.** D5 is blue: Vf 2.8–3.2 V. From a 3.3 V GPIO a 1 k ballast leaves
  ~0.1–0.4 mA and a brightness that depends entirely on the LED lot. 220 Ω gives 0.5–2 mA.
  D4 (green, Vf ≈ 2.0 V) keeps its 1 k — that is ~1.3 mA, which is fine.
- **R5/R6 are 0 Ω**, see §6.
- **All KiCad diode/LED symbols are pad 1 = cathode, pad 2 = anode.** Every `.A`/`.K`
  reference from the source documents was resolved accordingly in `design.json`.

---

## 8. Custom library parts

Three parts are not in the official KiCad 10 libraries and must be drawn:

| Part | Symbol | Footprint | Risk |
|---|---|---|---|
| ESP32-WROVER-IB | `MeganeCAN:ESP32-WROVER-IB` | `MeganeCAN:ESP32-WROVER-IB` | Low — geometry vector-extracted from datasheet Figure 11 |
| ESP32-C3 SuperMini | `MeganeCAN:ESP32-C3-SuperMini` | `MeganeCAN:MODULE_ESP32-C3-SUPERMINI` | Medium — clone variance |
| Mini-360 | `MeganeCAN:Mini360` | `MeganeCAN:MINI360_4pad_Socket` | **High — no published drawing** |

Everything else uses official symbols and footprints, all verified present on disk in
KiCad 10.0.4.

> **The single change that would silently ruin the board:** the WROVER is a **19 + 19 two-row**
> part with **no bottom row**. `RF_Module:ESP32-WROOM-32` is 14 left + 10 bottom + 14 right.
> Do not start from it and stretch it. `RF_Module:ESP32-S2-WROVER` shares the 18 × 31.4 body
> but is a 42-pin 1.5 mm-pitch part — also not a donor.

---

## 9. Layout notes

- **Both layers ground pour**, stitched generously, connected to pads with **thermal reliefs**
  (2 spokes minimum). The pours were originally set to solid pad connection; on a board this
  owner hand-solders, that turns every through-hole ground pin — three Phoenix screw terminals,
  a JST-XH, four radial electrolytics (two of them 10 mm), two 6 mm tactiles — into a two-layer
  heatsink and produces cold joints. Two per-pad overrides stay **solid**: `U5`'s TO-263 tab
  (it wants the thermal path, and it has 12 vias in it) and the WROVER's three castellated
  perimeter GND pads (1/15/38 — a 0.9 mm edge pad cannot resolve two thermal spokes, which the
  `min_resolved_spokes: 2` rule flags). The WROVER's exposed GND pad 39 keeps its **thermal
  relief**, which was already a deliberate choice: a solid connection there floats the module
  when hand-soldering.
- Track widths actually used: **12 V input and the switch node 1.5 mm** (`V12_SEL` is 0.6 mm for
  the ~4 mm between JP1's centre pad and F1 — a 1.5 mm trace cannot escape a 1.3 mm-pitch solder
  jumper), **V12_PROT 1.2 mm**, **+3V3 / +5V / +5V_BUCK 1.0 mm**, **signals 0.4 mm**. The earlier
  "power 1.5–2.0 mm" rule was not achievable for the 3V3/5V distribution, which has to reach
  0805 and SOIC pads; 1.0 mm is the honest figure and is 2.5× the copper the WROVER feed
  previously had.
- **CAN pairs** (CANH/CANL to J2 and J3) routed as tight pairs **on F.Cu, side by side, no layer
  changes**, away from the U5 switch node. The previous layout ran one leg of each pair on the
  opposite layer — `CANL_V` had an 18 mm B.Cu span while `CANH_V` was entirely F.Cu, and the
  multimedia pair was the mirror image. That is not a differential pair: it destroys the coupling
  the pair exists for and converts differential drive into common-mode current, on a bus tapped
  from a vehicle harness.
- **CAN transient protection**: `D7`/`D8` (NUP2105L dual-line CAN TVS, SOT-23) sit between each
  transceiver and its screw terminal. The 12 V input was protected and the two bus pairs — the
  other two conductors that leave this board into a car — were not.
- **WROVER antenna area** (the 6.22 mm strip at the module's antenna end, Y < −9.48 mm in
  footprint coordinates): the datasheet keep-out is drawn for the PCB-antenna **-B** variant.
  Our **-IB** uses an external IPEX antenna, so for this board it is a *mechanical* clearance
  for the U.FL connector and its pigtail rather than an RF requirement, and may be relaxed if
  area is tight. **Do not put the WROVER's decoupling there regardless.**
- **Under the C3 module** there is ~8.5 mm of height and a 13.44 mm inter-row corridor. Parts
  placed there must be ≤ 7 mm tall and solderable *before* the headers are fitted. Prefer to
  keep it for GND pour and routing only.
- **C3 (220 µF)** within 10 mm of the WROVER 3V3 pads — see §7.6. **Achieved: 9.8 mm.**
- **U5 tab** ≥ 2 cm² pour, ≥ 8 thermal vias. **Achieved: 12 vias (0.6/0.3 mm) inside the
  9.4 × 10.8 mm tab pad, on a 2.3 × 3.0 mm grid.** They were missing entirely before, which invalidated the 35 °C/W
  assumption §2.2's 98 °C junction estimate rests on: with no path to the bottom pour, the tab
  was heatsunk by top copper alone.
- **`EN` stays local to the module.** `R3` and `C15` sit 3.3 mm from U1 pad 3; the header stubs
  hang off `EN_EXT` behind `R16` (470 Ω). Before, `EN` was the longest signal net on the board —
  113 mm and 14 vias — with its filter capacitor 35 mm from the pin it was supposed to protect,
  and it was brought out raw on two headers. The symptom of that is intermittent, unreproducible
  resets in the car.

---

## 10. Before fab — checklist

1. **Caliper the Mini-360** and patch the four pad coordinates in `MINI360_4pad_Socket`. (§7.4)
2. **Caliper the C3 SuperMini** row-to-row spacing (expect 15.24 mm) and the 17.78 mm pin span.
3. **Photograph the C3 silkscreen** and confirm the left row reads `5V G 3V3 0 1 2 3 4` downward
   with USB up. This is the one item where published sources disagreed — the vendor diagram
   transcriptions say the GPIO run **ascends away from USB**, contradicting an earlier brief.
4. Confirm GPIO20/GPIO21 order at the far corner of the C3.
5. Confirm the Mini-360 silkscreen corner orientation (IN+/IN− vs OUT+/OUT−) against the
   footprint pin-1 marker.
6. Confirm the -IB U.FL connector XY position and mated-cable height from datasheet Figure 9
   (allow ~6.5 mm total height for a mated pigtail) before finalising any enclosure.
7. ~~Silkscreen~~ — **done, in the board file.** Every field-wired connector now carries per-pin
   silk (`J1 BAT/ACC/GND`, `J2`/`J3` `CANH/CANL/GND`, `J4 GWTX/GWRX/GND`, `J5` jig pinout,
   `J7 BUCK-ALT`), the jumpers carry `ACC<->BAT SEL` / `CUT 1-2 FIRST` and `BENCH ONLY`,
   `PS1` keeps `TRIM POT TO 5.00V BEFORE FITTING C3` and there is an `IO2 LED TO GND` note by
   D5 and a `POPULATE ONE` note between PS1 and J7. Previously the board had **no board-level
   silkscreen text at all** — three visually identical 3-pin screw terminals, one of which is
   12 V, with nothing to tell them apart.
8. Dry-fit the C3 into its female headers before soldering the headers to the carrier.
9. **Source the parts introduced by this revision** — the BOM lines with an empty `LCSC` field:
   `D1` SS56, `D6` SS36, `D7`/`D8` NUP2105L, `R5`/`R6` 0 Ω 0805, `R8` 220 Ω 0805, `R16` 470 Ω
   0805, `C17` 10 µF/50 V X7R 1210, `JP1` (3-pad solder jumper — a footprint, not a purchase).

## 11. Bring-up order

1. **No modules fitted.** Apply 12 V to J1.2/J1.3 (ACC — `JP1` selects it from the factory).
   Verify V12_PROT ≈ 12 V and +3V3 = 3.3 V.
2. Verify reverse polarity does nothing (D1 blocks).
3. **Fit PS1**, trim its pot to **5.00 V** measured at PS1.3, then verify +5V ≈ 4.55 V after D3.
4. **Fit the C3.** Confirm it enumerates over USB with 12 V *disconnected* (backfeed path).
   Expect +3V3 to sit at a few hundred millivolts, not 0 V — R13/R14/R15 leak a couple of
   milliamps into the rail through the C3's ESD diodes by design (§7.3). It must not reach 2.7 V;
   if it does, one of those three resistors is missing or shorted.
5. **Solder the WROVER.** Flash through J5 with a USB-serial jig — **with 12 V applied.**
   J5.1 is an *output*: the 3V3 rail comes only from U5, and the C3's USB backfeed reaches the
   +5V rail only. A CP2102/CH340 jig's on-chip regulator (typically ≤ 100 mA) cannot carry a rail
   budgeted at 646 mA peak; driving 3.3 V into J5.1 will brown out mid-flash. The silkscreen at
   J5 marks pin 1 `3V3` for reference, not for supply.
6. Verify the link UART loops between the two MCUs before connecting either CAN bus.
7. CAN last, with JP2/JP3 **open** in the car.
