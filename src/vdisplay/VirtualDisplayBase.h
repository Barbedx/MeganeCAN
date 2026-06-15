#pragma once
#include "IVirtualDisplay.h"
#include "../affa/IsoTp.h"

// Shared display-side mechanics, protocol-parameterised. Extracted from the auto-ACK
// / sync-reply / key-extract logic that already lived in UpdateListBase::recv() and
// CarminatDisplay::recv(). Subclasses supply the protocol IDs + a decode() for their
// screen payload; everything else (ISO-TP reassembly, auto-ACK, key TX) is shared.
struct VdProtocol {
    uint16_t ctrlId;       // primary text/ctrl frames from the radio (decode + ACK)
    uint16_t textId;       // secondary text id (UL 0x121); == ctrlId if unused
    uint16_t keyId;        // key TX id (display -> radio)
    uint16_t syncId;       // radio sync id (e.g. 0x3AF Carminat / 0x3DF UL)
    uint16_t syncReplyId;  // sync channel the display answers on (0x3CF)
    uint16_t replyFlag;    // ACK channel flag (0x400)
    uint8_t  filler;       // pad byte (0x00 Carminat / 0x81 UL)
};

class VirtualDisplayBase : public IVirtualDisplay {
public:
    explicit VirtualDisplayBase(const VdProtocol& p) : _p(p) {}

    void begin(ICanBus& bus, IClock& clock) override { _bus = &bus; _clock = &clock; }
    void onCanRx(const Frame& f) override;
    void pressKey(uint16_t code, bool hold) override;
    const ScreenModel& screen() const override { return _screen; }

    // EMULATION vs PASSIVE: when off, the display only decodes (no ACK / no sync
    // reply) — mirrors a real panel running in parallel for observation. Default on.
    void setEmulate(bool on) { _emulate = on; }
    bool emulating() const { return _emulate; }

    // Per-frame ACK content. ACK_DONE (0x74) finishes a transfer; ACK_PARTIAL
    // (30 01 00) tells the radio to keep sending. For driving the REAL radio's
    // affa3_do_send over the bus, ACK_PARTIAL on every frame makes it emit the whole
    // multi-frame buffer (the radio breaks only on DONE); the trailing SendFailed is
    // ignored by showMenu callers. ACK_DONE is the default (single-frame transfers).
    enum AckMode { ACK_DONE, ACK_PARTIAL };
    void setAckMode(AckMode m) { _ackMode = m; }

    // Test/inspection hooks.
    bool synced() const { return _synced; }

protected:
    // Decode a control/text frame for this protocol into _screen. Called for every
    // frame on ctrlId/textId. Implementations use _asm (ISO-TP) + ScreenDecode.
    virtual void decode(const Frame& f) = 0;

    // Auto-ACK a received frame: 0x74 + filler on (id | replyFlag). Matches the
    // 0x74 reply both recv() handlers emit. No-op in PASSIVE mode.
    void autoAck(uint16_t id);

    VdProtocol           _p;
    ICanBus*             _bus = nullptr;
    IClock*              _clock = nullptr;
    ScreenModel          _screen;
    IsoTp::Reassembler   _asm;
    bool                 _emulate = true;
    bool                 _synced = false;
    AckMode              _ackMode = ACK_DONE;
};
