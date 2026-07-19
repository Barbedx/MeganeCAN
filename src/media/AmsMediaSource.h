#pragma once
#include "IMediaSource.h"

// The iPhone (Apple Media Service) as a media source. Owns the AMS notification
// callback and the AMS->MediaInfo conversion, so AMS types stay inside this file
// and everything downstream (displays, web UI, router) sees only MediaInfo.
// Target-only (talks to bluetooth/AMS) — excluded from the native env.
namespace AppleMediaService { struct MediaInformation; }

class AmsMediaSource : public IMediaSource {
public:
    // Registers the AMS notification callback. Call only when the AMS role is
    // active (BleHub mode ams/both), same gating setup() always had.
    void begin();

    const MediaInfo& current() const override { return _info; }
    bool active() const override;         // Bluetooth::IsConnected()
    const char* statusText() const override;
    const char* name() const override { return "ams"; }

private:
    void onAmsUpdate(const AppleMediaService::MediaInformation& mi);
    MediaInfo _info;
};
