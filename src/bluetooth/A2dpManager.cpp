#include "A2dpManager.h"
#include "AudioTools.h"
#include "BluetoothA2DPSink.h"

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

void A2dpManager::begin(const char* deviceName) {
    s_instance = this;

    Serial.println(F("------------------------------------------------------------"));
    Serial.println(F("STARTING A2DP SINK"));
    Serial.println(F("------------------------------------------------------------"));

    s_sink.set_auto_reconnect(false);
    s_sink.set_stream_reader(audioDataCallback, false);
    s_sink.set_on_connection_state_changed(connectionStateChanged);
    s_sink.set_on_audio_state_changed(audioStateChanged);
    s_sink.set_avrc_metadata_callback(avrcMetadataCallback);
    s_sink.set_avrc_rn_playstatus_callback(avrcPlayStatusCallback);
    s_sink.start(deviceName);
}

void A2dpManager::tick() {
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
    if (state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
        s_instance->_playing = false;
    }
    Serial.printf("[A2DP] Connection state: %s\n", connectionStateToString(state));
}

void A2dpManager::audioStateChanged(esp_a2d_audio_state_t state, void* ptr) {
    (void)ptr;
    if (!s_instance) return;

    s_instance->_playing = (state == ESP_A2D_AUDIO_STATE_STARTED);
    Serial.printf("[A2DP] Audio state: %s\n", audioStateToString(state));
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
    Serial.printf("[AVRCP] Playback status: %s\n", playStatusToString(status));
}
