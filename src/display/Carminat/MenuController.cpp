#include "MenuController.h"

// pushPage/popPage cut verbatim from CarminatDisplay (mainMenu -> _menu).
void MenuController::pushPage(IPage* p)
{
    if (!p) return;
    _currentPage = p;
    p->onEnter();
}

void MenuController::popPage()
{
    if (!_currentPage) return;
    _currentPage->onExit();
    _currentPage = nullptr;
    if (_menu.isActive())
        _menu.show();
}

// The key-routing body of the old CarminatDisplay::ProcessKey, minus the keyHandler
// fall-through (which stays in the coordinator, where keyHandler lives). Returns
// whether the menu system consumed the key:
//   - active page present  -> page handles it, consumed.
//   - no page              -> menu handles it; consumed iff the menu is now active.
bool MenuController::routeKey(AffaCommon::AffaKey key, bool isHold)
{
    if (_currentPage) {
        _currentPage->handleKey(key, isHold);
        return true;
    }
    _menu.handleKey(key, isHold);
    return _menu.isActive();
}
