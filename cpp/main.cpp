#include "dslvm.hpp"
#include "resources.hpp"
#include "renderer.hpp"
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
fs::path safe_png_path(const std::string& raw) {
  fs::path p(raw);
  if (p.is_absolute() || p.has_root_name() || p.extension() != ".png" || raw.rfind("build/rendered/", 0) != 0)
    throw dslvm::Error("PNG output must be a .png file under build/rendered/");
  for (const auto& part : p)
    if (part == ".." || part == ".") throw dslvm::Error("PNG output traversal is refused");
  std::error_code ec;
  const auto build_status = fs::symlink_status("build", ec);
  if (ec || fs::is_symlink(build_status) || !fs::is_directory(build_status))
    throw dslvm::Error("build/ must be a real output directory");
  auto rendered_status = fs::symlink_status("build/rendered", ec);
  if (!ec && rendered_status.type() == fs::file_type::not_found) {
    fs::create_directory("build/rendered", ec);
    if (ec) throw dslvm::Error("could not create build/rendered/");
    rendered_status = fs::symlink_status("build/rendered", ec);
  }
  if (ec || fs::is_symlink(rendered_status) || !fs::is_directory(rendered_status))
    throw dslvm::Error("build/rendered/ may not be a symbolic link");
  auto root = fs::weakly_canonical("build/rendered");
  auto parent = fs::weakly_canonical(p.parent_path());
  auto full = fs::weakly_canonical(p);
  if (parent != root || full.parent_path() != root)
    throw dslvm::Error("PNG output must remain directly under build/rendered/");
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
    throw dslvm::Error("could not create a temporary output file under build/rendered/");
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
    if ((name == "width" || name == "height" || name == "r" || name == "rx" || name == "ry") && n <= 0)
      throw dslvm::Error(name + " must be positive");
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
  std::vector<const Node*> nested;
  for (const auto& parent : scene.nodes)
    for (const auto* child : parent->children) nested.push_back(child);
  std::string out;
  for (const auto& node : scene.nodes)
    if (node->generation == scene.generation && std::find(nested.begin(), nested.end(), node.get()) == nested.end() && !node->from_svg)
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
          {"font-size", "13"}, {"fill", a[9].s}, {"text-anchor", "middle"}}, num(a[4].f + t * (a[5].f - a[4].f)));
      add(xlabel);
      auto* ylabel = add_node(*s, "text", {{"x", num(x - 9)}, {"y", num(py + 4)}, {"font-family", "Noto Sans"},
          {"font-size", "13"}, {"fill", a[9].s}, {"text-anchor", "end"}}, num(a[6].f + t * (a[7].f - a[6].f)));
      add(ylabel);
    }
    return Value::object("Group", group);
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
