#pragma once
#include <stdint.h>
#include <string>

// Source-neutral now-playing model (ARCHITECTURE-V2 §6.1). Everything the display
// drivers and the web UI need, with no dependency on where it came from — Apple
// AMS today, the DUDU7 head unit over LinkProto later. Portable (no Arduino) so
// the media layer builds in the native test env.
struct MediaInfo {
    // Values mirror AMS's wire encoding so AmsMediaSource can static_cast; the HU
    // source maps its own states onto these.
    enum class PlaybackState : uint8_t {
        Paused = 0,
        Playing = 1,
        Rewinding = 2,
        FastForwarding = 3
    };
    enum class ShuffleMode : uint8_t { Off = 0, One = 1, All = 2 };
    enum class RepeatMode  : uint8_t { Off = 0, One = 1, All = 2 };

    std::string title;
    std::string artist;
    std::string album;
    std::string playerName;   // app/source name shown as the screen header

    PlaybackState playbackState = PlaybackState::Paused;
    float playbackRate = 0.0f;
    float elapsedTime  = 0.0f;   // seconds, as of lastUpdateMs
    float duration     = 0.0f;   // seconds, 0 = unknown
    float volume       = 0.0f;   // 0.0 .. 1.0

    int queueIndex = 0;
    int queueCount = 0;
    ShuffleMode shuffleMode = ShuffleMode::Off;
    RepeatMode  repeatMode  = RepeatMode::Off;

    // Clock ms when elapsedTime/playbackState were last set by the source
    // (0 = never). Lets consumers extrapolate elapsed without polling the source.
    uint32_t lastUpdateMs = 0;

    bool playing() const { return playbackState == PlaybackState::Playing; }

    // Elapsed seconds extrapolated to `nowMs`. While playing, the position keeps
    // advancing at playbackRate between source updates (the extrapolation that
    // CarminatNowPlaying::tick() used to do by re-pulling AMS every tick).
    float elapsedAt(uint32_t nowMs) const {
        if (playbackState != PlaybackState::Playing || lastUpdateMs == 0)
            return elapsedTime;
        return elapsedTime + (float)(nowMs - lastUpdateMs) / 1000.0f * playbackRate;
    }
};
