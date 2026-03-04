#pragma once
#include "../IPage.h"
#include "ElmManager/MyELMManager.h"
#include <vector>

class Affa3NavDisplay;

class DiagPage : public IPage {
public:
    DiagPage(Affa3NavDisplay& display, MyELMManager* elm,
             const String& header, const String& title);

    void onEnter()  override;
    void onExit()   override;
    void onTick()   override;
    void handleKey(AffaCommon::AffaKey key, bool isHold) override;

    const String& getHeader() const { return _header; }

private:
    void refresh();
    String formatSingle(const MetricSnapshot& snap) const; // max 8 chars
    uint8_t computeScroll() const;

    Affa3NavDisplay& _display;
    MyELMManager*    _elm;
    String           _header;       // ECU filter, e.g. "7E0"
    String           _title;        // display header, e.g. "ENGINE"
    size_t           _topIdx          = 0;
    uint32_t         _lastRefreshMs   = 0;
    bool             _wasHeaderEnabled = true;
    std::vector<MetricSnapshot> _metrics;
};
