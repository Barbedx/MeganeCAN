#pragma once
#include <vector>
#include <string>
#include <functional>
#include <stdint.h>
#include "DiagPlanCommon.h"
// ---------------------- Helpers to read bytes ----------------------
// Positive response layout for Renault UDS 0x21 PID reads is:
//   byte0 = 0x61 (positive resp to 0x21)
//   byte1 = PID (e.g., 0xA0, 0xA1, ...)
//   byte2.. = data bytes we reference as A, B, C ... Z, AA, AB, ...
// These helpers protect against short frames.

static inline uint8_t _safe_at(const std::vector<uint8_t>& b, size_t idx) {
  return (idx < b.size()) ? b[idx] : 0;
}

// single-letter symbol (A..Z)
static inline uint8_t U8(const std::vector<uint8_t>& b, char L) {
  if (L < 'A') return 0;
  size_t pos = (size_t)(L - 'A');
  size_t idx = 2 + pos; // skip 61 <pid>
  return _safe_at(b, idx);
}

// two-letter symbol AA..AZ (we only need up to AH in this plan)
static inline uint8_t U8x(const std::vector<uint8_t>& b, const char* sym) {
  // supports 1 or 2 letters: "A".."Z" or "AA".."AZ"
  if (!sym || !sym[0]) return 0;
  size_t pos = 0;
  if (!sym[1]) {
    pos = (size_t)(sym[0] - 'A');
  } else {
    // Only AA..AZ are used here -> map AA->26, AB->27, ...
    pos = 26 + (size_t)(sym[1] - 'A');
  }
  size_t idx = 2 + pos;
  return _safe_at(b, idx);
}

static inline uint16_t U16(const std::vector<uint8_t>& b, char hi, char lo) {
  return ((uint16_t)U8(b, hi) << 8) | U8(b, lo);
}

static inline uint16_t U16x(const std::vector<uint8_t>& b, const char* hi, const char* lo) {
  return ((uint16_t)U8x(b, hi) << 8) | U8x(b, lo);
}

static inline int16_t S16(const std::vector<uint8_t>& b, char hi, char lo) {
  // sign extend the high byte, then add low
  int16_t h = (int16_t)((int8_t)U8(b, hi));
  return (int16_t)((h << 8) | U8(b, lo));
}

static inline int16_t S16x(const std::vector<uint8_t>& b, const char* hi, const char* lo) {
  int16_t h = (int16_t)((int8_t)U8x(b, hi));
  return (int16_t)((h << 8) | U8x(b, lo));
}

static inline uint8_t getBIT(const std::vector<uint8_t>& b, char L, uint8_t n) {
  return (U8(b, L) >> n) & 0x01;
}

static inline uint8_t BITS(const std::vector<uint8_t>& b, char L, uint8_t shift, uint8_t mask) {
  return (U8(b, L) >> shift) & mask;
}

// ---------------------- Build plan for ECU 7E0 (S3000) ----------------------
inline std::vector<diag::PidPlan> buildS3000_Plan_7E0() { 
  // 21A0 ---------------------------------------------------------------------
  std::vector<diag::PidPlan> plan;
  
  {
    diag::PidPlan p; p.header = "7E0"; p.modePid = "21A0"; p.needsSession = true;
    p.metrics = {
      {"PR_НАПРЯЖЕНИЕ ПИТАНИЯ КОМПЬЮТЕРА","PR071","V", 8, 16,
        [](const std::vector<uint8_t>& b){ return 0.03215f*U8(b,'A') + 8.0f; }},
      {"PR_АТМОСФЕРНОЕ ДАВЛЕНИЕ","PR035","мбар", 700, 1047,
        [](const std::vector<uint8_t>& b){ return 1047.0f - (3.7f*U8(b,'F')); }},
      {"PR_ДАВЛЕНИЕ ВПУСКА","PR032","мбар", 114, 1048,
        [](const std::vector<uint8_t>& b){ return 0.0578f*(float)U16(b,'D','E') + 103.0f; }},
      {"PR_ТЕМПЕРАТУРА ВОДЫ","PR064","°C", -40, 120,
        [](const std::vector<uint8_t>& b){ return 0.625f*U8(b,'B') - 40.0f; }},
      {"PR_ТЕМПЕРАТУРА ВОЗДУХА","PR058","°C", -40, 120,
        [](const std::vector<uint8_t>& b){ return 0.625f*U8(b,'C') - 40.0f; }},
      {"PR_СКОРОСТИ АВТОМОБИЛЯ","PR089","км/час", 0, 255,
        [](const std::vector<uint8_t>& b){ return (float)U8(b,'L'); }},
      {"PR_РЕЖИМ ДВИГАТЕЛЯ","PR055","об/мин", 500, 6500,
        [](const std::vector<uint8_t>& b){ return (float)U16(b,'G','H'); }},
      {"PR_КОРРЕКЦИЯ РЕЖИМА ХОЛОСТОГО ХОДА","PR014","об/мин", 0, 225,
        [](const std::vector<uint8_t>& b){ return 16.0f*U8(b,'K'); }},
      {"PR_УСТАНОВКА РЕГУЛИРОВКИ ХОЛ. ХОДА","PR010","об/мин", 752, 1216,
        [](const std::vector<uint8_t>& b){ return (float)U16(b,'I','J'); }},
      {"PR_ОПЕРЕЖЕНИЕ","PR001","*V", 0, 0,
        [](const std::vector<uint8_t>& b){ return 0.375f*U8(b,'P') - 23.625f; }},
      {"PR_РЕГУЛИРОВКА ДЛЯ УСТР. ДЕТОНАЦИЙ","PR095","*V", 0, 8,
        [](const std::vector<uint8_t>& b){ return 0.375f*U8(b,'Q'); }},
      {"PR_ДАВЛЕНИЕ НАДДУВА","PR041","мбар", 120, 2200,
        [](const std::vector<uint8_t>& b){ return 0.0578125f*(float)U16(b,'N','O') + 103.0f; }},
      {"PR_СЦО ЭЛЕКТРОМАГ. КЛАПАНА РЕГ. ДАВ. НАДДУВ","PR846","%", 0, 100,
        [](const std::vector<uint8_t>& b){ return 100.0f * ((float)U8(b,'R')/256.0f); }},
    };
    plan.push_back(p);
  }

  // 21A1 ---------------------------------------------------------------------
  {
    diag::PidPlan p; p.header = "7E0"; p.modePid = "21A1"; p.needsSession = true;
    p.metrics = {
      {"ST_+ ПОСЛЕ ВКЛ. ЗАЖИГ. НА КОМПЬЮТЕРЕ","ET001","", 0, 1,
        [](const std::vector<uint8_t>& b){ return (float)getBIT(b,'A',7); }},
      {"ST_ЦЕПЬ УПРАВЛЕНИЯ БЕНЗОНАСОСА","ET047","", 0, 1,
        [](const std::vector<uint8_t>& b){ return (float)getBIT(b,'F',6); }},
      {"ST_КОМАНДА ПРИВОДА","ET048","", 0, 1,
        [](const std::vector<uint8_t>& b){ return (float)getBIT(b,'F',7); }},
      {"ST_КОМАНДА ВОДЯНОГО НАСОСА","ET543","", 0, 1,
        [](const std::vector<uint8_t>& b){ return (float)BITS(b,'G',2,0x01); }},
      {"ST_КОМАНДА РЕЛЕ ВОЗДУШНОГО НАСОСА","ET049","", 0, 1,
        [](const std::vector<uint8_t>& b){ return (float)BITS(b,'G',1,0x01); }},
      {"ST_ПЕДАЛЬ ПОЛН. ОТПУЩ. И ДРОСС. КЛАПАН ЗАКР.","ET075","", 0, 1,
        [](const std::vector<uint8_t>& b){ return (float)getBIT(b,'A',4); }},
      {"ST_НАГРЕВ ВЕРХНЕГО ДАТЧИКА КИСЛОРОДА","ET052","", 0, 1,
        [](const std::vector<uint8_t>& b){ return (float)BITS(b,'G',3,0x01); }},
      {"ST_ПОДОГРЕВ НИЖНЕГО КИСЛОРОДНОГО ДАТЧИКА","ET053","", 0, 1,
        [](const std::vector<uint8_t>& b){ return (float)BITS(b,'G',4,0x01); }},
      {"ST_РАЗРЕШЕНИЕ РАБОТЫ КОНДИЦИОНЕРА","ET004","", 0, 1,
        [](const std::vector<uint8_t>& b){ return (float)BITS(b,'H',2,0x01); }},
      {"ST_УПРАВЛЕНИЕ ЭЛЕКТРОВЕНТИЛЯТОРА 1","ET014","", 0, 1,
        [](const std::vector<uint8_t>& b){ return (float)getBIT(b,'F',5); }},
      {"ST_УПРАВЛЕНИЕ ЭЛЕКТРОВЕНТИЛЯТОРА 2","ET015","", 0, 1,
        [](const std::vector<uint8_t>& b){ return (float)getBIT(b,'F',4); }},
      {"ST_ПОЛОЖЕНИЕ ПАРК / НЕЙТРАЛЬ.","ET063","", 0, 1,
        [](const std::vector<uint8_t>& b){ return (float)getBIT(b,'B',3); }},
      {"ST_СЖ. ГАЗ В СОСТ. НЕИСПРАВНОСТИ","ET066","", 0, 1,
        [](const std::vector<uint8_t>& b){ return (float)getBIT(b,'L',2); }},
      {"ST_ГОТОВНОСТЬ СЖ. ГАЗА","ET067","", 0, 1,
        [](const std::vector<uint8_t>& b){ return (float)getBIT(b,'L',1); }},
      {"ST_БАК СЖ. ГАЗА ПУСТОЙ","ET068","", 0, 1,
        [](const std::vector<uint8_t>& b){ return (float)getBIT(b,'L',3); }},
      {"ST_ФУНКЦИОНИРОВАНИЕ В РЕЖИМЕ СЖ. ГАЗА","ET069","", 0, 1,
        [](const std::vector<uint8_t>& b){ return (float)getBIT(b,'L',7); }},
      {"ST_ПЕРЕХОД ИЗ РЕЖИМА БЕНЗ. В РЕЖ. СЖ. ГАЗА","ET071","", 0, 1,
        [](const std::vector<uint8_t>& b){ return (float)getBIT(b,'L',4); }},
      {"ST_ПЕРЕХОД ИЗ РЕЖ. СЖ. ГАЗА В РЕЖИМ БЕНЗИНА","ET072","", 0, 1,
        [](const std::vector<uint8_t>& b){ return (float)getBIT(b,'L',5); }},
      {"ST_УСЛОВИЯ ПЕРЕХОДА В РЕЖИМ СЖ. ГАЗА","ET073","", 0, 3,
        [](const std::vector<uint8_t>& b){ return (float)BITS(b,'L',0,0x01); }},
      {"ST_ИНДИКАТОР OBD ВКЛЮЧЕН АВТ. ТРАНСМИССИЕЙ","ET074","", 0, 1,
        [](const std::vector<uint8_t>& b){ return (float)getBIT(b,'B',1); }},
    };
    plan.push_back(p);
  }

  // 21A2 ---------------------------------------------------------------------
  {
    diag::PidPlan p; p.header = "7E0"; p.modePid = "21A2"; p.needsSession = true;
    p.metrics = {
      {"PR_ПОЛОЖЕНИЕ ПЕДАЛИ АКСЕЛЕРАТОРА","PR030","%", 0, 100,
        [](const std::vector<uint8_t>& b){ return 100.0f*((float)U8(b,'J')/256.0f); }},
      {"PR_СЧИТАННОЕ ЗНАЧ. ОТПУЩ. ПЕДАЛИ","PR112","%", 0, 15.65f,
        [](const std::vector<uint8_t>& b){ return 100.0f*((float)U8(b,'M')/256.0f); }},
      {"PR_ВВЕД.ЗНАЧ. КРАЙН.ВЕРХН.П. ДР.КЛ. С ПРИВ.","PR096","%", 87.9f, 90.0f,
        [](const std::vector<uint8_t>& b){ return 0.4f*U8(b,'E'); }},
      {"PR_ВВЕД.ЗНАЧ. КРАЙН.НИЖН.П. ДР.КЛ. С ПРИВ.","PR097","%", 5.96f, 13.96f,
        [](const std::vector<uint8_t>& b){ return 0.4f*U8(b,'D'); }},
      {"PR_ПОЛОЖЕНИЕ ПЕДАЛИ (ПОЛОСА 1)","PR028","%", 9.96f, 95.0f,
        [](const std::vector<uint8_t>& b){ return 100.0f*((float)U8(b,'K')/256.0f); }},
      {"PR_ПОЛОЖЕНИЕ ПЕДАЛИ (ПОЛОСА 2)","PR029","%", 4.98f, 95.0f,
        [](const std::vector<uint8_t>& b){ return 100.0f*((float)U8(b,'L')/256.0f); }},
      {"PR_ОЦЕНОЧН. РАСХОД ВОЗДУХА","PR018","kg/h", 0, 0,
        [](const std::vector<uint8_t>& b){ return (float)U8(b,'P'); }},
      {"PR_СКОРР. ПОЛОЖ. ДРОСС. КЛАПАНА С СЕРВОПРИВ.","PR111","%", 0, 100,
        [](const std::vector<uint8_t>& b){ return 100.0f * ((float)U16x(b,"AE","AF")/2048.0f); }},
      {"PR_НИЖН. ОГРАН. ДР. КЛАП. ПОСЛЕ ВВЕД. СМЕЩ.","PR113","%", 5.96f, 13.96f,
        [](const std::vector<uint8_t>& b){ return 100.0f * ((float)U16x(b,"AA","AB")/1024.0f); }},
      {"PR_НИЖНИЙ ОГРАНИЧИТЕЛЬ ДРОССЕЛЬНОГО КЛАПАНА","PR114","%", 5.96f, 13.96f,
        [](const std::vector<uint8_t>& b){ return 100.0f * ((float)U16x(b,"AC","AD")/1024.0f); }},
      {"PR_УСТАВКА СКОРР. ПОЛОЖ. ДР. КЛ. С СЕРВОПР.","PR116","%", 0, 100,
        [](const std::vector<uint8_t>& b){ return 100.0f * ((float)U16x(b,"AG","AH")/2048.0f); }},
      {"PR_ВЕРХНИЙ ОГРАНИЧИТЕЛЬ ДРОССЕЛЬНОГО КЛАПАНА","PR115","%", 87.95f, 88.0f,
        [](const std::vector<uint8_t>& b){ return 100.0f * ((float)U16(b,'Y','Z')/1024.0f); }},
      {"PR_ИЗМЕРЕННОЕ ПОЛОЖ. ДР. КЛАПАНА. ДОРОЖКА 1","PR118","%", 4, 99,
        [](const std::vector<uint8_t>& b){ return 0.4f*U8(b,'B'); }},
      {"PR_ИЗМЕРЕННОЕ ПОЛОЖ. ДР. КЛАПАНА. ДОРОЖКА 2","PR119","%", 4, 100,
        [](const std::vector<uint8_t>& b){ return 0.4f*U8(b,'C'); }},
    };
    plan.push_back(p);
  }

  // 21A3 ---------------------------------------------------------------------
  {
    diag::PidPlan p; p.header = "7E0"; p.modePid = "21A3"; p.needsSession = true;
    p.metrics = {
      {"PR_ЗНАЧЕНИЕ ВВОДА ПАРАМЕТРОВ РЕГУЛ. Х. ХОДА","PR090","%", -12, 12,
        [](const std::vector<uint8_t>& b){ return ((float)S16(b,'H','I'))/100.0f; }},
      {"PR_ТЕОРЕТИЧ. ЦИКЛ. ОТН. ОТКР. РЕГУЛИР. Х.Х.","PR091","%", 5, 50,
        [](const std::vector<uint8_t>& b){ return 0.0015259f*(float)U16(b,'L','M'); }},
      {"PR_КОРРЕКЦИЯ СОСТАВА СМЕСИ","PR138","%", 0, 100,
        [](const std::vector<uint8_t>& b){ return 0.001526f*(float)U16(b,'N','O'); }},
      {"PR_СМЕЩ. ПАРАМ. САМОАД. КОРР. СОСТ. СМЕСИ","PR144","", 0, 0,
        [](const std::vector<uint8_t>& b){ return (float)U8(b,'J'); }},
      {"PR_УСИЛЕНИЕ ПАР. САМОАД. КОРР. СОСТ. СМЕСИ","PR143","", 0, 0,
        [](const std::vector<uint8_t>& b){ return (float)U8(b,'K'); }},
      {"PR_ИЗМЕРЕННОЕ ПОЛОЖЕНИЕ РЕГУЛЯТОРА AAC","PR093","*V", 0, 45,
        [](const std::vector<uint8_t>& b){ return 0.375f*U8(b,'S'); }},
      {"PR_СЦО УПР. СМЕЩ. ФАЗ ГАЗОРАС. ВПУС. КЛ.","PR876","%", 0, 100,
        [](const std::vector<uint8_t>& b){ return 100.0f*((float)U16(b,'Q','R')/1024.0f); }},
      {"PR_ПРОДОЛЖИТЕЛЬНОСТЬ ВПРЫСКА","PR101","ms", 0, 0,
        [](const std::vector<uint8_t>& b){ return 0.0032f*(float)U16(b,'A','B'); }},
      {"PR_ОТН. Ц. ОТКР. ЭМ. КЛ. ПР. Ф. ПАРОВ ТОПЛ.","PR102","%", 0, 100,
        [](const std::vector<uint8_t>& b){ return 0.0039f*(float)U8(b,'E'); }},
      {"PR_ЭТАЛ. ПОЛОЖ. ФАЗОРЕГ. РАСП. ВАЛА ВП.КЛАП.","PR745","*V", 0, 45,
        [](const std::vector<uint8_t>& b){ return 0.375f*U8(b,'P'); }},
    };
    plan.push_back(p);
  }

  // 21A4 ---------------------------------------------------------------------
  {
    diag::PidPlan p; p.header = "7E0"; p.modePid = "21A4"; p.needsSession = true;
    p.metrics = {
      {"PR_НАПРЯЖЕНИЕ ВЕРХН. ДАТЧ. КИСЛОРОДА","PR098","mV", 19, 1395,
        [](const std::vector<uint8_t>& b){ return 9.76f*U8(b,'A'); }},
      {"PR_НАПРЯЖ. НИЖН. ДАТЧИКА КИСЛОРОДА","PR099","mV", 19, 1395,
        [](const std::vector<uint8_t>& b){ return 9.76f*U8(b,'H'); }},
      {"PR_СРЕДНИЙ ПЕРИОД ВЕРХНЕГО ДАТЧИКА","PR121","ms", 0, 0,
        [](const std::vector<uint8_t>& b){ return 16.0f*U8(b,'B'); }},
      {"ST_ФАЗОРЕГУЛЯТОР РАСПР. ВАЛА ВПУСК. КЛАПАНОВ","ET084","", 0, 1,
        [](const std::vector<uint8_t>& b){ return (float)U8(b,'Q'); }},
    };
    plan.push_back(p);
  }

  // 21A5 ---------------------------------------------------------------------
  {
    diag::PidPlan p; p.header = "7E0"; p.modePid = "21A5"; p.needsSession = true;
    p.metrics = {
      {"PR_КРУТЯЩИЙ МОМЕНТ ДВИГАТЕЛЯ","PR015","Nm", -50, 200,
        [](const std::vector<uint8_t>& b){ return 2.0f*U8(b,'A') - 100.0f; }},
      {"PR_ОЦЕН. КРУТЯЩ. МОМЕНТ ДВИГ. ПО ЗАД. ВОДИТ.","PR123","Nm", 0, 200,
        [](const std::vector<uint8_t>& b){ return 2.0f*U8(b,'B') - 100.0f; }},
      {"PR_КРУТ. МОМЕНТ. ПРИНЯТЫЙ ГИДРОТРАНСФ. АКП","PR122","Nm", 0, 30,
        [](const std::vector<uint8_t>& b){ return (2.0f*U8(b,'D'))/256.0f; }},
      {"PR_КРУТ. МОМ. СОПР. ДВИГ.. ПЕРЕДАННЫЙ НА CAN","PR124","Nm", 0, 100,
        [](const std::vector<uint8_t>& b){ return 2.0f*U8(b,'E') - 100.0f; }},
    };
    plan.push_back(p);
  }

  // 21A6 ---------------------------------------------------------------------
  {
    diag::PidPlan p; p.header = "7E0"; p.modePid = "21A6"; p.needsSession = true;
    p.metrics = {
      {"PR_СЧЕТЧИК ПРОБЕГА С ВКЛ. ИНДИК. НЕИСПР. OBD","PR105","km", 0, 0,
        [](const std::vector<uint8_t>& b){ return (float)U16(b,'O','P'); }},
      {"ST_ХОД ТЕСТА","ET099","", 0, 1,
        [](const std::vector<uint8_t>& b){ return (float)getBIT(b,'D',5); }},
      {"ST_ХОД ВЫПОЛНЕНИЯ ИСПЫТАНИЯ","ET100","", 0, 1,
        [](const std::vector<uint8_t>& b){ return (float)getBIT(b,'D',7); }},
      {"ST_РЕЗУЛЬТАТ ТЕСТА","ET092","", 0, 255,
        [](const std::vector<uint8_t>& b){ return (float)U8(b,'X'); }},
      {"ST_РЕЗУЛЬТАТ ИСПЫТАНИЯ","ET101","", 0, 255,
        [](const std::vector<uint8_t>& b){ return (float)U8(b,'Z'); }},
      {"ST_ETAT PRIVE 2","ET091","", 0, 1,
        [](const std::vector<uint8_t>& b){ return (float)getBIT(b,'A',7); }},
      {"ST_ETAT PRIVE 3","ET096","", 0, 1,
        [](const std::vector<uint8_t>& b){ return (float)getBIT(b,'A',1); }},
      {"ST_ETAT PRIVE 4","ET097","", 0, 1,
        [](const std::vector<uint8_t>& b){ return (float)getBIT(b,'A',5); }},
      {"ST_ETAT PRIVE 5","ET098","", 0, 1,
        [](const std::vector<uint8_t>& b){ return (float)getBIT(b,'E',7); }},
      {"ST_ETAT PRIVE 6","ET103","", 0, 1,
        [](const std::vector<uint8_t>& b){ return (float)getBIT(b,'C',1); }},
      {"ST_ПРОПУСК ЗАЖИГАНИЯ В ЦИЛИНДРЕ 1","ET057","", 0, 1,
        [](const std::vector<uint8_t>& b){ return (float)getBIT(b,'B',7); }},
      {"ST_ПРОПУСК ЗАЖИГАНИЯ В ЦИЛИНДРЕ 2","ET058","", 0, 1,
        [](const std::vector<uint8_t>& b){ return (float)getBIT(b,'B',6); }},
      {"ST_ПРОПУСК ЗАЖИГАНИЯ В ЦИЛИНДРЕ 3","ET059","", 0, 1,
        [](const std::vector<uint8_t>& b){ return (float)getBIT(b,'B',5); }},
      {"ST_ПРОПУСК ЗАЖИГАНИЯ В ЦИЛИНДРЕ 4","ET060","", 0, 1,
        [](const std::vector<uint8_t>& b){ return (float)getBIT(b,'B',4); }},
      {"ST_ПОСЛЕДОВАТ. БОРТ. ДИАГН. КАЛТАЛИЗАТОРА","LC021","", 0, 1,
        [](const std::vector<uint8_t>& b){ return (float)getBIT(b,'E',5); }},
      {"ST_ПОСЛЕДОВАТ. БОРТ. ДИАГН. ПРОПУСКА СГОРАН.","LC022","", 0, 1,
        [](const std::vector<uint8_t>& b){ return (float)getBIT(b,'E',1); }},
      {"ST_ПОСЛЕДОВАТ. БОРТ. ДИАГНОСТИКИ ДАТЧИКОВ","LC023","", 0, 1,
        [](const std::vector<uint8_t>& b){ return (float)getBIT(b,'E',7); }},
    };
    plan.push_back(p);
  }

  // 21A7 ---------------------------------------------------------------------
  {
    diag::PidPlan p; p.header = "7E0"; p.modePid = "21A7"; p.needsSession = true;
    p.metrics = {
      {"ST_ВВОД ПАРАМЕТРОВ КРАЙНИХ ПОЛОЖ. ДРОСС. КЛ.","ET051","", 0, 1,
        [](const std::vector<uint8_t>& b){ return (float)getBIT(b,'J',2); }},
      {"ST_РЕГУЛИРОВКА ХОЛОСТОГО ХОДА","ET054","", 0, 1,
        [](const std::vector<uint8_t>& b){ return (float)getBIT(b,'J',6); }},
      {"ST_КОНТУР СОСТАВА СМЕСИ ВЕРХНЕГО ДАТЧИКА","ET055","", 0, 1,
        [](const std::vector<uint8_t>& b){ return (float)getBIT(b,'J',7); }},
      {"ST_ДВОЙНОЙ КОНТУР СОСТАВА СМЕСИ","ET056","", 0, 1,
        [](const std::vector<uint8_t>& b){ return (float)getBIT(b,'J',5); }},
      {"ST_КОНДИЦИОНЕР","LC009","", 0, 1,
        [](const std::vector<uint8_t>& b){ return (float)getBIT(b,'A',4); }},
      {"ST_ТИП СОЕДИНЕНИЯ СКОРОСТИ АВТОМОБИЛЯ","LC001","", 0, 15,
        [](const std::vector<uint8_t>& b){ return (float)BITS(b,'G',4,0x0F); }},
      {"ST_КОНТРОЛЬ ТРАЕКТОРИИ","LC010","", 0, 1,
        [](const std::vector<uint8_t>& b){ return (float)getBIT(b,'A',2); }},
      {"ST_ЗАМЫК. КОНТАКТ ТОРМОЗА ПРОВОДН. ТИПА","LC018","", 0, 1,
        [](const std::vector<uint8_t>& b){ return (float)getBIT(b,'B',2); }},
      {"ST_ВЕРХНИЙ ДАТЧИК КИСЛОРОДА","LC003","", 0, 1,
        [](const std::vector<uint8_t>& b){ return (float)getBIT(b,'D',2); }},
      {"ST_НИЖНИЙ ДАТЧИК КИСЛОРОДА","LC004","", 0, 1,
        [](const std::vector<uint8_t>& b){ return (float)getBIT(b,'D',1); }},
      {"ST_УПРАВЛЕНИЕ СИГНАЛОМ ДАВЛЕНИЯ КОНДИЦИОНЕРА","LC016","", 0, 1,
        [](const std::vector<uint8_t>& b){ return (float)getBIT(b,'H',0); }},
      {"ST_ТИП КОРОБКИ ПЕРЕДАЧ","LC005","", 0, 7,
        [](const std::vector<uint8_t>& b){ return (float)BITS(b,'D',3,0x07); }},
      {"ST_РАСПОЗНАВАНИЕ ЦИЛИНДРА 1","LC007","", 0, 1,
        [](const std::vector<uint8_t>& b){ return (float)getBIT(b,'H',4); }},
      {"ST_НАГРЕВАТЕЛЬНЫЙ РЕЗИСТОР","LC025","", 0, 1,
        [](const std::vector<uint8_t>& b){ return (float)getBIT(b,'A',0); }},
      {"ST_СТРАТЕГИЯ РАБОТЫ ВОЗДУШНОГО НАСОСА","LC014","", 0, 1,
        [](const std::vector<uint8_t>& b){ return (float)getBIT(b,'B',0); }},
      {"ST_СТРАТЕГИЯ РАБОТЫ ВОДЯНОГО НАСОСА","LC015","", 0, 1,
        [](const std::vector<uint8_t>& b){ return (float)getBIT(b,'B',1); }},
      {"ST_УПРАВЛЕНИЕ ИНДИКАТОРОМ БОРТ. ДИАГН. OBD","LC024","", 0, 1,
        [](const std::vector<uint8_t>& b){ return (float)getBIT(b,'D',6); }},
      {"ST_ВОЗДУШНЫЙ НАСОС","LC164","", 0, 1,
        [](const std::vector<uint8_t>& b){ return (float)getBIT(b,'E',7); }},
      {"ST_ЭЛЕКТРИЧЕСКИЙ ВОДЯНОЙ НАСОС","LC170","", 0, 1,
        [](const std::vector<uint8_t>& b){ return (float)getBIT(b,'E',6); }},
    };
    plan.push_back(p);
  }

  // 21A9 ---------------------------------------------------------------------
  {
    diag::PidPlan p; p.header = "7E0"; p.modePid = "21A9"; p.needsSession = true;
    p.metrics = {
      {"PR_ИНТЕГРАЛЬН. КОРРЕКЦИЯ РЕГУЛИРОВКИ ХОЛОСТОГО Х.","PR141","%", 4.7f, 32.0f,
        [](const std::vector<uint8_t>& b){ return (256.0f * (float)U8(b,'I'))/100.0f; }},
      {"PR_ПОТЕРЯ КРУТЯЩЕГО МОМЕНТА","PR100","Nm", -3277, 3277,
        [](const std::vector<uint8_t>& b){ return 0.1f * (float)S16(b,'G','H'); }},
      {"PR_МГНОВЕННЫЙ РАСХОД ТОПЛИВА ЗА ЕД ВРЕМЕНИ","PR103","l/h", 0, 50,
        [](const std::vector<uint8_t>& b){ return ((float)U16(b,'A','B'))/100.0f; }},
      {"PR_МОЩН.. ПОТРЕБЛ. КОМПРЕССОРОМ КОНД.","PR125","W", 300, 3000,
        [](const std::vector<uint8_t>& b){ return 20.0f*(float)U8(b,'K'); }},
    };
    plan.push_back(p);
  // }

  // // 21AF ---------------------------------------------------------------------
  // {
  //   diag::PidPlan p; p.header = "7E0"; p.modePid = "21AF"; p.needsSession = true;
  //   p.metrics = {
  //     {"ST_ВВОД КОДА ВЫПОЛНЕН","ET006","", 0, 1,
  //       [](const std::vector<uint8_t>& b){ return (float)getBIT(b,'A',5); }},
  //     {"ST_ИМОБИЛАЙЗЕР","ET003","", 0, 1,
  //       [](const std::vector<uint8_t>& b){ return (float)getBIT(b,'A',6); }},
  //     {"ST_УДАР ОБНАРУЖЕН","ET077","", 0, 255,
  //       [](const std::vector<uint8_t>& b){ return (float)U8(b,'E'); }},
  //   };
  //   plan.push_back(p);
  // }

  // 21C1 ---------------------------------------------------------------------
  {
    diag::PidPlan p; p.header = "7E0"; p.modePid = "21C1"; p.needsSession = true;
    p.metrics = {
      {"PR_ДАВЛЕНИЕ ХЛАДАГЕНТА","PR037","bar", 0, 25,
        [](const std::vector<uint8_t>& b){ return 0.2f*(float)U8(b,'J'); }},
      {"PR_МАКС. ДОПУСТ. МОЩНОСТЬ СОПРОТИВЛ. ОБОГР.","PR127","W", 0, 1200,
        [](const std::vector<uint8_t>& b){ return 100.0f*(float)U8(b,'P'); }},
      {"ST_ЗАПРОС НА ВКЛЮЧЕНИЕ КОМПРЕССОРА","ET088","", 0, 1,
        [](const std::vector<uint8_t>& b){ return (float)getBIT(b,'N',0); }},
      {"ST_ЧИСЛО РОС ЗАФИКСИРОВАНО","ET111","", 0, 3,
        [](const std::vector<uint8_t>& b){ return (float)((U8(b,'N') >> 6) & 0x03); }},
      {"ST_ОТКЛЮЧЕНИЕ РОС","ET112","", 0, 3,
        [](const std::vector<uint8_t>& b){ return (float)((U8(b,'N') >> 6) & 0x03); }},
    };
    plan.push_back(p);
  }

  // // 21C7 ---------------------------------------------------------------------
  // {
  //   diag::PidPlan p; p.header = "7E0"; p.modePid = "21C7"; p.needsSession = true;
  //   p.metrics = {
  //     {"ST_ЗАПУСК","ET076","", 0, 1,
  //       [](const std::vector<uint8_t>& b){ return (float)getBIT(b,'J',2); }},
  //   };
  //   plan.push_back(p);
  // }

  // // 21D1 ---------------------------------------------------------------------
  // {
  //   diag::PidPlan p; p.header = "7E0"; p.modePid = "21D1"; p.needsSession = true;
  //   p.metrics = {
  //     {"ST_ПЕДАЛЬ ТОРМОЗА","ET039","", 0, 1,
  //       [](const std::vector<uint8_t>& b){ return (float)getBIT(b,'I',0); }},
  //     {"ST_КОНТАКТ № 1 ВЫКЛЮЧАТЕЛЯ СТОП-СИГНАЛА","ET704","", 0, 255,
  //       [](const std::vector<uint8_t>& b){ return (float)U8(b,'B'); }},
  //     {"ST_КОНТАКТ № 2 ВЫКЛЮЧАТЕЛЯ СТОП-СИГНАЛА","ET705","", 0, 255,
  //       [](const std::vector<uint8_t>& b){ return (float)U8(b,'I'); }},
  //     {"ST_РЕГУЛЯТОР СКОРОСТИ","LC120","", 0, 1,
  //       [](const std::vector<uint8_t>& b){ return (float)getBIT(b,'K',3); }},
  //     {"ST_ОГРАНИЧИТЕЛЬ СКОРОСТИ","LC121","", 0, 1,
  //       [](const std::vector<uint8_t>& b){ return (float)getBIT(b,'K',4); }},
  //   };
  //   plan.push_back(p);
  // }

  // // 2182 ---------------------------------------------------------------------
  // {
  //   diag::PidPlan p; p.header = "7E0"; p.modePid = "2182"; p.needsSession = true;
  //   p.metrics = {
  //     {"ST_ВЕРСИЯ ДИАГНОСТИКИ","MAS2","", 0, 255,
  //       [](const std::vector<uint8_t>& b){ return (float)U8(b,'M'); }},
  //   };
  //   plan.push_back(p);
  // }

  return plan;
}
