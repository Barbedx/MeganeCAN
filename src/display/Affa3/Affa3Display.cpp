
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
 
void Affa3Display::tick()
{

	struct CAN_FRAME packet;
	static int8_t timeout = SYNC_TIMEOUT;

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

AffaCommon::AffaError Affa3Display::setState(bool enabled)
{
	Affa3::DisplayCtrl state = enabled ? Affa3::DisplayCtrl::Enable : Affa3::DisplayCtrl::Disable;
	uint8_t data[] = {
		0x04, 0x52, static_cast<uint8_t>(state), 0xFF, 0xFF};

	return affa3_send(Affa3::PACKET_ID_DISPLAY_CTRL, data, sizeof(data));
}

void Affa3Display::recv(CAN_FRAME *packet)
{

	uint8_t i;

	if (packet->id == Affa3::PACKET_ID_SYNC_REPLY)
	{ /* Pakiety synchronizacyjne */
		if ((packet->data.uint8[0] == 0x61) && (packet->data.uint8[1] == 0x11))
		{ /* Żądanie synchronizacji */
			
			CanUtils::sendCan(Affa3::PACKET_ID_SYNC, 0x70, 0x1A, 0x11, 0x00, 0x00, 0x00, 0x00, 0x01);
 
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

		// if (!isKeyQueueFull())
		// {
		// 	_key_q[_key_q_in] = (packet->data.uint8[2] << 8) | packet->data.uint8[3];
		// 	printf_P(PSTR("AFFA2: key code: 0x%X\n"), _key_q[_key_q_in]);
		// 	_key_q_in = (_key_q_in + 1) % AFFA3_KEY_QUEUE_SIZE;
		// }
	}

	struct CAN_FRAME reply;
	/* Wysyłamy odpowiedź */
	reply.id = packet->id | Affa3::PACKET_REPLY_FLAG;
	reply.length = AffaCommon::PACKET_LENGTH;
	i = 0;
	reply.data.uint8[i++] = 0x74;

	for (; i < AffaCommon::PACKET_LENGTH; i++)
		reply.data.uint8[i] = Affa3::PACKET_FILLER;

	CanUtils::sendFrame(reply);
}

	// AffaError affa3_do_set_text(uint8_t icons, uint8_t mode, uint8_t chan, uint8_t loc, uint8_t textType, char old[8], char neww[12])
	// {

	// 	//

	// // return affa3_do_set_text(0xFF, 0x00, chan, 0x01, 0x76, oldBuf, newBuf);
	// 	static uint8_t old_icons = 0xFF;
	// 	static uint8_t old_mode = 0x00;
	// 	uint8_t data[32];
	// 	uint8_t i, len = 0;
	// 	// TODO: Check if need newone
	// 	data[len++] = 0x10; /* Ustaw tekst */
	// 	if ((icons != old_icons) || (mode != old_mode))
	// 	{
	// 		data[len++] = 0x1C; /* Tekst + ikony */
	// 		data[len++] = 0x7F;
	// 		data[len++] = icons;
	// 		data[len++] = 0x55;
	// 		data[len++] = mode;
	// 	}
	// 	else
	// 	{
	// 		data[len++] = 0x19;		/* Sam tekst */
	// 		data[len++] = textType; //??? 0x76 was setted but not working value, have put 0x7E isntead //possible 0x7E will add channel, but ox76 will not
	// 	}

	// 	data[len++] = chan; // 0x70<chan<0x7A  0..9+null;
	// 	data[len++] = loc;	// 0x01 always by now

	// 	for (i = 0; i < 8; i++)
	// 	{
	// 		data[len++] = old[i];
	// 	}

	// 	data[len++] = 0x10; /* Separator */

	// 	for (i = 0; i < 12; i++)
	// 	{
	// 		data[len++] = neww[i];
	// 	}

	// 	data[len++] = 0x00; /* Terminator */
	// 	data[len++] = 0x81; /* filler to try */
	// 	data[len++] = 0x81; /*  filler to try */
	// 						// 👇 Add debug output
	// 						// AFFA3_PRINT("[do_set_text] ID: 0x%X, len=%u\n", AFFA3_PACKET_ID_SETTEXT, len);
	// 						// for (uint8_t j = 0; j < len; j++)
	// 						// {
	// 						//     AFFA3_PRINT("%02X ", data[j]);
	// 						// }
	// 						// AFFA3_PRINT("\n");
	// 	return affa3_send(Affa3::PACKET_ID_SETTEXT, data, len);
	// }

AffaCommon::AffaError Affa3Display::setText(const char *text, uint8_t digit /* 0-9, or anything else for none */)
{
	char oldBuf[8] = {' '};
	char newBuf[12] = {' '}; 
	// Prepare channel byte
	uint8_t chan = (digit <= 9 && digit >= 0) ? (0x70 + digit) : 0x7A;
	uint8_t loc = 0x01;		 // Always 0x01 for now, unknown purpose
	uint8_t textType = 0x76; // unknown purposes (maybe text with . separator like in radio)
	// Fill old and new buffers
	strncpy(oldBuf, text, sizeof(oldBuf));
	strncpy(newBuf, text, sizeof(newBuf));
	// return affa3_do_set_text(0xFF, 0x00, chan, 0x01, 0x76, oldBuf, newBuf);

	static uint8_t old_icons = 0xFF;
	static uint8_t old_mode = 0x00;
	uint8_t data[32];
	uint8_t i, len = 0;
	// TODO: Check if need newone
	data[len++] = 0x10;		/* Ustaw tekst */
	// if (/*(icons != old_icons) ||*/ (mode != old_mode)) //not suppotring new display for now, maybe TODO: later
	// {
	// 	data[len++] = 0x1C; /* Tekst + ikony */
	// 	data[len++] = 0x7F;
	// 	data[len++] = icons;
	// 	data[len++] = 0x55;
	// 	data[len++] = mode;
	// }
	// else
	// {
	data[len++] = 0x19;		/* Sam tekst */
	data[len++] = textType; //??? 0x76 was setted but not working value, have put 0x7E isntead //possible 0x7E will add channel, but ox76 will not
	// }

	data[len++] = chan; // 0x70<chan<0x7A  0..9+null;
	data[len++] = loc;	// 0x01 always by now

	for (i = 0; i < 8; i++)
	{
		data[len++] = oldBuf[i];
	}

	data[len++] = 0x10; /* Separator */

	for (i = 0; i < 12; i++)
	{
		data[len++] = newBuf[i];
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
	//return affa3_do_set_text(0xFF, 0x00, chan, 0x01, 0x76, oldBuf, newBuf);
}
 

AffaCommon::AffaError Affa3Display::setTime(const char *clock)
{
	return AffaCommon::AffaError::NoError; // Placeholder for actual implementation
}