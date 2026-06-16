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

    void enable(bool on);                 // on: twin ACKs/syncs the radio (ACTIVE); off: passive
    bool enabled() const { return _on; }

    // Whether radio TX frames are fed to the twin at all. Default true (always-live
    // decode, Phase B). DisplayTransport drops it in CAN_ONLY so the twin idles when
    // no virtual view is wanted. Independent of enable() (feed = observe; enable = ACK).
    void setFeed(bool on) { _feed = on; }
    bool feeding() const { return _feed; }

    IBusTap& tap() { return _tap; }       // register on the radio's bus (HwCanBus)
    const ScreenModel& screen() const { return _vd->screen(); }

    // Clock ms of the last decode (0 = none). The twin decodes the radio's frames
    // continuously (always-live), so the screen reflects the panel whether or not
    // ACK-emulation is enabled; callers compute staleness from this.
    uint32_t lastDecodeMs() const { return _vd ? _vd->lastDecodeMs() : 0; }

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
    // Every radio TX frame -> the twin, always. Decode is read-only state; the
    // ACK/sync reply is gated independently by the twin's _emulate flag (set by
    // enable()), so feeding unconditionally keeps the decoded screen always live
    // without making the twin answer the radio while passive.
    struct FeedTap : IBusTap {
        EmuBridge* owner = nullptr;
        void onTx(const Frame& f) override {
            if (owner && owner->_feed && owner->_vd) owner->_vd->onCanRx(f);
        }
    };

    VirtualDisplayBase* _vd = nullptr;
    RecvFn  _recv = nullptr;
    void*   _ctx = nullptr;
    IClock* _clk = nullptr;
    BackBus _back;
    FeedTap _tap;
    bool    _on = false;     // ACTIVE: twin ACKs the radio
    bool    _feed = true;    // feed radio TX -> twin (always-live decode by default)
};
