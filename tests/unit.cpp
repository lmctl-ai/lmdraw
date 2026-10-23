#include <lunasvg.h>
#include <plutovg.h>

#include "tick_format.hpp"

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

static std::vector<char> slurp_font(const char* rel) {
  for (const std::string prefix : {"", "../"}) {
    std::ifstream in(prefix + rel, std::ios::binary);
    if (in) return std::vector<char>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  }
  return {};
}

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
  if (lmdraw::superscript_int(0) != "⁰" || lmdraw::superscript_int(-3) != "⁻³" ||
      lmdraw::superscript_int(12) != "¹²" || lmdraw::log_tick_label(-3) != "10⁻³" ||
      lmdraw::log_tick_label(-2) != "0.01" || lmdraw::log_tick_label(0) != "1" ||
      lmdraw::log_tick_label(3) != "1000" || lmdraw::log_tick_label(4) != "10⁴" ||
      lmdraw::log_tick_label(6) != "10⁶")
    return 7;

  // Per-glyph math/symbol fallback in plutovg_font_face.
  const auto reg_bytes = slurp_font("assets/fonts/NotoSans-Regular.ttf");
  const auto math_bytes = slurp_font("assets/fonts/NotoSansMath-Regular.ttf");
  if (reg_bytes.empty() || math_bytes.empty()) return 8;
  plutovg_font_face_t* face0 = plutovg_font_face_load_from_data(reg_bytes.data(), (unsigned int)reg_bytes.size(), 0, nullptr, nullptr);
  if (!face0) return 9;
  plutovg_path_t* path = plutovg_path_create();
  plutovg_path_t* notdef_path = plutovg_path_create();
  plutovg_font_face_get_glyph_path(face0, 16.0f, 0.0f, 0.0f, 0xFFFF, notdef_path);
  plutovg_font_face_get_glyph_path(face0, 16.0f, 0.0f, 0.0f, 0x221D, path);
  const plutovg_path_element_t* notdef_data = nullptr;
  const int notdef_elems = plutovg_path_get_elements(notdef_path, &notdef_data);
  const plutovg_path_element_t* path_data = nullptr;
  if (plutovg_path_get_elements(path, &path_data) != notdef_elems ||
      std::memcmp(path_data, notdef_data, size_t(notdef_elems) * sizeof(plutovg_path_element_t)) != 0)
    return 10; // without fallback, ∝ is .notdef
  plutovg_font_face_destroy(face0);
  if (!plutovg_font_face_set_fallback_data(math_bytes.data(), (unsigned int)math_bytes.size()))
    return 11;
  plutovg_font_face_t* face = plutovg_font_face_load_from_data(reg_bytes.data(), (unsigned int)reg_bytes.size(), 0, nullptr, nullptr);
  if (!face) return 9;
  for (const plutovg_codepoint_t cp : {0x221Du, 0x2192u, 0x2190u, 0x2191u, 0x2193u, 0x2194u, 0x21D2u, 0x21D4u,
                                       0x221Eu, 0x2264u, 0x2265u, 0x2248u, 0x2260u, 0x221Au, 0x2202u, 0x2206u,
                                       0x2207u, 0x222Bu, 0x2211u, 0x220Fu, 0x2713u, 0x2714u, 0x2717u}) {
    plutovg_path_reset(path);
    const float adv = plutovg_font_face_get_glyph_path(face, 16.0f, 0.0f, 0.0f, cp, path);
    const int elems = plutovg_path_get_elements(path, &path_data);
    const bool same_as_notdef = (elems == notdef_elems) &&
        (std::memcmp(path_data, notdef_data, size_t(notdef_elems) * sizeof(plutovg_path_element_t)) == 0);
    if (adv <= 0.0f || elems <= 0 || same_as_notdef) {
      std::cerr << "missing fallback glyph U+" << std::hex << cp << "\n";
      return 12;
    }
  }
  plutovg_path_destroy(notdef_path);
  plutovg_path_destroy(path);
  plutovg_font_face_destroy(face);

  std::cout << "tick labels: uniform decimals, superscripts, and log decades without float noise\n";
  std::cout << "font fallback: math and arrow glyphs resolve with non-empty outlines\n";
  std::cout << "renderer smoke: deterministic 16x16 raster; foreground differs from background\n";
  return 0;
}
