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

Examples run themselves: `lmscript examples/NAME.lms build/NAME.dslb` compiles the block, then the script calls `system ${dsl_cli(p)} --dsl` and copies the PNG to images/NAME.png. test.sh puts the fresh build/ first on PATH so `cli="lmdraw"` binds to the local host (otherwise lmscript installs the latest release). See 07-examples-and-images.md.

The test suite checks the README gallery (every autorun URL names an existing example, every docs/ image has PNG/GIF magic and fits 300 KB / 3 MB), the SDK pin, embedded schema byte identity, compiler/registry signature parity, bytecode v5 compilation, six SVG fixture renders, every example self-running into a valid images/NAME.png (GIFs when ffmpeg exists), a title card, a node API example, 24 tween frames, the images/ output root and its refusals, deterministic rerenders, multi-file prevalidation, output-path refusal, stale handles, normal umask permissions, and Linux static linkage. The C++ unit test checks deterministic raster bytes, a non-background foreground pixel, and the axes tick-label formatter (cpp/tick_format.hpp).

For a sanitizer build:

```sh
BUILD_DIR=build-sanitize ./test.sh
```

The resulting sanitizer binary is dynamically linked to sanitizer runtimes; it is not the release artifact. Linux release binaries are static except for the system glibc (>= 2.35), which is linked dynamically (Rule 14); test.sh enforces a NEEDED allowlist of libc/libm/libpthread/libdl/librt/ld-linux. macOS retains only system runtime libraries and is checked with otool. Windows uses MinGW-w64 static compiler runtimes and platform system DLLs.
