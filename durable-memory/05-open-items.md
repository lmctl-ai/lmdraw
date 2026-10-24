# Open items

The initial renderer does not expose blur or drop-shadow filters, SVG filter graphs, radial-gradient constructors, clipping constructors, SVG import from in-memory strings, or APNG output. Numbered PNG frames are the supported animation handoff; ffmpeg can encode them separately.

Complex text shaping, bidirectional layout, font hinting controls, and browser-equivalent SVG behavior are outside the first milestone. The current six diagrams, embedded Noto Sans, title-card, and tween path are the measured practical baseline.

The public five-platform CI matrix must establish build and runtime behavior for Linux x64 and arm64, macOS arm64 and x64, and Windows x64 before a release tag is cut. Cross-platform byte-for-byte PNG identity is not promised; same-binary deterministic rerenders are tested.

Resolved in 0.2.4: (1) `images/` output root, (2) embedded `NotoSansMath-Regular.ttf` (OFL-1.1) per-glyph fallback for arrows and math symbols, and (3) `Scene.log_axes` (semi-log and log–log decade + sub-decade ticks) and `Scene.legend`. Resolved in 0.2.5: Linux binaries link glibc dynamically (LGPL-2.1 relink obligations no longer apply); licenses/STATIC-RELINKING.md and the source-bundle release asset were removed.

2026-10-24 — helper fn duplication across the ten diagram examples added today (flowchart-order-approval, etl-pipeline, sequence-oauth-refresh, state-machines, architecture-layers, org-chart-grid, decision-tree, swimlane-process, timeline-gantt, entity-relationship). These are candidates for promotion into the host API or a shared stdlib chunk; script fns cannot capture chunk-level state, so each block redefines them with literal colors. Actual counts:

RESOLVED 2026-10-24 in 0.2.6: the helpers moved into the host API as typed Scene methods — labeled `box`/`diamond`/`cylinder`/`note`/`cluster` shapes with named anchors (n/s/e/w/corners/c), a `connector` with straight/elbow/curve routing + named markers + dash + arc-length label placement, sequence notation (`lifeline`/`message`/`activation`/`self_message`), and a cell `grid`. All ten examples were rewritten on the new API: 976 -> 689 lines (-287, about 29%). What stays in-script by design: example-specific layout math (Gantt week mapping, org-chart bus/stub tree layout, multi-bend routes that dodge obstacles, two-line org labels), the title + muted subtitle header idiom (2 lines each), and genuinely custom geometry (ER entity bodies, door-lock arcs, the swimlane retry loop).
