#include "MediaRouter.h"
#include <string.h>

namespace { const MediaInfo kEmpty{}; }

MediaRouter::Mode MediaRouter::modeFromStr(const char* s)
{
    if (s && !strcmp(s, "hu"))   return Mode::Hu;
    if (s && !strcmp(s, "auto")) return Mode::Auto;
    return Mode::Ams;
}

const char* MediaRouter::modeStr(Mode m)
{
    switch (m) {
        case Mode::Hu:   return "hu";
        case Mode::Auto: return "auto";
        default:         return "ams";
    }
}

IMediaSource* MediaRouter::activeSource() const
{
    switch (_mode) {
        case Mode::Hu:   return _hu;
        case Mode::Auto: return (_hu && _hu->active()) ? _hu : _ams;
        default:         return _ams;
    }
}

const MediaInfo& MediaRouter::current() const
{
    IMediaSource* s = activeSource();
    return s ? s->current() : kEmpty;
}

bool MediaRouter::active() const
{
    IMediaSource* s = activeSource();
    return s && s->active();
}

const char* MediaRouter::statusText() const
{
    IMediaSource* s = activeSource();
    return s ? s->statusText() : "no media source";
}

const char* MediaRouter::sourceName() const
{
    IMediaSource* s = activeSource();
    return s ? s->name() : "none";
}
