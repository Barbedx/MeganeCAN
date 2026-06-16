# AFFA3 popup icon codes (`icon` byte)

Catalog of the popup overlay's `icon` header byte — byte 1 of
`0x151  10 0E 74 <icon> 55 <srcIcon> <fmt> 01 <text>` (see `showPopupText`).

Drive it from `/preview` (Popup overlay card, `icon` field) or:
`GET /api/popup?text=<t>&icon=<HH>&src=FF&fmt=60`  (HH = hex).

The captured "VOL 28" volume popup used `icon=09`.

| icon (hex) | glyph / effect | notes |
|------------|----------------|-------|
| 09 | (volume context) | value used by the real radio's "VOL nn" popup |
| 94 | — (empty) | no icon shown |
| 9B | traffic | shows a traffic icon; the list blinked when sent |

The icon codes **repeat cyclically** across the 0x00–0xFF range (the same glyph set
recurs), so there's no need to sweep all 256 — a small window covers the set.

The `icon` / `src` / `fmt` bytes are exposed as hex inputs on the `/preview`
"Popup overlay" card, so any value can be tried directly without a sweep.
(`src` = byte 3, `fmt` = byte 4 — still to catalog.)
