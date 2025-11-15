#pragma once

#include "IDisplay.h"
#include "AffaCommonConstants.h"
#include <functional>   
#include <Arduino.h>   


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
    using KeyHandler = std::function<bool(AffaCommon::AffaKey, bool)>;
    void setMediaInfo();
    void setKeyHandler(KeyHandler handler) {
        keyHandler = std::move(handler);
    }
    // by default – нічого не робить, не всі дисплеї зобов’язані підтримувати AMS
    virtual void setMediaInfo(const AppleMediaService::MediaInformation& info) {
        (void)info;
    }
    virtual void tickMedia(){

    }

protected: 
    static constexpr int SYNC_TIMEOUT = 5;
    KeyHandler keyHandler = nullptr;
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