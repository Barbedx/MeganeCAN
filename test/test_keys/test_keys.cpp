// P0 key-router tests: per-class sink masks, legacy hold-gating, and the
// bt_mode migration defaults that must reproduce the old HandleKey exactly.
#include <unity.h>
#include "keys/KeyRouter.h"

using AffaCommon::AffaKey;

// ---- fake sink -------------------------------------------------------------
struct FakeSink : IKeySink {
    AffaKey lastKey = AffaKey::Load;
    bool lastHold = false;
    int count = 0;
    void onKey(AffaKey key, bool isHold) override {
        lastKey = key; lastHold = isHold; count++;
    }
};

void setUp(void) {}
void tearDown(void) {}

// pack/unpack round-trips every field.
static void test_pack_roundtrip(void) {
    uint8_t p = KeyRouter::pack(KeyRouter::SINK_AMS, KeyRouter::SINK_HID, true);
    uint8_t t, v; bool h;
    KeyRouter::unpack(p, t, v, h);
    TEST_ASSERT_EQUAL_UINT8(KeyRouter::SINK_AMS, t);
    TEST_ASSERT_EQUAL_UINT8(KeyRouter::SINK_HID, v);
    TEST_ASSERT_TRUE(h);
}

// Migration defaults mirror the three legacy bt_mode behaviors.
static void test_defaults_for_bt_mode(void) {
    uint8_t t, v; bool h;

    KeyRouter::unpack(KeyRouter::defaultsForBtMode("ams"), t, v, h);
    TEST_ASSERT_EQUAL_UINT8(KeyRouter::SINK_AMS, t);
    TEST_ASSERT_EQUAL_UINT8(KeyRouter::SINK_AMS, v);
    TEST_ASSERT_TRUE(h);

    KeyRouter::unpack(KeyRouter::defaultsForBtMode("keyboard"), t, v, h);
    TEST_ASSERT_EQUAL_UINT8(KeyRouter::SINK_HID, t);
    TEST_ASSERT_EQUAL_UINT8(KeyRouter::SINK_HID, v);
    TEST_ASSERT_TRUE(h);

    KeyRouter::unpack(KeyRouter::defaultsForBtMode("both"), t, v, h);
    TEST_ASSERT_EQUAL_UINT8(KeyRouter::SINK_AMS, t);
    TEST_ASSERT_EQUAL_UINT8(KeyRouter::SINK_HID, v);
    TEST_ASSERT_FALSE(h);

    // Unknown/null fall back to the "ams" profile.
    TEST_ASSERT_EQUAL_UINT8(KeyRouter::defaultsForBtMode("ams"),
                            KeyRouter::defaultsForBtMode(nullptr));
}

// Transport keys reach the transport sink immediately, hold or not.
static void test_transport_routing(void) {
    FakeSink ams, hid;
    KeyRouter r;
    r.bind(KeyRouter::SINK_AMS, &ams);
    r.bind(KeyRouter::SINK_HID, &hid);
    r.configure(KeyRouter::defaultsForBtMode("both"));   // transport -> AMS only

    TEST_ASSERT_TRUE(r.route(AffaKey::Pause, false));
    TEST_ASSERT_EQUAL_INT(1, ams.count);
    TEST_ASSERT_EQUAL_INT(0, hid.count);
    TEST_ASSERT_TRUE(ams.lastKey == AffaKey::Pause);

    r.route(AffaKey::RollUp, true);
    TEST_ASSERT_EQUAL_INT(2, ams.count);
    TEST_ASSERT_TRUE(ams.lastHold);
}

// "both" profile: volume goes to HID immediately, both directions, never to AMS.
static void test_both_volume_immediate(void) {
    FakeSink ams, hid;
    KeyRouter r;
    r.bind(KeyRouter::SINK_AMS, &ams);
    r.bind(KeyRouter::SINK_HID, &hid);
    r.configure(KeyRouter::defaultsForBtMode("both"));

    r.route(AffaKey::VolumeUp, false);
    r.route(AffaKey::VolumeDown, false);
    TEST_ASSERT_EQUAL_INT(2, hid.count);
    TEST_ASSERT_EQUAL_INT(0, ams.count);
    TEST_ASSERT_TRUE(hid.lastKey == AffaKey::VolumeDown);
}

// Legacy hold-gate ("ams"/"keyboard"): VolumeUp only on hold, VolumeDown never.
static void test_hold_gated_volume(void) {
    FakeSink ams;
    KeyRouter r;
    r.bind(KeyRouter::SINK_AMS, &ams);
    r.configure(KeyRouter::defaultsForBtMode("ams"));

    TEST_ASSERT_TRUE(r.route(AffaKey::VolumeUp, false));  // short press -> dropped
    TEST_ASSERT_EQUAL_INT(0, ams.count);
    r.route(AffaKey::VolumeDown, true);                   // down -> dropped even held
    TEST_ASSERT_EQUAL_INT(0, ams.count);
    r.route(AffaKey::VolumeUp, true);                     // hold -> forwarded
    TEST_ASSERT_EQUAL_INT(1, ams.count);
    TEST_ASSERT_TRUE(ams.lastKey == AffaKey::VolumeUp);
    TEST_ASSERT_TRUE(ams.lastHold);
}

// Multiple sinks on one class fan out (future: AMS + HU-UART mirroring).
static void test_multi_sink_fanout(void) {
    FakeSink ams, hid;
    KeyRouter r;
    r.bind(KeyRouter::SINK_AMS, &ams);
    r.bind(KeyRouter::SINK_HID, &hid);
    r.configure(KeyRouter::pack(KeyRouter::SINK_AMS | KeyRouter::SINK_HID,
                                0, false));
    r.route(AffaKey::Pause, false);
    TEST_ASSERT_EQUAL_INT(1, ams.count);
    TEST_ASSERT_EQUAL_INT(1, hid.count);
}

// An unbound sink bit in the mask is skipped safely (HU configured before P1).
static void test_unbound_sink_safe(void) {
    FakeSink ams;
    KeyRouter r;
    r.bind(KeyRouter::SINK_AMS, &ams);
    r.configure(KeyRouter::pack(KeyRouter::SINK_AMS | KeyRouter::SINK_HU,
                                KeyRouter::SINK_HU, false));
    TEST_ASSERT_TRUE(r.route(AffaKey::Pause, false));   // HU unbound -> no crash
    TEST_ASSERT_EQUAL_INT(1, ams.count);
    TEST_ASSERT_TRUE(r.route(AffaKey::VolumeUp, false));
    TEST_ASSERT_EQUAL_INT(1, ams.count);                // volume mask has no AMS
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_pack_roundtrip);
    RUN_TEST(test_defaults_for_bt_mode);
    RUN_TEST(test_transport_routing);
    RUN_TEST(test_both_volume_immediate);
    RUN_TEST(test_hold_gated_volume);
    RUN_TEST(test_multi_sink_fanout);
    RUN_TEST(test_unbound_sink_safe);
    return UNITY_END();
}
