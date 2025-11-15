#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <Arduino.h>
#include <functional>

// ---- metric + plan types ----
struct MetricDef {
  const char* name;
  const char* shortName;
  const char* units;     // "" if boolean / raw
  float minVal;
  float maxVal;
  std::function<float(const std::vector<uint8_t>&)> eval; // expects DATA BYTES ONLY
};

struct PidPlan {
  const char* header;        // "7E0", "74B", ...
  const char* modePid;       // "21A0", "2101", ...
  bool needsSession;         // true if SDS 10 C0 is required
  std::vector<MetricDef> metrics;
};
 
// Strip UDS positive response service+PID (e.g., 0x61 0xA0) → return *data bytes only*.
// If already data-only, it just returns the same vector.
inline std::vector<uint8_t> udsDataOnly(const std::vector<uint8_t>& raw) {
  if (raw.size() >= 2 && (raw[0] == 0x61 || raw[0] == 0x62 || raw[0] == 0x50))
    return std::vector<uint8_t>(raw.begin() + 2, raw.end());
  return raw;
}
static inline uint8_t _safe_at(const std::vector<uint8_t>& b, size_t idx) {
  return (idx < b.size()) ? b[idx] : 0;
}

// single-letter symbol A..Z  (A=0, B=1, ...)
static inline uint8_t U8(const std::vector<uint8_t>& b, char L) {
  if (L < 'A' || L > 'Z') return 0;
  return _safe_at(b, static_cast<size_t>(L - 'A'));
}

static inline uint16_t U16(const std::vector<uint8_t>& b, char hi, char lo) {
  return (uint16_t(U8(b, hi)) << 8) | U8(b, lo);
}

static inline uint8_t getBIT(const std::vector<uint8_t>& b, char L, uint8_t n) {
  return (U8(b, L) >> n) & 0x01;
}

// optional: two-letter AA..AZ (AA=26, AB=27, ...). Safe even if unused.
static inline uint8_t U8x(const std::vector<uint8_t>& b, const char* sym) {
  if (!sym || !sym[0]) return 0;
  size_t pos = 0;
  if (!sym[1]) {
    if (sym[0] < 'A' || sym[0] > 'Z') return 0;
    pos = size_t(sym[0] - 'A');
  } else {
    if (sym[0] != 'A' || sym[1] < 'A' || sym[1] > 'Z') return 0;
    pos = 26 + size_t(sym[1] - 'A');
  }
  return _safe_at(b, pos);
}
