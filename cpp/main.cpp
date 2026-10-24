#include "dslvm.hpp"
#include "resources.hpp"
#include "renderer.hpp"
#include "tick_format.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <regex>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;
namespace {
using dslvm::Args;
using dslvm::Value;

struct Node {
  std::string tag;
  std::map<std::string, std::string> attrs;
  std::string text;
  std::vector<Node*> children;
  Node* parent = nullptr;
  struct Scene* owner = nullptr;
  uint64_t generation = 0;
  std::string svg_id;
  bool from_svg = false;
};

struct Scene {
  int width = 0, height = 0;
  std::string background = "#152438", markup;
  bool loaded_svg = false;
  std::string loaded_svg_path;
  std::unique_ptr<lmdraw::ImportedDocument> document;
  std::vector<std::unique_ptr<Node>> nodes;
  std::string definitions;
  struct GradientData { std::string id; float x1, y1, x2, y2; Scene* owner = nullptr; uint64_t generation = 0; std::vector<std::pair<float, std::string>> stops; };
  std::vector<std::unique_ptr<GradientData>> gradients;
  struct ShapeData;
  struct GridData;
  std::vector<std::unique_ptr<ShapeData>> shapes;
  std::vector<std::unique_ptr<GridData>> grids;
  uint64_t generation = 0;

  void require_canvas() const {
    if (width <= 0 || height <= 0)
      throw dslvm::Error("call scene.canvas or scene.load_svg before drawing");
  }
  void require_building() const {
    require_canvas();
  }
};

void finite(double v, const char* label) {
  if (!std::isfinite(v) || std::abs(v) > 1'000'000.0)
    throw dslvm::Error(std::string(label) + " must be finite and within +/-1000000 pixels");
}
void positive(double v, const char* label) {
  finite(v, label);
  if (v <= 0) throw dslvm::Error(std::string(label) + " must be positive");
}
void validate_dims(int64_t w, int64_t h) {
  if (w < 1 || h < 1 || w > 4096 || h > 4096 || uint64_t(w) * uint64_t(h) > 8'000'000)
    throw dslvm::Error("canvas must fit 1..4096 pixels per side and 8000000 pixels total");
}
std::string esc(const std::string& s) {
  std::string o;
  for (char c : s) {
    switch (c) {
      case '&': o += "&amp;"; break;
      case '<': o += "&lt;"; break;
      case '>': o += "&gt;"; break;
      case '"': o += "&quot;"; break;
      case '\'': o += "&apos;"; break;
      default: o += c;
    }
  }
  return o;
}
std::string num(double x) {
  std::ostringstream o;
  o.precision(9);
  o << x;
  return o.str();
}
fs::path safe_svg_path(const std::string& raw) {
  fs::path p(raw);
  if (p.is_absolute() || p.has_root_name() || p.extension() != ".svg" || raw.rfind("assets/", 0) != 0)
    throw dslvm::Error("load_svg accepts only an assets/*.svg relative path");
  for (const auto& part : p)
    if (part == ".." || part == ".") throw dslvm::Error("load_svg path traversal is refused");
  if (fs::is_symlink(fs::symlink_status("assets"))) throw dslvm::Error("assets/ may not be a symbolic link");
  auto root = fs::weakly_canonical("assets");
  auto full = fs::weakly_canonical(p);
  if (full.parent_path() != root || !fs::is_regular_file(full) || fs::file_size(full) > 2 * 1024 * 1024)
    throw dslvm::Error("load_svg path must name a file directly under assets/ (max 2 MiB)");
  return full;
}
fs::path safe_font_path(const std::string& raw) {
  fs::path p(raw);
  if (p.is_absolute() || p.has_root_name() || p.extension() != ".ttf" || raw.rfind("assets/fonts/", 0) != 0)
    throw dslvm::Error("load_font accepts only a .ttf file directly under assets/fonts/");
  for (const auto& part : p)
    if (part == ".." || part == ".") throw dslvm::Error("font path traversal is refused");
  if (fs::is_symlink(fs::symlink_status("assets")) || fs::is_symlink(fs::symlink_status("assets/fonts")))
    throw dslvm::Error("assets/fonts/ may not be a symbolic link");
  const auto root = fs::weakly_canonical("assets/fonts"), full = fs::weakly_canonical(p);
  if (full.parent_path() != root || !fs::is_regular_file(full) || fs::file_size(full) > 8 * 1024 * 1024)
    throw dslvm::Error("font path must name a file directly under assets/fonts/ (max 8 MiB)");
  return full;
}
// PNG outputs go directly under one of two relative roots: build/rendered/
// (the original contract) or images/ (what self-running examples write for
// their users). Both roots are created on demand and may not be symbolic links.
fs::path safe_png_path(const std::string& raw) {
  fs::path p(raw);
  const bool to_images = raw.rfind("images/", 0) == 0;
  if (p.is_absolute() || p.has_root_name() || p.extension() != ".png" ||
      (!to_images && raw.rfind("build/rendered/", 0) != 0))
    throw dslvm::Error("PNG output must be a .png file directly under build/rendered/ or images/");
  for (const auto& part : p)
    if (part == ".." || part == ".") throw dslvm::Error("PNG output traversal is refused");
  std::error_code ec;
  const char* root_name = to_images ? "images" : "build/rendered";
  if (!to_images) {
    const auto build_status = fs::symlink_status("build", ec);
    if (ec || fs::is_symlink(build_status) || !fs::is_directory(build_status))
      throw dslvm::Error("build/ must be a real output directory");
  }
  auto rendered_status = fs::symlink_status(root_name, ec);
  if (!ec && rendered_status.type() == fs::file_type::not_found) {
    fs::create_directory(root_name, ec);
    if (ec) throw dslvm::Error(std::string("could not create ") + root_name + "/");
    rendered_status = fs::symlink_status(root_name, ec);
  }
  if (ec || fs::is_symlink(rendered_status) || !fs::is_directory(rendered_status))
    throw dslvm::Error(std::string(root_name) + "/ must be a real directory, not a symbolic link");
  auto root = fs::weakly_canonical(root_name);
  auto parent = fs::weakly_canonical(p.parent_path());
  auto full = fs::weakly_canonical(p);
  if (parent != root || full.parent_path() != root)
    throw dslvm::Error(std::string("PNG output must remain directly under ") + root_name + "/");
  if (fs::is_symlink(fs::symlink_status(full, ec)))
    throw dslvm::Error("PNG output may not replace a symbolic link");
  return full;
}

class AtomicOutput {
 public:
  explicit AtomicOutput(fs::path destination) : destination_(std::move(destination)) {
    static std::atomic<uint64_t> sequence{0};
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    for (int tries = 0; tries < 100; ++tries) {
      temp_ = destination_.parent_path() /
          (destination_.filename().string() + ".tmp-" + std::to_string(stamp) + "-" + std::to_string(sequence++));
#ifdef _WIN32
      HANDLE h = CreateFileW(temp_.wstring().c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                             FILE_ATTRIBUTE_TEMPORARY, nullptr);
      if (h != INVALID_HANDLE_VALUE) { CloseHandle(h); return; }
      if (GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_ALREADY_EXISTS) break;
#else
      int fd = ::open(temp_.c_str(), O_CREAT | O_EXCL | O_WRONLY | O_NOFOLLOW, 0666);
      if (fd >= 0) { ::close(fd); return; }
      if (errno != EEXIST) break;
#endif
    }
    throw dslvm::Error("could not create a temporary PNG output file");
  }
  ~AtomicOutput() { if (!committed_) { std::error_code ec; fs::remove(temp_, ec); } }
  const fs::path& path() const { return temp_; }
  void commit() {
#ifdef _WIN32
    if (!MoveFileExW(temp_.wstring().c_str(), destination_.wstring().c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
      throw dslvm::Error("could not atomically replace PNG output");
#else
    if (::rename(temp_.c_str(), destination_.c_str()) != 0)
      throw dslvm::Error("could not atomically replace PNG output");
#endif
    committed_ = true;
  }
 private:
  fs::path destination_, temp_;
  bool committed_ = false;
};
void validate_svg_subset(const std::string& bytes) {
  static const std::regex forbidden(
      R"(<\s*(script|style|foreignObject|filter|animate|animateTransform|animateMotion|set|link)\b|\b(class|style)\s*=|\bon[a-z]+\s*=|\bhref\s*=\s*["'](?!#)|url\(\s*["']?(?!#))",
      std::regex::icase);
  if (std::regex_search(bytes, forbidden))
    throw dslvm::Error("SVG uses a feature outside the subset; inline presentational attributes and remove scripts, CSS, filters, and external references");
  static const std::regex doctype(R"(<\s*!\s*(DOCTYPE|ENTITY)\b)", std::regex::icase);
  if (std::regex_search(bytes, doctype)) throw dslvm::Error("SVG DTDs and custom entities are not supported");
  static const std::vector<std::string> elements = {
      "svg", "g", "title", "desc", "rect", "circle", "ellipse", "line", "polyline", "polygon",
      "path", "text", "tspan", "defs", "linearGradient", "radialGradient", "stop", "clipPath", "mask", "use"};
  static const std::vector<std::string> attributes = {
      "xmlns", "xmlns:xlink", "width", "height", "viewBox", "preserveAspectRatio", "role", "aria-labelledby",
      "aria-label", "id", "x", "y", "rx", "ry", "cx", "cy", "r", "x1", "y1", "x2", "y2", "points", "d",
      "fill", "fill-opacity", "stroke", "stroke-width", "stroke-linecap", "stroke-linejoin", "stroke-opacity",
      "stroke-dasharray", "stroke-dashoffset", "opacity", "transform", "font-family", "font-size", "font-weight", "text-anchor", "href", "xlink:href",
      "gradientUnits", "gradientTransform", "offset", "stop-color", "stop-opacity", "clip-path", "mask"};
  static const std::regex tag(R"(<\s*([A-Za-z][\w:-]*)\b([^<>]*)>)");
  static const std::regex attr(R"(\s+([A-Za-z_:][\w:.-]*)\s*=)");
  for (std::sregex_iterator it(bytes.begin(), bytes.end(), tag), end; it != end; ++it) {
    const std::string name = (*it)[1].str();
    if (std::find(elements.begin(), elements.end(), name) == elements.end())
      throw dslvm::Error("unsupported SVG element <" + name + ">; use the documented static SVG subset");
    const std::string source_attrs = (*it)[2].str();
    for (std::sregex_iterator ai(source_attrs.begin(), source_attrs.end(), attr), ae; ai != ae; ++ai) {
      const std::string attr_name = (*ai)[1].str();
      if (std::find(attributes.begin(), attributes.end(), attr_name) == attributes.end())
        throw dslvm::Error("unsupported SVG attribute '" + attr_name + "'; use presentation attributes from the documented subset");
    }
  }
}

bool allowed_attr(const std::string& name) {
  static const std::vector<std::string> names = {
      "fill", "stroke", "stroke-width", "opacity", "fill-opacity", "stroke-opacity",
      "marker-start", "marker-end", "marker-mid", "stroke-dasharray", "stroke-dashoffset",
      "transform", "font-family", "font-size", "font-weight", "text-anchor",
      "stroke-linecap", "stroke-linejoin", "x", "y", "width", "height", "cx", "cy", "r",
      "rx", "ry", "x1", "y1", "x2", "y2", "points", "d", "href"};
  return std::find(names.begin(), names.end(), name) != names.end();
}
void validate_png_data_uri(const std::string& uri);
void validate_attr(const std::string& name, const std::string& value) {
  if (!allowed_attr(name))
    throw dslvm::Error("unsupported node attribute '" + name + "'; use SVG geometry, fill, stroke, opacity, transform, or text attributes");
  if (value.size() > 16384) throw dslvm::Error("attribute value exceeds 16384 bytes");
  if (name == "opacity" || name == "fill-opacity" || name == "stroke-opacity") {
    char* end = nullptr;
    const double n = std::strtod(value.c_str(), &end);
    if (end != value.c_str() + value.size() || !std::isfinite(n) || n < 0 || n > 1)
      throw dslvm::Error(name + " must be a number from 0 to 1");
  }
  if (name == "stroke-width" || name == "font-size") {
    char* end = nullptr;
    const double n = std::strtod(value.c_str(), &end);
    if (end != value.c_str() + value.size() || !std::isfinite(n) || n <= 0 || n > 1'000'000)
      throw dslvm::Error(name + " must be a positive number in pixels");
  }
  if (name == "x" || name == "y" || name == "width" || name == "height" || name == "cx" ||
      name == "cy" || name == "r" || name == "rx" || name == "ry" || name == "x1" || name == "y1" || name == "x2" || name == "y2") {
    char* end = nullptr;
    const double n = std::strtod(value.c_str(), &end);
    if (end != value.c_str() + value.size() || !std::isfinite(n) || std::abs(n) > 1'000'000)
      throw dslvm::Error(name + " must be a finite pixel value");
    if ((name == "width" || name == "height" || name == "r") && n <= 0)
      throw dslvm::Error(name + " must be positive");
    if ((name == "rx" || name == "ry") && n < 0)
      throw dslvm::Error(name + " must be nonnegative");
  }
  if (name == "fill" || name == "stroke") {
    static const std::regex color(R"(^#([0-9A-Fa-f]{3}|[0-9A-Fa-f]{4}|[0-9A-Fa-f]{6}|[0-9A-Fa-f]{8})$|^[A-Za-z]{1,24}$|^none$)");
    static const std::regex local_gradient(R"(^url\(#[A-Za-z_][A-Za-z0-9_.:-]{0,127}\)$)");
    if (!std::regex_match(value, color) && !std::regex_match(value, local_gradient))
      throw dslvm::Error(name + " must be a color name, hex color, or none");
  }
  if (name == "href" && value.rfind("data:image/png;base64,", 0) != 0)
    throw dslvm::Error("image href must be an embedded PNG data URI; file and network references are refused");
  if (name == "href") validate_png_data_uri(value);
  if (name == "font-weight" && value != "normal" && value != "bold" &&
      value != "100" && value != "200" && value != "300" && value != "400" &&
      value != "500" && value != "600" && value != "700" && value != "800" && value != "900")
    throw dslvm::Error("font-weight must be normal, bold, or a value from 100 to 900");
  if (name == "text-anchor" && value != "start" && value != "middle" && value != "end")
    throw dslvm::Error("text-anchor must be start, middle, or end");
  if (name == "stroke-linecap" && value != "butt" && value != "round" && value != "square")
    throw dslvm::Error("stroke-linecap must be butt, round, or square");
  if (name == "stroke-linejoin" && value != "miter" && value != "round" && value != "bevel")
    throw dslvm::Error("stroke-linejoin must be miter, round, or bevel");
  if (name == "marker-start" || name == "marker-mid" || name == "marker-end")
    if (value != "none" && value != "arrow" && value != "dot" && value != "diamond")
      throw dslvm::Error(name + " must be none, arrow, dot, or diamond");
  if (name == "stroke-dasharray") {
    if (value == "none") return;
    static const std::regex list(R"(^\s*(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+)(?:[eE][+-]?[0-9]+)?(?:\s*[, ]\s*(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+)(?:[eE][+-]?[0-9]+)?)*\s*$)");
    if (!std::regex_match(value, list))
      throw dslvm::Error("stroke-dasharray must be none or a comma/space separated list of positive pixel lengths");
    std::istringstream input(value);
    std::string token;
    size_t count = 0;
    while (std::getline(input, token, ',')) {
      std::istringstream words(token);
      std::string number;
      while (words >> number) {
        char* end = nullptr;
        const double n = std::strtod(number.c_str(), &end);
        if (end != number.c_str() + number.size() || !std::isfinite(n) || n <= 0 || n > 1'000'000)
          throw dslvm::Error("stroke-dasharray entries must be positive finite pixel lengths");
        if (++count > 32) throw dslvm::Error("stroke-dasharray may contain at most 32 lengths");
      }
    }
    if (count == 0) throw dslvm::Error("stroke-dasharray needs at least one length");
  }
  if (name == "stroke-dashoffset") {
    char* end = nullptr;
    const double n = std::strtod(value.c_str(), &end);
    if (end != value.c_str() + value.size() || !std::isfinite(n) || std::abs(n) > 1'000'000)
      throw dslvm::Error("stroke-dashoffset must be a finite pixel length");
  }
  if (name == "d" && value.find_first_not_of("MmZzLlHhVvCcSsQqTtAa0123456789.,+ -eE") != std::string::npos)
    throw dslvm::Error("path data contains unsupported characters");
}
void validate_plain_color(const std::string& name, const std::string& value) {
  validate_attr(name, value);
  if (value.rfind("url(", 0) == 0) throw dslvm::Error(name + " must be a direct color value");
}
void validate_png_data_uri(const std::string& uri) {
  const std::string prefix = "data:image/png;base64,";
  if (uri.rfind(prefix, 0) != 0 || uri.size() > 1'500'000)
    throw dslvm::Error("image accepts a PNG data URI of at most 1.5 MiB");
  const std::string payload = uri.substr(prefix.size());
  if (payload.empty() || payload.size() % 4 != 0) throw dslvm::Error("PNG data URI must contain valid base64 data");
  auto digit = [](char c) -> int {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
  };
  std::vector<unsigned char> header;
  header.reserve(24);
  bool padding = false;
  for (size_t i = 0; i < payload.size(); i += 4) {
    uint32_t value = 0;
    int padding_count = 0;
    for (size_t j = 0; j < 4; ++j) {
      const char c = payload[i + j];
      if (c == '=') { padding = true; ++padding_count; value <<= 6; }
      else {
        const int d = digit(c);
        if (d < 0 || padding) throw dslvm::Error("PNG data URI must contain valid base64 data");
        value = (value << 6) | static_cast<uint32_t>(d);
      }
    }
    if (padding_count > 2 || (padding_count && i + 4 != payload.size()))
      throw dslvm::Error("PNG data URI must contain valid base64 data");
    if (header.size() < 24) header.push_back(static_cast<unsigned char>((value >> 16) & 255));
    if (padding_count < 2 && header.size() < 24) header.push_back(static_cast<unsigned char>((value >> 8) & 255));
    if (padding_count == 0 && header.size() < 24) header.push_back(static_cast<unsigned char>(value & 255));
  }
  static const unsigned char png_signature[] = {137, 80, 78, 71, 13, 10, 26, 10};
  if (header.size() < 24 || std::memcmp(header.data(), png_signature, sizeof(png_signature)) != 0 ||
      std::memcmp(header.data() + 12, "IHDR", 4) != 0)
    throw dslvm::Error("image data URI must start with a PNG header");
  auto u32 = [&](size_t at) { return (uint32_t(header[at]) << 24) | (uint32_t(header[at + 1]) << 16) |
                                    (uint32_t(header[at + 2]) << 8) | uint32_t(header[at + 3]); };
  const uint32_t w = u32(16), h = u32(20);
  if (w == 0 || h == 0 || w > 4096 || h > 4096 || uint64_t(w) * h > 8'000'000)
    throw dslvm::Error("embedded PNG dimensions must fit 1..4096 pixels and 8000000 pixels total");
}
std::string wrap_text(const std::string& text, double width, float size) {
  std::istringstream paragraphs(text);
  std::string paragraph, result;
  size_t lines = 0;
  bool first_paragraph = true;
  while (std::getline(paragraphs, paragraph, '\n')) {
    if (!first_paragraph) result += '\n';
    first_paragraph = false;
    std::istringstream words(paragraph);
    std::string word, line;
    if (!(words >> word)) {
      ++lines;
      if (lines > 256) throw dslvm::Error("wrapped text may contain at most 256 lines");
      continue;
    }
    while (true) {
      std::string candidate = line.empty() ? word : line + " " + word;
      const double measured = lmdraw::renderer().measure_text(candidate, size, false);
      if (!line.empty() && measured > width) {
        result += line + '\n';
        if (++lines > 256) throw dslvm::Error("wrapped text may contain at most 256 lines");
        line = word;
      } else {
        line = std::move(candidate);
      }
      if (!(words >> word)) break;
    }
    result += line;
    if (++lines > 256) throw dslvm::Error("wrapped text may contain at most 256 lines");
  }
  if (!text.empty() && text.back() == '\n') {
    if (++lines > 256) throw dslvm::Error("wrapped text may contain at most 256 lines");
    result += '\n';
  }
  return result;
}
Node* add_node(Scene& s, const std::string& tag, std::map<std::string, std::string> attrs,
               std::string text = {}) {
  if (s.nodes.size() >= 10000) throw dslvm::Error("a scene may contain at most 10000 nodes");
  auto node = std::make_unique<Node>();
  node->tag = tag;
  node->attrs = std::move(attrs);
  node->text = std::move(text);
  node->owner = &s;
  node->generation = s.generation;
  Node* raw = node.get();
  s.nodes.push_back(std::move(node));
  return raw;
}
// Construct a node and adopt it into a group in one step; every grouped
// helper below uses this so children serialize under their parent group.
Node* attach(Scene& s, Node* group, const std::string& tag,
             std::map<std::string, std::string> attrs, std::string text = {}) {
  Node* child = add_node(s, tag, std::move(attrs), std::move(text));
  child->parent = group;
  group->children.push_back(child);
  return child;
}
// A Shape is a labeled diagram node (rounded box, diamond, cylinder, note,
// cluster, or grid cell) that remembers its bounding rect so connectors can
// address named anchors on it. A Grid is a rows-by-cols array of invisible
// cell shapes over an author-sized extent.
struct Scene::ShapeData {
  Scene* owner = nullptr;
  uint64_t generation = 0;
  Node* group = nullptr;
  double x = 0, y = 0, w = 0, h = 0;
};
struct Scene::GridData {
  Scene* owner = nullptr;
  uint64_t generation = 0;
  int rows = 0, cols = 0;
  std::vector<ShapeData*> cells;
};
using ShapeData = Scene::ShapeData;
using GridData = Scene::GridData;
ShapeData* live_shape(void* p) {
  auto* d = static_cast<ShapeData*>(p);
  if (!d || !d->owner || d->generation != d->owner->generation)
    throw dslvm::Error("shape handle is stale; create it after setting the canvas");
  return d;
}
GridData* live_grid(void* p) {
  auto* d = static_cast<GridData*>(p);
  if (!d || !d->owner || d->generation != d->owner->generation)
    throw dslvm::Error("grid handle is stale; create it after setting the canvas");
  return d;
}
ShapeData* new_shape(Scene& s, Node* group, double x, double y, double w, double h) {
  auto d = std::make_unique<ShapeData>();
  d->owner = &s; d->generation = s.generation; d->group = group;
  d->x = x; d->y = y; d->w = w; d->h = h;
  auto* raw = d.get();
  s.shapes.push_back(std::move(d));
  return raw;
}
std::pair<double, double> anchor_point(const ShapeData& d, const std::string& anchor,
                                       const std::string& label) {
  const double cx = d.x + d.w / 2.0, cy = d.y + d.h / 2.0;
  if (anchor == "n") return {cx, d.y};
  if (anchor == "s") return {cx, d.y + d.h};
  if (anchor == "e") return {d.x + d.w, cy};
  if (anchor == "w") return {d.x, cy};
  if (anchor == "c") return {cx, cy};
  if (anchor == "ne") return {d.x + d.w, d.y};
  if (anchor == "nw") return {d.x, d.y};
  if (anchor == "se") return {d.x + d.w, d.y + d.h};
  if (anchor == "sw") return {d.x, d.y + d.h};
  throw dslvm::Error(label + " must be one of n, s, e, w, ne, nw, se, sw, c");
}
void shape_style(const std::string& fill, const std::string& stroke) {
  validate_attr("fill", fill);
  validate_attr("stroke", stroke);
}
// Centered wrapped label shared by the labeled shapes: the baseline sits a
// third of the size below center, and a wrapped multi-line label shifts up
// 0.6em so it stays visually centered.
Node* centered_label(Scene& s, Node* group, double cx, double cy, double size,
                     const std::string& label, double wrap) {
  if (label.empty()) return nullptr;
  if (label.size() > 4096) throw dslvm::Error("shape label exceeds 4096 bytes");
  const std::string wrapped = wrap_text(label, wrap, float(size));
  double baseline = cy + 0.35 * size;
  if (wrapped.find('\n') != std::string::npos) baseline -= 0.6 * size;
  return attach(s, group, "text", {{"x", num(cx)}, {"y", num(baseline)},
      {"font-family", "Noto Sans"}, {"font-size", num(size)},
      {"fill", "#dbe7f3"}, {"text-anchor", "middle"}}, wrapped);
}
ShapeData* build_box(Scene& s, double cx, double cy, double w, double h,
                     const std::string& label, const std::string& fill, const std::string& stroke,
                     double radius, double size) {
  s.require_building();
  finite(cx, "box center x"); finite(cy, "box center y");
  positive(w, "box width"); positive(h, "box height");
  finite(radius, "box corner radius");
  if (radius < 0) throw dslvm::Error("box corner radius must be nonnegative");
  positive(size, "box label size");
  shape_style(fill, stroke);
  const double x = cx - w / 2.0, y = cy - h / 2.0;
  auto* group = add_node(s, "g", {});
  std::map<std::string, std::string> attrs = {{"x", num(x)}, {"y", num(y)},
      {"width", num(w)}, {"height", num(h)}, {"fill", fill}};
  if (stroke != "none") { attrs["stroke"] = stroke; attrs["stroke-width"] = "2"; }
  if (radius > 0) { attrs["rx"] = num(radius); attrs["ry"] = num(radius); }
  attach(s, group, "rect", std::move(attrs));
  centered_label(s, group, cx, cy, size, label, std::max(0.0, w - 16.0));
  return new_shape(s, group, x, y, w, h);
}
ShapeData* build_diamond(Scene& s, double cx, double cy, double w, double h,
                         const std::string& label, const std::string& fill,
                         const std::string& stroke, double size) {
  s.require_building();
  finite(cx, "diamond center x"); finite(cy, "diamond center y");
  positive(w, "diamond width"); positive(h, "diamond height");
  positive(size, "diamond label size");
  shape_style(fill, stroke);
  const double x = cx - w / 2.0, y = cy - h / 2.0;
  auto* group = add_node(s, "g", {});
  std::map<std::string, std::string> attrs = {
      {"d", "M " + num(cx) + " " + num(y) + " L " + num(x + w) + " " + num(cy) +
            " L " + num(cx) + " " + num(y + h) + " L " + num(x) + " " + num(cy) + " Z"},
      {"fill", fill}};
  if (stroke != "none") { attrs["stroke"] = stroke; attrs["stroke-width"] = "2"; }
  attach(s, group, "path", std::move(attrs));
  centered_label(s, group, cx, cy, size, label, std::max(0.0, w - 24.0));
  return new_shape(s, group, x, y, w, h);
}
ShapeData* build_cylinder(Scene& s, double cx, double cy, double w, double h,
                          const std::string& label, const std::string& fill,
                          const std::string& stroke, double size) {
  s.require_building();
  finite(cx, "cylinder center x"); finite(cy, "cylinder center y");
  positive(w, "cylinder width"); finite(h, "cylinder height");
  if (h <= 20.0) throw dslvm::Error("cylinder height must be greater than 20 pixels");
  positive(size, "cylinder label size");
  shape_style(fill, stroke);
  const double x = cx - w / 2.0, y = cy - h / 2.0;
  auto* group = add_node(s, "g", {});
  auto cap = [&](double cap_cy) {
    std::map<std::string, std::string> attrs = {{"cx", num(cx)}, {"cy", num(cap_cy)},
        {"rx", num(w / 2.0)}, {"ry", "10"}, {"fill", fill}};
    if (stroke != "none") { attrs["stroke"] = stroke; attrs["stroke-width"] = "2"; }
    attach(s, group, "ellipse", std::move(attrs));
  };
  std::map<std::string, std::string> body = {{"x", num(x)}, {"y", num(y + 10.0)},
      {"width", num(w)}, {"height", num(h - 20.0)}, {"fill", fill}};
  if (stroke != "none") { body["stroke"] = stroke; body["stroke-width"] = "2"; }
  attach(s, group, "rect", std::move(body));
  cap(y + 10.0);
  cap(y + h - 10.0);
  centered_label(s, group, cx, cy, size, label, std::max(0.0, w - 16.0));
  return new_shape(s, group, x, y, w, h);
}
ShapeData* build_note(Scene& s, double x, double y, double w, double h,
                      const std::string& label, const std::string& fill,
                      const std::string& stroke, double size) {
  s.require_building();
  finite(x, "note x"); finite(y, "note y");
  positive(w, "note width"); positive(h, "note height");
  positive(size, "note label size");
  shape_style(fill, stroke);
  const double fold = std::min(w, h) * 0.3;
  auto* group = add_node(s, "g", {});
  std::map<std::string, std::string> body = {
      {"d", "M " + num(x) + " " + num(y) + " H " + num(x + w - fold) +
            " L " + num(x + w) + " " + num(y + fold) + " V " + num(y + h) +
            " H " + num(x) + " Z"},
      {"fill", fill}};
  std::map<std::string, std::string> crease = {
      {"d", "M " + num(x + w - fold) + " " + num(y) + " V " + num(y + fold) +
            " H " + num(x + w)},
      {"fill", "none"}};
  if (stroke != "none") {
    body["stroke"] = stroke; body["stroke-width"] = "2";
    crease["stroke"] = stroke; crease["stroke-width"] = "2";
  }
  attach(s, group, "path", std::move(body));
  attach(s, group, "path", std::move(crease));
  if (!label.empty()) {
    if (label.size() > 4096) throw dslvm::Error("shape label exceeds 4096 bytes");
    const std::string wrapped = wrap_text(label, std::max(0.0, w - 20.0), float(size));
    attach(s, group, "text", {{"x", num(x + 10.0)}, {"y", num(y + 6.0 + size)},
        {"font-family", "Noto Sans"}, {"font-size", num(size)},
        {"fill", "#dbe7f3"}}, wrapped);
  }
  return new_shape(s, group, x, y, w, h);
}
ShapeData* build_cluster(Scene& s, double x, double y, double w, double h,
                         const std::string& label, const std::string& fill,
                         const std::string& stroke, double radius, double size) {
  s.require_building();
  finite(x, "cluster x"); finite(y, "cluster y");
  positive(w, "cluster width"); positive(h, "cluster height");
  finite(radius, "cluster corner radius");
  if (radius < 0) throw dslvm::Error("cluster corner radius must be nonnegative");
  positive(size, "cluster label size");
  shape_style(fill, stroke);
  auto* group = add_node(s, "g", {});
  std::map<std::string, std::string> attrs = {{"x", num(x)}, {"y", num(y)},
      {"width", num(w)}, {"height", num(h)}, {"fill", fill}};
  if (stroke != "none") { attrs["stroke"] = stroke; attrs["stroke-width"] = "2"; }
  if (radius > 0) { attrs["rx"] = num(radius); attrs["ry"] = num(radius); }
  attach(s, group, "rect", std::move(attrs));
  if (!label.empty()) {
    if (label.size() > 4096) throw dslvm::Error("shape label exceeds 4096 bytes");
    attach(s, group, "text", {{"x", num(x + w / 2.0)}, {"y", num(y + 13.0 + size)},
        {"font-family", "Noto Sans"}, {"font-size", num(size)},
        {"fill", stroke == "none" ? "#dbe7f3" : stroke}, {"text-anchor", "middle"},
        {"font-weight", "700"}}, label);
  }
  return new_shape(s, group, x, y, w, h);
}
GridData* build_grid(Scene& s, double x, double y, double w, double h,
                     int64_t rows, int64_t cols, double gap_x, double gap_y) {
  s.require_building();
  finite(x, "grid x"); finite(y, "grid y");
  positive(w, "grid width"); positive(h, "grid height");
  finite(gap_x, "grid gap"); finite(gap_y, "grid gap");
  if (gap_x < 0 || gap_y < 0) throw dslvm::Error("grid gaps must be nonnegative");
  if (rows < 1 || rows > 64 || cols < 1 || cols > 64)
    throw dslvm::Error("grid rows and columns must be from 1 to 64");
  const double cw = (w - double(cols - 1) * gap_x) / double(cols);
  const double ch = (h - double(rows - 1) * gap_y) / double(rows);
  if (!(cw > 0.0)) throw dslvm::Error("grid gaps must leave a positive cell width");
  if (!(ch > 0.0)) throw dslvm::Error("grid gaps must leave a positive cell height");
  if (s.nodes.size() > 10000 - size_t(2) * size_t(rows) * size_t(cols))
    throw dslvm::Error("grid would exceed the scene node limit");
  auto grid = std::make_unique<GridData>();
  grid->owner = &s; grid->generation = s.generation;
  grid->rows = int(rows); grid->cols = int(cols);
  auto* raw = grid.get();
  for (int64_t r = 0; r < rows; ++r) {
    for (int64_t c = 0; c < cols; ++c) {
      auto* group = add_node(s, "g", {});
      const double cell_x = x + double(c) * (cw + gap_x);
      const double cell_y = y + double(r) * (ch + gap_y);
      attach(s, group, "rect", {{"x", num(cell_x)}, {"y", num(cell_y)},
          {"width", num(cw)}, {"height", num(ch)}, {"fill", "none"}, {"stroke", "none"}});
      grid->cells.push_back(new_shape(s, group, cell_x, cell_y, cw, ch));
    }
  }
  s.grids.push_back(std::move(grid));
  return raw;
}
struct ConnSpec {
  double x1, y1, x2, y2;
  std::string routing, start_marker, end_marker, dash, color;
  double width;
  std::string label;
  double label_t, label_dx, label_dy;
};
std::pair<double, double> polyline_point(const std::vector<std::pair<double, double>>& pts,
                                         double t) {
  std::vector<double> lens(pts.size(), 0.0);
  double total = 0.0;
  for (size_t i = 1; i < pts.size(); ++i) {
    total += std::hypot(pts[i].first - pts[i - 1].first, pts[i].second - pts[i - 1].second);
    lens[i] = total;
  }
  if (total == 0.0) return pts.front();
  const double target = t * total;
  for (size_t i = 1; i < pts.size(); ++i) {
    if (target <= lens[i] || i + 1 == pts.size()) {
      const double seg = lens[i] - lens[i - 1];
      const double u = seg > 0.0 ? (target - lens[i - 1]) / seg : 0.0;
      return {pts[i - 1].first + u * (pts[i].first - pts[i - 1].first),
              pts[i - 1].second + u * (pts[i].second - pts[i - 1].second)};
    }
  }
  return pts.back();
}
Node* build_connector(Scene& s, const ConnSpec& c) {
  s.require_building();
  finite(c.x1, "connector x1"); finite(c.y1, "connector y1");
  finite(c.x2, "connector x2"); finite(c.y2, "connector y2");
  if (c.routing != "straight" && c.routing != "elbow" && c.routing != "curve")
    throw dslvm::Error("connector routing must be straight, elbow, or curve");
  validate_attr("marker-start", c.start_marker);
  validate_attr("marker-end", c.end_marker);
  validate_attr("stroke", c.color);
  positive(c.width, "connector width");
  if (!c.dash.empty() && c.dash != "none") validate_attr("stroke-dasharray", c.dash);
  if (c.label.size() > 4096) throw dslvm::Error("connector label exceeds 4096 bytes");
  finite(c.label_t, "connector label position");
  if (c.label_t < 0.0 || c.label_t > 1.0)
    throw dslvm::Error("connector label position must be from 0 to 1");
  finite(c.label_dx, "connector label offset"); finite(c.label_dy, "connector label offset");
  auto* group = add_node(s, "g", {});
  std::map<std::string, std::string> style = {{"stroke", c.color},
      {"stroke-width", num(c.width)}};
  if (!c.dash.empty() && c.dash != "none") style["stroke-dasharray"] = c.dash;
  std::pair<double, double> label_at{c.x1 + (c.x2 - c.x1) * c.label_t,
                                     c.y1 + (c.y2 - c.y1) * c.label_t};
  if (c.routing == "straight") {
    std::map<std::string, std::string> attrs = {{"x1", num(c.x1)}, {"y1", num(c.y1)},
        {"x2", num(c.x2)}, {"y2", num(c.y2)}, {"stroke-linecap", "round"}};
    attrs.insert(style.begin(), style.end());
    if (c.start_marker != "none") attrs["marker-start"] = c.start_marker;
    if (c.end_marker != "none") attrs["marker-end"] = c.end_marker;
    attach(s, group, "line", std::move(attrs));
  } else if (c.routing == "elbow") {
    std::vector<std::pair<double, double>> pts;
    if (c.x1 == c.x2 || c.y1 == c.y2) {
      pts = {{c.x1, c.y1}, {c.x2, c.y2}};
    } else if (std::abs(c.x2 - c.x1) >= std::abs(c.y2 - c.y1)) {
      const double mx = (c.x1 + c.x2) / 2.0;
      pts = {{c.x1, c.y1}, {mx, c.y1}, {mx, c.y2}, {c.x2, c.y2}};
    } else {
      const double my = (c.y1 + c.y2) / 2.0;
      pts = {{c.x1, c.y1}, {c.x1, my}, {c.x2, my}, {c.x2, c.y2}};
    }
    std::string d = "M " + num(pts[0].first) + " " + num(pts[0].second);
    for (size_t i = 1; i < pts.size(); ++i)
      d += " L " + num(pts[i].first) + " " + num(pts[i].second);
    std::map<std::string, std::string> attrs = {{"d", d}, {"fill", "none"},
        {"stroke-linecap", "round"}, {"stroke-linejoin", "round"}};
    attrs.insert(style.begin(), style.end());
    if (c.start_marker != "none") attrs["marker-start"] = c.start_marker;
    if (c.end_marker != "none") attrs["marker-end"] = c.end_marker;
    attach(s, group, "path", std::move(attrs));
    label_at = polyline_point(pts, c.label_t);
  } else {
    double c1x, c1y, c2x, c2y;
    if (std::abs(c.x2 - c.x1) >= std::abs(c.y2 - c.y1)) {
      c1x = c.x1 + (c.x2 - c.x1) / 2.0; c1y = c.y1;
      c2x = c.x2 - (c.x2 - c.x1) / 2.0; c2y = c.y2;
    } else {
      c1x = c.x1; c1y = c.y1 + (c.y2 - c.y1) / 2.0;
      c2x = c.x2; c2y = c.y2 - (c.y2 - c.y1) / 2.0;
    }
    std::map<std::string, std::string> attrs = {
        {"d", "M " + num(c.x1) + " " + num(c.y1) + " C " + num(c1x) + " " + num(c1y) +
              " " + num(c2x) + " " + num(c2y) + " " + num(c.x2) + " " + num(c.y2)},
        {"fill", "none"}, {"stroke-linecap", "round"}};
    attrs.insert(style.begin(), style.end());
    if (c.start_marker != "none") attrs["marker-start"] = c.start_marker;
    if (c.end_marker != "none") attrs["marker-end"] = c.end_marker;
    attach(s, group, "path", std::move(attrs));
    const double t = c.label_t, u = 1.0 - t;
    label_at = {u * u * u * c.x1 + 3.0 * u * u * t * c1x + 3.0 * u * t * t * c2x + t * t * t * c.x2,
                u * u * u * c.y1 + 3.0 * u * u * t * c1y + 3.0 * u * t * t * c2y + t * t * t * c.y2};
  }
  if (!c.label.empty())
    attach(s, group, "text", {{"x", num(label_at.first + c.label_dx)},
        {"y", num(label_at.second + c.label_dy)}, {"font-family", "Noto Sans"},
        {"font-size", "13"}, {"fill", c.color}, {"text-anchor", "middle"}}, c.label);
  return group;
}
Node* build_lifeline(Scene& s, double x, double y0, double y1, const std::string& color) {
  s.require_building();
  finite(x, "lifeline x"); finite(y0, "lifeline start"); finite(y1, "lifeline end");
  if (y1 <= y0) throw dslvm::Error("lifeline end must be below its start");
  validate_attr("stroke", color);
  auto* group = add_node(s, "g", {});
  attach(s, group, "line", {{"x1", num(x)}, {"y1", num(y0)}, {"x2", num(x)}, {"y2", num(y1)},
      {"stroke", color}, {"stroke-width", "1.5"}, {"stroke-dasharray", "5 6"}});
  return group;
}
Node* build_message(Scene& s, double x1, double y1, double x2, double y2,
                    const std::string& label, bool dashed, const std::string& color) {
  s.require_building();
  finite(x1, "message x1"); finite(y1, "message y1");
  finite(x2, "message x2"); finite(y2, "message y2");
  if (label.size() > 4096) throw dslvm::Error("message label exceeds 4096 bytes");
  validate_attr("stroke", color);
  auto* group = add_node(s, "g", {});
  std::map<std::string, std::string> attrs = {{"x1", num(x1)}, {"y1", num(y1)},
      {"x2", num(x2)}, {"y2", num(y2)}, {"stroke", color}, {"stroke-width", "2"},
      {"stroke-linecap", "round"}, {"marker-end", "arrow"}};
  if (dashed) attrs["stroke-dasharray"] = "6 5";
  attach(s, group, "line", std::move(attrs));
  if (!label.empty())
    attach(s, group, "text", {{"x", num((x1 + x2) / 2.0)}, {"y", num((y1 + y2) / 2.0 - 7.0)},
        {"font-family", "Noto Sans"}, {"font-size", "13"}, {"fill", "#dbe7f3"},
        {"text-anchor", "middle"}}, label);
  return group;
}
Node* build_activation(Scene& s, double x, double y0, double y1, const std::string& fill,
                       const std::string& stroke) {
  s.require_building();
  finite(x, "activation x"); finite(y0, "activation start"); finite(y1, "activation end");
  if (y1 <= y0) throw dslvm::Error("activation end must be below its start");
  validate_attr("fill", fill);
  validate_attr("stroke", stroke);
  auto* group = add_node(s, "g", {});
  std::map<std::string, std::string> attrs = {{"x", num(x - 4.0)}, {"y", num(y0)},
      {"width", "8"}, {"height", num(y1 - y0)}, {"fill", fill}};
  if (stroke != "none") { attrs["stroke"] = stroke; attrs["stroke-width"] = "1"; }
  attach(s, group, "rect", std::move(attrs));
  return group;
}
Node* build_self_message(Scene& s, double x, double y, const std::string& label,
                         const std::string& color) {
  s.require_building();
  finite(x, "self message x"); finite(y, "self message y");
  if (label.size() > 4096) throw dslvm::Error("message label exceeds 4096 bytes");
  validate_attr("stroke", color);
  auto* group = add_node(s, "g", {});
  attach(s, group, "path", {{"d", "M " + num(x) + " " + num(y) + " H " + num(x + 60.0) +
        " V " + num(y + 26.0) + " H " + num(x + 3.0)}, {"fill", "none"}, {"stroke", color},
      {"stroke-width", "2"}, {"stroke-linejoin", "round"}, {"marker-end", "arrow"}});
  if (!label.empty())
    attach(s, group, "text", {{"x", num(x + 70.0)}, {"y", num(y + 18.0)},
        {"font-family", "Noto Sans"}, {"font-size", "13"}, {"fill", "#dbe7f3"}}, label);
  return group;
}
using MarkerKey = std::tuple<std::string, std::string, std::string>;
MarkerKey marker_key(const Node& node, const std::string& kind) {
  const auto stroke = node.attrs.find("stroke");
  const auto width = node.attrs.find("stroke-width");
  return {kind, stroke == node.attrs.end() ? "black" : stroke->second,
          width == node.attrs.end() ? "1" : width->second};
}
std::map<MarkerKey, std::string> collect_markers(const Scene& scene) {
  std::map<MarkerKey, std::string> result;
  for (const auto& node : scene.nodes) {
    if (node->generation != scene.generation || node->from_svg) continue;
    for (const char* name : {"marker-start", "marker-mid", "marker-end"}) {
      const auto found = node->attrs.find(name);
      if (found == node->attrs.end() || found->second == "none") continue;
      const MarkerKey key = marker_key(*node, found->second);
      if (!result.count(key)) result.emplace(key, "ld-marker-" + std::to_string(result.size()));
    }
  }
  return result;
}
std::string serialize_markers(const std::map<MarkerKey, std::string>& markers) {
  if (markers.empty()) return {};
  std::string out = "<defs>";
  for (const auto& [key, id] : markers) {
    const auto& [kind, color, width] = key;
    out += "<marker id=\"" + id + "\" markerWidth=\"4\" markerHeight=\"4\" refX=\"" +
           (kind == "dot" ? "2" : "4") + "\" refY=\"2\" orient=\"auto\" markerUnits=\"strokeWidth\" overflow=\"visible\">";
    if (kind == "arrow") out += "<path d=\"M0,0 L4,2 L0,4 z\" fill=\"" + esc(color) + "\"/>";
    else if (kind == "diamond") out += "<path d=\"M0,2 L2,0 L4,2 L2,4 z\" fill=\"" + esc(color) + "\"/>";
    else out += "<circle cx=\"2\" cy=\"2\" r=\"1.5\" fill=\"" + esc(color) + "\"/>";
    out += "</marker>";
  }
  return out + "</defs>";
}
std::string serialize(const Node& node, const std::map<MarkerKey, std::string>& markers) {
  std::string out = "<" + node.tag;
  for (const auto& [name, value] : node.attrs) {
    std::string emitted = value;
    if ((name == "marker-start" || name == "marker-mid" || name == "marker-end") && value != "none")
      emitted = "url(#" + markers.at(marker_key(node, value)) + ")";
    out += " " + name + "=\"" + esc(emitted) + "\"";
  }
  out += ">";
  if (node.tag == "text" && node.text.find('\n') != std::string::npos) {
    std::istringstream lines(node.text);
    std::string line;
    size_t index = 0;
    while (std::getline(lines, line)) {
      if (index == 0) out += "<tspan x=\"" + node.attrs.at("x") + "\">" + esc(line) + "</tspan>";
      else out += "<tspan x=\"" + node.attrs.at("x") + "\" dy=\"1.2em\">" + esc(line) + "</tspan>";
      ++index;
    }
    if (!node.text.empty() && node.text.back() == '\n')
      out += "<tspan x=\"" + node.attrs.at("x") + "\" dy=\"1.2em\"></tspan>";
  } else out += esc(node.text);
  for (const Node* child : node.children) out += serialize(*child, markers);
  out += "</" + node.tag + ">";
  return out;
}
std::string serialize_gradients(const Scene& scene) {
  bool active = false;
  for (const auto& gradient : scene.gradients)
    if (gradient->generation == scene.generation) active = true;
  if (!active) return {};
  std::string out = "<defs>";
  for (const auto& g : scene.gradients) {
    if (g->generation != scene.generation) continue;
    if (g->stops.size() < 2) throw dslvm::Error("each linear gradient needs at least two stops");
    out += "<linearGradient id=\"" + esc(g->id) + "\" x1=\"" + num(g->x1) + "\" y1=\"" + num(g->y1) +
           "\" x2=\"" + num(g->x2) + "\" y2=\"" + num(g->y2) + "\">";
    for (const auto& stop : g->stops)
      out += "<stop offset=\"" + num(stop.first) + "\" stop-color=\"" + esc(stop.second) + "\"/>";
    out += "</linearGradient>";
  }
  return out + "</defs>";
}
std::string serialize_nodes(const Scene& scene, const std::map<MarkerKey, std::string>& markers) {
  // Grouped nodes are emitted by their parent; a parent pointer is set exactly
  // when a node is some group's child (Group.add and Scene.axes), so this
  // linear check replaces a quadratic search over every group's children.
  std::string out;
  for (const auto& node : scene.nodes)
    if (node->generation == scene.generation && !node->parent && !node->from_svg)
      out += serialize(*node, markers);
  return out;
}
Value set_node_attr(void* object, Args& args, const char* result_type) {
  auto* node = static_cast<Node*>(object);
  if (!node || !node->owner || node->generation != node->owner->generation)
    throw dslvm::Error("node handle is stale; create or load it after setting the canvas");
  validate_attr(args[0].s, args[1].s);
  if ((args[0].s == "marker-start" || args[0].s == "marker-mid" || args[0].s == "marker-end") &&
      node->tag != "line" && node->tag != "path" && node->tag != "polyline")
    throw dslvm::Error("markers are supported on line, path, and polyline nodes");
  if ((args[0].s == "marker-start" || args[0].s == "marker-mid" || args[0].s == "marker-end") && node->from_svg)
    throw dslvm::Error("named markers are supported on constructed line, path, and polyline nodes");
  if (node->from_svg) lmdraw::renderer().set_attribute(*node->owner->document, node->svg_id, args[0].s, args[1].s);
  else node->attrs[args[0].s] = args[1].s;
  return Value::object(result_type, object);
}
Value group_add(void* object, Args& args) {
  auto* group = static_cast<Node*>(object);
  auto* child = static_cast<Node*>(args[0].obj);
  if (!group || group->tag != "g") throw dslvm::Error("Group.add must be called on a group node");
  if (!group->owner || group->generation != group->owner->generation)
    throw dslvm::Error("group handle is stale; create it after setting the canvas");
  if (!child || child->owner != group->owner || child->from_svg)
    throw dslvm::Error("a group can contain only constructed nodes from the same scene");
  if (child->generation != child->owner->generation)
    throw dslvm::Error("node handle is stale; create it after setting the canvas");
  if (group == child) throw dslvm::Error("a group cannot contain itself");
  for (Node* ancestor = group; ancestor; ancestor = ancestor->parent)
    if (ancestor == child) throw dslvm::Error("group nesting cannot create a cycle");
  if (child->parent && child->parent != group) throw dslvm::Error("a node can belong to only one parent group");
  if (std::find(group->children.begin(), group->children.end(), child) == group->children.end()) {
    child->parent = group;
    group->children.push_back(child);
  }
  return Value::object("Group", object);
}
Node* build_log_axes(Scene& s, double x, double y, double w, double h,
                     double xmin, double xmax, double ymin, double ymax,
                     bool log_x, bool log_y, int64_t intervals, const std::string& color) {
  s.require_building();
  finite(x, "axes coordinate"); finite(y, "axes coordinate");
  finite(w, "axes coordinate"); finite(h, "axes coordinate");
  finite(xmin, "axes coordinate"); finite(xmax, "axes coordinate");
  finite(ymin, "axes coordinate"); finite(ymax, "axes coordinate");
  positive(w, "axes width"); positive(h, "axes height");
  finite(x + w, "axes right edge"); finite(y + h, "axes bottom edge");
  if (!log_x && !log_y) throw dslvm::Error("log_axes requires at least one logarithmic axis");
  if (xmax <= xmin || ymax <= ymin) throw dslvm::Error("axes ranges must be increasing");
  if (intervals < 2 || intervals > 20) throw dslvm::Error("axes tick count must be from 2 to 20");
  finite(xmax - xmin, "axes x range"); finite(ymax - ymin, "axes y range");
  if (log_x) {
    if (xmin <= 0.0) throw dslvm::Error("log_axes logarithmic x range must be positive");
    const double span = std::log10(xmax) - std::log10(xmin);
    if (!std::isfinite(span) || span < 0.05 || span > 20.0)
      throw dslvm::Error("log_axes logarithmic x range must span between 0.05 and 20 decades");
  }
  if (log_y) {
    if (ymin <= 0.0) throw dslvm::Error("log_axes logarithmic y range must be positive");
    const double span = std::log10(ymax) - std::log10(ymin);
    if (!std::isfinite(span) || span < 0.05 || span > 20.0)
      throw dslvm::Error("log_axes logarithmic y range must span between 0.05 and 20 decades");
  }
  validate_attr("stroke", color);
  if (s.nodes.size() > 10000 - 300) throw dslvm::Error("axes would exceed the scene node limit");
  auto* group = add_node(s, "g", {});
  auto add = [&](Node* child) {
    child->parent = group;
    group->children.push_back(child);
  };
  add(add_node(s, "line", {{"x1", num(x)}, {"y1", num(y + h)}, {"x2", num(x + w)}, {"y2", num(y + h)},
      {"stroke", color}, {"stroke-width", "1"}, {"stroke-linecap", "round"}}));
  add(add_node(s, "line", {{"x1", num(x)}, {"y1", num(y)}, {"x2", num(x)}, {"y2", num(y + h)},
      {"stroke", color}, {"stroke-width", "1"}, {"stroke-linecap", "round"}}));
  if (log_x) {
    const double lmin = std::log10(xmin), lmax = std::log10(xmax), lspan = lmax - lmin;
    const int dmin = static_cast<int>(std::ceil(lmin - 1e-9));
    const int dmax = static_cast<int>(std::floor(lmax + 1e-9));
    if (dmax >= dmin) {
      const int step = (dmax - dmin > 10) ? 2 : 1;
      for (int d = dmin; d <= dmax; d += step) {
        const double t = std::clamp((double(d) - lmin) / lspan, 0.0, 1.0);
        const double px = x + t * w;
        add(add_node(s, "line", {{"x1", num(px)}, {"y1", num(y + h)}, {"x2", num(px)}, {"y2", num(y + h + 5)},
            {"stroke", color}, {"stroke-width", "1"}}));
        add(add_node(s, "text", {{"x", num(px)}, {"y", num(y + h + 20)}, {"font-family", "Noto Sans"},
            {"font-size", "13"}, {"fill", color}, {"text-anchor", "middle"}}, lmdraw::log_tick_label(d)));
      }
      if (dmax - dmin <= 8) {
        for (int d = dmin - 1; d <= dmax; ++d) {
          const double base = std::pow(10.0, d);
          for (int m = 2; m <= 9; ++m) {
            const double val = m * base;
            if (val <= xmin * (1.0 + 1e-9) || val >= xmax * (1.0 - 1e-9)) continue;
            const double t = (std::log10(val) - lmin) / lspan;
            const double px = x + t * w;
            add(add_node(s, "line", {{"x1", num(px)}, {"y1", num(y + h)}, {"x2", num(px)}, {"y2", num(y + h + 3)},
                {"stroke", color}, {"stroke-width", "1"}}));
          }
        }
      }
    } else {
      const int dec = lmdraw::tick_decimals((xmax - xmin) / double(intervals));
      for (int i = 0; i <= intervals; ++i) {
        const double t = double(i) / double(intervals);
        const double val = xmin * std::pow(xmax / xmin, t);
        const double px = x + t * w;
        add(add_node(s, "line", {{"x1", num(px)}, {"y1", num(y + h)}, {"x2", num(px)}, {"y2", num(y + h + 5)},
            {"stroke", color}, {"stroke-width", "1"}}));
        add(add_node(s, "text", {{"x", num(px)}, {"y", num(y + h + 20)}, {"font-family", "Noto Sans"},
            {"font-size", "13"}, {"fill", color}, {"text-anchor", "middle"}}, lmdraw::tick_label(val, dec)));
      }
    }
  } else {
    const int x_decimals = lmdraw::tick_decimals((xmax - xmin) / double(intervals));
    for (int i = 0; i <= intervals; ++i) {
      const double t = double(i) / double(intervals);
      const double px = x + t * w;
      add(add_node(s, "line", {{"x1", num(px)}, {"y1", num(y + h)}, {"x2", num(px)}, {"y2", num(y + h + 5)},
          {"stroke", color}, {"stroke-width", "1"}}));
      add(add_node(s, "text", {{"x", num(px)}, {"y", num(y + h + 20)}, {"font-family", "Noto Sans"},
          {"font-size", "13"}, {"fill", color}, {"text-anchor", "middle"}}, lmdraw::tick_label(xmin + t * (xmax - xmin), x_decimals)));
    }
  }
  if (log_y) {
    const double lmin = std::log10(ymin), lmax = std::log10(ymax), lspan = lmax - lmin;
    const int dmin = static_cast<int>(std::ceil(lmin - 1e-9));
    const int dmax = static_cast<int>(std::floor(lmax + 1e-9));
    if (dmax >= dmin) {
      const int step = (dmax - dmin > 10) ? 2 : 1;
      for (int d = dmin; d <= dmax; d += step) {
        const double t = std::clamp((double(d) - lmin) / lspan, 0.0, 1.0);
        const double py = y + h - t * h;
        add(add_node(s, "line", {{"x1", num(x - 5)}, {"y1", num(py)}, {"x2", num(x)}, {"y2", num(py)},
            {"stroke", color}, {"stroke-width", "1"}}));
        add(add_node(s, "text", {{"x", num(x - 9)}, {"y", num(py + 4)}, {"font-family", "Noto Sans"},
            {"font-size", "13"}, {"fill", color}, {"text-anchor", "end"}}, lmdraw::log_tick_label(d)));
      }
      if (dmax - dmin <= 8) {
        for (int d = dmin - 1; d <= dmax; ++d) {
          const double base = std::pow(10.0, d);
          for (int m = 2; m <= 9; ++m) {
            const double val = m * base;
            if (val <= ymin * (1.0 + 1e-9) || val >= ymax * (1.0 - 1e-9)) continue;
            const double t = (std::log10(val) - lmin) / lspan;
            const double py = y + h - t * h;
            add(add_node(s, "line", {{"x1", num(x - 3)}, {"y1", num(py)}, {"x2", num(x)}, {"y2", num(py)},
                {"stroke", color}, {"stroke-width", "1"}}));
          }
        }
      }
    } else {
      const int dec = lmdraw::tick_decimals((ymax - ymin) / double(intervals));
      for (int i = 0; i <= intervals; ++i) {
        const double t = double(i) / double(intervals);
        const double val = ymin * std::pow(ymax / ymin, t);
        const double py = y + h - t * h;
        add(add_node(s, "line", {{"x1", num(x - 5)}, {"y1", num(py)}, {"x2", num(x)}, {"y2", num(py)},
            {"stroke", color}, {"stroke-width", "1"}}));
        add(add_node(s, "text", {{"x", num(x - 9)}, {"y", num(py + 4)}, {"font-family", "Noto Sans"},
            {"font-size", "13"}, {"fill", color}, {"text-anchor", "end"}}, lmdraw::tick_label(val, dec)));
      }
    }
  } else {
    const int y_decimals = lmdraw::tick_decimals((ymax - ymin) / double(intervals));
    for (int i = 0; i <= intervals; ++i) {
      const double t = double(i) / double(intervals);
      const double py = y + h - t * h;
      add(add_node(s, "line", {{"x1", num(x - 5)}, {"y1", num(py)}, {"x2", num(x)}, {"y2", num(py)},
          {"stroke", color}, {"stroke-width", "1"}}));
      add(add_node(s, "text", {{"x", num(x - 9)}, {"y", num(py + 4)}, {"font-family", "Noto Sans"},
          {"font-size", "13"}, {"fill", color}, {"text-anchor", "end"}}, lmdraw::tick_label(ymin + t * (ymax - ymin), y_decimals)));
    }
  }
  return group;
}
Node* build_legend(Scene& s, double x, double y, const std::string& labels_str,
                   const std::string& colors_str, const std::string& text_color,
                   const std::string& background) {
  s.require_building();
  finite(x, "legend x"); finite(y, "legend y");
  if (labels_str.empty() || labels_str.size() > 4096)
    throw dslvm::Error("legend labels must contain 1..4096 bytes");
  if (colors_str.empty() || colors_str.size() > 4096)
    throw dslvm::Error("legend colors must contain 1..4096 bytes");
  validate_plain_color("fill", text_color);
  validate_plain_color("fill", background);
  std::vector<std::string> labels;
  {
    std::istringstream ls(labels_str);
    std::string line;
    while (std::getline(ls, line, '\n')) {
      if (!line.empty() && line.back() == '\r') line.pop_back();
      if (!line.empty()) labels.push_back(line);
    }
  }
  std::vector<std::string> colors;
  {
    std::string norm = colors_str;
    for (char& c : norm) if (c == ',' || c == ';' || c == '\n' || c == '\r' || c == '\t') c = ' ';
    std::istringstream cs(norm);
    std::string tok;
    while (cs >> tok) {
      validate_plain_color("stroke", tok);
      colors.push_back(tok);
    }
  }
  if (labels.empty() || labels.size() > 16 || labels.size() != colors.size())
    throw dslvm::Error("legend requires 1..16 non-empty labels and one matching color per label");
  if (s.nodes.size() > 10000 - (2 + 2 * labels.size()))
    throw dslvm::Error("legend would exceed the scene node limit");
  double max_w = 0.0;
  for (const auto& lbl : labels) {
    max_w = std::max(max_w, lmdraw::renderer().measure_text(lbl, 13.0f, false));
  }
  const double box_w = std::max(96.0, max_w + 56.0);
  const double box_h = double(labels.size()) * 22.0 + 12.0;
  finite(x + box_w, "legend right edge");
  finite(y + box_h, "legend bottom edge");
  auto* group = add_node(s, "g", {});
  auto add = [&](Node* child) {
    child->parent = group;
    group->children.push_back(child);
  };
  if (background != "none") {
    add(add_node(s, "rect", {{"x", num(x)}, {"y", num(y)}, {"width", num(box_w)}, {"height", num(box_h)},
        {"rx", "6"}, {"ry", "6"}, {"fill", background}, {"stroke", "#2c4156"}, {"stroke-width", "1"}}));
  }
  for (size_t i = 0; i < labels.size(); ++i) {
    const double ry = y + 17.0 + double(i) * 22.0;
    add(add_node(s, "line", {{"x1", num(x + 12.0)}, {"y1", num(ry)}, {"x2", num(x + 36.0)}, {"y2", num(ry)},
        {"stroke", colors[i]}, {"stroke-width", "3"}, {"stroke-linecap", "round"}}));
    add(add_node(s, "text", {{"x", num(x + 44.0)}, {"y", num(ry + 4.5)}, {"font-family", "Noto Sans"},
        {"font-size", "13"}, {"fill", text_color}}, labels[i]));
  }
  return group;
}
void register_host(dslvm::Registry& r) {
  r.context("scene", "Scene");
  r.method("Scene", "canvas", {"int", "int", "string"}, "Scene", [](void* p, Args& a) {
    auto* s = static_cast<Scene*>(p);
    validate_dims(a[0].i, a[1].i);
    validate_plain_color("fill", a[2].s);
    s->width = int(a[0].i); s->height = int(a[1].i); s->background = a[2].s;
    ++s->generation; s->loaded_svg = false; s->loaded_svg_path.clear(); s->document.reset(); s->definitions.clear();
    s->markup = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" + std::to_string(s->width) +
                "\" height=\"" + std::to_string(s->height) + "\" viewBox=\"0 0 " +
                std::to_string(s->width) + " " + std::to_string(s->height) + "\"><rect width=\"100%\" height=\"100%\" fill=\"" + esc(s->background) + "\"/>";
    return Value::object("Scene", p);
  });
  r.method("Scene", "rect", {"float", "float", "float", "float", "string"}, "Node", [](void* p, Args& a) {
    auto* s = static_cast<Scene*>(p); s->require_building();
    for (int i = 0; i < 4; ++i) finite(a[i].f, "rect coordinate");
    positive(a[2].f, "rect width"); positive(a[3].f, "rect height");
    validate_attr("fill", a[4].s);
    return Value::object("Node", add_node(*s, "rect", {{"x", num(a[0].f)}, {"y", num(a[1].f)},
        {"width", num(a[2].f)}, {"height", num(a[3].f)}, {"fill", a[4].s}}));
  });
  r.method("Scene", "circle", {"float", "float", "float", "string"}, "Node", [](void* p, Args& a) {
    auto* s = static_cast<Scene*>(p); s->require_building();
    finite(a[0].f, "circle x"); finite(a[1].f, "circle y"); positive(a[2].f, "circle radius");
    validate_attr("fill", a[3].s);
    return Value::object("Node", add_node(*s, "circle", {{"cx", num(a[0].f)}, {"cy", num(a[1].f)},
        {"r", num(a[2].f)}, {"fill", a[3].s}}));
  });
  r.method("Scene", "ellipse", {"float", "float", "float", "float", "string"}, "Node", [](void* p, Args& a) {
    auto* s = static_cast<Scene*>(p); s->require_building();
    finite(a[0].f, "ellipse x"); finite(a[1].f, "ellipse y");
    positive(a[2].f, "ellipse x radius"); positive(a[3].f, "ellipse y radius");
    validate_attr("fill", a[4].s);
    return Value::object("Node", add_node(*s, "ellipse", {{"cx", num(a[0].f)}, {"cy", num(a[1].f)},
        {"rx", num(a[2].f)}, {"ry", num(a[3].f)}, {"fill", a[4].s}}));
  });
  r.method("Scene", "polyline", {"string", "string", "float"}, "Node", [](void* p, Args& a) {
    auto* s = static_cast<Scene*>(p); s->require_building();
    if (a[0].s.empty() || a[0].s.size() > 16384 || a[0].s.find_first_not_of("0123456789.,+ -eE") != std::string::npos)
      throw dslvm::Error("polyline points must be a bounded list of numeric x,y pairs");
    positive(a[2].f, "polyline stroke width"); validate_attr("stroke", a[1].s);
    return Value::object("Node", add_node(*s, "polyline", {{"points", a[0].s}, {"fill", "none"},
        {"stroke", a[1].s}, {"stroke-width", num(a[2].f)}, {"stroke-linecap", "round"}, {"stroke-linejoin", "round"}}));
  });
  r.method("Scene", "path", {"string", "string", "string", "float"}, "Node", [](void* p, Args& a) {
    auto* s = static_cast<Scene*>(p); s->require_building();
    if (a[0].s.empty() || a[0].s.size() > 16384) throw dslvm::Error("path data must contain 1..16384 bytes");
    validate_attr("d", a[0].s); validate_attr("fill", a[1].s); validate_attr("stroke", a[2].s);
    positive(a[3].f, "path stroke width");
    return Value::object("Node", add_node(*s, "path", {{"d", a[0].s}, {"fill", a[1].s},
        {"stroke", a[2].s}, {"stroke-width", num(a[3].f)}}));
  });
  r.method("Scene", "box", {"float", "float", "float", "float", "string", "string", "string", "float", "float"}, "Shape", [](void* p, Args& a) {
    auto* s = static_cast<Scene*>(p);
    return Value::object("Shape", build_box(*s, a[0].f, a[1].f, a[2].f, a[3].f,
        a[4].s, a[5].s, a[6].s, a[7].f, a[8].f));
  });
  r.method("Scene", "box", {"Shape", "string", "float", "float", "string", "string", "string", "float", "float"}, "Shape", [](void* p, Args& a) {
    auto* s = static_cast<Scene*>(p);
    const auto at = anchor_point(*live_shape(a[0].obj), a[1].s, "box anchor");
    return Value::object("Shape", build_box(*s, at.first, at.second, a[2].f, a[3].f,
        a[4].s, a[5].s, a[6].s, a[7].f, a[8].f));
  });
  r.method("Scene", "diamond", {"float", "float", "float", "float", "string", "string", "string", "float"}, "Shape", [](void* p, Args& a) {
    auto* s = static_cast<Scene*>(p);
    return Value::object("Shape", build_diamond(*s, a[0].f, a[1].f, a[2].f, a[3].f,
        a[4].s, a[5].s, a[6].s, a[7].f));
  });
  r.method("Scene", "cylinder", {"float", "float", "float", "float", "string", "string", "string", "float"}, "Shape", [](void* p, Args& a) {
    auto* s = static_cast<Scene*>(p);
    return Value::object("Shape", build_cylinder(*s, a[0].f, a[1].f, a[2].f, a[3].f,
        a[4].s, a[5].s, a[6].s, a[7].f));
  });
  r.method("Scene", "note", {"float", "float", "float", "float", "string", "string", "string", "float"}, "Shape", [](void* p, Args& a) {
    auto* s = static_cast<Scene*>(p);
    return Value::object("Shape", build_note(*s, a[0].f, a[1].f, a[2].f, a[3].f,
        a[4].s, a[5].s, a[6].s, a[7].f));
  });
  r.method("Scene", "cluster", {"float", "float", "float", "float", "string", "string", "string", "float", "float"}, "Shape", [](void* p, Args& a) {
    auto* s = static_cast<Scene*>(p);
    return Value::object("Shape", build_cluster(*s, a[0].f, a[1].f, a[2].f, a[3].f,
        a[4].s, a[5].s, a[6].s, a[7].f, a[8].f));
  });
  r.method("Scene", "connector", {"float", "float", "float", "float", "string", "string", "string", "string", "string", "float", "string", "float", "float", "float"}, "Group", [](void* p, Args& a) {
    auto* s = static_cast<Scene*>(p);
    return Value::object("Group", build_connector(*s, ConnSpec{a[0].f, a[1].f, a[2].f, a[3].f,
        a[4].s, a[5].s, a[6].s, a[7].s, a[8].s, a[9].f, a[10].s, a[11].f, a[12].f, a[13].f}));
  });
  r.method("Scene", "connector", {"Shape", "string", "Shape", "string", "string", "string", "string", "string", "string", "float", "string", "float", "float", "float"}, "Group", [](void* p, Args& a) {
    auto* s = static_cast<Scene*>(p);
    const auto from = anchor_point(*live_shape(a[0].obj), a[1].s, "connector start anchor");
    const auto to = anchor_point(*live_shape(a[2].obj), a[3].s, "connector end anchor");
    return Value::object("Group", build_connector(*s, ConnSpec{from.first, from.second,
        to.first, to.second, a[4].s, a[5].s, a[6].s, a[7].s, a[8].s, a[9].f,
        a[10].s, a[11].f, a[12].f, a[13].f}));
  });
  r.method("Scene", "connector", {"float", "float", "Shape", "string", "string", "string", "string", "string", "string", "float", "string", "float", "float", "float"}, "Group", [](void* p, Args& a) {
    auto* s = static_cast<Scene*>(p);
    const auto to = anchor_point(*live_shape(a[2].obj), a[3].s, "connector end anchor");
    return Value::object("Group", build_connector(*s, ConnSpec{a[0].f, a[1].f,
        to.first, to.second, a[4].s, a[5].s, a[6].s, a[7].s, a[8].s, a[9].f,
        a[10].s, a[11].f, a[12].f, a[13].f}));
  });
  r.method("Scene", "connector", {"Shape", "string", "float", "float", "string", "string", "string", "string", "string", "float", "string", "float", "float", "float"}, "Group", [](void* p, Args& a) {
    auto* s = static_cast<Scene*>(p);
    const auto from = anchor_point(*live_shape(a[0].obj), a[1].s, "connector start anchor");
    return Value::object("Group", build_connector(*s, ConnSpec{from.first, from.second,
        a[2].f, a[3].f, a[4].s, a[5].s, a[6].s, a[7].s, a[8].s, a[9].f,
        a[10].s, a[11].f, a[12].f, a[13].f}));
  });
  r.method("Scene", "grid", {"float", "float", "float", "float", "int", "int", "float", "float"}, "Grid", [](void* p, Args& a) {
    auto* s = static_cast<Scene*>(p);
    return Value::object("Grid", build_grid(*s, a[0].f, a[1].f, a[2].f, a[3].f,
        a[4].i, a[5].i, a[6].f, a[7].f));
  });
  r.method("Scene", "lifeline", {"float", "float", "float", "string"}, "Group", [](void* p, Args& a) {
    auto* s = static_cast<Scene*>(p);
    return Value::object("Group", build_lifeline(*s, a[0].f, a[1].f, a[2].f, a[3].s));
  });
  r.method("Scene", "message", {"float", "float", "float", "float", "string", "bool", "string"}, "Group", [](void* p, Args& a) {
    auto* s = static_cast<Scene*>(p);
    return Value::object("Group", build_message(*s, a[0].f, a[1].f, a[2].f, a[3].f,
        a[4].s, a[5].b, a[6].s));
  });
  r.method("Scene", "activation", {"float", "float", "float", "string", "string"}, "Group", [](void* p, Args& a) {
    auto* s = static_cast<Scene*>(p);
    return Value::object("Group", build_activation(*s, a[0].f, a[1].f, a[2].f, a[3].s, a[4].s));
  });
  r.method("Scene", "self_message", {"float", "float", "string", "string"}, "Group", [](void* p, Args& a) {
    auto* s = static_cast<Scene*>(p);
    return Value::object("Group", build_self_message(*s, a[0].f, a[1].f, a[2].s, a[3].s));
  });
  r.method("Shape", "set_attr", {"string", "string"}, "Shape", [](void* p, Args& a) {
    auto* d = live_shape(p);
    validate_attr(a[0].s, a[1].s);
    for (Node* child : d->group->children)
      if (child->tag != "text") child->attrs[a[0].s] = a[1].s;
    return Value::object("Shape", p);
  });
  r.method("Shape", "cx", {}, "float", [](void* p, Args&) {
    auto* d = live_shape(p);
    return Value::real(d->x + d->w / 2.0);
  });
  r.method("Shape", "cy", {}, "float", [](void* p, Args&) {
    auto* d = live_shape(p);
    return Value::real(d->y + d->h / 2.0);
  });
  r.method("Grid", "cell", {"int", "int"}, "Shape", [](void* p, Args& a) {
    auto* g = live_grid(p);
    if (a[0].i < 0 || a[0].i >= g->cols) throw dslvm::Error("grid cell column out of range");
    if (a[1].i < 0 || a[1].i >= g->rows) throw dslvm::Error("grid cell row out of range");
    return Value::object("Shape", g->cells[size_t(a[1].i) * size_t(g->cols) + size_t(a[0].i)]);
  });
  r.method("Scene", "group", {}, "Group", [](void* p, Args&) {
    auto* s = static_cast<Scene*>(p); s->require_building();
    return Value::object("Group", add_node(*s, "g", {}));
  });
  r.method("Group", "add", {"Node"}, "Group", [](void* p, Args& a) {
    return group_add(p, a);
  });
  r.method("Group", "add", {"Group"}, "Group", [](void* p, Args& a) { return group_add(p, a); });
  r.method("Group", "set_attr", {"string", "string"}, "Group", [](void* p, Args& a) { return set_node_attr(p, a, "Group"); });
  r.method("Scene", "image", {"string", "float", "float", "float", "float"}, "Node", [](void* p, Args& a) {
    auto* s = static_cast<Scene*>(p); s->require_building();
    validate_png_data_uri(a[0].s);
    for (int i = 1; i < 5; ++i) finite(a[i].f, "image coordinate");
    positive(a[3].f, "image width"); positive(a[4].f, "image height");
    return Value::object("Node", add_node(*s, "image", {{"href", a[0].s}, {"x", num(a[1].f)},
        {"y", num(a[2].f)}, {"width", num(a[3].f)}, {"height", num(a[4].f)}}));
  });
  r.method("Scene", "linear_gradient", {"string", "float", "float", "float", "float"}, "Gradient", [](void* p, Args& a) {
    auto* s = static_cast<Scene*>(p); s->require_building();
    static const std::regex id(R"(^[A-Za-z_][A-Za-z0-9_.:-]{0,127}$)");
    if (!std::regex_match(a[0].s, id)) throw dslvm::Error("gradient id must be a simple 1..128 character identifier");
    for (int i = 1; i < 5; ++i) finite(a[i].f, "gradient coordinate");
    for (int i = 1; i < 5; ++i)
      if (a[i].f < 0 || a[i].f > 1) throw dslvm::Error("linear gradient coordinates must be fractions from 0 to 1");
    if (s->gradients.size() >= 1024) throw dslvm::Error("a host run may create at most 1024 gradients");
    for (const auto& g : s->gradients) if (g->id == a[0].s) throw dslvm::Error("gradient ids must be unique in a scene");
    auto g = std::make_unique<Scene::GradientData>(); g->id=a[0].s; g->x1=float(a[1].f); g->y1=float(a[2].f); g->x2=float(a[3].f); g->y2=float(a[4].f);
    g->owner = s; g->generation = s->generation;
    auto* raw=g.get(); s->gradients.push_back(std::move(g));
    return Value::object("Gradient", raw);
  });
  r.method("Gradient", "stop", {"float", "string"}, "Gradient", [](void* p, Args& a) {
    auto* g = static_cast<Scene::GradientData*>(p); finite(a[0].f, "gradient stop offset");
    if (!g || !g->owner || g->generation != g->owner->generation)
      throw dslvm::Error("gradient handle is stale; create it after setting the canvas");
    if (g->stops.size() >= 128) throw dslvm::Error("a gradient may contain at most 128 stops");
    if (a[0].f < 0 || a[0].f > 1) throw dslvm::Error("gradient stop offset must be from 0 to 1");
    validate_plain_color("fill", a[1].s);
    g->stops.emplace_back(float(a[0].f), a[1].s);
    return Value::object("Gradient", p);
  });
  r.method("Scene", "line", {"float", "float", "float", "float", "string", "float"}, "Node", [](void* p, Args& a) {
    auto* s = static_cast<Scene*>(p); s->require_building();
    for (int i = 0; i < 4; ++i) finite(a[i].f, "line coordinate");
    positive(a[5].f, "line width");
    validate_attr("stroke", a[4].s);
    return Value::object("Node", add_node(*s, "line", {{"x1", num(a[0].f)}, {"y1", num(a[1].f)},
        {"x2", num(a[2].f)}, {"y2", num(a[3].f)}, {"stroke", a[4].s},
        {"stroke-width", num(a[5].f)}, {"stroke-linecap", "round"}}));
  });
  r.method("Scene", "arrow", {"float", "float", "float", "float", "string", "float"}, "Node", [](void* p, Args& a) {
    auto* s = static_cast<Scene*>(p); s->require_building();
    for (int i = 0; i < 4; ++i) finite(a[i].f, "arrow coordinate");
    positive(a[5].f, "arrow width");
    validate_attr("stroke", a[4].s);
    return Value::object("Node", add_node(*s, "line", {{"x1", num(a[0].f)}, {"y1", num(a[1].f)},
        {"x2", num(a[2].f)}, {"y2", num(a[3].f)}, {"stroke", a[4].s},
        {"stroke-width", num(a[5].f)}, {"stroke-linecap", "round"}, {"marker-end", "arrow"}}));
  });
  r.method("Scene", "axes", {"float", "float", "float", "float", "float", "float", "float", "float", "int", "string"}, "Group", [](void* p, Args& a) {
    auto* s = static_cast<Scene*>(p); s->require_building();
    for (int i = 0; i < 8; ++i) finite(a[i].f, "axes coordinate");
    positive(a[2].f, "axes width"); positive(a[3].f, "axes height");
    finite(a[0].f + a[2].f, "axes right edge"); finite(a[1].f + a[3].f, "axes bottom edge");
    if (a[5].f <= a[4].f || a[7].f <= a[6].f) throw dslvm::Error("axes ranges must be increasing");
    if (a[8].i < 2 || a[8].i > 20) throw dslvm::Error("axes tick count must be from 2 to 20");
    finite(a[5].f - a[4].f, "axes x range"); finite(a[7].f - a[6].f, "axes y range");
    validate_attr("stroke", a[9].s);
    const size_t required = 6 + 4 * static_cast<size_t>(a[8].i);
    if (s->nodes.size() > 10000 - required) throw dslvm::Error("axes would exceed the scene node limit");
    auto* group = add_node(*s, "g", {});
    auto add = [&](Node* child) {
      child->parent = group;
      group->children.push_back(child);
    };
    const double x = a[0].f, y = a[1].f, w = a[2].f, h = a[3].f;
    const int x_decimals = lmdraw::tick_decimals((a[5].f - a[4].f) / double(a[8].i));
    const int y_decimals = lmdraw::tick_decimals((a[7].f - a[6].f) / double(a[8].i));
    add(add_node(*s, "line", {{"x1", num(x)}, {"y1", num(y + h)}, {"x2", num(x + w)}, {"y2", num(y + h)},
        {"stroke", a[9].s}, {"stroke-width", "1"}, {"stroke-linecap", "round"}}));
    add(add_node(*s, "line", {{"x1", num(x)}, {"y1", num(y)}, {"x2", num(x)}, {"y2", num(y + h)},
        {"stroke", a[9].s}, {"stroke-width", "1"}, {"stroke-linecap", "round"}}));
    for (int i = 0; i <= a[8].i; ++i) {
      const double t = double(i) / double(a[8].i);
      const double px = x + t * w, py = y + h - t * h;
      add(add_node(*s, "line", {{"x1", num(px)}, {"y1", num(y + h)}, {"x2", num(px)}, {"y2", num(y + h + 5)},
          {"stroke", a[9].s}, {"stroke-width", "1"}}));
      add(add_node(*s, "line", {{"x1", num(x - 5)}, {"y1", num(py)}, {"x2", num(x)}, {"y2", num(py)},
          {"stroke", a[9].s}, {"stroke-width", "1"}}));
      auto* xlabel = add_node(*s, "text", {{"x", num(px)}, {"y", num(y + h + 20)}, {"font-family", "Noto Sans"},
          {"font-size", "13"}, {"fill", a[9].s}, {"text-anchor", "middle"}}, lmdraw::tick_label(a[4].f + t * (a[5].f - a[4].f), x_decimals));
      add(xlabel);
      auto* ylabel = add_node(*s, "text", {{"x", num(x - 9)}, {"y", num(py + 4)}, {"font-family", "Noto Sans"},
          {"font-size", "13"}, {"fill", a[9].s}, {"text-anchor", "end"}}, lmdraw::tick_label(a[6].f + t * (a[7].f - a[6].f), y_decimals));
      add(ylabel);
    }
    return Value::object("Group", group);
  });
  r.method("Scene", "log_axes", {"float", "float", "float", "float", "float", "float", "float", "float", "int", "string"}, "Group", [](void* p, Args& a) {
    auto* s = static_cast<Scene*>(p);
    return Value::object("Group", build_log_axes(*s, a[0].f, a[1].f, a[2].f, a[3].f, a[4].f, a[5].f, a[6].f, a[7].f, true, false, a[8].i, a[9].s));
  });
  r.method("Scene", "log_axes", {"float", "float", "float", "float", "float", "float", "float", "float", "bool", "bool", "int", "string"}, "Group", [](void* p, Args& a) {
    auto* s = static_cast<Scene*>(p);
    return Value::object("Group", build_log_axes(*s, a[0].f, a[1].f, a[2].f, a[3].f, a[4].f, a[5].f, a[6].f, a[7].f, a[8].b, a[9].b, a[10].i, a[11].s));
  });
  r.method("Scene", "legend", {"float", "float", "string", "string", "string"}, "Group", [](void* p, Args& a) {
    auto* s = static_cast<Scene*>(p);
    return Value::object("Group", build_legend(*s, a[0].f, a[1].f, a[2].s, a[3].s, a[4].s, "#16222e"));
  });
  r.method("Scene", "legend", {"float", "float", "string", "string", "string", "string"}, "Group", [](void* p, Args& a) {
    auto* s = static_cast<Scene*>(p);
    return Value::object("Group", build_legend(*s, a[0].f, a[1].f, a[2].s, a[3].s, a[4].s, a[5].s));
  });
  r.method("Scene", "text", {"float", "float", "float", "string", "string"}, "Node", [](void* p, Args& a) {
    auto* s = static_cast<Scene*>(p); s->require_building();
    finite(a[0].f, "text x"); finite(a[1].f, "text y"); positive(a[2].f, "text size");
    if (a[3].s.size() > 4096) throw dslvm::Error("text exceeds 4096 bytes");
    validate_attr("fill", a[4].s);
    return Value::object("Node", add_node(*s, "text", {{"x", num(a[0].f)}, {"y", num(a[1].f)},
        {"font-family", "Noto Sans"}, {"font-size", num(a[2].f)}, {"fill", a[4].s}}, a[3].s));
  });
  r.method("Scene", "text", {"float", "float", "float", "string", "string", "float"}, "Node", [](void* p, Args& a) {
    auto* s = static_cast<Scene*>(p); s->require_building();
    finite(a[0].f, "text x"); finite(a[1].f, "text y"); positive(a[2].f, "text size"); positive(a[5].f, "text wrap width");
    if (a[3].s.size() > 4096) throw dslvm::Error("text exceeds 4096 bytes");
    validate_attr("fill", a[4].s);
    const std::string wrapped = wrap_text(a[3].s, a[5].f, static_cast<float>(a[2].f));
    return Value::object("Node", add_node(*s, "text", {{"x", num(a[0].f)}, {"y", num(a[1].f)},
        {"font-family", "Noto Sans"}, {"font-size", num(a[2].f)}, {"fill", a[4].s}}, wrapped));
  });
  r.method("Scene", "load_font", {"string", "string", "bool"}, "void", [](void*, Args& a) {
    if (a[0].s.empty() || a[0].s.size() > 128) throw dslvm::Error("font family must be 1..128 bytes");
    const auto path = safe_font_path(a[1].s);
    if (!lmdraw::renderer().load_font_file(a[0].s, a[2].b, path))
      throw dslvm::Error("font could not be loaded; provide a valid TrueType font file");
    return Value::none();
  });
  r.method("Scene", "measure_text", {"string", "float", "bool"}, "float", [](void*, Args& a) {
    positive(a[1].f, "font size");
    if (a[0].s.size() > 4096) throw dslvm::Error("text exceeds 4096 bytes");
    return Value::real(lmdraw::renderer().measure_text(a[0].s, static_cast<float>(a[1].f), a[2].b));
  });
  r.method("Scene", "load_svg", {"string", "string"}, "Node", [](void* p, Args& a) {
    auto* s = static_cast<Scene*>(p);
    if (a[1].s.empty() || a[1].s.size() > 128) throw dslvm::Error("SVG node id must be 1..128 bytes");
    auto path = safe_svg_path(a[0].s);
    if (!s->loaded_svg || s->loaded_svg_path != path.string()) {
      // A different SVG starts a new scene; repeated IDs from the same file
      // remain live handles into one parsed document.
      ++s->generation;
      s->loaded_svg = false;
      s->document.reset();
      std::ifstream f(path, std::ios::binary);
      std::string bytes((std::istreambuf_iterator<char>(f)), {});
      if (bytes.size() > 2 * 1024 * 1024) throw dslvm::Error("SVG exceeds 2 MiB");
      validate_svg_subset(bytes);
      try { s->document = lmdraw::renderer().load_svg(bytes); }
      catch (const std::exception& e) { throw dslvm::Error(e.what()); }
      double w = s->document->width(), h = s->document->height();
      if (!std::isfinite(w) || !std::isfinite(h) || w < 1 || h < 1 || w > 4096 || h > 4096 || double(w) * h > 8'000'000)
        throw dslvm::Error("SVG intrinsic size exceeds canvas limits");
      s->width = int(std::lround(w)); s->height = int(std::lround(h)); s->background = "#152438";
      s->loaded_svg_path = path.string();
      s->loaded_svg = true;
    }
    if (!lmdraw::renderer().has_element(*s->document, a[1].s))
      throw dslvm::Error("SVG does not contain id " + a[1].s);
    auto node = std::make_unique<Node>(); node->svg_id = a[1].s; node->from_svg = true; node->owner = s; node->generation = s->generation;
    Node* raw = node.get(); s->nodes.push_back(std::move(node));
    return Value::object("Node", raw);
  });
  r.method("Node", "set_attr", {"string", "string"}, "Node", [](void* p, Args& a) {
    return set_node_attr(p, a, "Node");
  });
  r.method("Scene", "render_png", {"string", "int", "int", "int"}, "void", [](void* p, Args& a) {
    auto* s = static_cast<Scene*>(p); s->require_canvas();
    if (a[0].s.size() > 1024) throw dslvm::Error("output path may not exceed 1024 bytes");
    validate_dims(a[1].i, a[2].i);
    if (a[3].i < 1 || a[3].i > 4 || uint64_t(a[1].i) * uint64_t(a[2].i) * uint64_t(a[3].i) * uint64_t(a[3].i) > 8'000'000)
      throw dslvm::Error("render scale must be 1..4 and keep total output at or below 8000000 pixels");
    const int rw = int(a[1].i * a[3].i), rh = int(a[2].i * a[3].i);
    fs::path out = safe_png_path(a[0].s);
    AtomicOutput atomic(out);
    out = atomic.path();
    const auto markers = collect_markers(*s);
    const std::string marker_defs = serialize_markers(markers);
    if (s->loaded_svg) {
      std::string overlay;
      const std::string nodes = serialize_nodes(*s, markers);
      const std::string gradients = serialize_gradients(*s);
      if (!nodes.empty() || !gradients.empty()) {
        overlay = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" + std::to_string(rw) +
            "\" height=\"" + std::to_string(rh) + "\" viewBox=\"0 0 " + std::to_string(s->width) +
            " " + std::to_string(s->height) + "\">" + marker_defs + gradients + nodes + "</svg>";
      }
      try { lmdraw::renderer().render_imported(*s->document, overlay, rw, rh, out); }
      catch (const std::exception& e) { throw dslvm::Error(e.what()); }
    } else {
      std::string svg = s->markup;
      svg += marker_defs;
      svg += serialize_gradients(*s);
      svg += serialize_nodes(*s, markers);
      svg += "</svg>";
      try {
        lmdraw::renderer().render_svg(svg, rw, rh, 0x00000000, out);
      } catch (const std::exception& e) {
        throw dslvm::Error(e.what());
      }
    }
    atomic.commit();
    return Value::none();
  });
}

void signatures(const dslvm::Registry& r) {
  for (const auto& x : r.signatures()) std::cout << x << "\n";
}
void schema_summary(const dslvm::Registry& r) {
  std::map<std::string, std::vector<std::string>> methods;
  for (const auto& signature : r.signatures()) {
    if (signature.rfind("context ", 0) == 0) {
      std::cout << "Context\n" << signature << "\n";
    } else if (signature.rfind("method ", 0) == 0) {
      const auto begin = signature.find(' ') + 1;
      const auto dot = signature.find('.', begin);
      if (dot != std::string::npos) methods[signature.substr(begin, dot - begin)].push_back(signature.substr(begin));
    }
  }
  for (const auto& [type, members] : methods) {
    std::cout << "\n" << type << "\n";
    for (const auto& member : members) std::cout << member << "\n";
  }
}
}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
  _setmode(_fileno(stdout), _O_BINARY);
#endif
  if (argc == 2 && std::string(argv[1]) == "--version") {
    std::cout << "lmdraw " << LMDRAW_VERSION << "\n";
    return 0;
  }
  lmdraw::renderer().add_font("Noto Sans", false, lmdraw::embedded::noto_regular,
                              lmdraw::embedded::noto_regular_size);
  lmdraw::renderer().add_font("Noto Sans", true, lmdraw::embedded::noto_bold,
                              lmdraw::embedded::noto_bold_size);
  lmdraw::renderer().add_fallback_font(lmdraw::embedded::noto_math,
                                       lmdraw::embedded::noto_math_size);
  Scene scene;
  dslvm::Registry reg;
  register_host(reg);
  const std::string schema_flag = argc > 1 ? argv[1] : "";
  if (argc == 2 && (schema_flag == "--schema" || schema_flag == "--dsl-schema")) {
    schema_summary(reg);
    return 0;
  }
  if (argc == 3 && (schema_flag == "--schema" || schema_flag == "--dsl-schema") &&
      std::string(argv[2]) == "--json") {
    std::cout.write(reinterpret_cast<const char*>(lmdraw::embedded::schema), lmdraw::embedded::schema_size);
    return 0;
  }
  if (argc == 2 && std::string(argv[1]) == "--dsl-signatures") { signatures(reg); return 0; }
  if (argc < 3 || std::string(argv[1]) != "--dsl") {
    std::cerr << "usage: lmdraw --schema [--json] | --dsl-schema [--json] | --dsl FILE [FILE ...] | --dsl-signatures\n";
    return 2;
  }
  try {
    std::string schema(reinterpret_cast<const char*>(lmdraw::embedded::schema), lmdraw::embedded::schema_size);
    std::vector<dslvm::Program> programs;
    programs.reserve(static_cast<size_t>(argc - 2));
    for (int i = 2; i < argc; ++i) {
      std::error_code ec;
      const auto size = fs::file_size(argv[i], ec);
      if (ec || size > 16 * 1024 * 1024) throw dslvm::Error("bytecode input must be a readable file no larger than 16 MiB");
      programs.push_back(dslvm::Program::load(dslvm::read_file(argv[i]), schema, reg));
    }
    dslvm::Context context; context.set("scene", &scene);
    for (auto& program : programs) {
      program.run(context, reg);
      std::cout << "{\"ok\": true, \"result\": null}\n";
    }
    return 0;
  } catch (const dslvm::Error& e) {
    std::cerr << e.what() << "\n"; return 3;
  } catch (const std::exception& e) {
    std::cerr << e.what() << "\n"; return 4;
  }
}
