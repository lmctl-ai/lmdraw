# Gotchas and limits

The default bundled faces are Noto Sans Regular and Bold, embedded in the executable. Custom fonts are loaded only from a direct child of assets/fonts and are not fetched. Text metrics use the renderer's font metrics; there is no promise of browser-identical shaping.

A canvas call or a load from a different SVG path advances the scene generation. Repeated loads of one path preserve handles for multiple IDs in the same document. Existing Node and Gradient handles become stale after a new scene starts; the host keeps their storage alive and reports a DSL error if a stale handle is used. This avoids dangling pointers when multiple bytecode blocks share the Scene object.

Scenes are limited to 10,000 created nodes and 1,024 gradients per host process; each gradient accepts at most 128 stops. Canvas sides are limited to 4096 pixels, unscaled area to 8 million pixels, and scaled PNG area to 8 million pixels. Numeric coordinates must be finite and within plus or minus 1,000,000 pixels. Bytecode files are limited to 16 MiB each. These are host-side resource checks; the VM itself has no instruction budget.

Output paths must be PNG files directly under build/rendered or (since 0.2.4) images; both roots are created on demand. Released 0.2.3 accepts only build/rendered, so examples render there and copy_file into images/ until a release with the images/ root is the latest. Symlinked output roots, subdirectories, output symlinks, and traversal are refused. Writes use a temporary sibling and atomic replacement. Temporary files are created with mode 0666 filtered by the process umask.

The imported SVG parser and scripted nodes do not implement CSS, browser layout, SMIL, filters, or external image/font references. Filter names are not exposed. Verify any less common static SVG construct visually because SVG acceptance does not guarantee browser parity.

lmscript gotchas met while writing examples: integral numbers cross `${...}` as int (10.0 becomes 10) and the block's float() only widens ints, so pass coordinates as strings via set_attr or path/points data, or compute in the block. Lists cannot cross `${}`. Outside the block there is no sin/cos/exp/log (only sqrt and pow); sprintf lacks %e and %x; numeric literals cannot use exponents (write 1000000000.0). `system` throws when the program is missing, so guard optional tools with which(). DSL blocks may define typed fns and may be created inside lmscript fns; several bytecode files passed to one --dsl call share the scene.

The embedded Noto Sans lacks arrows (U+2190..), ∞, ≤, ≥, ≈, ≠, √, ∑, ∫, ∝ and ✓; those render as boxes. Greek, sub/superscripts, °, ×, ±, ½, µ and Ω work.
