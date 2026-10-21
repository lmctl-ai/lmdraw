#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
dest="${1:?usage: scripts/stage-release-notices.sh DEST [BINARY] }"
mkdir -p "$dest/licenses"
cp "$root/LICENSE" "$root/THIRD_PARTY_NOTICES" "$dest/"
cp "$root/licenses/"*.txt "$dest/licenses/"
cp "$root/licenses/STATIC-RELINKING.md" "$dest/licenses/"
if [[ -n "${2:-}" ]]; then
  bin="$2"
  name="$(basename "$bin")"
  if command -v sha256sum >/dev/null 2>&1; then sha256sum "$bin" > "$dest/$name.sha256"
  else shasum -a 256 "$bin" > "$dest/$name.sha256"; fi
fi
