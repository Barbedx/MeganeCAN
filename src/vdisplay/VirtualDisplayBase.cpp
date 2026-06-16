#include "VirtualDisplayBase.h"
#include "../display/AffaCommonConstants.h"

void VirtualDisplayBase::autoAck(uint16_t id)
{
    if (!_emulate || !_bus) return;
    Frame ack;
    ack.id = (uint16_t)(id | _p.replyFlag);
    ack.len = 8;
    int i = 1;
    if (_ackMode == ACK_PARTIAL) {
        ack.data[0] = 0x30; ack.data[1] = 0x01; ack.data[2] = 0x00;   // PARTIAL: keep sending
        i = 3;
    } else {
        ack.data[0] = 0x74;                          // DONE (matches recv() reply)
    }
    for (; i < 8; i++) ack.data[i] = _p.filler;
    _bus->send(ack);
}

void VirtualDisplayBase::onCanRx(const Frame& f)
{
    // ACK echoes on the reply channel are not for the display.
    if (f.id & _p.replyFlag) return;

    // Sync channel: the radio's registration (0x70) confirms the link is up.
    if (f.id == _p.syncId) {
        if (f.len >= 1 && f.data[0] == 0x70)
            _synced = true;
        return;
    }

    // Text / control frames: decode the screen, then auto-ACK like a real panel.
    if (f.id == _p.ctrlId || f.id == _p.textId) {
        decode(f);
        if (_clock) _lastDecodeMs = _clock->millis();
        autoAck(f.id);
        return;
    }

    // Keys are display->radio; we never consume our own. Anything else: ignore.
}

void VirtualDisplayBase::pressKey(uint16_t code, bool hold)
{
    if (!_bus) return;
    uint8_t hi = (uint8_t)(code >> 8);
    uint8_t lo = (uint8_t)(code & 0xFF);

    // Encoder rotations (RollUp 0x0101 / RollDown 0x0141) are never hold-masked —
    // matches the extract logic in both recv() handlers.
    bool isEncoder = (code == 0x0101 || code == 0x0141);
    if (hold && !isEncoder)
        lo |= AffaCommon::KEY_HOLD_MASK;

    Frame k;
    k.id = _p.keyId;
    k.len = 8;
    k.data[0] = 0x03;
    k.data[1] = 0x89;
    k.data[2] = hi;
    k.data[3] = lo;
    for (int i = 4; i < 8; i++) k.data[i] = _p.filler;
    _bus->send(k);
}
