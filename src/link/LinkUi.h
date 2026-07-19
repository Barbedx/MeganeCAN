#pragma once
#include <stdint.h>
#include <stddef.h>
#include <Arduino.h>

// The link surface the web layer (HttpServerManager) talks to — role-neutral.
// Exactly ONE implementation links into each image (CR-02/03: no ifdefs, the
// board role is a link-time choice):
//   - C3/MM builds: link/mm/MmLinkService.cpp — proxies everything to the peer
//     over the link (the vehicle board is remote).
//   - GW builds: gw/GwLink.cpp — vehicle data/config/captures are LOCAL;
//     ota* flashes the attached peripheral over the link.
namespace LinkUi
{
    bool enabled();
    bool up();
    String statusJson();

    bool cfgGet(const char* key, char* val, size_t valLen);
    bool cfgSet(const char* key, const char* val);
    bool capCtl(uint8_t op, uint8_t mode, uint16_t secs);
    bool fileLs(String& json);
    int  fileRead(const char* name, uint32_t offset, uint8_t* buf, size_t maxLen,
                  uint32_t& totalSize);

    // Firmware update of the OTHER board via ProgProto over the link.
    bool otaBegin(uint32_t size);
    bool otaWrite(const uint8_t* data, size_t len);
    bool otaEnd();
    const char* otaError();
}
