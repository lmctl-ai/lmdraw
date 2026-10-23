#pragma once
// Axis tick label formatting shared by Scene.axes and the unit test.
#include <cmath>
#include <cstdio>
#include <string>

namespace lmdraw {

// Number of decimals that shows every tick of a uniform step distinctly:
// the fewest decimals that print the step exactly, or one more than the
// step's leading digit position for non-terminating steps such as 1/3.
inline int tick_decimals(double step) {
  step = std::fabs(step);
  if (!std::isfinite(step) || step <= 0) return 0;
  int base = static_cast<int>(std::ceil(-std::log10(step) - 1e-9));
  if (base < 0) base = 0;
  for (int d = base; d <= base + 1 && d <= 9; ++d) {
    const double scaled = step * std::pow(10.0, d);
    if (std::fabs(scaled - std::round(scaled)) < 1e-6 * scaled) return d;
  }
  return base + 1 > 9 ? 9 : base + 1;
}

inline std::string tick_label(double value, int decimals) {
  char buffer[64];
  std::snprintf(buffer, sizeof buffer, "%.*f", decimals, value);
  std::string text(buffer);
  // A tick that rounds to zero prints as 0, never -0 or -0.00.
  if (text[0] == '-' && text.find_first_not_of("-0.") == std::string::npos) text.erase(0, 1);
  return text;
}

inline std::string superscript_int(int value) {
  static const char* const kDigits[10] = {
      "\xe2\x81\xb0", "\xc2\xb9", "\xc2\xb2", "\xc2\xb3", "\xe2\x81\xb4",
      "\xe2\x81\xb5", "\xe2\x81\xb6", "\xe2\x81\xb7", "\xe2\x81\xb8", "\xe2\x81\xb9"};
  std::string digits = std::to_string(std::abs(value));
  std::string out = value < 0 ? "\xe2\x81\xbb" : "";
  for (char c : digits) out += kDigits[c - '0'];
  return out;
}

// Decade tick label for logarithmic axes: plain decimals for 0.01..1000 and
// 10^k with Unicode superscripts outside that range.
inline std::string log_tick_label(int exp10) {
  switch (exp10) {
    case -2: return "0.01";
    case -1: return "0.1";
    case 0: return "1";
    case 1: return "10";
    case 2: return "100";
    case 3: return "1000";
    default: return "10" + superscript_int(exp10);
  }
}

}  // namespace lmdraw
