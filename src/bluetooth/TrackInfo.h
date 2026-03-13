#pragma once
#include <Arduino.h>

struct TrackInfo {
    enum class PlaybackState : uint8_t {
        Unknown = 0,
        Stopped,
        Playing,
        Paused,
        ForwardSeek,
        ReverseSeek,
        Error,
    };

    String title;
    String artist;
    String album;
    uint32_t trackNumber = 0;
    uint32_t totalTracks = 0;
    uint32_t durationMs = 0;
    uint32_t positionMs = 0;
    PlaybackState playbackState = PlaybackState::Unknown;
    bool connected = false;

    bool isPlaying() const { return playbackState == PlaybackState::Playing; }
    bool isPaused() const { return playbackState == PlaybackState::Paused; }

    static const char *playbackStateName(PlaybackState state)
    {
        switch (state)
        {
        case PlaybackState::Stopped:
            return "STOPPED";
        case PlaybackState::Playing:
            return "PLAYING";
        case PlaybackState::Paused:
            return "PAUSED";
        case PlaybackState::ForwardSeek:
            return "FWD_SEEK";
        case PlaybackState::ReverseSeek:
            return "REV_SEEK";
        case PlaybackState::Error:
            return "ERROR";
        default:
            return "UNKNOWN";
        }
    }
};
