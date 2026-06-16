#include "AffaDisplayBase.h"
#include "AffaCommonConstants.h" /* Constants related to Affa3 */
#include "../utils/AffaDebug.h"

using FuncStatus = AffaCommon::FuncStatus;
using SyncStatus = AffaCommon::SyncStatus;
using AffaError = AffaCommon::AffaError;

// The single definition of the AFFA3_PRINT runtime gate (declared extern in
// utils/AffaDebug.h). Lives here because this TU is compiled in BOTH the firmware and
// the native test builds; the `vb` serial command flips it. OFF by default so a
// continuous now-playing re-render doesn't flood/stall the USB-CDC serial link.
volatile bool g_affaVerbose = false;
 
AffaError AffaDisplayBase::affa3_do_send(uint8_t idx, uint8_t *data, uint8_t len)
  {
    AFFA3_PRINT("affa3_do_send called\n");
    // Portable Frame, sent through the injected bus — no vendor CAN_FRAME / CanUtils,
    // so this (the AFFA3 ISO-TP send + ACK loop) builds + unit-tests on the host.
    Frame packet;
    uint8_t i, stat, num = 0, left = len;
    int16_t timeout;

    if (!_skipFuncReg && hasFlag(_sync_status, SyncStatus::FAILED))
      return AffaError::NoSync;

    while (left > 0)
    {
      i = 0;

      packet.id = funcs[idx].id;
      packet.len = AffaCommon::PACKET_LENGTH;
      packet.extended = false;

      if (num > 0)
      {
        packet.data[i++] = 0x20 + num;
      }

      while ((i < AffaCommon::PACKET_LENGTH) && (left > 0))
      {
        packet.data[i++] = *data++;
        left--;
      }

      for (; i < AffaCommon::PACKET_LENGTH; i++)
      {
        packet.data[i] = getPacketFiller();  //Affa3Nav::PACKET_FILLER;
      }

      AFFA3_PRINT("Sending packet #%d to ID 0x%03X: ", num, packet.id);
      for (int j = 0; j < packet.len; j++)
        AFFA3_PRINT("%02X ", packet.data[j]);
      AFFA3_PRINT("\n");

      funcs[idx].stat = FuncStatus::WAIT;

      if (_bus)
        _bus->send(packet);

      // Bench emulator self-ACK: no real display answers, so acknowledge our own
      // frame here (PARTIAL while bytes remain, DONE on the last). `left` is already
      // this frame's remainder. The wait loop below then exits immediately and the
      // normal PARTIAL/DONE handling sends the next frame / finishes — so the whole
      // real AFFA3 sequence is emitted for the PC emulator instead of just frame 0.
      if (_emuSelfAck)
        funcs[idx].stat = (left > 0) ? FuncStatus::PARTIAL : FuncStatus::DONE;

      /* Czkekamy na odpowiedź */
      timeout = 2000; /* 2sek */
      uint16_t wait_counter = 0;
      while ((funcs[idx].stat == FuncStatus::WAIT) && (--timeout > 0))
      {
        if (_clock) _clock->delayMs(1);

        // Log every 500ms
        if (wait_counter++ % 500 == 0)
        {
          AFFA3_PRINT("Waiting... %dms elapsed for packet #%d (ID: 0x%03X)\n", wait_counter, num, packet.id);
        }
      }

      stat = funcs[idx].stat;
      funcs[idx].stat = FuncStatus::IDLE;

      if (!timeout)
      { /* Nie dostaliśmy odpowiedzi */
        AFFA3_PRINT("affa3_send(): timeout, num = %d\n", num);
        return AffaError::Timeout;
      }

      if (stat == FuncStatus::DONE)
      {
        AFFA3_PRINT("affa3_send(): DONE received on packet #%d\n", num);
        break;
      }
      else if (stat == FuncStatus::PARTIAL)
      {
        AFFA3_PRINT("affa3_send(): PARTIAL ack on packet #%d, remaining: %d bytes\n", num, left);
        if (!left)
        { /* Nie mamy więcej danych */
          AFFA3_PRINT("affa3_send(): no more data\n");
          return AffaError::SendFailed;
        }
        num++;
      }
      else if (stat == FuncStatus::ERROR)
      {
        AFFA3_PRINT("affa3_send(): ERROR received on packet #%d\n", num);
        return AffaError::SendFailed;
      }
    }

    return AffaError::NoError;
  }

AffaError AffaDisplayBase::affa3_send(uint16_t id, uint8_t *data, uint8_t len)
  {
    uint8_t idx;
    uint8_t regdata[1] = {0x70};
    AffaError err;

    // if ((_sync_status & AFFA3_SYNC_STAT_FUNCSREG) != AFFA3_SYNC_STAT_FUNCSREG)
    if (_skipFuncReg)
    {
      AFFA3_PRINT("[send] skipFuncReg: skipping registration for 0x%03X, sending direct\n", id);
    }
    else if (!hasFlag(_sync_status, SyncStatus::FUNCSREG))
    {
      AFFA3_PRINT("[send] Registering supported functions...\n");

      for (idx = 0; idx < funcsMax; idx++)
      {
        AFFA3_PRINT("[send] Registering func ID 0x%X\n", funcs[idx].id);

        err = affa3_do_send(idx, regdata, sizeof(regdata));
        if (err != AffaError::NoError)
        {
          //		AFFA3_PRINT("[send] Registration failed for func 0x%X, error %d\n", _funcs[idx].id, err);

          return err;
        }
      }

      _sync_status |= SyncStatus::FUNCSREG;
      // AFFA3_PRINT("[send] All functions registered.\n");
    }

    for (idx = 0; idx < funcsMax; idx++)
    {
      if (funcs[idx].id == id)
        break;
    }

    if (idx >= funcsMax)
    {
      // AFFA3_PRINT("[send] Unknown function ID: 0x%X\n", id);

      return AffaError::UnknownFunc;
    }
    // AFFA3_PRINT("[send] Sending data to func 0x%X, length: %d\n", id, len);

    return affa3_do_send(idx, data, len);
  }
