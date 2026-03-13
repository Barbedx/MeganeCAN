#pragma once

#include "IDisplay.h"
#include "AffaCommonConstants.h"
#include <Arduino.h>
#include <bluetooth/TrackInfo.h>

    using SyncStatus = AffaCommon::SyncStatus;
    using FuncStatus = AffaCommon::FuncStatus;
    using AffaError = AffaCommon::AffaError;
    
    using KeyHandler = bool (*)(AffaCommon::AffaKey, bool);

class AffaDisplayBase : public IDisplay
{
    private:

    static constexpr uint8_t AFFA3_MAX_TX_DATA = 32;
    static constexpr uint32_t AFFA3_ACK_TIMEOUT_MS = 2000;
     struct Affa3TxState
    {
        bool active = false;
        bool waitingAck = false;
        bool registrationPhase = false;

        uint8_t currentFuncIdx = 0;   // current func being sent/registered
        uint8_t targetFuncIdx = 0;    // final target func for real payload
        uint8_t seqNum = 0;           // multipart packet number
        uint8_t dataLen = 0;
        uint8_t dataOffset = 0;

        uint32_t ackDeadlineMs = 0;

        AffaError result = AffaError::NoError;

        uint8_t data[AFFA3_MAX_TX_DATA]{};
    };

    Affa3TxState _tx;
    bool affa3_start_tx(uint8_t targetFuncIdx, const uint8_t* data, uint8_t len);
    bool affa3_queue_next_packet();
    void affa3_finish_tx(AffaError result);
    void tickTx();

public:

    AffaDisplayBase() = default;  
    virtual ~AffaDisplayBase()
    {
        if (funcs)
        {
            delete[] funcs;
            funcs = nullptr;
        }
    }

    
    // Prevent copying to avoid double deletes
    AffaDisplayBase(const AffaDisplayBase &) = delete;
    AffaDisplayBase &operator=(const AffaDisplayBase &) = delete;

    void setKeyHandler(KeyHandler handler)
    {
        keyHandler = handler;
        Serial.print("[AffaDisplayBase] setKeyHandler done; ");

    }
    
    virtual void begin() {}     
    

    // by default – нічого не робить, не всі дисплеї зобов’язані підтримувати AMS
    virtual void setMediaInfo(const TrackInfo info)
    {
        (void)info;
    }

    virtual void tickMedia() {}

    // Called by main loop when a new ELM value arrives (key = PID shortName, e.g. "PR071") //todo to tick?
    virtual void onElmUpdate(const char *key, float value)
    {
        (void)key;
        (void)value;



    }
    virtual void ProcessKey(AffaCommon::AffaKey key, bool isHold) = 0;

    // When true, skip function registration (FUNCSREG) handshake.
    // Set this when connected to a real radio — the radio handles auth itself.
    void setSkipFuncReg(bool skip) { _skipFuncReg = skip; }
    
    void serviceTx() override { tickTx(); }
    
    bool isTxBusy() const { return _tx.active; }
    AffaError getLastTxResult() const { return _tx.result; }

protected:
    static constexpr int SYNC_TIMEOUT = 5;
    KeyHandler keyHandler = nullptr;
    SyncStatus _sync_status = SyncStatus::FAILED;
    bool _skipFuncReg = false;
    struct Affa3Func
    {
        uint16_t id;
        FuncStatus stat;
    };

    size_t funcsMax = 0;
    Affa3Func *funcs = nullptr;
  

    virtual void initializeFuncs() = 0;
    virtual uint8_t getPacketFiller() const = 0; 
    

    // async tx state machine
    // bool affa3_start_tx(uint8_t targetFuncIdx, const uint8_t* data, uint8_t len);
    // bool affa3_queue_next_packet();
    // void affa3_finish_tx(AffaError result);
    // void tickTx();

    // public API used by derived displays
    AffaError affa3_send(uint16_t id, uint8_t *data, uint8_t len);

    // AffaError affa3_do_send(uint8_t idx, uint8_t *data, uint8_t len);
    // AffaError affa3_send(uint16_t id, uint8_t *data, uint8_t len);
    // Optional init method — default no-op

public:

};