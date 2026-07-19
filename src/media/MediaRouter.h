#pragma once
#include "IMediaSource.h"

// Picks the active media source (ARCHITECTURE-V2 §6.1). Config media_source:
// "ams" | "hu" | "auto" (auto: the HU wins while its link is up and sending,
// else AMS). P0 registers only the AMS source; the HU slot stays null until P1.
// Portable — native tests drive it with fake sources.
class MediaRouter {
public:
    enum class Mode : uint8_t { Ams, Hu, Auto };

    void setSources(IMediaSource* ams, IMediaSource* hu) { _ams = ams; _hu = hu; }
    void setMode(Mode m) { _mode = m; }
    Mode mode() const { return _mode; }
    static Mode modeFromStr(const char* s);
    static const char* modeStr(Mode m);

    // The source the current mode selects (may be inactive — e.g. AMS with no
    // phone connected still renders its status screen). Null only if the slot
    // for the selected mode was never registered.
    IMediaSource* activeSource() const;

    const MediaInfo& current() const;     // active source's info (empty if none)
    bool active() const;                  // active source exists and is live
    const char* statusText() const;
    const char* sourceName() const;

private:
    IMediaSource* _ams = nullptr;
    IMediaSource* _hu = nullptr;
    Mode _mode = Mode::Ams;
};
