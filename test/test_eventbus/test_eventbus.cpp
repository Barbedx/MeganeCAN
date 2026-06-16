// EventBus: static, heap-free, typed fan-out. Verify subscribe/publish, ctx routing,
// and the compile-time-bounded capacity.
#include <unity.h>
#include "core/EventBus.h"

struct Counter { int n = 0; Event last = Event::Count; };
static void onEvt(Event e, void* ctx) {
    Counter* c = (Counter*)ctx;
    c->n++;
    c->last = e;
}

void setUp(void) { EventBus::reset(); }
void tearDown(void) {}

static void test_publish_fanout(void) {
    Counter a, b;
    TEST_ASSERT_TRUE(EventBus::subscribe(onEvt, &a));
    TEST_ASSERT_TRUE(EventBus::subscribe(onEvt, &b));
    TEST_ASSERT_EQUAL_INT(2, EventBus::subscriberCount());

    EventBus::publish(Event::BtConnected);
    TEST_ASSERT_EQUAL_INT(1, a.n);
    TEST_ASSERT_EQUAL_INT(1, b.n);
    TEST_ASSERT_EQUAL_INT((int)Event::BtConnected, (int)a.last);

    EventBus::publish(Event::AuxOn);
    TEST_ASSERT_EQUAL_INT(2, a.n);
    TEST_ASSERT_EQUAL_INT((int)Event::AuxOn, (int)b.last);
}

static void test_no_subscribers_is_safe(void) {
    EventBus::publish(Event::ScreenChanged);   // must not crash with zero subs
    TEST_ASSERT_EQUAL_INT(0, EventBus::subscriberCount());
}

static void test_capacity_bounded(void) {
    Counter c;
    for (int i = 0; i < EventBus::MAX_SUBS; i++)
        TEST_ASSERT_TRUE(EventBus::subscribe(onEvt, &c));
    TEST_ASSERT_FALSE(EventBus::subscribe(onEvt, &c));   // full -> rejected, no heap growth
    TEST_ASSERT_FALSE(EventBus::subscribe(nullptr, &c)); // null handler rejected
    TEST_ASSERT_EQUAL_INT(EventBus::MAX_SUBS, EventBus::subscriberCount());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_publish_fanout);
    RUN_TEST(test_no_subscribers_is_safe);
    RUN_TEST(test_capacity_bounded);
    return UNITY_END();
}
