#pragma once
#include "Affa2Base.h"

// Affa2 full LED (menu-capable) display.
// Uses the same CAN protocol as Affa2Base.
// Distinct type so it can be selected via NVS display_type = "affa2menu"
// and extended independently of the 8-segment variant later.
class Affa2MenuDisplay : public Affa2Base
{
public:
    Affa2MenuDisplay() = default;
};
