
#pragma once // or use include guards instead
// #include "affa3.h"
#include "Affa3Display.h"
#include <string.h>

inline void AFFA3_PRINT(const char *fmt, ...)
{
#ifdef DEBUG
	va_list args;
	va_start(args, fmt);
	vprintf(fmt, args);
	va_end(args);
#endif
}

using SyncStatus = Affa3::SyncStatus;
using FuncStatus = Affa3::FuncStatus;
using Affa3Error = Affa3::Affa3Error;

namespace
{
	static SyncStatus _sync_status = SyncStatus::FAILED;
	static uint8_t _menu_max_items = 0; /* Status synchronizacji z wświetlaczem */
	constexpr int AFFA3_PING_TIMEOUT = 5;
	constexpr size_t AFFA3_KEY_QUEUE_SIZE = 8;

	// Definition for affa3_func struct
	struct Affa3Func
	{
		uint16_t id;
		FuncStatus stat;
	};
	Affa3Func funcs[] = {
		{Affa3::PACKET_ID_SETTEXT, FuncStatus::IDLE},
		{Affa3::PACKET_ID_DISPLAY_CTRL, FuncStatus::IDLE}};

	// #define FUNCS_MAX (sizeof(_funcs) / sizeof(struct affa3_func))??
	constexpr size_t funcsMax = sizeof(funcs) / sizeof(funcs[0]);

	static uint16_t _key_q[AFFA3_KEY_QUEUE_SIZE] = {
		0,
	};
	static uint8_t _key_q_in = 0;
	static uint8_t _key_q_out = 0;
	bool isKeyQueueFull() { return ((_key_q_in + 1) % AFFA3_KEY_QUEUE_SIZE) == _key_q_out; }
	bool isKeyQueueEmpty() { return _key_q_in == _key_q_out; }

	static Affa3Error affa3_do_send(uint8_t idx, uint8_t *data, uint8_t len)
	{
		struct CAN_FRAME packet;
		uint8_t i, stat, num = 0, left = len;
		int16_t timeout;

		if (hasFlag(_sync_status, SyncStatus::FAILED))
			return Affa3Error::NoSync;

		while (left > 0)
		{
			i = 0;

			packet.id = funcs[idx].id;
			packet.length = Affa3::PACKET_LENGTH;

			if (num > 0)
			{
				packet.data.uint8[i++] = 0x20 + num;
			}

			while ((i < Affa3::PACKET_LENGTH) && (left > 0))
			{
				packet.data.uint8[i++] = *data++;
				left--;
			}

			for (; i < Affa3::PACKET_LENGTH; i++)
			{
				packet.data.uint8[i] = Affa3::PACKET_FILLER;
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
				return Affa3Error::Timeout;
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
					return Affa3Error::SendFailed;
				}
				num++;
			}
			else if (stat == FuncStatus::ERROR)
			{
				AFFA3_PRINT("affa3_send(): ERROR received on packet #%d\n", num);
				return Affa3Error::SendFailed;
			}
		}

		return Affa3Error::NoError;
	}

	static Affa3Error affa3_send(uint16_t id, uint8_t *data, uint8_t len)
	{
		uint8_t idx;
		uint8_t regdata[1] = {0x70};
		Affa3Error err;

		// if ((_sync_status & AFFA3_SYNC_STAT_FUNCSREG) != AFFA3_SYNC_STAT_FUNCSREG)
		if (!hasFlag(_sync_status, SyncStatus::FUNCSREG))
		{
			//	AFFA3_PRINT("[send] Registering supported functions...\n");

			for (idx = 0; idx < funcsMax; idx++)
			{
				AFFA3_PRINT("[send] Registering func ID 0x%X\n", funcs[idx].id);

				err = affa3_do_send(idx, regdata, sizeof(regdata));
				if (err != Affa3Error::NoError)
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

			return Affa3Error::UnknownFunc;
		}
		// AFFA3_PRINT("[send] Sending data to func 0x%X, length: %d\n", id, len);

		return affa3_do_send(idx, data, len);
	}
	Affa3Error affa3_display_ctrl(Affa3::DisplayCtrl state)
	{
		uint8_t data[] = {
			0x04, 0x52, static_cast<uint8_t>(state), 0xFF, 0xFF};

		return affa3_send(Affa3::PACKET_ID_DISPLAY_CTRL, data, sizeof(data));
	}

	Affa3Error affa3_do_set_text(uint8_t icons, uint8_t mode, uint8_t chan, uint8_t loc, uint8_t textType, char old[8], char neww[12])
	{

		static uint8_t old_icons = 0xFF;
		static uint8_t old_mode = 0x00;
		uint8_t data[32];
		uint8_t i, len = 0;
		// TODO: Check if need newone
		data[len++] = 0x10; /* Ustaw tekst */
		if ((icons != old_icons) || (mode != old_mode))
		{
			data[len++] = 0x1C; /* Tekst + ikony */
			data[len++] = 0x7F;
			data[len++] = icons;
			data[len++] = 0x55;
			data[len++] = mode;
		}
		else
		{
			data[len++] = 0x19;		/* Sam tekst */
			data[len++] = textType; //??? 0x76 was setted but not working value, have put 0x7E isntead //possible 0x7E will add channel, but ox76 will not
		}

		data[len++] = chan; // 0x70<chan<0x7A  0..9+null;
		data[len++] = loc;	// 0x01 always by now

		for (i = 0; i < 8; i++)
		{
			data[len++] = old[i];
		}

		data[len++] = 0x10; /* Separator */

		for (i = 0; i < 12; i++)
		{
			data[len++] = neww[i];
		}

		data[len++] = 0x00; /* Terminator */
		data[len++] = 0x81; /* filler to try */
		data[len++] = 0x81; /*  filler to try */
							// 👇 Add debug output
							// AFFA3_PRINT("[do_set_text] ID: 0x%X, len=%u\n", AFFA3_PACKET_ID_SETTEXT, len);
							// for (uint8_t j = 0; j < len; j++)
							// {
							//     AFFA3_PRINT("%02X ", data[j]);
							// }
							// AFFA3_PRINT("\n");
		return affa3_send(Affa3::PACKET_ID_SETTEXT, data, len);
	}

}

bool affa3_is_synced = false; 

void Affa3Display::tick()
{

	struct CAN_FRAME packet;
	static int8_t timeout = AFFA3_PING_TIMEOUT;

	// AFFA3_PRINT("[tick] Sending alive ping\n");
	/* Wysyłamy pakiet informujący o tym że żyjemy */
	CanUtils::sendCan(Affa3::PACKET_ID_SYNC, 0x79, 0x00, Affa3::PACKET_FILLER, Affa3::PACKET_FILLER, Affa3::PACKET_FILLER, Affa3::PACKET_FILLER, Affa3::PACKET_FILLER, Affa3::PACKET_FILLER);

	if (hasFlag(_sync_status, SyncStatus::FAILED) || hasFlag(_sync_status, SyncStatus::START))
	{ /* Błąd synchronizacji */
		/* Wysyłamy pakiet z żądaniem synchronizacji */
		AFFA3_PRINT("[tick] Sync failed or requested, sending sync request\n");
		CanUtils::sendCan(Affa3::PACKET_ID_SYNC, 0x7A, 0x01, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81);
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





Affa3Error Affa3Display::setState(bool enabled)
{
	Affa3::DisplayCtrl state = enabled ? Affa3::DisplayCtrl::Enable : Affa3::DisplayCtrl::Disable;
	return affa3_display_ctrl(state);
}

void Affa3Display::recv(CAN_FRAME *packet)
{

	uint8_t i;

	if (packet->id == Affa3::PACKET_ID_SYNC_REPLY)
	{ /* Pakiety synchronizacyjne */
		if ((packet->data.uint8[0] == 0x61) && (packet->data.uint8[1] == 0x11))
		{ /* Żądanie synchronizacji */
			CanUtils::sendCan(Affa3::PACKET_ID_SYNC, 0x70, 0x1A, 0x11, 0x00, 0x00, 0x00, 0x00, 0x01);
			affa3_is_synced = false;
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

	if (packet->id & Affa3::PACKET_REPLY_FLAG)
	{
		packet->id &= ~Affa3::PACKET_REPLY_FLAG;
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

	if (packet->id == Affa3::PACKET_ID_KEYPRESSED)
	{
		if ((packet->data.uint8[0] == 0x03) && (packet->data.uint8[1] != 0x89)) /* Błędny pakiet */
			return;

		if (!isKeyQueueFull())
		{
			_key_q[_key_q_in] = (packet->data.uint8[2] << 8) | packet->data.uint8[3];
			printf_P(PSTR("AFFA2: key code: 0x%X\n"), _key_q[_key_q_in]);
			_key_q_in = (_key_q_in + 1) % AFFA3_KEY_QUEUE_SIZE;
		}
	}

	struct CAN_FRAME reply;
	/* Wysyłamy odpowiedź */
	reply.id = packet->id | Affa3::PACKET_REPLY_FLAG;
	reply.length = Affa3::PACKET_LENGTH;
	i = 0;
	reply.data.uint8[i++] = 0x74;

	for (; i < Affa3::PACKET_LENGTH; i++)
		reply.data.uint8[i] = Affa3::PACKET_FILLER;

	CanUtils::sendFrame(reply);
}

Affa3Error Affa3Display::setText(const char *text, uint8_t digit /* 0-9, or anything else for none */)
{
	char oldBuf[8] = {' '};
	char newBuf[12] = {' '};

	// Prepare channel byte
	uint8_t chan = (digit <= 9 && digit >= 0) ? (0x70 + digit) : 0x7A;

	// Fill old and new buffers
	strncpy(oldBuf, text, sizeof(oldBuf));
	strncpy(newBuf, text, sizeof(newBuf));

	return affa3_do_set_text(0xFF, 0x00, chan, 0x01, 0x76, oldBuf, newBuf);
}
 
SyncStatus affa3_sync_status(void)
{
	return _sync_status;
}

Affa3::Affa3Error Affa3Display::setTime(const char *clock) {
	return Affa3::Affa3Error::NoError; // Placeholder for actual implementation
}