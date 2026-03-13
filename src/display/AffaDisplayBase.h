#pragma once

#include "AffaCommonConstants.h"
#include "IDisplay.h"

#include <Arduino.h>

namespace AppleMediaService
{
    struct MediaInformation;
}

using SyncStatus = AffaCommon::SyncStatus;
using FuncStatus = AffaCommon::FuncStatus;
using AffaError = AffaCommon::AffaError;
using KeyHandler = bool (*)(AffaCommon::AffaKey, bool);

class AffaDisplayBase : public IDisplay
{
private:
    static constexpr uint32_t DEFAULT_SYNC_INTERVAL_MS = 1000;
    static constexpr uint32_t DEFAULT_PEER_TIMEOUT_MS = DEFAULT_SYNC_INTERVAL_MS * 5;
    static constexpr uint8_t AFFA3_MAX_TX_DATA = 128;
    static constexpr uint32_t AFFA3_ACK_TIMEOUT_MS = 2000;

    struct Affa3TxState
    {
        bool active = false;
        bool waitingAck = false;
        bool registrationPhase = false;

        uint8_t currentFuncIdx = 0;
        uint8_t targetFuncIdx = 0;
        uint8_t seqNum = 0;
        uint8_t dataLen = 0;
        uint8_t dataOffset = 0;

        uint32_t ackDeadlineMs = 0;
        AffaError result = AffaError::NoError;
        uint8_t data[AFFA3_MAX_TX_DATA]{};
    };

    Affa3TxState _tx;
    uint32_t _lastSyncTickMs = 0;
    uint32_t _lastPeerAliveMs = 0;

    bool affa3_start_tx(uint8_t targetFuncIdx, const uint8_t *data, uint8_t len);
    bool affa3_queue_next_packet();
    void affa3_finish_tx(AffaError result);
    void tickTx();
    void tickSync(uint32_t now);

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

    AffaDisplayBase(const AffaDisplayBase &) = delete;
    AffaDisplayBase &operator=(const AffaDisplayBase &) = delete;

    void setKeyHandler(KeyHandler handler)
    {
        keyHandler = handler;
        Serial.println("[AffaDisplayBase] setKeyHandler done");
    }

    virtual void begin() {}
    void tick() override;

    virtual void setMediaInfo(const AppleMediaService::MediaInformation &info)
    {
        (void)info;
    }

    virtual void tickMedia() {}
    virtual void onElmUpdate(const char *key, float value)
    {
        (void)key;
        (void)value;
    }
    virtual void ProcessKey(AffaCommon::AffaKey key, bool isHold) = 0;
    virtual void processEvents() override {}

    void setSkipFuncReg(bool skip) { _skipFuncReg = skip; }
    void serviceTx() override { tickTx(); }
    bool isTxBusy() const override { return _tx.active; }
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
    virtual void sendAliveFrame() = 0;
    virtual void sendSyncRequestFrame() = 0;
    virtual bool shouldProactivelyRequestSync() const { return true; }
    virtual void onTick(uint32_t now) { (void)now; }

    void markPeerAlive(uint32_t now);
    void noteSyncRequest(bool startRequested, uint32_t now);
    void markSyncLost();
    void resetSyncSchedule();
    uint32_t getSyncIntervalMs() const { return DEFAULT_SYNC_INTERVAL_MS; }
    uint32_t getPeerTimeoutMs() const { return DEFAULT_PEER_TIMEOUT_MS; }

    AffaError affa3_send(uint16_t id, uint8_t *data, uint8_t len);
};
