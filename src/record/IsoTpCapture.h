#pragma once
#include <stdint.h>
#include <string.h>

// Captures ONE complete ISO-TP message on a chosen CAN id into a RAM buffer, fed from
// the RX callback. Unlike the @RX WebSocket mirror (which batches into a small buffer
// and drops frames during a fast burst), this appends directly into a fixed array in
// the CAN callback path, so it grabs a big payload WHOLE — e.g. the 0x1F1 "planet"
// image (302-byte ISO-TP), which we couldn't reconstruct from the lossy stream.
//
// 11-bit ISO-TP framing (as used by AFFA3):
//   single  0L        + L data bytes
//   first   1L LL     + 6 data bytes   (len = 0xLLL, 12-bit)
//   consec  2N        + 7 data bytes   (N = rolling sequence, not validated here)
//   other   (0x70 funcreg, 0x30 flow) -> ignored, won't corrupt the buffer
//
// Header-only, no Arduino deps (native-safe). Not thread-safe by itself: feed it only
// from the single CAN RX callback; read the snapshot from the HTTP task between frames
// (a torn read at worst shows a partially-updated buffer, harmless for a manual grab).
class IsoTpCapture {
public:
    static const uint32_t CAP = 512;

    explicit IsoTpCapture(uint32_t watchId) : _id(watchId) {}

    void onRx(uint32_t id, const uint8_t *d, uint8_t len)
    {
        if (id != _id || !d || len < 1)
            return;
        uint8_t pci = d[0] & 0xF0;
        if (pci == 0x10) {                          // first frame
            if (len < 2) return;
            _need = ((uint32_t)(d[0] & 0x0F) << 8) | d[1];
            if (_need > CAP) _need = CAP;
            _got = 0; _active = true; _complete = false;
            for (uint8_t i = 2; i < len && _got < _need; i++) _buf[_got++] = d[i];
            if (_got >= _need) { _complete = true; _active = false; }
        } else if (pci == 0x20) {                   // consecutive frame
            if (!_active) return;
            for (uint8_t i = 1; i < len && _got < _need; i++) _buf[_got++] = d[i];
            if (_got >= _need) { _complete = true; _active = false; }
        } else if (pci == 0x00) {                   // single frame
            uint8_t n = d[0] & 0x0F;
            if (n > CAP) n = CAP;
            _need = n; _got = 0;
            for (uint8_t i = 1; i < len && _got < n; i++) _buf[_got++] = d[i];
            _complete = true; _active = false;
        }
    }

    bool           complete() const { return _complete; }
    uint32_t       length()   const { return _got; }    // bytes captured so far
    uint32_t       declared() const { return _need; }   // length the first frame promised
    const uint8_t *data()     const { return _buf; }
    uint32_t       watchId()  const { return _id; }

private:
    uint32_t _id;
    uint8_t  _buf[CAP];
    uint32_t _need = 0, _got = 0;
    bool     _active = false, _complete = false;
};
