#pragma once

#include <Arduino.h>
#include "TrackInfo.h"
#include <esp_a2dp_api.h>
#include <esp_avrc_api.h>

class A2dpManager {
public:
    void begin(const char* deviceName = "MeganeCAN-A2DP");
    void tick();

    bool isConnected() const;
    bool isConnectionActive() const;
    bool isPlaying() const;
    const char* connectionStateName() const;
    const char* audioStateName() const;
    const char* playbackStatusName() const;

    const TrackInfo& trackInfo() const;

    void playPause();
    void next();
    void previous();

private:
    static void audioDataCallback(const uint8_t* data, uint32_t length);
    static void connectionStateChanged(esp_a2d_connection_state_t state, void* ptr);
    static void audioStateChanged(esp_a2d_audio_state_t state, void* ptr);
    static void avrcMetadataCallback(uint8_t id, const uint8_t* text);
    static void avrcPlayStatusCallback(esp_avrc_playback_stat_t status);
    static void avrcPlayPositionCallback(uint32_t playPosMs);
    static void avrcTrackChangeCallback(uint8_t *id);

    static const char* playStatusToString(esp_avrc_playback_stat_t status);
    static const char* connectionStateToString(esp_a2d_connection_state_t state);
    static const char* audioStateToString(esp_a2d_audio_state_t state);
    static TrackInfo::PlaybackState playbackStateFromAvrc(esp_avrc_playback_stat_t status);

private:
    bool _connected = false;
    bool _playing = false;
    esp_a2d_connection_state_t _connectionState = ESP_A2D_CONNECTION_STATE_DISCONNECTED;
    esp_a2d_audio_state_t _audioState = ESP_A2D_AUDIO_STATE_STOPPED;
    esp_avrc_playback_stat_t _playbackStatus = ESP_AVRC_PLAYBACK_STOPPED;
    TrackInfo _trackInfo;
    uint32_t _positionBaseMs = 0;
    uint32_t _positionBaseTickMs = 0;
};
