#pragma once
#include <vector>
#include <functional>
#include <stdint.h>
#include "DiagPlanCommon.h"

// -------- 0x745 — cluster/body set (needs SDS) --------
inline std::vector<PidPlan> buildPlan_745() {
  std::vector<PidPlan> plan;

  // 21C1 — battery & outside temperature
  {
    PidPlan p; p.header = "745"; p.modePid = "21C1"; p.needsSession = true;
    p.metrics = {
      {"PR_НАПРЯЖЕНИЕ АКБ 12 В", "PR001","V",   9.0f, 15.8f,
        [](const std::vector<uint8_t>& b){ return 0.0625f * float(U8(b,'B')); }, "IBAT"},
      {"PR_НАРУЖНАЯ ТЕМПЕРАТУРА","PR002","°C", -40.0f, 215.0f,
        [](const std::vector<uint8_t>& b){ return float(U8(b,'A')) - 40.0f; }, "IOUT"},
    };
    plan.push_back(p);
  }

  // 21C7 — vehicle speed
  {
    PidPlan p; p.header = "745"; p.modePid = "21C7"; p.needsSession = true;
    p.metrics = {
      {"PR_СКОРОСТЬ АВТОМОБИЛЯ", "PR008","km/h", 0.0f, 255.0f,
        [](const std::vector<uint8_t>& b){ return 0.01f * float(U16(b,'D','C')); }, "ISPD"},
    };
    plan.push_back(p);
  }

  // 2111 — reverse gear status
  {
    PidPlan p; p.header = "745"; p.modePid = "2111"; p.needsSession = true;
    p.metrics = {
      {"ST_ВКЛЮЧЕН ЗАДНИЙ ХОД", "ET109","", 0.0f, 1.0f,
        [](const std::vector<uint8_t>& b){ return float(getBIT(b,'H',5)); }, "IREV"},
    };
    plan.push_back(p);
  }

  return plan;
}
