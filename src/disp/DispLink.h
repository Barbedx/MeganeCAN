#pragma once
#include <stdint.h>
#include "../display/AffaDisplayBase.h"
#include "../media/MediaRouter.h"
#include "../media/HuLinkMediaSource.h"
#include "../vh/target/LinkTunnel.h"
#include "DispCfg.h"

// DISP-side link endpoint (M2): the thin peripheral's only brain-facing port.
//   in:  MEDIA_TEXT -> MediaInfo -> display, DISP_CMD -> local driver calls,
//        TIME -> system clock, CFG/OTA -> LinkTunnel (GW flashes/configures us)
//   out: KEY_EVT (SWC keys), forwarded ProgProto replies for the USB tool
// The USB<->link programming bridge (§3.4): the PC tool sends "@PROG <hex>"
// lines (a raw [type][payload] link frame, typically OTA_* toward GW); every
// ProgProto reply that is NOT for our own tunnel echoes back as "@PROG <hex>".
namespace DispLink
{
    void begin(AffaDisplayBase* display, MediaRouter* router, LinkTunnel* tunnel);
    void service(uint32_t nowMs);
    bool up();

    HuLinkMediaSource& mediaSource();
    DispCfg& cfg();   // reboot-pending polled by disp_main

    void sendKey(uint16_t affaKey, uint8_t edge);
    // From the USB console: inject one raw frame into the link (the @PROG bridge).
    bool sendRaw(uint8_t type, const uint8_t* payload, uint16_t len);
}
