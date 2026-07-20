#pragma once
#include "../vh/target/LinkTunnel.h"   // ITunnelConfig

// The DISP board's tunnel-config binding (CR-02: GW decides, we store):
//   display_type  "carminat" | "updatelist" | "updatelist_menu"
//   skip_funcreg  0/1 (a real radio owns registration)
//   autorestore   0/1 (restore last text on boot)
// display_type / skip_funcreg take effect at driver construction, so a set
// schedules a reboot (the CFG_ACK drains first) — same semantics the legacy
// C3 web config always had.
class DispCfg : public ITunnelConfig {
public:
    bool get(const char* key, char* out, size_t outLen) override;
    bool set(const char* key, const char* value) override;

    // Polled by disp_main: nonzero = millis() deadline to ESP.restart().
    uint32_t rebootAtMs() const { return _rebootAtMs; }

private:
    uint32_t _rebootAtMs = 0;
};
