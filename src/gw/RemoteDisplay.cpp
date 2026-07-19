#include "RemoteDisplay.h"
#include "../link/LinkProto.h"
#include <string.h>

using namespace LinkProto;
using Err = AffaCommon::AffaError;

bool RemoteDisplay::sendOp(uint8_t op, const uint8_t* args, uint16_t argLen)
{
    if (!_send) return false;
    uint8_t p[MAX_PAYLOAD];
    if (argLen > MAX_PAYLOAD - 1) argLen = MAX_PAYLOAD - 1;
    p[0] = op;
    if (argLen) memcpy(p + 1, args, argLen);
    return _send(DISP_CMD, p, (uint16_t)(1 + argLen), _ctx);
}

bool RemoteDisplay::sendOpStrings(uint8_t op, const char* s1, const char* s2,
                                  const char* s3, const uint8_t* prefix, uint8_t prefixLen)
{
    uint8_t args[MAX_PAYLOAD];
    uint16_t n = 0;
    for (uint8_t i = 0; i < prefixLen && n < sizeof(args); i++)
        args[n++] = prefix[i];
    const char* strs[3] = { s1, s2, s3 };
    for (int i = 0; i < 3; i++)
    {
        const char* s = strs[i] ? strs[i] : "";
        while (*s && n < sizeof(args) - 1) args[n++] = (uint8_t)*s++;
        args[n++] = 0;
    }
    return sendOp(op, args, n);
}

Err RemoteDisplay::setText(const char* text, uint8_t digit)
{
    uint8_t args[MAX_PAYLOAD];
    uint16_t n = 0;
    args[n++] = digit;
    for (const char* c = text ? text : ""; *c && n < sizeof(args) - 1; c++)
        args[n++] = (uint8_t)*c;
    return sendOp(DO_SET_TEXT, args, n) ? Err::NoError : Err::SendFailed;
}

Err RemoteDisplay::setState(bool enabled)
{
    uint8_t on = enabled ? 1 : 0;
    return sendOp(DO_SET_STATE, &on, 1) ? Err::NoError : Err::SendFailed;
}

Err RemoteDisplay::setTime(const char* clock)
{
    uint16_t n = clock ? (uint16_t)strlen(clock) : 0;
    return sendOp(DO_SET_TIME, (const uint8_t*)clock, n) ? Err::NoError : Err::SendFailed;
}

void RemoteDisplay::ProcessKey(AffaCommon::AffaKey key, bool isHold)
{
    // Remote key inject (/emulate/key): let the peripheral's real display+menu
    // consume it exactly as a physical SWC press would.
    uint16_t k = AffaCommon::to_uint16(key);
    uint8_t args[3] = { (uint8_t)(k & 0xFF), (uint8_t)(k >> 8), (uint8_t)(isHold ? 1 : 0) };
    sendOp(DO_KEY, args, sizeof(args));
}

Err RemoteDisplay::showMenu(const char* header, const char* item1,
                            const char* item2, uint8_t scroll)
{
    return sendOpStrings(DO_SHOW_MENU, header, item1, item2, &scroll, 1)
               ? Err::NoError : Err::SendFailed;
}

Err RemoteDisplay::showInfoPopup(const char* l1, const char* l2, const char* l3)
{
    return sendOpStrings(DO_INFO_POPUP, l1, l2, l3) ? Err::NoError : Err::SendFailed;
}

void RemoteDisplay::hideInfoPopup() { sendOp(DO_HIDE_INFO); }

Err RemoteDisplay::showConfirmBox(const char* cap, const char* r1, const char* r2)
{
    return sendOpStrings(DO_CONFIRM, cap, r1, r2) ? Err::NoError : Err::SendFailed;
}

Err RemoteDisplay::showFullscreenText(const char* l1, const char* l2, const char* l3)
{
    return sendOpStrings(DO_FULLSCREEN, l1, l2, l3) ? Err::NoError : Err::SendFailed;
}

void RemoteDisplay::hideFullscreenText() { sendOp(DO_HIDE_FULL); }

Err RemoteDisplay::showPopupText(const char* text, uint8_t icon, uint8_t srcIcon, uint8_t fmt)
{
    uint8_t args[MAX_PAYLOAD];
    uint16_t n = 0;
    args[n++] = icon;
    args[n++] = srcIcon;
    args[n++] = fmt;
    for (const char* c = text ? text : ""; *c && n < sizeof(args) - 1; c++)
        args[n++] = (uint8_t)*c;
    return sendOp(DO_POPUP_TEXT, args, n) ? Err::NoError : Err::SendFailed;
}

void RemoteDisplay::hidePopup() { sendOp(DO_HIDE_POPUP); }

void RemoteDisplay::setAuxMode(bool on)
{
    uint8_t v = on ? 1 : 0;
    sendOp(DO_AUX, &v, 1);
}

// ---- media push -------------------------------------------------------------

void RemoteDisplay::sendMediaField(uint8_t field, const char* text)
{
    if (!_send) return;
    uint8_t p[MAX_PAYLOAD];
    uint16_t n = 0;
    p[n++] = field;
    for (const char* c = text ? text : ""; *c && n < sizeof(p); c++)
        p[n++] = (uint8_t)*c;
    _send(MEDIA_TEXT, p, n, _ctx);
}

void RemoteDisplay::pushMedia(const MediaInfo& info)
{
    bool force = !_everSent;
    if (force || info.title != _lastSent.title)
        sendMediaField(MF_TITLE, info.title.c_str());
    if (force || info.artist != _lastSent.artist)
        sendMediaField(MF_ARTIST, info.artist.c_str());
    if (force || info.album != _lastSent.album)
        sendMediaField(MF_ALBUM, info.album.c_str());
    if (force || info.playerName != _lastSent.playerName)
        sendMediaField(MF_SOURCE, info.playerName.c_str());
    if (force || info.playing() != _lastSent.playing())
        sendMediaField(MF_STATE, info.playing() ? "1" : "0");
    _lastSent = info;
    _everSent = true;
}

void RemoteDisplay::service(uint32_t nowMs)
{
    // Periodic full refresh: resyncs a peripheral that rebooted and keeps its
    // stale-timeout from firing during long tracks with no field changes.
    if (!_everSent) return;
    if (nowMs - _lastRefreshMs < REFRESH_MS) return;
    _lastRefreshMs = nowMs;
    sendMediaField(MF_TITLE, _lastSent.title.c_str());
    sendMediaField(MF_ARTIST, _lastSent.artist.c_str());
    sendMediaField(MF_ALBUM, _lastSent.album.c_str());
    sendMediaField(MF_SOURCE, _lastSent.playerName.c_str());
    sendMediaField(MF_STATE, _lastSent.playing() ? "1" : "0");
}
