#include "LinkCodec.h"
#include "Cobs.h"
#include "Crc16.h"

namespace LinkCodec
{
    uint16_t encode(uint8_t type, uint8_t seq,
                    const uint8_t* payload, uint16_t payloadLen, uint8_t* out)
    {
        using namespace LinkProto;
        if (payloadLen > MAX_PAYLOAD)
            return 0;

        uint8_t raw[MAX_FRAME];
        uint16_t n = 0;
        raw[n++] = VERSION;
        raw[n++] = type;
        raw[n++] = seq;
        for (uint16_t i = 0; i < payloadLen; i++)
            raw[n++] = payload[i];
        uint16_t crc = Crc16::ccitt(raw, n);
        raw[n++] = (uint8_t)(crc >> 8);
        raw[n++] = (uint8_t)(crc & 0xFF);

        // Leading AND trailing delimiter: the leading zero terminates any garbage
        // the receiver was accumulating (line noise, a reboot mid-frame), so every
        // frame is self-synchronizing at the cost of one byte. Empty blocks are
        // ignored by the decoder.
        uint16_t enc = 0;
        out[enc++] = 0x00;
        enc = (uint16_t)(enc + Cobs::encode(raw, n, out + enc));
        out[enc++] = 0x00;
        return enc;
    }

    void Decoder::feed(const uint8_t* data, uint16_t len)
    {
        for (uint16_t i = 0; i < len; i++)
            feed(data[i]);
    }

    void Decoder::feed(uint8_t b)
    {
        using namespace LinkProto;
        if (b != 0x00)
        {
            if (_skip) return;                 // discarding until the delimiter
            if (_len >= sizeof(_buf)) { _skip = true; _overrun++; return; }
            _buf[_len++] = b;
            return;
        }

        // Delimiter: close out the accumulated block (empty = idle delimiter).
        uint16_t len = _len;
        bool skipped = _skip;
        _len = 0;
        _skip = false;
        if (skipped || len == 0)
            return;

        uint8_t raw[MAX_WIRE];
        int n = Cobs::decode(_buf, len, raw);
        if (n < (int)(HEADER_LEN + CRC_LEN)) { _badCobs++; return; }

        uint16_t body = (uint16_t)n - CRC_LEN;
        uint16_t crc = ((uint16_t)raw[body] << 8) | raw[body + 1];
        if (Crc16::ccitt(raw, body) != crc) { _badCrc++; return; }
        if (raw[0] != VERSION) { _badVer++; return; }

        _frames++;
        if (_cb)
            _cb(raw[1], raw[2], raw + HEADER_LEN, body - HEADER_LEN, _ctx);
    }
}
