// VH domain-core tests: Raise frame golden vectors, HU RX parsing, and the
// VehicleDbc decode table (synthetic Clio-III-hypothesis frames).
#include <unity.h>
#include <vector>
#include "vh/CanboxEmitter.h"
#include "vh/HuRxParser.h"
#include "vh/VehicleDecoder.h"

using namespace Vh;

void setUp(void) {}
void tearDown(void) {}

// ---- Raise framing golden vectors (§8.4) -----------------------------------

// Verified example from the protocol reference: Vol+ = 2E 20 02 01 01 DB.
static void test_raise_golden_volplus(void) {
    uint8_t out[8];
    const uint8_t p[] = {0x01, 0x01};
    uint8_t n = CanboxEmitter::buildFrame(0x20, p, 2, out);
    const uint8_t golden[] = {0x2E, 0x20, 0x02, 0x01, 0x01, 0xDB};
    TEST_ASSERT_EQUAL_UINT8(sizeof(golden), n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(golden, out, n);
}

// HU->box examples from the spec: volume 2E C4 01 04 36, info-req 2E 90 01 1E 50.
static void test_raise_checksum_spec_frames(void) {
    const uint8_t vol = 0x04;
    TEST_ASSERT_EQUAL_HEX8(0x36, CanboxEmitter::checksum(0xC4, 1, &vol));
    const uint8_t req = 0x1E;
    TEST_ASSERT_EQUAL_HEX8(0x50, CanboxEmitter::checksum(0x90, 1, &req));
}

// ---- CanboxEmitter schedulers ----------------------------------------------

struct TxCapture {
    std::vector<std::vector<uint8_t>> frames;
};
static void onTx(const uint8_t* f, uint8_t len, void* ctx) {
    static_cast<TxCapture*>(ctx)->frames.emplace_back(f, f + len);
}
static int countCmd(const TxCapture& c, uint8_t cmd, int sub = -1) {
    int n = 0;
    for (auto& f : c.frames)
        if (f.size() > 3 && f[1] == cmd && (sub < 0 || f[3] == (uint8_t)sub)) n++;
    return n;
}

static void test_emitter_key_immediate(void) {
    TxCapture cap;
    CanboxEmitter e;
    e.begin(onTx, &cap);
    e.sendKey(0x01, 1);   // VOL+ press
    e.sendKey(0x01, 0);   // release
    TEST_ASSERT_EQUAL_INT(2, cap.frames.size());
    const uint8_t golden[] = {0x2E, 0x20, 0x02, 0x01, 0x01, 0xDB};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(golden, cap.frames[0].data(), 6);
}

static void test_emitter_speed_schedule(void) {
    TxCapture cap;
    CanboxEmitter e;
    e.begin(onTx, &cap);
    VehicleState s;
    s.set(SIG_SPEED, 5000, 1000);        // 50.00 km/h

    e.tick(s, 1000);
    TEST_ASSERT_EQUAL_INT(1, countCmd(cap, 0x7D, 0x03));
    e.tick(s, 1100);                     // < 500ms: no repeat
    TEST_ASSERT_EQUAL_INT(1, countCmd(cap, 0x7D, 0x03));
    e.tick(s, 1600);
    TEST_ASSERT_EQUAL_INT(2, countCmd(cap, 0x7D, 0x03));

    // Payload check: [0x03][5000 LE][0][0]
    for (auto& f : cap.frames)
        if (f[1] == 0x7D && f[3] == 0x03) {
            TEST_ASSERT_EQUAL_HEX8(0x88, f[4]);   // 5000 = 0x1388
            TEST_ASSERT_EQUAL_HEX8(0x13, f[5]);
            break;
        }
}

static void test_emitter_doors_on_change(void) {
    TxCapture cap;
    CanboxEmitter e;
    e.begin(onTx, &cap);
    VehicleState s;
    s.set(SIG_DOOR_FL, 0, 1000);
    e.tick(s, 1000);                     // first: interval fire, mask 0x00
    TEST_ASSERT_EQUAL_INT(1, countCmd(cap, 0x24));
    e.tick(s, 1010);                     // no change, not due
    TEST_ASSERT_EQUAL_INT(1, countCmd(cap, 0x24));
    s.set(SIG_DOOR_FL, 1, 1020);         // driver door opens
    e.tick(s, 1020);                     // on-change fires before 250ms
    TEST_ASSERT_EQUAL_INT(2, countCmd(cap, 0x24));
    TEST_ASSERT_EQUAL_HEX8(0x80, cap.frames.back()[3]);   // driver bit
}

static void test_emitter_temp_encoding(void) {
    TxCapture cap;
    CanboxEmitter e;
    e.begin(onTx, &cap);
    VehicleState s;
    s.set(SIG_OUT_TEMP, 25, 1000);
    e.tick(s, 1000);
    TEST_ASSERT_EQUAL_INT(1, countCmd(cap, 0x28));
    const auto& f = cap.frames.back();
    TEST_ASSERT_EQUAL_INT(3 + 12 + 1, f.size());          // hdr + 12B payload + cs
    TEST_ASSERT_EQUAL_HEX8((25 + 40) * 2, f[3 + 5]);      // payload[5]
}

static void test_emitter_steering_clamp(void) {
    TxCapture cap;
    CanboxEmitter e;
    e.begin(onTx, &cap);
    VehicleState s;
    s.set(SIG_STEERING, -6000, 1000);    // -600.0° -> clamp to -540
    e.tick(s, 1000);
    TEST_ASSERT_EQUAL_INT(1, countCmd(cap, 0x29));
    const auto& f = cap.frames.back();
    int16_t v = (int16_t)(f[3] | (f[4] << 8));
    TEST_ASSERT_EQUAL_INT16(-540, v);
}

static void test_emitter_disabled_silent(void) {
    TxCapture cap;
    CanboxEmitter e;
    e.begin(onTx, &cap);
    e.setEnabled(false);
    VehicleState s;
    s.set(SIG_SPEED, 1000, 1000);
    e.tick(s, 1000);
    e.sendKey(0x01, 1);
    TEST_ASSERT_EQUAL_INT(0, cap.frames.size());
}

// ---- HuRxParser ------------------------------------------------------------

struct HuCapture {
    uint8_t cmd = 0;
    std::vector<uint8_t> payload;
    bool known = false;
    int count = 0;
};
static void onHu(uint8_t cmd, const uint8_t* p, uint8_t len, bool known, void* ctx) {
    auto* c = static_cast<HuCapture*>(ctx);
    c->cmd = cmd; c->payload.assign(p, p + len); c->known = known; c->count++;
}

static void test_hu_parses_volume(void) {
    HuRxParser hu;
    HuCapture cap;
    hu.onFrame(onHu, &cap);
    const uint8_t wire[] = {0x2E, 0xC4, 0x01, 0x04, 0x36};   // spec example
    hu.feed(wire, sizeof(wire));
    TEST_ASSERT_EQUAL_INT(1, cap.count);
    TEST_ASSERT_EQUAL_HEX8(0xC4, cap.cmd);
    TEST_ASSERT_TRUE(cap.known);
    TEST_ASSERT_EQUAL_HEX8(0x04, cap.payload[0]);
}

static void test_hu_unknown_cmd_flagged(void) {
    HuRxParser hu;
    HuCapture cap;
    hu.onFrame(onHu, &cap);
    // Hypothetical undocumented media-text frame: cmd 0xC7 "AB"
    const uint8_t p[] = {'A', 'B'};
    uint8_t wire[8];
    uint8_t n = CanboxEmitter::buildFrame(0xC7, p, 2, wire);
    hu.feed(wire, n);
    TEST_ASSERT_EQUAL_INT(1, cap.count);
    TEST_ASSERT_FALSE(cap.known);              // -> goes to the capture log
    TEST_ASSERT_EQUAL_UINT32(1, hu.unknown());
}

static void test_hu_bad_checksum_dropped_then_resync(void) {
    HuRxParser hu;
    HuCapture cap;
    hu.onFrame(onHu, &cap);
    const uint8_t bad[]  = {0x2E, 0xC4, 0x01, 0x04, 0x00};   // wrong cs
    const uint8_t good[] = {0x2E, 0x90, 0x01, 0x1E, 0x50};
    hu.feed(bad, sizeof(bad));
    hu.feed(good, sizeof(good));
    TEST_ASSERT_EQUAL_INT(1, cap.count);
    TEST_ASSERT_EQUAL_HEX8(0x90, cap.cmd);
    TEST_ASSERT_EQUAL_UINT32(1, hu.badCs());
}

static void test_hu_ack_nack_bytes(void) {
    HuRxParser hu;
    const uint8_t stream[] = {0xFF, 0xFF, 0xF0, 0x2E, 0x90, 0x01, 0x1E, 0x50};
    hu.feed(stream, sizeof(stream));
    TEST_ASSERT_EQUAL_UINT32(2, hu.acks());
    TEST_ASSERT_EQUAL_UINT32(1, hu.nacks());
    TEST_ASSERT_EQUAL_UINT32(1, hu.frames());
}

// ---- VehicleDecoder --------------------------------------------------------

static Frame mkFrame(uint16_t id, std::initializer_list<uint8_t> bytes) {
    Frame f;
    f.id = id;
    f.len = 0;
    for (uint8_t b : bytes) f.data[f.len++] = b;
    return f;
}

static void test_decode_speed(void) {
    VehicleDecoder d;
    d.feed(mkFrame(0x354, {0x13, 0x88, 0, 0, 0, 0, 0, 0}), 1000); // 5000 = 50km/h
    TEST_ASSERT_TRUE(d.state().has(SIG_SPEED));
    TEST_ASSERT_EQUAL_INT32(5000, d.state().get(SIG_SPEED));
}

static void test_decode_steering_center_and_offset(void) {
    VehicleDecoder d;
    d.feed(mkFrame(0x0C2, {0x80, 0x00, 0, 0, 0, 0, 0, 0}), 1000); // centered
    TEST_ASSERT_EQUAL_INT32(0, d.state().get(SIG_STEERING));
    d.feed(mkFrame(0x0C2, {0x80, 0x64, 0, 0, 0, 0, 0, 0}), 1001); // +100 = +10.0°
    TEST_ASSERT_EQUAL_INT32(100, d.state().get(SIG_STEERING));
    d.feed(mkFrame(0x0C2, {0x7F, 0x9C, 0, 0, 0, 0, 0, 0}), 1002); // -100 = -10.0°
    TEST_ASSERT_EQUAL_INT32(-100, d.state().get(SIG_STEERING));
}

static void test_decode_rpm_div8(void) {
    VehicleDecoder d;
    d.feed(mkFrame(0x181, {0xFA, 0x00, 0, 0, 0, 0x01, 0, 0}), 1000); // 64000/8
    TEST_ASSERT_EQUAL_INT32(8000, d.state().get(SIG_RPM));
    TEST_ASSERT_EQUAL_INT32(1, d.state().get(SIG_BRAKE));            // b5 bit0
}

static void test_decode_60d_status(void) {
    VehicleDecoder d;
    // bit19 (FL door) -> b0 bit3; b4 outside temp 65 -> 25°C; b6 bit4 reverse
    d.feed(mkFrame(0x60D, {0x08, 0x00, 0x00, 0x00, 65, 0x00, 0x10, 0x00}), 1000);
    TEST_ASSERT_EQUAL_INT32(1, d.state().get(SIG_DOOR_FL));
    TEST_ASSERT_EQUAL_INT32(0, d.state().get(SIG_DOOR_FR));
    TEST_ASSERT_EQUAL_INT32(25, d.state().get(SIG_OUT_TEMP));
    TEST_ASSERT_EQUAL_INT32(1, d.state().get(SIG_REVERSE));
}

static void test_decode_indicator_latch(void) {
    VehicleDecoder d;
    // bit14 (right indicator) -> 24-bit word bit14 = b1 bit6 = 0x40
    d.feed(mkFrame(0x60D, {0x00, 0x40, 0x00, 0x00, 40, 0x00, 0x00, 0x00}), 1000);
    TEST_ASSERT_EQUAL_INT32(1, d.state().get(SIG_IND_RIGHT));
    // Blink gap: a 0-frame 200ms later must NOT clear the latch...
    d.feed(mkFrame(0x60D, {0x00, 0x00, 0x00, 0x00, 40, 0x00, 0x00, 0x00}), 1200);
    TEST_ASSERT_EQUAL_INT32(1, d.state().get(SIG_IND_RIGHT));
    // ...but after the 500ms window it clears.
    d.feed(mkFrame(0x60D, {0x00, 0x00, 0x00, 0x00, 40, 0x00, 0x00, 0x00}), 1700);
    TEST_ASSERT_EQUAL_INT32(0, d.state().get(SIG_IND_RIGHT));
}

static void test_decode_latch_expires_via_tick(void) {
    VehicleDecoder d;
    d.feed(mkFrame(0x60D, {0x00, 0x40, 0x00, 0x00, 40, 0x00, 0x00, 0x00}), 1000);
    TEST_ASSERT_EQUAL_INT32(1, d.state().get(SIG_IND_RIGHT));
    d.tick(1300);                          // inside the window: still on
    TEST_ASSERT_EQUAL_INT32(1, d.state().get(SIG_IND_RIGHT));
    d.tick(1600);                          // pulses stopped: expire
    TEST_ASSERT_EQUAL_INT32(0, d.state().get(SIG_IND_RIGHT));
}

static void test_decode_unknown_census(void) {
    VehicleDecoder d;
    d.feed(mkFrame(0x123, {1, 2, 3}), 1000);
    d.feed(mkFrame(0x123, {1, 2, 3}), 1001);
    d.feed(mkFrame(0x777, {1}), 1002);
    uint8_t n = 0;
    const VehicleDecoder::IdCount* c = d.census(n);
    TEST_ASSERT_EQUAL_UINT8(2, n);
    TEST_ASSERT_EQUAL_HEX16(0x123, c[0].id);
    TEST_ASSERT_EQUAL_UINT32(2, c[0].count);
    TEST_ASSERT_EQUAL_UINT32(3, d.unknownFrames());
}

static void test_decode_signal_disable(void) {
    VehicleDecoder d;
    d.setSignalEnabled(SIG_SPEED, false);
    d.feed(mkFrame(0x354, {0x13, 0x88, 0, 0, 0, 0, 0, 0}), 1000);
    TEST_ASSERT_FALSE(d.state().has(SIG_SPEED));
    d.setSignalEnabled(SIG_SPEED, true);
    d.feed(mkFrame(0x354, {0x13, 0x88, 0, 0, 0, 0, 0, 0}), 1001);
    TEST_ASSERT_TRUE(d.state().has(SIG_SPEED));
}

static void test_decode_short_frame_ignored(void) {
    VehicleDecoder d;
    d.feed(mkFrame(0x60D, {0x08, 0x00}), 1000);   // dlc 2 < minDlc 7
    TEST_ASSERT_FALSE(d.state().has(SIG_DOOR_FL));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_raise_golden_volplus);
    RUN_TEST(test_raise_checksum_spec_frames);
    RUN_TEST(test_emitter_key_immediate);
    RUN_TEST(test_emitter_speed_schedule);
    RUN_TEST(test_emitter_doors_on_change);
    RUN_TEST(test_emitter_temp_encoding);
    RUN_TEST(test_emitter_steering_clamp);
    RUN_TEST(test_emitter_disabled_silent);
    RUN_TEST(test_hu_parses_volume);
    RUN_TEST(test_hu_unknown_cmd_flagged);
    RUN_TEST(test_hu_bad_checksum_dropped_then_resync);
    RUN_TEST(test_hu_ack_nack_bytes);
    RUN_TEST(test_decode_speed);
    RUN_TEST(test_decode_steering_center_and_offset);
    RUN_TEST(test_decode_rpm_div8);
    RUN_TEST(test_decode_60d_status);
    RUN_TEST(test_decode_indicator_latch);
    RUN_TEST(test_decode_latch_expires_via_tick);
    RUN_TEST(test_decode_unknown_census);
    RUN_TEST(test_decode_signal_disable);
    RUN_TEST(test_decode_short_frame_ignored);
    return UNITY_END();
}
