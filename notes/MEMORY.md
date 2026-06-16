# Memory — how to reason about heap on this board (so we never chase a ghost again)

The classic ESP32 (esp32dev) runs BLE + WiFi + HTTP + AMS all at once on ~224 KB of
usable heap. It is **tight but healthy**, and historically there was no leak — the
fear of "3 days on a memory leak" was really fragmentation + the cost of the `/wire`
viewer. This note records the *method* so the next investigation takes 5 minutes.

## The one observable: `GET /api/health`

```json
{"heap":{"free":58024,"min":42048,"maxblk":40948,"total":225160}, ...}
```
- `free`   — total free heap right now
- `min`    — lowest `free` ever seen since boot (the worst-case watermark)
- `maxblk` — largest *contiguous* block (what a single big alloc can get)
- `ws.clients` — connected `/canstream` WebSocket clients

The loop also prints `[heap] free/min/maxblk` every 10 s and warns when
`maxblk < 20000`. WS connect/disconnect prints `[ws] ... free=.. maxblk=..` so the
heaviest consumer's cost is always visible.

## Decision tree (leak vs steady-state vs fragmentation)

1. **Does `free` recover when clients disconnect / load stops?**
   - Recovers → **NOT a leak.** It was the cost of whatever was connected.
   - Never recovers, `min` keeps dropping run over run → **real leak**, hunt the owner.
2. **Is `free` healthy but `maxblk` ≪ `free`?** → **fragmentation**, not exhaustion.
   Find what allocs/frees many differently-sized blocks (WS sends, String churn).
3. **Both stable but low?** → **steady-state cost.** Raise the floor (below) or accept.

Measured baselines (June 2026, after the mem-floor commit):
- Idle, no WS:           free ~54 KB, maxblk ~35 KB — healthy.
- `/wire` WS streaming:  maxblk holds ~35 KB (before the fix it fragmented to ~14 KB).
- Active media render:   maxblk flat — the render path does NOT fragment (don't
  "optimize" it; de-Stringing NowPlaying was considered and rejected on evidence).

## Levers (what actually works here, and what doesn't)

Works:
- **NimBLE `CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1`** (platformio.ini build_flags) — we
  only link one iPhone; `getClient()` reuses that inbound connection. Freed ~5.4 KB
  free / +6 KB maxblk. Do NOT touch MTU (247 — ANCS bodies), roles (getClient needs
  central), the MSYS mbuf pool, or bonds — those break AMS/ANCS.
- **WS flush cadence** (`WsWireLink::FLUSH_MS`) — fewer sends = less alloc churn =
  less fragmentation while `/wire` is open. 10 Hz idle; `FLUSH_HI` keeps bursts lossless.
- PsychicHttp `max_open_sockets` small + `lru_purge_enable` (already set).

Does NOT work / not worth it here:
- Trimming `max_uri_handlers` — 64 has only ~4 slots over the 55 routes + ElegantOTA;
  going lower risks `ESP_ERR_HTTPD_HANDLERS_FULL` silently dropping routes for ~120 B.
- lwIP / WiFi socket counts and the BT controller heap — baked into the **precompiled
  Arduino** libs. Changing them needs Arduino-as-IDF-component (big, risky) — out of
  scope unless we ever migrate the framework.

## Practical rule

`/wire` is a debugging tool and the single biggest consumer. Leave it closed during
normal use; the firmware drops the whole WS buffer when no client is connected.
