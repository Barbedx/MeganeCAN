// ANCS stub for the DISP (IO Controller) image: the C3 no longer runs BLE —
// notifications will arrive from GW over the link in a later phase. This
// satisfies the display drivers' references without pulling NimBLE in.
#include "../apple_notification_service.h"

namespace AppleNotificationService
{
    std::vector<NotificationInfo> GetRecent() { return {}; }
    const char *CategoryName(uint8_t) { return ""; }
    std::string AppName(const std::string &) { return std::string(); }
    void Process() {}
    void Detach() {}
}
