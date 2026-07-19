// VH board firmware entry (ARCHITECTURE-V2 §5) — ESP32-WROVER on the vehicle
// main CAN. Radio-less by design: WiFi/BT are never initialized (nothing here
// links them — this is also what makes SuperMini-LDO power viable, §3.1).
//
//   TWAI listen-only GPIO21/22  -> VehicleDecoder -> VehicleState
//   VehicleState                -> CanboxEmitter  -> UART1 (GPIO25/26, DUDU7)
//   UART1 RX                    -> HuRxParser     -> HU_STATUS / capture
//   UART2 (GPIO18/19)           -> LinkProto      -> MM board (the only UI)
//
// If MM is absent, VH runs canbox duty headless on its NVS config.
#include <Arduino.h>
#include <esp_task_wdt.h>
#include "driver/twai.h"

#include "../VehicleDecoder.h"
#include "../CanboxEmitter.h"
#include "../HuRxParser.h"
#include "../../link/LinkPort.h"
#include "../../link/HwSerialLinkStream.h"
#include "../../bus/ArduinoClock.h"
#include "VhConfig.h"
#include "FsLogger.h"
#include "LinkTunnel.h"

// ---- identity ---------------------------------------------------------------
static constexpr uint16_t VH_FW_VER = 0x0100;   // 1.0
static constexpr uint32_t VH_CAPS   = 0x7;      // bit0 canbox, bit1 capture, bit2 ota

// ---- pins (§3.2; GPIO16/17 are PSRAM-reserved on WROVER — never use) --------
static constexpr gpio_num_t PIN_CAN_TX = GPIO_NUM_22;  // unused in listen-only, wired for future
static constexpr gpio_num_t PIN_CAN_RX = GPIO_NUM_21;
static constexpr int PIN_HU_TX = 25, PIN_HU_RX = 26;   // UART1 -> DUDU7, 38400
static constexpr int PIN_MM_TX = 18, PIN_MM_RX = 19;   // UART2 -> MM link, 460800

// ---- modules ----------------------------------------------------------------
static Vh::VehicleDecoder g_decoder;
static Vh::CanboxEmitter  g_canbox;
static Vh::HuRxParser     g_huRx;
static VhFs::FsLogger     g_fsLog;
static LinkPort           g_link;
static HwSerialLinkStream g_linkStream;
static LinkTunnel         g_tunnel;

static uint32_t g_lastCanRxMs = 0;
static uint32_t g_cfgGen = 0;

// ---- canbox UART out (whole frames only — one write per frame) --------------
static void huWrite(const uint8_t* frame, uint8_t len, void*)
{
    Serial1.write(frame, len);
}

// ---- AffaKey (Mégane SWC) -> Raise RAV4 key code map ------------------------
// P3 tunes this on the real DUDU7; the mapping lives here (not MM) so the HU
// profile owns its own codes.
static uint8_t affaToRav4(uint16_t affaKey)
{
    switch (affaKey)
    {
    case 0x0003: return 0x01;   // VolumeUp  -> VOL+
    case 0x0004: return 0x02;   // VolumeDown-> VOL-
    case 0x0005: return 0x16;   // Pause     -> OK (play/pause in DUDU UI)
    case 0x0101: return 0x85;   // RollUp    -> NEXT
    case 0x0141: return 0x86;   // RollDown  -> PREV
    case 0x0001: return 0x07;   // SrcRight  -> SOURCE
    case 0x0002: return 0x88;   // SrcLeft   -> TEL
    case 0x0000: return 0x08;   // Load      -> VOICE
    default:     return 0;
    }
}

// ---- link RX ----------------------------------------------------------------
static void onLinkMsg(uint8_t type, const uint8_t* p, uint16_t len, void*)
{
    if (g_tunnel.handle(type, p, len))
        return;

    switch (type)
    {
    case LinkProto::KEY_EVT:
        // {affaKey u16 LE, edge}; forwarded with priority over telemetry —
        // sendKey emits immediately, ahead of the schedulers (§4).
        if (len >= 3 && VhConfig::keyForward)
        {
            uint16_t key = (uint16_t)p[0] | ((uint16_t)p[1] << 8);
            uint8_t code = affaToRav4(key);
            if (code)
                g_canbox.sendKey(code, p[2]);
        }
        break;
    case LinkProto::TIME:
        // {unix u32 LE} — stamp the system clock (log timestamps only).
        if (len >= 4)
        {
            struct timeval tv = {};
            tv.tv_sec = (time_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8)
                       | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
            settimeofday(&tv, nullptr);
        }
        break;
    default:
        break;
    }
}

// ---- HU RX ------------------------------------------------------------------
static void onHuFrame(uint8_t cmd, const uint8_t* payload, uint8_t len,
                      bool known, void*)
{
    uint32_t now = millis();
    g_fsLog.onHuFrame(cmd, payload, len, now);

    // Live HU_STATUS for the MM dashboard: source/volume/freq from the
    // documented commands.
    if (cmd == 0xC0 || cmd == 0xC2 || cmd == 0xC4 || cmd == 0x81)
    {
        uint8_t st[6] = { 1, /*source*/0, /*vol*/0, /*freq lo*/0, /*freq hi*/0, cmd };
        static uint8_t s_source = 0, s_vol = 0;
        static uint16_t s_freq = 0;
        if (cmd == 0xC0 && len >= 2) s_source = payload[1];
        if (cmd == 0xC4 && len >= 1) s_vol = payload[0];
        if (cmd == 0xC2 && len >= 3) s_freq = (uint16_t)((payload[1] << 8) | payload[2]);
        st[1] = s_source; st[2] = s_vol;
        st[3] = (uint8_t)(s_freq & 0xFF); st[4] = (uint8_t)(s_freq >> 8);
        g_link.send(LinkProto::HU_STATUS, st, sizeof(st));
    }

    // The RE capture path (P4: the undocumented DUDU media-metadata frame):
    // every unknown frame also streams to MM as RAW_FRAME bus=2, rate-limited —
    // bulk capture belongs to the local FS log.
    if (!known)
    {
        static uint32_t s_lastMs = 0;
        static uint8_t  s_inWindow = 0;
        if (now - s_lastMs > 1000) { s_lastMs = now; s_inWindow = 0; }
        if (s_inWindow < 20)
        {
            s_inWindow++;
            uint8_t raw[4 + Vh::HuRxParser::MAX_PAYLOAD];
            raw[0] = 2;                     // bus 2 = HU UART
            raw[1] = cmd; raw[2] = 0;       // "id" = cmd
            raw[3] = len;
            memcpy(raw + 4, payload, len);
            g_link.send(LinkProto::RAW_FRAME, raw, (uint16_t)(4 + len));
        }
    }
}

// ---- telemetry to MM --------------------------------------------------------
static void sendSigBatch(uint32_t now)
{
    // All known signals, 5B each ({sig_id, i32 LE}) — 24 sigs max = 120B ≤ 123.
    const Vh::VehicleState& s = g_decoder.state();
    uint8_t p[LinkProto::MAX_PAYLOAD];
    uint16_t n = 0;
    for (uint8_t i = 0; i < Vh::SIG_COUNT; i++)
    {
        if (!s.has(i)) continue;
        if (n + 5 > sizeof(p)) break;
        int32_t v = s.val[i];
        p[n++] = i;
        p[n++] = (uint8_t)(v & 0xFF);
        p[n++] = (uint8_t)((v >> 8) & 0xFF);
        p[n++] = (uint8_t)((v >> 16) & 0xFF);
        p[n++] = (uint8_t)((v >> 24) & 0xFF);
    }
    if (n)
        g_link.send(LinkProto::SIG_BATCH, p, n);
    (void)now;
}

// ---- config -> runtime ------------------------------------------------------
static void applyConfig()
{
    g_canbox.setEnabled(VhConfig::canboxEnabled);
    g_canbox.setRpmScale(VhConfig::rpmMul, VhConfig::rpmDiv);
    for (uint8_t i = 0; i < Vh::SIG_COUNT; i++)
        g_decoder.setSignalEnabled(i, !(VhConfig::sigDisable & (1u << i)));
    g_cfgGen = VhConfig::generation();
}

void setup()
{
    Serial.begin(115200);
    delay(500);
    Serial.println("=== MeganeCAN VH board ===");
    Serial.printf("fw 0x%04X, PSRAM %s\n", VH_FW_VER, psramFound() ? "yes" : "NO");

    VhConfig::load();
    g_fsLog.begin();

    // Canbox UART to the DUDU7 (38400 8N1, 3.3V TTL direct — §8.4).
    Serial1.begin(38400, SERIAL_8N1, PIN_HU_RX, PIN_HU_TX);
    // Inter-board link to MM.
    Serial2.begin(460800, SERIAL_8N1, PIN_MM_RX, PIN_MM_TX);

    g_canbox.begin(huWrite, nullptr);
    g_huRx.onFrame(onHuFrame, nullptr);
    g_linkStream.bind(Serial2);
    g_link.begin(g_linkStream, defaultClock(), VH_FW_VER, VH_CAPS);
    g_link.onMessage(onLinkMsg, nullptr);
    g_tunnel.begin(g_link, g_fsLog);
    applyConfig();

    // TWAI listen-only: physically incapable of ACKing or erroring the vehicle
    // bus (§5 safety). TX pin is configured but never driven in this mode.
    twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(PIN_CAN_TX, PIN_CAN_RX,
                                                          TWAI_MODE_LISTEN_ONLY);
    g.rx_queue_len = 32;
    twai_timing_config_t t = TWAI_TIMING_CONFIG_500KBITS();
    twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();
    if (twai_driver_install(&g, &t, &f) == ESP_OK && twai_start() == ESP_OK)
        Serial.println("[can] TWAI up (listen-only, 500k)");
    else
        Serial.println("[can] TWAI INSTALL FAILED");

    // 5s watchdog (§8.5). CAN-silence and link-loss are states, not reboots —
    // the HU may be on while the car is off.
    esp_task_wdt_init(5, true);
    esp_task_wdt_add(nullptr);

    Serial.println("[sys] VH up");
}

void loop()
{
    esp_task_wdt_reset();
    uint32_t now = millis();

    // Vehicle CAN in (drain the queue, non-blocking).
    twai_message_t msg;
    while (twai_receive(&msg, 0) == ESP_OK)
    {
        if (msg.rtr) continue;
        g_lastCanRxMs = now;
        Frame f;
        f.id = msg.identifier;
        f.extended = msg.extd;
        f.source = Frame::SRC_VH_CAN;
        f.len = msg.data_length_code > 8 ? 8 : msg.data_length_code;
        for (int i = 0; i < f.len; i++) f.data[i] = msg.data[i];

        // knownFrames() only moves for table-matched ids — the delta tells the
        // capture filter whether this frame was "known".
        uint32_t knownBefore = g_decoder.knownFrames();
        g_decoder.feed(f, now);
        g_fsLog.onCanFrame((uint16_t)f.id, f.data, f.len,
                           g_decoder.knownFrames() != knownBefore, now);
    }

    g_decoder.tick(now);
    g_canbox.tick(g_decoder.state(), now);

    // HU UART in.
    uint8_t hb[64];
    int hn;
    while ((hn = Serial1.read(hb, sizeof(hb))) > 0)
        g_huRx.feed(hb, (uint16_t)hn);

    // Link + maintenance plane.
    g_link.service();
    g_tunnel.service(now);
    g_fsLog.service(now);

    if (VhConfig::generation() != g_cfgGen)
        applyConfig();

    // 1Hz telemetry to MM (only while the link is up — no point queueing).
    static uint32_t s_lastBatch = 0;
    if (g_link.up() && now - s_lastBatch >= 1000)
    {
        s_lastBatch = now;
        sendSigBatch(now);
    }

    // 10s heartbeat log line on the debug console.
    static uint32_t s_lastLog = 0;
    if (now - s_lastLog >= 10000)
    {
        s_lastLog = now;
        Serial.printf("[vh] can:%s known=%lu unk=%lu link:%s cap:%d heap=%u\n",
                      (now - g_lastCanRxMs < 2000) ? "live" : "silent",
                      (unsigned long)g_decoder.knownFrames(),
                      (unsigned long)g_decoder.unknownFrames(),
                      g_link.up() ? "up" : "down",
                      g_fsLog.active(),
                      (unsigned)ESP.getFreeHeap());
    }
}
