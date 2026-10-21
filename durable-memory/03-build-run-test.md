# Build, run, and test

Requirements are C++17, CMake 3.24 or newer, Ninja, Python 3 for generating the embedded byte arrays, and a current lmscript compiler for compiling examples. Python is a build and test tool only; it is not linked into or required by the executable.

Run these commands from the repository root:

```sh
scripts/sync-sdk.sh --verify
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 3
./test.sh
```

The binary reports its version with `lmdraw --version`; this value comes from the single `project(... VERSION ...)` declaration in CMakeLists.txt. Bump that version before creating the matching `vX.Y.Z` release tag. The release workflow checks the tag against the binary version.

Compile an example with lmscript and run its bytecode with build/lmdraw --dsl FILE.dslb. The output directory must be build/rendered. The example test script creates it before rendering.

The test suite checks the SDK pin, embedded schema byte identity, compiler/registry signature parity, bytecode v5 compilation, six SVG fixture renders, a title card, a node API example, 24 tween frames, deterministic rerenders, multi-file prevalidation, output-path refusal, stale handles, normal umask permissions, and Linux static linkage. The C++ unit test checks deterministic raster bytes and a non-background foreground pixel.

For a sanitizer build:

```sh
BUILD_DIR=build-sanitize ./test.sh
```

The resulting sanitizer binary is dynamically linked to sanitizer runtimes; it is not the release artifact. Linux release binaries are fully static. macOS retains only system runtime libraries and is checked with otool. Windows uses MinGW-w64 static compiler runtimes and platform system DLLs.
