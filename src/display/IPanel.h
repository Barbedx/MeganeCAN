#pragma once
#include "AffaCommonConstants.h"

// IPanel — the minimal RENDERING port: "how to draw on the panel", nothing else.
//
// The decomposed CarminatDisplay collaborators (NowPlaying / MenuController /
// DiagController) render through this small interface instead of the concrete
// display, so they depend only on the three primitives they actually use and are
// unit-testable against a fake panel. A future WebPanel (render a ScreenModel to
// the web, no CAN) implements the same port — the "display can be virtual" seam.
//
// The signatures of showMenu/setText are deliberately identical to IDisplay's, so
// a single CarminatDisplay override satisfies both bases with no ambiguity
// (interfaces carry no data — no diamond). highlightItem already exists on
// CarminatDisplay returning AffaError. No defaults here: a rendering caller passes
// every argument explicitly; the concrete display keeps IDisplay's defaults.
struct IPanel {
    virtual ~IPanel() = default;

    virtual AffaCommon::AffaError showMenu(const char *header, const char *item1,
                                           const char *item2, uint8_t scrollLockIndicator) = 0;
    virtual AffaCommon::AffaError setText(const char *text, uint8_t digit) = 0;
    virtual AffaCommon::AffaError highlightItem(uint8_t id) = 0;
};
