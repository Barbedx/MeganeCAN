#pragma once

// Minimal seam for enabling/disabling the real CAN transmit, independent of the bus
// implementation. HwCanBus implements it; DisplayTransport drives it (VIRTUAL_ONLY
// drops TX). Kept tiny + dependency-free so DisplayTransport stays host-testable.
struct ITxGate {
    virtual ~ITxGate() = default;
    virtual void setTxEnabled(bool on) = 0;
};
