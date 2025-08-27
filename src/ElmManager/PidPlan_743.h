#pragma once
#include <vector>
#include <functional>
#include <stdint.h>
#include "DiagPlanCommon.h"



inline std::vector<PidPlan> buildCluster_Plan_743() {
  std::vector<PidPlan> plan;


  
  // --- 2110 ---
  {
    PidPlan p; p.header = "743"; p.modePid = "2110"; p.needsSession = true;
    p.metrics = {
      // PR_РЕЖИМ ДВИГАТЕЛЯ = (F*256+G)/8  [об/мин]
      {"PR_РЕЖИМ ДВИГАТЕЛЯ", "PR116", "об/мин", 0, 0,
        [](const std::vector<uint8_t>& b){ return (float)U16(b,'F','G') / 8.0f; }},

      // PR_СКОРОСТЬ АВТОМОБИЛЯ = (H*256+I)/100  [км/час]
      {"PR_СКОРОСТЬ АВТОМОБИЛЯ", "PR099", "км/час", 0, 0,
        [](const std::vector<uint8_t>& b){ return (float)U16(b,'H','I') / 100.0f; }},

      // PR_ТЕМПЕРАТУРА ВОДЫ = N - 40  [°C]
      {"PR_ТЕМПЕРАТУРА ВОДЫ", "PR027", "°C", 0, 0,
        [](const std::vector<uint8_t>& b){ return (float)U8(b,'N') - 40.0f; }},
    };
    plan.push_back(p);
  }

  // --- 2112 ---
  {
    PidPlan p; p.header = "743"; p.modePid = "2112"; p.needsSession = true;
    p.metrics = {
      // PR_УРОВЕНЬ ТОПЛИВА = (C*256+D)/1000  [л.]
      {"PR_УРОВЕНЬ ТОПЛИВА", "PR035", "л.", 0, 60,
        [](const std::vector<uint8_t>& b){ return (float)U16(b,'C','D') / 1000.0f; }},

      // PR_ПОДАЧА ТОПЛИВА = O  [l/h]
      {"PR_ПОДАЧА ТОПЛИВА", "PR112", "l/h", 0, 0,
        [](const std::vector<uint8_t>& b){ return (float)U8(b,'O'); }},

      // PR_НАПРЯЖЕНИЕ АКБ 12 В = (V*256+W)/1000  [V]
      {"PR_НАПРЯЖЕНИЕ АККУМУЛЯТОРНОЙ БАТАРЕИ 12 В", "PR110", "V", 7.5f, 16.0f,
        [](const std::vector<uint8_t>& b){ return (float)U16(b,'V','W') / 1000.0f; }},

      // PR_НАПРЯЖЕНИЕ РЕОСТАТА ОСВЕЩЕНИЯ = (X*256+Y)/1000  [V]
      {"PR_НАПРЯЖЕНИЕ РЕОСТАТА ОСВЕЩЕНИЯ", "PR111", "V", 0.0f, 8.5f,
        [](const std::vector<uint8_t>& b){ return (float)U16(b,'X','Y') / 1000.0f; }},

      // PR_НАРУЖНАЯ ТЕМПЕРАТУРА = Z - 40  [°C]
      {"PR_НАРУЖНАЯ ТЕМПЕРАТУРА", "PR109", "°C", 0, 0,
        [](const std::vector<uint8_t>& b){ return (float)U8(b,'Z') - 40.0f; }},
    };
    plan.push_back(p);
  }

  return plan;
}
