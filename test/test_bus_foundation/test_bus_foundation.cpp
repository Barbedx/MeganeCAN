// Phase 2 foundation tests: prove the host harness + the two pure-logic mocks
// (LoopbackCanBus, FakeClock) behave, with no hardware. Later phases bind a radio
// sender + a virtual display to a LoopbackCanBus and assert decoded screens.
#include <unity.h>
#include "bus/LoopbackCanBus.h"
#include "test/FakeClock.h"

// ---- RX capture helper (function-pointer + ctx, mirrors the real callback) ----
struct Capture {
    Frame last;
    int   count = 0;
    uint32_t ids[8] = {};
};
static void onRx(const Frame& f, void* ctx) {
    Capture* c = static_cast<Capture*>(ctx);
    if (c->count < 8) c->ids[c->count] = f.id;
    c->last = f;
    c->count++;
}

static Frame mk(uint32_t id, uint8_t b0 = 0) {
    Frame f;
    f.id = id;
    f.len = 8;
    f.data[0] = b0;
    return f;
}

void setUp(void) {}
void tearDown(void) {}

// A sent frame is delivered to the handler only after poll(), unchanged.
static void test_loopback_echo(void) {
    LoopbackCanBus bus;
    Capture cap;
    bus.onReceive(onRx, &cap);

    TEST_ASSERT_TRUE(bus.send(mk(0x151, 0x10)));
    TEST_ASSERT_EQUAL_INT(0, cap.count);     // not delivered until poll()
    TEST_ASSERT_EQUAL_INT(1, bus.pending());

    bus.poll();
    TEST_ASSERT_EQUAL_INT(1, cap.count);
    TEST_ASSERT_EQUAL_HEX32(0x151, cap.last.id);
    TEST_ASSERT_EQUAL_UINT8(0x10, cap.last.data[0]);
    TEST_ASSERT_EQUAL_INT(0, bus.pending());
}

// Frames are delivered FIFO in send order.
static void test_loopback_fifo_order(void) {
    LoopbackCanBus bus;
    Capture cap;
    bus.onReceive(onRx, &cap);

    bus.send(mk(0x3CF));
    bus.send(mk(0x151));
    bus.send(mk(0x1C1));
    bus.poll();

    TEST_ASSERT_EQUAL_INT(3, cap.count);
    TEST_ASSERT_EQUAL_HEX32(0x3CF, cap.ids[0]);
    TEST_ASSERT_EQUAL_HEX32(0x151, cap.ids[1]);
    TEST_ASSERT_EQUAL_HEX32(0x1C1, cap.ids[2]);
}

// isLive() is always true (the gate never blocks host loops).
static void test_loopback_is_live(void) {
    LoopbackCanBus bus;
    TEST_ASSERT_TRUE(bus.isLive());
}

// Overflowing the ring drops frames and reports the count; it never corrupts.
static void test_loopback_overflow_drops(void) {
    LoopbackCanBus bus;
    Capture cap;
    bus.onReceive(onRx, &cap);

    for (int i = 0; i < LoopbackCanBus::CAPACITY; i++)
        TEST_ASSERT_TRUE(bus.send(mk(0x100 + i)));
    TEST_ASSERT_FALSE(bus.send(mk(0x999)));   // full -> dropped
    TEST_ASSERT_EQUAL_INT(1, bus.dropped());

    bus.poll();
    TEST_ASSERT_EQUAL_INT(LoopbackCanBus::CAPACITY, cap.count);
}

// A handler that send()s back doesn't get its own reply delivered in the same poll()
// (snapshot semantics) — it lands on the next poll(). Guards against infinite loops.
static int s_pingpong = 0;
static LoopbackCanBus* s_bus = nullptr;
static void onRxReply(const Frame& f, void*) {
    s_pingpong++;
    if (f.id == 0x121 && s_pingpong < 100)   // reply once; bounded
        s_bus->send(mk(0x521));
}
static void test_loopback_reentrant_send(void) {
    LoopbackCanBus bus;
    s_bus = &bus;
    s_pingpong = 0;
    bus.onReceive(onRxReply, nullptr);

    bus.send(mk(0x121));
    bus.poll();                               // delivers 0x121 -> handler queues 0x521
    TEST_ASSERT_EQUAL_INT(1, s_pingpong);
    TEST_ASSERT_EQUAL_INT(1, bus.pending());  // the reply waits for next poll
    bus.poll();
    TEST_ASSERT_EQUAL_INT(2, s_pingpong);
}

// FakeClock only moves when advanced; delayMs jumps it (no real sleep).
static void test_fakeclock(void) {
    FakeClock clk;
    TEST_ASSERT_EQUAL_UINT32(0, clk.millis());
    clk.delayMs(1500);
    TEST_ASSERT_EQUAL_UINT32(1500, clk.millis());
    clk.advance(500);
    TEST_ASSERT_EQUAL_UINT32(2000, clk.millis());
    clk.reset();
    TEST_ASSERT_EQUAL_UINT32(0, clk.millis());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_loopback_echo);
    RUN_TEST(test_loopback_fifo_order);
    RUN_TEST(test_loopback_is_live);
    RUN_TEST(test_loopback_overflow_drops);
    RUN_TEST(test_loopback_reentrant_send);
    RUN_TEST(test_fakeclock);
    return UNITY_END();
}
