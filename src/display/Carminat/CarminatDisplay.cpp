#include "CarminatDisplay.h"
#include <esp32_can.h>          // CAN_FRAME (recv shim) — no longer transitive via IDisplay
#include "utils/CanUtils.h"     // sends
#include "Pages/DiagPage.h"
#include "ElmManager/MyELMManager.h"

#include "AuxModeTracker.h"
#include "utils/TextUtils.h"
#include "bluetooth.h"
#include <NimBLEDevice.h>
#include <vector>
#include <Arduino.h>
#include <queue>
#include <Preferences.h>
#include <time.h>

// _autoTime is defined in main.cpp; flipped by the Auto-time menu item onChange
extern bool _autoTime;

inline void AFFA3_PRINT(const char *fmt, ...)
{
  // #ifdef DEBUG
  va_list args;
  va_start(args, fmt);
  vprintf(fmt, args);
  va_end(args);
  // #endif
}

int windowFirstItemIndex = 0;

namespace
{

  void emulateKey(AffaCommon::AffaKey key, bool hold = false)
  {

    uint16_t raw = static_cast<uint16_t>(key);
    CAN_FRAME frame;
    frame.id = 0x1C1; //
    frame.length = 8;
    frame.extended = 0; // standard frame

    frame.data.uint8[0] = 0x03;              // must be 0x03
    frame.data.uint8[1] = 0x89;              // must be 0x89
    frame.data.uint8[2] = (raw >> 8) & 0xFF; // key high byte
    frame.data.uint8[3] = raw & 0xFF;        // key low byte

    // Fill the rest with 0 or whatever is standard
    frame.data.uint8[4] = 0;
    frame.data.uint8[5] = 0;
    frame.data.uint8[6] = 0;
    frame.data.uint8[7] = 0;

    if (hold)
    {
      frame.data.uint8[3] |= AffaCommon::KEY_HOLD_MASK;
    }

    CanUtils::sendFrame(frame);

    Serial.print("Emulated key press: 0x");
    // Serial.println(key, HEX);
  }

  void sendPasswordSequence()
  {
    // 5
    for (int i = 0; i < 5; i++)
    {
      emulateKey(AffaCommon::AffaKey::RollUp);
      delay(200);
    }
    emulateKey(AffaCommon::AffaKey::Load);
    delay(200);

    // 3
    for (int i = 0; i < 3; i++)
    {
      emulateKey(AffaCommon::AffaKey::RollUp);
      delay(200);
    }
    emulateKey(AffaCommon::AffaKey::Load);
    delay(200);

    // 2
    for (int i = 0; i < 2; i++)
    {
      emulateKey(AffaCommon::AffaKey::RollUp);
      delay(200);
    }
    emulateKey(AffaCommon::AffaKey::Load);
    delay(200);

    // 1
    for (int i = 0; i < 1; i++)
    {
      emulateKey(AffaCommon::AffaKey::RollUp);
      delay(200);
    }

    emulateKey(AffaCommon::AffaKey::Load, true); // <-- hold
    delay(200);
  }

}

void CarminatDisplay::begin()
{
    // BT init is handled entirely in main.cpp; nothing to do here
}

void CarminatDisplay::initializeMenu()
{
    // Live read-only items (updated via onElmUpdate)
    mainMenu.addItem(MenuItem("Voltage", Field(0, "V"),    false));
    mainMenu.addItem(MenuItem("Boost",   Field(0, "mbar"), false));

    // Stubs — commented out until implemented
    // mainMenu.addItem(MenuItem("Color", {"Red", "Green", "Blue", "White"}, 1));
    // mainMenu.addItem(MenuItem("Effect", {"Static", "Blink", "Fade"}, 2));
    // mainMenu.addItem(MenuItem("Power", Field(163, 0, 500, 1, 2, "HP")));
    // mainMenu.addItem(MenuItem("Mileage", Field(250345, 0, 500000, 1, 100, "km")));

    mainMenu.addItem(MenuItem("Brightness", Field(50, 0, 100, 5, 2, "%")));

    auto &timeItem = mainMenu.addItem(MenuItem(
        "Time",
        {Field(12, 0, 23, 1, 3), Field(34, 0, 59, 1, 5)}));
    timeItem.onChange = [&](const MenuItem &item)
    {
        char buf[5];
        snprintf(buf, sizeof(buf), "%02d%02d",
                 item.fields[0].intValue,
                 item.fields[1].intValue);
        Serial.printf("Time changed to: %s\n", buf);
        setTime(buf);
    };

    // Read BT config from NVS
    Preferences prefs;
    prefs.begin("config", true);
    String btMode  = prefs.getString("bt_mode", "ams");
    bool autoTime  = prefs.getBool("auto_time", true);
    prefs.end();

    int btModeIdx   = (btMode == "ams") ? 1 : 0;
    int autoTimeIdx = autoTime ? 1 : 0;

    // "BT Mode" — always shown; takes effect on next reboot
    auto &btItem = mainMenu.addItem(
        MenuItem("BT Mode", Field(std::vector<String>{"Keyboard", "AMS"}, btModeIdx)));
    btItem.onChange = [](const MenuItem &item)
    {
        const char *val = (item.fields[0].listIndex == 1) ? "ams" : "keyboard";
        Preferences p;
        p.begin("config", false);
        p.putString("bt_mode", val);
        p.end();
        Serial.printf("[Menu] BT Mode saved: %s (reboot to apply)\n", val);
    };

    // "Auto-time" — only in AMS mode; runtime toggle (no reboot needed)
    if (btMode == "ams")
    {
        auto &atItem = mainMenu.addItem(
            MenuItem("Auto-time", Field(std::vector<String>{"Off", "On"}, autoTimeIdx)));
        atItem.onChange = [](const MenuItem &item)
        {
            _autoTime = (item.fields[0].listIndex == 1);
            Preferences p;
            p.begin("config", false);
            p.putBool("auto_time", _autoTime);
            p.end();
            Serial.printf("[Menu] Auto-time: %s\n", _autoTime ? "On" : "Off");
        };
    }

    // ---- Diagnostics section ----
    {
        auto &sep = mainMenu.addItem(
            MenuItem("-- DIAG --", std::vector<String>{""}, 0, false));
        sep.onActivate = []() {}; // no-op so hold-Load doesn't enter edit mode

        static const struct { const char* key; const char* title; } ecus[] = {
            {"7E0", "ENGINE"},
            {"743", "GEARBOX"},
            {"744", "HVAC"},
            {"745", "ECU 745"},
            {"74D", "ALT GBX"},
        };
        for (const auto& e : ecus) {
            auto &item = mainMenu.addItem(
                MenuItem(e.title, std::vector<String>{""}, 0, false));
            String hdr(e.key);
            item.onActivate = [this, hdr]() {
                if (DiagPage* p = _diag.page(hdr)) pushPage(p);
            };
        }
    }
}

void CarminatDisplay::onKeyPressed(AffaCommon::AffaKey key, bool isHold)
{
    // Kept for base-class contract; logic moved to ProcessKey
}

void CarminatDisplay::tick()
{
  // In radio mode the radio owns sync — ESP32 only injects data, never sends sync packets.
  if (_skipFuncReg) return;

  struct CAN_FRAME packet;
  static int8_t timeout = SYNC_TIMEOUT;

  /* Wysyłamy pakiet informujący o tym że żyjemy */
  CanUtils::sendCan(Carminat::PACKET_ID_SYNC, 0xB9, 0x00, Carminat::PACKET_FILLER, Carminat::PACKET_FILLER, Carminat::PACKET_FILLER, Carminat::PACKET_FILLER, Carminat::PACKET_FILLER, Carminat::PACKET_FILLER);

  if (hasFlag(_sync_status, SyncStatus::FAILED) || hasFlag(_sync_status, SyncStatus::START))
  { /* Błąd synchronizacji */
    /* Wysyłamy pakiet z żądaniem synchronizacji */
    // Throttle this log: with no display answering (bench) it would spam the
    // serial channel every tick. The sync packet below is still sent each time.
    static uint32_t _lastSyncLog = 0;
    if (millis() - _lastSyncLog > 10000)
    {
      _lastSyncLog = millis();
      AFFA3_PRINT("[tick] Sync failed or requested, sending sync request\n");
    }
    CanUtils::sendCan(Carminat::PACKET_ID_SYNC, 0xBA, Carminat::PACKET_FILLER, Carminat::PACKET_FILLER, Carminat::PACKET_FILLER, Carminat::PACKET_FILLER, Carminat::PACKET_FILLER, Carminat::PACKET_FILLER, Carminat::PACKET_FILLER);
    _sync_status &= ~SyncStatus::START;
    delay(100);
  }
  else
  {
    if (hasFlag(_sync_status, SyncStatus::PEER_ALIVE))
    {
      //	AFFA3_PRINT("[tick] Peer is alive, resetting timeout\n");
      timeout = SYNC_TIMEOUT;
      _sync_status &= ~SyncStatus::PEER_ALIVE;
    }
    else
    {
      timeout--;
      AFFA3_PRINT("[tick] Waiting for peer... timeout in %d\n", timeout);
      if (timeout <= 0)
      { /* Nic nie odpowiada, wymuszamy resynchronizację */
        _sync_status = SyncStatus::FAILED;
        /* Wszystkie funkcje tracą rejestracje */
        _sync_status &= ~SyncStatus::FUNCSREG;

        AFFA3_PRINT("ping timeout!\n");
      }
    }
  }
}

#define VOLTAGE_PIN 33 // Use  GPIO32
struct VoltageInfo
{
  float voltage;
  float adcValue;
};
VoltageInfo getVoltage()
{
  const int samples = 10;
  int adcTotal = 0;

  for (int i = 0; i < samples; ++i)
  {
    adcTotal += analogRead(VOLTAGE_PIN);
    delay(5);
  }

  float adcValue = adcTotal / float(samples);

  float r1 = 47000.0f;
  float r2 = 9770.0f; // 10k
  float vRef = 3.3f;
  float adcResolution = 4095.0f;

  float voltageAtPin = (adcValue * vRef) / adcResolution;
  float Vbatt = voltageAtPin * ((r1 + r2) / r2);

  Serial.print("Voltage: ");
  Serial.print(Vbatt, 2);
  Serial.print(" V | Adc: ");
  Serial.print(adcValue);
  Serial.print(" | Scale: ");
  Serial.println((vRef / adcResolution) * ((r1 + r2) / r2), 6);

  return {Vbatt, adcValue};
}

void ShowMyInfoMenu()
{

  VoltageInfo info = getVoltage();

  char row1[32];
  snprintf(row1, sizeof(row1), "Voltage: %.1fV (%.0f)", info.voltage, info.adcValue);

  // showMenu("MeganeCAN", row1, "Color: ORANGE");
}
struct Event
{
  enum Type
  {
    KeyPress,
    MediaInfoUpdate,
    Other
  } type;
  AffaCommon::AffaKey key;
  bool isHold;
};

std::queue<Event> eventQueue;

void CarminatDisplay::recv(const Frame &fr)
{
  // Shim the portable Frame back to a local CAN_FRAME so the (unchanged) handler body
  // below keeps working on `packet`. The body will be rewritten to use Frame directly
  // in a later step (which makes the AFFA3 handshake host-testable).
  CAN_FRAME _pkt;
  _pkt.id = fr.id; _pkt.extended = fr.extended; _pkt.rtr = false; _pkt.length = fr.len;
  for (uint8_t k = 0; k < fr.len && k < 8; k++) _pkt.data.uint8[k] = fr.data[k];
  CAN_FRAME *packet = &_pkt;

  uint8_t i;

  if (packet->id == Carminat::PACKET_ID_SYNC_REPLY)
  { /* Pakiety synchronizacyjne */
    Serial.printf("[recv] sync packet 0x%02X 0x%02X | _skipFuncReg=%s\n",
                  packet->data.uint8[0], packet->data.uint8[1],
                  _skipFuncReg ? "TRUE (will ignore)" : "FALSE (will process!)");
    if (_skipFuncReg)
    {
      return;
    }
    if ((packet->data.uint8[0] == 0x61) && (packet->data.uint8[1] == 0x11))
    { /* Żądanie synchronizacji */
      AFFA3_PRINT("[recv] sync request (0x61/0x11), sending registration\n");
      CanUtils::sendCan(Carminat::PACKET_ID_SYNC, 0x70, 0x1A, 0x11, 0x00, 0x00, 0x00, 0x00, 0x01);

      CanUtils::sendCan(Carminat::PACKET_ID_SYNC, 0xB0, 0x14, 0x11, 0x00, 0x1F, 0x00, 0x00, 0x00);
      CanUtils::sendCan(Carminat::PACKET_ID_SYNC, 0xB0, 0x14, 0x11, 0x00, 0x1F, 0x00, 0x00, 0x00);

      _sync_status &= ~SyncStatus::FAILED;
      if (packet->data.uint8[2] == 0x01)
        _sync_status |= SyncStatus::START;
    }
    else if (packet->data.uint8[0] == 0x69)
    {
      AFFA3_PRINT("[recv] peer alive (0x69)\n");
      _sync_status |= SyncStatus::PEER_ALIVE;
      tick();
    }
    else
    {
      AFFA3_PRINT("[recv] unknown sync packet 0x%02X 0x%02X\n",
                  packet->data.uint8[0], packet->data.uint8[1]);
    }
    return;
  }

  if (packet->id & Carminat::PACKET_REPLY_FLAG)
  {
    packet->id &= ~Carminat::PACKET_REPLY_FLAG;
    for (i = 0; i < funcsMax; i++)
    { /* Szukamy w tablicy funkcji */
      if (funcs[i].id == packet->id)
        break;
    }

    if ((i < funcsMax) && (funcs[i].stat == FuncStatus::WAIT))
    { /* Jeżeli funkcja ma status: oczekiwanie na odpowiedź */
      if (packet->data.uint8[0] == 0x74)
      { /* Koniec danych */
        funcs[i].stat = FuncStatus::DONE;
      }
      else if ((packet->data.uint8[0] == 0x30) && (packet->data.uint8[1] == 0x01) && (packet->data.uint8[2] == 0x00))
      { /* Wyświetlacz potwierdza przyjęcie części danych */
        funcs[i].stat = FuncStatus::PARTIAL;
      }
      else
      {
        funcs[i].stat = FuncStatus::ERROR;
      }
    }
    return;
  }
  if (packet->id == Carminat::PACKET_ID_SETTEXT)
  { /* Pakiet z danymi text */
    _aux.onCanMessage(*packet);
    mainMenu.handleMessage(*packet);
    if (packet->data.uint8[0] == 0x21) // Start of the text payload
    {
      if (packet->data.uint8[1] == 0x20 && packet->data.uint8[2] == 0x20 && packet->data.uint8[3] == 0xB0 && packet->data.uint8[4] == 0x30 && packet->data.uint8[5] == 0x30 && packet->data.uint8[6] == 0x30 && packet->data.uint8[7] == 0x20)
      {
        // input password
        delay(1000);
        sendPasswordSequence();
      }
    }
    // Process the NAV data here
    // For example, you can parse the data and update the display or internal state
  }

  if (!_skipFuncReg)
  {
    struct CAN_FRAME reply;
    /* Wysyłamy odpowiedź */
    reply.id = packet->id | Carminat::PACKET_REPLY_FLAG;
    reply.length = AffaCommon::PACKET_LENGTH;
    i = 0;
    reply.data.uint8[i++] = 0x74;
    for (; i < AffaCommon::PACKET_LENGTH; i++)
      reply.data.uint8[i] = Carminat::PACKET_FILLER;
    CanUtils::sendFrame(reply);
  }

  if (packet->id == Carminat::PACKET_ID_KEYPRESSED) // TODO CHECK IT
  {
    if (!((packet->data.uint8[0] == 0x03) && (packet->data.uint8[1] == 0x89))) /* Błędny pakiet */
      return;

    // Extract full key
    //% uint16_t rawKey = (packet->data.uint8[2] << 8) | packet->data.uint8[3];

    uint8_t highByte = packet->data.uint8[2];
    uint8_t lowByte = packet->data.uint8[3];
    // Combine into rawKey
    uint16_t rawKey = (highByte << 8) | lowByte;

    bool isHold = false;
    uint16_t maskedKey = rawKey;

    // Only apply hold masking for non-encoder keys
    if (!(rawKey == 0x0101 || rawKey == 0x0141))
    {
      isHold = (lowByte & AffaCommon::KEY_HOLD_MASK) != 0;
      maskedKey = rawKey & ~AffaCommon::KEY_HOLD_MASK;
    }

    // Debug log
    Serial.printf(
        "[KEY DEBUG] raw=0x%04X masked=0x%04X isHold=%d\n",
        rawKey, maskedKey, isHold);
    // Cast to enum after masking
    AffaCommon::AffaKey key = static_cast<AffaCommon::AffaKey>(maskedKey);

    // // Mask out hold bits for key comparison
    // AffaCommon::AffaKey key = static_cast<AffaCommon::AffaKey>(rawKey & ~AffaCommon::KEY_HOLD_MASK);

    // // Detect hold status
    // bool isHold = (rawKey & AffaCommon::KEY_HOLD_MASK) != 0;
    eventQueue.push({Event::KeyPress, key, isHold});
    // onKeyPressed(key, isHold);
  }
}

void CarminatDisplay::processEvents()
{
  while (!eventQueue.empty())
  {
    Event e = eventQueue.front();
    eventQueue.pop();
    switch (e.type)
    {
    case Event::KeyPress:
    {
      ProcessKey(e.key, e.isHold);  // dual-mode: AMS keyHandler OR BleKeyboard fallback
      break;
    }

    case Event::MediaInfoUpdate:
    {
      // є новий AMS-стан → перемальовуємо медіа-екран
      //Serial.println("Processing media update event...");
      _nowPlaying.renderMediaScreen(/*forceRedraw=*/true);
      break;
    }
    }
  }

  // Tick the active page (rate-limited internally by DiagPage)
  _menuCtrl.tickCurrentPage();
}
/**
 * Sends a text string to the car display over CAN bus.
 *
 * @param mode Display mode:
 *             0x74 - full window,
 *             0x77 - partial window (some values freeze UI if not matched)
 *
 * @param rdsIcon RDS icon display:
 *                0x45 - AF-RDS icon,
 *                0x55 - no icon
 *
 * @param sourceIcon Source label icon:
 *                   0xDF - "MANU",
 *                   0xFD - "PRESET",
 *                   0xFF - none,
 *                   others show icons like "LIST"
 *
 * @param channel Text display format:
 *                   0x19–0x3F - Radio-style (5 digits + '.' + 1 char),
 *                   0x59–0x7F - Plain ASCII (up to 7 chars visible)
 *                    19-3f 5sym + point+ 2sym + channel.  (19 for ✔️),59-7F plain 8 sym + Channel ascii
 *
 * @param controlByte Always 0x01 — required by display protocol
 *
 * @param rawText Text to display (max 7 characters shown).
 *                Underscores (_) are replaced with spaces.
 */
AffaCommon::AffaError CarminatDisplay::setText(const char *text, uint8_t digit)
{

  // 74- full window, 77-not full. if sended not full when not applid - it fill freze at main screen.
  // sc 151 2 54 3 0 0 0 0 0  to close full window

  Serial.println("[setText] --- Sending Text to Display AFFA3NAV ---");
  uint8_t mode = 0x77;       // 74- full window, 77-not full. if sended not full when not applid - it fill freze at main screen.
                             // sc 151 2 54 3 0 0 0 0 0  to close full window
  uint8_t rdsIcon = 0x55;    // strtol(rdsIconStr,    nullptr, 16);
  uint8_t sourceIcon = 0xFF; // strtol(sourceIconStr, nullptr, 16);

  uint8_t textFormat = 0x60; // strtol(textFormatStr, nullptr, 16);

  uint8_t data[32]; // max payload
  uint8_t len = 0;

  // First ISO-TP frame + length
  data[len++] = 0x10; // First ISO-TP frame
  data[len++] = 0x0E; // 14 bytes total (header + text)

  // Display header structure
  data[len++] = mode;
  data[len++] = rdsIcon;
  data[len++] = 0x55; // unknown/fixed
  data[len++] = sourceIcon;
  data[len++] = textFormat;
  data[len++] = 0x01; // control byte

  // Sanitize and pad text to 14 bytes
  char paddedText[15] = {0}; // null-terminated buffer
  strncpy(paddedText, text, 14);
  for (uint8_t i = 0; i < 14; i++)
  {
    // if (paddedText[i] == '_') paddedText[i] = ' ';
    data[len++] = paddedText[i];
  }

  return affa3_send(0x151, data, len);
  // Possibly allows longer or formatted text
  return AffaCommon::AffaError::NoError;
}

void CarminatDisplay::setMediaInfo(const AppleMediaService::MediaInformation &info)
{
  // State update lives in the collaborator; the coordinator owns the event loop and
  // schedules the redraw via the queue (drained in processEvents).
  _nowPlaying.setMediaInfo(info);
  eventQueue.push({Event::MediaInfoUpdate, AffaCommon::AffaKey::Load, false});
}

void CarminatDisplay::ProcessKey(AffaCommon::AffaKey key, bool isHold)
{
    // Page/menu routing lives in MenuController; the keyHandler fall-through stays
    // here, where keyHandler lives (AffaDisplayBase). routeKey returns false only
    // when no page is active and the menu didn't consume the key.
    if (!_menuCtrl.routeKey(key, isHold) && keyHandler)
        keyHandler(key, isHold);
}

void CarminatDisplay::setAuxMode(bool on)
{
    _aux.SetAuxMode(on);
    Serial.printf("[AUX] setAuxMode(%s)\n", on ? "true" : "false");
}

void CarminatDisplay::tickMedia()
{
    _nowPlaying.tick();
}
// menuit 41 44 48 77 AUX__AUTO AFAUTO SPEED_0
void showInfoMenu(
    const char *item1,
    const char *item2,
    const char *item3,
    uint8_t offset1,    // 0x41
    uint8_t offset2,    // 0x44
    uint8_t offset3,    // 0x48 -- dont know what changing, maybe itr constants
    uint8_t infoPrefix) // same as with text,
// 19-3f 5symbols + point + 2sym + channel.  (19 for ✔️),59-7F plain 8 sym + Channel ascii
// so if you send 19-3f it shows text with period dot at the end, starting from second symbol. for example sometext=> will apear like omete.x
// if you send 59-7f it shows text with period dot at the end, starting from second symbol. for example sometext=> will apear like omete.x
//  its also shows somechannel symbol based on asccii code, for example to show 9 you need send 39 or 79, for show # - 23 or 63
{
  Serial.println("[showInfoMenu] --- Sending Info Menu ---");

  auto sendMenuItem = [&](uint8_t offset, const char *text, const char *label)
  {
    char padded[8] = {' '};
    strncpy(padded, text, 8);

    Serial.print("[MenuItem] ");
    Serial.print(label);
    Serial.print(" | Offset: 0x");
    Serial.print(offset, HEX);
    Serial.print(" | Text: \"");
    Serial.print(padded);
    Serial.println("\"");

    CanUtils::sendCan(0x151, 0x10, 0x0B, 0x76, infoPrefix, offset, padded[0], padded[1], padded[2]);
    delay(5);
    CanUtils::sendCan(0x151, 0x21, padded[3], padded[4], padded[5], padded[6], padded[7], 0x00, 0x00);
    delay(5);
  };

  sendMenuItem(offset1, item1, "Item1");
  sendMenuItem(offset2, item2, "Item2");
  sendMenuItem(offset3, item3, "Item3");

  Serial.println("[showInfoMenu] --- Done ---");
}

AffaCommon::AffaError CarminatDisplay::setState(bool enabled)
{
  Carminat::DisplayCtrl state = enabled ? Carminat::DisplayCtrl::Enable : Carminat::DisplayCtrl::Disable;

  uint8_t data[] = {
      0x3, 0x52, static_cast<uint8_t>(state), 0xFF, 0xFF}; // sc 151 3 52 9 0 0 0 0 0

  return affa3_send(Carminat::PACKET_ID_DISPLAY_CTRL, data, sizeof(data));
}

AffaCommon::AffaError CarminatDisplay::setTime(const char *clock)
{
  // NAV-specific clock logic
  CAN_FRAME answer;
  answer.id = 0x151;
  answer.length = 8;
  answer.data.uint8[0] = 0x05;
  answer.data.uint8[1] = 'V'; // likely constant
  answer.data.uint8[2] = clock[0];
  answer.data.uint8[3] = clock[1];
  answer.data.uint8[4] = clock[2];
  answer.data.uint8[5] = clock[3];
  answer.data.uint8[6] = 0x00;
  answer.data.uint8[7] = 0x00;

  affa3_send(answer.id, answer.data.uint8, answer.length);
  Serial.print("Sent time set frame with year: ");
  Serial.println(clock);
  return AffaCommon::AffaError::NoError;
}
AffaCommon::AffaError CarminatDisplay::highlightItem(uint8_t id)
{
  CAN_FRAME frame;
  frame.id = 0x151;
  frame.length = 8;
  frame.extended = 0;

  frame.data.uint8[0] = 0x07;
  frame.data.uint8[1] = 0x29;
  frame.data.uint8[2] = 0x01;
  frame.data.uint8[3] = id == 0 ? 0x7E : 0x7F;
  frame.data.uint8[4] = 0x80;
  frame.data.uint8[5] = 0x00;
  frame.data.uint8[6] = 0x00;
  frame.data.uint8[7] = 0x00;

  CanUtils::sendFrame(frame);
  Serial.print(">> Highlight item: ");
  Serial.println(id);
  return AffaCommon::AffaError::NoError;
}

// SCROLL LOCK INDICATOR
//  0x00 - no scroll lock, 0x07 - scroll UP -0x0B - scroll DOWN, 0x0C - scroll UP and DOWNCarminat::ScrollLockIndicator scrollLockIndicator
AffaCommon::AffaError CarminatDisplay::showMenu(const char *header, const char *item1, const char *item2, uint8_t scrollLockIndicator)
{
  // The Carminat charset can't render UTF-8. showMenu is the single choke point for
  // ALL text sent to it (menu, now-playing, notifications), so transliterate here:
  // Cyrillic/Polish -> Latin, anything else -> '?'. Idempotent on ASCII, so callers
  // that already transliterated are unaffected; callers that didn't (e.g. the app
  // name, media titles) no longer leak raw UTF-8 -> mojibake on the display.
  String _h  = transliterateToAscii(String(header ? header : ""));
  String _i1 = transliterateToAscii(String(item1 ? item1 : ""));
  String _i2 = transliterateToAscii(String(item2 ? item2 : ""));
  header = _h.c_str();
  item1  = _i1.c_str();
  item2  = _i2.c_str();

  uint8_t payload[96] = {0};
  int idx = 0;

  payload[idx++] = 0x10; // First ISO-TP frame
  payload[idx++] = 0x5A;
  payload[idx++] = 0x21;
  payload[idx++] = 0x01;
  payload[idx++] = 0x7E;
  payload[idx++] = 0x80;
  payload[idx++] = 0x00;
  payload[idx++] = 0x00;

  payload[idx++] = 0x82;
  payload[idx++] = 0xFF;
  payload[idx++] = scrollLockIndicator;

  int h = 0;
  for (; h < 26 && header[h]; ++h)
  {
    payload[idx++] = header[h];
  }

  while (idx < 37)
    payload[idx++] = 0x00;

  // Item 1
  payload[idx++] = 0x00;
  payload[idx++] = 0x7E;

  while (*item1 && idx < 64)
    payload[idx++] = *item1++;
  // Padding to reach item2
  while (idx < 64)
    payload[idx++] = 0x00;

  payload[idx++] = 0x01;
  payload[idx++] = 0x7F;
  while (*item2 && idx < 96)
    payload[idx++] = *item2++;
  while (idx < 96)
    payload[idx++] = 0x00;

  // Final length check
  int totalLen = idx;

  Serial.printf("[CAN] do_send totalLen: %d  \n", totalLen);
  return affa3_send(0x151, payload, totalLen);
}

void CarminatDisplay::onElmUpdate(const char* key, float value)
{
    _diag.onElmUpdate(key, value);
}

// Big "confirm box" popup (caption button + two text rows) over 0x151.
//
// Reverse-engineered wire format (reproduced byte-for-byte from the original raw
// implementation), but now routed through affa3_send so it respects the busAlive
// gate, per-frame ACK, the @TX serial mirror and the bench self-ACK emulator
// instead of blasting CanUtils::sendCan directly.
//
// ISO-TP layout: first frame = 0x10 <len> + a fixed 6-byte header (21 05 00 00 01
// 49); affa3_do_send adds the 0x2N PCI for the 15 consecutive frames. The visible
// text lives in a 105-byte content region (15 × 7) at these offsets:
//   0x1A.. : button caption (max 7 chars)
//   0x20.. : row1, 0x0D separator, row2, 0x0D   (bounded < 0x36)
AffaCommon::AffaError CarminatDisplay::showConfirmBoxWithOffsets(const char *caption, const char *row1, const char *row2)
{
  // Carminat can't render UTF-8 — transliterate, same rule as showMenu().
  String _cap = transliterateToAscii(String(caption ? caption : ""));
  String _r1  = transliterateToAscii(String(row1 ? row1 : ""));
  String _r2  = transliterateToAscii(String(row2 ? row2 : ""));

  uint8_t content[105] = {0};

  const char *cap = _cap.c_str();
  for (uint8_t i = 0; i < 7 && cap[i]; i++)
    content[0x1A + i] = cap[i];

  uint8_t off = 0x20;
  auto insertRow = [&](const char *t)
  {
    while (*t && off < 0x36)
      content[off++] = *t++;
    if (off < 0x36)
      content[off++] = 0x0D; // row separator
  };
  insertRow(_r1.c_str());
  insertRow(_r2.c_str());

  // Build the ISO-TP buffer: 0x10 <len> + 6-byte header + 105 content bytes.
  // len = 6 (first-frame payload) + 105 = 111 (0x6F).
  uint8_t buf[2 + 6 + sizeof(content)];
  uint8_t n = 0;
  buf[n++] = 0x10; // ISO-TP first frame
  buf[n++] = 0x6F; // total content length = 111 bytes
  buf[n++] = 0x21; // ---- fixed 6-byte header ----
  buf[n++] = 0x05;
  buf[n++] = 0x00;
  buf[n++] = 0x00;
  buf[n++] = 0x01;
  buf[n++] = 0x49;
  memcpy(buf + n, content, sizeof(content));
  n += sizeof(content);

  Serial.println("[showConfirmBoxWithOffsets] sending confirm box via affa3_send");
  return affa3_send(0x151, buf, n);
}

AffaCommon::AffaError CarminatDisplay::showInfoMenu(const char *item1, const char *item2, const char *item3, uint8_t offset1, uint8_t offset2, uint8_t offset3, uint8_t infoPrefix)
{
  // Wire the (previously stubbed) method to the free implementation above, which
  // sends the 3-item info popup over 0x151 (10 0B 76 <prefix> <offset> + 8 chars).
  ::showInfoMenu(item1, item2, item3, offset1, offset2, offset3, infoPrefix);
  return AffaCommon::AffaError::NoError;
}

// IDisplay capability: generic 3-line info popup -> Carminat showInfoMenu defaults.
AffaCommon::AffaError CarminatDisplay::showInfoPopup(const char *line1, const char *line2, const char *line3)
{
  return showInfoMenu(line1, line2, line3);
}

void CarminatDisplay::hideInfoPopup()
{
  // Best-effort dismiss: return to normal text. The exact popup-close command is
  // still being confirmed on the car; refine once observed.
  setText("RENAULT", 0);
}

// ---- Page management ----

void CarminatDisplay::attachElm(MyELMManager* m)
{
    _diag.attachElm(m);
}

void CarminatDisplay::pushPage(IPage* p)
{
    _menuCtrl.pushPage(p);
}

void CarminatDisplay::popPage()
{
    _menuCtrl.popPage();
}
