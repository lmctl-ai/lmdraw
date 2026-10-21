#include <lunasvg.h>

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
  std::cout << "renderer smoke: deterministic 16x16 raster; foreground differs from background\n";
  return 0;
}
