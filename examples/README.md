# lmdraw examples

Every example is a self-contained lmscript program. It computes its content in
plain lmscript, compiles a `_dsl_` block bound to lmdraw
(`cli="lmdraw" install="github:lmctl-ai/lmdraw@latest"`), runs it with
`system ${dsl_cli(p)} --dsl ...`, and copies the PNG from `build/rendered/` to
`images/<name>.png` in the current folder. Run any of them from an empty folder:

    lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmdraw/main/examples/<name>.lms

From a checkout, `lmscript examples/<name>.lms` does the same (put `build/` first on
`PATH` to use a local build). `render-all.sh` regenerates the README pictures in
`docs/images/`.

## Stories (a computed result, drawn)

| Example | What it shows | Known answer checked |
| --- | --- | --- |
| `hydrogen-spectrum.lms` | Bohr levels, Lyman/Balmer/Paschen transitions, the visible spectrum | H-alpha 656.5 nm, series limits 91.2 / 364.7 / 820.6 nm |
| `kepler-second-law.lms` | 12 equal-time wedges of an e = 0.6 orbit, plus a 60-frame GIF | every wedge = pi a b / 12 (shoelace, 0.004 %) |
| `hohmann-transfer.lms` | LEO to GEO to scale, two burns, plane-change what-if | 2.40 + 1.46 = 3.85 km/s, 5.3 h coast |
| `carnot-cycle.lms` | P-V loop between 500 K and 300 K, energy flow, hot-side sweep | W = 1153 J (loop area), efficiency 40 % |
| `lens-ray-diagram.lms` | Principal rays for a real image and for a magnifier | di = 15 cm, m = -0.5; di = -15 cm, m = +2.5 |
| `incline-free-body.lms` | Crate on a 30 degree ramp, forces to scale, angle sweep | a = 3.21 m/s^2, slides above 16.7 degrees |
| `truss-bridge.lms` | Pratt vs Howe truss solved by the method of joints | reactions 45 kN, diagonals +/-21.2 kN |
| `rc-lowpass.lms` | RC schematic and magnitude/phase Bode plots, two-stage what-if | fc = 1591.5 Hz, -3.01 dB, -45 degrees |
| `lotka-volterra.lms` | Predator-prey time series and phase portrait (RK4) | period 9.50 vs 2 pi / sqrt(a g) = 9.47 |
| `punnett-square.lms` | Dihybrid cross and a chi-square test on Mendel's counts | 9 : 3 : 3 : 1, chi-square 0.47, p = 0.93 |
| `mortgage-amortization.lms` | Yearly interest vs principal, balance, extra-payment what-if | $2,528.27 a month, $510,178 interest |
| `npv-irr-cashflow.lms` | Cash-flow arrow diagram and the NPV-versus-rate curve | NPV +$19.9k at 8 %, IRR 13.9 % |
| `relativity-lorentz.lms` | Lorentz factor γ = 1/√(1 − β²), length contraction, cosmic-ray muons (math/Greek glyph showcase) | γ(0.998) = 15.82, 23.6 % sea-level survival |
| `kleiber-allometry.lms` | 7-decade × 6-decade `Scene.log_axes` log–log metabolic scaling from shrew to blue whale | OLS exponent α = 0.746 ≈ 3/4 |
| `damped-oscillator.lms` | Underdamped, critically damped and overdamped RK4 responses + phase portrait with `Scene.legend` | ζ = 1.0 settles fastest (5.8 s) with 0 % overshoot |

## API tours

| Example | Shows |
| --- | --- |
| `title-card.lms` | shapes and text on one canvas |
| `node-tour.lms` | groups, a linear gradient, an embedded PNG, `measure_text`, a TrueType font from `assets/fonts/` (downloaded when run outside the repository) |
| `render-assets.lms` | six static SVG files loaded from `assets/` (downloaded when missing) |
| `animated-ladder.lms` | 24 tweened frames with `ease`/`lerp`, encoded to a GIF when ffmpeg is installed |
| `tween-morph.lms` | a path morph and a dash-offset draw-on animation |
| `flow-pipeline.lms`, `decision-flowchart.lms`, `tree-org.lms` | connectors, named markers, wrapped labels |
| `flowchart-order-approval.lms`, `decision-tree.lms`, `swimlane-process.lms` | flowchart idioms: decision diamonds, branch labels, lanes |
| `etl-pipeline.lms`, `architecture-layers.lms`, `org-chart-grid.lms` | cylinders, cluster boxes, elbow connectors, folded-corner note |
| `sequence-oauth-refresh.lms`, `state-machines.lms` | lifelines, activation bars, self-messages, state panels |
| `timeline-gantt.lms`, `entity-relationship.lms` | Gantt bars, dependencies, milestones, ER shapes and cardinalities |
| `table-metrics.lms`, `heatmap-grid.lms`, `chart-bars-lines.lms` | tables, cell grids and hand-built charts |
| `geometry-ellipse.lms`, `arrow-helper.lms` | geometry constructions, `arrow` and `axes` |

See `../docs/LLM_GUIDE.md` for the full API, attribute rules and limits.
