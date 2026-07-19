#include "AmsKeySink.h"
#include "../apple_media_service.h"
#include "../bluetooth.h"

void AmsKeySink::onKey(AffaCommon::AffaKey key, bool isHold)
{
    (void)isHold;
    // Peripheral model: nothing to steer while disconnected — the phone
    // pairs/reconnects from iOS Settings. Each event no-ops independently so a
    // disconnected iPhone never breaks the other sinks.
    if (!Bluetooth::IsConnected())
        return;

    switch (key)
    {
    case AffaCommon::AffaKey::Pause:    AppleMediaService::Toggle();    break;
    case AffaCommon::AffaKey::RollUp:   AppleMediaService::NextTrack(); break;
    case AffaCommon::AffaKey::RollDown: AppleMediaService::PrevTrack(); break;
    // AMS volume steps are tiny, so one (hold-gated) key = a 15-step burst.
    case AffaCommon::AffaKey::VolumeUp:
        for (int i = 0; i < 15; i++) AppleMediaService::VolumeUp();
        break;
    case AffaCommon::AffaKey::VolumeDown:
        for (int i = 0; i < 15; i++) AppleMediaService::VolumeDown();
        break;
    default: break;
    }
}
