#pragma once
#include <vector>
#include <map>
#include <Arduino.h>
#include "apple_media_service.h"
#include "apple_notification_service.h"
#include "CarminatConstants.h"
#include "../AffaCommonConstants.h" /* Common Affa constants and enums */
#include "../AffaDisplayBase.h" /* Base class for Affa displays */
#include "../IPanel.h"          /* rendering port the collaborators draw through */
#include "Menu/Menu.h"          // Include the shared MenuItemType and MenuItem definitions
#include "AuxModeTracker.h"     // AUX-mode state (member; read by recv + NowPlaying)
#include "CarminatNowPlaying.h" // media + notification collaborator
#include "MenuController.h"     // navigation stack (page + key routing)
#include "DiagController.h"     // ELM / diagnostics pages

class IPage;
class DiagPage;
class MyELMManager;

// Also implements IPanel: showMenu/setText share IDisplay's signatures (one override
// satisfies both bases), and highlightItem already returns AffaError. This is the
// seam the Phase-G collaborators render through.
class CarminatDisplay : public AffaDisplayBase, public IPanel
{
public:
    CarminatDisplay()
        : mainMenu(
              "Main Menu",
              [this](const String &h, const String &t, const String &b, uint8_t scroll)
              {
                  this->showMenu(h.c_str(), t.c_str(), b.c_str(), scroll);
              },
              [this](uint8_t row)
              {
                  this->highlightItem(row);
              },

              [this]()
              {
                  this->setText("RENAULT", 0); // set defdault  text on close"));
              }

          ),
          _nowPlaying(*this, _aux, mainMenu),  // panel = *this (IPanel); aux + menu seams
          _menuCtrl(mainMenu),
          _diag(*this, mainMenu)
    {

        initializeFuncs();
        initializeMenu();
    }
    // using KeyHandler = std::function<bool(AffaCommon::AffaKey, bool)>;

    Menu &getMenu() { return mainMenu; }
    void  ProcessKey(AffaCommon::AffaKey key, bool isHold) override;
    void  setAuxMode(bool on) override;

    //void setKeyHandler(KeyHandler handler) { keyHandler = handler; }
 
    void recv(const Frame &frame) override;
    void processEvents();
    void setMediaInfo(const AppleMediaService::MediaInformation& info) override;
    void tick() override;

    AffaCommon::AffaError setText(const char *text, uint8_t digit = 255) override;
    AffaCommon::AffaError setState(bool enabled) override;
    AffaCommon::AffaError setTime(const char *clock) override;
    // Carminat::ScrollLockIndicator::SCROLL_BOTH
    AffaCommon::AffaError showMenu(const char *header, const char *item1, const char *item2, uint8_t scrollLockIndicator = 0x0B) override;

    AffaCommon::AffaError highlightItem(uint8_t id) override;   // IPanel
    AffaCommon::AffaError showConfirmBoxWithOffsets(const char *caption, const char *row1, const char *row2); // Show confirm box with offsets
    AffaCommon::AffaError showInfoMenu(const char *item1, const char *item2, const char *item3,
                                       uint8_t offset1 = 0x41, uint8_t offset2 = 0x44, uint8_t offset3 = 0x48,
                                       uint8_t infoPrefix = 0x70); // Show info menu with items and offsets

    // IDisplay info-popup capability (maps to showInfoMenu with default offsets).
    AffaCommon::AffaError showInfoPopup(const char *line1, const char *line2, const char *line3) override;
    void hideInfoPopup() override;

    // Fullscreen big-text screen (0x21 mode 0x05) — reverse-engineered from the OEM
    // "Please insert navigation CD" capture. Up to 3 centred-ish lines, \r-separated.
    AffaCommon::AffaError showFullscreenText(const char *line1, const char *line2, const char *line3) override;
    void hideFullscreenText() override;
    // Transient popup overlay (e.g. "VOL 28") — mode 0x74 setText-family frame.
    // icon/srcIcon/fmt = the variable header bytes (see IDisplay), exposed for RE.
    AffaCommon::AffaError showPopupText(const char *text, uint8_t icon = 0x09,
                                        uint8_t srcIcon = 0xFF, uint8_t fmt = 0x60) override;
    void hidePopup() override;
    // IDisplay confirm-box capability -> the existing showConfirmBoxWithOffsets builder.
    AffaCommon::AffaError showConfirmBox(const char *caption, const char *row1, const char *row2) override
    { return showConfirmBoxWithOffsets(caption, row1, row2); }

    bool isCarminat() const override { return true; }

    void begin() override;
    void tickMedia() override;
    void onElmUpdate(const char* key, float value) override;

    void attachElm(MyELMManager* m);
    void pushPage(IPage* p);
    void popPage();
protected:
    Menu mainMenu;
    void onKeyPressed(AffaCommon::AffaKey key, bool isHold) override;
    void initializeMenu();  // implemented in .cpp (reads NVS for BT mode items)

    uint8_t getPacketFiller() const override
    {
        return Carminat::PACKET_FILLER; // your specific constant
    }

    void initializeFuncs() override
    {
        funcsMax = 2;
        funcs = new Affa3Func[funcsMax]{
            {Carminat::PACKET_ID_SETTEXT, AffaCommon::FuncStatus::IDLE},
            {Carminat::PACKET_ID_NAV, AffaCommon::FuncStatus::IDLE}};
    }

private:
    // Declared after mainMenu (above) so the constructor init list sees mainMenu (and
    // _aux before _nowPlaying) fully constructed — member init follows declaration order.
    AuxModeTracker     _aux;         // AUX-mode tracker (was a file-scope global)
    CarminatNowPlaying _nowPlaying;  // media + ANCS-notification screen collaborator
    MenuController     _menuCtrl;    // navigation: active page + key routing
    DiagController     _diag;        // ELM diagnostics pages + live menu fields
};