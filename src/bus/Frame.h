#pragma once
#include <stdint.h>

// Portable CAN frame used by the logic layer (radio-side AFFA3 code, virtual
// displays, tests). It deliberately carries NO dependency on the driver's
// CAN_FRAME / can_common so the logic layer can build on the native host
// (`pio test -e native`). HwCanBus is the ONLY place that converts between this
// and the controller's CAN_FRAME. ~16 bytes.
struct Frame {
    uint32_t id = 0;
    uint8_t  len = 0;
    uint8_t  data[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    bool     extended = false;
};
