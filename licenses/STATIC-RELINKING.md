# Static runtime relinking

The Linux executable statically links the platform C runtime and GCC C++
runtime. The full corresponding project source is available in the source
archive attached to each release. To relink against a modified runtime, unpack
the source, install CMake, Ninja, Python 3, and a C++17 compiler, configure with
`-DLMDRAW_FULL_STATIC=ON`, and build with `cmake --build build`. The CMake
build compiles the application and the included vector libraries from source;
linker selection comes from the configured toolchain. This rebuilds the
executable with the locally installed runtime archives and preserves the
embedded schema and fonts.
