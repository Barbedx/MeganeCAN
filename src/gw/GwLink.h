#pragma once
#include <stdint.h>
#include "../keys/KeyRouter.h"
#include "../vh/VehicleDecoder.h"
#include "../vh/CanboxEmitter.h"
#include "../vh/target/FsLogger.h"
#include "../vh/target/LinkTunnel.h"

// GW-side link endpoint (M1): owns UART2 to the peripheral (IOC/DISP board).
//   in:  KEY_EVT -> KeyRouter, LOG -> serial, CFG/CAP/FILE/OTA -> LinkTunnel
//        (the DISP-USB tunnel flashing GW), RAW_FRAME -> counters
//   out: MEDIA_TEXT / DISP_CMD (RemoteDisplay), TIME, SIG_BATCH
// Also implements LinkUi (see link/LinkUi.h): on GW the vehicle data, config
// and captures are LOCAL; LinkUi::ota* flashes the PERIPHERAL over the link.
// LinkPort is touched only from loop() (service()); the peer-OTA ops from the
// httpd task go through a small mailbox, same discipline as MmLinkService.
namespace GwLink
{
    void begin(KeyRouter* router,
               Vh::VehicleDecoder* decoder,
               Vh::CanboxEmitter* canbox,
               VhFs::FsLogger* fsLog,
               LinkTunnel* tunnel);
    void service(uint32_t nowMs);   // loop() only

    bool up();
    uint16_t peerFwVer();
    uint32_t peerCaps();

    // Safe from any task (LinkPort::send is lock-guarded), though the GW
    // design keeps producers on the loop task anyway.
    bool send(uint8_t type, const uint8_t* payload, uint16_t len);
    void sendTime(uint32_t unixSecs);

    // Record an unrecognized HU->box frame (loop task): live counter + tail
    // surfaced in LinkUi::statusJson — the P4 RE window on the /vh page.
    void noteHuUnknown(uint8_t cmd, const uint8_t* payload, uint8_t len);
}
