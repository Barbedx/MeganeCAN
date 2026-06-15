#pragma once
#include "../bus/ICanBus.h"

// Replays a captured .canlog (the wireless recorder's frame stream from Phase 1) back
// through the bus seam: bind a virtual display's onCanRx to onReceive(), then pump
// with poll()/step(). On the host (FakeClock) committed fixtures become regression
// goldens; on the bench (ArduinoClock) it re-drives a real/virtual display from a
// recording. It is a SOURCE — send() is a no-op (replayed buses don't accept TX).
//
// .canlog v1 line grammar (one frame per line; blank lines and '#' comments ignored):
//     [<ms>] <@TX|@RX|TX|RX> <id-hex> <b0> <b1> ...
//   * leading <ms> timestamp is optional (relative capture time; informational)
//   * the direction tag is optional and defaults to TX (radio -> display)
//   * id + bytes are hex; up to 8 data bytes
// This is exactly the WireProto @TX/@RX wire format (optionally timestamped), so a
// saved recorder stream is a valid fixture with no transformation.
class ReplayCanBus : public ICanBus {
public:
    static constexpr int MAX_FRAMES = 512;

    // Parse capture text. Returns the number of frames loaded (appended to any
    // already loaded). data need not be NUL-terminated if len is given.
    int loadText(const char* data, int len = -1);

    // Read + parse a .canlog file (host convenience; NATIVE/desktop only).
    int loadFile(const char* path);

    // ICanBus
    bool send(const Frame&) override { return false; }   // replay source: no TX
    void onReceive(RxHandler h, void* ctx) override { _rx = h; _rxCtx = ctx; }
    bool isLive() const override { return _pos < _count; }
    void poll() override { while (step()) {} }           // deliver all remaining

    // Deliver the next frame to the handler. Returns false when exhausted.
    bool step();

    int  count() const { return _count; }
    int  remaining() const { return _count - _pos; }
    void rewind() { _pos = 0; }
    void clear() { _count = 0; _pos = 0; }

    // Inspect a loaded record (tests).
    const Frame& frameAt(int i) const { return _rec[i].f; }
    uint32_t timeAt(int i) const { return _rec[i].ms; }

private:
    struct Rec { uint32_t ms; Frame f; };
    bool parseLine(const char* line, int len);

    Rec       _rec[MAX_FRAMES];
    int       _count = 0;
    int       _pos = 0;
    RxHandler _rx = nullptr;
    void*     _rxCtx = nullptr;
};
