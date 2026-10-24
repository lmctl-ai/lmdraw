#!/usr/bin/env bash
# Regenerate every README picture in docs/images/ from the example programs.
# Each example runs itself (exactly as `lmctl autorun <url>` does) and writes
# images/<name>.png (plus a GIF for the animated ones when ffmpeg is installed);
# this script then copies the gallery pictures into docs/images/.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$HERE"
BUILD_DIR="${BUILD_DIR:-build}"
HOST="$BUILD_DIR/lmdraw"
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) HOST="$HOST.exe";; esac
if [[ ! -x "$HOST" ]]; then
  cmake -S . -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release >/dev/null
  cmake --build "$BUILD_DIR" --parallel 3 >/dev/null
fi
command -v ffmpeg >/dev/null 2>&1 || { echo "error: ffmpeg is needed for the gallery GIF" >&2; exit 2; }
LMSCRIPT="$(scripts/select-compiler.sh)"
PATH="$HERE/$BUILD_DIR:$PATH"; export PATH
rm -rf images
mkdir -p docs/images
for src in examples/*.lms; do
  name="$(basename "$src" .lms)"
  "$LMSCRIPT" "$src" "$BUILD_DIR/$name.dslb" >/dev/null
done
gallery="hydrogen-spectrum kepler-second-law hohmann-transfer carnot-cycle lens-ray-diagram incline-free-body
  truss-bridge rc-lowpass lotka-volterra punnett-square mortgage-amortization npv-irr-cashflow
  relativity-lorentz kleiber-allometry damped-oscillator
  decision-flowchart render-assets
  flowchart-order-approval etl-pipeline sequence-oauth-refresh state-machines architecture-layers
  org-chart-grid decision-tree swimlane-process timeline-gantt entity-relationship"
for name in $gallery; do cp "images/$name.png" "docs/images/$name.png"; done
cp images/kepler-second-law.gif docs/images/kepler-second-law.gif
rm -rf images
echo "docs/images/ updated ($(ls -1 docs/images | wc -l | tr -d ' ') files)"
