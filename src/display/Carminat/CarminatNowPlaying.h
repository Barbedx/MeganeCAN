#pragma once
#include <Arduino.h>
#include "apple_media_service.h"
#include "apple_notification_service.h"
#include "../IPanel.h"          // renders through the panel port, not the concrete display
#include "Menu/Menu.h"          // reads isActive() — never paint over an open menu
#include "AuxModeTracker.h"     // reads isInAuxMode() — only draw media in AUX

// Now-playing media screen + ANCS notification popup for the Carminat panel.
//
// Cut verbatim from CarminatDisplay so the emitted frames are byte-identical — it
// renders only through IPanel::showMenu (the single choke point that transliterates
// + frames the text). It depends on three small seams, no concrete display:
//   - IPanel&          : how to draw (showMenu)
//   - AuxModeTracker&  : is the radio in AUX (don't draw media otherwise)
//   - Menu&            : is a menu open (don't paint over it)
// The event loop + the queue push stay in the coordinator; this owns state + render.
class CarminatNowPlaying {
public:
    CarminatNowPlaying(IPanel& panel, AuxModeTracker& aux, Menu& menu)
        : _panel(panel), _aux(aux), _menu(menu) {}

    // State update only (was the front of CarminatDisplay::setMediaInfo). The
    // coordinator keeps the eventQueue push that schedules the redraw.
    void setMediaInfo(const AppleMediaService::MediaInformation& info);

    // Per-loop media tick (was CarminatDisplay::tickMedia): BT status, notification
    // popup window, AMS refresh + scroll, then renderMediaScreen.
    void tick();

    // Draw the now-playing screen (was CarminatDisplay::renderMediaScreen). Called by
    // tick() and by the coordinator on a MediaInfoUpdate event.
    void renderMediaScreen(bool forceRedraw = false);

private:
    IPanel&         _panel;
    AuxModeTracker& _aux;
    Menu&           _menu;

    AppleMediaService::MediaInformation _mediaInfo;
    String   _mediaLine2Full;     // full "Artist - Title"
    String   _mediaPlayerName;
    uint32_t _lastMediaRenderMs = 0;
    uint32_t _lastScrollStepMs  = 0;
    uint16_t _scrollPos         = 0;

    static constexpr uint16_t MEDIA_SCROLL_INTERVAL_MS = 400;
    static constexpr uint8_t  MEDIA_VISIBLE_CHARS      = 18;

    String buildProgressLine() const;
    String buildScrollingTitle();

    // ANCS notification popup (shown briefly over the media screen on arrival)
    uint32_t _lastNotifUid = 0;
    uint32_t _notifUntilMs = 0;
    void renderNotificationScreen(const AppleNotificationService::NotificationInfo& n);
};
