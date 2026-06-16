// DisplayTransport: one Route -> (CAN tx, twin feed, twin ACK) policy. Verifies each
// route sets the three knobs the way the firmware relies on, against a fake tx-gate +
// a real EmuBridge/twin.
#include <unity.h>
#include "emulation/DisplayTransport.h"
#include "vdisplay/CarminatVirtualDisplay.h"
#include "test/FakeClock.h"

struct FakeTxGate : ITxGate {
    bool tx = true;
    void setTxEnabled(bool on) override { tx = on; }
};

static void recvSink(const Frame&, void*) {}

static FakeClock clk;
void setUp(void) { clk.reset(); }
void tearDown(void) {}

static void test_routes(void) {
    CarminatVirtualDisplay vd;
    EmuBridge emu;
    emu.begin(vd, recvSink, nullptr, clk);   // binds twin (passive)
    FakeTxGate gate;
    DisplayTransport t(gate, emu);

    // CAN_ONLY: real bus transmits; twin idle (no feed, no ACK).
    t.setRoute(DisplayTransport::CAN_ONLY);
    TEST_ASSERT_TRUE(gate.tx);
    TEST_ASSERT_FALSE(emu.feeding());
    TEST_ASSERT_FALSE(emu.enabled());

    // VIRTUAL_ONLY: no CAN; twin fed + ACKs (it IS the panel).
    t.setRoute(DisplayTransport::VIRTUAL_ONLY);
    TEST_ASSERT_FALSE(gate.tx);
    TEST_ASSERT_TRUE(emu.feeding());
    TEST_ASSERT_TRUE(emu.enabled());

    // CAN_AND_VIRTUAL: CAN transmits; twin fed but passive (decode only, real panel ACKs).
    t.setRoute(DisplayTransport::CAN_AND_VIRTUAL);
    TEST_ASSERT_TRUE(gate.tx);
    TEST_ASSERT_TRUE(emu.feeding());
    TEST_ASSERT_FALSE(emu.enabled());

    TEST_ASSERT_EQUAL_INT(DisplayTransport::CAN_AND_VIRTUAL, t.route());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_routes);
    return UNITY_END();
}
