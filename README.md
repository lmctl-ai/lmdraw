# lmdraw

## Status

Experimental. The CLI and schema are evolving; it reads bytecode v5 and schema v2.

lmdraw is a native, headless host for retained 2D scenes written in lmscript. Scripts create and update nodes, import a safe static SVG subset, render PNGs, and use ordinary loops to produce numbered frames. The schema is embedded in the executable.

## Build

Requirements: C11/C++17 toolchains, CMake 3.24+, Ninja, Python 3 for embedding schema/font resources at build time, and Node.js 24.15+ with the published lmscript compiler for examples and tests.

```sh
scripts/sync-sdk.sh --verify
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 3
```

The release binary statically links the project libraries and, on Linux and Windows, the C/C++ runtimes. macOS uses system libraries, as macOS does not support static linking of its system runtime. The embedded Noto Sans Regular and Bold fonts and schema are compiled into the binary.

## One minute example

```sh
lmscript examples/title-card.lms build/title-card.dslb
build/lmdraw --dsl build/title-card.dslb
```

The PNG appears at build/rendered/title-card.png. See docs/LLM_GUIDE.md for the full API, SVG subset, safety limits, frame recipes, and errors. Public documentation is at https://lmctl.com/tools/lmdraw/.

## Examples

Under examples/, each .lms file compiles to bytecode and renders PNGs; palette-limited previews live in examples/renders/ (regenerate with scripts/render-previews.sh).

- title-card.lms: text, rectangles, and a gradient on a canvas.
- node-tour.lms: the node constructors on one canvas.
- render-assets.lms: loading the SVG subset and fonts from assets/.
- animated-ladder.lms: 24 numbered frames driven by set_attr in a loop.
- flow-pipeline.lms: a four-stage pipeline diagram with named arrow markers and a cubic feedback edge.
- decision-flowchart.lms: stadium/process/decision flowchart with named arrow and diamond markers and wrapped labels.
- tree-org.lms: a three-level layered tree with dot-marked branches.
- table-metrics.lms: a zebra-striped metrics table with marked arrows.
- chart-bars-lines.lms: bar and line series drawn from data arrays, with marker dots at data points.
- heatmap-grid.lms: a color-bucketed cell grid with a measured, wrapped legend label.
- geometry-ellipse.lms: circle with tangent, ellipse with foci, and dashed distance guides.
- tween-morph.lms: control-point morph frames plus a stroke-dashoffset draw-on sequence.

Scene.text has an optional measured wrap width and converts newline characters to tspans. Constructed line, path, and polyline nodes accept SVG-named marker-start, marker-mid, marker-end, stroke-dasharray, and stroke-dashoffset attributes. See docs/LLM_GUIDE.md for accepted values and limits.

## CLI

- `lmdraw --version` prints the release version from `project(... VERSION ...)` in CMakeLists.txt. Bump that project version before creating a matching `vX.Y.Z` release tag.
- lmdraw --schema prints a compact registry-derived summary.
- lmdraw --schema --json prints the exact embedded schema bytes.
- lmdraw --dsl-schema [--json] is the matching compiler protocol alias for the summary and exact schema bytes.
- lmdraw --dsl-signatures prints canonical registry signatures.
- lmdraw --dsl A.dslb [B.dslb ...] validates every file before running them in order.

Exit codes are 0 for success, 2 for invalid command-line arguments, 3 for a DSL host/load error, and 4 for another application failure. PNG outputs are restricted to direct children of build/rendered/ and are atomically replaced.

## Test

```sh
./test.sh
```

The test script compiles the examples, checks compiler/host signature parity, renders six SVG fixtures and 24 deterministic frames, checks refusal cases, and audits static linking on Linux or macOS.

## License

lmdraw-authored code is licensed under MIT; see LICENSE. Third-party component licenses and runtime notices are in THIRD_PARTY_NOTICES and licenses/. Each release also includes a source bundle and checksum files.
