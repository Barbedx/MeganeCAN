#pragma once
#include <stdint.h>

// COBS (Consistent Overhead Byte Stuffing) — LinkProto's wire framing. Encoded
// blocks contain no 0x00, so a bare 0x00 is an unambiguous frame delimiter and
// the receiver can resynchronize after any corruption by hunting for it.
// Header-only, portable.
namespace Cobs
{
    // Worst-case encoded size for n payload bytes (one overhead byte per 254).
    constexpr uint16_t maxEncoded(uint16_t n) { return n + n / 254 + 1; }

    // Encode src[0..len) into dst; returns encoded length. dst must hold
    // maxEncoded(len) bytes. Does NOT append the 0x00 delimiter.
    inline uint16_t encode(const uint8_t* src, uint16_t len, uint8_t* dst)
    {
        uint16_t out = 0;
        uint16_t codeIdx = out++;   // where the current block's code byte goes
        uint8_t  code = 1;
        for (uint16_t i = 0; i < len; i++)
        {
            if (src[i] == 0)
            {
                dst[codeIdx] = code;
                codeIdx = out++;
                code = 1;
            }
            else
            {
                dst[out++] = src[i];
                if (++code == 0xFF)
                {
                    dst[codeIdx] = code;
                    codeIdx = out++;
                    code = 1;
                }
            }
        }
        dst[codeIdx] = code;
        return out;
    }

    // Decode src[0..len) (no delimiter) into dst; returns decoded length, or -1
    // on malformed input (embedded zero / truncated block). dst may need len bytes.
    inline int decode(const uint8_t* src, uint16_t len, uint8_t* dst)
    {
        uint16_t out = 0, i = 0;
        while (i < len)
        {
            uint8_t code = src[i++];
            if (code == 0) return -1;               // zeros never appear inside
            for (uint8_t j = 1; j < code; j++)
            {
                if (i >= len) return -1;            // truncated block
                if (src[i] == 0) return -1;
                dst[out++] = src[i++];
            }
            if (code != 0xFF && i < len)
                dst[out++] = 0;                     // implicit zero between blocks
        }
        return out;
    }
}
