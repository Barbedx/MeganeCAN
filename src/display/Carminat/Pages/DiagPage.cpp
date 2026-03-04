#include "DiagPage.h"
#include "../CarminatDisplay.h"

DiagPage::DiagPage(CarminatDisplay& display, MyELMManager* elm,
                   const String& header, const String& title)
    : _display(display), _elm(elm), _header(header), _title(title) {}

// ---- IPage interface ----

void DiagPage::onEnter() {
    _topIdx = 0;
    _metrics.clear();
    if (_elm) {
        // Force-enable this header for DiagPage regardless of web UI setting
        _wasHeaderEnabled = _elm->isHeaderEnabled(_header.c_str());
        _elm->setHeaderEnabled(_header.c_str(), true);
        _elm->setFocusHeader(_header);
        _metrics = _elm->getCachedMetrics(_header);
    }
    refresh();
    _lastRefreshMs = millis(); // arm timer AFTER initial render to avoid immediate double-refresh
}

void DiagPage::onExit() {
    if (_elm) {
        _elm->clearFocusHeader();
        // Restore original enabled state
        _elm->setHeaderEnabled(_header.c_str(), _wasHeaderEnabled);
    }
}

void DiagPage::onTick() {
    uint32_t now = millis();
    if (now - _lastRefreshMs >= 1000) {
        _lastRefreshMs = now;
        if (_elm) _metrics = _elm->getCachedMetrics(_header);
        refresh();
    }
}

void DiagPage::handleKey(AffaCommon::AffaKey key, bool isHold) {
    if (isHold && key == AffaCommon::AffaKey::Load) {
        _display.popPage();
        return;
    }
    switch (key) {
    case AffaCommon::AffaKey::RollDown:
        if (_topIdx + 4 < _metrics.size()) {
            _topIdx += 4;
            refresh();
        }
        break;
    case AffaCommon::AffaKey::RollUp:
        if (_topIdx >= 4) {
            _topIdx -= 4;
            refresh();
        }
        break;
    default:
        break;
    }
}

// ---- formatting ----

String DiagPage::formatSingle(const MetricSnapshot& snap) const {
    char buf[12];
    if (!snap.hasValue) {
        snprintf(buf, sizeof(buf), "%s:---", snap.shortName.c_str());
    } else if (snap.unit.length() > 0) {
        snprintf(buf, sizeof(buf), "%s:%.0f%s", snap.shortName.c_str(), snap.value, snap.unit.c_str());
    } else {
        snprintf(buf, sizeof(buf), "%s:%.0f", snap.shortName.c_str(), snap.value);
    }
    String s(buf);
    if (s.length() > 8) s = s.substring(0, 8);
    return s;
}

uint8_t DiagPage::computeScroll() const {
    size_t count = _metrics.size();
    if (count == 0) return 0x00;
    bool atTop    = (_topIdx == 0);
    bool atBottom = (_topIdx + 4 >= count);
    if (atTop && atBottom) return 0x00;
    if (atTop)    return 0x0B; // down only
    if (atBottom) return 0x07; // up only
    return 0x0C;               // both
}

void DiagPage::refresh() {
    auto slot = [&](size_t i) -> String {
        return (i < _metrics.size()) ? formatSingle(_metrics[i]) : String("");
    };

    char row0buf[20], row1buf[20];
    snprintf(row0buf, sizeof(row0buf), "%-8s %-8s", slot(_topIdx).c_str(),     slot(_topIdx + 1).c_str());
    snprintf(row1buf, sizeof(row1buf), "%-8s %-8s", slot(_topIdx + 2).c_str(), slot(_topIdx + 3).c_str());

    // Trim trailing spaces when second slot is empty
    String row0(row0buf); row0.trim();
    String row1(row1buf); row1.trim();

    _display.showMenu(_title.c_str(), row0.c_str(), row1.c_str(), computeScroll());
}
