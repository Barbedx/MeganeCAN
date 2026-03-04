#pragma once
#include <vector>
#include <functional>
#include <stdint.h>
#include "DiagPlanCommon.h"

inline std::vector<PidPlan> buildPlan_743() {
  std::vector<PidPlan> plan;

  // --- 2110 ---
  {
    PidPlan p; p.header = "743"; p.modePid = "2110"; p.needsSession = true;
    p.metrics = {
      {"PR_РЕЖИМ ДВИГАТЕЛЯ",     "PR116","rpm",  0, 0,
        [](const std::vector<uint8_t>& b){ return (float)U16(b,'F','G') / 8.0f; }, "GRPM"},
      {"PR_СКОРОСТЬ АВТОМОБИЛЯ", "PR099","km/h", 0, 0,
        [](const std::vector<uint8_t>& b){ return (float)U16(b,'H','I') / 100.0f; }, "GSPD"},
      {"PR_ТЕМПЕРАТУРА ВОДЫ",    "PR027","°C",   0, 0,
        [](const std::vector<uint8_t>& b){ return (float)U8(b,'N') - 40.0f; }, "GWTR"},
    };
    plan.push_back(p);
  }

  // --- 2112 ---
  {
    PidPlan p; p.header = "743"; p.modePid = "2112"; p.needsSession = true;
    p.metrics = {
      {"PR_УРОВЕНЬ ТОПЛИВА",               "PR035","L",   0, 60,
        [](const std::vector<uint8_t>& b){ return (float)U16(b,'C','D') / 1000.0f; }, "FUEL"},
      {"PR_ПОДАЧА ТОПЛИВА",                "PR112","L/h", 0, 0,
        [](const std::vector<uint8_t>& b){ return (float)U8(b,'O'); }, "FC"},
      {"PR_НАПРЯЖЕНИЕ АКБ 12 В",           "PR110","V",   7.5f, 16.0f,
        [](const std::vector<uint8_t>& b){ return (float)U16(b,'V','W') / 1000.0f; }, "GBAT"},
      {"PR_НАПРЯЖЕНИЕ РЕОСТАТА ОСВЕЩЕНИЯ", "PR111","V",   0.0f, 8.5f,
        [](const std::vector<uint8_t>& b){ return (float)U16(b,'X','Y') / 1000.0f; }, "RHEO"},
      {"PR_НАРУЖНАЯ ТЕМПЕРАТУРА",          "PR109","°C",  0, 0,
        [](const std::vector<uint8_t>& b){ return (float)U8(b,'Z') - 40.0f; }, "GOUT"},
    };
    plan.push_back(p);
  }

  return plan;
}
