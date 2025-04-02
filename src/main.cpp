#include <Arduino.h>

#include <esp32_can.h> /* https://github.com/collin80/esp32_can */

#include <SerialCommand.h>
#include <affa3.h>

#include <BleKeyboard.h>

#define AFFA2_KEY_LOAD 0x0000 /* This at the bottom of the remote;) */
#define AFFA2_KEY_SRC_RIGHT 0x0001
#define AFFA2_KEY_SRC_LEFT 0x0002
#define AFFA2_KEY_VOLUME_UP 0x0003
#define AFFA2_KEY_VOLUME_DOWN 0x0004
#define AFFA2_KEY_PAUSE 0x0005
#define AFFA2_KEY_ROLL_UP 0x0101
#define AFFA2_KEY_ROLL_DOWN 0x0141
#define AFFA2_KEY_HOLD_MASK (0x80 | 0x40)
BleKeyboard bleKeyboard("Bluetooth Device Name", "Bluetooth Device Manufacturer", 100);

bool current_in_AUX_mode = false; // Global variable for AUX mode tracking

// void printDisp();
// void do_send_to(uint16_t id, uint8_t *data, uint8_t datasz, uint8_t filler);
// void send_to_display(uint16_t id, uint8_t *data, uint8_t datasz);
// void send_to_hu(uint16_t id, uint8_t *data, uint8_t datasz);
// void testDisp();
void unrecognized(const char *command);
// void startSync();
void CANsendMsgBuf(uint32_t id, uint8_t ex, uint8_t length, unsigned char message[]);
// void syncOK();
// void syncDisp();
// void registerDisplay();
// void enableDisplay();
// void initDisplay();
// void messageTest();
// void messageTextIcons();
// void displaySong();
// void display8ByteString(String s);
// void scrollDisplay(String s);
// void semiScroll(String s);
// INT8U Flag_Recv = 0;
// INT8U len = 0;
// INT8U buf[8];
// INT32U canId = 0x000;

#define AFFA3_SYNC_STAT_FAILED 0x01
static uint8_t _sync_status; /* Status synchronizacji z wświetlaczem */

void affa3_init(void)
{
  _sync_status = AFFA3_SYNC_STAT_FAILED; /* Brak synchronziacji z wyświetlaczem */
}

static uint8_t _menu_max_items = 0;
static volatile struct affa3_func _funcs[] = {
    {.id = AFFA3_PACKET_ID_SETTEXT, .stat = AFFA3_FUNC_STAT_IDLE},
    {.id = AFFA3_PACKET_ID_DISPLAY_CTRL, .stat = AFFA3_FUNC_STAT_IDLE},
};

#define FUNCS_MAX (sizeof(_funcs) / sizeof(struct affa3_func))

int8_t affa3_display_ctrl(uint8_t state) {
	uint8_t data[] = {
		0x04, 0x52, state, 0xFF, 0xFF
	};
	
	return affa3_send(AFFA3_PACKET_ID_DISPLAY_CTRL, data, sizeof(data));
}

void affa3_tick(void)
{
  struct CAN_FRAME packet;
  static int8_t timeout = AFFA3_PING_TIMEOUT;

  /* Wysyłamy pakiet informujący o tym że żyjemy */
  packet.id = AFFA3_PACKET_ID_SYNC;
  packet.length = AFFA3_PACKET_LEN;
  // if(_sync_status & AFFA3_SYNC_STAT_FAILED){
    packet.data.uint8[0] = 0xB9; //or ba if not synchronize and then b9?
  // }else
  // {
  //   packet.data.uint8[0] = 0xB9; //or ba if not synchronize and then b9?
  // }
  packet.data.uint8[1] = 0x00; /* Tutaj czasem pojawia się 0x01, czemu? */
  packet.data.uint8[2] = 0x00;
  packet.data.uint8[3] = 0x00;
  packet.data.uint8[4] = 0x00;
  packet.data.uint8[5] = 0x00;
  packet.data.uint8[6] = 0x00;
  packet.data.uint8[7] = 0x00;
  CAN0.sendFrame(packet);// ping?

  if ((_sync_status & AFFA3_SYNC_STAT_FAILED) || (_sync_status & AFFA3_SYNC_STAT_START))
  { /* Błąd synchronizacji */
    /* Wysyłamy pakiet z żądaniem synchronizacji */
    packet.id = AFFA3_PACKET_ID_SYNC;
    packet.length = AFFA3_PACKET_LEN;
    packet.data.uint8[0] = 0xBA;
    packet.data.uint8[1] = AFFA3_PACKET_FILLER;
    packet.data.uint8[2] = AFFA3_PACKET_FILLER;
    packet.data.uint8[3] = AFFA3_PACKET_FILLER;
    packet.data.uint8[4] = AFFA3_PACKET_FILLER;
    packet.data.uint8[5] = AFFA3_PACKET_FILLER;
    packet.data.uint8[6] = AFFA3_PACKET_FILLER;
    packet.data.uint8[7] = AFFA3_PACKET_FILLER;
    CAN0.sendFrame(packet);

    _sync_status &= ~AFFA3_SYNC_STAT_START;
    delay(100);
  }
  else
  {
    if (_sync_status & AFFA3_SYNC_STAT_PEER_ALIVE)
    {
      timeout = AFFA3_PING_TIMEOUT;
      _sync_status &= ~AFFA3_SYNC_STAT_PEER_ALIVE;
    }
    else
    {
      timeout--;
      if (timeout <= 0)
      { /* Nic nie odpowiada, wymuszamy resynchronizację */
        _sync_status = AFFA3_SYNC_STAT_FAILED;
        /* Wszystkie funkcje tracą rejestracje */
        _sync_status &= ~AFFA3_SYNC_STAT_FUNCSREG;

        Serial.println("ping timeout!\n");
      }
    }
  }
}

void show_text(char text[14]) {
  struct CAN_FRAME packets[3];
  uint8_t i;
  
  /* Budujemy pakiety */
  packets[0].id = 0x151;
  packets[0].rtr = 0;
  packets[0].length = 8;
  packets[0].data.uint8[0] = 0x10; //text start frame
  packets[0].data.uint8[1] = 0x0E; // lenght
  packets[0].data.uint8[2] = 0x77; //cmd1
  packets[0].data.uint8[3] = 0x60; // 54/14/55 ????
  packets[0].data.uint8[4] = 0x55; //always
  packets[0].data.uint8[5] = 0xFF; // alwaystext[0];
  packets[0].data.uint8[6] = 0x60; //60/71/73 ?????
  packets[0].data.uint8[7] = 0x1;//always
  
  packets[1].id = 0x121;
  packets[1].rtr = 0;
  packets[1].length = 8;
  packets[1].data.uint8[0] = 0x21;
  packets[1].data.uint8[1] = text[0];
  packets[1].data.uint8[2] = text[1];
  packets[1].data.uint8[3] = text[2];
  packets[1].data.uint8[4] = text[3];
  packets[1].data.uint8[5] = text[4];
  packets[1].data.uint8[6] = text[5];
  packets[1].data.uint8[7] = text[6];
  
  packets[2].id = 0x121;
  packets[2].rtr = 0;
  packets[2].length = 8;
  packets[2].data.uint8[0] = 0x22;
  packets[2].data.uint8[1] = text[7];
  packets[2].data.uint8[2] = text[8];
  packets[2].data.uint8[3] = text[9];
  packets[2].data.uint8[4] = text[10];
  packets[2].data.uint8[5] = text[11];
  packets[2].data.uint8[6] = text[12];
  packets[2].data.uint8[7] = text[13];
   
  
  for(i = 0; i < 4; i++) {
    CAN0.sendFrame(packets[i]);
    delay(1); 
  }
}

static int8_t affa3_do_set_text(uint8_t icons, uint8_t mode, uint8_t chan, uint8_t loc, char old[8], char newewst[12])
{
  static uint8_t old_icons = 0xFF;
  static uint8_t old_mode = 0x00;
  uint8_t data[16];
  uint8_t i, len = 0;

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
    data[len++] = 0x76;
  }

  data[len++] = 0x60 | (chan & 7);
  data[len++] = loc;

  for (i = 0; i < 8; i++)
  {
    data[len++] = old[i];
  }

  data[len++] = 0x10; /* Separator */

  for (i = 0; i < 12; i++)
  {
    data[len++] = newewst[i];
  }

  data[len++] = 0x00; /* Terminator */

  return affa3_send(AFFA3_PACKET_ID_SETTEXT, data, len);
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

    _funcs[idx].stat = AFFA3_FUNC_STAT_WAIT;

    Can0.sendFrame(packet);

    /* Czkekamy na odpowiedź */
    timeout = 2000; /* 2sek */
    while ((_funcs[idx].stat == AFFA3_FUNC_STAT_WAIT) && (--timeout > 0))
    {
      delay(1);
    }

    stat = _funcs[idx].stat;
    _funcs[idx].stat = AFFA3_FUNC_STAT_IDLE;

    if (!timeout)
    { /* Nie dostaliśmy odpowiedzi */
      Serial.printf("affa3_send(): timeout, num = %d\n", num);
      return -AFFA3_ETIMEOUT;
    }

    if (stat == AFFA3_FUNC_STAT_DONE)
    {
      break;
    }
    else if (stat == AFFA3_FUNC_STAT_PARTIAL)
    {
      if (!left)
      { /* Nie mamy więcej danych */
        Serial.printf("affa3_send(): no more data\n");
        return -AFFA3_ESENDFAILED;
      }
      num++;
    }
    else if (stat == AFFA3_FUNC_STAT_ERROR)
    {
      Serial.printf("affa3_send(): error\n");
      return -AFFA3_ESENDFAILED;
    }
  }

  return 0;
}

int8_t affa3_send(uint16_t id, uint8_t *data, uint8_t len)
{
  uint8_t idx;
  uint8_t regdata[1] = {0x70};
  int8_t err;

  if ((_sync_status & AFFA3_SYNC_STAT_FUNCSREG) != AFFA3_SYNC_STAT_FUNCSREG)
  {
    for (idx = 0; idx < FUNCS_MAX; idx++)
    {
      err = affa3_do_send(idx, regdata, sizeof(regdata));
      if (err != 0)
      {
        return err;
      }
    }

    _sync_status |= AFFA3_SYNC_STAT_FUNCSREG;
  }

  for (idx = 0; idx < FUNCS_MAX; idx++)
  {
    if (_funcs[idx].id == id)
      break;
  }

  if (idx >= FUNCS_MAX)
    return -AFFA3_EUNKNOWNFUNC;

  return affa3_do_send(idx, data, len);
}

int8_t affa3_display_full_screen(const char *str)
{
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

  for (i = 0; i < packets; i++)
  {
    j = 0;
    while ((j < 8) && (*str))
      data[j++] = *str++;

    for (; j < 12; j++)
      data[j] = ' ';

    err = affa3_do_set_text(AFFA3_ICON_NO_TRAFFIC | AFFA3_ICON_NO_NEWS | AFFA3_ICON_NO_AFRDS | AFFA3_ICON_NO_MODE,
                            AFFA3_ICON_MODE_NONE, 0, AFFA3_LOCATION(packets - 1, i) | AFFA3_LOCATION_SELECTED | AFFA3_LOCATION_FULLSCREEN,
                            data, data);

    if (err != 0)
      return err;
  }

  return 0;
}

int8_t affa3_display_normal(char *str, char *oldstr, uint8_t icons, uint8_t mode, uint8_t ch)
{
  char data[12];
  char olddata[8];
  uint8_t i;

  if (!oldstr)
    oldstr = str;

  i = 0;
  while ((i < sizeof(data)) && (*str))
    data[i++] = *str++;

  for (; i < sizeof(data); i++)
    data[i] = ' ';

  i = 0;
  while ((i < sizeof(olddata)) && (*oldstr))
    olddata[i++] = *oldstr++;

  for (; i < sizeof(olddata); i++)
    olddata[i] = ' ';

  _menu_max_items = 0;

  return affa3_do_set_text(icons, mode, ch, AFFA3_LOCATION_SELECTED, olddata, data);
}

uint8_t pingMsg[] = {
    'y',
    0x00,
    0x81,
    0x81,
    0x81,
    0x81,
    0x81,
    0x81,
};

unsigned char msg5c1[8] = {0x74, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81};
unsigned char msg4a9[8] = {0x74, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81};
uint8_t test_packet[] = {
    0x10,                                                             /* Set text */
    0x1C,                                                             /* If we want to inform about them display here 0x1C and additional bytes further */
    0x7F,                                                             /* ??? */
    0x55,                                                             /* For old type: station number not set */
    0x55,                                                             /* Normal text */
    '1', '2', '3', '4', '5', '6', '7', '8',                           /* 8 characters for the old type */
    0x10,                                                             /* Serparator? */
    'H', 'e', 'l', 'l', 'o', ' ', 'W', 'o', 'r', 'l', 'd', '!', '\0', /* 12 characters for new type + null byte */
};

String Txt = "";      // text to display
String savedTxt = ""; // new text wiating to be displayed
bool flagUpdateTxt = false;

SerialCommand sCmd; // The SerialCommand object
// put function declarations here:
// int myFunction(int, int);
 
// This gets set as the default handler, and gets called when no other command matches.
void unrecognized(const char *command)
{
  Serial.println(F("What?"));
  Serial.print(F("Unrecognized command: "));
  Serial.println(command); // Write the unrecognized command to Serial
}


void CANsendMsgBuf(uint32_t id, uint8_t ex, uint8_t length, unsigned char message[])
{
  CAN_FRAME txFrame;            // Create a CAN frame instance
  txFrame.rtr = 0;              // Set to data frame
  txFrame.id = id;              // Set the CAN ID
  txFrame.extended = (ex != 0); // Set extended flag based on `ex` parameter
  txFrame.length = length;      // Set length of the data

  // Ensure length does not exceed maximum
  if (length > 8)
  {
    length = 8; // CAN frames can only have a maximum of 8 bytes of data
    Serial.println("Frame cannot exceed 8 bytes!");
    throw;
  }

  // Fill the data into the CAN frame
  for (uint8_t i = 0; i < length; i++)
  {
    txFrame.data.uint8[i] = message[i]; // Fill the CAN data
  }

  // Send the CAN frame
  CAN0.sendFrame(txFrame);
}

void printDisp(){
  affa3_display_full_screen("hello");
}
void testDisp(){
  show_text("meganeUA     ");
}
#pragma endregion

void printFrame(CAN_FRAME *frame, int mailbox = -1){
  Serial.print("got message in mailbox []");
  Serial.println(mailbox);
  
  Serial.print("CAN MSG: 0x");
  Serial.print(frame->id, HEX);
  Serial.print(" [");
  Serial.print(frame->length, DEC);
  Serial.print("] <");
  for (int i = 0; i < frame->length; i++)
  {
    if (i != 0)
      Serial.print(":");
    Serial.print(frame->data.byte[i], HEX);
  }
  Serial.println(">");
}
// Callback function for frame with ID 0x1C1
void gotFrame_0x1C1(CAN_FRAME *packet) 
{
  printFrame(packet, 0);

  
    if ((packet->data.uint8[0] == 0x03) && (packet->data.uint8[1] != 0x89)) /* Błędny pakiet */
    {
      Serial.println("bledny packet");
      return;
    }
    
     
    // Extract key value
    uint16_t key = (packet->data.uint8[2] << 8) | packet->data.uint8[3];

    // Handle key actions
    switch (key) 
    {
        case AFFA3_KEY_PAUSE:
            Serial.println("Pause/Play");
            bleKeyboard.write(KEY_MEDIA_PLAY_PAUSE);
            break;

        case AFFA3_KEY_ROLL_UP: // Next track
            Serial.println("Next Track");
            bleKeyboard.write(KEY_MEDIA_NEXT_TRACK);
            break;

        case AFFA3_KEY_ROLL_DOWN: // Previous track
            Serial.println("Previous Track");
            bleKeyboard.write(KEY_MEDIA_PREVIOUS_TRACK);
            break;

        default:
            Serial.print("Unknown key: 0x");
            Serial.println(key, HEX);
            break;
    }
      
}
 

// Callback function for frame with ID 0x151
void gotFrame_0x151(CAN_FRAME *packet) 
{
  printFrame(packet, 1);

  
    // Extract key text from CAN message
    if (packet->data.uint8[0] == 0x21) // Start of the text payload
    {
        char displayText[9]; // 8 characters + null terminator
        memcpy(displayText, &packet->data.uint8[1], 8); // Copy text data
        displayText[8] = '\0'; // Null terminate the string

        if (strncmp(displayText, "AUX     ", 8) == 0) 
        {
            current_in_AUX_mode = true;
            Serial.println("AUX mode enabled");
        } 
        else if (strncmp(displayText, " PAUSE  ", 8) == 0) 
        {
            Serial.println("PAUSE detected, staying in AUX mode.");
        } 
        else 
        {
            current_in_AUX_mode = false;
            Serial.print("AUX mode disabled, current text: ");
            Serial.println(displayText);
        }
    }
}

// General callback function for frames that don't match the specific IDs
void gotFrame(CAN_FRAME *frame) 
{
  printFrame(frame, -1);
}


void setup()
{

  Serial.begin(115200);
  bleKeyboard.begin();

  Serial.println("------------------------");
  Serial.println("    MrDIY CAN SHIELD v0.1");
  Serial.println("------------------------");

  Serial.println(" CAN...............INIT");
  CAN0.setCANPins(GPIO_NUM_4, GPIO_NUM_5); // config for shield v1.3+, see important note above!
  CAN0.begin(CAN_BPS_500K);                      // 500Kbps


  // Set filters to only catch frames with ID 0x1C1 and 0x151
  Can0.setRXFilter(0, 0x1C1, 0x7FF, false);  // Catch frames with ID 0x1C1
  Can0.setRXFilter(1, 0x151, 0x7FF, false);  // Catch frames with ID 0x151
  
  // Register callback functions for specific IDs
  Can0.setCallback(0, gotFrame_0x1C1);  // Frame with ID 0x1C1 will trigger this callback
  Can0.setCallback(1, gotFrame_0x151);  // Frame with ID 0x151 will trigger this callback
  
  // General callback function for any other frame that doesn't match the above filters
  Can0.setGeneralCallback(gotFrame);


 // CAN0.watchFor();



  Serial.println(" CAN............500Kbps");

  // // Setup callbacks for SerialCommand commands
  sCmd.addCommand("p", printDisp);        // print message on display
  sCmd.addCommand("t", testDisp);         // print test message on display
  // sCmd.addCommand("ss", startSync);       // 
  // sCmd.addCommand("sd", syncDisp);        //
  // sCmd.addCommand("r", registerDisplay);  //
  // sCmd.addCommand("e", enableDisplay);    //
  // sCmd.addCommand("i", initDisplay);      //
  // sCmd.addCommand("mt", messageTest);     //
  // sCmd.addCommand("m", messageTextIcons); //
  // sCmd.addCommand("d", displaySong);      //
  sCmd.setDefaultHandler(unrecognized);   // Handler for command that isn't matched  (says "What?")

  //affa3_init();
 
}
 
#define KEYQ_IS_EMPTY() (_key_q_in == _key_q_out)
#define KEYQ_IS_FULL() (((_key_q_in + 1) % AFFA3_KEY_QUEUE_SIZE) == _key_q_out)
static uint16_t _key_q[AFFA3_KEY_QUEUE_SIZE] = {
    0,
};
static uint8_t _key_q_in = 0;
static uint8_t _key_q_out = 0;

void affa3_recv(struct CAN_FRAME packet)
{
  struct CAN_FRAME reply;
  uint8_t i, last = 1;

  if (packet.id == AFFA3_PACKET_ID_SYNC_REPLY)
  { /* Pakiety synchronizacyjne */
    Serial.println("Got sync packet");
    if ((packet.data.uint8[0] == 0x61) && (packet.data.uint8[1] == 0x11))
    { /* Żądanie synchronizacji */
      reply.id = AFFA3_PACKET_ID_SYNC;
      reply.length = AFFA3_PACKET_LEN;
      reply.data.uint8[0] = 0xB0;
      reply.data.uint8[1] = 0x14;
      reply.data.uint8[2] = 0x11;
      reply.data.uint8[3] = 0x00;
      reply.data.uint8[4] = 0x1F;
      reply.data.uint8[5] = 0x00;
      reply.data.uint8[6] = 0x00;
      reply.data.uint8[7] = 0x00;

      if (CAN0.sendFrame(reply))
      {
        Serial.println("sync answer sended");
      }
      else
      {
        Serial.println("sync answer not sended");
      }

      _sync_status &= ~AFFA3_SYNC_STAT_FAILED;
      if (packet.data.uint8[2] == 0x01)
        _sync_status |= AFFA3_SYNC_STAT_START;
    }
    else if (packet.data.uint8[0] == 0x69)
    {
      _sync_status |= AFFA3_SYNC_STAT_PEER_ALIVE;
    }
    Serial.println("status:");
    Serial.println(_sync_status);
    return;
  }

  if (packet.id & AFFA3_PACKET_REPLY_FLAG)
  {
    
    Serial.println("got status reply flag:");
    packet.id &= ~AFFA3_PACKET_REPLY_FLAG;
    for (i = 0; i < FUNCS_MAX; i++)
    { /* Szukamy w tablicy funkcji */
      if (_funcs[i].id == packet.id)
        break;
    }

    if ((i < FUNCS_MAX) && (_funcs[i].stat == AFFA3_FUNC_STAT_WAIT))
    { /* Jeżeli funkcja ma status: oczekiwanie na odpowiedź */
      Serial.println("fcja ma statuis ozykiwania:");

      if (packet.data.uint8[0] == 0x74)
      { /* Koniec danych */
        _funcs[i].stat = AFFA3_FUNC_STAT_DONE;
      }
      else if ((packet.data.uint8[0] == 0x30) && (packet.data.uint8[1] == 0x01) && (packet.data.uint8[2] == 0x00))
      { /* Wyświetlacz potwierdza przyjęcie części danych */
        _funcs[i].stat = AFFA3_FUNC_STAT_PARTIAL;
      }
      else
      {
        _funcs[i].stat = AFFA3_FUNC_STAT_ERROR;
      }
    }
    Serial.println("status:");
    Serial.println(_funcs[i].stat);
    return;
  }

  if (packet.id == AFFA3_PACKET_ID_KEYPRESSED)
  {
    Serial.println("packet id key presed???0x0a9");
    
    if ((packet.data.uint8[0] == 0x03) && (packet.data.uint8[1] != 0x89)) /* Błędny pakiet */
    Serial.println("bledny packet");
      return;

    if (!KEYQ_IS_FULL())
    {
      _key_q[_key_q_in] = (packet.data.uint8[2] << 8) | packet.data.uint8[3];
      printf_P(PSTR("AFFA2: key code: 0x%X\n"), _key_q[_key_q_in]);
      _key_q_in = (_key_q_in + 1) % AFFA3_KEY_QUEUE_SIZE;
    }
  }

  /* Wysyłamy odpowiedź */
  reply.id = packet.id | AFFA3_PACKET_REPLY_FLAG;
  reply.length = AFFA3_PACKET_LEN;
  i = 0;
  if (last)
  {
    reply.data.uint8[i++] = 0x74;
  }
  else
  {
    reply.data.uint8[i++] = 0x30;
    reply.data.uint8[i++] = 0x01;
    reply.data.uint8[i++] = 0x00;
  }

  for (; i < AFFA3_PACKET_LEN; i++)
    reply.data.uint8[i] = AFFA3_PACKET_FILLER;
    bool a = CAN0.sendFrame(reply);
    if (a)
    {
      Serial.println("final answer sended");
    }
    else
    {
      Serial.println("final answer not sended");
    }
   
// Print the CAN frame contents
  Serial.print("ID: 0x");
  Serial.print(reply.id, HEX);
  Serial.print(" DLC: ");
  Serial.print(reply.length);
  Serial.print(" Data: ");
  for (int i = 0; i < reply.length; i++) {
      Serial.print(reply.data.uint8[i], HEX);
      Serial.print(" ");
  }
  Serial.println();

}

uint32_t canId = 0x000;


void loop()
{
  
  //CAN_FRAME can_message;
  //affa3_tick();
  // if (CAN0.read(can_message))
  // {
   
  //   canId = can_message.id;
  //   affa3_recv(can_message);
    // if (canId == 0x3CF)
    // { // pong received  // TODO: check entire frame (not just can ID)
    //   //        CAN.sendMsgBuf(0x3DF, NULL, 8, pingMsg);
    //   // Serial.println("Pion");Serial.println();
    // }
    // //      else if(canId == 0x521) { // TODO: check entire frame (not just can ID)
    // //        Serial.println("TEXT ACK received"); Serial.println();
    // //      }
    // else if (canId == 0x1C1)
    // {
    //   CANsendMsgBuf(0x5C1, 0, 8, msg5c1);
    // }
    // else if (canId == 0x0A9)
    // {
    //   CANsendMsgBuf(0x4A9, 0, 8, msg4a9);
    // }
    // else
    // {
    //   //        Serial.print("CAN ID: "); Serial.println(canId, HEX); Serial.println();
    // }
  //}

  // serial interpreter:
  sCmd.readSerial(); // We don't do much, just process serial commands

  // TODO: periodically send sync command to display (100ms -> 1sec)
  //syncOK();
   
}

void messageTest()
{
  unsigned char msg11[8] = {0x10, 0x19, '~', 'q', 0x01, 'E', 'U', 'R'};
  unsigned char msg12[8] = {0x21, 'O', 'P', 'E', ' ', '2', 0x10, ' '};
  unsigned char msg13[8] = {0x22, 'E', 'U', 'R', 'O', 'P', 'E', ' '};
  unsigned char msg14[8] = {0x23, '2', ' ', 'P', '1', 0x00, 0x81, 0x81};

  CANsendMsgBuf(0x121, 0, 8, msg11);
  delay(1);
  CANsendMsgBuf(0x121, 0, 8, msg12);
  delay(1);
  CANsendMsgBuf(0x121, 0, 8, msg13);
  delay(1);
  CANsendMsgBuf(0x121, 0, 8, msg14);
  delay(1);
}

void messageTextIcons()
{
  unsigned char msg1[8] = {0x10, 0x1C, 0x7F, 0x55, 0x55, 0x3F, 0x60, 0x01};
  unsigned char msg2[8] = {0x21, 0x46, 0x4D, 0x20, 0x20, 0x20, 0x20, 0x20};
  unsigned char msg3[8] = {0x22, 0x20, 0x10, 0x52, 0x41, 0x44, 0x49, 0x4F};
  unsigned char msg4[8] = {0x23, 0x20, 0x46, 0x4D, 0x20, 0x20, 0x20, 0x20};
  unsigned char msg5[8] = {0x24, 0x00, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81};

  CANsendMsgBuf(0x121, 0, 8, msg1);
  delay(1);
  CANsendMsgBuf(0x121, 0, 8, msg2);
  delay(1);
  CANsendMsgBuf(0x121, 0, 8, msg3);
  delay(1);
  CANsendMsgBuf(0x121, 0, 8, msg4);
  delay(1);
  CANsendMsgBuf(0x121, 0, 8, msg5);
  delay(1);
}
 