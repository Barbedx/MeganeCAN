#pragma once

#include "IDisplay.h"
#include "AffaCommonConstants.h"

class AffaDisplayBase : public IDisplay
{
protected:
    AffaCommon::SyncStatus _sync_status = AffaCommon::SyncStatus::FAILED;

    struct Affa3Func {
        uint16_t id;
        AffaCommon::FuncStatus stat;
    };

    size_t funcsMax = 0;
    Affa3Func* funcs = nullptr;

    virtual void initializeFuncs() = 0;
    AffaCommon::AffaError affa3_do_send(uint8_t idx, uint8_t *data, uint8_t len);
    AffaCommon::AffaError affa3_send(uint16_t id, uint8_t *data, uint8_t len);
    virtual uint8_t getPacketFiller() const = 0;//This means every derived class must implement this method.
    

public:
    AffaDisplayBase() { initializeFuncs(); }
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