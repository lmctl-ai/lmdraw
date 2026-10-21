#!/usr/bin/env bash
# Select by emitting bytecode v5 against the schema used by the host.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [[ -n "${LMSCRIPT_BIN:-}" ]]; then printf '%s\n' "$LMSCRIPT_BIN"; exit 0; fi
candidate="$(command -v lmscript || true)"
work="$HERE/build/compiler-probe"
mkdir -p "$work"
cat > "$work/probe.lms" <<'PROBE'
p = _dsl_ schema="../../schema/lmdraw.dsl.json";
while (false) {}
end _dsl_
savedsl(p, "build/compiler-probe/probe.dslb");
PROBE
if [[ -n "$candidate" ]] &&
   "$candidate" --dsl-signatures "$HERE/schema/lmdraw.dsl.json" >/dev/null 2>&1 &&
   (cd "$HERE" && "$candidate" build/compiler-probe/probe.lms >/dev/null 2>&1) &&
   python3 - "$work/probe.dslb" <<'PY'
from pathlib import Path
import sys
p=Path(sys.argv[1]); sys.exit(0 if p.is_file() and p.read_bytes()[:6] == b'DSLB\x00\x05' else 1)
PY
then printf '%s\n' "$candidate"
else echo "error: no DSL v5 compiler found; install @lmctl-ai/lmctl or set LMSCRIPT_BIN" >&2; exit 2
fi
