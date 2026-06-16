#include "HwCanBus.h"
#include <Arduino.h>

HwCanBus& HwCanBus::instance()
{
    static HwCanBus bus;
    return bus;
}

void HwCanBus::addTap(IBusTap* t)
{
    if (t && _tapCount < MAX_TAPS)
        _taps[_tapCount++] = t;
}

void HwCanBus::noteRxActivity()
{
    _lastRxMs = millis();
}

bool HwCanBus::isLive() const
{
    return _lastRxMs != 0 && (millis() - _lastRxMs) < BUS_ALIVE_WINDOW_MS;
}

bool HwCanBus::send(const Frame& f)
{
    // TX taps (the @TX serial mirror, the virtual-display feed) run BEFORE any gate,
    // so observation + the virtual display capture the frame even when the real CAN
    // transmit is suppressed (bench with no transceiver, or the VIRTUAL_ONLY route).
    for (int i = 0; i < _tapCount; i++)
        _taps[i]->onTx(f);

    // Route gate: VIRTUAL_ONLY drops the real CAN transmit (the frame already reached
    // the twin via the tap above). Distinct from the busAlive gate below.
    if (!_txEnabled)
        return false;

    // Only transmit onto a confirmed-live bus. No RX traffic (e.g. the bench board
    // with no transceiver) -> drop the frame so TX never drives the controller
    // BUS_OFF and trips the IDF auto-recovery assert (twai.c:184) -> reboot loop.
    // On the car bus, frames arrive constantly so this is open within milliseconds.
    if (!isLive())
    {
        static uint32_t lastWarnMs = 0;
        if (millis() - lastWarnMs > 5000)
        {
            lastWarnMs = millis();
            Serial.println("[CAN] no live bus (no RX) — TX suppressed");
        }
        return false;
    }

    CAN_FRAME out;
    out.id = f.id;
    out.extended = f.extended;
    out.rtr = false;
    out.length = f.len;
    for (int i = 0; i < f.len && i < 8; i++)
        out.data.uint8[i] = f.data[i];
    CAN0.sendFrame(out);
    return true;
}

void HwCanBus::ingest(const CAN_FRAME& f)
{
    noteRxActivity(); // confirms a live bus -> unlocks TX

    Frame fr;
    fr.id = f.id;
    fr.extended = f.extended;
    fr.len = f.length > 8 ? 8 : f.length;
    for (int i = 0; i < fr.len; i++)
        fr.data[i] = f.data.uint8[i];

    for (int i = 0; i < _tapCount; i++)
        _taps[i]->onRx(fr);

    if (_rx)
        _rx(fr, _rxCtx);
}
