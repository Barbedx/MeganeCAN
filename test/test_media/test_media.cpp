// P0 media-layer tests: MediaInfo elapsed extrapolation + MediaRouter source
// selection. Pure host logic — fake sources, no Arduino.
#include <unity.h>
#include "media/MediaRouter.h"

// ---- fake source -----------------------------------------------------------
struct FakeSource : IMediaSource {
    MediaInfo info;
    bool isActive = false;
    const char* srcName;
    explicit FakeSource(const char* n) : srcName(n) {}
    const MediaInfo& current() const override { return info; }
    bool active() const override { return isActive; }
    const char* statusText() const override { return "fake status"; }
    const char* name() const override { return srcName; }
    void fireChange() { notifyChange(); }
};

void setUp(void) {}
void tearDown(void) {}

// ---- MediaInfo::elapsedAt --------------------------------------------------

// Paused: elapsed never advances, whatever the clock does.
static void test_elapsed_paused(void) {
    MediaInfo m;
    m.playbackState = MediaInfo::PlaybackState::Paused;
    m.elapsedTime = 42.0f;
    m.playbackRate = 1.0f;
    m.lastUpdateMs = 1000;
    TEST_ASSERT_EQUAL_FLOAT(42.0f, m.elapsedAt(99000));
}

// Playing at rate 1: advances by wall-clock dt since the last source update.
static void test_elapsed_playing(void) {
    MediaInfo m;
    m.playbackState = MediaInfo::PlaybackState::Playing;
    m.elapsedTime = 10.0f;
    m.playbackRate = 1.0f;
    m.lastUpdateMs = 5000;
    TEST_ASSERT_EQUAL_FLOAT(13.5f, m.elapsedAt(8500));
}

// Playback rate scales the extrapolation (2x fast-forwardish playback).
static void test_elapsed_rate_scaled(void) {
    MediaInfo m;
    m.playbackState = MediaInfo::PlaybackState::Playing;
    m.elapsedTime = 10.0f;
    m.playbackRate = 2.0f;
    m.lastUpdateMs = 1000;
    TEST_ASSERT_EQUAL_FLOAT(14.0f, m.elapsedAt(3000));
}

// No update ever received (lastUpdateMs 0): no extrapolation, return the base.
static void test_elapsed_never_updated(void) {
    MediaInfo m;
    m.playbackState = MediaInfo::PlaybackState::Playing;
    m.elapsedTime = 7.0f;
    m.playbackRate = 1.0f;
    m.lastUpdateMs = 0;
    TEST_ASSERT_EQUAL_FLOAT(7.0f, m.elapsedAt(123456));
}

// ---- MediaRouter -----------------------------------------------------------

// Mode Ams selects the AMS source even when it is inactive (it still renders
// its "waiting for phone" status screen).
static void test_router_ams_mode(void) {
    FakeSource ams("ams"), hu("hu");
    hu.isActive = true;
    MediaRouter r;
    r.setSources(&ams, &hu);
    r.setMode(MediaRouter::Mode::Ams);
    TEST_ASSERT_EQUAL_PTR(&ams, r.activeSource());
    TEST_ASSERT_FALSE(r.active());
    ams.isActive = true;
    TEST_ASSERT_TRUE(r.active());
    TEST_ASSERT_EQUAL_STRING("ams", r.sourceName());
}

// Mode Hu pins the HU source.
static void test_router_hu_mode(void) {
    FakeSource ams("ams"), hu("hu");
    ams.isActive = true;
    MediaRouter r;
    r.setSources(&ams, &hu);
    r.setMode(MediaRouter::Mode::Hu);
    TEST_ASSERT_EQUAL_PTR(&hu, r.activeSource());
}

// Auto: the HU wins while its link is live, else fall back to AMS.
static void test_router_auto_fallback(void) {
    FakeSource ams("ams"), hu("hu");
    MediaRouter r;
    r.setSources(&ams, &hu);
    r.setMode(MediaRouter::Mode::Auto);
    TEST_ASSERT_EQUAL_PTR(&ams, r.activeSource());  // HU down -> AMS
    hu.isActive = true;
    TEST_ASSERT_EQUAL_PTR(&hu, r.activeSource());   // HU up -> HU
    hu.isActive = false;
    TEST_ASSERT_EQUAL_PTR(&ams, r.activeSource());  // HU lost -> back to AMS
}

// Auto with no HU registered at all (P0 reality) always picks AMS.
static void test_router_auto_no_hu(void) {
    FakeSource ams("ams");
    MediaRouter r;
    r.setSources(&ams, nullptr);
    r.setMode(MediaRouter::Mode::Auto);
    TEST_ASSERT_EQUAL_PTR(&ams, r.activeSource());
}

// current() on an empty router returns the static empty info, not a crash.
static void test_router_empty(void) {
    MediaRouter r;
    r.setMode(MediaRouter::Mode::Hu);   // hu slot never registered
    TEST_ASSERT_NULL(r.activeSource());
    TEST_ASSERT_FALSE(r.active());
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)r.current().lastUpdateMs);
    TEST_ASSERT_EQUAL_STRING("no media source", r.statusText());
}

// Mode string parsing matches the NVS values.
static void test_router_mode_from_str(void) {
    TEST_ASSERT_TRUE(MediaRouter::modeFromStr("ams")  == MediaRouter::Mode::Ams);
    TEST_ASSERT_TRUE(MediaRouter::modeFromStr("hu")   == MediaRouter::Mode::Hu);
    TEST_ASSERT_TRUE(MediaRouter::modeFromStr("auto") == MediaRouter::Mode::Auto);
    TEST_ASSERT_TRUE(MediaRouter::modeFromStr("junk") == MediaRouter::Mode::Ams);
    TEST_ASSERT_TRUE(MediaRouter::modeFromStr(nullptr) == MediaRouter::Mode::Ams);
}

// The change callback fires with the source that changed.
static IMediaSource* s_changed = nullptr;
static void onChange(IMediaSource& s, void*) { s_changed = &s; }
static void test_source_change_callback(void) {
    FakeSource ams("ams");
    s_changed = nullptr;
    ams.setChangeCallback(onChange, nullptr);
    ams.info.title = "Song";
    ams.fireChange();
    TEST_ASSERT_EQUAL_PTR(&ams, s_changed);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_elapsed_paused);
    RUN_TEST(test_elapsed_playing);
    RUN_TEST(test_elapsed_rate_scaled);
    RUN_TEST(test_elapsed_never_updated);
    RUN_TEST(test_router_ams_mode);
    RUN_TEST(test_router_hu_mode);
    RUN_TEST(test_router_auto_fallback);
    RUN_TEST(test_router_auto_no_hu);
    RUN_TEST(test_router_empty);
    RUN_TEST(test_router_mode_from_str);
    RUN_TEST(test_source_change_callback);
    return UNITY_END();
}
