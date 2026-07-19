#pragma once
#include "IMediaSource.h"
#include <stdint.h>

// The DUDU7 head unit as a media source, fed by MEDIA_TEXT / HU_STATUS frames
// arriving over LinkProto (ARCHITECTURE-V2 §6.1). The DUDU media-metadata
// message is P4's RE target — this source goes live the moment VH starts
// emitting MEDIA_TEXT; until then active() stays false and MediaRouter's
// "auto" keeps falling back to AMS.
class HuLinkMediaSource : public IMediaSource {
public:
    // Field ids = LinkProto::MediaField. State text: "1" playing, else paused.
    void onMediaText(uint8_t field, const char* text, uint16_t len, uint32_t nowMs);
    void setLinkUp(bool up) { _linkUp = up; }

    const MediaInfo& current() const override { return _info; }
    bool active() const override;
    const char* statusText() const override { return _linkUp ? "HU link up" : "HU link down"; }
    const char* name() const override { return "hu"; }

    // Media considered stale after this long without any MEDIA_TEXT.
    static constexpr uint32_t STALE_MS = 30000;

    uint32_t lastTextMs() const { return _lastTextMs; }
    void setNow(uint32_t nowMs) { _nowMs = nowMs; }   // updated by the service loop

private:
    MediaInfo _info;
    bool _linkUp = false;
    uint32_t _lastTextMs = 0;
    uint32_t _nowMs = 0;
};
