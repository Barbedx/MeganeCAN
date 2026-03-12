#pragma once
#include <Arduino.h>

struct TrackInfo {
    String title;
    String artist;
    String album;
    uint32_t trackNumber = 0;
    uint32_t totalTracks = 0;
    uint32_t durationMs = 0;
};