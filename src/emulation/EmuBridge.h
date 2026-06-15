#pragma once
#include "../bus/ICanBus.h"
#include "../bus/BusTap.h"
#include "../bus/IClock.h"
#include "../vdisplay/VirtualDisplayBase.h"

// Wires the FULL-EMULATION closed loop, decoupled from main/firmware:
//   radio --(TX tap)--> virtual display --(ACK/key)--> radio recv()
// Every outbound radio frame is fed to a virtual display twin; the twin's ACK/key
// frames are delivered back into the radio's recv() via recvFn. The radio's per-frame
// ACK wait is satisfied inline, so the whole multi-frame sequence emits with no real
// panel and no self-ACK. Depends only on the portable seams (Frame/ICanBus/IClock/
// VirtualDisplayBase) -> compiles + unit-tests on the native host.
class EmuBridge {
public:
    using RecvFn = void (*)(const Frame&, void* ctx);

    // vd: the virtual-display twin (its protocol must match the emulated radio).
    // recvFn: delivers a frame into the radio's recv() path.
    void begin(VirtualDisplayBase& vd, RecvFn recvFn, void* ctx, IClock& clk);

    void enable(bool on);                 // on: bind the twin (PARTIAL ACK) + start feeding
    bool enabled() const { return _on; }

    IBusTap& tap() { return _tap; }       // register on the radio's bus (HwCanBus)
    const ScreenModel& screen() const { return _vd->screen(); }

private:
    // The twin's outbound frames (ACKs/keys) -> the radio's recv().
    struct BackBus : ICanBus {
        EmuBridge* owner = nullptr;
        bool send(const Frame& f) override {
            if (owner && owner->_recv) owner->_recv(f, owner->_ctx);
            return true;
        }
        void onReceive(RxHandler, void*) override {}
        bool isLive() const override { return true; }
    };
    // Every radio TX frame -> the twin (only while enabled).
    struct FeedTap : IBusTap {
        EmuBridge* owner = nullptr;
        void onTx(const Frame& f) override {
            if (owner && owner->_on && owner->_vd) owner->_vd->onCanRx(f);
        }
    };

    VirtualDisplayBase* _vd = nullptr;
    RecvFn  _recv = nullptr;
    void*   _ctx = nullptr;
    IClock* _clk = nullptr;
    BackBus _back;
    FeedTap _tap;
    bool    _on = false;
};
