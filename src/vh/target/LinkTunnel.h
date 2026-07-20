#pragma once
#include <stdint.h>
#include <stddef.h>
#include "../../link/LinkPort.h"
#include "FsLogger.h"

// What the CFG_GET/SET tunnel exposes is board-specific (CR-02: the peer
// decides nothing) — GW binds its vehicle/canbox knobs (VhConfig), DISP binds
// its display keys (display_type, skip_funcreg, ...). String-keyed either way.
struct ITunnelConfig {
    virtual ~ITunnelConfig() = default;
    virtual bool get(const char* key, char* out, size_t outLen) = 0;
    virtual bool set(const char* key, const char* value) = 0;
};

// The VH maintenance plane (ARCHITECTURE-V2 §5): config, capture control, log
// pull and firmware-OTA — all over LinkProto, because VH has no WiFi/BT and
// must never need the bench flasher again once installed. In the very first
// image that goes into the car.
//
// Protocol shapes (stateless where possible so a rebooted MM just carries on):
//   CFG_GET {key\0}            -> CFG_ACK {ok, key\0value}
//   CFG_SET {key\0value}       -> CFG_ACK {ok, key\0value}
//   CAP_CTL {op, mode, secs LE}-> CFG_ACK {ok, "cap\0<active>"}
//   FILE_LS {}                 -> FILE_LS  {N x {size:4 LE, name\0}}
//   FILE_REQ {off:4 LE, name\0}-> FILE_DATA {off:4, total:4, data...} (stop-and-wait,
//                                 requester paces by re-REQ; empty data = EOF/miss)
//   OTA_BEGIN {size:4 LE}      -> OTA_STAT {0, 0} | {err, detail}
//   OTA_DATA {off:4 LE, data}  -> OTA_STAT {0, nextOff} per chunk (stop-and-wait);
//                                 offset mismatch -> OTA_STAT {2, expectedOff}
//   OTA_END {}                 -> OTA_STAT {0, size} then reboot into the new image
class LinkTunnel {
public:
    void begin(LinkPort& link, VhFs::FsLogger& fs, ITunnelConfig& cfg);
    // Feed one app-level link message; returns true if consumed.
    bool handle(uint8_t type, const uint8_t* p, uint16_t len);
    void service(uint32_t nowMs);      // deferred reboot after OTA_END

    bool otaActive() const { return _otaActive; }

private:
    void cfgAck(bool ok, const char* key, const char* value);
    void otaStat(uint8_t code, uint32_t detail);

    LinkPort* _link = nullptr;
    VhFs::FsLogger* _fs = nullptr;
    ITunnelConfig* _cfg = nullptr;

    bool _otaActive = false;
    uint32_t _otaSize = 0;
    uint32_t _otaOff = 0;
    uint32_t _rebootAtMs = 0;
};
