#pragma once
#include "Frame.h"

// Passive observer of bus traffic. A tap never sends and never blocks — it just
// watches every frame that goes out (onTx) or comes in (onRx). Used for the @TX
// serial mirror today; CAN logging / wireless recording in later phases. Registered
// on a bus and fanned out to in send()/ingest().
struct IBusTap {
    virtual ~IBusTap() = default;
    virtual void onTx(const Frame& f) { (void)f; }
    virtual void onRx(const Frame& f) { (void)f; }
};
