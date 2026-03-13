#include "UpdateListBase.h"
#include <string.h>

inline void AFFA2_PRINT(const char *fmt, ...)
{
#ifdef DEBUG
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
#endif
}

void UpdateListBase::sendAliveFrame()
{
    CanUtils::sendCan(UpdateList::PACKET_ID_SYNC, 0x79, 0x00,
                      UpdateList::PACKET_FILLER, UpdateList::PACKET_FILLER,
                      UpdateList::PACKET_FILLER, UpdateList::PACKET_FILLER,
                      UpdateList::PACKET_FILLER, UpdateList::PACKET_FILLER);
}

void UpdateListBase::sendSyncRequestFrame()
{
    CanUtils::sendCan(UpdateList::PACKET_ID_SYNC, 0x7A, 0x01,
                      UpdateList::PACKET_FILLER, UpdateList::PACKET_FILLER,
                      UpdateList::PACKET_FILLER, UpdateList::PACKET_FILLER,
                      UpdateList::PACKET_FILLER, UpdateList::PACKET_FILLER);
}

AffaCommon::AffaError UpdateListBase::setState(bool enabled)
{
    UpdateList::DisplayCtrl state = enabled ? UpdateList::DisplayCtrl::Enable : UpdateList::DisplayCtrl::Disable;
    uint8_t data[] = {
        0x04, 0x52, static_cast<uint8_t>(state), 0xFF, 0xFF};

    return affa3_send(UpdateList::PACKET_ID_DISPLAY_CTRL, data, sizeof(data));
}

void UpdateListBase::recv(CAN_FRAME *packet)
{
    uint8_t i;

    if (packet->id == UpdateList::PACKET_ID_SYNC_REPLY)
    {
        Serial.printf("[UL recv] sync 0x%02X 0x%02X | _skipFuncReg=%s sync_status=0x%02X\n",
                      packet->data.uint8[0], packet->data.uint8[1],
                      _skipFuncReg ? "TRUE" : "FALSE",
                      (uint8_t)_sync_status);
        if ((packet->data.uint8[0] == 0x61) && (packet->data.uint8[1] == 0x11))
        {
            Serial.println("[UL recv] -> sync request, sending registration");
            CanUtils::sendCan(UpdateList::PACKET_ID_SYNC, 0x70, 0x1A, 0x11, 0x00, 0x00, 0x00, 0x00, 0x01);
            noteSyncRequest(packet->data.uint8[2] == 0x01, millis());
        }
        else if (packet->data.uint8[0] == 0x69)
        {
            Serial.println("[UL recv] -> peer alive 0x69");
            markPeerAlive(millis());
        }
        else
        {
            Serial.printf("[UL recv] -> unknown sync packet, ignoring\n");
        }
        return;
    }

    Serial.printf("[UL recv] non-sync ID=0x%03X data[0]=0x%02X\n", packet->id, packet->data.uint8[0]);

    if (packet->id & UpdateList::PACKET_REPLY_FLAG)
    {
        packet->id &= ~UpdateList::PACKET_REPLY_FLAG;
        for (i = 0; i < funcsMax; i++)
        {
            if (funcs[i].id == packet->id)
                break;
        }

        if ((i < funcsMax) && (funcs[i].stat == FuncStatus::WAIT))
        {
            if (packet->data.uint8[0] == 0x74)
            {
                funcs[i].stat = FuncStatus::DONE;
            }
            else if ((packet->data.uint8[0] == 0x30) && (packet->data.uint8[1] == 0x01) && (packet->data.uint8[2] == 0x00))
            {
                funcs[i].stat = FuncStatus::PARTIAL;
            }
            else
            {
                funcs[i].stat = FuncStatus::ERROR;
            }
        }
        return;
    }

    // Text frame from radio (0x121) — we never receive our own sends on CAN.
    // First frame has no counter prefix: data[0]=0x10 (set text command).
    // AUX detection: check bytes 5-7 for 'A','U','X' (0x41,0x55,0x58) in 0x19 format,
    // where chan/loc occupy bytes 3-4 and old-text starts at byte 5.
    if (packet->id == UpdateList::PACKET_ID_SETTEXT)
    {
        bool isAux = false;
        if (packet->data.uint8[0] == 0x10 && packet->data.uint8[1] == 0x19)
        {
            isAux = (packet->data.uint8[5] == 'A' &&
                     packet->data.uint8[6] == 'U' &&
                     packet->data.uint8[7] == 'X');
        }
        Serial.printf("[UL recv] radio SETTEXT, isAux=%d\n", isAux);
        onRadioText(isAux);
        return; // do NOT auto-reply — this frame was addressed to the display, not to us
    }

    if (packet->id == UpdateList::PACKET_ID_KEYPRESSED)
    {
        if ((packet->data.uint8[0] == 0x03) && (packet->data.uint8[1] != 0x89))
            return; // malformed key packet — no reply (matches archive behavior)

        if ((packet->data.uint8[0] == 0x03) && (packet->data.uint8[1] == 0x89))
        {
            // valid key — extract and queue, then fall through to auto-reply like archive
            uint8_t highByte = packet->data.uint8[2];
            uint8_t lowByte  = packet->data.uint8[3];
            uint16_t rawKey  = ((uint16_t)highByte << 8) | lowByte;

            bool isHold = false;
            uint16_t maskedKey = rawKey;
            if (!(rawKey == 0x0101 || rawKey == 0x0141))
            {
                isHold    = (lowByte & AffaCommon::KEY_HOLD_MASK) != 0;
                maskedKey = rawKey & ~AffaCommon::KEY_HOLD_MASK;
            }
            _keyQueue.push({static_cast<AffaCommon::AffaKey>(maskedKey), isHold});
        }
        // non-key 0x0A9 frames (e.g. 0x70 registration) fall through to auto-reply below
    }

    CAN_FRAME reply{};
    reply.id = packet->id | UpdateList::PACKET_REPLY_FLAG;
    reply.length = AffaCommon::PACKET_LENGTH;
    i = 0;
    reply.data.uint8[i++] = 0x74;

    for (; i < AffaCommon::PACKET_LENGTH; i++)
        reply.data.uint8[i] = UpdateList::PACKET_FILLER;

    CanUtils::sendFrame(reply);
}

AffaCommon::AffaError UpdateListBase::setText(const char *text, uint8_t digit)
{
    char oldBuf[8]  = {' '};
    char newBuf[12] = {' '};

    uint8_t chan     = (digit <= 9) ? (0x70 + digit) : 0x7A;
    uint8_t loc      = 0x01;
    uint8_t textType = 0x76;

    strncpy(oldBuf, text, sizeof(oldBuf));
    strncpy(newBuf, text, sizeof(newBuf));

    uint8_t data[32];
    uint8_t i, len = 0;

    data[len++] = 0x10;
    data[len++] = 0x19;
    data[len++] = textType;
    data[len++] = chan;
    data[len++] = loc;

    for (i = 0; i < 8; i++)
        data[len++] = oldBuf[i];

    data[len++] = 0x10;

    for (i = 0; i < 12; i++)
        data[len++] = newBuf[i];

    data[len++] = 0x00;
    data[len++] = 0x81;
    data[len++] = 0x81;

    return affa3_send(UpdateList::PACKET_ID_SETTEXT, data, len);
}

AffaCommon::AffaError UpdateListBase::setTime(const char *clock)
{
    return AffaCommon::AffaError::NoError;
}

void UpdateListBase::processEvents()
{
    while (!_keyQueue.empty())
    {
        KeyEvent e = _keyQueue.front();
        _keyQueue.pop();
        ProcessKey(e.key, e.isHold);
    }
}

void UpdateListBase::ProcessKey(AffaCommon::AffaKey key, bool isHold)
{
    // Hold-Load toggles AMS key forwarding regardless of _amsKeysEnabled state.
    if (isHold && key == AffaCommon::AffaKey::Load)
    {
        _amsKeysEnabled = !_amsKeysEnabled;
        const char *msg = _amsKeysEnabled ? "AMS  ON " : "AMS OFF ";
        Serial.printf("[UL] AMS keys %s\n", _amsKeysEnabled ? "enabled" : "disabled");
        showTransientText(msg, 1200);
        return;
    }

    if (_amsKeysEnabled && keyHandler)
        keyHandler(key, isHold);
}

void UpdateListBase::showTransientText(const char *text, uint32_t durationMs)
{
    strncpy(_transientText, text, sizeof(_transientText) - 1);
    _transientText[sizeof(_transientText) - 1] = '\0';
    _transientUntilMs = millis() + durationMs;
    setText(_transientText);
}
