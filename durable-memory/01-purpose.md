# Purpose and place

lmdraw is a schema-first native host for retained two-dimensional scenes written in lmscript. It turns a compiled DSL bytecode file into PNG stills or numbered PNG frames. A script creates nodes once and updates their attributes in ordinary loops.

The lmcode-cpp header is pinned by commit and SHA-256 in sdk.lock. The same schema bytes are embedded in the executable and are printed by the JSON schema command. The C++ registry signatures are checked against the compiler's canonical signatures.

The renderer is an internal implementation detail. The schema uses scene, node, group, gradient, and SVG-style presentation vocabulary. The C++ host talks to a small Renderer interface so a different raster engine can replace the current one without changing scripts or schema.

The host is headless and performs no network operations. The compiler and runtime bytecode format are separate project components; lmdraw requires the current bytecode version only.
