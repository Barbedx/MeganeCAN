#include "Affa3NavDisplay.h"
// #include "Affa3Display.h"  //   (false) -> Ensure this is included if emulateKey is from there NO,
// we dont need that file here, Affa3NavDisplay is a separate display class
#include "AuxModeTracker.h"
#include <NimBLEDevice.h>
#include <vector>
 
#include <queue>

inline void AFFA3_PRINT(const char *fmt, ...)
{
  // #ifdef DEBUG
  va_list args;
  va_start(args, fmt);
  vprintf(fmt, args);
  va_end(args);
  // #endif
}

BleKeyboard bleKeyboard("MeganeCAN", "gycer", 100);

int windowFirstItemIndex = 0;

namespace
{

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

}

void Affa3NavDisplay::onKeyPressed(AffaCommon::AffaKey key, bool isHold)
{
  Serial.print("onKeyPressed fired, key:");
  Serial.println(static_cast<uint16_t>(key), HEX);
  if (isHold && key == AffaCommon::AffaKey::Load)
  {
    Serial.println(">> Load (hold) pressed - doing stuff");
    // setState(true);
    tracker.SetAuxMode(true);
  }

  mainMenu.handleKey(key, isHold);

  if (!mainMenu.isActive() && tracker.isInAuxMode())
  {
    {
      // Not in menu, but AUX mode: BLE media control
      Serial.println("Key in AUX mode:");
      switch (key)
      {
      case AffaCommon::AffaKey::Pause:
        Serial.println("Pause/Play");
        bleKeyboard.write(KEY_MEDIA_PLAY_PAUSE);
        break;

      case AffaCommon::AffaKey::RollUp:
        Serial.println("Next Track");
        bleKeyboard.write(KEY_MEDIA_NEXT_TRACK);
        break;

      case AffaCommon::AffaKey::RollDown:
        Serial.println("Previous Track");
        bleKeyboard.write(KEY_MEDIA_PREVIOUS_TRACK);
        break;

      default:
        Serial.print("Unhandled key: 0x");
        Serial.println(static_cast<uint16_t>(key), HEX);
        break;
      }
    }
  }
}

void Affa3NavDisplay::tick()
{

  struct CAN_FRAME packet;
  static int8_t timeout = SYNC_TIMEOUT;

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
    Other
  } type;
  AffaCommon::AffaKey key;
  bool isHold;
};

std::queue<Event> eventQueue;

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
  if (packet->id == Affa3Nav::PACKET_ID_SETTEXT)
  { /* Pakiet z danymi text */
    tracker.onCanMessage(*packet);
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

  bool answerNeeded = false;//TODO:move to settings

  if (answerNeeded)
  {
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

  if (packet->id == Affa3Nav::PACKET_ID_KEYPRESSED) // TODO CHECK IT
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

void Affa3NavDisplay::processEvents()
{
  while (!eventQueue.empty())
  {
    Event e = eventQueue.front();
    eventQueue.pop();

    switch (e.type)
    {
    case Event::KeyPress:

      Affa3NavDisplay::onKeyPressed(e.key, e.isHold); // safe to call affa3_send here
      break;
      // future: other event types
    }
  }
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

AffaCommon::AffaError Affa3NavDisplay::setState(bool enabled)
{
  Affa3Nav::DisplayCtrl state = enabled ? Affa3Nav::DisplayCtrl::Enable : Affa3Nav::DisplayCtrl::Disable;

  uint8_t data[] = {
      0x3, 0x52, static_cast<uint8_t>(state), 0xFF, 0xFF}; // sc 151 3 52 9 0 0 0 0 0

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
AffaCommon::AffaError Affa3NavDisplay::highlightItem(uint8_t id)
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
//  0x00 - no scroll lock, 0x07 - scroll UP -0x0B - scroll DOWN, 0x0C - scroll UP and DOWNAffa3Nav::ScrollLockIndicator scrollLockIndicator
AffaCommon::AffaError Affa3NavDisplay::showMenu(const char *header, const char *item1, const char *item2, uint8_t scrollLockIndicator)
{
  Serial.println("[showMenu] --- Building Menu ---");
  Serial.printf("[Header] %s\n[Item1] %s\n[Item2] %s\n", header, item1, item2);
  // uint8_t selectionItem1 = 0x00;//unknown, to test
  // uint8_t selectionItem2 = 0x01;//unknown, to test

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

AffaCommon::AffaError Affa3NavDisplay::showConfirmBoxWithOffsets(const char *caption, const char *row1, const char *row2)
{
  return AffaCommon::AffaError();
}

AffaCommon::AffaError Affa3NavDisplay::showInfoMenu(const char *item1, const char *item2, const char *item3, uint8_t offset1, uint8_t offset2, uint8_t offset3, uint8_t infoPrefix)
{
  return AffaCommon::AffaError();
}
