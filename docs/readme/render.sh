#!/usr/bin/env bash
# Regenerate the README architecture diagrams (dark and light) from docs/readme/*.lms.
# The picture gallery is regenerated separately by examples/render-all.sh.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$root"

compiler="$(scripts/select-compiler.sh)"
host=build/lmdraw
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) host="$host.exe";; esac
[[ -x "$host" ]] || { echo "error: build lmdraw first with cmake --build build" >&2; exit 2; }
mkdir -p build/rendered docs/readme

for name in architecture architecture-light; do
  "$compiler" "docs/readme/$name.lms" "build/readme-$name.dslb"
  "$host" --lmcode "build/readme-$name.dslb" >/dev/null
  cp "build/rendered/readme-$name.png" "docs/readme/$name.png"
done
echo "README architecture diagrams updated in docs/readme/"
