#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <functional>

namespace diag {

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

// ---- byte helpers ----
inline uint8_t B(const std::vector<uint8_t>& v, char sym) {
  int i = (sym >= 'A' && sym <= 'Z') ? (sym - 'A') : 0;
  return (i >= 0 && (size_t)i < v.size()) ? v[(size_t)i] : 0;
}
inline uint16_t W(const std::vector<uint8_t>& v, char hi, char lo) {
  return (uint16_t(B(v, hi)) << 8) | B(v, lo);
}
inline float getBIT(const std::vector<uint8_t>& v, char sym, int bit) {
  return (B(v, sym) >> bit) & 0x1;
}

// Strip UDS positive response service+PID (e.g., 0x61 0xA0) → return *data bytes only*.
// If already data-only, it just returns the same vector.
inline std::vector<uint8_t> udsDataOnly(const std::vector<uint8_t>& raw) {
  if (raw.size() >= 2 && (raw[0] == 0x61 || raw[0] == 0x62 || raw[0] == 0x50))
    return std::vector<uint8_t>(raw.begin() + 2, raw.end());
  return raw;
}

} // namespace diag
