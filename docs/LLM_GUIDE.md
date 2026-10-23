# lmdraw script guide

lmdraw runs lmscript bytecode. Compile each script against schema/lmdraw.dsl.json (or the identical bytes printed by lmdraw --schema --json), then run the bytecode with lmdraw --dsl. The executable embeds that schema and the Noto Sans Regular and Bold fonts.

## Coordinate system

Coordinates and sizes are floating-point CSS pixels. The canvas origin is at the upper left; x grows right and y grows down. Numeric coordinates must be finite and within +/-1,000,000 pixels. Text y is its baseline. render_png(path, width, height, scale) writes a PNG with dimensions width × scale by height × scale. Scale is an integer from 1 to 4. Width and height may each be at most 4096, the base canvas at most 8,000,000 pixels, and scaled output at most 8,000,000 pixels.

## One minute script

```lmscript
p = _dsl_ schema="../schema/lmdraw.dsl.json";
scene.canvas(640, 360, "#152438");
let card = scene.rect(36.0, 36.0, 568.0, 288.0, "#1f344b");
card.set_attr("stroke", "#6fbdb4");
card.set_attr("stroke-width", "2");
scene.text(72.0, 148.0, 38.0, "A retained scene", "#f5f8fb");
scene.render_png("build/rendered/card.png", 640, 360, 2);
end _dsl_
savedsl(p, "build/card.dslb");
```

Compile and run from the repository root:

```sh
lmscript examples/title-card.lms build/title-card.dslb
build/lmdraw --dsl build/title-card.dslb
```

## Schema reference

The compact schema is grouped by host type. The current members are:

### Scene

- canvas(int width, int height, string background) -> Scene
- rect(float x, float y, float width, float height, string fill) -> Node
- circle(float cx, float cy, float radius, string fill) -> Node
- ellipse(float cx, float cy, float rx, float ry, string fill) -> Node
- line(float x1, float y1, float x2, float y2, string stroke, float stroke_width) -> Node
- arrow(float x1, float y1, float x2, float y2, string stroke, float stroke_width) -> Node
- axes(float x, float y, float width, float height, float xmin, float xmax, float ymin, float ymax, int intervals, string color) -> Group
- polyline(string points, string stroke, float stroke_width) -> Node
- path(string d, string fill, string stroke, float stroke_width) -> Node
- text(float x, float baseline_y, float size, string content, string fill) -> Node
- text(float x, float baseline_y, float size, string content, string fill, float wrap_width) -> Node
- group() -> Group
- image(string png_data_uri, float x, float y, float width, float height) -> Node
- load_svg(string relative_path, string id) -> Node
- load_font(string family, string relative_ttf_path, bool bold) -> void
- measure_text(string content, float size, bool bold) -> float
- linear_gradient(string id, float x1, float y1, float x2, float y2) -> Gradient
- render_png(string path, int width, int height, int scale) -> void

### Node

- set_attr(string name, string value) -> Node

### Group

- add(Node child) -> Group
- add(Group child) -> Group
- set_attr(string name, string value) -> Group

### Gradient

- stop(float offset, string color) -> Gradient

Scene.arrow creates a line with a deterministic arrow marker at its endpoint.
Scene.axes creates a group containing horizontal and vertical axes, intervals
evenly spaced tick marks, and numeric labels for both ranges. The returned group
can be styled or combined with plotted nodes.

The exact registry summary and signature list are generated from the live host:

```sh
build/lmdraw --schema
build/lmdraw --dsl-signatures
lmscript --dsl-signatures schema/lmdraw.dsl.json
```

## Node attributes

Node.set_attr uses SVG-style names. Geometry values and the string-valued attributes below are validated by the host.

| Family | Names | Values |
| --- | --- | --- |
| Paint | fill, stroke | Named colors, #RGB/#RGBA/#RRGGBB/#RRGGBBAA, none, or a same-scene linear gradient reference such as url(#warm) |
| Opacity | opacity, fill-opacity, stroke-opacity | Number from 0 to 1 |
| Stroke | stroke-width | Positive pixel value |
| Stroke pattern | stroke-dasharray, stroke-dashoffset | Comma or space separated positive pixel lengths (at most 32 entries), or none; offset is a finite signed pixel length |
| Markers | marker-start, marker-mid, marker-end | arrow, dot, diamond, or none; supported on constructed line, path, and polyline nodes |
| Geometry | x, y, width, height, cx, cy, r, rx, ry, x1, y1, x2, y2 | Finite pixels; width, height, and `r` must be positive; `rx` and `ry` may be zero |
| Paths | d, points | Bounded SVG path data or numeric point pairs |
| Text | font-family, font-size, font-weight, text-anchor | Font family name, positive pixel size, numeric/string weight, start/middle/end anchor |
| Transform | transform | SVG transform syntax supported by the renderer |
| Stroke style | stroke-linecap, stroke-linejoin | SVG cap/join values |
| Image | href | Embedded data:image URI only; file and network references are refused |

A node is a retained handle and can be updated between renders. A canvas or SVG load starts a new scene generation; handles from an earlier generation produce a DSL error if reused. Groups own their children; a node or group can have only one parent, and cyclic groups are refused. Group.set_attr accepts the same presentation attributes as Node.set_attr.

## Gradients

Create a linear gradient, add at least two stops with offsets from 0 to 1, then assign it through a local fragment reference:

```lmscript
let gradient = scene.linear_gradient("warm", 0.0, 0.0, 1.0, 1.0);
gradient.stop(0.0, "#f5c16c");
gradient.stop(1.0, "#6fbdb4");
let panel = scene.rect(24.0, 24.0, 240.0, 120.0, "url(#warm)");
```

The first renderer supports linear gradients from scripts. Coordinates are fractions of the painted object's bounding box, from 0 to 1. Static linear/radial gradients in imported SVGs are passed through the renderer. Blur and drop-shadow filters are not supported by this release and are refused.

## Text and fonts

The host embeds Noto Sans Regular and Bold. Scene.text uses Noto Sans at the requested pixel size, with y at the text baseline. Set font-family, font-weight, or text-anchor after creating a text node. Scene.measure_text(text, size, bold) returns a width in pixels for layout. It measures UTF-8 text with the bundled font; it does not perform complex-script shaping or bidirectional layout. Custom TrueType files can be loaded only through an explicit assets/fonts/*.ttf path. Font files are data inputs, not fetched from the network.

Text content may include newline characters; each line becomes an SVG tspan with a 1.2em baseline step. The six-argument Scene.text overload also wraps at word boundaries to fit a positive pixel width, measuring each candidate line with the embedded Noto Sans regular face. A word wider than the wrap width stays intact on its own line. Wrapped text is limited to 256 lines.

## Markers and dashed strokes

Set marker-start, marker-mid, or marker-end to arrow, dot, diamond, or none on a constructed line, path, or polyline node. Marker size scales with stroke-width and its fill follows the node stroke. Markers use automatic path orientation. Imported SVG nodes can still update ordinary supported presentation attributes, but named markers are limited to constructed line, path, and polyline nodes.

Set stroke-dasharray to a comma or space separated list of positive pixel lengths, or none. Up to 32 lengths are accepted; an odd-length list repeats as SVG specifies. stroke-dashoffset accepts any finite signed pixel length. Updating the offset between renders enables a draw-on sequence without rebuilding path geometry:

```lmscript
let path = scene.polyline("40 80 140 30 240 80", "#f5c16c", 3.0);
path.set_attr("stroke-dasharray", "400 400");
for (i in range(0, 12)) {
  path.set_attr("stroke-dashoffset", str(400.0 - float(i) * 400.0 / 11.0));
  scene.render_png("build/rendered/draw-" + str(i) + ".png", 320, 120, 1);
}
```

## SVG input subset

load_svg parses an SVG under assets/ and returns a retained handle for the requested id. Call it more than once with the same path to keep handles for multiple IDs in one document; loading a different path starts a new scene. The IDs are addressable through Node.set_attr. SVG files must be direct children of assets/, under 2 MiB, with valid intrinsic dimensions. The parser refuses CSS class/style attributes and stylesheets, script and event handlers, DTD/entities, SMIL animation, foreignObject, filters, and external URLs or files. Only same-document fragment references such as href="#shape" are accepted. Imported data images are not accepted; use the Scene.image constructor with a PNG data URI.

| SVG element | Scene meaning |
| --- | --- |
| svg | Canvas dimensions and viewBox |
| g | Retained group with child order |
| rect, circle, ellipse, line | Matching geometry node |
| polyline, polygon, path | Path geometry; polygon closes its outline |
| text, tspan | Text and ordered text runs |
| defs, linearGradient, radialGradient, stop | Definitions referenced by local fragment URLs |
| clipPath, mask | Static clipping definitions |
| use | Same-document reference by fragment ID |
| title, desc | Document metadata, not painted |

Supported SVG attributes are xmlns, xmlns:xlink, width, height, viewBox, preserveAspectRatio, role, aria-labelledby, aria-label, id, x, y, rx, ry, cx, cy, r, x1, y1, x2, y2, points, d, fill, fill-opacity, stroke, stroke-width, stroke-linecap, stroke-linejoin, stroke-opacity, opacity, transform, font-family, font-size, font-weight, text-anchor, href, xlink:href, gradientUnits, gradientTransform, offset, stop-color, stop-opacity, clip-path, and mask. The implementation validates this allowlist; renderer support can vary for less common SVG forms, so visually verify imported drawings.

## Tweening frames

Use normal lmscript loops and math built-ins to update retained attributes before each render. No special animation bytecode or APNG container is required; the host writes deterministic numbered PNG files.

```lmscript
for (i in range(0, 24)) {
  let t = float(i) / 23.0;
  marker.set_attr("cx", str(lerp(140.0, 500.0, ease(t))));
  scene.render_png("build/rendered/frame-" + str(i) + ".png", 640, 360, 2);
}
```

To encode a numbered sequence into H.264 MP4 with ffmpeg:

```sh
ffmpeg -framerate 24 -start_number 0 -i 'build/rendered/frame-%d.png' -frames:v 24 -c:v libx264 -pix_fmt yuv420p -movflags +faststart build/animation.mp4
```

`examples/animated-ladder.lms` demonstrates this pattern with 24 frames. Numbered PNGs are also useful as individual stills or as APNG input to a separate encoder.

## File and host limits

- Scene nodes: at most 10,000 in a host run; gradients: 1,024; gradient stops: 128 each.
- SVG input: direct child of assets/ and at most 2 MiB.
- Font input: direct child of assets/fonts/, TrueType, at most 8 MiB.
- Scene image node: embedded PNG data URI, at most 1.5 MiB, with dimensions no larger than 4096 per side and 8,000,000 pixels total.
- Bytecode input: at most 16 MiB per file. Every file passed to one --dsl invocation is verified before any script runs.
- PNG outputs: direct child of build/rendered/; symbolic-link targets and path traversal are rejected. Writes use same-directory temporary files and atomic replacement. New file permissions follow the process umask.
- No network access is performed by the host. VM instruction budgets are not imposed; only host-side input and image bounds apply.

## Errors and exit codes

Exit code 2 means command-line syntax is invalid. Exit code 3 means bytecode load/verification or a DSL host call failed. Exit code 4 means another application-level error occurred. Error text identifies the DSL line where a host method fails and says which argument or feature needs correction.

Common fixes:

- Missing canvas: call scene.canvas or scene.load_svg before creating nodes.
- Canvas limit: lower dimensions or render scale so the total stays within 8,000,000 pixels.
- Unsupported SVG: inline presentation attributes and remove CSS, scripts, filters, animation, and external references.
- Output path refused: write a .png directly below build/rendered/.
- Text too wide: call scene.measure_text and reduce size or adjust x/text-anchor.
- Gradient not found: define it on the current scene, add at least two stops, then use url(#id).

## Verify your script

```sh
lmscript my-scene.lms build/my-scene.dslb
build/lmdraw --dsl build/my-scene.dslb
file build/rendered/my-scene.png
sha256sum build/rendered/my-scene.png
```
