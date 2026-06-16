#include "DisplayTransport.h"

void DisplayTransport::setRoute(Route r)
{
    _route = r;
    // tx     : real CAN transmit on?      (off only for VIRTUAL_ONLY)
    // feed   : feed radio TX -> twin?      (off only for CAN_ONLY — no virtual view)
    // active : twin ACKs the radio?        (on only for VIRTUAL_ONLY — it IS the panel)
    const bool tx     = (r != VIRTUAL_ONLY);
    const bool feed   = (r != CAN_ONLY);
    const bool active = (r == VIRTUAL_ONLY);

    _can.setTxEnabled(tx);
    _emu.setFeed(feed);
    _emu.enable(active);
}

const char* DisplayTransport::name(Route r)
{
    switch (r) {
        case CAN_ONLY:        return "can";
        case VIRTUAL_ONLY:    return "virtual";
        case CAN_AND_VIRTUAL: return "both";
    }
    return "?";
}
