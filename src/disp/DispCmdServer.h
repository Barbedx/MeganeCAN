#pragma once
#include <stdint.h>
#include "../display/IDisplay.h"

// The receiving half of the DISP_CMD protocol: executes a serialized IDisplay
// call (built by gw/RemoteDisplay) on a local driver. Portable — the native
// suite round-trips RemoteDisplay -> wire -> execute on a fake display, so the
// two halves can never drift apart silently.
namespace DispCmd
{
    // payload = the DISP_CMD frame body: {op:1, args...}. Malformed input is
    // ignored (never reads past len); unknown ops are skipped (forward-compat).
    void execute(IDisplay& d, const uint8_t* payload, uint16_t len);
}
