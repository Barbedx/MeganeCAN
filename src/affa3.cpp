#include "affa3.h"
#include <string.h>
#include "CanUtils.h"

#define KEYQ_IS_EMPTY() (_key_q_in == _key_q_out)
#define KEYQ_IS_FULL() (((_key_q_in + 1) % AFFA3_KEY_QUEUE_SIZE) == _key_q_out)

static uint16_t _key_q[AFFA3_KEY_QUEUE_SIZE] = {
	0,
};
static uint8_t _key_q_in = 0;
static uint8_t _key_q_out = 0;

bool affa3_is_synced = false;
static uint8_t _sync_status = AFFA3_SYNC_STAT_FAILED; /* Status synchronizacji z wświetlaczem */
static uint8_t _menu_max_items = 0;

#define FUNCS_MAX (sizeof(_funcs) / sizeof(struct affa3_func))
static volatile struct affa3_func _funcs[] = {
	{.id = AFFA3_PACKET_ID_SETTEXT, .stat = AFFA3_FUNC_STAT_IDLE},
	{.id = AFFA3_PACKET_ID_DISPLAY_CTRL, .stat = AFFA3_FUNC_STAT_IDLE},
};

void affa3_tick()
{
	struct CAN_FRAME packet;
	static int8_t timeout = AFFA3_PING_TIMEOUT;

	//AFFA3_PRINT("[tick] Sending alive ping\n");
	/* Wysyłamy pakiet informujący o tym że żyjemy */
	CanUtils::sendCan(AFFA3_PACKET_ID_SYNC, 0x79, 0x00, AFFA3_PACKET_FILLER, AFFA3_PACKET_FILLER, AFFA3_PACKET_FILLER, AFFA3_PACKET_FILLER, AFFA3_PACKET_FILLER, AFFA3_PACKET_FILLER);

	if ((_sync_status & AFFA3_SYNC_STAT_FAILED) || (_sync_status & AFFA3_SYNC_STAT_START))
	{	/* Błąd synchronizacji */
		/* Wysyłamy pakiet z żądaniem synchronizacji */
		AFFA3_PRINT("[tick] Sync failed or requested, sending sync request\n");
		CanUtils::sendCan(AFFA3_PACKET_ID_SYNC, 0x7A, 0x01, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81);
		_sync_status &= ~AFFA3_SYNC_STAT_START;
		delay(100);
	}
	else
	{
		if (_sync_status & AFFA3_SYNC_STAT_PEER_ALIVE)
		{
		//	AFFA3_PRINT("[tick] Peer is alive, resetting timeout\n");
			timeout = AFFA3_PING_TIMEOUT;
			_sync_status &= ~AFFA3_SYNC_STAT_PEER_ALIVE;
		}
		else
		{
			timeout--;
			AFFA3_PRINT("[tick] Waiting for peer... timeout in %d\n", timeout);
			if (timeout <= 0)
			{ /* Nic nie odpowiada, wymuszamy resynchronizację */
				_sync_status = AFFA3_SYNC_STAT_FAILED;
				/* Wszystkie funkcje tracą rejestracje */
				_sync_status &= ~AFFA3_SYNC_STAT_FUNCSREG;

				AFFA3_PRINT("ping timeout!\n");
			}
		}
	}
}
void affa3_recv(struct CAN_FRAME *packet)
{
	uint8_t i;

	if (packet->id == AFFA3_PACKET_ID_SYNC_REPLY)
	{ /* Pakiety synchronizacyjne */
		if ((packet->data.uint8[0] == 0x61) && (packet->data.uint8[1] == 0x11))
		{ /* Żądanie synchronizacji */
			CanUtils::sendCan(AFFA3_PACKET_ID_SYNC, 0x70, 0x1A, 0x11, 0x00, 0x00, 0x00, 0x00, 0x01);
			 affa3_is_synced = false;
			_sync_status &= ~AFFA3_SYNC_STAT_FAILED;
			if (packet->data.uint8[2] == 0x01)
				_sync_status |= AFFA3_SYNC_STAT_START;
		}
		else if (packet->data.uint8[0] == 0x69)
		{ 
        	_sync_status |= AFFA3_SYNC_STAT_PEER_ALIVE;
			affa3_tick();
		}
		return;
	}

	if (packet->id & AFFA3_PACKET_REPLY_FLAG)
	{
		packet->id &= ~AFFA3_PACKET_REPLY_FLAG;
		for (i = 0; i < FUNCS_MAX; i++)
		{ /* Szukamy w tablicy funkcji */
			if (_funcs[i].id == packet->id)
				break;
		}

		if ((i < FUNCS_MAX) && (_funcs[i].stat == AFFA3_FUNC_STAT_WAIT))
		{ /* Jeżeli funkcja ma status: oczekiwanie na odpowiedź */
			if (packet->data.uint8[0] == 0x74)
			{ /* Koniec danych */
				_funcs[i].stat = AFFA3_FUNC_STAT_DONE;
			}
			else if ((packet->data.uint8[0] == 0x30) && (packet->data.uint8[1] == 0x01) && (packet->data.uint8[2] == 0x00))
			{ /* Wyświetlacz potwierdza przyjęcie części danych */
				_funcs[i].stat = AFFA3_FUNC_STAT_PARTIAL;
			}
			else
			{
				_funcs[i].stat = AFFA3_FUNC_STAT_ERROR;
			}
		}
		return;
	}

	if (packet->id == AFFA3_PACKET_ID_KEYPRESSED)
	{
		if ((packet->data.uint8[0] == 0x03) && (packet->data.uint8[1] != 0x89)) /* Błędny pakiet */
			return;

		if (!KEYQ_IS_FULL())
		{
			_key_q[_key_q_in] = (packet->data.uint8[2] << 8) | packet->data.uint8[3];
			printf_P(PSTR("AFFA2: key code: 0x%X\n"), _key_q[_key_q_in]);
			_key_q_in = (_key_q_in + 1) % AFFA3_KEY_QUEUE_SIZE;
		}
	}

	struct CAN_FRAME reply;
	/* Wysyłamy odpowiedź */
	reply.id = packet->id | AFFA3_PACKET_REPLY_FLAG;
	reply.length = AFFA3_PACKET_LEN;
	i = 0;
	reply.data.uint8[i++] = 0x74;

	for (; i < AFFA3_PACKET_LEN; i++)
		reply.data.uint8[i] = AFFA3_PACKET_FILLER;

	CanUtils::sendFrame(reply);
}

uint8_t affa3_sync_status(void)
{
	return _sync_status;
}

static int8_t affa3_do_send(uint8_t idx, uint8_t *data, uint8_t len)
{
	struct CAN_FRAME packet;
	uint8_t i, stat, num = 0, left = len;
	int16_t timeout;

	if (_sync_status & AFFA3_SYNC_STAT_FAILED)
		return -AFFA3_ENOTSYNC;

	while (left > 0)
	{
		i = 0;

		packet.id = _funcs[idx].id;
		packet.length = AFFA3_PACKET_LEN;

		if (num > 0)
		{
			packet.data.uint8[i++] = 0x20 + num;
		}

		while ((i < AFFA3_PACKET_LEN) && (left > 0))
		{
			packet.data.uint8[i++] = *data++;
			left--;
		}

		for (; i < AFFA3_PACKET_LEN; i++)
		{
			packet.data.uint8[i] = AFFA3_PACKET_FILLER;
		}

		AFFA3_PRINT("Sending packet #%d to ID 0x%03X: ", num, packet.id);
		for (int j = 0; j < packet.length; j++)
			AFFA3_PRINT("%02X ", packet.data.uint8[j]);
		AFFA3_PRINT("\n");


		_funcs[idx].stat = AFFA3_FUNC_STAT_WAIT;

		CanUtils::sendFrame(packet);

		/* Czkekamy na odpowiedź */
		timeout = 2000; /* 2sek */
			uint16_t wait_counter = 0;
		while ((_funcs[idx].stat == AFFA3_FUNC_STAT_WAIT) && (--timeout > 0))
		{
			delay(1);
			
			// Log every 500ms
			if (wait_counter++ % 500 == 0)
			{
				AFFA3_PRINT("Waiting... %dms elapsed for packet #%d (ID: 0x%03X)\n", wait_counter, num, packet.id);
			}
		}

		stat = _funcs[idx].stat;
		_funcs[idx].stat = AFFA3_FUNC_STAT_IDLE;

		if (!timeout)
		{ /* Nie dostaliśmy odpowiedzi */
			AFFA3_PRINT("affa3_send(): timeout, num = %d\n", num);
			return -AFFA3_ETIMEOUT;
		}

		if (stat == AFFA3_FUNC_STAT_DONE)
		{
			AFFA3_PRINT("affa3_send(): DONE received on packet #%d\n", num);
			break;
		}
		else if (stat == AFFA3_FUNC_STAT_PARTIAL)
		{
			AFFA3_PRINT("affa3_send(): PARTIAL ack on packet #%d, remaining: %d bytes\n", num, left);
			if (!left)
			{ /* Nie mamy więcej danych */
				AFFA3_PRINT("affa3_send(): no more data\n");
				return -AFFA3_ESENDFAILED;
			}
			num++;
		}
		else if (stat == AFFA3_FUNC_STAT_ERROR)
		{
				AFFA3_PRINT("affa3_send(): ERROR received on packet #%d\n", num);
				return -AFFA3_ESENDFAILED;
		}
	}

	return 0;
}

static int8_t affa3_send(uint16_t id, uint8_t *data, uint8_t len)
{
	uint8_t idx;
	uint8_t regdata[1] = {0x70};
	int8_t err;

	if ((_sync_status & AFFA3_SYNC_STAT_FUNCSREG) != AFFA3_SYNC_STAT_FUNCSREG)
	{
		AFFA3_PRINT("[send] Registering supported functions...\n");

		for (idx = 0; idx < FUNCS_MAX; idx++)
		{
			AFFA3_PRINT("[send] Registering func ID 0x%X\n", _funcs[idx].id);

			err = affa3_do_send(idx, regdata, sizeof(regdata));
			if (err != 0)
			{
				AFFA3_PRINT("[send] Registration failed for func 0x%X, error %d\n", _funcs[idx].id, err);

				return err;
			}
		}

		_sync_status |= AFFA3_SYNC_STAT_FUNCSREG;
		AFFA3_PRINT("[send] All functions registered.\n");
	}

	for (idx = 0; idx < FUNCS_MAX; idx++)
	{
		if (_funcs[idx].id == id)
			break;
	}

	if (idx >= FUNCS_MAX)
	{
		AFFA3_PRINT("[send] Unknown function ID: 0x%X\n", id);

		return -AFFA3_EUNKNOWNFUNC;
	}
	AFFA3_PRINT("[send] Sending data to func 0x%X, length: %d\n", id, len);

	return affa3_do_send(idx, data, len);
	
}

int8_t affa3_display_ctrl(uint8_t state)
{
	uint8_t data[] = {
		0x04, 0x52, state, 0xFF, 0xFF};

	return affa3_send(AFFA3_PACKET_ID_DISPLAY_CTRL, data, sizeof(data));
}


 int8_t affa3_old_scroll_text(const char* text, uint16_t delayMs)
{
    char buffer[8];
    size_t len = strlen(text);
    size_t total = len + 8 + 1 ; // +8 to pad with blanks at the end +1 to clear entire screen

    for (size_t i = 0; i < total; i++)
    {
        // Build 8-char window with leading/trailing spaces
        for (size_t j = 0; j < 8; j++)
        {
            size_t pos = i + j;
            if (pos < 8) {
                buffer[j] = ' '; // leading space
            } else if (pos - 8 < len) {
                buffer[j] = text[pos - 8];
            } else {
                buffer[j] = ' '; // trailing space
            }
        }

        // Call original function
        affa3_old_set_text(buffer);

        // Wait between frames
        delay(delayMs);
    }

    return 0;
}
int8_t affa3_old_set_text(char text[8])
{
    char padded[8];
    uint8_t i = 0;

    // Copy input text into padded buffer, up to 8 characters
    for (; i < 8 && text[i]; i++) {
        padded[i] = text[i];
    }

    // Fill the rest with spaces if less than 8 characters
    for (; i < 8; i++) {
        padded[i] = ' ';
    }

    return affa3_old_set_text(0x7E, 0x7A /*without channel*/, 0x01, padded);
 
}

int8_t affa3_old_set_text(uint8_t textType, uint8_t chan, uint8_t loc, char oldText[8])
{
    char newText[12];

    // Copy old text to beginning of new text
    for (uint8_t i = 0; i < 8; i++)
    {
        newText[i] = oldText[i];
    }

    // Pad with 4 spaces
    for (uint8_t i = 8; i < 12; i++)
    {
        newText[i] = ' ';
    }

    // Call actual method with default icons and mode
    return affa3_do_set_text(0xFF, 0x00, chan, loc, textType, oldText, newText);
}
 
int8_t affa3_do_set_text(uint8_t icons, uint8_t mode, uint8_t chan, uint8_t loc, uint8_t textType, char old[8], char neww[12])
{

	///long delay in work


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
		data[len++] = 0x19; /* Sam tekst */
		data[len++] = textType; //??? 0x76 was setted but not working value, have put 0x7E isntead //possible 0x7E will add channel, but ox76 will not
	}

	data[len++] = chan; // 0x70<chan<0x7A  0..9+null;
	data[len++] = loc; //0x01 always by now

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
    AFFA3_PRINT("[do_set_text] ID: 0x%X, len=%u\n", AFFA3_PACKET_ID_SETTEXT, len);
    for (uint8_t j = 0; j < len; j++)
    {
        AFFA3_PRINT("%02X ", data[j]);
    }
    AFFA3_PRINT("\n");
	return affa3_send(AFFA3_PACKET_ID_SETTEXT, data, len);
}
