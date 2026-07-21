// GW board firmware entry (M1, ARCHITECTURE-V2 §5) — ESP32-WROVER, the only
// application processor (§0). Merges the C3 brain stack (BLE, WiFi, web, media
// + key routing) with the P1 vehicle stack (vehicle CAN, Raise canbox, HU RX,
// capture logger, link tunnel). The OEM display is remote: RemoteDisplay
// serializes IDisplay + now-playing over the link to the peripheral (IOC).
#include <Arduino.h>
#include <time.h>
#include <WiFi.h>
#include <ElegantOTA.h>
#include <esp_task_wdt.h>
#include <secrets.h>

#include "server/HttpServerManager.h"
#include "wifi_manager.h"
#include "bluetooth.h"
#include "ble/BleHub.h"
#include "utils/AppConfig.h"
#include "utils/CanLog.h"
#include "utils/Log.h"
#include "media/AmsMediaSource.h"
#include "media/MediaRouter.h"
#include "keys/KeyRouter.h"
#include "keys/AmsKeySink.h"
#include "keys/HidKeySink.h"
#include "ElmManager/MyELMManager.h"

#include "vh/VehicleDecoder.h"
#include "vh/CanboxEmitter.h"
#include "vh/HuRxParser.h"
#include "vh/target/TwaiCanBus.h"
#include "vh/target/VhConfig.h"
#include "vh/target/FsLogger.h"
#include "vh/target/LinkTunnel.h"
#include "link/LinkPort.h"
#include "RemoteDisplay.h"
#include "GwLink.h"
#include "CanboxKeySink.h"

// ---- pins (§3.2) ------------------------------------------------------------
static constexpr gpio_num_t PIN_CAN_TX = GPIO_NUM_22;   // listen-only; wired for future
static constexpr gpio_num_t PIN_CAN_RX = GPIO_NUM_21;
static constexpr int PIN_HU_TX = 25, PIN_HU_RX = 26;    // UART1 -> DUDU7, 38400

// ---- modules ----------------------------------------------------------------
static RemoteDisplay g_display;
Preferences preferences;
static HttpServerManager* serverManager = nullptr;
static MyELMManager* elmManager = nullptr;

static AmsMediaSource g_amsSource;
MediaRouter g_mediaRouter;

static TwaiCanBus         g_vehicleCan;
static Vh::VehicleDecoder g_decoder;
static Vh::CanboxEmitter  g_canbox;
static Vh::HuRxParser     g_huRx;
static VhFs::FsLogger     g_fsLog;
static LinkTunnel         g_tunnel;   // served over the link (GW's own maintenance)

static AmsKeySink    g_amsKeySink;
static HidKeySink    g_hidKeySink;
static CanboxKeySink g_canboxKeySink(g_canbox);
KeyRouter g_keyRouter;

String btMode = "ams";
static bool _autoTime = true;
static bool _timeSyncDone = false;
static uint32_t g_cfgGen = 0;

const char *ssid = Soft_AP_WIFI_SSID;
const char *password = Soft_AP_WIFI_PASS;

// ---- stubs for main.cpp externs the shared web routes reference -------------
// The GW has no local display emulation / virtual twin / image capture; the
// bench-RE routes answer honestly instead of dangling.
void setFullEmu(bool) {}
const char* setDisplayRoute(const String&) { return "remote"; }
String imageCaptureJson() { return "{\"declared\":0,\"got\":0,\"complete\":false,\"hex\":\"\"}"; }
String fullEmuScreenJson() { return "{\"on\":false,\"route\":\"remote\",\"screenAge_ms\":-1}"; }

// ---- keys / media -----------------------------------------------------------
bool HandleKey(AffaCommon::AffaKey key, bool isHold)
{
    return g_keyRouter.route(key, isHold);
}

// A source changed its now-playing state. The callback fires on the SOURCE's
// task (NimBLE host for AMS), so it only raises a flag — the loop task does the
// actual snapshot + link push, keeping RemoteDisplay and LinkPort single-writer.
static volatile bool s_mediaDirty = false;
static void onMediaChange(IMediaSource &, void *)
{
    s_mediaDirty = true;
}

static void pushMediaFromLoop()
{
    if (!s_mediaDirty) return;
    s_mediaDirty = false;
    IMediaSource* src = g_mediaRouter.activeSource();
    if (src == (IMediaSource*)&g_amsSource)
        g_display.pushMedia(g_amsSource.snapshot());   // mutex-guarded copy
    else if (src)
        g_display.pushMedia(src->current());           // loop-task sources
}

// ---- canbox UART ------------------------------------------------------------
static void huWrite(const uint8_t* frame, uint8_t len, void*)
{
    Serial1.write(frame, len);
}

static void onHuFrame(uint8_t cmd, const uint8_t* payload, uint8_t len,
                      bool known, void*)
{
    g_fsLog.onHuFrame(cmd, payload, len, millis());
    // P4 RE window: unknown frames (the DUDU media-metadata candidates) stay
    // visible live on /vh even with no capture running.
    if (!known)
        GwLink::noteHuUnknown(cmd, payload, len);
}

// ---- vehicle CAN ------------------------------------------------------------
static void onVehicleFrame(const Frame& f, void*)
{
    uint32_t now = millis();
    uint32_t knownBefore = g_decoder.knownFrames();
    g_decoder.feed(f, now);
    g_fsLog.onCanFrame((uint16_t)f.id, f.data, f.len,
                       g_decoder.knownFrames() != knownBefore, now);
}

static void applyVhConfig()
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
    Serial.println("=== MeganeCAN GW board ===");
    Serial.printf("PSRAM %s\n", psramFound() ? "yes" : "NO");

    AppConfig::Load();
    VhConfig::load();
    btMode    = AppConfig::btMode;
    _autoTime = AppConfig::autoTime;
    g_fsLog.begin();

    // Canbox UART to the DUDU7 (38400 8N1, §8.4).
    Serial1.begin(38400, SERIAL_8N1, PIN_HU_RX, PIN_HU_TX);
    g_canbox.begin(huWrite, nullptr);
    g_huRx.onFrame(onHuFrame, nullptr);

    // Media routing: AMS is the only source in M1 (the DUDU metadata source is
    // P4); the router's pick streams to the peripheral's display.
    g_mediaRouter.setSources(&g_amsSource, /*hu=*/nullptr);
    g_mediaRouter.setMode(MediaRouter::modeFromStr(AppConfig::mediaSource.c_str()));
    g_amsSource.setChangeCallback(onMediaChange, nullptr);

    // Key routing: SINK_HU is now the LOCAL canbox (no link hop).
    g_keyRouter.bind(KeyRouter::SINK_AMS, &g_amsKeySink);
    g_keyRouter.bind(KeyRouter::SINK_HID, &g_hidKeySink);
    g_keyRouter.bind(KeyRouter::SINK_HU,  &g_canboxKeySink);
    g_keyRouter.configure(AppConfig::keySinks);
    LOGI("KEYS", "key_sinks=0x%02X media_source=%s",
         AppConfig::keySinks, AppConfig::mediaSource.c_str());

    // Link to the peripheral: KEY_EVT in -> router; DISP_CMD/MEDIA_TEXT out.
    // GwLink wires the tunnel to its LinkPort internally.
    GwLink::begin(&g_keyRouter, &g_decoder, &g_canbox, &g_fsLog, &g_tunnel);
    g_display.begin([](uint8_t type, const uint8_t* p, uint16_t n, void*) {
        return GwLink::send(type, p, n);
    }, nullptr);

    // BLE before WiFi (radio bring-up order, same as the C3 main).
    {
        const BleHub::Mode bleMode =
            btMode == "keyboard" ? BleHub::Mode::Keyboard
          : btMode == "both"     ? BleHub::Mode::Both
                                 : BleHub::Mode::Ams;
        if (BleHub::amsActive(bleMode))
            g_amsSource.begin();
        static BleHub::Mode s_mode = bleMode;
        xTaskCreate([](void*) {
            BleHub::Begin(s_mode, "MCD1");
            vTaskDelete(nullptr);
        }, "bt_begin", 16384, nullptr, 1, nullptr);
        LOGI("BT", "BLE init launched (mode=%s)", btMode.c_str());
    }

    WiFiManager::Begin(ssid, password, "meganecan");
    elmManager = new MyELMManager(g_display);
    serverManager = new HttpServerManager(g_display, preferences);
    serverManager->attachElm(elmManager);
    serverManager->attachMedia(&g_mediaRouter);
    serverManager->begin();

    // Vehicle CAN through the ICanBus seam (CR-04), listen-only (§5 safety).
    g_vehicleCan.onReceive(onVehicleFrame, nullptr);
    if (g_vehicleCan.begin(PIN_CAN_TX, PIN_CAN_RX, /*listenOnly=*/true))
        LOGI("CAN", "vehicle TWAI up (listen-only, 500k)");
    else
        LOGW("CAN", "vehicle TWAI INSTALL FAILED");

    applyVhConfig();
    CanLog::begin();

    esp_task_wdt_init(5, true);
    esp_task_wdt_add(nullptr);
    LOGI("SYS", "GW up");
}

void loop()
{
    esp_task_wdt_reset();
    uint32_t now = millis();

    ElegantOTA.loop();
    BleHub::Service();

    if (BleHub::amsActive())
    {
        // Auto-time: once per BT connection via CTS -> GW clock + peripheral.
        if (_autoTime && Bluetooth::IsConnected() && Bluetooth::IsTimeSet() && !_timeSyncDone)
        {
            struct tm t;
            if (getLocalTime(&t))
            {
                char buf[5];
                snprintf(buf, sizeof(buf), "%02d%02d", t.tm_hour, t.tm_min);
                g_display.setTime(buf);              // DISP_CMD to the panel
                GwLink::sendTime((uint32_t)time(nullptr));
                _timeSyncDone = true;
                LOGI("BT", "Auto-time synced: %s", buf);
            }
        }
        if (!Bluetooth::IsConnected())
            _timeSyncDone = false;
    }

    WiFiManager::Handle();

    // Vehicle side
    g_vehicleCan.poll();
    g_decoder.tick(now);
    g_canbox.tick(g_decoder.state(), now);
    uint8_t hb[64];
    int hn;
    while ((hn = Serial1.read(hb, sizeof(hb))) > 0)
        g_huRx.feed(hb, (uint16_t)hn);

    // Link + maintenance + media refresh
    pushMediaFromLoop();
    GwLink::service(now);
    g_tunnel.service(now);
    g_fsLog.service(now);
    g_display.service(now);
    if (VhConfig::generation() != g_cfgGen)
        applyVhConfig();

    // Heap watchdog (same discipline as the C3 main; PSRAM eases it but keep eyes on)
    static uint32_t lastHeapLog = 0;
    if (now - lastHeapLog > 10000)
    {
        lastHeapLog = now;
        LOGI("HEAP", "free=%u min=%u maxblk=%u psram=%u",
             (unsigned)ESP.getFreeHeap(),
             (unsigned)ESP.getMinFreeHeap(),
             (unsigned)ESP.getMaxAllocHeap(),
             (unsigned)ESP.getFreePsram());
    }
}
