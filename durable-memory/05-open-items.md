# Open items

The initial renderer does not expose blur or drop-shadow filters, SVG filter graphs, radial-gradient constructors, clipping constructors, SVG import from in-memory strings, or APNG output. Numbered PNG frames are the supported animation handoff; ffmpeg can encode them separately.

Complex text shaping, bidirectional layout, font hinting controls, and browser-equivalent SVG behavior are outside the first milestone. The current six diagrams, embedded Noto Sans, title-card, and tween path are the measured practical baseline.

The public five-platform CI matrix must establish build and runtime behavior for Linux x64 and arm64, macOS arm64 and x64, and Windows x64 before a release tag is cut. Cross-platform byte-for-byte PNG identity is not promised; same-binary deterministic rerenders are tested.

Resolved in 0.2.4: (1) `images/` output root, (2) embedded `NotoSansMath-Regular.ttf` (OFL-1.1) per-glyph fallback for arrows and math symbols, and (3) `Scene.log_axes` (semi-log and log–log decade + sub-decade ticks) and `Scene.legend`. Resolved in 0.2.5: Linux binaries link glibc dynamically (LGPL-2.1 relink obligations no longer apply); licenses/STATIC-RELINKING.md and the source-bundle release asset were removed.

2026-10-24 — helper fn duplication across the ten diagram examples added today (flowchart-order-approval, etl-pipeline, sequence-oauth-refresh, state-machines, architecture-layers, org-chart-grid, decision-tree, swimlane-process, timeline-gantt, entity-relationship). These are candidates for promotion into the host API or a shared stdlib chunk; script fns cannot capture chunk-level state, so each block redefines them with literal colors. Actual counts:

- box-with-centered-label (rounded rect + wrapped `text-anchor: middle` label): 7 of 10 — `process` (flowchart-order-approval), `box_with_text` (etl-pipeline, architecture-layers), `state` (state-machines), `org_box` (org-chart-grid), `step_box` (swimlane-process), `leaf` (decision-tree).
- elbow/connector fn drawing an SVG path with `marker-end: arrow`: 6 of 10 — `edge` (flowchart-order-approval), `dashed_edge` + inline source fan-in (etl-pipeline), `elbow` (architecture-layers), `bus`/`stub`/`elbow_dashed` (org-chart-grid), `flow` (swimlane-process), `dep` (timeline-gantt).
- diamond shape from four path segments: 4 of 10 — `diamond` (flowchart-order-approval, swimlane-process), `split_node` (decision-tree), `rel_diamond` (entity-relationship).
- labeled band/panel rect (lane, layer, panel): 3 of 10 — `lane` (swimlane-process), `layer` (architecture-layers), `panel` (state-machines).
- labeled straight transition edge: 3 of 10 — `trans` (state-machines), `branch` (decision-tree), `message` (sequence-oauth-refresh).
- cylinder (rect + two ellipse caps): 2 of 10 — `cylinder` (etl-pipeline, architecture-layers).
- grid/row coordinate helper returning a float: 2 of 10 — `col_x` (org-chart-grid), `xw`/`row_y` (timeline-gantt).
- lifeline/message/activation trio: 1 of 10 — sequence-oauth-refresh only.

Every one of the ten also repeats the title + muted subtitle header idiom.
