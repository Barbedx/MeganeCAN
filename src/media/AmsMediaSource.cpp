#include "AmsMediaSource.h"
#include "../apple_media_service.h"
#include "../bluetooth.h"
#include <Arduino.h>

namespace { AmsMediaSource* s_self = nullptr; }

void AmsMediaSource::begin()
{
    s_self = this;
    AppleMediaService::RegisterForNotifications(
        [](const AppleMediaService::MediaInformation& mi) {
            if (s_self) s_self->onAmsUpdate(mi);
        },
        AppleMediaService::NotificationLevel::All);
}

bool AmsMediaSource::active() const
{
    return Bluetooth::IsConnected();
}

const char* AmsMediaSource::statusText() const
{
    return Bluetooth::GetStatusText();
}

void AmsMediaSource::onAmsUpdate(const AppleMediaService::MediaInformation& mi)
{
    // dump() is 8 serial lines; AMS fires often during playback, so throttle the
    // log to keep the serial channel readable. The conversion runs every time.
    static uint32_t s_lastDump = 0;
    if (millis() - s_lastDump > 5000)
    {
        s_lastDump = millis();
        mi.dump();
    }

    _info.playerName = mi.mPlayerName;
    _info.title      = mi.mTitle;
    _info.artist     = mi.mArtist;
    _info.album      = mi.mAlbum;

    _info.playbackState = static_cast<MediaInfo::PlaybackState>(mi.mPlaybackState);
    _info.playbackRate  = mi.mPlaybackRate;
    _info.elapsedTime   = mi.mElapsedTime;
    _info.duration      = mi.mDuration;
    _info.volume        = mi.mVolume;

    _info.queueIndex  = mi.mQueueIndex;
    _info.queueCount  = mi.mQueueCount;
    _info.shuffleMode = static_cast<MediaInfo::ShuffleMode>(mi.mShuffleMode);
    _info.repeatMode  = static_cast<MediaInfo::RepeatMode>(mi.mRepeatMode);

    _info.lastUpdateMs = mi.mLastPlaybackInfoMs;

    notifyChange();
}
