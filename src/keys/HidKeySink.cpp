#include "HidKeySink.h"
#include "../ble/HidRole.h"

void HidKeySink::onKey(AffaCommon::AffaKey key, bool isHold)
{
    (void)isHold;
    // Hid::press no-ops when no HID peer is subscribed, so no connection gate here.
    switch (key)
    {
    case AffaCommon::AffaKey::Pause:      Hid::press(KEY_MEDIA_PLAY_PAUSE);     break;
    case AffaCommon::AffaKey::RollUp:     Hid::press(KEY_MEDIA_NEXT_TRACK);     break;
    case AffaCommon::AffaKey::RollDown:   Hid::press(KEY_MEDIA_PREVIOUS_TRACK); break;
    case AffaCommon::AffaKey::VolumeUp:   Hid::press(KEY_MEDIA_VOLUME_UP);      break;
    case AffaCommon::AffaKey::VolumeDown: Hid::press(KEY_MEDIA_VOLUME_DOWN);    break;
    default: break;
    }
}
