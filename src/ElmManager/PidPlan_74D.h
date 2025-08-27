#pragma once
#include <vector>
#include <functional>
#include <stdint.h>
#include "DiagPlanCommon.h"


inline std::vector<PidPlan> buildPlan_74D() {
  std::vector<PidPlan> plan;

  {
    PidPlan p; p.header = "74D"; p.modePid = "2102"; p.needsSession = true;
    p.metrics = {
      {"PR_ЗАРЯД ОТ ГЕНЕРАТОРА", "PR010", "%", 0, 0,
        [](const std::vector<uint8_t>& b){ return 0.390625f * (float)U8(b,'A'); }},
      {"ST_ВКЛЮЧЕН ЗАДНИЙ ХОД", "ET004", "", 0, 1,
        [](const std::vector<uint8_t>& b){ return (U8(b,'B') > 2) ? 1.0f : 0.0f; }},
      {"ST_ПОЛОЖЕНИЕ РЫЧАГА ПЕРЕКЛЮЧЕНИЯ ПЕРЕДАЧ", "ET005", "", 0, 0,
        [](const std::vector<uint8_t>& b){ return (float)U8(b,'B'); }},
    };
    plan.push_back(p);
  }
  return plan;
}
