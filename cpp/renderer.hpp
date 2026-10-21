#pragma once

#include <cstdint>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>

namespace lmdraw {

// The host passes a complete scene description across this boundary. Backend
// types are private to their implementation and never enter the schema.
class ImportedDocument {
 public:
  virtual ~ImportedDocument() = default;
  virtual double width() const = 0;
  virtual double height() const = 0;
};

class Renderer {
 public:
  virtual ~Renderer() = default;
  virtual std::unique_ptr<ImportedDocument> load_svg(const std::string& source) = 0;
  virtual bool has_element(ImportedDocument& document, const std::string& id) = 0;
  virtual void set_attribute(ImportedDocument& document, const std::string& id,
                             const std::string& name, const std::string& value) = 0;
  virtual void render_imported(ImportedDocument& document, const std::string& overlay,
                               int width, int height, const std::filesystem::path& output) = 0;
  virtual void render_svg(const std::string& document, int width, int height,
                          uint32_t background, const std::filesystem::path& output) = 0;
  virtual void add_font(const std::string& family, bool bold, const unsigned char* data, size_t size) = 0;
  virtual bool load_font_file(const std::string& family, bool bold, const std::filesystem::path& path) = 0;
  virtual double measure_text(const std::string& text, float size, bool bold) = 0;
};

Renderer& renderer();

}  // namespace lmdraw
