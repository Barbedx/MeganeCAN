#include "LinkPort.h"

using namespace LinkProto;

void LinkPort::begin(ILinkStream& stream, IClock& clock, uint16_t fwVer, uint32_t caps)
{
    _s = &stream;
    _clk = &clock;
    _fwVer = fwVer;
    _caps = caps;
    _dec.onFrame(onFrameTrampoline, this);
    sendHello();
}

void LinkPort::sendHello()
{
    uint8_t p[7];
    p[0] = VERSION;
    p[1] = (uint8_t)(_fwVer & 0xFF);
    p[2] = (uint8_t)(_fwVer >> 8);
    p[3] = (uint8_t)(_caps & 0xFF);
    p[4] = (uint8_t)((_caps >> 8) & 0xFF);
    p[5] = (uint8_t)((_caps >> 16) & 0xFF);
    p[6] = (uint8_t)((_caps >> 24) & 0xFF);
    send(HELLO, p, sizeof(p), PRIO_HIGH);
    _lastHelloMs = _clk ? _clk->millis() : 0;
}

bool LinkPort::send(uint8_t type, const uint8_t* payload, uint16_t len)
{
    return send(type, payload, len, defaultPrio(type));
}

bool LinkPort::send(uint8_t type, const uint8_t* payload, uint16_t len, Prio prio)
{
    if (!_s) return false;

    // Pick a free slot; if none, evict the lowest-priority queued frame (only if
    // it is not higher-priority than the newcomer, and never the one mid-write).
    int free = -1;
    for (int i = 0; i < QUEUE_SLOTS; i++)
        if (_q[i].len == 0) { free = i; break; }
    if (free < 0)
    {
        int victim = -1;
        for (int i = 0; i < QUEUE_SLOTS; i++)
        {
            if (i == _curSlot) continue;
            if (victim < 0 || _q[i].prio < _q[victim].prio) victim = i;
        }
        if (victim < 0 || _q[victim].prio > prio) { _txDropped++; return false; }
        _txDropped++;                    // the victim is the drop
        free = victim;
    }

    uint16_t n = LinkCodec::encode(type, _txSeq, payload, len, _q[free].bytes);
    if (n == 0) { _q[free].len = 0; return false; }   // payload too large
    _txSeq++;
    _q[free].len = n;
    _q[free].prio = (uint8_t)prio;
    return true;
}

void LinkPort::drainTx()
{
    for (;;)
    {
        if (_curSlot < 0)
        {
            // Highest priority first; round-robin from _qHead within a class so
            // same-priority frames keep FIFO-ish order.
            int best = -1;
            for (int k = 0; k < QUEUE_SLOTS; k++)
            {
                int i = (_qHead + k) % QUEUE_SLOTS;
                if (_q[i].len == 0) continue;
                if (best < 0 || _q[i].prio > _q[best].prio) best = i;
            }
            if (best < 0) return;
            _curSlot = best;
            _curOff = 0;
        }

        Slot& s = _q[_curSlot];
        int wrote = _s->write(s.bytes + _curOff, s.len - _curOff);
        if (wrote > 0) _curOff = (uint16_t)(_curOff + wrote);
        if (_curOff < s.len) return;     // stream full — resume next service()

        s.len = 0;                       // frame fully on the wire
        _qHead = (_curSlot + 1) % QUEUE_SLOTS;
        _curSlot = -1;
    }
}

void LinkPort::service()
{
    if (!_s || !_clk) return;
    uint32_t now = _clk->millis();

    // RX pump
    uint8_t buf[64];
    int n;
    while ((n = _s->read(buf, sizeof(buf))) > 0)
        _dec.feed(buf, (uint16_t)n);

    // Peer timeout
    if (_up && (now - _lastRxMs) > PEER_TIMEOUT_MS)
        _up = false;

    // 1Hz heartbeat
    if (now - _lastPingMs >= PING_INTERVAL_MS)
    {
        _lastPingMs = now;
        send(PING, nullptr, 0, PRIO_HIGH);
    }

    drainTx();
}

void LinkPort::onFrameTrampoline(uint8_t type, uint8_t seq,
                                 const uint8_t* payload, uint16_t len, void* ctx)
{
    static_cast<LinkPort*>(ctx)->onFrame(type, seq, payload, len);
}

void LinkPort::onFrame(uint8_t type, uint8_t seq, const uint8_t* payload, uint16_t len)
{
    uint32_t now = _clk ? _clk->millis() : 0;

    if (_haveRxSeq && (uint8_t)(_lastRxSeq + 1) != seq)
        _rxSeqGaps++;
    _haveRxSeq = true;
    _lastRxSeq = seq;

    bool wasUp = _up;
    _lastRxMs = now;
    _up = true;
    // Reconnect (peer was silent > timeout): re-introduce ourselves, throttled
    // so two fresh ports don't ping-pong HELLOs.
    if (!wasUp && (now - _lastHelloMs) > 1000)
        sendHello();

    switch (type)
    {
    case PING:
        send(PONG, nullptr, 0, PRIO_HIGH);
        return;
    case PONG:
        return;
    case HELLO:
        if (len >= 7)
        {
            _peerFwVer = (uint16_t)payload[1] | ((uint16_t)payload[2] << 8);
            _peerCaps  = (uint32_t)payload[3] | ((uint32_t)payload[4] << 8)
                       | ((uint32_t)payload[5] << 16) | ((uint32_t)payload[6] << 24);
        }
        break;   // fall through to the app callback too
    default:
        break;
    }

    if (_cb)
        _cb(type, payload, len, _ctx);
}
