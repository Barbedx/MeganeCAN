// DISP board firmware entry (M2, ARCHITECTURE-V2 §6) — ESP32-C3 SuperMini as
// the thin IO Controller (CR-02: zero business logic). Owns the multimedia CAN
// + AFFA3 display drivers + SWC capture + USB console; everything else — media
// routing, key routing, config decisions, web — happens on GW and arrives over
// the link (MEDIA_TEXT / DISP_CMD in, KEY_EVT out). No BLE, no WiFi, no web.
#include <Arduino.h>
#include <Preferences.h>
#include <esp_task_wdt.h>

#include "display/UpdateList/UpdateListDisplay.h"
#include "display/UpdateList/UpdateListMenuDisplay.h"
#include "display/UpdateList/UpdateListBase.h"
#include "display/Carminat/CarminatDisplay.h"
#include "effects/ScrollEffect.h"
#include "bus/HwCanBus.h"
#include "bus/SerialMirrorTap.h"
#include "bus/ArduinoClock.h"
#include "wire/SerialWireLink.h"
#include "utils/WireProto.h"
#include "utils/CanUtils.h"
#include "utils/CanLog.h"
#include "utils/AppConfig.h"
#include "utils/Log.h"
#include "media/MediaRouter.h"
#include "vh/target/LinkTunnel.h"
#include "DispLink.h"
#include "DispConsole.h"

AffaDisplayBase *display = nullptr;
Preferences preferences;
// Referenced by the Carminat settings menu; on the thin peripheral the real
// auto-time decision lives on GW (TIME frames) — this is just menu state.
bool _autoTime = true;
MediaRouter g_mediaRouter;          // hu-only: the GW feeds it over the link
static LinkTunnel g_tunnel;         // GW configures/flashes this board
static SerialWireLink g_serialLink; // @TX/@RX serial proxy (bench RE)

// ---- SWC keys -> GW ---------------------------------------------------------
// The display's menu may consume a key locally (navigation); everything that
// falls through goes to GW's KeyRouter as an edge pair (press/long + release —
// a lost frame can never stick a key, §10).
bool HandleKey(AffaCommon::AffaKey key, bool isHold)
{
    uint16_t code = AffaCommon::to_uint16(key);
    DispLink::sendKey(code, isHold ? 2 : 1);
    DispLink::sendKey(code, 0);
    return true;
}

// GW's routed now-playing changed: push it into the local driver.
static void onMediaChange(IMediaSource &src, void *)
{
    if (display && g_mediaRouter.activeSource() == &src)
        display->setMediaInfo(src.current());
}

// ---- CAN RX -----------------------------------------------------------------
static void busRx(const Frame& f, void*)
{
    if (f.id != 0x3CF && f.id != 0x3AF && f.id != 0x7AF)
        CanUtils::printCanFrame(f, false);
    CanLog::onFrame(f.id, f.extended, f.len, f.data);
    if (display) display->recv(f);
}

void gotFrame(CAN_FRAME *frame)
{
    HwCanBus::instance().ingest(*frame);
}

static void restoreDisplay()
{
    preferences.begin("display", true);
    bool autoRestore = preferences.getBool("autoRestore", false);
    String savedText = preferences.getString("lastText", "");
    String welcomeText = preferences.getString("welcomeText", "");
    preferences.end();
    if (!autoRestore) return;

    display->setState(true);
    ScrollEffect(display, ScrollDirection::Left,
                 welcomeText.length() ? welcomeText.c_str()
                                      : "                  Welcome to MEGANE 2", 250);
    display->setText(savedText.length() ? savedText.c_str() : "MEGANE");
}

void setup()
{
    DispConsole::begin();
    AppConfig::Load();   // display_type + skip_funcreg drive this board
    LOGI("DISP", "type=%s skip_funcreg=%d",
         AppConfig::displayType.c_str(), (int)AppConfig::skipFuncReg);

    if (AppConfig::displayType == "carminat")
        display = new CarminatDisplay();
    else if (AppConfig::displayType == "updatelist")
        display = new UpdateListDisplay();
    else if (AppConfig::displayType == "updatelist_menu")
        display = new UpdateListMenuDisplay();
    else
        display = new UpdateListBase();

    display->setBus(HwCanBus::instance());
    display->setClock(defaultClock());
    display->setSkipFuncReg(AppConfig::skipFuncReg);
    display->setKeyHandler(HandleKey);

    // Media: the only source is the GW link; the router seam keeps the driver's
    // "waiting for source" status screen working ("HU link down" until GW talks).
    g_mediaRouter.setSources(/*ams=*/nullptr, &DispLink::mediaSource());
    g_mediaRouter.setMode(MediaRouter::Mode::Hu);
    DispLink::mediaSource().setChangeCallback(onMediaChange, nullptr);
    display->attachMediaRouter(&g_mediaRouter);

    DispLink::begin(display, &g_mediaRouter, &g_tunnel);

    // Serial proxy (@TX mirror) for the PC display emulator — unchanged bench flow.
    WireProto::addLink(&g_serialLink);
    static SerialMirrorTap s_mirror;
    HwCanBus::instance().addTap(&s_mirror);

    HwCanBus::instance().onReceive(busRx, nullptr);
    CAN0.setCANPins(GPIO_NUM_3, GPIO_NUM_4);
    CAN0.begin(CAN_BPS_500K);
    CAN0.setGeneralCallback(gotFrame);
    CAN0.watchFor();
    CanLog::begin();

    display->begin();
    restoreDisplay();

    esp_task_wdt_init(5, true);
    esp_task_wdt_add(nullptr);
    LOGI("SYS", "DISP up");
}

#define SYNC_INTERVAL_MS 1000
static uint32_t last_sync = 0;

void loop()
{
    esp_task_wdt_reset();
    uint32_t now = millis();

    DispConsole::loop();
    DispLink::service(now);
    g_tunnel.service(now);

    display->processEvents();
    display->tickMedia();   // link-fed media; no BLE gate on this board
    if (now - last_sync > SYNC_INTERVAL_MS)
    {
        last_sync = now;
        display->tick();
    }

    static uint32_t lastHeapLog = 0;
    if (now - lastHeapLog > 10000)
    {
        lastHeapLog = now;
        LOGI("HEAP", "free=%u min=%u maxblk=%u link=%s",
             (unsigned)ESP.getFreeHeap(),
             (unsigned)ESP.getMinFreeHeap(),
             (unsigned)ESP.getMaxAllocHeap(),
             DispLink::up() ? "up" : "down");
    }
}
