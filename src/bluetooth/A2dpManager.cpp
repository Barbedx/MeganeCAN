#include "A2dpManager.h"
#include "AudioTools.h"
#include "BluetoothA2DPSink.h"
#include <esp_heap_caps.h>

static BluetoothA2DPSink s_sink;
static A2dpManager* s_instance = nullptr;

const char* A2dpManager::playStatusToString(esp_avrc_playback_stat_t status) {
    switch (status) {
        case ESP_AVRC_PLAYBACK_STOPPED: return "STOPPED";
        case ESP_AVRC_PLAYBACK_PLAYING: return "PLAYING";
        case ESP_AVRC_PLAYBACK_PAUSED: return "PAUSED";
        case ESP_AVRC_PLAYBACK_FWD_SEEK: return "FWD_SEEK";
        case ESP_AVRC_PLAYBACK_REV_SEEK: return "REV_SEEK";
        case ESP_AVRC_PLAYBACK_ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

const char* A2dpManager::connectionStateToString(esp_a2d_connection_state_t state) {
    switch (state) {
        case ESP_A2D_CONNECTION_STATE_DISCONNECTED: return "DISCONNECTED";
        case ESP_A2D_CONNECTION_STATE_CONNECTING: return "CONNECTING";
        case ESP_A2D_CONNECTION_STATE_CONNECTED: return "CONNECTED";
        case ESP_A2D_CONNECTION_STATE_DISCONNECTING: return "DISCONNECTING";
        default: return "UNKNOWN";
    }
}

const char* A2dpManager::audioStateToString(esp_a2d_audio_state_t state) {
    switch (state) {
        case ESP_A2D_AUDIO_STATE_REMOTE_SUSPEND: return "REMOTE_SUSPEND";
        case ESP_A2D_AUDIO_STATE_STOPPED: return "STOPPED";
        case ESP_A2D_AUDIO_STATE_STARTED: return "STARTED";
        default: return "UNKNOWN";
    }
}

TrackInfo::PlaybackState A2dpManager::playbackStateFromAvrc(esp_avrc_playback_stat_t status) {
    switch (status) {
        case ESP_AVRC_PLAYBACK_STOPPED: return TrackInfo::PlaybackState::Stopped;
        case ESP_AVRC_PLAYBACK_PLAYING: return TrackInfo::PlaybackState::Playing;
        case ESP_AVRC_PLAYBACK_PAUSED: return TrackInfo::PlaybackState::Paused;
        case ESP_AVRC_PLAYBACK_FWD_SEEK: return TrackInfo::PlaybackState::ForwardSeek;
        case ESP_AVRC_PLAYBACK_REV_SEEK: return TrackInfo::PlaybackState::ReverseSeek;
        case ESP_AVRC_PLAYBACK_ERROR: return TrackInfo::PlaybackState::Error;
        default: return TrackInfo::PlaybackState::Unknown;
    }
}

void A2dpManager::begin(const char* deviceName) {
    s_instance = this;

    Serial.println(F("------------------------------------------------------------"));
    Serial.println(F("STARTING A2DP SINK"));
    Serial.println(F("------------------------------------------------------------"));

    s_sink.set_auto_reconnect(true);
    s_sink.set_stream_reader(audioDataCallback, false);
    s_sink.set_on_connection_state_changed(connectionStateChanged);
    s_sink.set_on_audio_state_changed(audioStateChanged);
    s_sink.set_avrc_metadata_callback(avrcMetadataCallback);
    s_sink.set_avrc_rn_playstatus_callback(avrcPlayStatusCallback);
    s_sink.set_avrc_rn_play_pos_callback(avrcPlayPositionCallback, 1);
    s_sink.set_avrc_rn_track_change_callback(avrcTrackChangeCallback);
    s_sink.start(deviceName);
}

void A2dpManager::tick() {
    if (!isConnectionActive()) {
        return;
    }

    if (_trackInfo.playbackState != TrackInfo::PlaybackState::Playing) {
        return;
    }

    const uint32_t now = millis();
    const uint32_t elapsedMs = now - _positionBaseTickMs;
    uint32_t positionMs = _positionBaseMs + elapsedMs;
    if (_trackInfo.durationMs > 0 && positionMs > _trackInfo.durationMs) {
        positionMs = _trackInfo.durationMs;
    }
    _trackInfo.positionMs = positionMs;
}

bool A2dpManager::isConnected() const {
    return _connected;
}

bool A2dpManager::isConnectionActive() const {
    return _connectionState != ESP_A2D_CONNECTION_STATE_DISCONNECTED;
}

bool A2dpManager::isPlaying() const {
    return _playing;
}

const char* A2dpManager::connectionStateName() const {
    return connectionStateToString(_connectionState);
}

const char* A2dpManager::audioStateName() const {
    return audioStateToString(_audioState);
}

const char* A2dpManager::playbackStatusName() const {
    return playStatusToString(_playbackStatus);
}

const TrackInfo& A2dpManager::trackInfo() const {
    return _trackInfo;
}

void A2dpManager::playPause() {
    s_sink.play();
}

void A2dpManager::next() {
    s_sink.next();
}

void A2dpManager::previous() {
    s_sink.previous();
}

void A2dpManager::audioDataCallback(const uint8_t* data, uint32_t length) {
    static uint32_t lastPrint = 0;
    uint32_t now = millis();

    if (now - lastPrint > 3000) {
        lastPrint = now;
        Serial.printf("[AUDIO] PCM packet length: %u bytes\n", length);
    }

    (void)data;
}

void A2dpManager::connectionStateChanged(esp_a2d_connection_state_t state, void* ptr) {
    (void)ptr;
    if (!s_instance) return;

    s_instance->_connectionState = state;
    s_instance->_connected = (state == ESP_A2D_CONNECTION_STATE_CONNECTED);
    s_instance->_trackInfo.connected = s_instance->_connected;
    if (state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
        s_instance->_playing = false;
        s_instance->_audioState = ESP_A2D_AUDIO_STATE_STOPPED;
        s_instance->_playbackStatus = ESP_AVRC_PLAYBACK_STOPPED;
        s_instance->_trackInfo.playbackState = TrackInfo::PlaybackState::Stopped;
        s_instance->_trackInfo.positionMs = 0;
        s_instance->_positionBaseMs = 0;
        s_instance->_positionBaseTickMs = millis();
    }
    Serial.printf("[A2DP] Connection state: %s free=%u min=%u largest=%u int=%u\n",
                  connectionStateToString(state),
                  ESP.getFreeHeap(),
                  ESP.getMinFreeHeap(),
                  heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
                  heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
}

void A2dpManager::audioStateChanged(esp_a2d_audio_state_t state, void* ptr) {
    (void)ptr;
    if (!s_instance) return;

    s_instance->_audioState = state;
    s_instance->_playing = (state == ESP_A2D_AUDIO_STATE_STARTED);
    if (state == ESP_A2D_AUDIO_STATE_STARTED &&
        s_instance->_trackInfo.playbackState == TrackInfo::PlaybackState::Paused) {
        s_instance->_trackInfo.playbackState = TrackInfo::PlaybackState::Playing;
        s_instance->_positionBaseMs = s_instance->_trackInfo.positionMs;
        s_instance->_positionBaseTickMs = millis();
    }
    Serial.printf("[A2DP] Audio state: %s free=%u min=%u largest=%u int=%u\n",
                  audioStateToString(state),
                  ESP.getFreeHeap(),
                  ESP.getMinFreeHeap(),
                  heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
                  heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
}

void A2dpManager::avrcMetadataCallback(uint8_t id, const uint8_t* text) {
    if (!s_instance) return;

    String value = text ? String(reinterpret_cast<const char*>(text)) : "";

    switch (id) {
        case ESP_AVRC_MD_ATTR_TITLE:
            s_instance->_trackInfo.title = value;
            Serial.printf("[META] Title: %s\n", s_instance->_trackInfo.title.c_str());
            break;
        case ESP_AVRC_MD_ATTR_ARTIST:
            s_instance->_trackInfo.artist = value;
            Serial.printf("[META] Artist: %s\n", s_instance->_trackInfo.artist.c_str());
            break;
        case ESP_AVRC_MD_ATTR_ALBUM:
            s_instance->_trackInfo.album = value;
            Serial.printf("[META] Album: %s\n", s_instance->_trackInfo.album.c_str());
            break;
        case ESP_AVRC_MD_ATTR_TRACK_NUM:
            s_instance->_trackInfo.trackNumber = value.toInt();
            Serial.printf("[META] Track #: %u\n", s_instance->_trackInfo.trackNumber);
            break;
        case ESP_AVRC_MD_ATTR_NUM_TRACKS:
            s_instance->_trackInfo.totalTracks = value.toInt();
            Serial.printf("[META] Total tracks: %u\n", s_instance->_trackInfo.totalTracks);
            break;
        case ESP_AVRC_MD_ATTR_PLAYING_TIME:
            s_instance->_trackInfo.durationMs = value.toInt();
            Serial.printf("[META] Duration: %u ms\n", s_instance->_trackInfo.durationMs);
            break;
        default:
            Serial.printf("[META] Unknown id %u: %s\n", id, value.c_str());
            break;
    }
}

void A2dpManager::avrcPlayStatusCallback(esp_avrc_playback_stat_t status) {
    if (!s_instance) return;
    s_instance->_playbackStatus = status;
    s_instance->_trackInfo.playbackState = playbackStateFromAvrc(status);
    s_instance->_playing = (status == ESP_AVRC_PLAYBACK_PLAYING);
    if (s_instance->_trackInfo.playbackState == TrackInfo::PlaybackState::Playing) {
        s_instance->_positionBaseMs = s_instance->_trackInfo.positionMs;
    }
    s_instance->_positionBaseTickMs = millis();
    Serial.printf("[AVRCP] Playback status: %s\n", playStatusToString(status));
}

void A2dpManager::avrcPlayPositionCallback(uint32_t playPosMs) {
    if (!s_instance) return;
    s_instance->_positionBaseMs = playPosMs;
    s_instance->_positionBaseTickMs = millis();
    s_instance->_trackInfo.positionMs = playPosMs;
    if (s_instance->_trackInfo.durationMs > 0 && s_instance->_trackInfo.positionMs > s_instance->_trackInfo.durationMs) {
        s_instance->_trackInfo.positionMs = s_instance->_trackInfo.durationMs;
        s_instance->_positionBaseMs = s_instance->_trackInfo.positionMs;
    }
    Serial.printf("[AVRCP] Playback position: %u ms\n", s_instance->_trackInfo.positionMs);
}

void A2dpManager::avrcTrackChangeCallback(uint8_t *id) {
    (void)id;
    if (!s_instance) return;

    s_instance->_trackInfo.positionMs = 0;
    s_instance->_positionBaseMs = 0;
    s_instance->_positionBaseTickMs = millis();
    Serial.println("[AVRCP] Track changed");
}
