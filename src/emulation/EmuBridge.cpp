#include "EmuBridge.h"

void EmuBridge::begin(VirtualDisplayBase& vd, RecvFn recvFn, void* ctx, IClock& clk)
{
    _vd = &vd;
    _recv = recvFn;
    _ctx = ctx;
    _clk = &clk;
    _back.owner = this;
    _tap.owner = this;

    // Bind the twin to the back-bus from startup so it decodes every frame the radio
    // emits (always-live screen), but passivate it so it never ACKs/syncs while the
    // emulation loop is off. enable() flips _emulate to activate ACKing.
    if (_vd && _clk) {
        _vd->begin(_back, *_clk);
        _vd->setEmulate(false);
    }
}

void EmuBridge::enable(bool on)
{
    if (_vd) {
        _vd->setEmulate(on);                              // on: twin ACKs/syncs the radio
        if (on) _vd->setAckMode(VirtualDisplayBase::ACK_PARTIAL); // make it emit everything
    }
    _on = on;
}
