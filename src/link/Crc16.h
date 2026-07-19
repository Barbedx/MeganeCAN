#pragma once
#include <stdint.h>

// CRC16-CCITT (poly 0x1021, init 0xFFFF) — LinkProto's frame check. Bitwise
// (no 512B table): link frames are ≤128B and the UART is the bottleneck anyway.
namespace Crc16
{
    inline uint16_t ccitt(const uint8_t* data, uint16_t len, uint16_t crc = 0xFFFF)
    {
        for (uint16_t i = 0; i < len; i++)
        {
            crc ^= (uint16_t)data[i] << 8;
            for (int b = 0; b < 8; b++)
                crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021)
                                     : (uint16_t)(crc << 1);
        }
        return crc;
    }
}
