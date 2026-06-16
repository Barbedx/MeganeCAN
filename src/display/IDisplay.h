#pragma once
#include "AffaCommonConstants.h" /* Constants related to Affa3 */
#include "../bus/Frame.h"        /* portable CAN frame — the display port speaks this, not CAN_FRAME */

// The display PORT is hardware-free: it speaks Frame + plain C strings only (no
// esp32_can / CanUtils / Arduino). Concrete displays include what they need.

class IDisplay {
public:
    virtual void tick() = 0;
    // Inbound bus frame. Frame (not the vendor CAN_FRAME) so the display port stays
    // free of the driver type — the keystone for host-testing the radio side.
    virtual void recv(const Frame& frame) = 0;
    
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

