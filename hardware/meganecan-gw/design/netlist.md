# MeganeCAN GW — canonical netlist + BOM

Board: dual-MCU automotive gateway. ESP32-WROVER-IB (GW brain) + ESP32-C3 SuperMini
(DISP co-processor, socketed) + 2x SN65HVD230 CAN transceivers, 12V automotive input.

Pin truth verified against:
- `notes/ARCHITECTURE-V2.md` §3.1–§3.4 (power tree CR-06, pin map §3.2, PCB provisions CR-09/CR-10, CAN tap §3.3)
- `src/gw/gw_main.cpp:40-42` — `PIN_CAN_TX = GPIO_NUM_22`, `PIN_CAN_RX = GPIO_NUM_21`, `PIN_HU_TX = 25`, `PIN_HU_RX = 26`
- `src/gw/GwLink.cpp:22` — `PIN_TX = 18, PIN_RX = 19` (UART2 link, 460800)
- `src/disp/DispLink.cpp:20` — `PIN_TX = 21, PIN_RX = 20` (C3 link UART)
- `src/disp/disp_main.cpp:123` — `CAN0.setCANPins(GPIO_NUM_3, GPIO_NUM_4)` (TX=3, RX=4 argument order is `setCANPins(rxPin, txPin)` in esp32_can → **RX=GPIO3, TX=GPIO4**, matching §3.2)

WROVER pin **names** are used throughout (IO21, TXD0, EN…), not pad numbers — the footprint
pad mapping is produced separately.

---

## 1. BOM

| Ref | Value | Part | Package | Qty note |
|---|---|---|---|---|
| U1 | ESP32-WROVER-IB | Espressif ESP32-WROVER-IB (8MB PSRAM, IPEX ant.) | Module 18.0 x 31.4mm, 38 castellated pads, 1.27mm pitch | Bare module, no regulator. GPIO16/17 PSRAM-reserved, pads NC. |
| U2 | ESP32-C3 SuperMini | ESP32-C3 SuperMini daughterboard | 2x 1x8 2.54mm female header socket | Socketed, not soldered. Own onboard LDO. |
| U3 | SN65HVD230 | TI SN65HVD230DR (vehicle CAN) | SOP-8 / SOIC-8 | 3.3V CAN transceiver |
| U4 | SN65HVD230 | TI SN65HVD230DR (multimedia CAN) | SOP-8 / SOIC-8 | 3.3V CAN transceiver |
| U5 | AMS1117-3.3 | AMS1117-3.3 LDO | SOT-223 | 5V→3.3V, ≥800mA |
| PS1 | Mini-360 | MP2307 buck module (Mini-360) | 4-pin 1x4 2.54mm socket (IN+ IN- OUT+ OUT-) | 12V→5V |
| F1 | 1.1A | Polyfuse PTC, hold 1.1A / trip 2.2A, 30V | Radial THT (or 1812 SMD) | Input protection |
| D1 | SS56 | Schottky **60V** 5A, series reverse-polarity | SMA (DO-214AC) | In series on 12V input, after F1 and the TVS |
| D2 | SMBJ24A | TVS unidirectional 24V standoff | SMB (DO-214AA) | After D1, across 12V→GND |
| D3 | SS34 | Schottky 40V 3A, OR-ing | SMA (DO-214AC) | Buck OUT+ → 5V rail; blocks USB backfeed from fighting the buck |
| D4 | GRN | LED power indicator | 0805 | On 3V3 rail |
| D5 | BLU | LED status indicator | 0805 | Driven by U1.IO2 (safe strap) |
| SW1 | RESET | Tactile switch SPST-NO | 6x6mm THT (or 3x4 SMD) | U1.EN → GND |
| SW2 | BOOT | Tactile switch SPST-NO | 6x6mm THT (or 3x4 SMD) | U1.IO0 → GND |
| J1 | PWR | Screw terminal 3-pin | 5.08mm pitch THT | BAT / ACC / GND |
| J2 | VCAN | Screw terminal 3-pin | 5.08mm pitch THT | CANH / CANL / GND (vehicle CAN) |
| J3 | MMCAN | Screw terminal 3-pin | 5.08mm pitch THT | CANH / CANL / GND (multimedia CAN) |
| J4 | HU | JST-XH 3-pin | 2.50mm pitch THT | DUDU7 canbox UART, 38400 8N1. Nets are `GW_TX_HU_RX` / `GW_RX_HU_TX` — named from **both** ends, because `HU_TX` reads to an installer as the head unit's transmitter, which is the opposite wire |
| J5 | JIG | Pin header 1x6 | 2.54mm THT | 3V3 / TX0 / RX0 / EN / IO0 / GND — CR-09 WROVER flash jig |
| J6 | EXP | Pin header 1x12 | 2.54mm THT | CR-10 expansion |
| JP1 | SRC-SEL | Solder jumper, **3-pad, 1-2 bridged from the factory** | `Jumper:SolderJumper-3_P1.3mm_Bridged12_RoundedPad1.0x1.5mm` | Selects **either** ACC (pad 1, default) **or** BAT (pad 3) onto pad 2 → F1. A 2-pad jumper here would have *paralleled* BAT onto the ACC harness wire, not selected between them — see power.md §4 |
| JP2 | VCAN-TERM-H | Solder jumper, 2-pad, **OPEN by default** | SJ 2-pad 1.27mm | Vehicle-CAN split termination, **H leg** |
| JP4 | VCAN-TERM-L | Solder jumper, 2-pad, **OPEN by default** | SJ 2-pad 1.27mm | Vehicle-CAN split termination, **L leg** — close with JP2 or not at all |
| JP3 | MMCAN-TERM-H | Solder jumper, 2-pad, **OPEN by default** | SJ 2-pad 1.27mm | MM-CAN split termination, H leg |
| JP5 | MMCAN-TERM-L | Solder jumper, 2-pad, **OPEN by default** | SJ 2-pad 1.27mm | MM-CAN split termination, L leg |
| R13 | 1k | Resistor 5% | 0805 | Series isolation, C3 GPIO21 → WROVER IO19 (see DESIGN.md §7.3) |
| R14 | 1k | Resistor 5% | 0805 | Series isolation, C3 GPIO4 → U4 D |
| R15 | 1k | Resistor 5% | 0805 | Series isolation, C3 GPIO3 ← U4 R |
| R16 | 470R | Resistor 5% | 0805 | EN ↔ EN_EXT isolation, keeps J5/J6 cabling off the WROVER reset pin |
| C17 | 10uF/50V X7R | Ceramic | 1210 | LM2596 input bulk ceramic, at U5.VIN |
| C18 | 100nF/50V X7R | Ceramic | 0805 | Mini-360 local input ceramic, at PS1.IN+ |
| D7 | NUP2105L | Dual-line CAN TVS, 24 Vrwm | SOT-23 | Vehicle CAN bus transient protection at J2 |
| D8 | NUP2105L | Dual-line CAN TVS, 24 Vrwm | SOT-23 | Multimedia CAN bus transient protection at J3 |
| R1 | 47k | Resistor 1% | 0805 | ACC sense divider, top |
| R2 | 10k | Resistor 1% | 0805 | ACC sense divider, bottom → IO34 |
| R3 | 10k | Resistor 5% | 0805 | EN pull-up to 3V3 |
| R4 | 10k | Resistor 5% | 0805 | IO0 pull-up to 3V3 |
| R5 | **0R** | Resistor 0R jumper | 0805 | U3 RS to GND — high-speed mode. Was 10k (slew-limited, ~130 ns/edge); the pads are kept so 10k can be refitted if EMC ever demands it |
| R6 | **0R** | Resistor 0R jumper | 0805 | U4 RS to GND — high-speed mode |
| R7 | 1k | Resistor 5% | 0805 | Power LED series |
| R8 | 1k | Resistor 5% | 0805 | Status LED series |
| R9 | 60.4R | Resistor 1% | 0805 | Vehicle CAN split term, H side |
| R10 | 60.4R | Resistor 1% | 0805 | Vehicle CAN split term, L side |
| R11 | 60.4R | Resistor 1% | 0805 | MM CAN split term, H side |
| R12 | 60.4R | Resistor 1% | 0805 | MM CAN split term, L side |
| C1 | 100µF/25V | Electrolytic | Radial THT 6.3mm | 12V input bulk |
| C2 | 100nF/50V | Ceramic X7R | 0805 | 12V input HF bypass |
| C3 | 220µF/10V | Electrolytic (low-ESR) | Radial THT 6.3mm | 3V3 bulk at U1 3V3/GND (§3.1 requires ≥220µF) |
| C4 | 10µF/16V | Ceramic X5R | 0805 | 3V3 at U1 |
| C5 | 100nF/50V | Ceramic X7R | 0805 | 3V3 at U1, close to pad |
| C6 | 10µF/16V | Ceramic X5R | 0805 | U5 VIN |
| C7 | 22µF/10V | Ceramic X5R | 0805 | U5 VOUT (AMS1117 stability) |
| C8 | 100nF/50V | Ceramic X7R | 0805 | U3 VCC decoupling |
| C9 | 10µF/16V | Ceramic X5R | 0805 | U3 VCC bulk |
| C10 | 100nF/50V | Ceramic X7R | 0805 | U4 VCC decoupling |
| C11 | 10µF/16V | Ceramic X5R | 0805 | U4 VCC bulk |
| C12 | 100nF/50V | Ceramic X7R | 0805 | Vehicle CAN split-term centre → GND |
| C13 | 100nF/50V | Ceramic X7R | 0805 | MM CAN split-term centre → GND |
| C14 | 47µF/10V | Electrolytic | Radial THT 5mm | 5V rail bulk |
| C15 | 100nF/50V | Ceramic X7R | 0805 | EN RC delay (with R3) |

**Total: 50 components** (17 unique passive values).

---

## 2. Net list

Format `NETNAME: ref.pin, ref.pin, …`

### Power input & protection
```
V12_BAT:      J1.1, JP1.1
V12_IN:       J1.2, JP1.2, F1.1, R1.1
V12_FUSED:    F1.2, D1.A
V12_PROT:     D1.K, D2.1, C1.1, C2.1, PS1.IN+
GND:          J1.3, J2.3, J3.3, J4.3, J5.6, J6.3,
              D2.2, C1.2, C2.2, PS1.IN-, PS1.OUT-,
              U5.GND, C6.2, C7.2, C14.2,
              C3.2, C4.2, C5.2, C15.2,
              U1.GND, U1.GND2, U1.GND3, U1.EP,
              U2.GND,
              U3.GND, C8.2, C9.2, R5.2, C12.2,
              U4.GND, C10.2, C11.2, R6.2, C13.2,
              R2.2, R8.2, D4.K,
              SW1.2, SW2.2
```

### 5V rail
```
+5V_BUCK:     PS1.OUT+, D3.A
+5V:          D3.K, C14.1, C6.1, U5.VIN, U2.5V, J6.2
```
`U2.5V` is bidirectional by design: bench USB into the C3 backfeeds the 5V rail
(documented maintenance path). D3 blocks that current from entering the buck output.

### 3.3V rail
```
+3V3:         U5.VOUT, U5.TAB, C7.1,
              C3.1, C4.1, C5.1, U1.3V3,
              U3.VCC, C8.1, C9.1,
              U4.VCC, C10.1, C11.1,
              R3.1, R4.1, R7.1,
              J5.1, J6.1
```
Note: U2 (C3 SuperMini) makes its own 3.3V from its onboard LDO — its 3V3 pin is
**not** connected to this rail.

### ACC ignition sense
```
ACC_SENSE:    R1.2, R2.1, U1.IO34
```
47k/10k from 12V → 2.10V at 12.0V, 2.63V at 15.0V. Within IO34's input-only ADC range.

### WROVER strapping / boot / console
```
EN:           U1.EN, R3.2, C15.1, SW1.1, J5.4, J6.4
IO0:          U1.IO0, R4.2, SW2.1, J5.5
TXD0:         U1.TXD0, J5.2
RXD0:         U1.RXD0, J5.3
```
`U1.IO12` — **NO CONNECTION.** MTDI is the flash-voltage strap; must float (no pull-up,
no test point, no pour stitch). Explicitly unrouted per §3.1.

### Status / power LEDs
```
LED_STAT:     U1.IO2, D5.A
LED_STAT_K:   D5.K, R8.1
PWR_LED_A:    R7.2, D4.A
```
IO2 is a safe strap (must be low or floating at boot); the LED-to-GND path pulls it low.

### Inter-board link UART (LinkProto, 460800 8N1)
```
LINK_GW_TX:      U1.IO18, U2.GPIO20
LINK_GW_RX:      U1.IO19, R13.2
LINK_GW_RX_C3:   R13.1,   U2.GPIO21
```
Cross-connected: GW TX(IO18) → C3 RX(GPIO20); C3 TX(GPIO21) → R13 (1k) → GW RX(IO19).
The 1 k is not optional: the C3 keeps running on USB when the 3V3 rail is dead, and without it
GPIO21 back-powers that rail through the WROVER's ESD clamp diodes. See DESIGN.md §7.3.

### Head-unit UART → DUDU7 canbox (38400 8N1)
```
GW_TX_HU_RX:  U1.IO25, J4.1     [GW transmits, head unit receives]
GW_RX_HU_TX:  U1.IO26, J4.2     [GW receives, head unit transmits]
```
J4.1 is GW TX → HU RX; J4.2 is GW RX ← HU TX; J4.3 = GND.

### Vehicle CAN (U3, WROVER TWAI, 500k)
```
VCAN_TX:      U1.IO22, U3.D
VCAN_RX:      U1.IO21, U3.R
VCAN_RS:      U3.RS, R5.1
CANH_V:       U3.CANH, J2.1, JP2.1
CANL_V:       U3.CANL, J2.2, R10.2
VCAN_TERM_H:  JP2.2, R9.1
VCAN_SPLIT:   R9.2, R10.1, C12.1
```
`U3.Vref` — NO CONNECTION (leave open).
Termination is normally **absent** (we tap an already-terminated bus, §3.3): JP2 open.

### Multimedia CAN (U4, C3 TWAI, 500k)
```
MCAN_TX:      U2.GPIO4, U4.D
MCAN_RX:      U2.GPIO3, U4.R
MCAN_RS:      U4.RS, R6.1
CANH_M:       U4.CANH, J3.1, JP3.1
CANL_M:       U4.CANL, J3.2, R12.2
MCAN_TERM_H:  JP3.2, R11.1
MCAN_SPLIT:   R11.2, R12.1, C13.1
```
`U4.Vref` — NO CONNECTION.

### Expansion header J6 (CR-10)
```
EXP_IO4:      U1.IO4,  J6.5
EXP_IO5:      U1.IO5,  J6.6
EXP_IO13:     U1.IO13, J6.7
EXP_IO14:     U1.IO14, J6.8
EXP_IO23:     U1.IO23, J6.9
EXP_IO27:     U1.IO27, J6.10
EXP_IO32:     U1.IO32, J6.11
EXP_IO33:     U1.IO33, J6.12
```
(J6.1=+3V3, J6.2=+5V, J6.3=GND, J6.4=EN — listed on their respective power nets above.)

**Total: 30 nets.**

### Deliberately unconnected
| Pin | Reason |
|---|---|
| U1.IO16, U1.IO17 | PSRAM-reserved on WROVER; module pads are NC. Never route. |
| U1.IO12 | Flash-voltage strap — must float. |
| U3.Vref, U4.Vref | SN65HVD230 Vref output, unused. |
| U1.IO34 partner pins IO35/VP/VN | spare, not brought out (J6 is full at 12 pins). |
| U2.3V3 | C3 has its own LDO; do not tie to the board 3V3 rail. |

---

## 3. Machine-readable

```json
{
  "components": [
    {"ref": "U1",  "value": "ESP32-WROVER-IB", "part": "ESP32-WROVER-IB", "package": "Module_18x31.4mm_38pad_1.27mm"},
    {"ref": "U2",  "value": "ESP32-C3 SuperMini", "part": "ESP32-C3-SuperMini", "package": "Socket_2x1x8_2.54mm"},
    {"ref": "U3",  "value": "SN65HVD230", "part": "SN65HVD230DR", "package": "SOIC-8_3.9x4.9mm_P1.27mm"},
    {"ref": "U4",  "value": "SN65HVD230", "part": "SN65HVD230DR", "package": "SOIC-8_3.9x4.9mm_P1.27mm"},
    {"ref": "U5",  "value": "AMS1117-3.3", "part": "AMS1117-3.3", "package": "SOT-223-3_TabPin2"},
    {"ref": "PS1", "value": "Mini-360", "part": "MP2307 Mini-360 buck module", "package": "PinHeader_1x04_P2.54mm"},
    {"ref": "F1",  "value": "1.1A", "part": "PTC polyfuse 1.1A/30V", "package": "Fuse_Radial_THT"},
    {"ref": "D1",  "value": "SS56", "part": "SS56 Schottky 60V 5A", "package": "D_SMA"},
    {"ref": "D2",  "value": "SMBJ24A", "part": "SMBJ24A TVS", "package": "D_SMB"},
    {"ref": "D3",  "value": "SS34", "part": "SS34 Schottky 40V 3A", "package": "D_SMA"},
    {"ref": "D4",  "value": "GRN", "part": "LED green", "package": "LED_0805"},
    {"ref": "D5",  "value": "BLU", "part": "LED blue", "package": "LED_0805"},
    {"ref": "SW1", "value": "RESET", "part": "Tactile SPST-NO", "package": "SW_Push_6x6mm_THT"},
    {"ref": "SW2", "value": "BOOT", "part": "Tactile SPST-NO", "package": "SW_Push_6x6mm_THT"},
    {"ref": "J1",  "value": "PWR", "part": "Screw terminal 3P 5.08mm", "package": "TerminalBlock_1x03_P5.08mm"},
    {"ref": "J2",  "value": "VCAN", "part": "Screw terminal 3P 5.08mm", "package": "TerminalBlock_1x03_P5.08mm"},
    {"ref": "J3",  "value": "MMCAN", "part": "Screw terminal 3P 5.08mm", "package": "TerminalBlock_1x03_P5.08mm"},
    {"ref": "J4",  "value": "HU", "part": "JST-XH 3P", "package": "JST_XH_B3B-XH-A_1x03_P2.50mm"},
    {"ref": "J5",  "value": "JIG", "part": "Pin header 1x6", "package": "PinHeader_1x06_P2.54mm"},
    {"ref": "J6",  "value": "EXP", "part": "Pin header 1x12", "package": "PinHeader_1x12_P2.54mm"},
    {"ref": "JP1", "value": "BAT-SEL", "part": "Solder jumper 2-pad (open)", "package": "SolderJumper-2_P1.3mm_Open"},
    {"ref": "JP2", "value": "VCAN-TERM", "part": "Solder jumper 2-pad (open)", "package": "SolderJumper-2_P1.3mm_Open"},
    {"ref": "JP3", "value": "MMCAN-TERM", "part": "Solder jumper 2-pad (open)", "package": "SolderJumper-2_P1.3mm_Open"},
    {"ref": "R1",  "value": "47k",   "part": "Resistor 1%", "package": "R_0805"},
    {"ref": "R2",  "value": "10k",   "part": "Resistor 1%", "package": "R_0805"},
    {"ref": "R3",  "value": "10k",   "part": "Resistor 5%", "package": "R_0805"},
    {"ref": "R4",  "value": "10k",   "part": "Resistor 5%", "package": "R_0805"},
    {"ref": "R5",  "value": "0R",    "part": "Resistor 0R jumper", "package": "R_0805"},
    {"ref": "R6",  "value": "0R",    "part": "Resistor 0R jumper", "package": "R_0805"},
    {"ref": "R7",  "value": "1k",    "part": "Resistor 5%", "package": "R_0805"},
    {"ref": "R8",  "value": "1k",    "part": "Resistor 5%", "package": "R_0805"},
    {"ref": "R9",  "value": "60.4R", "part": "Resistor 1%", "package": "R_0805"},
    {"ref": "R10", "value": "60.4R", "part": "Resistor 1%", "package": "R_0805"},
    {"ref": "R11", "value": "60.4R", "part": "Resistor 1%", "package": "R_0805"},
    {"ref": "R12", "value": "60.4R", "part": "Resistor 1%", "package": "R_0805"},
    {"ref": "C1",  "value": "100uF/25V", "part": "Electrolytic", "package": "CP_Radial_D6.3mm_P2.50mm"},
    {"ref": "C2",  "value": "100nF",     "part": "Ceramic X7R 50V", "package": "C_0805"},
    {"ref": "C3",  "value": "220uF/10V", "part": "Electrolytic low-ESR", "package": "CP_Radial_D6.3mm_P2.50mm"},
    {"ref": "C4",  "value": "10uF",      "part": "Ceramic X5R 16V", "package": "C_0805"},
    {"ref": "C5",  "value": "100nF",     "part": "Ceramic X7R 50V", "package": "C_0805"},
    {"ref": "C6",  "value": "10uF",      "part": "Ceramic X5R 16V", "package": "C_0805"},
    {"ref": "C7",  "value": "22uF",      "part": "Ceramic X5R 10V", "package": "C_0805"},
    {"ref": "C8",  "value": "100nF",     "part": "Ceramic X7R 50V", "package": "C_0805"},
    {"ref": "C9",  "value": "10uF",      "part": "Ceramic X5R 16V", "package": "C_0805"},
    {"ref": "C10", "value": "100nF",     "part": "Ceramic X7R 50V", "package": "C_0805"},
    {"ref": "C11", "value": "10uF",      "part": "Ceramic X5R 16V", "package": "C_0805"},
    {"ref": "C12", "value": "100nF",     "part": "Ceramic X7R 50V", "package": "C_0805"},
    {"ref": "C13", "value": "100nF",     "part": "Ceramic X7R 50V", "package": "C_0805"},
    {"ref": "C14", "value": "47uF/10V",  "part": "Electrolytic", "package": "CP_Radial_D5.0mm_P2.00mm"},
    {"ref": "C15", "value": "100nF",     "part": "Ceramic X7R 50V", "package": "C_0805"}
  ],
  "nets": {
    "V12_BAT":     ["J1.1", "JP1.1"],
    "V12_IN":      ["J1.2", "JP1.2", "F1.1", "R1.1"],
    "V12_FUSED":   ["F1.2", "D1.A"],
    "V12_PROT":    ["D1.K", "D2.1", "C1.1", "C2.1", "PS1.IN+"],
    "GND":         ["J1.3", "J2.3", "J3.3", "J4.3", "J5.6", "J6.3",
                    "D2.2", "C1.2", "C2.2", "PS1.IN-", "PS1.OUT-",
                    "U5.GND", "C6.2", "C7.2", "C14.2",
                    "C3.2", "C4.2", "C5.2", "C15.2",
                    "U1.GND", "U1.GND2", "U1.GND3", "U1.EP",
                    "U2.GND",
                    "U3.GND", "C8.2", "C9.2", "R5.2", "C12.2",
                    "U4.GND", "C10.2", "C11.2", "R6.2", "C13.2",
                    "R2.2", "R8.2", "D4.K", "SW1.2", "SW2.2"],
    "+5V_BUCK":    ["PS1.OUT+", "D3.A"],
    "+5V":         ["D3.K", "C14.1", "C6.1", "U5.VIN", "U2.5V", "J6.2"],
    "+3V3":        ["U5.VOUT", "U5.TAB", "C7.1",
                    "C3.1", "C4.1", "C5.1", "U1.3V3",
                    "U3.VCC", "C8.1", "C9.1",
                    "U4.VCC", "C10.1", "C11.1",
                    "R3.1", "R4.1", "R7.1", "J5.1", "J6.1"],
    "ACC_SENSE":   ["R1.2", "R2.1", "U1.IO34"],
    "EN":          ["U1.EN", "R3.2", "C15.1", "SW1.1", "J5.4", "J6.4"],
    "IO0":         ["U1.IO0", "R4.2", "SW2.1", "J5.5"],
    "TXD0":        ["U1.TXD0", "J5.2"],
    "RXD0":        ["U1.RXD0", "J5.3"],
    "LED_STAT":    ["U1.IO2", "D5.A"],
    "LED_STAT_K":  ["D5.K", "R8.1"],
    "PWR_LED_A":   ["R7.2", "D4.A"],
    "LINK_GW_TX":  ["U1.IO18", "U2.GPIO20"],
    "LINK_GW_RX":  ["U1.IO19", "U2.GPIO21"],
    "GW_TX_HU_RX": ["U1.IO25", "J4.1"],
    "GW_RX_HU_TX": ["U1.IO26", "J4.2"],
    "VCAN_TX":     ["U1.IO22", "U3.D"],
    "VCAN_RX":     ["U1.IO21", "U3.R"],
    "VCAN_RS":     ["U3.RS", "R5.1"],
    "CANH_V":      ["U3.CANH", "J2.1", "JP2.1"],
    "CANL_V":      ["U3.CANL", "J2.2", "R10.2"],
    "VCAN_TERM_H": ["JP2.2", "R9.1"],
    "VCAN_SPLIT":  ["R9.2", "R10.1", "C12.1"],
    "MCAN_TX":     ["U2.GPIO4", "U4.D"],
    "MCAN_RX":     ["U2.GPIO3", "U4.R"],
    "MCAN_RS":     ["U4.RS", "R6.1"],
    "CANH_M":      ["U4.CANH", "J3.1", "JP3.1"],
    "CANL_M":      ["U4.CANL", "J3.2", "R12.2"],
    "MCAN_TERM_H": ["JP3.2", "R11.1"],
    "MCAN_SPLIT":  ["R11.2", "R12.1", "C13.1"],
    "EXP_IO4":     ["U1.IO4",  "J6.5"],
    "EXP_IO5":     ["U1.IO5",  "J6.6"],
    "EXP_IO13":    ["U1.IO13", "J6.7"],
    "EXP_IO14":    ["U1.IO14", "J6.8"],
    "EXP_IO23":    ["U1.IO23", "J6.9"],
    "EXP_IO27":    ["U1.IO27", "J6.10"],
    "EXP_IO32":    ["U1.IO32", "J6.11"],
    "EXP_IO33":    ["U1.IO33", "J6.12"]
  },
  "no_connect": ["U1.IO12", "U1.IO16", "U1.IO17", "U3.Vref", "U4.Vref", "U2.3V3"],
  "series_isolation": {
    "R13": "LINK_GW_RX_C3 -> LINK_GW_RX  (1k, C3 GPIO21 out -> WROVER IO19)",
    "R14": "MCAN_TX_C3 -> MCAN_TX        (1k, C3 GPIO4 out -> U4 D)",
    "R15": "MCAN_RX_C3 -> MCAN_RX        (1k, C3 GPIO3 in  <- U4 R)",
    "R16": "EN -> EN_EXT                 (470R, WROVER reset node -> J5.4/J6.4)"
  }
}
```

---

## 4. Open questions

1. **ACC sense reads the selected source, not true ACC.** R1 taps `V12_PROT`, downstream of
   JP1, so if JP1 is re-bridged 2-3 (BAT) the divider reports battery, not ignition — ignition
   sense becomes meaningless in that mode. Fix option: move R1.1 to J1.2 exclusively
   (pre-jumper). Currently benign: ACC is the factory selection and the firmware does not use
   IO34 yet. (Note the jumper is now a 3-pad *selector*; it can no longer parallel the two
   sources, which was a separate and more serious problem — see power.md §4.)
2. **WROVER GND pad count.** Netlist assumes 3 perimeter GND pads + exposed pad
   (`GND`/`GND2`/`GND3`/`EP`). The custom footprint must confirm against the ESP32-WROVER-B
   datasheet — pad-name mapping is produced by the parallel footprint task.
3. **AMS1117-3.3 thermal headroom.** 5V→3.3V at WROVER WiFi-TX peaks (~250mA avg, 500mA
   burst) = 0.43–0.85W in SOT-223. Needs a copper pour on the tab. Consider an MP1584/
   AP63203 buck or a lower-dropout part if bench measurement runs hot.
4. **Split-termination value with a series jumper.** 60.4R + 60.4R = 120.8R nominal, but the
   solder jumper adds its own bridge resistance in the H leg only — slight asymmetry.
   Acceptable for an optional/never-populated feature; flag if termination is ever used.
5. **C3 SuperMini 5V backfeed current path.** D3 protects the buck, but with USB attached
   the C3's LDO must carry the whole 3.3V rail (WROVER + 2 transceivers) if U5 is fed from
   the same 5V node — that is exactly the "thermally marginal" interim case §3.1 warns about.
   Confirm bench USB supplies enough, or add a second jumper to isolate U5.VIN from U2.5V.
6. **No 120R vs. bench use.** §3.3 mandates open termination in-car. On the bench, with no
   other node, both CAN controllers will bus-off. JP2/JP3 are the intended bench remedy —
   worth documenting on the silkscreen.
7. **J6 EXP has no I²C/SPI labelling.** CR-10 asks for "UART, SPI, I²C". The 8 spare GPIOs
   can serve any of those (IO14/IO13 = HSPI-capable, IO32/IO33 = generic), but the header is
   unlabelled by function. Confirm whether a fixed pinout convention is wanted on silk.
8. **IO2 status LED and boot.** IO2 must be low/floating at boot; an LED to GND satisfies
   that, but if the LED is ever swapped for a pull-up-driven indicator the board will fail
   to enter download mode. Silkscreen note recommended.
