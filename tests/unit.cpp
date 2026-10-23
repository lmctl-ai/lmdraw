#include <lunasvg.h>

#include "tick_format.hpp"

#include <cstdlib>
#include <cstring>
#include <iostream>

int main() {
  const auto document = lunasvg::Document::loadFromData(
      "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16'>"
      "<circle cx='8' cy='8' r='5' fill='#f5c16c'/></svg>");
  if (!document) return 1;
  const auto first = document->renderToBitmap(16, 16, 0x152438ff);
  const auto second = document->renderToBitmap(16, 16, 0x152438ff);
  if (first.isNull() || second.isNull() || first.width() != 16 || first.height() != 16) return 2;
  if (std::memcmp(first.data(), second.data(), 16u * 16u * 4u) != 0) return 3;
  if (std::memcmp(first.data(), first.data() + (8u * 16u + 8u) * 4u, 4u) == 0) return 4;
  // Axis tick labels: one decimal count per axis, no float noise, no -0.
  struct Case { double step; int decimals; };
  for (const Case c : {Case{2.0, 0}, Case{0.5, 1}, Case{0.25, 2}, Case{0.1, 1}, Case{1.0 / 3.0, 2},
                       Case{2500.0, 0}, Case{0.002, 3}, Case{0.0, 0}})
    if (lmdraw::tick_decimals(c.step) != c.decimals) {
      std::cerr << "tick_decimals(" << c.step << ") = " << lmdraw::tick_decimals(c.step) << ", expected " << c.decimals << "\n";
      return 5;
    }
  if (lmdraw::tick_label(0.1 + 0.2, 1) != "0.3" || lmdraw::tick_label(2.0 / 3.0, 2) != "0.67" ||
      lmdraw::tick_label(-1e-12, 2) != "0.00" || lmdraw::tick_label(-0.5, 1) != "-0.5" ||
      lmdraw::tick_label(1500.0, 0) != "1500")
    return 6;
  std::cout << "tick labels: uniform decimals without float noise or negative zero\n";
  std::cout << "renderer smoke: deterministic 16x16 raster; foreground differs from background\n";
  return 0;
}
