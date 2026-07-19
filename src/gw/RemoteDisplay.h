#pragma once
#include "../display/IDisplay.h"
#include "../media/MediaInfo.h"
#include <stdint.h>

// The GW's view of the OEM display (M1): every IDisplay call serializes into a
// DISP_CMD link frame; now-playing state diffs into MEDIA_TEXT frames. The
// peripheral's DISP_CMD server (M2) executes them on its local AFFA3 driver —
// until then the current C3 image simply skips the unknown type (forward-compat)
// while MEDIA_TEXT already renders through its HuLinkMediaSource.
//
// Business logic stays on GW (CR-02): this class holds no display state beyond
// the last-sent media diff.
class RemoteDisplay : public IDisplay {
public:
    using SendFn = bool (*)(uint8_t type, const uint8_t* payload, uint16_t len, void* ctx);
    void begin(SendFn send, void* ctx) { _send = send; _ctx = ctx; }

    // Media push (called by the GW media flow, not via IDisplay): diff-send the
    // changed MEDIA_TEXT fields; service() refreshes periodically so a rebooted
    // peripheral resyncs and its stale-timeout never fires mid-playback.
    void pushMedia(const MediaInfo& info);
    void service(uint32_t nowMs);

    // ---- IDisplay -> DISP_CMD ----
    void tick() override {}
    void recv(const Frame& frame) override { (void)frame; }
    void processEvents() override {}
    AffaCommon::AffaError setText(const char* text, uint8_t digit = 255) override;
    AffaCommon::AffaError setState(bool enabled) override;
    AffaCommon::AffaError setTime(const char* clock) override;
    void ProcessKey(AffaCommon::AffaKey key, bool isHold) override;
    AffaCommon::AffaError showMenu(const char* header, const char* item1,
                                   const char* item2, uint8_t scroll = 0x0B) override;
    AffaCommon::AffaError showInfoPopup(const char* l1, const char* l2, const char* l3) override;
    void hideInfoPopup() override;
    AffaCommon::AffaError showConfirmBox(const char* cap, const char* r1, const char* r2) override;
    AffaCommon::AffaError showFullscreenText(const char* l1, const char* l2, const char* l3) override;
    void hideFullscreenText() override;
    AffaCommon::AffaError showPopupText(const char* text, uint8_t icon = 0x09,
                                        uint8_t srcIcon = 0xFF, uint8_t fmt = 0x60) override;
    void hidePopup() override;
    void setAuxMode(bool on) override;

protected:
    void onKeyPressed(AffaCommon::AffaKey key, bool isHold) override
    { (void)key; (void)isHold; }

private:
    bool sendOp(uint8_t op, const uint8_t* args = nullptr, uint16_t argLen = 0);
    bool sendOpStrings(uint8_t op, const char* s1, const char* s2, const char* s3,
                       const uint8_t* prefix = nullptr, uint8_t prefixLen = 0);
    void sendMediaField(uint8_t field, const char* text);

    SendFn _send = nullptr;
    void* _ctx = nullptr;

    MediaInfo _lastSent;
    bool _everSent = false;
    uint32_t _lastRefreshMs = 0;
    static constexpr uint32_t REFRESH_MS = 10000;   // < HuLinkMediaSource::STALE_MS
};
