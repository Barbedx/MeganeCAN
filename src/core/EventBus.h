#pragma once
#include <stdint.h>

// Lightweight static pub/sub for cross-feature signals (BT state, AUX, screen changed).
// Per the framework research: a TYPED, statically-sized observer — fixed subscriber
// slots, NO heap, NO std::function (a plain function pointer + ctx, matching the DI
// style). Use ONLY for genuine N×M fan-out; hot CAN frames stay on direct interface
// calls. Subscribers register at init and are not removed in practice.
enum class Event : uint8_t {
    BtConnected,
    BtDisconnected,
    AuxOn,
    AuxOff,
    ScreenChanged,
    Count
};

class EventBus {
public:
    using Handler = void (*)(Event e, void* ctx);
    static constexpr int MAX_SUBS = 8;

    // Returns false if the (compile-time-bounded) subscriber table is full.
    static bool subscribe(Handler h, void* ctx);
    static void publish(Event e);

    static int  subscriberCount();
    static void reset();   // tests only

private:
    struct Sub { Handler h; void* ctx; };
    static Sub _subs[MAX_SUBS];
    static int _n;
};
