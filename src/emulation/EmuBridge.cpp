#include "EmuBridge.h"

void EmuBridge::begin(VirtualDisplayBase& vd, RecvFn recvFn, void* ctx, IClock& clk)
{
    _vd = &vd;
    _recv = recvFn;
    _ctx = ctx;
    _clk = &clk;
    _back.owner = this;
    _tap.owner = this;
}

void EmuBridge::enable(bool on)
{
    if (on && !_on && _vd && _clk) {
        _vd->begin(_back, *_clk);                       // twin replies on the back-bus
        _vd->setAckMode(VirtualDisplayBase::ACK_PARTIAL); // make the radio emit everything
    }
    _on = on;
}
