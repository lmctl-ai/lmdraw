#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"
command -v ffmpeg >/dev/null || { echo "error: install ffmpeg to render preview PNGs" >&2; exit 2; }
lmscript="$(scripts/select-compiler.sh)"
host=build/lmdraw
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) host="$host.exe";; esac
mkdir -p build/rendered examples/renders
for name in title-card node-tour render-assets animated-ladder; do
  "$lmscript" "examples/$name.lms" "build/$name.dslb"
  "$host" --dsl "build/$name.dslb" >/dev/null
done

preview() {
  local source="$1" target="$2" dimensions="$3" palette="build/preview-palette.png"
  ffmpeg -v error -y -i "$source" -frames:v 1 \
    -vf "scale=$dimensions:flags=lanczos,palettegen=max_colors=128" "$palette"
  ffmpeg -v error -y -i "$source" -i "$palette" -frames:v 1 \
    -lavfi "scale=$dimensions:flags=lanczos[x];[x][1:v]paletteuse=dither=none" "$target"
}
preview build/rendered/title-card.png examples/renders/title-card.png 480:270
preview build/rendered/node-tour.png examples/renders/node-tour.png 320:180
preview build/rendered/svg-automation-ladder.png examples/renders/automation-ladder.png 480:180
preview build/rendered/ladder-12.png examples/renders/animated-ladder-frame.png 480:180
echo "Preview PNGs written under examples/renders/ (palette-limited for compact documentation)."
