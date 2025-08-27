#pragma once
#include <vector>
#include <functional>
#include <stdint.h>
#include "DiagPlanCommon.h"

// ---------------------- byte helpers (DATA-ONLY!) ----------------------
// Your decodeToUdsData() returns bytes AFTER 0x61 <pid>,
// so A == data[0], B == data[1], etc. No +2 offset anywhere.
// -------- 0x74D — Gear/Reverse/Alternator (PID 2102) with SDS --------
static inline uint8_t _safe_at(const std::vector<uint8_t>& b, size_t idx) {
  return (idx < b.size()) ? b[idx] : 0;
}

// single-letter symbol A..Z  (A=0, B=1, ...)
static inline uint8_t U8(const std::vector<uint8_t>& b, char L) {
  if (L < 'A') return 0;
  size_t pos = (size_t)(L - 'A');
  return _safe_at(b, pos);
}

static inline uint16_t U16(const std::vector<uint8_t>& b, char hi, char lo) {
  return (uint16_t(U8(b, hi)) << 8) | U8(b, lo);
}

static inline uint8_t getBIT(const std::vector<uint8_t>& b, char L, uint8_t n) {
  return (U8(b, L) >> n) & 0x01;
}
inline std::vector<diag::PidPlan> buildPlan_74D() {
  std::vector<diag::PidPlan> plan;

  {
    diag::PidPlan p; p.header = "74D"; p.modePid = "2102"; p.needsSession = true;
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
