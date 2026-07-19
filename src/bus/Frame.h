#pragma once
#include <stdint.h>

// Portable CAN frame used by the logic layer (radio-side AFFA3 code, virtual
// displays, tests). It deliberately carries NO dependency on the driver's
// CAN_FRAME / can_common so the logic layer can build on the native host
// (`pio test -e native`). HwCanBus is the ONLY place that converts between this
// and the controller's CAN_FRAME. ~16 bytes.
struct Frame {
    // Which physical/virtual bus the frame came from. Tagged at the conversion
    // points (HwCanBus::ingest for the real controller, the EmuBridge back-path
    // for the virtual display); the VH board's frames arrive over LinkProto (P1+).
    enum Source : uint8_t {
        SRC_MM_CAN  = 0,   // multimedia CAN (this board's TWAI controller)
        SRC_VH_CAN  = 1,   // vehicle main CAN, relayed by the VH board
        SRC_VIRTUAL = 2,   // in-firmware virtual display (EmuBridge)
        SRC_INJECT  = 3,   // test/RE injection (@INJ / web)
    };

    uint32_t id = 0;
    uint8_t  len = 0;
    uint8_t  data[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    bool     extended = false;
    uint8_t  source = SRC_MM_CAN;
};
