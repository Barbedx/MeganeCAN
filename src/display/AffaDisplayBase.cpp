#include "AffaDisplayBase.h"
#include "AffaCommonConstants.h" /* Constants related to Affa3 */
#include "../utils/AffaDebug.h"

using FuncStatus = AffaCommon::FuncStatus;
using SyncStatus = AffaCommon::SyncStatus;
using AffaError = AffaCommon::AffaError;
 
AffaError AffaDisplayBase::affa3_do_send(uint8_t idx, uint8_t *data, uint8_t len)
  {
    Serial.println("affa3_do_send called");
    struct CAN_FRAME packet;
    uint8_t i, stat, num = 0, left = len;
    int16_t timeout;

    if (hasFlag(_sync_status, SyncStatus::FAILED))
      return AffaError::NoSync;

    while (left > 0)
    {
      i = 0;

      packet.id = funcs[idx].id;
      packet.length = AffaCommon::PACKET_LENGTH;

      if (num > 0)
      {
        packet.data.uint8[i++] = 0x20 + num;
      }

      while ((i < AffaCommon::PACKET_LENGTH) && (left > 0))
      {
        packet.data.uint8[i++] = *data++;
        left--;
      }

      for (; i < AffaCommon::PACKET_LENGTH; i++)
      {
        packet.data.uint8[i] = getPacketFiller();  //Affa3Nav::PACKET_FILLER;
      }

      AFFA3_PRINT("Sending packet #%d to ID 0x%03X: ", num, packet.id);
      for (int j = 0; j < packet.length; j++)
        AFFA3_PRINT("%02X ", packet.data.uint8[j]);
      AFFA3_PRINT("\n");

      funcs[idx].stat = FuncStatus::WAIT;

      CanUtils::sendFrame(packet);

      /* Czkekamy na odpowiedź */
      timeout = 2000; /* 2sek */
      uint16_t wait_counter = 0;
      while ((funcs[idx].stat == FuncStatus::WAIT) && (--timeout > 0))
      {
        delay(1);

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
    if (!hasFlag(_sync_status, SyncStatus::FUNCSREG))
    {
      //	AFFA3_PRINT("[send] Registering supported functions...\n");

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
