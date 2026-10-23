#include "renderer.hpp"

#include <lunasvg.h>
#include <plutovg.h>
#include <algorithm>
#include <climits>
#include <cmath>
#include <stdexcept>

namespace lmdraw {
namespace {

class LunaDocument final : public ImportedDocument {
 public:
  explicit LunaDocument(std::unique_ptr<lunasvg::Document> doc) : document(std::move(doc)) {}
  double width() const override { return document->width(); }
  double height() const override { return document->height(); }
  std::unique_ptr<lunasvg::Document> document;
};

LunaDocument& as_luna(ImportedDocument& document) {
  auto* value = dynamic_cast<LunaDocument*>(&document);
  if (!value) throw std::runtime_error("imported document belongs to another renderer");
  return *value;
}

void composite(lunasvg::Bitmap& base, const lunasvg::Bitmap& layer) {
  if (base.isNull() || layer.isNull() || base.width() != layer.width() || base.height() != layer.height())
    throw std::runtime_error("rendered scene layers have different dimensions");
  auto* dst = reinterpret_cast<uint32_t*>(base.data());
  const auto* src = reinterpret_cast<const uint32_t*>(layer.data());
  const size_t count = size_t(base.width()) * size_t(base.height());
  for (size_t i = 0; i < count; ++i) {
    const uint32_t s = src[i], d = dst[i];
    const uint32_t alpha = s >> 24, inverse = 255 - alpha;
    const uint32_t a = alpha + (((d >> 24) * inverse + 127) / 255);
    const uint32_t r = ((s >> 16) & 255) + ((((d >> 16) & 255) * inverse + 127) / 255);
    const uint32_t g = ((s >> 8) & 255) + ((((d >> 8) & 255) * inverse + 127) / 255);
    const uint32_t b = (s & 255) + (((d & 255) * inverse + 127) / 255);
    dst[i] = (a << 24) | (std::min(r, 255u) << 16) | (std::min(g, 255u) << 8) | std::min(b, 255u);
  }
}

class LunaRenderer final : public Renderer {
 public:
  std::unique_ptr<ImportedDocument> load_svg(const std::string& source) override {
    auto doc = lunasvg::Document::loadFromData(source);
    if (!doc) throw std::runtime_error("SVG document could not be parsed");
    return std::make_unique<LunaDocument>(std::move(doc));
  }
  bool has_element(ImportedDocument& document, const std::string& id) override {
    return bool(as_luna(document).document->getElementById(id));
  }
  void set_attribute(ImportedDocument& document, const std::string& id,
                    const std::string& name, const std::string& value) override {
    auto element = as_luna(document).document->getElementById(id);
    if (!element) throw std::runtime_error("SVG element id is no longer available");
    element.setAttribute(name, value);
  }
  void render_imported(ImportedDocument& document, const std::string& overlay,
                       int width, int height, const std::filesystem::path& output) override {
    auto& source = as_luna(document).document;
    auto bitmap = source->renderToBitmap(width, height, 0x152438ff);
    if (bitmap.isNull()) throw std::runtime_error("SVG scene could not be rendered");
    if (!overlay.empty()) {
      auto doc = lunasvg::Document::loadFromData(overlay);
      if (!doc) throw std::runtime_error("scene overlay could not be parsed; check node attributes");
      auto layer = doc->renderToBitmap(width, height, 0x00000000);
      composite(bitmap, layer);
    }
    if (!bitmap.writeToPng(output.string())) throw std::runtime_error("failed to write PNG output");
  }
  void render_svg(const std::string& source, int width, int height,
                  uint32_t background, const std::filesystem::path& output) override {
    auto document = lunasvg::Document::loadFromData(source);
    if (!document) throw std::runtime_error("scene description could not be rendered; check node attributes");
    auto bitmap = document->renderToBitmap(width, height, background);
    if (bitmap.isNull() || !bitmap.writeToPng(output.string()))
      throw std::runtime_error("failed to render PNG output");
  }
  void add_font(const std::string& family, bool bold, const unsigned char* data, size_t size) override {
    if (!lunasvg_add_font_face_from_data(family.c_str(), bold, false, data, size, nullptr, nullptr))
      throw std::runtime_error("embedded font could not be registered");
    if (family == "Noto Sans") {
      if (bold) { bold_data_ = data; bold_size_ = size; }
      else { regular_data_ = data; regular_size_ = size; }
    }
  }
  void add_fallback_font(const unsigned char* data, size_t size) override {
    if (!plutovg_font_face_set_fallback_data(data, static_cast<unsigned int>(size)))
      throw std::runtime_error("embedded fallback font could not be registered");
  }
  bool load_font_file(const std::string& family, bool bold, const std::filesystem::path& path) override {
    return lunasvg_add_font_face_from_file(family.c_str(), bold, false, path.string().c_str());
  }
  double measure_text(const std::string& text, float size, bool bold) override {
    const auto* data = bold ? bold_data_ : regular_data_;
    const auto length = bold ? bold_size_ : regular_size_;
    if (!data || text.size() > static_cast<size_t>(INT_MAX)) throw std::runtime_error("font metrics are unavailable");
    auto* face = plutovg_font_face_load_from_data(data, static_cast<unsigned int>(length), 0, nullptr, nullptr);
    if (!face) throw std::runtime_error("font metrics are unavailable");
    const float width = plutovg_font_face_text_extents(face, size, text.data(), static_cast<int>(text.size()),
                                                        PLUTOVG_TEXT_ENCODING_UTF8, nullptr);
    plutovg_font_face_destroy(face);
    if (!std::isfinite(width) || width < 0) throw std::runtime_error("text width could not be measured");
    return width;
  }
 private:
  const unsigned char* regular_data_ = nullptr;
  const unsigned char* bold_data_ = nullptr;
  size_t regular_size_ = 0;
  size_t bold_size_ = 0;
};

}  // namespace

Renderer& renderer() {
  static LunaRenderer instance;
  return instance;
}

}  // namespace lmdraw
