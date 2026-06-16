#pragma once
#include "../AffaCommonConstants.h"
#include "Menu/Menu.h"
#include "IPage.h"

// Owns the Carminat navigation stack: the active full-screen page (DIAG pages) and
// key routing between page / menu / fall-through. Cut verbatim from CarminatDisplay
// — the coordinator's ProcessKey/processEvents/pushPage/popPage now delegate here.
//
// It holds the menu by reference (the coordinator owns mainMenu, also shared with
// NowPlaying); it does not own pages — DiagController creates them and the
// coordinator pushes them in. No frame builder lives here: pages render through the
// display they were given.
class MenuController {
public:
    explicit MenuController(Menu& menu) : _menu(menu) {}

    void   pushPage(IPage* p);
    void   popPage();
    IPage* currentPage() const { return _currentPage; }
    void   tickCurrentPage() { if (_currentPage) _currentPage->onTick(); }

    // Route a key: active page first, else the menu. Returns true when the key was
    // consumed by the menu system (a page handled it, or the menu is active after
    // handling) — i.e. the coordinator should NOT fall through to its keyHandler.
    bool routeKey(AffaCommon::AffaKey key, bool isHold);

private:
    Menu&  _menu;
    IPage* _currentPage = nullptr;
};
