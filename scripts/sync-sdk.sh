#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
sdk="${CPP_DSL_SDK:-$root/../cpp-dsl-sdk}"
if [[ "${1:-}" == "--verify" ]]; then
  expected="$(sed -n 's/^sha256=//p' "$root/sdk.lock")"
  actual="$(sha256sum "$root/sdk/dslvm.hpp" | cut -d' ' -f1)"
  [[ "$expected" == "$actual" ]] || { echo "SDK header hash mismatch; run scripts/sync-sdk.sh" >&2; exit 1; }
  echo "SDK header matches sdk.lock"
  exit 0
fi
commit="$(git -C "$sdk" rev-parse origin/main)"
git -C "$sdk" show "$commit:include/dslvm.hpp" > "$root/sdk/dslvm.hpp"
printf 'repo=https://github.com/lmctl-ai/cpp-dsl-sdk\ncommit=%s\nsha256=%s\n' "$commit" "$(sha256sum "$root/sdk/dslvm.hpp" | cut -d' ' -f1)" > "$root/sdk.lock"
echo "synced SDK $commit"
