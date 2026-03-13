#include "AffaDisplayBase.h"

#include "../utils/AffaDebug.h"

void AffaDisplayBase::tick()
{
    const uint32_t now = millis();

    tickSync(now);
    onTick(now);
    processEvents();
    tickMedia();
    tickTx();
}

void AffaDisplayBase::tickSync(uint32_t now)
{
    if (_skipFuncReg)
        return;

    if (_lastSyncTickMs != 0 && (now - _lastSyncTickMs) < getSyncIntervalMs())
        return;

    _lastSyncTickMs = now;
    sendAliveFrame();

    if (hasFlag(_sync_status, SyncStatus::FAILED) || hasFlag(_sync_status, SyncStatus::START))
    {
        if (shouldProactivelyRequestSync())
        {
            AFFA3_PRINT("[sync] handshake requested, sending sync request\n");
            sendSyncRequestFrame();
            _sync_status &= ~SyncStatus::START;
        }
        return;
    }

    if (_lastPeerAliveMs == 0 || (now - _lastPeerAliveMs) > getPeerTimeoutMs())
    {
        AFFA3_PRINT("[sync] peer timeout\n");
        markSyncLost();
    }
}

void AffaDisplayBase::markPeerAlive(uint32_t now)
{
    _lastPeerAliveMs = now;
    _sync_status |= SyncStatus::PEER_ALIVE;
    _sync_status &= ~SyncStatus::FAILED;
}

void AffaDisplayBase::noteSyncRequest(bool startRequested, uint32_t now)
{
    _lastPeerAliveMs = now;
    _sync_status &= ~SyncStatus::FAILED;
    if (startRequested && shouldProactivelyRequestSync())
        _sync_status |= SyncStatus::START;
    else
        _sync_status &= ~SyncStatus::START;
}

void AffaDisplayBase::markSyncLost()
{
    _sync_status |= SyncStatus::FAILED;
    _sync_status &= ~SyncStatus::FUNCSREG;
}

void AffaDisplayBase::resetSyncSchedule()
{
    _lastSyncTickMs = 0;
    _lastPeerAliveMs = 0;
}

void AffaDisplayBase::affa3_finish_tx(AffaError result)
{
    if (_tx.currentFuncIdx < funcsMax)
        funcs[_tx.currentFuncIdx].stat = FuncStatus::IDLE;

    _tx.active = false;
    _tx.waitingAck = false;
    _tx.registrationPhase = false;
    _tx.result = result;
}

bool AffaDisplayBase::affa3_start_tx(uint8_t targetFuncIdx, const uint8_t *data, uint8_t len)
{
    if (len > AFFA3_MAX_TX_DATA)
    {
        AFFA3_PRINT("[tx] payload too large: %u\n", len);
        return false;
    }

    _tx = {};
    _tx.active = true;
    _tx.registrationPhase = (!_skipFuncReg && !hasFlag(_sync_status, SyncStatus::FUNCSREG));
    _tx.currentFuncIdx = _tx.registrationPhase ? 0 : targetFuncIdx;
    _tx.targetFuncIdx = targetFuncIdx;
    _tx.dataLen = len;

    if (data && len > 0)
        memcpy(_tx.data, data, len);

    AFFA3_PRINT("[tx] started: target idx=%u regPhase=%d len=%u\n",
                targetFuncIdx, _tx.registrationPhase ? 1 : 0, len);
    return true;
}

bool AffaDisplayBase::affa3_queue_next_packet()
{
    if (!_tx.active)
        return false;

    CAN_FRAME packet{};
    uint8_t i = 0;

    packet.id = funcs[_tx.currentFuncIdx].id;
    packet.length = AffaCommon::PACKET_LENGTH;

    if (_tx.registrationPhase)
    {
        packet.data.uint8[i++] = 0x70;
    }
    else
    {
        if (_tx.seqNum > 0)
            packet.data.uint8[i++] = 0x20 + _tx.seqNum;

        while ((i < AffaCommon::PACKET_LENGTH) && (_tx.dataOffset < _tx.dataLen))
            packet.data.uint8[i++] = _tx.data[_tx.dataOffset++];
    }

    for (; i < AffaCommon::PACKET_LENGTH; i++)
        packet.data.uint8[i] = getPacketFiller();

    funcs[_tx.currentFuncIdx].stat = FuncStatus::WAIT;

    if (!CanUtils::enqueueFrame(packet))
    {
        funcs[_tx.currentFuncIdx].stat = FuncStatus::IDLE;
        affa3_finish_tx(AffaError::SendFailed);
        return false;
    }

    _tx.waitingAck = true;
    _tx.ackDeadlineMs = millis() + AFFA3_ACK_TIMEOUT_MS;
    return true;
}

void AffaDisplayBase::tickTx()
{
    if (!_tx.active)
        return;

    if (!_tx.waitingAck)
    {
        affa3_queue_next_packet();
        return;
    }

    FuncStatus stat = funcs[_tx.currentFuncIdx].stat;

    if (stat == FuncStatus::WAIT)
    {
        if ((int32_t)(millis() - _tx.ackDeadlineMs) < 0)
            return;

        AFFA3_PRINT("[tx] timeout on func idx=%u seq=%u\n", _tx.currentFuncIdx, _tx.seqNum);
        affa3_finish_tx(AffaError::Timeout);
        return;
    }

    funcs[_tx.currentFuncIdx].stat = FuncStatus::IDLE;
    _tx.waitingAck = false;

    if (_tx.registrationPhase)
    {
        if (stat == FuncStatus::DONE)
        {
            _tx.currentFuncIdx++;
            if (_tx.currentFuncIdx >= funcsMax)
            {
                _sync_status |= SyncStatus::FUNCSREG;
                _tx.registrationPhase = false;
                _tx.currentFuncIdx = _tx.targetFuncIdx;
                _tx.seqNum = 0;
                _tx.dataOffset = 0;
            }
            return;
        }

        affa3_finish_tx(AffaError::SendFailed);
        return;
    }

    if (stat == FuncStatus::DONE)
    {
        affa3_finish_tx(AffaError::NoError);
        return;
    }

    if (stat == FuncStatus::PARTIAL)
    {
        if (_tx.dataOffset >= _tx.dataLen)
        {
            affa3_finish_tx(AffaError::SendFailed);
            return;
        }
        _tx.seqNum++;
        return;
    }

    affa3_finish_tx(AffaError::SendFailed);
}

AffaError AffaDisplayBase::affa3_send(uint16_t id, uint8_t *data, uint8_t len)
{
    if (!_skipFuncReg && hasFlag(_sync_status, SyncStatus::FAILED))
        return AffaError::NoSync;

    if (_tx.active)
    {
        AFFA3_PRINT("[send] tx busy\n");
        return AffaError::SendFailed;
    }

    uint8_t idx;
    for (idx = 0; idx < funcsMax; idx++)
    {
        if (funcs[idx].id == id)
            break;
    }

    if (idx >= funcsMax)
        return AffaError::UnknownFunc;

    if (!affa3_start_tx(idx, data, len))
        return AffaError::SendFailed;

    return AffaError::NoError;
}
