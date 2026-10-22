#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$root"

compiler="$(scripts/select-compiler.sh)"
host=build/lmdraw
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) host="$host.exe";; esac
[[ -x "$host" ]] || { echo "error: build lmdraw first with cmake --build build" >&2; exit 2; }
mkdir -p build/rendered docs/readme

for name in hero-dark hero-light architecture; do
  "$compiler" "docs/readme/$name.lms" "build/readme-$name.dslb"
  "$host" --dsl "build/readme-$name.dslb" >/dev/null
  cp "build/rendered/readme-$name.png" "docs/readme/$name.png"
done

# The gallery uses actual examples; their checked-in previews are regenerated
# from examples/*.lms by this existing renderer.
scripts/render-previews.sh

python3 - <<'PY'
from pathlib import Path
images = [Path("docs/readme") / f"{name}.png" for name in ("hero-dark", "hero-light", "architecture")]
images += [Path("examples/renders") / f"{name}.png" for name in ("title-card", "flow-pipeline", "chart-bars-lines", "tree-org")]
for image in images:
    if not image.is_file() or image.stat().st_size == 0:
        raise SystemExit(f"missing generated README image: {image}")
    if image.stat().st_size > 400_000:
        raise SystemExit(f"README image exceeds 400 KB: {image} ({image.stat().st_size} bytes)")
print(f"README images ready: {len(images)} files, each under 400 KB")
PY
