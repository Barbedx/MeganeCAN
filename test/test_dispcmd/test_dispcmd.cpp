// Round-trip tests for the remote-display protocol: RemoteDisplay (GW side)
// serializes IDisplay calls + media into link frames; DispCmd::execute /
// HuLinkMediaSource (DISP side) reconstruct them. Testing the two halves
// together means they can never drift apart silently.
#include <unity.h>
#include <vector>
#include <string>
#include "gw/RemoteDisplay.h"
#include "disp/DispCmdServer.h"
#include "media/HuLinkMediaSource.h"
#include "link/LinkProto.h"

using AffaCommon::AffaKey;
using Err = AffaCommon::AffaError;

// ---- wire capture -----------------------------------------------------------
struct WireFrame { uint8_t type; std::vector<uint8_t> payload; };
static std::vector<WireFrame> s_wire;
static bool captureSend(uint8_t type, const uint8_t* p, uint16_t len, void*)
{
    s_wire.push_back({type, std::vector<uint8_t>(p, p + len)});
    return true;
}

// ---- fake display -----------------------------------------------------------
struct FakeDisplay : IDisplay {
    std::string text, timeStr, menuH, menuI1, menuI2, popup, full1;
    uint8_t digit = 0, scroll = 0, icon = 0;
    bool state = false, aux = false;
    AffaKey lastKey = AffaKey::Load;
    bool lastHold = false;
    int keyCount = 0, calls = 0;

    void tick() override {}
    void recv(const Frame&) override {}
    void processEvents() override {}
    Err setText(const char* t, uint8_t d) override
    { text = t; digit = d; calls++; return Err::NoError; }
    Err setState(bool on) override { state = on; calls++; return Err::NoError; }
    Err setTime(const char* c) override { timeStr = c; calls++; return Err::NoError; }
    void ProcessKey(AffaKey k, bool h) override
    { lastKey = k; lastHold = h; keyCount++; calls++; }
    Err showMenu(const char* h, const char* i1, const char* i2, uint8_t s) override
    { menuH = h; menuI1 = i1; menuI2 = i2; scroll = s; calls++; return Err::NoError; }
    Err showPopupText(const char* t, uint8_t ic, uint8_t, uint8_t) override
    { popup = t; icon = ic; calls++; return Err::NoError; }
    Err showFullscreenText(const char* l1, const char*, const char*) override
    { full1 = l1; calls++; return Err::NoError; }
    void setAuxMode(bool on) override { aux = on; calls++; }
protected:
    void onKeyPressed(AffaKey, bool) override {}
};

void setUp(void) { s_wire.clear(); }
void tearDown(void) {}

// Feed every captured DISP_CMD into the server side.
static void replay(FakeDisplay& d)
{
    for (auto& f : s_wire)
        if (f.type == LinkProto::DISP_CMD)
            DispCmd::execute(d, f.payload.data(), (uint16_t)f.payload.size());
}

static void test_roundtrip_settext(void) {
    RemoteDisplay rd;
    rd.begin(captureSend, nullptr);
    TEST_ASSERT_TRUE(rd.setText("HELLO CAR", 3) == Err::NoError);
    FakeDisplay d;
    replay(d);
    TEST_ASSERT_EQUAL_STRING("HELLO CAR", d.text.c_str());
    TEST_ASSERT_EQUAL_UINT8(3, d.digit);
}

static void test_roundtrip_menu(void) {
    RemoteDisplay rd;
    rd.begin(captureSend, nullptr);
    rd.showMenu("Header", "Item One", "Item Two", 0x0C);
    FakeDisplay d;
    replay(d);
    TEST_ASSERT_EQUAL_STRING("Header", d.menuH.c_str());
    TEST_ASSERT_EQUAL_STRING("Item One", d.menuI1.c_str());
    TEST_ASSERT_EQUAL_STRING("Item Two", d.menuI2.c_str());
    TEST_ASSERT_EQUAL_HEX8(0x0C, d.scroll);
}

static void test_roundtrip_menu_empty_strings(void) {
    RemoteDisplay rd;
    rd.begin(captureSend, nullptr);
    rd.showMenu("H", nullptr, "", 0x00);
    FakeDisplay d;
    replay(d);
    TEST_ASSERT_EQUAL_STRING("H", d.menuH.c_str());
    TEST_ASSERT_EQUAL_STRING("", d.menuI1.c_str());
    TEST_ASSERT_EQUAL_STRING("", d.menuI2.c_str());
}

static void test_roundtrip_key_state_time_aux(void) {
    RemoteDisplay rd;
    rd.begin(captureSend, nullptr);
    rd.ProcessKey(AffaKey::RollUp, true);
    rd.setState(true);
    rd.setTime("1234");
    rd.setAuxMode(true);
    FakeDisplay d;
    replay(d);
    TEST_ASSERT_TRUE(d.lastKey == AffaKey::RollUp);
    TEST_ASSERT_TRUE(d.lastHold);
    TEST_ASSERT_TRUE(d.state);
    TEST_ASSERT_EQUAL_STRING("1234", d.timeStr.c_str());
    TEST_ASSERT_TRUE(d.aux);
}

static void test_roundtrip_popup(void) {
    RemoteDisplay rd;
    rd.begin(captureSend, nullptr);
    rd.showPopupText("VOL 28", 0x09, 0xFF, 0x60);
    rd.showFullscreenText("LINE1", "L2", "L3");
    FakeDisplay d;
    replay(d);
    TEST_ASSERT_EQUAL_STRING("VOL 28", d.popup.c_str());
    TEST_ASSERT_EQUAL_HEX8(0x09, d.icon);
    TEST_ASSERT_EQUAL_STRING("LINE1", d.full1.c_str());
}

static void test_malformed_never_crashes(void) {
    FakeDisplay d;
    DispCmd::execute(d, nullptr, 0);
    uint8_t junk[] = { LinkProto::DO_SHOW_MENU };          // op, no args
    DispCmd::execute(d, junk, 1);
    uint8_t junk2[] = { LinkProto::DO_SHOW_MENU, 0x0B, 'A' };  // unterminated
    DispCmd::execute(d, junk2, sizeof(junk2));
    // Unterminated strings are corrupt (the sender always NUL-terminates) and
    // yield empty — never a read past the buffer.
    TEST_ASSERT_EQUAL_STRING("", d.menuH.c_str());
    uint8_t unknown[] = { 0xEE, 1, 2, 3 };
    DispCmd::execute(d, unknown, sizeof(unknown));         // skipped silently
}

// ---- media: RemoteDisplay diff-push -> HuLinkMediaSource --------------------

static void replayMedia(HuLinkMediaSource& hu, uint32_t nowMs)
{
    for (auto& f : s_wire)
        if (f.type == LinkProto::MEDIA_TEXT && !f.payload.empty())
            hu.onMediaText(f.payload[0], (const char*)f.payload.data() + 1,
                           (uint16_t)(f.payload.size() - 1), nowMs);
}

static void test_media_roundtrip(void) {
    RemoteDisplay rd;
    rd.begin(captureSend, nullptr);
    MediaInfo m;
    m.title = "Track";
    m.artist = "Artist";
    m.playerName = "Spotify";
    m.playbackState = MediaInfo::PlaybackState::Playing;
    rd.pushMedia(m);

    HuLinkMediaSource hu;
    hu.setLinkUp(true);
    replayMedia(hu, 1000);
    hu.setNow(1500);
    TEST_ASSERT_EQUAL_STRING("Track", hu.current().title.c_str());
    TEST_ASSERT_EQUAL_STRING("Artist", hu.current().artist.c_str());
    TEST_ASSERT_EQUAL_STRING("Spotify", hu.current().playerName.c_str());
    TEST_ASSERT_TRUE(hu.current().playing());
    TEST_ASSERT_TRUE(hu.active());
}

static void test_media_diff_only_sends_changes(void) {
    RemoteDisplay rd;
    rd.begin(captureSend, nullptr);
    MediaInfo m;
    m.title = "Track";
    m.playbackState = MediaInfo::PlaybackState::Playing;
    rd.pushMedia(m);                       // first push: all 5 fields
    size_t first = s_wire.size();
    TEST_ASSERT_EQUAL_INT(5, first);
    m.title = "Track 2";                   // only the title changed
    rd.pushMedia(m);
    TEST_ASSERT_EQUAL_INT(first + 1, s_wire.size());
    TEST_ASSERT_EQUAL_UINT8(LinkProto::MF_TITLE, s_wire.back().payload[0]);
}

static void test_media_stale_goes_inactive(void) {
    HuLinkMediaSource hu;
    hu.setLinkUp(true);
    hu.onMediaText(LinkProto::MF_TITLE, "T", 1, 1000);
    hu.setNow(2000);
    TEST_ASSERT_TRUE(hu.active());
    hu.setNow(1000 + HuLinkMediaSource::STALE_MS + 1);
    TEST_ASSERT_FALSE(hu.active());        // GW silent -> auto falls back / status
    hu.setLinkUp(false);
    hu.setNow(2000);
    TEST_ASSERT_FALSE(hu.active());        // link down beats fresh text
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_roundtrip_settext);
    RUN_TEST(test_roundtrip_menu);
    RUN_TEST(test_roundtrip_menu_empty_strings);
    RUN_TEST(test_roundtrip_key_state_time_aux);
    RUN_TEST(test_roundtrip_popup);
    RUN_TEST(test_malformed_never_crashes);
    RUN_TEST(test_media_roundtrip);
    RUN_TEST(test_media_diff_only_sends_changes);
    RUN_TEST(test_media_stale_goes_inactive);
    return UNITY_END();
}
