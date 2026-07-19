#include "HuRxParser.h"
#include "CanboxEmitter.h"   // checksum (same Raise formula both directions)

namespace Vh
{
    bool HuRxParser::isKnownCmd(uint8_t cmd)
    {
        switch (cmd)
        {
        case 0x81:   // start/stop (b0 1=connect)
        case 0x83:   // vehicle-set
        case 0x84:   // common-set
        case 0x90:   // info request
        case 0xA0:   // amp
        case 0xA6:   // time sync
        case 0xC0:   // source select
        case 0xC1:   // play mode
        case 0xC2:   // tuner freq
        case 0xC3:   // media playback disc/folder/track/time
        case 0xC4:   // volume
            return true;
        default:
            return false;
        }
    }

    void HuRxParser::feed(const uint8_t* data, uint16_t len)
    {
        for (uint16_t i = 0; i < len; i++)
            feed(data[i]);
    }

    void HuRxParser::feed(uint8_t b)
    {
        switch (_st)
        {
        case WAIT:
            if (b == 0x2E)      _st = CMD;
            else if (b == 0xFF) _acks++;
            else if (b == 0xF0) _nacks++;
            else                _noise++;
            return;
        case CMD:
            _cmd = b;
            _st = LEN;
            return;
        case LEN:
            if (b > MAX_PAYLOAD) { _noise++; _st = WAIT; return; }
            _len = b;
            _got = 0;
            _st = _len ? PAYLOAD : CS;
            return;
        case PAYLOAD:
            _buf[_got++] = b;
            if (_got == _len) _st = CS;
            return;
        case CS:
            _st = WAIT;
            if (b != CanboxEmitter::checksum(_cmd, _len, _buf))
            {
                _badCs++;
                return;
            }
            _frames++;
            bool known = isKnownCmd(_cmd);
            if (!known) _unknown++;
            if (_cb) _cb(_cmd, _buf, _len, known, _ctx);
            return;
        }
    }
}
