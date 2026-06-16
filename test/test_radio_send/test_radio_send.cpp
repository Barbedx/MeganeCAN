// Host test of the RADIO send path (AffaDisplayBase::affa3_send / affa3_do_send) — the
// AFFA3 ISO-TP fragmentation + per-frame ACK loop — now possible because the display
// port speaks Frame and the send goes through an injected ICanBus. Previously this
// could only be checked on the bench.
#include <unity.h>
#include <string.h>
#include "display/AffaDisplayBase.h"
#include "bus/LoopbackCanBus.h"
#include "test/FakeClock.h"

// Minimal concrete radio: one function (0x151), Carminat filler, everything else stubbed.
class TestRadio : public AffaDisplayBase {
public:
    TestRadio() { initializeFuncs(); }
    void initializeFuncs() override {
        funcs = new Affa3Func[1];
        funcs[0].id = 0x151;
        funcs[0].stat = AffaCommon::FuncStatus::IDLE;
        funcsMax = 1;
    }
    uint8_t getPacketFiller() const override { return 0x00; }
    void ProcessKey(AffaCommon::AffaKey, bool) override {}
    void tick() override {}
    void recv(const Frame&) override {}
    void processEvents() override {}
    AffaCommon::AffaError setText(const char*, uint8_t) override { return AffaCommon::AffaError::NoError; }
    AffaCommon::AffaError setState(bool) override { return AffaCommon::AffaError::NoError; }
    AffaCommon::AffaError setTime(const char*) override { return AffaCommon::AffaError::NoError; }
    AffaCommon::AffaError showMenu(const char*, const char*, const char*, uint8_t) override { return AffaCommon::AffaError::NoError; }
    // Public test entry to the protected sender.
    AffaCommon::AffaError sendData(uint16_t id, uint8_t* d, uint8_t len) { return affa3_send(id, d, len); }
protected:
    void onKeyPressed(AffaCommon::AffaKey, bool) override {}
};

struct Cap { Frame f[32]; int n = 0; };
static Cap g_cap;
static void onTx(const Frame& f, void* ctx) {
    Cap* c = (Cap*)ctx;
    if (c->n < 32) c->f[c->n++] = f;
}

void setUp(void) { g_cap.n = 0; }
void tearDown(void) {}

// A 96-byte showMenu-shaped payload fragments into the exact 14-frame ISO-TP sequence
// the bench radio emits: frame0 = 8 raw bytes (10 5A ...), then 0x2N continuations.
static void test_radio_isotp_send(void) {
    LoopbackCanBus bus;
    FakeClock clk;
    bus.onReceive(onTx, &g_cap);

    TestRadio radio;
    radio.setBus(bus);
    radio.setClock(clk);
    radio.setSkipFuncReg(true);   // no registration handshake
    radio.setEmuSelfAck(true);    // self-ACK -> the wait loop exits, full sequence emits

    uint8_t payload[96];
    memset(payload, 0, sizeof(payload));
    payload[0] = 0x10; payload[1] = 0x5A;        // ISO-TP first-frame header
    payload[11] = 'H'; payload[12] = 'i';        // some header text

    AffaCommon::AffaError err = radio.sendData(0x151, payload, 96);
    TEST_ASSERT_EQUAL_INT((int)AffaCommon::AffaError::NoError, (int)err);

    bus.poll();
    TEST_ASSERT_EQUAL_INT(14, g_cap.n);                       // 96B -> 14 frames
    TEST_ASSERT_EQUAL_HEX32(0x151, g_cap.f[0].id);
    TEST_ASSERT_EQUAL_HEX8(0x10, g_cap.f[0].data[0]);         // frame0: no PCI prefix
    TEST_ASSERT_EQUAL_HEX8(0x5A, g_cap.f[0].data[1]);
    TEST_ASSERT_EQUAL_HEX8(0x21, g_cap.f[1].data[0]);         // continuation 0x20+1
    TEST_ASSERT_EQUAL_HEX8(0x2D, g_cap.f[13].data[0]);        // last continuation 0x20+13
    // padding uses the protocol filler (0x00)
    TEST_ASSERT_EQUAL_HEX8(0x00, g_cap.f[13].data[7]);
}

// Without a bus set, send is a no-op (nullptr-safe) and returns cleanly via self-ACK.
static void test_radio_no_bus_safe(void) {
    FakeClock clk;
    TestRadio radio;
    radio.setClock(clk);
    radio.setSkipFuncReg(true);
    radio.setEmuSelfAck(true);
    uint8_t p[8] = {0x10, 0x02, 0, 0, 0, 0, 0, 0};
    AffaCommon::AffaError err = radio.sendData(0x151, p, 8);
    TEST_ASSERT_EQUAL_INT((int)AffaCommon::AffaError::NoError, (int)err);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_radio_isotp_send);
    RUN_TEST(test_radio_no_bus_safe);
    return UNITY_END();
}
