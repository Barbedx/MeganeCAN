#pragma once

#include "IDisplay.h"
#include "AffaCommonConstants.h"

class AffaDisplayBase : public IDisplay
{
protected:
    using SyncStatus = AffaCommon::SyncStatus;
    using FuncStatus = AffaCommon::FuncStatus;
    using AffaError  = AffaCommon::AffaError;
    static constexpr int SYNC_TIMEOUT = 5;
    
    SyncStatus _sync_status = SyncStatus::FAILED;
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
    

public:
    AffaDisplayBase() = default; // ✅ Allow default constructor
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