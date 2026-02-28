#include "Affa2Display.h"

void Affa2Display::setMediaInfo(const AppleMediaService::MediaInformation &info)
{
    String title  = String(info.mTitle.c_str());
    String artist = String(info.mArtist.c_str());

    // Build scroll string: "Artist - Title" when artist is known, else just title.
    // Append 8 spaces so the wrap-around looks clean on the 8-segment display.
    String full;
    if (artist.length() > 0)
        full = artist + " - " + title;
    else
        full = title;

    full += "        "; // 8-space gap before the text loops back

    _scrollTitle = full;
    _scrollPos   = 0;
    _lastScrollMs = 0;

    Serial.printf("[Affa2] setMediaInfo: scrolling \"%s\"\n", full.c_str());
}

void Affa2Display::tickMedia()
{
    if (_scrollTitle.length() == 0)
        return;

    uint32_t now = millis();
    if (now - _lastScrollMs < SCROLL_INTERVAL_MS)
        return;
    _lastScrollMs = now;

    uint16_t len = _scrollTitle.length();

    // Extract VISIBLE_CHARS window, wrapping circularly
    char buf[VISIBLE_CHARS + 1];
    for (uint8_t i = 0; i < VISIBLE_CHARS; i++)
        buf[i] = _scrollTitle[(_scrollPos + i) % len];
    buf[VISIBLE_CHARS] = '\0';

    setText(buf);

    _scrollPos = (_scrollPos + 1) % len;
}
