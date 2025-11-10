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
      // "PR_НАПРЯЖЕНИЕ АККУМУЛЯТОРНОЙ БАТАРЕИ 12 В","PR001","21C1","B*0.0625","9","15.8","V","745"
      { "PR_НАПРЯЖЕНИЕ АККУМУЛЯТОРНОЙ БАТАРЕИ 12 В", "PR001", "V", 9.0f, 15.8f,
        [](const std::vector<uint8_t>& b){ return 0.0625f * float(U8(b,'B')); }},

      // "PR_НАРУЖНАЯ ТЕМПЕРАТУРА","PR002","21C1","A-40","-40","215","*C","745"
      { "PR_НАРУЖНАЯ ТЕМПЕРАТУРА", "PR002", "°C", -40.0f, 215.0f,
        [](const std::vector<uint8_t>& b){ return float(U8(b,'A')) - 40.0f; }},
    };
    plan.push_back(p);
  }

  // 21C7 — vehicle speed
  {
    PidPlan p; p.header = "745"; p.modePid = "21C7"; p.needsSession = true;
    p.metrics = {
      // "PR_СКОРОСТЬ АВТОМОБИЛЯ","PR008","21C7","0.01*(D*256+C)","0","255","км/час","745"
      { "PR_СКОРОСТЬ АВТОМОБИЛЯ", "PR008", "км/час", 0.0f, 255.0f,
        [](const std::vector<uint8_t>& b){ return 0.01f * float(U16(b,'D','C')); }},
    };
    plan.push_back(p);
  }

  // 2111 — reverse gear status
  {
    PidPlan p; p.header = "745"; p.modePid = "2111"; p.needsSession = true;
    p.metrics = {
      // "ST_ВКЛЮЧЕН ЗАДНИЙ ХОД","ET109","2111","{H:5}","0","0","","745"
      { "ST_ВКЛЮЧЕН ЗАДНИЙ ХОД", "ET109", "", 0.0f, 1.0f,
        [](const std::vector<uint8_t>& b){ return float(getBIT(b,'H',5)); }},
    };
    plan.push_back(p);
  }

  return plan;
}
