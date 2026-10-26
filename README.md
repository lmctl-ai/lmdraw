# lmdraw

> The public README, docs, images, and examples for `lmdraw` are MIT under `LICENSE-examples`. Binary releases use `LICENSE`; implementation source and its history are maintained privately in `lmctlhq/lmdraw`.

**Scripted 2D diagrams that compute their own content: write a small lmscript program, get a crisp, repeatable PNG (or a GIF of numbered frames).**

[![CI](https://github.com/lmctl-ai/lmdraw/actions/workflows/public-ci.yml/badge.svg)](https://github.com/lmctl-ai/lmdraw/actions/workflows/public-ci.yml)
[![Latest release](https://img.shields.io/github/v/release/lmctl-ai/lmdraw?label=release)](https://github.com/lmctl-ai/lmdraw/releases)
[![Examples: MIT](https://img.shields.io/badge/examples-MIT-blue.svg)](LICENSE-examples)

Each picture is one self-contained example. Paste the command under it into a terminal and it runs from any empty folder: lmscript computes the result, lmdraw (installed automatically) draws it into `images/`.

### Physics and astronomy

| Kepler's second law (animated) | Hydrogen's visible lines | Hohmann transfer to GEO |
| --- | --- | --- |
| <img src="docs/images/kepler-second-law.gif" width="290" alt="An elliptical orbit with a constant-area wedge riding around the Sun"> | <img src="docs/images/hydrogen-spectrum.png" width="290" alt="Hydrogen energy levels with Lyman, Balmer and Paschen transitions and the visible spectrum"> | <img src="docs/images/hohmann-transfer.png" width="290" alt="To-scale LEO and GEO orbits joined by a half transfer ellipse with two burns"> |
| Kepler's equation solved per frame: the wedge's shape changes, its area never does<br>`lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmdraw/main/examples/kepler-second-law.lms` | Bohr levels and the Rydberg formula explain why a hydrogen lamp shows four colours<br>`lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmdraw/main/examples/hydrogen-spectrum.lms` | Vis-viva gives 2.40 + 1.46 km/s; a 28.5° plane change costs 0.37 km/s more<br>`lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmdraw/main/examples/hohmann-transfer.lms` |

| Carnot engine P–V loop | Thin lens: real vs virtual image | Free-body diagram on a ramp |
| --- | --- | --- |
| <img src="docs/images/carnot-cycle.png" width="290" alt="Carnot cycle on a pressure-volume diagram with the enclosed work shaded"> | <img src="docs/images/lens-ray-diagram.png" width="290" alt="Two ray diagrams for a converging lens: a real inverted image and a virtual magnified one"> | <img src="docs/images/incline-free-body.png" width="290" alt="A crate on a 30 degree ramp with weight, normal, friction and gravity components"> |
| Enclosed area equals the work, 1153 J; efficiency is 40 % = 1 − Tc/Th<br>`lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmdraw/main/examples/carnot-cycle.lms` | Three principal rays to scale: di = 15 cm (m = −0.5) and a 2.5× magnifier<br>`lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmdraw/main/examples/lens-ray-diagram.lms` | Forces drawn to scale give a = 3.21 m/s²; sliding starts at 16.7°<br>`lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmdraw/main/examples/incline-free-body.lms` |

| Special relativity & cosmic-ray muons | Damped harmonic oscillator regimes | Kleiber's ¾-power metabolic scaling |
| --- | --- | --- |
| <img src="docs/images/relativity-lorentz.png" width="290" alt="Lorentz factor gamma, relativistic momentum and length contraction curves with cosmic-ray muon survival"> | <img src="docs/images/damped-oscillator.png" width="290" alt="Underdamped, critically damped and overdamped oscillator curves with phase portrait and Scene.legend"> | <img src="docs/images/kleiber-allometry.png" width="290" alt="Seven-decade log-log plot of mammal basal metabolic rate versus body mass using Scene.log_axes"> |
| γ = 1/√(1 − β²) → ∞ as v → c; time dilation lets 23.6 % of 15 km muons reach sea level<br>`lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmdraw/main/examples/relativity-lorentz.lms` | RK4 across four damping ratios (ζ = 0.15, 0.40, 1.0, 2.0) with `Scene.legend` and phase spiral<br>`lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmdraw/main/examples/damped-oscillator.lms` | 7-decade × 6-decade `Scene.log_axes` log–log fit from shrew (15 g) to blue whale (90 t): B ∝ M⁰·⁷⁵<br>`lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmdraw/main/examples/kleiber-allometry.lms` |

### Engineering

| Pratt vs Howe truss | RC low-pass filter |
| --- | --- |
| <img src="docs/images/truss-bridge.png" width="290" alt="Two bridge trusses with members coloured by tension and compression"> | <img src="docs/images/rc-lowpass.png" width="290" alt="RC filter schematic above magnitude and phase Bode plots"> |
| Method of joints (16 equations): flipping the diagonals flips their force sign<br>`lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmdraw/main/examples/truss-bridge.lms` | Schematic to Bode plot: fc = 1591.5 Hz, −3 dB, −45°, −20 dB per decade<br>`lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmdraw/main/examples/rc-lowpass.lms` |

### Biology

| Predator–prey cycles | Mendel's 9 : 3 : 3 : 1 |
| --- | --- |
| <img src="docs/images/lotka-volterra.png" width="290" alt="Lotka-Volterra time series and closed phase-plane orbits"> | <img src="docs/images/punnett-square.png" width="290" alt="Dihybrid Punnett square with pea phenotypes and a chi-square test"> |
| RK4 Lotka–Volterra: predators lag prey; bigger swings have longer periods<br>`lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmdraw/main/examples/lotka-volterra.lms` | Dihybrid Punnett square; Mendel's 556 seeds fit with χ² = 0.47<br>`lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmdraw/main/examples/punnett-square.lms` |

### Finance

| Mortgage amortization | NPV, IRR and payback |
| --- | --- |
| <img src="docs/images/mortgage-amortization.png" width="290" alt="Yearly interest and principal bars for a 30-year mortgage with the remaining balance"> | <img src="docs/images/npv-irr-cashflow.png" width="290" alt="Cash-flow arrow diagram and the NPV versus discount rate curve"> |
| $400k at 6.5 %: $510k of interest; +$300 a month saves $150k<br>`lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmdraw/main/examples/mortgage-amortization.lms` | Cash-flow diagram for a $100k machine: NPV +$19.9k at 8 %, IRR 13.9 %<br>`lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmdraw/main/examples/npv-irr-cashflow.lms` |

### Diagrams

Flowcharts, pipelines, sequence diagrams, state machines, org charts, decision trees, swimlanes, Gantt timelines and ER sketches — all geometry is explicit lmscript, no auto-layout. `render-assets.lms` shows the imported-SVG path.

| Order approval flowchart | ETL pipeline | OAuth refresh (sequence) |
| --- | --- | --- |
| <img src="docs/images/flowchart-order-approval.png" width="290" alt="Order approval flowchart with two decision diamonds, approve and reject branches and elbow connectors"> | <img src="docs/images/etl-pipeline.png" width="290" alt="ETL pipeline with source cylinders, a transform cluster box, dashed stage edges and a bidirectional warehouse sync arrow"> | <img src="docs/images/sequence-oauth-refresh.png" width="290" alt="OAuth refresh sequence diagram with lifelines, activation bars, numbered messages and a self-message loop"> |
| Two gates — in stock, then payment — with yes/no branches<br>`lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmdraw/main/examples/flowchart-order-approval.lms` | Extract, transform, load: cylinder datastores, dashed stage edges, one bidirectional sync<br>`lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmdraw/main/examples/etl-pipeline.lms` | An expired access token rotated mid-request: introspect, refresh, 200 OK<br>`lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmdraw/main/examples/sequence-oauth-refresh.lms` |

| Four state machines | Layered architecture | Org chart on a grid |
| --- | --- | --- |
| <img src="docs/images/state-machines.png" width="290" alt="Four state machine panels: traffic light, TCP connection, door lock and thermostat with initial dots and labeled transitions"> | <img src="docs/images/architecture-layers.png" width="290" alt="Layered architecture diagram with presentation, service, data and external bands, elbow connectors and a folded-corner note"> | <img src="docs/images/org-chart-grid.png" width="290" alt="Org chart with a CEO, two VPs and six reports on an explicit grid, elbow tree connectors and one dotted-line report"> |
| Traffic light, TCP, door lock, thermostat — one notation, 2×2 panels<br>`lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmdraw/main/examples/state-machines.lms` | Four layers of components with cross-layer elbows and a database cylinder<br>`lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmdraw/main/examples/architecture-layers.lms` | Grid-placed boxes, bus connectors, and a dotted line to a second manager<br>`lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmdraw/main/examples/org-chart-grid.lms` |

| Loan decision tree | Checkout swimlane | Release Gantt |
| --- | --- | --- |
| <img src="docs/images/decision-tree.png" width="290" alt="Loan approval decision tree with five split diamonds, six outcome leaves and branch labels"> | <img src="docs/images/swimlane-process.png" width="290" alt="Checkout swimlane diagram with customer, frontend, backend and payments lanes and labeled cross-lane flows"> | <img src="docs/images/timeline-gantt.png" width="290" alt="Eight-week Gantt chart with task bars, dependency arrows, a milestone diamond, a summary bracket and a today line"> |
| Five splits, six outcomes: leaves coloured by approve/reject/neutral<br>`lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmdraw/main/examples/decision-tree.lms` | Who does what: four lanes, decisions in place, dashed retry edge<br>`lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmdraw/main/examples/swimlane-process.lms` | Dependencies between bar ends, a launch milestone and a today line<br>`lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmdraw/main/examples/timeline-gantt.lms` |

| Entity relationship sketch | Flowchart with wrapped labels | Imported SVG |
| --- | --- | --- |
| <img src="docs/images/entity-relationship.png" width="290" alt="ER sketch with five titled entities, relationship diamonds, 1:N cardinality labels and a referrer self-loop"> | <img src="docs/images/decision-flowchart.png" width="290" alt="Decision flowchart with elbow connectors and arrow markers"> | <img src="docs/images/render-assets.png" width="290" alt="An imported SVG automation ladder rendered to PNG"> |
| Five entities, four relationships, 1:N labels, one self-loop<br>`lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmdraw/main/examples/entity-relationship.lms` | Elbow edges, named markers, measured text wrapping<br>`lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmdraw/main/examples/decision-flowchart.lms` | Static SVG files load as retained, editable scenes<br>`lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmdraw/main/examples/render-assets.lms` |

All 38 examples, including the API tours (title card, nodes and groups, charts, heat map, tree, table, tweened frames), are listed in [examples/README.md](examples/README.md). `examples/render-all.sh` regenerates every picture above.

## Quick start (1 minute)

```sh
npm install -g @lmctl-ai/lmctl
lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmdraw/main/examples/hohmann-transfer.lms
```

The first run downloads the current Linux x86-64 or arm64 lmdraw release binary (glibc >= 2.35); the example then prints its computed answers and writes `images/hohmann-transfer.png` in the current folder. Earlier releases may contain other platform assets; macOS and Windows are not produced by the current lane. Every example runs the same way and finishes in a few seconds.

## Write your own

An lmdraw program is an lmscript file. Plain lmscript computes; the `_dsl_` block is compiled against lmdraw's typed schema and runs inside the native host.

```lmscript
mkdir("images");
mkdir("build");
v = sqrt(398600.4418 / 6778.137);                       # circular speed at 400 km
label = sprintf("LEO speed %.2f km/s", v);
p = _lmcode_ cli="lmdraw" install="github:lmctl-ai/lmdraw@latest";
scene.canvas(640, 200, "#0f1720");
scene.circle(100.0, 100.0, 60.0, "#4f9fd8");
scene.arrow(170.0, 100.0, 300.0, 100.0, "#f5c16c", 3.0);
scene.text(320.0, 106.0, 22.0, ${label}, "#e6edf3");
scene.render_png("images/leo.png", 640, 200, 2);
end _lmcode_
savedsl(p, "private implementation detail");
r = system ${dsl_cli(p)} --dsl private implementation detail;
assert(r.code == 0);
```

The scene API: `canvas`, `rect`, `circle`, `ellipse`, `line`, `arrow`, `polyline`, `path` (SVG path data), `text` (with optional wrap width), `axes`, `log_axes` (semi-log and log–log decade ticks), `legend` (auto-measured multi-series legend box), `group`, `image` (PNG data URI), `linear_gradient`, `load_svg`, `load_font`, `measure_text` and `render_png`; every node keeps SVG-style attributes (`set_attr`) and can be changed between frames. Run `lmdraw --dsl-schema` for the exact signatures and read [docs/LLM_GUIDE.md](docs/LLM_GUIDE.md) for attributes, limits and recipes.

## How it works

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/readme/architecture.png">
  <source media="(prefers-color-scheme: light)" srcset="docs/readme/architecture-light.png">
  <img src="docs/readme/architecture-light.png" alt="lmscript and the lmdraw schema feed the native host, which renders PNG output; SVG assets can be loaded into a scene" width="100%">
</picture>

lmdraw is a single self-contained executable (LunaSVG/PlutoVG rasterizer, Noto Sans and Noto Sans Math compiled in); on Linux only the system glibc (>= 2.35, Ubuntu 22.04 or newer) is linked dynamically. It verifies the bytecode against its embedded schema before running anything, keeps a retained scene of nodes, and writes PNGs atomically. The same binary renders the same bytes on every run.

## Honest limits

- **PNG only.** No SVG or PDF export; animations are numbered PNG frames that you encode with ffmpeg (the GIF examples do this when ffmpeg is installed).
- **Output folders are fixed.** PNGs go directly under `images/` or `private implementation detail`; file inputs come only from `assets/` (SVG) and `assets/fonts/` (TrueType).
- **Glyph coverage.** The embedded Noto Sans Regular/Bold and Noto Sans Math fallback cover Latin, Greek, Cyrillic, sub/superscripts, arrows (→ ← ↑ ↓ ↔ ⇒ ⇔), and common mathematical operators (∝, ∞, ≤, ≥, ≈, ≠, √, ∂, ∆, ∇, ∫, ∑, ∏, ✓). No complex-script OpenType shaping or right-to-left layout.
- **Lightweight plot helpers, not a statistical charting suite.** `axes`, `log_axes`, and `legend` draw linear/logarithmic axes and multi-series legends; curve sampling and fitting are done in lmscript (see `kleiber-allometry.lms` and `rc-lowpass.lms`). For data-heavy statistical charts use [lmplot](https://github.com/lmctl-ai/lmplot).
- **No filters.** No blur, drop shadow or radial-gradient constructor; imported SVG must stay within a static subset (no CSS, scripts or external references).
- **lmscript has no trig or log built-ins** outside the `_dsl_` block (the block itself has `sin`, `cos`, `sqrt`, `pow`); the examples carry short series implementations. Integral numbers cross into a `_dsl_` block as `int`, so pass coordinates as strings or compute them inside the block.
- Byte-identical output is promised for the same binary; different platforms may differ in the last bits of anti-aliasing.

## Build and test

Requires C11/C++17, CMake 3.24+, Ninja, Python 3 and Node.js 24+ with the published lmscript compiler (`npm install -g @lmctl-ai/lmctl`). ffmpeg is optional (GIF frames).

```sh
private implementation detail --verify
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 3
./test.sh                  # host checks, refusals, and every example self-run
examples/render-all.sh     # regenerate docs/images/
```

More: [get started](https://lmctl.com/tools/lmdraw/get_started.html) · [tool guide](https://lmctl.com/tools/lmdraw/guide.html) · [LMScript language docs](https://lmctl.com/tools/lmscript/skill.html) · [releases](https://github.com/lmctl-ai/lmdraw/releases)

## License


Releases up to and including v0.2.6 were published under the MIT License (see the LICENSE asset of that release). Releases after v0.2.6 are published under the Lattice Systems Binary License 1.0 (this file).
The README, docs, images, and `examples/*.lms` are MIT-licensed under `LICENSE-examples`.
Binary releases are licensed under `LICENSE`. Third-party components remain under their own licenses and notices.
