#pragma once
#include "../AffaCommonConstants.h"

class IPage {
public:
    virtual ~IPage() = default;
    virtual void onEnter()  = 0;
    virtual void onExit()   = 0;
    virtual void onTick()   = 0;
    virtual void handleKey(AffaCommon::AffaKey key, bool isHold) = 0;
};
