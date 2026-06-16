#include "DiagController.h"
#include "CarminatDisplay.h"
#include "Pages/DiagPage.h"
#include "ElmManager/MyELMManager.h"
#include <math.h>

DiagController::~DiagController()
{
    // Own the pages we new'd in attachElm (the monolith leaked these; the
    // coordinator is effectively a singleton so it never showed at runtime, but
    // owning them here keeps the lifetime honest).
    for (auto& kv : _diagPages) delete kv.second;
    _diagPages.clear();
}

// attachElm / onElmUpdate cut verbatim from CarminatDisplay.
void DiagController::attachElm(MyELMManager* m)
{
    _elm = m;
    _diagPages["7E0"] = new DiagPage(_display, m, "7E0", "ENGINE");
    _diagPages["743"] = new DiagPage(_display, m, "743", "GEARBOX");
    _diagPages["744"] = new DiagPage(_display, m, "744", "HVAC");
    _diagPages["745"] = new DiagPage(_display, m, "745", "ECU 745");
    _diagPages["74D"] = new DiagPage(_display, m, "74D", "ALT GBX");
}

void DiagController::onElmUpdate(const char* key, float value)
{
    if (strcmp(key, "PR071") == 0)
    {
        _menu.updateFieldExternally("Voltage", 0, (int)roundf(value));
    }
    else if (strcmp(key, "DRV_BOOST") == 0)
    {
        _menu.updateFieldExternally("Boost", 0, (int)roundf(value));
    }
}

DiagPage* DiagController::page(const String& header)
{
    auto it = _diagPages.find(header);
    return (it != _diagPages.end()) ? it->second : nullptr;
}
