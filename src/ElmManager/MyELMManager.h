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
#include "PidPlan_743.h"  
#include "PidPlan_744.h"
#include "PidPlan_74D.h"  

#include "DiagPlanCommon.h"
// -------- Data structures --------
// struct QueryData
// {
//     const char *modeAndPid;                         // e.g. "2112", "21C1"
//     const char *name;                               // human label
//     std::function<float(const uint8_t *)> evalFunc; // value extraction
// };
// struct PidPlan {
//   const char* header;     // "7E0"
//   const char* cmd;        // "21A0", "21C1", ...
//   bool needSession;       // true for 7E0
//   std::vector<Metric> metrics;
// };

// struct Metric {
//   const char* name;  // "ECT", "IAT", "MAP", ...
//   const char* unit;  // "°C","mbar","V","km/h","bar"
//   std::function<float(const std::vector<uint8_t>&)> eval; // uses letter helper
// };
// struct QueryGroup
// {
//     const char *header;             // 3-hex CAN ID, e.g. "743"
//     std::vector<QueryData> queries; // all PIDs inside this header
// };
inline std::vector<PidPlan> buildCombinedPlan() {
  auto p7e0 = buildS3000_Plan_7E0();
  auto p744 = buildPlan_743();
  auto p74d = buildPlan_74D();

  std::vector<PidPlan> out;
  out.reserve(p7e0.size() + p744.size() + p74d.size());

  // Group by header → fewer ATSH/10C0 switches:
  out.insert(out.end(), p7e0.begin(), p7e0.end()); // 7E0 (engine)
  out.insert(out.end(), p744.begin(), p744.end()); // 744 (HVAC)
  out.insert(out.end(), p74d.begin(), p74d.end()); // 74D (gear/alt)
  return out;
}
// -------- Manager --------
class MyELMManager
{
public:
    MyELMManager(IDisplay &display) : display(display)
    {
        // plan = buildS3000_Plan_7E0();
        // queryList.clear();
        // queryList = buildS3000_Plan_7E0();
        // build the plan with explicit types (NO auto in lambda params)
    }

    // Call frequently from loop()
    void tick();

    // Expose latest metric values (by ShortName)
    bool getCached(const char *shortName, float &out) const
    {
        auto it = valueCache.find(String(shortName));
        if (it == valueCache.end())
            return false;
        out = it->second;
        return true;
    }

    enum QueryState
    {
        SEND_COMMAND,
        WAITING_RESP
    };
 String snapshotJson() const {
  String out = "{";
  bool first = true;

  auto emit = [&](const char* key, float val){
    if (!first) out += ",";
    first = false;
    out += "\""; out += key; out += "\":";
    out += String(val, 3);
  };

  // Iterate the plan to know which short names exist,
  // and emit those that are present in valueCache.
  for (const auto& node : plan) {
    for (const auto& m : node.metrics) {
      auto it = valueCache.find(String(m.shortName));
      if (it != valueCache.end()) emit(m.shortName, it->second);
    }
  }
  out += "}";
  return out;
}

private:
    bool ensureWifi();
    bool connectTcpOnce(uint16_t port);
    void disconnectTcp();
    bool initElmOnce();

    // --------- low-level helpers ----------
    void sendHeaderIfNeeded(const char *header);
    bool openSessionIfNeeded(const char *header);
    bool sendTesterIfDue(const char *header);

    // Parse ELM multi-line ASCII hex into bytes, then strip 61/62/50 xx
    std::vector<uint8_t> decodeToUdsData(const char *ascii, size_t len);

    // --------- members ----------
    WiFiClient wifiClient;
    ELM327 elm;
    IDisplay &display;

    IPAddress host = IPAddress(192, 168, 0, 10);
    uint16_t port = 35000;

    QueryState nb_query_state = SEND_COMMAND; // state machine
    size_t currentQuery = 0;                  // index into your plan/queries
    unsigned long lastQueryTime = 0;          // ms timestamp for pacing

    std::vector<PidPlan> plan =  buildCombinedPlan();
    size_t planIndex = 0;
    bool ensureTcpAndElm(); 
    struct Sess
    {
        bool open = false;
        uint32_t lastMs = 0;
    };
    std::map<String, Sess> sessions; // key = header

    // simple value cache (ShortName -> value)
    std::map<String, float> valueCache;

    // state flags for current in-flight command
    bool waiting = false;
    bool waitingHeaderOK = false;
    bool waitingSDS = false;
    bool waitingPing = false;
    bool waitingPid = false;

    String currentHeader = ""; // last ATSH set

    // timings
    uint32_t lastGoodRxMs = 0;
    uint32_t lastCmdMs = 0;

    // tune as needed
    static constexpr uint32_t kCmdIntervalMs = 120; // min gap between commands
    static constexpr uint32_t kPingPeriodMs = 1200; // tester present when idle
    static constexpr uint32_t kReopenSdsMs = 2500;  // if idle this long, reopen SDS
    static constexpr uint32_t kDeadLinkMs = 8000;   // liveness watchdog

    // reconnect backoff
    uint32_t nextTcpAttemptMs = 0;
    uint32_t tcpBackoffMs = 1000;
    static constexpr uint32_t kBackoffMaxMs = 30000;

    bool elmReady = false;
};
