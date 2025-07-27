#include "Affa3NavDisplay.h"
// #include "Affa3Display.h"  //   (false) -> Ensure this is included if emulateKey is from there NO,
// we dont need that file here, Affa3NavDisplay is a separate display class
#include "AuxModeTracker.h"
#include <BleKeyboard.h>
#include <NimBLEDevice.h>

inline void AFFA3_PRINT(const char *fmt, ...)
{
#ifdef DEBUG
  va_list args;
  va_start(args, fmt);
  vprintf(fmt, args);
  va_end(args);
#endif
}

// You can copy code from Affa3Display.cpp as a starting point
using FuncStatus = AffaCommon::FuncStatus;
using SyncStatus = AffaCommon::SyncStatus;
using AffaError = AffaCommon::AffaError;

BleKeyboard bleKeyboard("MeganeCAN", "gycer", 100);

namespace
{

  static SyncStatus _sync_status = SyncStatus::FAILED; /* Status synchronizacji z wświetlaczem */
  static uint8_t _menu_max_items = 0;
  constexpr int AFFA3_PING_TIMEOUT = 5;
  constexpr size_t AFFA3_KEY_QUEUE_SIZE = 8;

  AuxModeTracker tracker;

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
  // Definition for affa3_func struct
  struct Affa3Func
  {
    uint16_t id;
    FuncStatus stat;
  };
  Affa3Func funcs[] = {
      {Affa3Nav::PACKET_ID_SETTEXT, FuncStatus::IDLE},
      {Affa3Nav::PACKET_ID_NAV, FuncStatus::IDLE}};

  constexpr size_t funcsMax = sizeof(funcs) / sizeof(funcs[0]);

  // static uint16_t _key_q[AFFA3_KEY_QUEUE_SIZE] = {
  //     0,
  // };
  // static uint8_t _key_q_in = 0;
  // static uint8_t _key_q_out = 0;
  // bool isKeyQueueFull() { return ((_key_q_in + 1) % AFFA3_KEY_QUEUE_SIZE) == _key_q_out; }
  // bool isKeyQueueEmpty() { return _key_q_in == _key_q_out; }


}

void Affa3NavDisplay::tick()
{

  struct CAN_FRAME packet;
  static int8_t timeout = AFFA3_PING_TIMEOUT;

  /* Wysyłamy pakiet informujący o tym że żyjemy */
  CanUtils::sendCan(Affa3Nav::PACKET_ID_SYNC, 0xB9, 0x00, Affa3Nav::PACKET_FILLER, Affa3Nav::PACKET_FILLER, Affa3Nav::PACKET_FILLER, Affa3Nav::PACKET_FILLER, Affa3Nav::PACKET_FILLER, Affa3Nav::PACKET_FILLER);

  if (hasFlag(_sync_status, SyncStatus::FAILED) || hasFlag(_sync_status, SyncStatus::START))
  { /* Błąd synchronizacji */
    /* Wysyłamy pakiet z żądaniem synchronizacji */
    AFFA3_PRINT("[tick] Sync failed or requested, sending sync request\n");
    CanUtils::sendCan(Affa3Nav::PACKET_ID_SYNC, 0xBA, Affa3Nav::PACKET_FILLER, Affa3Nav::PACKET_FILLER, Affa3Nav::PACKET_FILLER, Affa3Nav::PACKET_FILLER, Affa3Nav::PACKET_FILLER, Affa3Nav::PACKET_FILLER, Affa3Nav::PACKET_FILLER);
    _sync_status &= ~SyncStatus::START;
    delay(100);
  }
  else
  {
    if (hasFlag(_sync_status, SyncStatus::PEER_ALIVE))
    {
      //	AFFA3_PRINT("[tick] Peer is alive, resetting timeout\n");
      timeout = AFFA3_PING_TIMEOUT;
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

// Extended NAV display logic

// Add enums for item types and key input
enum class MenuItemType {
  StaticText,
  OptionSelector,
  IntegerEditor,
  SubMenu,
};

struct MenuItem;

struct Menu {
  const char* header;
  std::vector<MenuItem> items;
  int selectedIndex = 0;
  Menu* parent = nullptr;

  Menu(const char* h) : header(h) {}

  // Navigate within menu items
  void navigateUp() {
    if (selectedIndex > 0) selectedIndex--;
    else selectedIndex = items.size() - 1;
  }
  void navigateDown() {
    if (selectedIndex < (int)items.size() - 1) selectedIndex++;
    else selectedIndex = 0;
  }

  void draw();
};

struct MenuItem {
  MenuItemType type;
  const char* label;

  // For StaticText
  const char* staticValue;

  // For OptionSelector
  std::vector<const char*> options;
  int selectedOption = 0;

  // For IntegerEditor
  int intValue = 0;
  int minValue = 0;
  int maxValue = 100;

  // For SubMenu
  Menu* submenu = nullptr;

  MenuItem(MenuItemType t, const char* l)
    : type(t), label(l), staticValue(nullptr), submenu(nullptr) {}
};






void Menu::draw() {
  // Show 3 items total: selected + 1 above + 1 below (if exist)
  const char* item1 = nullptr;
  const char* item2 = nullptr;
  const char* item3 = nullptr;

  int sel = selectedIndex;

  if (items.empty()) {
    item1 = "";
    item2 = "";
    item3 = "";
  } else {
    // item2 is selected
    item2 = nullptr;
    // item1 is selected-1 or blank
    // item3 is selected+1 or blank

    // Helper to get string for display of an item
    auto getItemString = [](const MenuItem& mi) -> String {
      switch (mi.type) {
        case MenuItemType::StaticText:
          return String(mi.label) + ": " + (mi.staticValue ? mi.staticValue : "");
        case MenuItemType::OptionSelector:
          return String(mi.label) + ": " + (mi.options.empty() ? "" : mi.options[mi.selectedOption]);
        case MenuItemType::IntegerEditor:
          return String(mi.label) + ": " + String(mi.intValue);
        case MenuItemType::SubMenu:
          return String(mi.label) + " >";
      }
      return "";
    };

    String str1 = (sel > 0) ? getItemString(items[sel - 1]) : "";
    String str2 = getItemString(items[sel]);
    String str3 = (sel + 1 < (int)items.size()) ? getItemString(items[sel + 1]) : "";

    item1 = str1.c_str();
    item2 = str2.c_str();
    item3 = str3.c_str();

    // WARNING: The pointers returned here are to temporary String objects!
    // We'll handle this in a safer way in actual code.
  }

  // Use your existing showMenu to send display.
  // For simplicity here, only 2 items shown in showMenu, but let's modify your showMenu later to 3 items or create a new one.
  // For now let's send first 2 items + header:

  // Because your showMenu takes 2 items, we can call it twice or modify it to support 3.
  // Let's create a simple 3 item version for our menu here:

  // We'll send a combined menu with header and 3 items, showing selected highlight with flags:
  // 0x7E = not selected, 0x7F = selected

  // We'll craft a simple helper here:

  auto sendMenu = [](const char* header, const char* i1, bool i1sel, const char* i2, bool i2sel, const char* i3, bool i3sel) {
    uint8_t sel1 = i1sel ? 0x7F : 0x7E;
    uint8_t sel2 = i2sel ? 0x7F : 0x7E;
    uint8_t sel3 = i3sel ? 0x7F : 0x7E;

    // We'll build and send menu with 3 items here using a similar method as your showMenu
    // You can copy/paste your showMenu and adapt to 3 items with those flags accordingly

    // For demo, just send 2 items (header + item1 + item2)
    // You can extend to 3 later easily.

   // showMenu(header, i1, i2, 0x5A, 0x0B, sel1, sel2);
    // Optionally send the 3rd item as info or in another way
  };

  sendMenu(header, item1, sel > 0 && sel-1 == sel, item2, true, item3, sel + 1 == sel);
}

Menu* currentMenu = nullptr;

Menu rootMenu("Settings");
Menu effectsMenu("Effects");

void setupMenus() {
  // Setup effects submenu
  effectsMenu.items.push_back(MenuItem(MenuItemType::StaticText, "Lighting"));
  effectsMenu.items.push_back(MenuItem(MenuItemType::StaticText, "Blink"));
  effectsMenu.items.push_back(MenuItem(MenuItemType::StaticText, "Off"));
  effectsMenu.parent = &rootMenu;

  // Setup root menu items
  MenuItem voltage(MenuItemType::StaticText, "Accu Voltage");
  voltage.staticValue = "14V";

  MenuItem color(MenuItemType::OptionSelector, "Color");
  color.options = {"Red", "Green", "Blue"};
  color.selectedOption = 0;

  MenuItem someInt(MenuItemType::IntegerEditor, "SomeIntPar");
  someInt.intValue = 1;
  someInt.minValue = 0;
  someInt.maxValue = 10;

  MenuItem effect(MenuItemType::SubMenu, "Effect");
  effect.submenu = &effectsMenu;
  effect.submenu->parent = &rootMenu;

  rootMenu.items.push_back(voltage);
  rootMenu.items.push_back(color);
  rootMenu.items.push_back(someInt);
  rootMenu.items.push_back(effect);

  currentMenu = &rootMenu;
}






#define VOLTAGE_PIN 33 // Use  GPIO32
struct VoltageInfo {
  float voltage;
  float adcValue;
};
VoltageInfo getVoltage()
{
  const int samples = 10;
  int adcTotal = 0;

  for (int i = 0; i < samples; ++i) {
    adcTotal += analogRead(VOLTAGE_PIN);
    delay(5);
  }

  float adcValue = adcTotal / float(samples);

  float r1 = 47000.0f;
  float r2 = 9770.0f;//10k
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

  return { Vbatt, adcValue };
}












void showMenu(
    const char *header,
    const char *item1,
    const char *item2,
    uint8_t firstFrameSize = 0x5A, 
    uint8_t scrollLockIndicator = 0x0B,
    uint8_t selectionItem1 = 0x00, 
    uint8_t selectionItem2 = 0x01
  )
{
  Serial.println("[showMenu] --- Building Menu ---");

  Serial.print("[Header] ");
  Serial.println(header);
  Serial.print("[Item1] ");
  Serial.println(item1);
  Serial.print("[Item2] ");
  Serial.println(item2);

  uint8_t payload[96] = {0};
  int idx = 0;

  payload[idx++] = 0x21;
  payload[idx++] = 0x01;
  payload[idx++] = 0x7E;
  payload[idx++] = 0x80;
  payload[idx++] = 0x00;
  payload[idx++] = 0x00;

  Serial.print("[CAN] Sending First Frame: ID=0x151 Data= ");
  Serial.printf("10 %02X ", firstFrameSize);
  for (int i = 0; i < 6; i++)
    Serial.printf("%02X ", payload[i]);
  Serial.println();

  CanUtils::sendCan(0x151, 0x10, firstFrameSize, payload[0], payload[1], payload[2], payload[3], payload[4], payload[5]);

  payload[idx++] = 0x82;
  payload[idx++] = 0xFF;
  payload[idx++] = scrollLockIndicator;

  int maxHeaderLen = 26;
  int h = 0;
  while (header && header[h] && h < maxHeaderLen && idx < 96)
  {
    payload[idx++] = header[h++];
  }

  while (idx < 35)
    payload[idx++] = 0x00;

  payload[idx++] = selectionItem1; // not selection tbh
  payload[idx++] = 0x7E;

  while (*item1 && idx < 62)
    payload[idx++] = *item1++;

  while (idx < 62)
    payload[idx++] = 0x00;

  payload[idx++] = selectionItem2;
  payload[idx++] = 0x7F;
  while (*item2 && idx < 96)
    payload[idx++] = *item2++;
  while (idx < 96)
    payload[idx++] = 0x00;

  uint8_t seq = 1;
  for (int i = 6; i < idx; i += 7)
  {
    uint8_t d[8];
    d[0] = 0x20 | (seq++ & 0x0F);
    for (int j = 0; j < 7; j++)
    {
      d[j + 1] = (i + j < idx) ? payload[i + j] : 0x00;
    }

    CanUtils::sendCan(0x151, d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7]);
    delay(5);
  }

  Serial.println("[showMenu] --- Done ---");
}
void ShowMyInfoMenu()
{

  VoltageInfo info = getVoltage();

  char row1[32];
  snprintf(row1, sizeof(row1), "Voltage: %.1fV (%.0f)", info.voltage, info.adcValue);

  showMenu("MeganeCAN", row1, "Color: ORANGE");

}

void Affa3NavDisplay::recv(CAN_FRAME *packet)
{

  uint8_t i;

  if (packet->id == Affa3Nav::PACKET_ID_SYNC_REPLY)
  { /* Pakiety synchronizacyjne */
    if ((packet->data.uint8[0] == 0x61) && (packet->data.uint8[1] == 0x11))
    { /* Żądanie synchronizacji */
      CanUtils::sendCan(Affa3Nav::PACKET_ID_SYNC, 0x70, 0x1A, 0x11, 0x00, 0x00, 0x00, 0x00, 0x01);
      // affa3_is_synced = false;

      CanUtils::sendCan(Affa3Nav::PACKET_ID_SYNC, 0xB0, 0x14, 0x11, 0x00, 0x1F, 0x00, 0x00, 0x00);
      CanUtils::sendCan(Affa3Nav::PACKET_ID_SYNC, 0xB0, 0x14, 0x11, 0x00, 0x1F, 0x00, 0x00, 0x00);

      _sync_status &= ~SyncStatus::FAILED;
      if (packet->data.uint8[2] == 0x01)
        _sync_status |= SyncStatus::START;
    }
    else if (packet->data.uint8[0] == 0x69)
    {
      _sync_status |= SyncStatus::PEER_ALIVE;
      tick();
    }
    return;
  }

  if (packet->id & Affa3Nav::PACKET_REPLY_FLAG)
  {
    packet->id &= ~Affa3Nav::PACKET_REPLY_FLAG;
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
  if (packet->id == Affa3Nav::PACKET_ID_KEYPRESSED) // TODO CHECK IT
  {
    if ((packet->data.uint8[0] == 0x03) && (packet->data.uint8[1] != 0x89)) /* Błędny pakiet */
      return;

    // Extract full key
    uint16_t rawKey = (packet->data.uint8[2] << 8) | packet->data.uint8[3];

    // Mask out hold bits for key comparison
    AffaCommon::AffaKey key = static_cast<AffaCommon::AffaKey>(rawKey & ~AffaCommon::KEY_HOLD_MASK);

    // Detect hold status
    bool isHold = (rawKey & AffaCommon::KEY_HOLD_MASK) != 0;
    

 
    if (isHold &&  key==AffaCommon::AffaKey::Load){ // load
 
          setState(true); 
          tracker.SetAuxMode(true);
          delay(50);
          ShowMyInfoMenu();  
    }


    if (tracker.isInAuxMode())
    {
      Serial.print("Current in aux");
      // Handle key actions
      switch (static_cast<AffaCommon::AffaKey>(key))
      {
      case AffaCommon::AffaKey::Pause:
        Serial.println("Pause/Play");
        bleKeyboard.write(KEY_MEDIA_PLAY_PAUSE);
        break;

      case AffaCommon::AffaKey::RollUp: // Next track
        Serial.println("Next Track");
        bleKeyboard.write(KEY_MEDIA_NEXT_TRACK);
        break;

      case AffaCommon::AffaKey::RollDown: // Previous track
        Serial.println("Previous Track");
        bleKeyboard.write(KEY_MEDIA_PREVIOUS_TRACK);
        break; 
      default:
        Serial.print("Unknown key: 0x"); 
        break;
      }
    }
   
  }
  if (packet->id == Affa3Nav::PACKET_ID_SETTEXT)
  { /* Pakiet z danymi text */
    tracker.onCanMessage(*packet);
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

  struct CAN_FRAME reply;
  /* Wysyłamy odpowiedź */
  reply.id = packet->id | Affa3Nav::PACKET_REPLY_FLAG;
  reply.length = AffaCommon::PACKET_LENGTH;
  i = 0;
  reply.data.uint8[i++] = 0x74;

  for (; i < AffaCommon::PACKET_LENGTH; i++)
    reply.data.uint8[i] = Affa3Nav::PACKET_FILLER;

  CanUtils::sendFrame(reply);
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
AffaCommon::AffaError Affa3NavDisplay::setText(const char *text, uint8_t digit)
{ 

  // 74- full window, 77-not full. if sended not full when not applid - it fill freze at main screen.
                                // sc 151 2 54 3 0 0 0 0 0  to close full window

Serial.println("[setText] --- Sending Text to Display AFFA3NAV ---");
   uint8_t mode = 0x77;       // 74- full window, 77-not full. if sended not full when not applid - it fill freze at main screen.
                                // sc 151 2 54 3 0 0 0 0 0  to close full window
   uint8_t rdsIcon = 0x55;    // strtol(rdsIconStr,    nullptr, 16);
   uint8_t sourceIcon = 0xFF; // strtol(sourceIconStr, nullptr, 16);
   
   uint8_t textFormat = 0x60; // strtol(textFormatStr, nullptr, 16);
   

  uint8_t data[32];     // max payload
  uint8_t len = 0;

  // First ISO-TP frame + length
  data[len++] = 0x10;   // First ISO-TP frame
  data[len++] = 0x0E;   // 14 bytes total (header + text)

  // Display header structure
  data[len++] = mode;
  data[len++] = rdsIcon;
  data[len++] = 0x55;         // unknown/fixed
  data[len++] = sourceIcon;
  data[len++] = textFormat;
  data[len++] = 0x01;         // control byte


    // Sanitize and pad text to 14 bytes
    char paddedText[15] = {0};  // null-terminated buffer
    strncpy(paddedText, text, 14);
    for (uint8_t i = 0; i < 14; i++) {
        //if (paddedText[i] == '_') paddedText[i] = ' ';
        data[len++] = paddedText[i];
    }


  return affa3_send(0x151, data, len);
  // Possibly allows longer or formatted text
  return AffaCommon::AffaError::NoError;
}

void showConfirmBoxWithOffsets(
    const char *caption,
    const char *row1,
    const char *row2)
{
  Serial.println("[showConfirmBoxWithOffsets] --- Sending Custom Confirm Box ---");

  // ISO-TP total payload: 112 bytes (16 frames × 7 bytes)
  uint8_t payload[112] = {0}; // Initialize all bytes to 0
  uint8_t currentFillEnd = 0; // Tracks where the last write ended

  // Insert button caption at offset 0x1A (max 7 characters)
  for (uint8_t i = 0; i < 7 && caption[i]; i++)
  {
    payload[0x1A + i] = caption[i];
  }

  // Insert rows with 0x20 between them, starting at 0x20
  uint8_t offset = 0x20;

  auto insertRow = [&](const char *text)
  {
    while (*text && offset < 0x36)
    {
      payload[offset++] = *text++;
    }
    // Add 0x20 to separate rows (unless last one)
    if (offset < 0x36)
    {
      payload[offset++] = 0xD;
    }
  };
  insertRow(row1);
  insertRow(row2);

  // Now send CAN frames
  // First frame (0x10): initialize ISO-TP with first data byte (0x6F)
  CanUtils::sendCan(0x151, 0x10, 0x6F, 0x21, 0x05, 0x00, 0x00, 0x01, 0x49);

  uint8_t payloadIndex = 0;

  // Now send 15 more frames: 0x21 to 0x2F
  for (uint8_t i = 0; i < 15; i++)
  {
    uint8_t pci = 0x21 + i; // Frame identifier
    uint8_t data[8] = {0};  // Data array to store 8 bytes

    data[0] = pci; // First byte is the frame identifier
    // Fill the remaining 7 bytes with payload data, from the correct offset
    for (uint8_t j = 0; j < 7; j++)
    {

      data[j + 1] = payload[payloadIndex++];
    }

    // Send the CAN frame with the correct data
    CanUtils::sendCan(0x151, data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7]);

    // Debugging: Print the CAN message sent
    Serial.printf("  [CAN] %03X -> %02X %02X %02X %02X %02X %02X %02X %02X\n",
                  0x151, data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7]);

    delay(5); // Small delay between frames
  }

  Serial.println("[showConfirmBoxWithOffsets] --- Done ---");
}

//menuit 41 44 48 77 AUX__AUTO AFAUTO SPEED_0
void showInfoMenu(
    const char *item1,
    const char *item2,
    const char *item3,
    uint8_t offset1,// 0x41
    uint8_t offset2,// 0x44
    uint8_t offset3,// 0x48 -- dont know what changing, maybe itr constants
    uint8_t infoPrefix)  //same as with text, 
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
 
AffaCommon::AffaError Affa3NavDisplay::setState(bool enabled)
{
  Affa3Nav::DisplayCtrl state = enabled ? Affa3Nav::DisplayCtrl::Enable : Affa3Nav::DisplayCtrl::Disable;
  
		uint8_t data[] = {
			0x3, 0x52, static_cast<uint8_t>(state), 0xFF, 0xFF};// sc 151 3 52 9 0 0 0 0 0

		return affa3_send(Affa3Nav::PACKET_ID_DISPLAY_CTRL, data, sizeof(data));
}


AffaCommon::AffaError Affa3NavDisplay::setTime(const char *clock)
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


//SCROLL LOCK INDICATOR
// 0x00 - no scroll lock, 0x07 - scroll UP -0x0B - scroll DOWN, 0x0C - scroll UP and DOWN
AffaCommon::AffaError Affa3NavDisplay::showMenu(const char *header, const char *item1, const char *item2,   Affa3Nav::ScrollLockIndicator scrollLockIndicator)
{ 
    Serial.println("[showMenu] --- Building Menu ---");
    Serial.printf("[Header] %s\n[Item1] %s\n[Item2] %s\n", header, item1, item2);
    uint8_t selectionItem1 = 0x01;//unknown, to test
    uint8_t selectionItem2 = 0x00;//unknown, to test
  
    uint8_t payload[96] = {0};
    int idx = 0;
  
    payload[idx++] = 0x10;// First ISO-TP frame
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
  
    //int maxHeaderLen = 26;
    // int h = 0;
    // while (header && header[h] && h < maxHeaderLen && idx < 96) {
    //   payload[idx++] = header[h++];
    // }
    // int maxHeaderLen = 26;
    // for (int i = 0; i < maxHeaderLen && header[i]; ++i) {
    //     payload[idx++] = header[i];
    // }
     // Copy header (max 26 bytes)
    int h = 0;
    for (; h < 26 && header[h]; ++h) {
        payload[idx++] = header[h];
    }

    // Padding to reach item1
    // while (idx < 35) payload[idx++] = 0x00;

    // Adjusted paddings to account for 2-byte header
    while (idx < 37) payload[idx++] = 0x00;
  
    // Item 1
    payload[idx++] = selectionItem1;
    payload[idx++] = 0x7E; //maybe this is ID for item, and to select we need to send that id? check it with infomenu later
    // for (int i = 0; i < 25 && item1[i]; ++i) {
    //         payload[idx++] = item1[i];
    //     }

    while (*item1 && idx < 64) payload[idx++] = *item1++;
    // Padding to reach item2
    while (idx < 64) payload[idx++] = 0x00;
    // while (idx < 62) payload[idx++] = 0x00;
  
    payload[idx++] = selectionItem2;
    payload[idx++] = 0x7F;
    while (*item2 && idx < 96) payload[idx++] = *item2++;
    while (idx < 96) payload[idx++] = 0x00;
  
    // Final length check
    int totalLen = idx;

    Serial.printf("[CAN] do_send totalLen: %d  \n", totalLen ); 
    return affa3_send(0x151, payload, totalLen);
  
}

AffaCommon::AffaError Affa3NavDisplay::showConfirmBoxWithOffsets(const char *caption, const char *row1, const char *row2)
{
    return AffaCommon::AffaError();
}

AffaCommon::AffaError Affa3NavDisplay::showInfoMenu(const char *item1, const char *item2, const char *item3, uint8_t offset1, uint8_t offset2, uint8_t offset3, uint8_t infoPrefix)
{
    return AffaCommon::AffaError();
}
