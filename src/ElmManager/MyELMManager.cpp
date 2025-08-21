#include "MyELMManager.h"

// ===== Queries per group =====

 MyELMManager::MyELMManager(IDisplay &display) : display(display)
 {
 } 

 const QueryData queries_74D[2] = {
    {"2102", "PR_ЗАРЯД ОТ ГЕНЕРАТОРА",
     [](const uint8_t *p) -> float { return 0.390625f * MyELMManager::getByte(p,'A'); }},
};
  static const QueryData queries_743[2] = {
     {"2112", "PR_НАПРЯЖЕНИЕ АККУМУЛЯТОРНОЙ БАТАРЕИ 12 В", [](const uint8_t *p)-> float { return (MyELMManager::getByte(p,'V')*256 + MyELMManager::getByte(p,'W'))/1000.0f; }},
     {"2112", "PR_НАРУЖНАЯ ТЕМПЕРАТУРА", [](const uint8_t *p) -> float { return MyELMManager::getByte(p,'Z') - 40; }},
 };

   static const QueryData queries_744[2] = {
      {"21C1", "PR_НАПРЯЖЕНИЕ АККУМУЛЯТОРНОЙ БАТАРЕИ 12 В", [](const uint8_t *p)-> float { return 0.078f * MyELMManager::getByte(p,'J'); }},
      {"21C1", "PR_НАРУЖНАЯ ТЕМПЕРАТУРА", [](const uint8_t *p)-> float { return MyELMManager::getByte(p,'B') - 40; }},
  };
 
   static const QueryData queries_745[3] = {
      {"21C1", "PR_НАПРЯЖЕНИЕ АККУМУЛЯТОРНОЙ БАТАРЕИ 12 В", [](const uint8_t *p)-> float { return MyELMManager::getByte(p,'B') * 0.0625f; }},
      {"21C1", "PR_НАРУЖНАЯ ТЕМПЕРАТУРА", [](const uint8_t *p)-> float { return MyELMManager::getByte(p,'A') - 40; }},
      {"2113", "ST_ДАТЧИК НАРУЖНОЙ ТЕМПЕРАТУРЫ", [](const uint8_t *p)-> float { return MyELMManager::getByte(p,'U'); }},
  };

    const std::array<QueryGroup, 4> MyELMManager::queryList = {
    {"74D", queries_74D, sizeof(queries_74D)/sizeof(QueryData)},
    // {"743", queries_743, sizeof(queries_743)/sizeof(QueryData)},
    // {"744", queries_744, sizeof(queries_744)/sizeof(QueryData)},
    // {"745", queries_745, sizeof(queries_745)/sizeof(QueryData)},
};