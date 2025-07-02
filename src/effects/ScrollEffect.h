#pragma once 
#include "display/IDisplay.h"    

enum class ScrollDirection {
    Left,
    Right
};

inline void ScrollEffect(
    IDisplay* display,
    ScrollDirection direction,
    const char* text,
    uint16_t delayMs
) {
    size_t windowSize = 8; // Or display->getMaxChars()

    std::string str(text);

    if (direction == ScrollDirection::Left) {
        str += std::string(windowSize, ' ');
        for (size_t i = 0; i <= str.size() - windowSize; ++i) {
            display->setText(str.substr(i, windowSize).c_str());
            delay(delayMs);
        }
    } 
    else if (direction == ScrollDirection::Right) {
        str = std::string(windowSize, ' ') + str;
        for (size_t i = str.size() - windowSize; i != static_cast<size_t>(-1); --i) {
            display->setText(str.substr(i, windowSize).c_str());
            delay(delayMs);
            if (i == 0) break;
        }
    }

}
  