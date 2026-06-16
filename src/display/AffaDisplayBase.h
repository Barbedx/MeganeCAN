#pragma once

#include "IDisplay.h"
#include "AffaCommonConstants.h"
#include "../bus/IClock.h"
#include "../bus/ICanBus.h"
#ifndef NATIVE
#include <Arduino.h>
#endif


// forward-declare, щоб не тягнути весь apple_media_service.h сюди
namespace AppleMediaService {
    struct MediaInformation;
}

class AffaDisplayBase : public IDisplay
{
public:
    using SyncStatus = AffaCommon::SyncStatus;
    using FuncStatus = AffaCommon::FuncStatus;
    using AffaError  = AffaCommon::AffaError;
   // using KeyHandler = std::function<bool(AffaCommon::AffaKey, bool)>;
     using KeyHandler = bool (*)(AffaCommon::AffaKey, bool);
    //void setMediaInfo();
void setKeyHandler(KeyHandler handler)
{
#ifndef NATIVE
    Serial.print("[AffaDisplayBase] setKeyHandler = ");
    Serial.println(((uint32_t)keyHandler), HEX);
#endif
    keyHandler = handler;
}
    // by default – нічого не робить, не всі дисплеї зобов’язані підтримувати AMS
    virtual void setMediaInfo(const AppleMediaService::MediaInformation& info) {
        (void)info;
    }
    virtual void tickMedia() {}

    // Called by main loop when a new ELM value arrives (key = PID shortName, e.g. "PR071")
    virtual void onElmUpdate(const char* key, float value) { (void)key; (void)value; }
    virtual void ProcessKey(AffaCommon::AffaKey key, bool isHold) =0;

    // When true, skip function registration (FUNCSREG) handshake.
    // Set this when connected to a real radio — the radio handles auth itself.
    void setSkipFuncReg(bool skip) { _skipFuncReg = skip; }

    // Emulator self-ACK: with no real display on the bus (bench), the per-frame
    // ACK that affa3_do_send waits for never arrives, so only the first frame of
    // a multi-frame message goes out. When this is on, the sender acknowledges its
    // own frames (PARTIAL while bytes remain, DONE on the last) so the COMPLETE,
    // real AFFA3 frame sequence is emitted (@TX) for the PC-side display emulator
    // to decode. The wire frames are identical to a real send; only the external
    // ACK wait is skipped. Toggled over serial via "@EMU 1".
    void setEmuSelfAck(bool e) { _emuSelfAck = e; }

    // Time seam. Defaults to the Arduino clock (millis/delay) so on-target behavior
    // is unchanged; native tests inject a FakeClock to make the 2s per-frame ACK
    // wait in affa3_do_send instant + deterministic.
    void setClock(IClock& c) { _clock = &c; }

    // CAN send seam. When set, affa3_do_send transmits through this bus instead of
    // CanUtils (which itself delegates to HwCanBus, so binding HwCanBus here is
    // behavior-neutral). Lets the radio drive a LoopbackCanBus on the host / a pure
    // virtual-display loop without the real controller.
    void setBus(ICanBus& b) { _bus = &b; }

protected:
    static constexpr int SYNC_TIMEOUT = 5;
    IClock* _clock = nullptr;   // set via setClock (ArduinoClock on target, FakeClock in tests)
    ICanBus* _bus = nullptr;    // set via setBus (HwCanBus on target, Loopback in tests)
    KeyHandler keyHandler = nullptr;
    SyncStatus _sync_status = SyncStatus::FAILED;
    bool _skipFuncReg = false;
    bool _emuSelfAck = false;
    struct Affa3Func {
        uint16_t id;
        FuncStatus stat;
    };

    size_t funcsMax = 0;
    Affa3Func* funcs = nullptr;

    virtual void initializeFuncs() = 0;
    AffaError affa3_do_send(uint8_t idx, uint8_t *data, uint8_t len);
    AffaError affa3_send(uint16_t id, uint8_t *data, uint8_t len);
    virtual uint8_t getPacketFiller() const = 0;//This means every derived class must implement this method.
    // Optional init method — default no-op
    
    
    public:
    AffaDisplayBase() = default; // ✅ Allow default constructor
    virtual void begin() {}  // ✅ Safe default
    virtual ~AffaDisplayBase() {
        if (funcs) {
            delete[] funcs;
            funcs = nullptr;
        }
    }

    // Prevent copying to avoid double deletes
    AffaDisplayBase(const AffaDisplayBase&) = delete;
    AffaDisplayBase& operator=(const AffaDisplayBase&) = delete;
};