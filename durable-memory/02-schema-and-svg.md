# Schema and SVG input

The schema has one Scene context and retained Node, Group, Gradient, Shape, and Grid object types. Constructors cover rectangles, circles, ellipses, lines, arrows, axes, log_axes, legends, polylines, paths, text, images, and groups, plus the 0.2.6 diagram building blocks: labeled box/diamond/cylinder/note/cluster shapes with named anchors, connector edges (straight/elbow/curve routing, named markers, dash patterns, arc-length-placed labels), sequence notation (lifeline, message, activation, self_message), and an invisible cell grid. Nodes expose set_attr with SVG-style names. Linear gradients use a Gradient handle with stop members. Imported SVG IDs return the same Node handle type. Shapes and grids are retained handles with the same stale-after-new-scene checks as Node and Gradient.

Canvas coordinates are floating-point pixels with the origin at upper left. Text y is the baseline. Image output uses a scale multiplier, so a 640 by 360 scene at scale 2 produces a 1280 by 720 PNG.

Scene.measure_text uses the embedded regular or bold Noto Sans face to return a pixel width. It does not provide complex-script shaping or bidirectional layout. Scene.load_font accepts explicit TrueType files under assets/fonts only.

SVG files are read only from direct children of assets, with a 2 MiB maximum. Accepted tags are svg, g, title, desc, rect, circle, ellipse, line, polyline, polygon, path, text, tspan, defs, linearGradient, radialGradient, stop, clipPath, mask, and use. The accepted attributes are listed in docs/LLM_GUIDE.md and enforced by the host. CSS classes and stylesheets, scripts, events, DTDs, external references, animation, foreignObject, and filters are refused. Same-document fragment references are allowed. The Scene.image constructor accepts embedded PNG data URIs; imported SVG images are not accepted.

The renderer boundary receives serialized scene descriptions or opaque imported-document handles. Backend classes and library types stay under cpp/ and do not appear in the schema or ordinary error messages.
