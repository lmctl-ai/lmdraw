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
for image in examples/renders/*.png; do
  [[ -s "$image" && $(wc -c < "$image" | tr -d ' ') -lt 20480 ]] || fail "preview is missing or over 20 KiB: $image"
  check_png "$image" || fail "invalid preview PNG: $image"
done
ok "four example preview PNGs are valid and below 20 KiB"
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
[[ "$("$HOST" --version)" == "lmdraw 0.1.1" ]] || fail "--version does not report the CMake project version"
set +e
"$HOST" --version extra >/dev/null 2>&1; code=$?
set -e
[[ $code == 2 ]] || fail "--version extra argument exit $code, expected 2"
ok "--version prints lmdraw 0.1.1 and rejects extra arguments"
"$HOST" --schema --json > "$BUILD_DIR/embedded-schema.json"
cmp schema/lmdraw.dsl.json "$BUILD_DIR/embedded-schema.json" || fail "embedded schema bytes differ"
ok "--schema --json is byte-identical to schema/lmdraw.dsl.json"
"$HOST" --schema > "$BUILD_DIR/schema-summary.txt"
cmp tests/golden/schema-summary.txt "$BUILD_DIR/schema-summary.txt" || fail "schema summary differs from its golden"
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

for example in title-card node-tour render-assets animated-ladder; do
  "$LMSCRIPT" "examples/$example.lms" "$BUILD_DIR/$example.dslb" > /dev/null || fail "$example compile"
  python3 - "$BUILD_DIR/$example.dslb" <<'PY' || fail "example is not bytecode v5"
from pathlib import Path
import sys
assert Path(sys.argv[1]).read_bytes()[:6] == b'DSLB\x00\x05'
PY
done
ok "four examples compile to bytecode v5"
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
s=Path('examples/title-card.lms').read_text().replace('../schema/lmdraw.dsl.json',schema)
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
  ldd "$HOST" > "$BUILD_DIR/ldd.txt" 2>&1 || true
  grep -q 'not a dynamic executable' "$BUILD_DIR/ldd.txt" || fail "Linux binary is dynamically linked"
  ok "Linux binary is fully static"
elif [[ "$(uname -s)" == Darwin* ]]; then
  otool -L "$HOST" > "$BUILD_DIR/otool.txt"
  if grep -E '(@rpath|/opt/homebrew|/usr/local)' "$BUILD_DIR/otool.txt"; then fail "non-system dynamic dependency"; fi
  ok "macOS binary links only platform libraries"
fi
printf 'PASS: %s checks\n' "$ok_count"
