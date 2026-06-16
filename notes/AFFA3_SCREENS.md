# AFFA3 screen command map (Carminat, 0x151)

Reverse-engineered from live-car captures in `OneDrive/Desktop/logs affa3 new` (decoded
with `tools/affa_decode.py`). All screens are ISO-TP messages on CAN id **0x151**; the
display ACKs on **0x551**. First byte after the ISO-TP length is the **screen command**.

| Screen | Cmd | Wire (content after `10 <len>`) | Builder |
|---|---|---|---|
| Now-playing / menu (windowed) | `0x21` mode `0x01` | `21 01 7E 80 00 00 82 FF <scroll> <hdr26> .. 00 7E <item1> 01 7F <item2>` | `showMenu` |
| **Fullscreen big text** | `0x21` mode `0x05` | `21 05 FF 00 00 40` + 26×`00` + text@off32, `\r`-separated, space-padded, 96 B | `showFullscreenText` |
| Full-window text popup | `0x74` | `74 09 55 FF 60 01 <≤14 chars>` (e.g. `VOL  9`, `AUX  ON`) | (raw `tx` for now) |
| Not-full radio text | `0x77` | `77 55 55 FF 60 01 <≤14 chars>` | `setText` |
| Info-menu list item | `0x76` | `76 60 <rowcode> <8 chars>` (one ISO-TP msg per item) | `showInfoMenu` |
| Close full window / popup | `0x54` | single frame `02 54 03` | `hideFullscreenText` |

## The key insight: mode byte = window vs fullscreen
The `0x21` screen command's **2nd byte is the layout mode**:
- `0x01` → **windowed**: a menu/now-playing box; the panel still shows the radio context
  and later overlays (menu/volume) replace it.
- `0x05` → **fullscreen**: the panel takes the whole screen (the "Please insert
  navigation CD" message). While fullscreen is up, the radio renders volume/settings as
  **popups over it** using the `0x74` full-window text command, and dismisses them with
  `54 03`. This matches the `0x74`/`0x77` = "full / not-full" note in `setText()`.

## Fullscreen text layout (cmd 0x21 mode 0x05, len 0x60 = 96 B content)
```
[0..5]   21 05 FF 00 00 40        cmd, mode, header
[6..31]  00 ...                   zero header region (26 B)
[32..33] 20 20                    two leading spaces
[34..95] text block               lines separated by 0x0D ('\r'), space-padded
```
Captured payload for "Please insert / navigation CD":
```
21 05 FF 00 00 40 00*26 20 20 "Please insert    \r  navigation CD    \r ... \r"
```

## Info-menu: full list vs popup
- **Full settings list** = command `0x76` (`showInfoMenu`), one `76 60 <rowcode> <text>`
  message per row. Captured rows: `AUX  ON` (row `0x41`), `AF   ON` (`0x44`),
  `SPEED 0` (`0x48`). Rowcode/format ranges are noted in `showInfoMenu()`.
  NOTE: real captures use prefix **`0x60`**; our `showInfoMenu` default is `0x70` — set
  `infoPrefix=0x60` to match the OEM exactly.
- **Popup (over fullscreen)** = the `0x74` full-window text command, a **single line**
  (this is why "settings in popup … displayed only 1 item"). Not the `0x76` list.

## ISO-TP build rule (how the builders feed affa3_send)
`affa3_do_send` adds the `0x2N` PCI for consecutive frames itself. The caller pre-builds
`10 <len>` + the **first 6 content bytes**, then the rest of the content **contiguously**
(no per-frame PCI). `len` = total content byte count (e.g. `0x5A`=90 menu, `0x60`=96
fullscreen, `0x0E`=14 text, `0x0B`=11 info row). See `showMenu` / `showFullscreenText`.

## Other channels & screens (from /canstream captures, 2026-06-17)
- **0x1C1** = key/button events (ACK 0x5C1). `03 89 <keyHi> <keyLo>`; RollUp=`01 01`,
  Load=`00 00`, +hold OR 0xC0 into keyLo (`03 89 00 C0`). `sendPasswordSequence` = 5-3-2-1.
- **0x1F1** = IMAGE channel (the "planet"/nav bitmap), ACK 0x5F1. Independent of radio text.
  ISO-TP first frame `11 2E` → **302-byte** payload. Header starts
  `21 0B 00 25 41 42 43 44 45 46 00 01 30 …` then bitmap data. (302 B ≈ 1bpp ~48×50.)
  The burst is ~43 frames in ~200ms and the firmware drops a chunk each time (CAN RX
  throughput, NOT the WS buffer) — so capture several cold boots and `affa_decode.py
  --id 1F1 --merge` the concatenation to union the fragments into the full bitmap.
- Funcreg/registration handshake spans 0x1C1/0x151/0x1F1 (replies `70 …`, acks `74 …`).

## "CODE" / password screen (0x151, windowed text)
Header `77 55 55 FF 60 01` (mode 0x77, icon 0x55, fmt 0x60 plain). Renders in two steps:
1. `"  CODE  "`  (`21 20 20 43 4F 44 45 20`)
2. entry field `"  <cur>000 "` (`21 20 20 B0 30 30 30 20`) — **`0xB0` = blink cursor on
   digit 0**, advancing `B0→B1→…` as digits are accepted. After 5-3-2-1 the radio shows
   normal text (`77 09 55 FF 31 01 "   1056"` = "105.6").

## Tools
- `tools/extract_log.py "<saved-page>.html"` → strips the proxy HTML dump to plain text.
- `tools/affa_decode.py LOG [--id 151] [--merge]` → reassembles ISO-TP and decodes each
  screen. `--merge` overlays fragments of a static screen by sequence index, recovering
  the full payload from **lossy** captures (dropped CFs from serial saturation).
- Proxy: **Mark** button (labelled divider), **Save log** (raw `.log` download). Run the
  firmware with `vb 0` (default) so continuous renders don't saturate serial and drop
  frames; `vb 1` for the full ISO-TP trace.

## Serial commands (drive any screen, no WiFi)
`menu cap i1 i2 [scrollHex]` · `full l1 l2 l3` / `fclose` · `info l1 l2 l3` / `infox` ·
`popup cap r1 r2` · `txt text [digit]` · `tx <id> <bytes>` (raw) · `@INJ <id> <bytes>` ·
`vb 0|1`. Text fields encode spaces as `_` and empty as `~` (the proxy buttons do this).
