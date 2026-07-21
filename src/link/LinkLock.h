#pragma once

// Tiny critical-section seam for LinkPort's TX queue. On target it is a
// FreeRTOS spinlock (portMUX): producers live on several tasks (CAN-callback
// task pushes keys, NimBLE host task pushes media, httpd task pushes OTA
// handshakes) while the loop task drains — every queue-structure mutation must
// be inside the lock. Held only for slot bookkeeping (never encoding or I/O),
// so sections are a few microseconds. On the native host the tests are
// single-threaded and it compiles to nothing.
#ifdef NATIVE
struct LinkLock {
    void lock() {}
    void unlock() {}
};
#else
#include <Arduino.h>
struct LinkLock {
    portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
    void lock() { portENTER_CRITICAL(&mux); }
    void unlock() { portEXIT_CRITICAL(&mux); }
};
#endif
