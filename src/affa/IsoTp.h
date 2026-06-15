#pragma once
#include <stdint.h>
#include "../bus/Frame.h"

// AFFA3 ISO-TP framing, shared by the radio sender, the virtual displays, the proxy
// and the tests so there is ONE definition of the wire layout. Matches
// AffaDisplayBase::affa3_do_send exactly:
//   * first frame  (num 0): 8 raw payload bytes, NO PCI prefix
//   * consecutive  (num N): [0x20 + N] then 7 payload bytes
//   * every frame padded to 8 with the protocol filler (Carminat 0x00 / UL 0x81)
namespace IsoTp {

constexpr int MAX_PAYLOAD = 128;

// Split a payload into CAN frames (the radio's outbound direction). Returns the
// number of frames written to `out` (caller sizes it; 128 payload bytes -> <=19).
int fragment(uint16_t id, const uint8_t* payload, int len, uint8_t filler,
             Frame* out, int maxOut);

// Reassembles the receive direction: feed each frame in arrival order; the buffer
// grows as continuation frames append. Decoders read buffer()/len() after each
// frame (the showMenu payload is decodable once len() >= 96).
class Reassembler {
public:
    // Returns true while a message is being assembled (first frame seen). A frame
    // with data[0]==0x10 starts a fresh message; 0x2N appends; anything else is
    // ignored (returns false, buffer unchanged).
    bool onFrame(const Frame& f);

    const uint8_t* buffer() const { return _buf; }
    int len() const { return _len; }
    bool active() const { return _active; }
    void reset() { _len = 0; _active = false; }

private:
    uint8_t _buf[MAX_PAYLOAD] = {0};
    int     _len = 0;
    bool    _active = false;
};

} // namespace IsoTp
