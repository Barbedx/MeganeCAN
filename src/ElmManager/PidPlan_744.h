#pragma once
#include <vector>
#include <functional>
#include <stdint.h>
#include "DiagPlanCommon.h"

// ---------------------- byte helpers (DATA-ONLY!) ----------------------
// Your decodeToUdsData() returns bytes AFTER 0x61 <pid>,
// so A == data[0], B == data[1], etc. No +2 offset anywhere.

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
// -------- 0x744 — HVAC (PIDs 21C1, 21C2) with SDS --------
inline std::vector<diag::PidPlan> buildPlan_744() {
  std::vector<diag::PidPlan> plan;

  // 21C1 — temps, battery, sun sensor
  {
    diag::PidPlan p; p.header = "744"; p.modePid = "21C1"; p.needsSession = true;
    p.metrics = {
      {"PR_НАРУЖНАЯ ТЕМПЕРАТУРА", "PR002", "°C", -40, 60,
        [](const std::vector<uint8_t>& b){ return (float)U8(b,'B') - 40.0f; }},
      {"PR_ВНУТРЕННЯЯ ТЕМПЕРАТУРА", "PR001", "°C", -40, 80,
        [](const std::vector<uint8_t>& b){ return 0.5f * (float)U8(b,'G') - 40.0f; }},
      {"PR_ТЕМПЕРАТУРА  ИСПАРИТЕЛЯ", "PR003", "°C", -40, 87,
        [](const std::vector<uint8_t>& b){ return 0.5f * (float)U8(b,'F') - 40.0f; }},
      {"PR_ТЕМПЕРАТУРА ВОДЫ", "PR004", "°C", -40, 214,
        [](const std::vector<uint8_t>& b){ return (float)U8(b,'D') - 40.0f; }},
      {"PR_НАПРЯЖЕНИЕ АККУМУЛЯТОРНОЙ БАТАРЕИ 12 В", "PR092", "V", 10.5f, 16.0f,
        [](const std::vector<uint8_t>& b){ return 0.078f * (float)U8(b,'J'); }},
      {"PR_ИНТЕНСИВНОСТЬ СОЛНЕЧНОГО СВЕТА", "PR006", "Вт/м2", 0, 400,
        [](const std::vector<uint8_t>& b){ return 3.0f * (float)U8(b,'H'); }},
    };
    plan.push_back(p);
  }

  // 21C2 — blower PWM, setpoints, flaps, vehicle speed proxy, heaters
  {
    diag::PidPlan p; p.header = "744"; p.modePid = "21C2"; p.needsSession = true;
    p.metrics = {
      {"PR_ЗАПРОС НАГРЕВАТЕЛЬНЫХ РЕЗИСТОРОВ", "PR137", "W", 0, 3000,
        [](const std::vector<uint8_t>& b){ return 100.0f * (float)((U8(b,'C') >> 3) & 0x1F); }},
      {"PR_УСТАНОВКА PWM ЭЛ. ВЕНТИЛЯТ. САЛОНА", "PR019", "%", 0, 100,
        [](const std::vector<uint8_t>& b){ return 0.5f * (float)U8(b,'M'); }},
      {"PR_УСТАВКА ТЕМПЕРАТУРЫ ПОДАВАЕМОГО ВОЗДУХА", "PR121", "°C", 0, 80,
        [](const std::vector<uint8_t>& b){ return (float)U8(b,'N') - 40.0f; }},
      {"PR_ПОЛОЖЕНИЕ СМЕСИТЕЛЬНОЙ ЗАСЛОНКИ", "PR012", "%", 0, 100,
        [](const std::vector<uint8_t>& b){ return (float)U8(b,'P'); }},
      {"PR_ПОЛОЖЕНИЕ РАСПРЕДЕЛИТ. ЗАСЛОНКИ", "PR011", "%", 0, 100,
        [](const std::vector<uint8_t>& b){ return (float)U8(b,'Q'); }},
      {"PR_НОЧНОЙ УРОВЕНЬ ОСВЕЩЕНИЯ", "PR122", "%", 0, 100,
        [](const std::vector<uint8_t>& b){ return 0.4f * (float)U8(b,'F'); }},
      {"PR_СКОРОСТЬ АВТОМОБИЛЯ", "PR095", "км/час", 0, 250,
        [](const std::vector<uint8_t>& b){ return 2.56f * (float)U8(b,'H'); }},
    };
    plan.push_back(p);
  }

  return plan;
}
