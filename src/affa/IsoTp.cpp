#include "IsoTp.h"

namespace IsoTp {

int fragment(uint16_t id, const uint8_t* payload, int len, uint8_t filler,
             Frame* out, int maxOut)
{
    int count = 0;
    int pos = 0;
    int num = 0;
    while (pos < len && count < maxOut) {
        Frame f;
        f.id = id;
        f.len = 8;
        f.extended = false;
        int i = 0;
        if (num > 0)
            f.data[i++] = (uint8_t)(0x20 + num);     // continuation PCI
        while (i < 8 && pos < len)
            f.data[i++] = payload[pos++];
        while (i < 8)
            f.data[i++] = filler;                    // pad
        out[count++] = f;
        num++;
    }
    return count;
}

bool Reassembler::onFrame(const Frame& f)
{
    if (f.len == 0)
        return false;

    if (f.data[0] == 0x10) {                         // first frame: 8 bytes incl. PCI
        _len = 0;
        _active = true;
        for (int i = 0; i < 8 && _len < MAX_PAYLOAD; i++)
            _buf[_len++] = f.data[i];
        return true;
    }

    if ((f.data[0] & 0xF0) == 0x20 && _active) {     // continuation: append [1..7]
        for (int i = 1; i < 8 && _len < MAX_PAYLOAD; i++)
            _buf[_len++] = f.data[i];
        return true;
    }

    return false;                                    // not an ISO-TP data frame
}

} // namespace IsoTp
