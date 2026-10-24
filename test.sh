#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$HERE"
BUILD_DIR="${BUILD_DIR:-build}"
HOST="$BUILD_DIR/lmdraw"
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) HOST="$HOST.exe";; esac
ok_count=0
ok(){ ok_count=$((ok_count+1)); printf 'ok %02d - %s\n' "$ok_count" "$1"; }
fail(){ echo "FAIL: $*" >&2; exit 1; }
check_png(){
  python3 - "$1" "${2:-0}" "${3:-0}" <<'PY'
from pathlib import Path
import struct,sys
b=Path(sys.argv[1]).read_bytes()
assert b[:8] == b'\x89PNG\r\n\x1a\n' and b[12:16] == b'IHDR', sys.argv[1]
w,h=struct.unpack('>II',b[16:24])
ew,eh=int(sys.argv[2]),int(sys.argv[3])
assert (not ew or w == ew) and (not eh or h == eh), (sys.argv[1],w,h,ew,eh)
PY
}
trap 'rm -rf "$HERE/images"' EXIT
# README gallery: every autorun URL names an existing example, every picture exists
# with valid PNG/GIF magic bytes and stays within the size budget.
readme_lms=0
while read -r lms; do
  [[ -f "$lms" ]] || fail "README references a missing example: $lms"
  readme_lms=$((readme_lms+1))
done < <(grep -oE 'raw.githubusercontent.com/lmctl-ai/lmdraw/main/examples/[A-Za-z0-9_.-]+\.lms' README.md | sed 's|.*/main/||' | sort -u)
[[ $readme_lms -ge 15 ]] || fail "expected at least 15 lmctl autorun examples in README.md, found $readme_lms"
if grep -nE '\[Source\]|see source' README.md; then fail "README still links sources instead of lmctl autorun commands"; fi
readme_img=0
while read -r img; do
  [[ -s "$img" ]] || fail "README references a missing image: $img"
  bytes=$(wc -c < "$img" | tr -d ' ')
  case "$img" in
    *.png) check_png "$img" || fail "invalid PNG: $img"; [[ $bytes -le 307200 ]] || fail "$img exceeds 300 KB";;
    *.gif) [[ "$(head -c 6 "$img")" == GIF8[79]a ]] || fail "invalid GIF: $img"; [[ $bytes -le 3145728 ]] || fail "$img exceeds 3 MB";;
  esac
  readme_img=$((readme_img+1))
done < <(grep -oE 'docs/[A-Za-z0-9_/.-]+\.(png|gif)' README.md | sort -u)
[[ $readme_img -ge 15 ]] || fail "expected at least 15 README images, found $readme_img"
ok "README: $readme_lms autorun examples exist and $readme_img images have valid PNG/GIF headers"
mkdir -p build/rendered
scripts/sync-sdk.sh --verify >/dev/null || fail "SDK lock mismatch"
ok "vendored SDK hash matches sdk.lock"
LMSCRIPT="$(scripts/select-compiler.sh)"
echo "compiler: $LMSCRIPT"
if [[ "$BUILD_DIR" == *sanitize* ]]; then
  cmake -S . -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_FLAGS=-fsanitize=address,undefined \
    -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
    -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined \
    -DLMDRAW_FULL_STATIC=OFF > "$BUILD_DIR-configure.log" 2>&1 || { cat "$BUILD_DIR-configure.log" >&2; fail "CMake configure"; }
else
  cmake -S . -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release > "$BUILD_DIR-configure.log" 2>&1 || { cat "$BUILD_DIR-configure.log" >&2; fail "CMake configure"; }
fi
if command -v nice >/dev/null 2>&1; then nice -n 10 cmake --build "$BUILD_DIR" --parallel 3 || fail "build"; else cmake --build "$BUILD_DIR" --parallel 3 || fail "build"; fi
ctest --test-dir "$BUILD_DIR" --output-on-failure || fail "C++ unit tests"
ok "host and C++ raster unit test build"
[[ "$("$HOST" --version)" == "lmdraw 0.2.5" ]] || fail "--version does not report the CMake project version"
set +e
"$HOST" --version extra >/dev/null 2>&1; code=$?
set -e
[[ $code == 2 ]] || fail "--version extra argument exit $code, expected 2"
ok "--version prints lmdraw 0.2.5 and rejects extra arguments"
"$HOST" --schema --json > "$BUILD_DIR/embedded-schema.json"
cmp schema/lmdraw.dsl.json "$BUILD_DIR/embedded-schema.json" || fail "embedded schema bytes differ"
ok "--schema --json is byte-identical to schema/lmdraw.dsl.json"
"$HOST" --dsl-schema --json > "$BUILD_DIR/compiler-schema.json"
cmp schema/lmdraw.dsl.json "$BUILD_DIR/compiler-schema.json" || fail "compiler schema protocol differs"
"$HOST" --schema > "$BUILD_DIR/schema-summary.txt"
cmp tests/golden/schema-summary.txt "$BUILD_DIR/schema-summary.txt" || fail "schema summary differs from its golden"
"$HOST" --dsl-schema > "$BUILD_DIR/dsl-schema-summary.txt"
cmp "$BUILD_DIR/schema-summary.txt" "$BUILD_DIR/dsl-schema-summary.txt" || fail "compiler schema summary differs"
ok "--dsl-schema protocol matches the embedded schema and summary"
"$HOST" --dsl-signatures > "$BUILD_DIR/host-signatures.txt"
"$LMSCRIPT" --dsl-signatures schema/lmdraw.dsl.json > "$BUILD_DIR/compiler-signatures.txt"
diff -u "$BUILD_DIR/compiler-signatures.txt" "$BUILD_DIR/host-signatures.txt" || fail "schema/registry signatures differ"
python3 - "$BUILD_DIR/schema-summary.txt" "$BUILD_DIR/host-signatures.txt" <<'PY'
import pathlib,sys
summary=pathlib.Path(sys.argv[1]).read_text(); signatures=pathlib.Path(sys.argv[2]).read_text().splitlines()
need=[]
for line in signatures:
    token=line.split(' ',1)[1] if line.startswith(('context ','method ','fn ')) else line
    if token not in summary: need.append(token)
assert not need, need
assert summary.startswith('Context\ncontext scene: Scene\n')
print(f'{len(signatures)} signatures listed in compact summary')
PY
ok "summary golden and registry signatures match the compiler"

# Examples run themselves (savedsl + system lmdraw --dsl) exactly as lmctl autorun
# does; the freshly built host is first on PATH so cli="lmdraw" binds to it.
PATH="$HERE/$BUILD_DIR:$PATH"; export PATH
rm -rf images
example_count=0
for src in examples/*.lms; do
  example="$(basename "$src" .lms)"
  "$LMSCRIPT" "$src" "$BUILD_DIR/$example.dslb" > "$BUILD_DIR/example-$example.log" 2>&1 || { cat "$BUILD_DIR/example-$example.log" >&2; fail "$example compile/self-run"; }
  python3 - "$BUILD_DIR/$example.dslb" <<'PY' || fail "example is not bytecode v5"
from pathlib import Path
import sys
assert Path(sys.argv[1]).read_bytes()[:6] == b'DSLB\x00\x05'
PY
  check_png "images/$example.png" || fail "$example did not write a valid images/$example.png"
  example_count=$((example_count+1))
done
[[ $example_count -ge 28 ]] || fail "expected at least 28 examples, found $example_count"
if command -v ffmpeg >/dev/null 2>&1; then
  for gif in kepler-second-law animated-ladder tween-morph; do
    [[ "$(head -c 6 "images/$gif.gif")" == GIF8[79]a ]] || fail "$gif did not write a GIF"
  done
  gif_note="and their GIFs"
else
  gif_note="(ffmpeg not found: GIF encoding skipped)"
fi
ok "$example_count examples compile to bytecode v5 and self-run into images/ $gif_note"
"$HOST" --dsl "$BUILD_DIR/title-card.dslb" "$BUILD_DIR/node-tour.dslb" > "$BUILD_DIR/multi.out" || fail "multi-file execution"
[[ "$(wc -l < "$BUILD_DIR/multi.out" | tr -d ' ')" == 2 ]] || fail "multi-file result count"
ok "all bytecode files load before ordered execution"
"$HOST" --dsl "$BUILD_DIR/render-assets.dslb" > /dev/null || fail "SVG fixtures render"
for f in automation-ladder automation-ladder-mobile level-automation level-delegate level-sessions level-team; do
  check_png "build/rendered/svg-$f.png" || fail "fixture render $f is not PNG"
done
ok "six homepage SVG fixtures load and render"
"$HOST" --dsl "$BUILD_DIR/animated-ladder.dslb" > /dev/null || fail "animation run"
[[ $(compgen -G "build/rendered/ladder-*.png" | wc -l | tr -d ' ') == 24 ]] || fail "expected 24 frames"
if command -v sha256sum >/dev/null; then sha256sum build/rendered/ladder-*.png | sort -k1,1 > "$BUILD_DIR/frames-first.sha"; else shasum -a 256 build/rendered/ladder-*.png | sort -k1,1 > "$BUILD_DIR/frames-first.sha"; fi
[[ $(cut -d' ' -f1 "$BUILD_DIR/frames-first.sha" | sort -u | wc -l | tr -d ' ') == 24 ]] || fail "animation frames are not distinct"
"$HOST" --dsl "$BUILD_DIR/animated-ladder.dslb" > /dev/null || fail "deterministic animation rerun"
if command -v sha256sum >/dev/null; then sha256sum build/rendered/ladder-*.png | sort -k1,1 > "$BUILD_DIR/frames-second.sha"; else shasum -a 256 build/rendered/ladder-*.png | sort -k1,1 > "$BUILD_DIR/frames-second.sha"; fi
cmp "$BUILD_DIR/frames-first.sha" "$BUILD_DIR/frames-second.sha" || fail "frame rerender changed bytes"
ok "24 frames are distinct and byte-identical on rerender"

"$HOST" --dsl "$BUILD_DIR/title-card.dslb" > /dev/null || fail "title-card rerender"
check_png build/rendered/title-card.png 960 540 || fail "title-card dimensions"
ok "title-card PNG has expected dimensions"

for spec in flow-pipeline:960:420 decision-flowchart:960:560 tree-org:960:520 table-metrics:960:430 chart-bars-lines:800:460 heatmap-grid:620:480 geometry-ellipse:800:460 arrow-helper:900:600; do
  IFS=: read -r example w h <<< "$spec"
  "$HOST" --dsl "$BUILD_DIR/$example.dslb" > /dev/null || fail "$example run"
  check_png "build/rendered/$example.png" "$w" "$h" || fail "$example PNG"
done
ok "seven drawing examples render PNGs at their canvas sizes"
cp build/rendered/decision-flowchart.png "$BUILD_DIR/decision-marked.png"
sed '/set_attr("marker-end", /d' examples/decision-flowchart.lms > "$BUILD_DIR/decision-no-markers.lms"
"$LMSCRIPT" "$BUILD_DIR/decision-no-markers.lms" "$BUILD_DIR/decision-no-markers.dslb" >/dev/null || fail "marker comparison compile"
"$HOST" --dsl "$BUILD_DIR/decision-no-markers.dslb" >/dev/null || fail "marker comparison render"
if cmp -s "$BUILD_DIR/decision-marked.png" build/rendered/decision-flowchart.png; then fail "named markers do not affect the rendered image"; fi
"$HOST" --dsl "$BUILD_DIR/decision-flowchart.dslb" >/dev/null || fail "marked decision rerender"
ok "named arrow, diamond and dot markers change the rendered image"
cp build/rendered/decision-flowchart.png "$BUILD_DIR/decision-wrapped.png"
sed 's/, w - 24.0)/)/g' examples/decision-flowchart.lms > "$BUILD_DIR/decision-no-wrap.lms"
"$LMSCRIPT" "$BUILD_DIR/decision-no-wrap.lms" "$BUILD_DIR/decision-no-wrap.dslb" >/dev/null || fail "wrap comparison compile"
"$HOST" --dsl "$BUILD_DIR/decision-no-wrap.dslb" >/dev/null || fail "wrap comparison render"
if cmp -s "$BUILD_DIR/decision-wrapped.png" build/rendered/decision-flowchart.png; then fail "measured wrapping does not affect the rendered image"; fi
"$HOST" --dsl "$BUILD_DIR/decision-flowchart.dslb" >/dev/null || fail "wrapped decision rerender"
ok "measured text wrapping changes the rendered image"
"$HOST" --dsl "$BUILD_DIR/tween-morph.dslb" > /dev/null || fail "tween-morph run"
[[ $(compgen -G "build/rendered/morph-*.png" | wc -l | tr -d ' ') == 6 ]] || fail "expected 6 morph frames"
[[ $(compgen -G "build/rendered/drawon-*.png" | wc -l | tr -d ' ') == 14 ]] || fail "expected 14 draw-on frames"
for f in build/rendered/morph-*.png build/rendered/drawon-*.png; do check_png "$f" 800 460 || fail "frame $f"; done
ok "tween-morph renders 6 morph and 14 dash-offset draw-on frames"
if command -v sha256sum >/dev/null; then
  sha256sum build/rendered/drawon-*.png | sort -k1,1 > "$BUILD_DIR/drawon-first.sha"
else
  shasum -a 256 build/rendered/drawon-*.png | sort -k1,1 > "$BUILD_DIR/drawon-first.sha"
fi
[[ $(cut -d' ' -f1 "$BUILD_DIR/drawon-first.sha" | sort -u | wc -l | tr -d ' ') -ge 10 ]] || fail "draw-on frames do not advance distinctly"
"$HOST" --dsl "$BUILD_DIR/tween-morph.dslb" >/dev/null || fail "deterministic draw-on rerender"
if command -v sha256sum >/dev/null; then
  sha256sum build/rendered/drawon-*.png | sort -k1,1 > "$BUILD_DIR/drawon-second.sha"
else
  shasum -a 256 build/rendered/drawon-*.png | sort -k1,1 > "$BUILD_DIR/drawon-second.sha"
fi
cmp "$BUILD_DIR/drawon-first.sha" "$BUILD_DIR/drawon-second.sha" || fail "draw-on rerender changed bytes"
ok "14 dash-offset frames are distinct and byte-identical on rerender"
for fixture in invalid-marker invalid-dash; do
  "$LMSCRIPT" "tests/refusals/$fixture.lms" "$BUILD_DIR/$fixture.dslb" >/dev/null || fail "$fixture fixture compile"
  set +e
  "$HOST" --dsl "$BUILD_DIR/$fixture.dslb" >/dev/null 2> "$BUILD_DIR/$fixture.err"; code=$?
  set -e
  [[ $code == 3 ]] || fail "$fixture refusal exit $code, expected 3"
done
grep -q 'marker-end must be none, arrow, dot, or diamond' "$BUILD_DIR/invalid-marker.err" || fail "marker value diagnostic"
grep -q 'stroke-dasharray entries must be positive finite pixel lengths' "$BUILD_DIR/invalid-dash.err" || fail "dash value diagnostic"
ok "invalid marker names and dash lengths are refused with located DSL errors"
"$LMSCRIPT" tests/refusals/invalid-ellipse-radius.lms "$BUILD_DIR/invalid-ellipse-radius.dslb" >/dev/null || fail "invalid ellipse radius fixture compile"
set +e
"$HOST" --dsl "$BUILD_DIR/invalid-ellipse-radius.dslb" >/dev/null 2> "$BUILD_DIR/invalid-ellipse-radius.err"; code=$?
set -e
[[ $code == 3 ]] || fail "invalid ellipse radius refusal exit $code, expected 3"
grep -q 'rx must be nonnegative' "$BUILD_DIR/invalid-ellipse-radius.err" || fail "ellipse radius diagnostic"
ok "invalid ellipse radii are refused before rendering"
"$LMSCRIPT" tests/refusals/invalid-axes.lms "$BUILD_DIR/invalid-axes.dslb" >/dev/null || fail "invalid axes fixture compile"
set +e
"$HOST" --dsl "$BUILD_DIR/invalid-axes.dslb" >/dev/null 2> "$BUILD_DIR/invalid-axes.err"; code=$?
set -e
[[ $code == 3 ]] || fail "invalid axes refusal exit $code, expected 3"
grep -q 'axes ranges must be increasing' "$BUILD_DIR/invalid-axes.err" || fail "axes range diagnostic"
ok "invalid axes ranges are refused before node creation"
for fixture in invalid-log-axes invalid-legend; do
  "$LMSCRIPT" "tests/refusals/$fixture.lms" "$BUILD_DIR/$fixture.dslb" >/dev/null || fail "$fixture fixture compile"
  set +e
  "$HOST" --dsl "$BUILD_DIR/$fixture.dslb" >/dev/null 2> "$BUILD_DIR/$fixture.err"; code=$?
  set -e
  [[ $code == 3 ]] || fail "$fixture refusal exit $code, expected 3"
done
grep -q 'log_axes logarithmic x range must be positive' "$BUILD_DIR/invalid-log-axes.err" || fail "log_axes range diagnostic"
grep -q 'legend requires 1..16 non-empty labels and one matching color per label' "$BUILD_DIR/invalid-legend.err" || fail "legend count diagnostic"
"$LMSCRIPT" tests/math-glyphs.lms "$BUILD_DIR/math-glyphs.dslb" >/dev/null || fail "math-glyphs fixture compile"
"$HOST" --dsl "$BUILD_DIR/math-glyphs.dslb" >/dev/null || fail "math-glyphs render"
check_png images/lmdraw-math-glyphs.png 320 120 || fail "math-glyphs PNG"
ok "math glyphs, log_axes, and legend render and refuse invalid inputs"
"$LMSCRIPT" tests/zero-corner-radius.lms "$BUILD_DIR/zero-corner-radius.dslb" >/dev/null || fail "zero corner radius fixture compile"
"$HOST" --dsl "$BUILD_DIR/zero-corner-radius.dslb" >/dev/null || fail "zero corner radius render"
check_png build/rendered/zero-corner-radius.png 120 80 || fail "zero corner radius PNG"
ok "zero rectangle corner radii remain valid"
rm -f images/lmdraw-test-output.png
"$LMSCRIPT" tests/images-output.lms "$BUILD_DIR/images-output.dslb" >/dev/null || fail "images output fixture compile"
"$HOST" --dsl "$BUILD_DIR/images-output.dslb" >/dev/null || fail "images output render"
check_png images/lmdraw-test-output.png 64 32 || fail "images/ output PNG"
for fixture in images-subdir images-traversal; do
  "$LMSCRIPT" "tests/refusals/$fixture.lms" "$BUILD_DIR/$fixture.dslb" >/dev/null || fail "$fixture fixture compile"
  set +e
  "$HOST" --dsl "$BUILD_DIR/$fixture.dslb" >/dev/null 2> "$BUILD_DIR/$fixture.err"; code=$?
  set -e
  [[ $code == 3 ]] || fail "$fixture refusal exit $code, expected 3"
done
grep -q 'PNG output must remain directly under images/' "$BUILD_DIR/images-subdir.err" || fail "images subdirectory diagnostic"
grep -q 'PNG output traversal is refused' "$BUILD_DIR/images-traversal.err" || fail "images traversal diagnostic"
[[ ! -e escaped.png && ! -e images/nested ]] || fail "refused images/ output left files behind"
ok "images/ is a second PNG output root; subdirectories and traversal are refused"
if [[ "$(uname -s)" == Linux* ]]; then
  (umask 027; "$HOST" --dsl "$BUILD_DIR/title-card.dslb" >/dev/null) || fail "umask output run"
  [[ "$(stat -c '%a' build/rendered/title-card.png)" == 640 ]] || fail "PNG mode does not honor umask"
  ok "atomic PNG output honors umask (0640 under 0027)"
fi

set +e
"$HOST" >/dev/null 2>&1; code=$?
set -e
[[ $code == 2 ]] || fail "bad args exit $code, expected 2"
ok "bad arguments rejected with exit 2"

python3 - "$BUILD_DIR" <<'PY'
import json
from pathlib import Path
import sys
build=Path(sys.argv[1])
s=json.loads(Path('schema/lmdraw.dsl.json').read_text()); s['name']='different_schema_for_refusal'
(build/'wrong-schema.json').write_text(json.dumps(s))
PY
# Build one chunk against altered schema using a temporary source with an explicit schema path.
python3 - "$BUILD_DIR" <<'PY'
from pathlib import Path
import os,sys
build=Path(sys.argv[1])
schema=os.path.relpath(build/'wrong-schema.json', build)
s=Path('tests/zero-corner-radius.lms').read_text().replace('../schema/lmdraw.dsl.json',schema).replace('"build/zero-corner-radius.dslb"','ARGV[0]')
(build/'wrong-schema.lms').write_text(s)
PY
"$LMSCRIPT" "$BUILD_DIR/wrong-schema.lms" "$BUILD_DIR/wrong-schema.dslb" >/dev/null || fail "wrong-schema fixture compile"
set +e
"$HOST" --dsl "$BUILD_DIR/wrong-schema.dslb" > /dev/null 2> "$BUILD_DIR/wrong-schema.err"; code=$?
set -e
if [[ $code != 3 ]] || ! grep -q 'schema mismatch' "$BUILD_DIR/wrong-schema.err"; then fail "wrong schema refusal (exit $code)"; fi
ok "wrong schema bytes refused with exit 3"
"$LMSCRIPT" tests/refusals/prevalidate.lms "$BUILD_DIR/prevalidate.dslb" >/dev/null || fail "prevalidation fixture compile"
python3 - <<'PY2'
from pathlib import Path
Path('build/rendered/prevalidation.png').unlink(missing_ok=True)
PY2
set +e
"$HOST" --dsl "$BUILD_DIR/prevalidate.dslb" "$BUILD_DIR/wrong-schema.dslb" >/dev/null 2>&1; code=$?
set -e
[[ $code == 3 && ! -e build/rendered/prevalidation.png ]] || fail "later invalid chunk ran earlier side effects"
ok "every bytecode file validates before the first block runs"
"$LMSCRIPT" tests/refusals/stale-node.lms "$BUILD_DIR/stale-node.dslb" >/dev/null || fail "stale-handle fixture compile"
set +e
"$HOST" --dsl "$BUILD_DIR/stale-node.dslb" >/dev/null 2> "$BUILD_DIR/stale-node.err"; code=$?
set -e
if [[ $code != 3 ]] || ! grep -q 'handle is stale' "$BUILD_DIR/stale-node.err"; then fail "stale handle rejection (exit $code)"; fi
ok "stale node handles fail safely after scene reset"

"$LMSCRIPT" tests/refusals/outside-output.lms "$BUILD_DIR/outside-output.dslb" >/dev/null || fail "outside-path fixture compile"
set +e
"$HOST" --dsl "$BUILD_DIR/outside-output.dslb" >/dev/null 2> "$BUILD_DIR/outside-output.err"; code=$?
set -e
if [[ $code != 3 ]] || ! grep -q 'output traversal is refused' "$BUILD_DIR/outside-output.err"; then fail "output path refusal (exit $code)"; fi
ok "output path escape refused"
"$LMSCRIPT" tests/refusals/refused-svg.lms "$BUILD_DIR/refused-svg.dslb" >/dev/null || fail "SVG refusal fixture compile"
set +e
"$HOST" --dsl "$BUILD_DIR/refused-svg.dslb" >/dev/null 2> "$BUILD_DIR/refused-svg.err"; code=$?
set -e
if [[ $code != 3 ]] || ! grep -q 'outside the subset' "$BUILD_DIR/refused-svg.err"; then fail "unsupported SVG refusal (exit $code)"; fi
ok "unsupported SVG presentation features refused"

if [[ "$(uname -s)" == Linux* && "$BUILD_DIR" != *sanitize* ]]; then
  if command -v readelf >/dev/null 2>&1; then
    readelf -d "$HOST" | sed -n 's/.*NEEDED.*\[\(.*\)\]/\1/p' > "$BUILD_DIR/needed.txt"
  else
    ldd "$HOST" | sed -n 's/^[[:space:]]*\([^[:space:]]*\) =>.*/\1/p' > "$BUILD_DIR/needed.txt"
  fi
  bad=""
  while IFS= read -r lib; do
    case "$lib" in
      libc.so.6|libm.so.6|libpthread.so.0|libdl.so.2|librt.so.1|ld-linux*) ;;
      *) bad="$bad $lib";;
    esac
  done < "$BUILD_DIR/needed.txt"
  [[ -z "$bad" ]] || fail "unexpected dynamic dependencies:$bad"
  [[ -s "$BUILD_DIR/needed.txt" ]] || fail "no dynamic dependencies found; glibc must stay dynamic"
  ok "Linux binary is dynamic only against the glibc runtime"
elif [[ "$(uname -s)" == Darwin* ]]; then
  otool -L "$HOST" > "$BUILD_DIR/otool.txt"
  if grep -E '(@rpath|/opt/homebrew|/usr/local)' "$BUILD_DIR/otool.txt"; then fail "non-system dynamic dependency"; fi
  ok "macOS binary links only platform libraries"
fi
printf 'PASS: %s checks\n' "$ok_count"
