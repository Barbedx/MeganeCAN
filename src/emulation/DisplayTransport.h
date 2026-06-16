#pragma once
#include "../bus/ITxGate.h"
#include "EmuBridge.h"

// The ONE knob for "where do display frames go and who ACKs them" — replaces the old
// scatter of skip_funcreg / self-ACK / full-emu / busAlive reasoning with a single
// Route. It only *selects sinks*; it never rewrites a frame, so the AFFA3 bytes on the
// wire are untouched.
//
//   CAN_ONLY         CAN transmits; twin idle.            real panel owns the exchange (car)
//   VIRTUAL_ONLY     no CAN; twin ACKs (active).          bench/dev — virtual IS the panel
//   CAN_AND_VIRTUAL  CAN transmits; twin decodes passive. real panel ACKs; virtual mirrors it
//
// The wire mirror (@TX serial/WS) is always on in every route — observation, orthogonal
// to routing. Depends only on the ITxGate seam + EmuBridge, so it host-tests.
class DisplayTransport {
public:
    enum Route { CAN_ONLY, VIRTUAL_ONLY, CAN_AND_VIRTUAL };

    DisplayTransport(ITxGate& can, EmuBridge& emu) : _can(can), _emu(emu) {}

    void  setRoute(Route r);
    Route route() const { return _route; }
    static const char* name(Route r);

private:
    ITxGate&   _can;
    EmuBridge& _emu;
    Route      _route = CAN_AND_VIRTUAL;
};
