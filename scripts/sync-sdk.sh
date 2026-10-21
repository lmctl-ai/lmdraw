#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
sdk="${CPP_DSL_SDK:-$root/../cpp-dsl-sdk}"
sha256() {
  if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | cut -d' ' -f1
  else shasum -a 256 "$1" | cut -d' ' -f1; fi
}
if [[ "${1:-}" == "--verify" ]]; then
  expected="$(sed -n 's/^sha256=//p' "$root/sdk.lock")"
  actual="$(sha256 "$root/sdk/dslvm.hpp")"
  [[ "$expected" == "$actual" ]] || { echo "SDK header hash mismatch; run scripts/sync-sdk.sh" >&2; exit 1; }
  echo "SDK header matches sdk.lock"
  exit 0
fi
commit="$(git -C "$sdk" rev-parse origin/main)"
git -C "$sdk" show "$commit:include/dslvm.hpp" > "$root/sdk/dslvm.hpp"
printf 'repo=https://github.com/lmctl-ai/cpp-dsl-sdk\ncommit=%s\nsha256=%s\n' "$commit" "$(sha256 "$root/sdk/dslvm.hpp")" > "$root/sdk.lock"
echo "synced SDK $commit"
