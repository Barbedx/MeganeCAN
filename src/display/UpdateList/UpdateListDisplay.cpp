#include "UpdateListDisplay.h"
#include "utils/TextUtils.h"

void UpdateListDisplay::setMediaInfo(const TrackInfo info)
{
    //TODO:check play state
    bool nowPlaying = true;// (info.mPlaybackState == AppleMediaService::MediaInformation::PlaybackState::Playing);

    String title  = String(info.title.c_str());
    String artist = String(info.artist.c_str());

    String full;
    if (artist.length() > 0)
        full = artist + " - " + title;
    else
        full = title;

    full = normalizeTitle(full);
    full += "        "; // 8-space gap before wrap-around

    // Only reset scroll position when the title content actually changes.
    if (full != _scrollTitle)
    {
        _scrollTitle  = full;
        _scrollPos    = 0;
        _lastScrollMs = 0;
    }

    if (nowPlaying && !_isPlaying)
    {
        // Resumed — clear the "title shown once" flag so scroll restarts.
        _titleShownOnce = false;
    }

    _isPlaying = nowPlaying;
    Serial.printf("[UpdateList] setMediaInfo: playing=%d title=\"%s\"\n", _isPlaying, full.c_str());
}

void UpdateListDisplay::tickMedia()
{
    uint32_t now = millis();

    if (isTransientActive(now))
        return;

    if (_scrollTitle.length() == 0)
        return;

    if (!_isPlaying)
    {
        // Paused/stopped: show static title once then freeze.
        if (!_titleShownOnce)
        {
            char buf[VISIBLE_CHARS + 1];
            for (uint8_t i = 0; i < VISIBLE_CHARS; i++)
                buf[i] = _scrollTitle[i < _scrollTitle.length() ? i : 0];
            buf[VISIBLE_CHARS] = '\0';
            setText(buf);
            _titleShownOnce = true;
        }
        return;
    }

    if (now - _lastScrollMs < SCROLL_INTERVAL_MS)
        return;
    _lastScrollMs = now;

    uint16_t len = _scrollTitle.length();

    char buf[VISIBLE_CHARS + 1];
    for (uint8_t i = 0; i < VISIBLE_CHARS; i++)
        buf[i] = _scrollTitle[(_scrollPos + i) % len];
    buf[VISIBLE_CHARS] = '\0';

    setText(buf);
    _scrollPos = (_scrollPos + 1) % len;
}

void UpdateListDisplay::onBtDisconnected()
{
    Serial.println("[UpdateList] BT disconnected — freezing display");
    _isPlaying = false;
    // Don't reset _titleShownOnce — keeps the frozen title visible without re-sending.
}

void UpdateListDisplay::onRadioText(bool isAux)
{
    if (!isAux)
        return;
    // Radio switched to AUX source — re-assert our content.
    // If paused: allow tickMedia to re-send the static title on next tick.
    // If playing: scroll continues normally on next tickMedia tick.
    Serial.println("[UpdateList] AUX detected from radio — re-asserting display");
    _titleShownOnce = false;
}
