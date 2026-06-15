#pragma once
#include <esp32_can.h> /* https://github.com/collin80/esp32_can */
#include "AffaCommonConstants.h" /* Constants related to Affa3 */
#include <utils/CanUtils.h>
#include <Arduino.h>

class IDisplay {
public:
    virtual void tick() = 0;
    virtual void recv(CAN_FRAME* frame) = 0;
    
    virtual void processEvents() = 0;
    virtual AffaCommon::AffaError setText(const char* text, uint8_t digit =255 /* 0-9, or anything else for none */) =0; 
    virtual AffaCommon::AffaError setState(bool enabled) = 0; 
    virtual AffaCommon::AffaError setTime(const char *clock) = 0;   
    virtual void ProcessKey(AffaCommon::AffaKey key, bool isHold) =0;

    virtual AffaCommon::AffaError showMenu(const char *header, const char *item1, const char *item2, uint8_t scrollLockIndicator=0x0B)=0;
    
    virtual bool isCarminat() const { return false; }

    // Called when CAN detects AUX source (or from web UI for testing).
    // Enables AMS media screen. Default no-op for display types without AUX tracking.
    virtual void setAuxMode(bool on) { (void)on; }

    // Called when BT disconnects so the display can freeze its content.
    virtual void onBtDisconnected() {}

    // Bench emulator self-ACK toggle (see AffaDisplayBase). Default no-op.
    virtual void setEmuSelfAck(bool on) { (void)on; }

    // Transient "info" popup of up to 3 short lines, and its dismissal. Displays
    // that don't support a popup default to no-op, so callers (e.g. the web UI)
    // depend only on this interface, never a concrete display.
    virtual AffaCommon::AffaError showInfoPopup(const char *line1, const char *line2, const char *line3)
    { (void)line1; (void)line2; (void)line3; return AffaCommon::AffaError::NoError; }
    virtual void hideInfoPopup() {}
protected:
    virtual void onKeyPressed(AffaCommon::AffaKey key, bool isHold) =0;

};

