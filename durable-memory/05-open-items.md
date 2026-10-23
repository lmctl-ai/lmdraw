# Open items

The initial renderer does not expose blur or drop-shadow filters, SVG filter graphs, radial-gradient constructors, clipping constructors, SVG import from in-memory strings, or APNG output. Numbered PNG frames are the supported animation handoff; ffmpeg can encode them separately.

Complex text shaping, bidirectional layout, font hinting controls, and browser-equivalent SVG behavior are outside the first milestone. The current six diagrams, embedded Noto Sans, title-card, and tween path are the measured practical baseline.

The public five-platform CI matrix must establish build and runtime behavior for Linux x64 and arm64, macOS arm64 and x64, and Windows x64 before a release tag is cut. Cross-platform byte-for-byte PNG identity is not promised; same-binary deterministic rerenders are tested.

Open after the 2026-10-23 examples pass: (1) release 0.2.4 so `images/` output and the tidier axes labels reach `lmctl autorun` users, then switch examples to render straight into images/; (2) a symbol fallback font (Noto Sans Math/Symbols, OFL) for arrows and math operators; (3) log-scale axes and a legend helper would remove hand-built ticks in rc-lowpass and similar examples; (4) Linux release binaries statically link glibc (LGPL-2.1, with relinking notes in licenses/STATIC-RELINKING.md), which conflicts with the family rule of no GPL/LGPL linking: a musl or dynamic-glibc build needs a decision.
