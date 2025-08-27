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

// ---------------------- minimal, useful plan (7E0) ----------------------
inline std::vector<diag::PidPlan> buildS3000_Plan_7E0() {
  std::vector<diag::PidPlan> plan;

  // 21A0 — core sensors (everything comes from one PID = fast + reliable)
  {
    diag::PidPlan p; p.header = "7E0"; p.modePid = "21A0"; p.needsSession = true;
    p.metrics = {
      // ECU / battery voltage
      {"PR_НАПРЯЖЕНИЕ ПИТАНИЯ КОМПЬЮТЕРА","PR071","V",    8, 16,
        [](const std::vector<uint8_t>& b){ return 0.03215f*U8(b,'A') + 8.0f; }},

      // Temps
      {"PR_ТЕМПЕРАТУРА ВОДЫ",  "PR064","°C", -40, 120,
        [](const std::vector<uint8_t>& b){ return 0.625f*U8(b,'B') - 40.0f; }},
      {"PR_ТЕМПЕРАТУРА ВОЗДУХА","PR058","°C", -40, 120,
        [](const std::vector<uint8_t>& b){ return 0.625f*U8(b,'C') - 40.0f; }},

      // Pressures
      {"PR_ДАВЛЕНИЕ ВПУСКА (MAP)","PR032","мбар", 114, 1048,
        [](const std::vector<uint8_t>& b){ return 0.0578f*float(U16(b,'D','E')) + 103.0f; }},
      {"PR_АТМОСФЕРНОЕ ДАВЛЕНИЕ","PR035","мбар", 700, 1047,
        [](const std::vector<uint8_t>& b){ return 1047.0f - (3.7f*U8(b,'F')); }},
      {"PR_ДАВЛЕНИЕ НАДДУВА ABS","PR041","мбар", 120, 2200,
        [](const std::vector<uint8_t>& b){ return 0.0578125f*float(U16(b,'N','O')) + 103.0f; }},

      // Derived: Boost (relative) = MAP - Atmos
      {"DRV_ОТН. ДАВЛЕНИЕ НАДДУВА","DRV_BOOST","мбар", -200, 2000,//move from plan to display screen
        [](const std::vector<uint8_t>& b){
          float map  = 0.0578f*float(U16(b,'D','E')) + 103.0f;
          float atm  = 1047.0f - (3.7f*U8(b,'F'));
          return map - atm;
        }},

      // Nice-to-have; same PID so "free"
      {"PR_СКОРОСТИ АВТОМОБИЛЯ","PR089","км/ч", 0, 255,
        [](const std::vector<uint8_t>& b){ return float(U8(b,'L')); }},
      {"PR_РЕЖИМ ДВИГАТЕЛЯ (RPM)","PR055","об/мин", 500, 6500,
        [](const std::vector<uint8_t>& b){ return float(U16(b,'G','H')); }},
    };
    plan.push_back(p);
  }

  // 21C1 — A/C pressure & request (climate essentials)
  {
    diag::PidPlan p; p.header = "7E0"; p.modePid = "21C1"; p.needsSession = true;
    p.metrics = {
      {"PR_ДАВЛЕНИЕ ХЛАДАГЕНТА","PR037","bar", 0, 25,
        [](const std::vector<uint8_t>& b){ return 0.2f*float(U8(b,'J')); }},
      {"ST_ЗАПРОС НА ВКЛЮЧЕНИЕ КОМПРЕССОРА","ET088","", 0, 1,
        [](const std::vector<uint8_t>& b){ return float(getBIT(b,'N',0)); }},
    };
    plan.push_back(p);
  }

  // 21A1 — A/C allowed + fan stages (simple booleans)
  {
    diag::PidPlan p; p.header = "7E0"; p.modePid = "21A1"; p.needsSession = true;
    p.metrics = {
      {"ST_РАЗРЕШЕНИЕ РАБОТЫ КОНДИЦИОНЕРА","ET004","", 0, 1,
        [](const std::vector<uint8_t>& b){ return float(getBIT(b,'H',2)); }},
      {"ST_УПРАВЛЕНИЕ ЭЛЕКТРОВЕНТИЛЯТОРА 1","ET014","", 0, 1,
        [](const std::vector<uint8_t>& b){ return float(getBIT(b,'F',5)); }},
      {"ST_УПРАВЛЕНИЕ ЭЛЕКТРОВЕНТИЛЯТОРА 2","ET015","", 0, 1,
        [](const std::vector<uint8_t>& b){ return float(getBIT(b,'F',4)); }},
    };
    plan.push_back(p);
  }

  // 21A7 — extra climate line (optional but handy)
  {
    diag::PidPlan p; p.header = "7E0"; p.modePid = "21A7"; p.needsSession = true;
    p.metrics = {
      {"ST_УПРАВЛЕНИЕ СИГНАЛОМ ДАВЛЕНИЯ КОНДИЦИОНЕРА","LC016","", 0, 1,
        [](const std::vector<uint8_t>& b){ return float(getBIT(b,'H',0)); }},
    };
    plan.push_back(p);
  }

  return plan;
}
