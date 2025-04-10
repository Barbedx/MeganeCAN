#include "affa3.h"
#include <string.h>
#include <esp32_can.h> /* https://github.com/collin80/esp32_can */
#include <Arduino.h>
#define KEYQ_IS_EMPTY()     (_key_q_in == _key_q_out)
#define KEYQ_IS_FULL()      (((_key_q_in + 1) % AFFA3_KEY_QUEUE_SIZE) == _key_q_out)

static uint16_t _key_q[AFFA3_KEY_QUEUE_SIZE] = { 0, };
static uint8_t _key_q_in = 0;
static uint8_t _key_q_out = 0 ;

static uint8_t _sync_status; /* Status synchronizacji z wświetlaczem */
static uint8_t _menu_max_items = 0;

#define FUNCS_MAX            (sizeof(_funcs) / sizeof(struct affa3_func))
static volatile struct affa3_func _funcs[] = {
	{ .id = AFFA3_PACKET_ID_SETTEXT, .stat = AFFA3_FUNC_STAT_IDLE },
	{ .id = AFFA3_PACKET_ID_DISPLAY_CTRL, .stat = AFFA3_FUNC_STAT_IDLE },	
};

void affa3_init(void) {
	_sync_status = AFFA3_SYNC_STAT_FAILED; /* Brak synchronziacji z wyświetlaczem */
}
int8_t AFFA3_SEND(CAN_FRAME packet){
	CAN0.sendFrame(packet);
}

void affa3_tick(void) {
	struct CAN_FRAME packet;
	static int8_t timeout = AFFA3_PING_TIMEOUT;
	
	/* Wysyłamy pakiet informujący o tym że żyjemy */
	packet.id = AFFA3_PACKET_ID_SYNC;
	packet.length = AFFA3_PACKET_LEN;
	packet.data.uint8[0] = 0x79;
	packet.data.uint8[1] = 0x00; /* Tutaj czasem pojawia się 0x01, czemu? */
	packet.data.uint8[2] = AFFA3_PACKET_FILLER;
	packet.data.uint8[3] = AFFA3_PACKET_FILLER;
	packet.data.uint8[4] = AFFA3_PACKET_FILLER;
	packet.data.uint8[5] = AFFA3_PACKET_FILLER;
	packet.data.uint8[6] = AFFA3_PACKET_FILLER;
	packet.data.uint8[7] = AFFA3_PACKET_FILLER;
	AFFA3_SEND(packet);
	
	if ((_sync_status & AFFA3_SYNC_STAT_FAILED) || (_sync_status & AFFA3_SYNC_STAT_START)) { /* Błąd synchronizacji */
		/* Wysyłamy pakiet z żądaniem synchronizacji */
		packet.id = AFFA3_PACKET_ID_SYNC;
		packet.length = AFFA3_PACKET_LEN;
		packet.data.uint8[0] = 0x7A;
		packet.data.uint8[1] = 0x01;
		packet.data.uint8[2] = AFFA3_PACKET_FILLER;
		packet.data.uint8[3] = AFFA3_PACKET_FILLER;
		packet.data.uint8[4] = AFFA3_PACKET_FILLER;
		packet.data.uint8[5] = AFFA3_PACKET_FILLER;
		packet.data.uint8[6] = AFFA3_PACKET_FILLER;
		packet.data.uint8[7] = AFFA3_PACKET_FILLER;
		AFFA3_SEND(packet);
		
		_sync_status &= ~AFFA3_SYNC_STAT_START;
		delay(100);
	}
	else {
		if (_sync_status & AFFA3_SYNC_STAT_PEER_ALIVE) {
			timeout = AFFA3_PING_TIMEOUT;
			_sync_status &= ~AFFA3_SYNC_STAT_PEER_ALIVE;
		}
		else {
			timeout--;
			if (timeout <= 0) { /* Nic nie odpowiada, wymuszamy resynchronizację */
				_sync_status = AFFA3_SYNC_STAT_FAILED;
				/* Wszystkie funkcje tracą rejestracje */
				_sync_status &= ~AFFA3_SYNC_STAT_FUNCSREG;
				
				AFFA3_PRINT("ping timeout!\n");
			}
		}
	}
	
	
}

void affa3_recv(struct CAN_FRAME * packet) {
	struct CAN_FRAME reply;
	uint8_t i, last = 1;
	
	if (packet->id == AFFA3_PACKET_ID_SYNC_REPLY) { /* Pakiety synchronizacyjne */
		if ((packet->data.uint8[0] == 0x61) && (packet->data.uint8[1] == 0x11)) { /* Żądanie synchronizacji */
			reply.id = AFFA3_PACKET_ID_SYNC;
			reply.length = AFFA3_PACKET_LEN;
			reply.data.uint8[0] = 0x70;
			reply.data.uint8[1] = 0x1A;
			reply.data.uint8[2] = 0x11;
			reply.data.uint8[3] = 0x00;
			reply.data.uint8[4] = 0x00;
			reply.data.uint8[5] = 0x00;
			reply.data.uint8[6] = 0x00;
			reply.data.uint8[7] = 0x01;
			
			AFFA3_SEND(reply);
			_sync_status &= ~AFFA3_SYNC_STAT_FAILED;
			if (packet->data.uint8[2] == 0x01)
				_sync_status |= AFFA3_SYNC_STAT_START;
		}
		else if (packet->data.uint8[0] == 0x69) {
			_sync_status |= AFFA3_SYNC_STAT_PEER_ALIVE;
		}
		return;
	}
		
	if (packet->id & AFFA3_PACKET_REPLY_FLAG) {
		packet->id &= ~AFFA3_PACKET_REPLY_FLAG;
		for(i = 0; i < FUNCS_MAX; i++) { /* Szukamy w tablicy funkcji */
			if (_funcs[i].id == packet->id)
				break;
		}
		
		if ((i < FUNCS_MAX) && (_funcs[i].stat == AFFA3_FUNC_STAT_WAIT)) { /* Jeżeli funkcja ma status: oczekiwanie na odpowiedź */
			if (packet->data.uint8[0] == 0x74) { /* Koniec danych */
				_funcs[i].stat = AFFA3_FUNC_STAT_DONE;
			}
			else if ((packet->data.uint8[0] == 0x30) && (packet->data.uint8[1] == 0x01) && (packet->data.uint8[2] == 0x00)) { /* Wyświetlacz potwierdza przyjęcie części danych */
				_funcs[i].stat = AFFA3_FUNC_STAT_PARTIAL;
			}
			else {
				_funcs[i].stat = AFFA3_FUNC_STAT_ERROR;
			}
		}
		return;
	}
	
	if (packet->id == AFFA3_PACKET_ID_KEYPRESSED) {
		if ((packet->data.uint8[0] == 0x03) && (packet->data.uint8[1] != 0x89)) /* Błędny pakiet */
			return;
		
		if (!KEYQ_IS_FULL()) {
			_key_q[_key_q_in] = (packet->data.uint8[2] << 8) | packet->data.uint8[3];
			printf_P(PSTR("AFFA2: key code: 0x%X\n"), _key_q[_key_q_in]);				
			_key_q_in = (_key_q_in + 1) % AFFA3_KEY_QUEUE_SIZE;
		}
	}
	
	/* Wysyłamy odpowiedź */
	reply.id = packet->id | AFFA3_PACKET_REPLY_FLAG;	
	reply.length = AFFA3_PACKET_LEN;
	i = 0;
	if (last) {
		reply.data.uint8[i++] = 0x74;
	}
	else {
		reply.data.uint8[i++] = 0x30;
		reply.data.uint8[i++] = 0x01;
		reply.data.uint8[i++] = 0x00;
	}
	
	for( ; i < AFFA3_PACKET_LEN; i++)
		reply.data.uint8[i] = AFFA3_PACKET_FILLER;
	
	AFFA3_SEND(reply);
}

uint8_t affa3_sync_status(void) {
	return _sync_status;
}

static int8_t affa3_do_send(uint8_t idx, uint8_t * data, uint8_t len) {
	struct CAN_FRAME packet;
	uint8_t i, stat, num = 0, left = len;
	int16_t timeout;
	
	if (_sync_status & AFFA3_SYNC_STAT_FAILED) 
		return -AFFA3_ENOTSYNC;
	
	while (left > 0) {
		i = 0;
		
		packet.id = _funcs[idx].id;
		packet.length = AFFA3_PACKET_LEN;
	
		if (num > 0) {
			packet.data.uint8[i++] = 0x20 + num;
		}
		
		while((i < AFFA3_PACKET_LEN) && (left > 0)) {
			packet.data.uint8[i++] = *data++;
			left--;
		}
		
		for(; i < AFFA3_PACKET_LEN; i++) {
			packet.data.uint8[i] = AFFA3_PACKET_FILLER;
		}		
		
		_funcs[idx].stat = AFFA3_FUNC_STAT_WAIT;
		
		AFFA3_SEND(packet);
		
		/* Czkekamy na odpowiedź */
		timeout = 2000; /* 2sek */
		while((_funcs[idx].stat == AFFA3_FUNC_STAT_WAIT) && (--timeout > 0)) {
			delay(1);
		}
		
		stat = _funcs[idx].stat;
		_funcs[idx].stat = AFFA3_FUNC_STAT_IDLE;
		
		if (!timeout) { /* Nie dostaliśmy odpowiedzi */			
			AFFA3_PRINT("affa3_send2(): timeout, num = %d\n", num);
			return -AFFA3_ETIMEOUT;
		}
		
		if (stat == AFFA3_FUNC_STAT_DONE) {
			break;
		}
		else if (stat == AFFA3_FUNC_STAT_PARTIAL) {
			if (!left) { /* Nie mamy więcej danych */
				AFFA3_PRINT("affa3_send2(): no more data\n");
				return -AFFA3_ESENDFAILED;
			}
			num++;
		}
		else if (stat == AFFA3_FUNC_STAT_ERROR) {
			AFFA3_PRINT("affa3_send2(): error\n");
			return -AFFA3_ESENDFAILED;
		}
	}
	
	return 0;
}


 int8_t affa3_send2(uint16_t id, uint8_t * data, uint8_t len) {
	uint8_t idx;
	uint8_t regdata[1] = { 0x70 };
	int8_t err;
	
	if ((_sync_status & AFFA3_SYNC_STAT_FUNCSREG) != AFFA3_SYNC_STAT_FUNCSREG) {
		for(idx = 0; idx < FUNCS_MAX; idx++) {
			err = affa3_do_send(idx, regdata, sizeof(regdata));
			if (err != 0) {
				return err;
			}
		}
		
		_sync_status |= AFFA3_SYNC_STAT_FUNCSREG;
	}
	
	for(idx = 0; idx < FUNCS_MAX; idx++) {
		if (_funcs[idx].id == id)
			break;
	}
	
	if (idx >= FUNCS_MAX)
		return -AFFA3_EUNKNOWNFUNC;
		
	return affa3_do_send(idx, data, len);
}

int8_t affa3_display_ctrl(uint8_t state) {
	uint8_t data[] = {
		0x04, 0x52, state, 0xFF, 0xFF
	};
	
	return affa3_send2(AFFA3_PACKET_ID_DISPLAY_CTRL, data, sizeof(data));
}

static int8_t affa3_do_set_text(uint8_t icons, uint8_t mode, uint8_t chan, uint8_t loc, char old[8], char newest[12]) {
	static uint8_t old_icons = 0xFF;
	static uint8_t old_mode = 0x00;
	uint8_t data[32];
	uint8_t i, len = 0;
		
	data[len++] = 0x10; /* Ustaw tekst */
	if ((icons != old_icons) || (mode != old_mode)) {
		data[len++] = 0x1C; /* Tekst + ikony */
		data[len++] = 0x7F;
		data[len++] = icons;
		data[len++] = 0x55;
		data[len++] = mode;
	}
	else {
		data[len++] = 0x19; /* Sam tekst */
		data[len++] = 0x76;
	}
	
	data[len++] = 0x60 | (chan & 7);
	data[len++] = loc;
	
	for(i = 0; i < 8; i++) {
		data[len++] = old[i];
	}
	
	data[len++] = 0x10; /* Separator */
	
	for(i = 0; i < 12; i++) {
		data[len++] = newest[i];
	}
	
	data[len++] = 0x00; /* Terminator */
	
	return affa3_send2(AFFA3_PACKET_ID_SETTEXT, data, len);
}

int8_t affa3_display_full_screen(char * str) {
	char data[12]; 
	uint8_t i, j, len, packets;
	int8_t err;
	
	len = strlen(str);
	packets = len / 8;
	if (len % 8)
		packets++;
	if (packets > 7) 
		return -AFFA3_ESTRTOLONG;
	
	_menu_max_items = 0;
	
	for(i = 0; i < packets; i++) {
		j = 0;
		while ((j < 8) && (*str))
			data[j++] = *str++;		
		
		for(; j < 12; j++)
			data[j] = ' ';
		
		err = affa3_do_set_text(AFFA3_ICON_NO_TRAFFIC | AFFA3_ICON_NO_NEWS | AFFA3_ICON_NO_AFRDS | AFFA3_ICON_NO_MODE,
					AFFA3_ICON_MODE_NONE, 0, AFFA3_LOCATION(packets - 1, i) | AFFA3_LOCATION_SELECTED | AFFA3_LOCATION_FULLSCREEN,
					data, data);
		
		if (err != 0)
			return err;
	}
	
	return 0;
}

int8_t affa3_display_normal(char * str, char * oldstr, uint8_t icons, uint8_t mode, uint8_t ch) {
	char data[12];
	char olddata[8];
	uint8_t i;
	
	if (!oldstr)
		oldstr = str;	
	
	i = 0;
	while((i < sizeof(data)) && (*str))
		data[i++] = *str++;
	
	for(; i < sizeof(data); i++)
		data[i] = ' ';
	
	i = 0;
	while((i < sizeof(olddata)) && (*oldstr)) 
		olddata[i++] = *oldstr++;
	
	for(; i < sizeof(olddata); i++)
		olddata[i] = ' ';
	
	_menu_max_items = 0;
	
	return affa3_do_set_text(icons, mode, ch, AFFA3_LOCATION_SELECTED, olddata, data);	
}

int8_t affa3_menu_begin(uint8_t max_items) {
	
	max_items &= 0x07;
	if (!max_items)
		_menu_max_items = 0;
	else
		_menu_max_items = max_items - 1;
	
	return 0;
}

int8_t affa3_menu_set_item(uint8_t idx, uint8_t is_selected, char * entry, char * oldentry) {
	char data[12];
	char olddata[8];
	uint8_t i, loc = AFFA3_LOCATION(_menu_max_items, idx);
	
	if (is_selected)
		loc |= AFFA3_LOCATION_SELECTED;
	
	if (!oldentry)
		oldentry = entry;
	
	i = 0;
	while((i < sizeof(data)) && (*entry))
		data[i++] = *entry++;
	
	for(; i < sizeof(data); i++)
		data[i] = ' ';
	
	i = 0;
	while((i < sizeof(olddata)) && (*oldentry)) 
		olddata[i++] = *oldentry++;
	
	for(; i < sizeof(olddata); i++)
		olddata[i] = ' ';
		
	return affa3_do_set_text(AFFA3_ICON_NO_NEWS | AFFA3_ICON_NO_TRAFFIC | AFFA3_ICON_NO_AFRDS | AFFA3_ICON_NO_MODE,
				 AFFA3_ICON_MODE_NONE, 0, loc, olddata, data);
}

uint16_t affa3_get_key(void) {
	uint16_t key;
	
	if (KEYQ_IS_EMPTY())
		return 0xFFFF;
	
	key = _key_q[_key_q_out];	
	_key_q_out = (_key_q_out + 1) % AFFA3_KEY_QUEUE_SIZE;
	
	return key;
}
