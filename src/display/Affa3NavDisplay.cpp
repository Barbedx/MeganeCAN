#include "Affa3NavDisplay.h"

// You can copy code from Affa3Display.cpp as a starting point

void Affa3NavDisplay::tick() {
    // Extended NAV display logic
}

void Affa3NavDisplay::recv(CAN_FRAME *frame) {
    // NAV-specific message handling
}

Affa3::Affa3Error Affa3NavDisplay::setText(const char *text, uint8_t digit) {
    // Possibly allows longer or formatted text
    return Affa3::Affa3Error::NoError;
}
 

Affa3::Affa3Error Affa3NavDisplay::setState(bool enabled) {
    // NAV might support more visual states
    return Affa3::Affa3Error::NoError;
}

Affa3::Affa3Error Affa3NavDisplay::setTime(const char *clock) {
    // NAV-specific clock logic
    return Affa3::Affa3Error::NoError;
}