#pragma once
#include <vector>
#include <functional>
#include <stdint.h>
#include "DiagPlanCommon.h"

// -------- 0x744 — HVAC (PIDs 21C1, 21C2) with SDS --------
inline std::vector<PidPlan> buildPlan_744() {
  std::vector<PidPlan> plan;

  // 21C1 — temps, battery, sun sensor
  {
    PidPlan p; p.header = "744"; p.modePid = "21C1"; p.needsSession = true;
    p.metrics = {
      {"PR_НАРУЖНАЯ ТЕМПЕРАТУРА",  "PR002", "°C", -40, 60,
        [](const std::vector<uint8_t>& b){ return (float)U8(b,'B') - 40.0f; }, "OUT"},
      {"PR_ВНУТРЕННЯЯ ТЕМПЕРАТУРА","PR001", "°C", -40, 80,
        [](const std::vector<uint8_t>& b){ return 0.5f * (float)U8(b,'G') - 40.0f; }, "IN"},
      {"PR_ТЕМПЕРАТУРА ИСПАРИТЕЛЯ","PR003", "°C", -40, 87,
        [](const std::vector<uint8_t>& b){ return 0.5f * (float)U8(b,'F') - 40.0f; }, "EVAP"},
      {"PR_ТЕМПЕРАТУРА ВОДЫ",      "PR004", "°C", -40, 214,
        [](const std::vector<uint8_t>& b){ return (float)U8(b,'D') - 40.0f; }, "WTR"},
      {"PR_НАПРЯЖЕНИЕ АКБ 12 В",   "PR092", "V",  10.5f, 16.0f,
        [](const std::vector<uint8_t>& b){ return 0.078f * (float)U8(b,'J'); }, "BAT"},
      {"PR_ИНТЕНСИВНОСТЬ СОЛНЕЧНОГО СВЕТА","PR006","W/m2", 0, 400,
        [](const std::vector<uint8_t>& b){ return 3.0f * (float)U8(b,'H'); }, "SUN"},
    };
    plan.push_back(p);
  }

  // 21C2 — blower PWM, setpoints, flaps, vehicle speed proxy, heaters
  {
    PidPlan p; p.header = "744"; p.modePid = "21C2"; p.needsSession = true;
    p.metrics = {
      {"PR_ЗАПРОС НАГРЕВАТЕЛЬНЫХ РЕЗИСТОРОВ",       "PR137","W",   0, 3000,
        [](const std::vector<uint8_t>& b){ return 100.0f * (float)((U8(b,'C') >> 3) & 0x1F); }, "HTR"},
      {"PR_УСТАНОВКА PWM ЭЛ. ВЕНТИЛЯТ. САЛОНА",     "PR019","%",   0, 100,
        [](const std::vector<uint8_t>& b){ return 0.5f * (float)U8(b,'M'); }, "BLWR"},
      {"PR_УСТАВКА ТЕМПЕРАТУРЫ ПОДАВАЕМОГО ВОЗДУХА","PR121","°C",  0, 80,
        [](const std::vector<uint8_t>& b){ return (float)U8(b,'N') - 40.0f; }, "TASP"},
      {"PR_ПОЛОЖЕНИЕ СМЕСИТЕЛЬНОЙ ЗАСЛОНКИ",        "PR012","%",   0, 100,
        [](const std::vector<uint8_t>& b){ return (float)U8(b,'P'); }, "FMIX"},
      {"PR_ПОЛОЖЕНИЕ РАСПРЕДЕЛИТ. ЗАСЛОНКИ",        "PR011","%",   0, 100,
        [](const std::vector<uint8_t>& b){ return (float)U8(b,'Q'); }, "FDST"},
      {"PR_НОЧНОЙ УРОВЕНЬ ОСВЕЩЕНИЯ",               "PR122","%",   0, 100,
        [](const std::vector<uint8_t>& b){ return 0.4f * (float)U8(b,'F'); }, "NITE"},
      {"PR_СКОРОСТЬ АВТОМОБИЛЯ",                    "PR095","km/h",0, 250,
        [](const std::vector<uint8_t>& b){ return 2.56f * (float)U8(b,'H'); }, "SPD2"},
    };
    plan.push_back(p);
  }

  return plan;
}
