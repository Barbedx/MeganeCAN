// Phase 4 tests: ReplayCanBus parses a captured .canlog and pumps it through a virtual
// display; a committed fixture is the regression golden (decoded ScreenModel must
// match). Also covers the .canlog line-grammar variants.
#include <unity.h>
#include "record/ReplayCanBus.h"
#include "vdisplay/CarminatVirtualDisplay.h"
#include "test/FakeClock.h"

static FakeClock clk;
static void feed(const Frame& f, void* ctx) {
    static_cast<CarminatVirtualDisplay*>(ctx)->onCanRx(f);
}

void setUp(void) { clk.reset(); }
void tearDown(void) {}

// --- line grammar: ms optional, tag optional (defaults TX), hex id + bytes --------
static void test_parse_grammar(void) {
    ReplayCanBus r;
    const char* cap =
        "# comment line ignored\n"
        "\n"
        "0 @TX 151 10 5A 21 01\n"      // ms + tag
        "@RX 3CF 61 11\n"              // tag, no ms
        "1C1 03 89 00 C0\n";           // bare: no ms, no tag (defaults TX)
    int n = r.loadText(cap);
    TEST_ASSERT_EQUAL_INT(3, n);
    TEST_ASSERT_EQUAL_INT(3, r.count());

    TEST_ASSERT_EQUAL_HEX32(0x151, r.frameAt(0).id);
    TEST_ASSERT_EQUAL_UINT8(4, r.frameAt(0).len);
    TEST_ASSERT_EQUAL_HEX8(0x10, r.frameAt(0).data[0]);
    TEST_ASSERT_EQUAL_UINT32(0, r.timeAt(0));

    TEST_ASSERT_EQUAL_HEX32(0x3CF, r.frameAt(1).id);
    TEST_ASSERT_EQUAL_HEX8(0x61, r.frameAt(1).data[0]);

    TEST_ASSERT_EQUAL_HEX32(0x1C1, r.frameAt(2).id);
    TEST_ASSERT_EQUAL_HEX8(0xC0, r.frameAt(2).data[3]);
}

// --- step() delivers one frame at a time; isLive() tracks remaining ---------------
static void test_step_and_islive(void) {
    ReplayCanBus r;
    r.loadText("100 @TX 100 01\n200 @TX 101 02\n");
    CarminatVirtualDisplay vd; vd.begin(r, clk);
    r.onReceive(feed, &vd);

    TEST_ASSERT_TRUE(r.isLive());
    TEST_ASSERT_EQUAL_INT(2, r.remaining());
    TEST_ASSERT_TRUE(r.step());
    TEST_ASSERT_EQUAL_INT(1, r.remaining());
    TEST_ASSERT_TRUE(r.step());
    TEST_ASSERT_FALSE(r.step());        // exhausted
    TEST_ASSERT_FALSE(r.isLive());
    r.rewind();
    TEST_ASSERT_TRUE(r.isLive());
}

// --- GOLDEN: replay the committed fixture, assert the decoded now-playing screen ---
static void test_golden_nowplaying(void) {
    ReplayCanBus r;
    int n = r.loadFile("test/fixtures/menu_nowplaying.canlog");
    TEST_ASSERT_MESSAGE(n > 0, "could not read test/fixtures/menu_nowplaying.canlog");
    TEST_ASSERT_EQUAL_INT(14, n);

    CarminatVirtualDisplay vd;
    vd.setEmulate(false);               // passive: decode only
    vd.begin(r, clk);
    r.onReceive(feed, &vd);
    r.poll();                           // replay the whole capture

    const ScreenModel& s = vd.screen();
    TEST_ASSERT_EQUAL_INT(ScreenModel::MENU, s.mode);
    TEST_ASSERT_EQUAL_STRING("Now Playing", s.header);
    TEST_ASSERT_EQUAL_STRING("Spotify", s.item0);
    TEST_ASSERT_EQUAL_STRING("Daft Punk - Around", s.item1);
    TEST_ASSERT_EQUAL_UINT8(0x0B, s.scroll);     // scroll DOWN
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_parse_grammar);
    RUN_TEST(test_step_and_islive);
    RUN_TEST(test_golden_nowplaying);
    return UNITY_END();
}
