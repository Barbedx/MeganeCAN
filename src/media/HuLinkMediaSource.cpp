#include "HuLinkMediaSource.h"
#include "../link/LinkProto.h"
#include <string>

void HuLinkMediaSource::onMediaText(uint8_t field, const char* text, uint16_t len,
                                    uint32_t nowMs)
{
    std::string v(text, len);
    switch (field)
    {
    case LinkProto::MF_TITLE:  _info.title = v; break;
    case LinkProto::MF_ARTIST: _info.artist = v; break;
    case LinkProto::MF_ALBUM:  _info.album = v; break;
    case LinkProto::MF_SOURCE: _info.playerName = v; break;
    case LinkProto::MF_STATE:
        _info.playbackState = (!v.empty() && v[0] == '1')
                                  ? MediaInfo::PlaybackState::Playing
                                  : MediaInfo::PlaybackState::Paused;
        _info.playbackRate = _info.playing() ? 1.0f : 0.0f;
        break;
    default:
        return;
    }
    _lastTextMs = nowMs ? nowMs : 1;
    _info.lastUpdateMs = _lastTextMs;
    notifyChange();
}

bool HuLinkMediaSource::active() const
{
    return _linkUp && _lastTextMs != 0 && (_nowMs - _lastTextMs) < STALE_MS;
}
