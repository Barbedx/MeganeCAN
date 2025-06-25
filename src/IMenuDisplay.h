

#pragma once

#ifdef HAS_MENU
class IMenuDisplay {
public:
    virtual void setMenu(const char* title, const char* items[], uint8_t count) = 0;
};
#endif