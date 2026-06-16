# CarminatDisplay decomposition — target skeleton

`CarminatDisplay` (~1146 lines) mixes 6 concerns. Target: a thin **coordinator** that
owns the panel driver, delegating each feature to a clean collaborator that depends only
on a small **panel port** (not on CarminatDisplay). Each collaborator becomes a base for
extension — the point of the framework.

## Concerns today (member/method → home)
| Concern | Members / methods | → |
|---|---|---|
| **Panel driver** (AFFA3 transport + screen builders) | `affa3_send` (base), `showMenu`, `showInfoMenu`, `showConfirmBoxWithOffsets`, `highlightItem`, `setText`, `setState`, `setTime`, `getPacketFiller`, `initializeFuncs` | **stays** in CarminatDisplay (this *is* "how to draw on the Carminat panel") |
| **Sync state machine** | `tick`, `recv` | **stays** (AFFA3 handshake + Carminat regframes) |
| **Menu + navigation** | `mainMenu` (Menu), `initializeMenu`, `ProcessKey`, `onKeyPressed`, `processEvents`, `_currentPage`, `pushPage`/`popPage` | → **MenuController** |
| **Now-playing (media)** | `_mediaInfo`, `_mediaLine2Full`, `_mediaPlayerName`, `_scrollPos`, `_lastMediaRenderMs`, `_lastScrollStepMs`, `MEDIA_*`, `setMediaInfo`, `tickMedia`, `renderMediaScreen`, `buildProgressLine`, `buildScrollingTitle`, `setAuxMode` | → **NowPlaying** |
| **Notifications** | `_lastNotifUid`, `_notifUntilMs`, `renderNotificationScreen` | → **NowPlaying** (notifs overlay the media screen, share its state) |
| **ELM / diag** | `_elm`, `onElmUpdate`, `attachElm`, `_diagPages` (DiagPage) | → **DiagController** (DiagPage already separate) |

## The panel port
Collaborators render through a tiny interface, not the concrete display:
```cpp
struct ICarminatPanel {
    virtual AffaError showMenu(const char* hdr, const char* a, const char* b, uint8_t scroll) = 0;
    virtual AffaError setText(const char* text, uint8_t digit) = 0;
    virtual AffaError highlightItem(uint8_t id) = 0;
};
```
`CarminatDisplay` implements it (it already has these methods). `NowPlaying` /
`MenuController` take `ICarminatPanel&` — so they're unit-testable against a fake panel,
and a future Android-radio / different panel just provides another `ICarminatPanel`.

## Target shape
- **CarminatDisplay**: coordinator + `ICarminatPanel`. `setMediaInfo`/`tickMedia`/
  `setAuxMode` → `_nowPlaying`; key events → `_menu`; `onElmUpdate` → menu live fields +
  `_diag`; `recv`/`tick` → base sync + route keys to `_menu`.
- **NowPlaying(ICarminatPanel&)**: media + notif state + rendering. ~300 lines out.
- **MenuController(Menu&, ICarminatPanel&)**: menu model + nav + page stack. ~150 lines.
- **DiagController(ICarminatPanel&)**: ELM live values + DiagPage stack.

## Order (low-risk first; verify on the REAL panel when it arrives)
1. **NowPlaying** (media + notif) — biggest cohesive chunk, clear state boundary.
2. **MenuController** — verify via full-emu menu + real panel keys.
3. **DiagController** — ELM optional/off by default.

Each step is a behavior-preserving move (cut methods+state into the collaborator;
CarminatDisplay delegates), `build` + `pio test -e native` green, then verify on the
real display: menu renders + navigates, now-playing scrolls, notification pops over.
