#include <Arduino.h>
#include <esp32_can.h> /* https://github.com/collin80/esp32_can */
#ifndef __AFFA3_H
#define __AFFA3_H 

#define AFFA3_PING_TIMEOUT           5

#define AFFA3_PACKET_LEN             0x08
#define AFFA3_PACKET_FILLER  0x81//        0x00//0x81 ???? AFFA3 NAV?

#define AFFA3_PACKET_REPLY_FLAG      0x400 /* Bit oznaczajązy że jest to odpowiedź */

#define AFFA3_PACKET_ID_SYNC_REPLY   0x3CF /* Pakiet uzywany w celu utrzymania synchronizacji od wyświetlacza */
#define AFFA3_PACKET_ID_SYNC         0x3AF /* Pakiet uzywany w celu utrzymania synchronizacji */  //3AF for NAV???
#define AFFA3_PACKET_ID_DISPLAY_CTRL 0x1c1 /* Pakiet używany do kontroli wyświetlacza (włączanie/wyłączanie) */
#define AFFA3_PACKET_ID_SETTEXT      0x151 /* Pakiet uzywany do wysyłania tekstu na wyświetlacz */
#define AFFA3_PACKET_ID_KEYPRESSED   0x0A9 /* Pakiet używany do przesyłania kodu klawisza z pilota */

/* 
3cf - 3af piong pong?
1c1 -5c1 cntrl?
1F1 - 5F1 
151 - 551 - yrcy
*/








#define AFFA3_SYNC_STAT_FAILED       0x01
#define AFFA3_SYNC_STAT_PEER_ALIVE   0x02
#define AFFA3_SYNC_STAT_START        0x04
#define AFFA3_SYNC_STAT_FUNCSREG     0x08 
 
#define AFFA3_PRINT(fmt, ...)        printf_P(PSTR(fmt), ##__VA_ARGS__) 

#define AFFA3_FUNC_STAT_IDLE         0x00 
#define AFFA3_FUNC_STAT_WAIT         0x01 /* Czekamy na odpowiedź */
#define AFFA3_FUNC_STAT_PARTIAL      0x02 /* Wyświetlacz otrzymał część danych */
#define AFFA3_FUNC_STAT_DONE         0x03 /* Zakończono wykonywanie */
#define AFFA3_FUNC_STAT_ERROR        0x04 /* Wystąpił błąd */

#define AFFA3_DISPLAY_CTRL_DISABLE   0x00
#define AFFA3_DISPLAY_CTRL_ENABLE    0x02

#define AFFA3_ICON_NO_NEWS           (1 << 0)
#define AFFA3_ICON_NEWS_ARROW        (1 << 1)
#define AFFA3_ICON_NO_TRAFFIC        (1 << 2)
#define AFFA3_ICON_TRAFFIC_ARROW     (1 << 3)
#define AFFA3_ICON_NO_AFRDS          (1 << 4)
#define AFFA3_ICON_AFRDS_ARROW       (1 << 5)
#define AFFA3_ICON_NO_MODE           (1 << 6)

#define AFFA3_LOCATION(max,idx)      ((((max) & 7) << 5) | (((idx) & 7) << 2))
#define AFFA3_LOCATION_SELECTED      0x01
#define AFFA3_LOCATION_FULLSCREEN    0x02

#define AFFA3_ICON_MODE_NONE         0xFF

#define AFFA3_ENOTSYNC               0x01
#define AFFA3_EUNKNOWNFUNC           0x02
#define AFFA3_ESENDFAILED            0x03
#define AFFA3_ETIMEOUT               0x04
#define AFFA3_ESTRTOLONG             0x05

#define AFFA3_KEY_QUEUE_SIZE         0x08

#define AFFA3_KEY_LOAD               0x0000 /* Ten na dole pilota ;) */
#define AFFA3_KEY_SRC_RIGHT          0x0001
#define AFFA3_KEY_SRC_LEFT           0x0002
#define AFFA3_KEY_VOLUME_UP          0x0003
#define AFFA3_KEY_VOLUME_DOWN        0x0004
#define AFFA3_KEY_PAUSE              0x0005
#define AFFA3_KEY_ROLL_UP            0x0101
#define AFFA3_KEY_ROLL_DOWN          0x0141

#define AFFA3_KEY_HOLD_MASK          (0x80 | 0x40)

struct affa3_func {
	uint16_t id;
	uint8_t stat;
};

void affa3_init(void); /* Inicjalizacja biblioteki */
void affa3_tick(void); /* Funkcja wywoływana z przerwania zegarowego */
void affa3_recv(struct can_packet * packet); /* Funkcja wywoływana przy nowych danych przychodzących */

uint8_t affa3_sync_status(void);
int8_t affa3_send2(uint16_t id, uint8_t * data, uint8_t len) ;
int8_t AFFA3_SEND(CAN_FRAME packet) ;
int8_t affa3_display_ctrl(uint8_t state);

int8_t affa3_display_full_screen(char * str); /* Wyświetla tekst na pełnym ekranie */
int8_t affa3_display_normal(char * str, char * oldstr, uint8_t icons, uint8_t mode, uint8_t ch); /* Wyświetla tekst w normalnym trybie */
int8_t affa3_menu_begin(uint8_t max_items); /* Rozpoczyna sekcję menu */
int8_t affa3_menu_set_item(uint8_t idx, uint8_t is_selected, char * entry, char * oldentry); /* Dodaje do menu pową pozycje */

uint16_t affa3_get_key(void); /* Sprawdza czy naciśnięty został jakiś klaiwsz i zwraca jego kod, 0xFFFF gdy brak */
#endif /* __AFFA3_H */