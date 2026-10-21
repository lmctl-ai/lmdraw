# Gotchas and limits

The default bundled faces are Noto Sans Regular and Bold, embedded in the executable. Custom fonts are loaded only from a direct child of assets/fonts and are not fetched. Text metrics use the renderer's font metrics; there is no promise of browser-identical shaping.

A canvas call or SVG load advances the scene generation. Existing Node and Gradient handles become stale; the host keeps their storage alive and reports a DSL error if a stale handle is used. This avoids dangling pointers when multiple bytecode blocks share the Scene object.

Scenes are limited to 10,000 created nodes and 1,024 gradients per host process; each gradient accepts at most 128 stops. Canvas sides are limited to 4096 pixels, unscaled area to 8 million pixels, and scaled PNG area to 8 million pixels. Numeric coordinates must be finite and within plus or minus 1,000,000 pixels. Bytecode files are limited to 16 MiB each. These are host-side resource checks; the VM itself has no instruction budget.

Output paths must be PNG files directly under build/rendered. Symlinked output roots, output symlinks, and traversal are refused. Writes use a temporary sibling and atomic replacement. Temporary files are created with mode 0666 filtered by the process umask.

The imported SVG parser and scripted nodes do not implement CSS, browser layout, SMIL, filters, or external image/font references. Filter names are not exposed. Verify any less common static SVG construct visually because SVG acceptance does not guarantee browser parity.
