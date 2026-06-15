// Phase 3 tests: the shared AFFA3 decode/encode layer + the virtual displays, end to
// end, with no hardware. We build a real showMenu/setText payload, ISO-TP-fragment it
// exactly as the radio's affa3_do_send would, feed the frames to a virtual display on
// a LoopbackCanBus, then assert the decoded ScreenModel + the auto-ACK / key frames it
// emits back onto the bus.
#include <unity.h>
#include <string.h>
#include "bus/LoopbackCanBus.h"
#include "affa/IsoTp.h"
#include "affa/ScreenDecode.h"
#include "vdisplay/CarminatVirtualDisplay.h"
#include "vdisplay/UpdateListSegVirtualDisplay.h"
#include "vdisplay/UpdateListLcdVirtualDisplay.h"
#include "test/FakeClock.h"

// ---- helpers ---------------------------------------------------------------
static void putStr(uint8_t* p, int at, const char* s, int field) {
    int i = 0;
    for (; s[i] && i < field; i++) p[at + i] = (uint8_t)s[i];
    for (; i < field; i++) p[at + i] = ' ';
}

// Build a 96-byte showMenu payload like CarminatDisplay::showMenu produces.
static int buildMenu(uint8_t* p, uint8_t scroll, const char* hdr,
                     const char* it0, const char* it1) {
    memset(p, 0x00, 96);
    p[0] = 0x10; p[1] = 0x5A;
    p[ScreenDecode::OFF_SCROLL] = scroll;
    putStr(p, ScreenDecode::OFF_HEADER, hdr, 26);
    p[ScreenDecode::OFF_ITEM0_MARK] = 0x7E;
    putStr(p, ScreenDecode::OFF_ITEM0, it0, 25);
    p[ScreenDecode::OFF_ITEM1_MARK] = 0x7F;
    putStr(p, ScreenDecode::OFF_ITEM1, it1, 30);
    return 96;
}

// Build an UpdateList setText payload like UpdateListBase::setText produces.
static int buildSeg(uint8_t* p, const char* oldT, const char* newT) {
    p[0] = 0x10; p[1] = 0x19; p[2] = 0x76; p[3] = 0x71; p[4] = 0x01;
    putStr(p, 5, oldT, 8);
    p[13] = 0x10;
    putStr(p, 14, newT, 12);
    p[26] = 0x00; p[27] = 0x81; p[28] = 0x81;
    return 29;
}

// Capture frames the display emits (ACKs / keys) onto the bus.
struct Cap { Frame f[64]; int n = 0; };
static void onCap(const Frame& f, void* ctx) {
    Cap* c = (Cap*)ctx;
    if (c->n < 64) c->f[c->n++] = f;
}

static FakeClock clk;

void setUp(void) { clk.reset(); }
void tearDown(void) {}

// ---- IsoTp round-trip ------------------------------------------------------
static void test_isotp_roundtrip(void) {
    uint8_t payload[96];
    buildMenu(payload, 0x0B, "HDR", "A", "B");

    Frame frames[24];
    int nf = IsoTp::fragment(0x151, payload, 96, 0x00, frames, 24);
    TEST_ASSERT_TRUE(nf >= 13);                 // 96B -> >=13 frames
    TEST_ASSERT_EQUAL_HEX8(0x10, frames[0].data[0]);  // first frame: no PCI prefix
    TEST_ASSERT_EQUAL_HEX8(0x21, frames[1].data[0]);  // continuation 0x20+1
    TEST_ASSERT_EQUAL_HEX8(0x22, frames[2].data[0]);

    IsoTp::Reassembler asmr;
    for (int i = 0; i < nf; i++) asmr.onFrame(frames[i]);
    TEST_ASSERT_TRUE(asmr.len() >= 96);
    // reassembled prefix matches the original payload
    TEST_ASSERT_EQUAL_UINT8_ARRAY(payload, asmr.buffer(), 96);
}

// ---- ScreenDecode asciiz ---------------------------------------------------
static void test_asciiz_trims(void) {
    const uint8_t p[] = "  Hello  \x00xx";
    char out[16];
    ScreenDecode::asciiz(p, sizeof(p), 0, 10, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("Hello", out);
}

// ---- Carminat: menu decode via the virtual display -------------------------
static void test_carminat_menu_decode(void) {
    LoopbackCanBus bus;
    Cap cap; bus.onReceive(onCap, &cap);
    CarminatVirtualDisplay vd;
    vd.begin(bus, clk);

    uint8_t payload[96];
    buildMenu(payload, Carminat::SCROLL_DOWN, "Main Menu", "Voltage: 0V", "Boost: 0mbar");
    Frame frames[24];
    int nf = IsoTp::fragment(0x151, payload, 96, 0x00, frames, 24);
    for (int i = 0; i < nf; i++) vd.onCanRx(frames[i]);

    const ScreenModel& s = vd.screen();
    TEST_ASSERT_EQUAL_INT(ScreenModel::MENU, s.mode);
    TEST_ASSERT_EQUAL_STRING("Main Menu", s.header);
    TEST_ASSERT_EQUAL_STRING("Voltage: 0V", s.item0);
    TEST_ASSERT_EQUAL_STRING("Boost: 0mbar", s.item1);
    TEST_ASSERT_EQUAL_HEX8(0x7E, s.item0Id);
    TEST_ASSERT_EQUAL_HEX8(0x7F, s.item1Id);
    TEST_ASSERT_EQUAL_UINT8(Carminat::SCROLL_DOWN, s.scroll);

    // Every received frame was auto-ACKed: 0x74 on (0x151 | 0x400) = 0x551.
    bus.poll();
    TEST_ASSERT_EQUAL_INT(nf, cap.n);
    TEST_ASSERT_EQUAL_HEX32(0x551, cap.f[0].id);
    TEST_ASSERT_EQUAL_HEX8(0x74, cap.f[0].data[0]);
}

// ---- Carminat: highlight + passive mode (no ACK) ---------------------------
static void test_carminat_highlight_and_passive(void) {
    LoopbackCanBus bus;
    Cap cap; bus.onReceive(onCap, &cap);
    CarminatVirtualDisplay vd;
    vd.begin(bus, clk);
    vd.setEmulate(false);                        // PASSIVE: decode only, no ACK

    Frame hl; hl.id = 0x151; hl.len = 8;
    hl.data[0] = 0x07; hl.data[1] = 0x29; hl.data[2] = 0x01; hl.data[3] = 0x7F;
    vd.onCanRx(hl);
    TEST_ASSERT_EQUAL_INT(0x7F, vd.screen().sel);

    bus.poll();
    TEST_ASSERT_EQUAL_INT(0, cap.n);             // passive -> emitted nothing
}

// ---- Carminat: key codec (hold mask + encoder exception) -------------------
static void test_carminat_key_codec(void) {
    LoopbackCanBus bus;
    Cap cap; bus.onReceive(onCap, &cap);
    CarminatVirtualDisplay vd;
    vd.begin(bus, clk);

    vd.pressKey(0x0000, true);                   // Load, hold -> low byte gets 0xC0
    vd.pressKey(0x0141, true);                   // RollDown encoder -> never masked
    bus.poll();

    TEST_ASSERT_EQUAL_INT(2, cap.n);
    TEST_ASSERT_EQUAL_HEX32(0x1C1, cap.f[0].id);
    TEST_ASSERT_EQUAL_HEX8(0x03, cap.f[0].data[0]);
    TEST_ASSERT_EQUAL_HEX8(0x89, cap.f[0].data[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00, cap.f[0].data[2]);
    TEST_ASSERT_EQUAL_HEX8(0xC0, cap.f[0].data[3]);   // hold mask applied
    TEST_ASSERT_EQUAL_HEX8(0x01, cap.f[1].data[2]);
    TEST_ASSERT_EQUAL_HEX8(0x41, cap.f[1].data[3]);   // encoder: unmasked
}

// ---- Carminat: sync registration sets synced --------------------------------
static void test_carminat_sync(void) {
    LoopbackCanBus bus;
    CarminatVirtualDisplay vd;
    vd.begin(bus, clk);
    TEST_ASSERT_FALSE(vd.synced());

    Frame reg; reg.id = 0x3AF; reg.len = 8; reg.data[0] = 0x70;
    vd.onCanRx(reg);
    TEST_ASSERT_TRUE(vd.synced());
}

// ---- UpdateList 8-seg: setText decode --------------------------------------
static void test_ul_seg_decode(void) {
    LoopbackCanBus bus;
    Cap cap; bus.onReceive(onCap, &cap);
    UpdateListSegVirtualDisplay vd;
    vd.begin(bus, clk);

    uint8_t payload[32];
    int len = buildSeg(payload, "OLDTEXT", "NEW TEXT");
    Frame frames[8];
    int nf = IsoTp::fragment(0x121, payload, len, 0x81, frames, 8);
    for (int i = 0; i < nf; i++) vd.onCanRx(frames[i]);

    TEST_ASSERT_EQUAL_INT(ScreenModel::MENU, vd.screen().mode);
    TEST_ASSERT_EQUAL_STRING("NEW TEXT", vd.screen().header);
    TEST_ASSERT_EQUAL_STRING("OLDTEXT", vd.screen().item0);

    bus.poll();                                  // setText frames ACKed on 0x521
    TEST_ASSERT_EQUAL_INT(nf, cap.n);
    TEST_ASSERT_EQUAL_HEX32(0x521, cap.f[0].id);
    TEST_ASSERT_EQUAL_HEX8(0x74, cap.f[0].data[0]);
}

// ---- UpdateList LCD: provisional, but mechanics work (key on 0x0A9) ---------
static void test_ul_lcd_key(void) {
    LoopbackCanBus bus;
    Cap cap; bus.onReceive(onCap, &cap);
    UpdateListLcdVirtualDisplay vd;
    vd.begin(bus, clk);
    vd.pressKey(0x0101, false);                  // RollUp
    bus.poll();
    TEST_ASSERT_EQUAL_INT(1, cap.n);
    TEST_ASSERT_EQUAL_HEX32(0x0A9, cap.f[0].id);
    TEST_ASSERT_EQUAL_HEX8(0x01, cap.f[0].data[2]);
    TEST_ASSERT_EQUAL_HEX8(0x01, cap.f[0].data[3]);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_isotp_roundtrip);
    RUN_TEST(test_asciiz_trims);
    RUN_TEST(test_carminat_menu_decode);
    RUN_TEST(test_carminat_highlight_and_passive);
    RUN_TEST(test_carminat_key_codec);
    RUN_TEST(test_carminat_sync);
    RUN_TEST(test_ul_seg_decode);
    RUN_TEST(test_ul_lcd_key);
    return UNITY_END();
}
