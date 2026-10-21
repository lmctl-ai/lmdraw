# Licensing and release contents

The project and its vendored SDK header use the repository MIT license. The vector parser and rasterizer are vendored at the exact source revision in third_party.lock; their upstream MIT license files and the bundled FreeType Project License text are retained under licenses/. The included stb helpers retain their public-domain notices in source.

Noto Sans Regular and Bold are included in the binary under the SIL Open Font License 1.1. The original notice and full license are retained in assets/fonts and licenses.

Linux release binaries statically link the C and C++ runtime archives. The relevant LGPL, GPL, and GCC Runtime Library Exception texts are included. The release includes the complete project source and build recipe so recipients can rebuild and relink against modified runtime libraries. macOS uses system runtime libraries. Windows statically links compiler runtimes and uses system DLLs.

Each release stages LICENSE, THIRD_PARTY_NOTICES, full license texts, a binary checksum, and a source archive. The release job is permitted to publish only for version tags after the five build jobs pass.
