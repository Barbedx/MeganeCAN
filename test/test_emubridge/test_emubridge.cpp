// Phase 5 / refactor: the FULL-EMULATION closed loop as a portable, unit-tested module.
// Feed the EXACT real menu frames through EmuBridge's TX tap (as if the radio sent
// them) and assert the twin decoded the screen AND that every frame was ACKed back
// (30 01 00 on id|0x400) through recvFn — i.e. the radio's per-frame ACK is satisfied.
#include <unity.h>
#include "emulation/EmuBridge.h"
#include "vdisplay/CarminatVirtualDisplay.h"
#include "test/FakeClock.h"

struct Cap { Frame f[64]; int n = 0; };
static Cap g_cap;
static void capRecv(const Frame& f, void* ctx) {
    Cap* c = (Cap*)ctx;
    if (c->n < 64) c->f[c->n++] = f;
}

// The real 14 menu frames + a highlight (same bytes as the committed fixture).
static const uint8_t REAL[][8] = {
    {0x10,0x5A,0x21,0x01,0x7E,0x80,0x00,0x00},
    {0x21,0x82,0xFF,0x0B,0x4D,0x61,0x69,0x6E},
    {0x22,0x20,0x4D,0x65,0x6E,0x75,0x00,0x00},
    {0x23,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x24,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x25,0x00,0x00,0x7E,0x56,0x6F,0x6C,0x74},
    {0x26,0x61,0x67,0x65,0x3A,0x20,0x30,0x56},
    {0x27,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x28,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x29,0x01,0x7F,0x42,0x6F,0x6F,0x73,0x74},
    {0x2A,0x3A,0x20,0x30,0x6D,0x62,0x61,0x72},
    {0x2B,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x2C,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x2D,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x07,0x29,0x01,0x7F,0x80,0x00,0x00,0x00},
};

static FakeClock clk;
void setUp(void) { clk.reset(); g_cap.n = 0; }
void tearDown(void) {}

static void test_bridge_closed_loop(void) {
    CarminatVirtualDisplay vd;
    EmuBridge br;
    br.begin(vd, capRecv, &g_cap, clk);

    // Disabled (passive): the tap still feeds the twin (always-live decode), but the
    // twin emits no ACK back — recvFn sees nothing.
    br.tap().onTx(Frame{0x151, 8, {0x10,0x5A,0,0,0,0,0,0}, false});
    TEST_ASSERT_EQUAL_INT(0, g_cap.n);
    TEST_ASSERT_FALSE(br.enabled());

    br.enable(true);
    TEST_ASSERT_TRUE(br.enabled());
    for (auto& row : REAL) {
        Frame f; f.id = 0x151; f.len = 8;
        for (int i = 0; i < 8; i++) f.data[i] = row[i];
        br.tap().onTx(f);
    }

    // Twin decoded the screen.
    const ScreenModel& s = br.screen();
    TEST_ASSERT_EQUAL_STRING("Main Menu", s.header);
    TEST_ASSERT_EQUAL_STRING("Voltage: 0V", s.item0);
    TEST_ASSERT_EQUAL_STRING("Boost: 0mbar", s.item1);
    TEST_ASSERT_EQUAL_INT(0x7F, s.sel);

    // Every frame ACKed back through recvFn: 30 01 00 on 0x551.
    TEST_ASSERT_EQUAL_INT(15, g_cap.n);
    TEST_ASSERT_EQUAL_HEX32(0x551, g_cap.f[0].id);
    TEST_ASSERT_EQUAL_HEX8(0x30, g_cap.f[0].data[0]);
    TEST_ASSERT_EQUAL_HEX8(0x01, g_cap.f[0].data[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00, g_cap.f[0].data[2]);
}

// Phase B: the screen is always-live. While the bridge is DISABLED, feeding the
// radio's frames still decodes the screen (read-only) and stamps lastDecodeMs, but
// the twin never ACKs back — passive observation, exactly like a real panel running
// in parallel. Enabling later begins ACKing without re-feeding.
static void test_always_live_decode(void) {
    CarminatVirtualDisplay vd;
    EmuBridge br;
    br.begin(vd, capRecv, &g_cap, clk);

    TEST_ASSERT_FALSE(br.enabled());
    TEST_ASSERT_EQUAL_UINT32(0, br.lastDecodeMs());

    clk.advance(1000);
    for (auto& row : REAL) {
        Frame f; f.id = 0x151; f.len = 8;
        for (int i = 0; i < 8; i++) f.data[i] = row[i];
        br.tap().onTx(f);
    }

    // Decoded while passive...
    const ScreenModel& s = br.screen();
    TEST_ASSERT_EQUAL_STRING("Main Menu", s.header);
    TEST_ASSERT_EQUAL_STRING("Voltage: 0V", s.item0);
    TEST_ASSERT_EQUAL_INT(0x7F, s.sel);
    TEST_ASSERT_EQUAL_UINT32(1000, br.lastDecodeMs());

    // ...but emitted no ACK (no recvFn traffic) because emulation is off.
    TEST_ASSERT_EQUAL_INT(0, g_cap.n);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_bridge_closed_loop);
    RUN_TEST(test_always_live_decode);
    return UNITY_END();
}
