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

void UpdateListBase::tick()
{
    // In radio mode the radio owns sync — ESP32 only injects data, never sends sync packets.
    if (_skipFuncReg) return;

    struct CAN_FRAME packet;
    static int8_t timeout = SYNC_TIMEOUT;

    CanUtils::sendCan(UpdateList::PACKET_ID_SYNC, 0x79, 0x00, UpdateList::PACKET_FILLER, UpdateList::PACKET_FILLER, UpdateList::PACKET_FILLER, UpdateList::PACKET_FILLER, UpdateList::PACKET_FILLER, UpdateList::PACKET_FILLER);

    if (hasFlag(_sync_status, SyncStatus::FAILED) || hasFlag(_sync_status, SyncStatus::START))
    {
        AFFA2_PRINT("[tick] Sync failed or requested, sending sync request\n");
        CanUtils::sendCan(UpdateList::PACKET_ID_SYNC, 0x7A, 0x01, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81);
        _sync_status &= ~SyncStatus::START;
        delay(100);
    }
    else
    {
        if (hasFlag(_sync_status, SyncStatus::PEER_ALIVE))
        {
            timeout = SYNC_TIMEOUT;
            _sync_status &= ~SyncStatus::PEER_ALIVE;
        }
        else
        {
            timeout--;
            AFFA2_PRINT("[tick] Waiting for peer... timeout in %d\n", timeout);
            if (timeout <= 0)
            {
                _sync_status = SyncStatus::FAILED;
                _sync_status &= ~SyncStatus::FUNCSREG;
                AFFA2_PRINT("ping timeout!\n");
            }
        }
    }
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
        if ((packet->data.uint8[0] == 0x61) && (packet->data.uint8[1] == 0x11))
        {
            CanUtils::sendCan(UpdateList::PACKET_ID_SYNC, 0x70, 0x1A, 0x11, 0x00, 0x00, 0x00, 0x00, 0x01);
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

    if (packet->id == UpdateList::PACKET_ID_KEYPRESSED)
    {
        if (!((packet->data.uint8[0] == 0x03) && (packet->data.uint8[1] == 0x89)))
            return;

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
        return; // key packet needs no CAN reply
    }

    struct CAN_FRAME reply;
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
