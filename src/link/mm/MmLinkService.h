#pragma once
#include <stdint.h>
#include <stddef.h>
#include <Arduino.h>
#include "../../vh/VehicleSignals.h"

class HuLinkMediaSource;

// MM-side LinkProto endpoint (ARCHITECTURE-V2 §6.3/§6.4): owns UART1 to the VH
// board, caches VH telemetry for the dashboard, and proxies the maintenance
// plane (config / capture / log pull / OTA-over-link) for the web UI.
//
// Threading contract: the LinkPort is touched ONLY from loop() (service()).
// HTTP handlers run in the httpd task, so every blocking op goes through a
// single-slot mailbox: the handler posts a request and polls; service()
// executes it and posts the reply. One op at a time — plenty for a config UI.
namespace MmLink
{
    void begin();       // opens UART1 (TX=21, RX=20) — call once, after AppConfig::Load
    void service();     // pump from loop() only
    bool enabled();
    bool up();

    HuLinkMediaSource& mediaSource();

    // Fire-and-forget (safe from any task: queued under a spinlock).
    void sendKey(uint16_t affaKey, uint8_t edge);
    void sendTime(uint32_t unixSecs);

    // ---- blocking ops for HTTP handlers (httpd task) ----
    // Each returns false on link-down/timeout/busy.
    bool cfgGet(const char* key, char* val, size_t valLen);
    bool cfgSet(const char* key, const char* val);
    bool capCtl(uint8_t op, uint8_t mode, uint16_t secs);
    // JSON array of VH files: [{"name":"/cap0.canlog","size":1234},...]
    bool fileLs(String& json);
    // One chunk of a VH file; returns bytes read (<0 = failure), totalSize set.
    int fileRead(const char* name, uint32_t offset, uint8_t* buf, size_t maxLen,
                 uint32_t& totalSize);
    // VH firmware OTA-over-link (exit criterion of P1): begin/write.../end.
    bool otaBegin(uint32_t size);
    bool otaWrite(const uint8_t* data, size_t len);
    bool otaEnd();
    const char* otaError();

    // Dashboard/status JSON: link state, peer info, VehicleState, HU status.
    String statusJson();

    const Vh::VehicleState& vehicleState();
}
