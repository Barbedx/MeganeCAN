#pragma once

#include <Arduino.h>
#include <vector>
#include <functional>
#include <map>
#include <WiFi.h>

#include "ELMduino.h"
#include "display/IDisplay.h"
#include "commands/DisplayCommands.h"
#include "PidPlan_7E0.h"
#include "PidPlan_74D.h"
#include "PidPlan_743.h"
#include "PidPlan_744.h"
#include "PidPlan_745.h"
#include "DiagPlanCommon.h"

inline std::vector<PidPlan> buildCombinedPlan()
{
    auto p7e0 = buildPlan_7E0();
    auto p743 = buildPlan_743();
    auto p744 = buildPlan_744();
    auto p745 = buildPlan_745();
    auto p74d = buildPlan_74D();

    std::vector<PidPlan> out;
    out.reserve(p7e0.size() + p743.size() + p744.size() + p745.size() + p74d.size());
    out.insert(out.end(), p7e0.begin(), p7e0.end()); // 7E0 (engine)
    out.insert(out.end(), p743.begin(), p743.end()); // 743 (gearbox)
    out.insert(out.end(), p744.begin(), p744.end()); // 744 (HVAC)
    out.insert(out.end(), p745.begin(), p745.end()); // 745
    out.insert(out.end(), p74d.begin(), p74d.end()); // 74D (gear/alt)
    return out;
}

class MyELMManager
{
public:
    MyELMManager(IDisplay &display) : display(display) {}

    // Call frequently from loop()
    void tick();

    bool getCached(const char *shortName, float &out) const
    {
        auto it = valueCache.find(String(shortName));
        if (it == valueCache.end()) return false;
        out = it->second;
        return true;
    }

    String snapshotJson() const
    {
        String out = "{";
        bool first = true;
        auto emit = [&](const char *key, float val) {
            if (!first) out += ",";
            first = false;
            out += "\""; out += key; out += "\":";
            out += String(val, 3);
        };
        for (const auto &node : plan)
            for (const auto &m : node.metrics) {
                auto it = valueCache.find(String(m.shortName));
                if (it != valueCache.end()) emit(m.shortName, it->second);
            }
        out += "}";
        return out;
    }

private:
    // ---- connection helpers ----
    bool ensureWifi();
    bool connectTcpOnce(uint16_t p);
    void disconnectTcp();
    bool initElmOnce();
    bool ensureTcpAndElm();

    // ---- decoding ----
    std::vector<uint8_t> decodeToUdsData(const char *ascii, size_t len);

    // ---- hw ----
    WiFiClient wifiClient;
    ELM327     elm;
    IDisplay  &display;

    IPAddress host = IPAddress(192, 168, 0, 10);
    uint16_t  port = 35000;

    // ---- plan ----
    std::vector<PidPlan> plan = buildCombinedPlan();
    size_t planIndex = 0;

    // ---- per-header session tracking ----
    struct Sess {
        bool     open  = false;
        uint32_t lastMs = 0;
    };
    std::map<String, Sess>  sessions;
    std::map<String, float> valueCache;

    // ---- state machine ----
    enum class WaitState { IDLE, HEADER, SDS, TESTER_PRESENT, PID };
    WaitState waitState   = WaitState::IDLE;
    String    pendingHeader;   // target header for ATSH in flight
    String    currentHeader;   // last committed header

    // ---- timings ----
    uint32_t lastGoodRxMs = 0;
    uint32_t lastCmdMs    = 0;

    static constexpr uint32_t kCmdIntervalMs = 120;   // min gap between commands
    static constexpr uint32_t kPingPeriodMs  = 1200;  // tester-present period
    static constexpr uint32_t kReopenSdsMs   = 2500;  // re-open SDS after idle
    static constexpr uint32_t kDeadLinkMs    = 8000;  // liveness watchdog

    // ---- reconnect backoff ----
    uint32_t nextTcpAttemptMs = 0;
    uint32_t tcpBackoffMs     = 1000;
    static constexpr uint32_t kBackoffMaxMs  = 30000;

    bool elmReady = false;
};
