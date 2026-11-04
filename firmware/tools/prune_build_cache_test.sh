#!/usr/bin/env bash
# Tests for prune_build_cache.sh: run it, exit 0 means pass.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
fail() { echo "FAIL: $*"; exit 1; }

cache=$tmp/cache
mkdir -p "$cache/ab" "$cache/cd"
echo old > "$cache/ab/stale.o"
echo used > "$cache/cd/used.o"
touch -d '2 hours ago' "$cache/ab/stale.o" "$cache/cd/used.o"
touch -d '1 hour ago' "$tmp/marker"
# The build retrieved used.o (SCons touches what it retrieves) and wrote new.o.
touch "$cache/cd/used.o"
echo new > "$cache/ab/new.o"

"$here/prune_build_cache.sh" "$cache" "$tmp/marker"

[[ ! -e $cache/ab/stale.o ]] || fail "an object the build didn't use is still there"
[[ -e $cache/cd/used.o ]] || fail "an object the build retrieved was removed"
[[ -e $cache/ab/new.o ]] || fail "an object the build wrote was removed"

# A cache that doesn't exist yet (the first run, or a failed restore) is fine.
"$here/prune_build_cache.sh" "$tmp/missing" "$tmp/marker" || fail "missing cache dir"

# Without a marker it refuses, instead of deleting the whole cache.
if "$here/prune_build_cache.sh" "$cache" "$tmp/no-marker" 2>/dev/null; then
    fail "ran without a marker"
fi
[[ -e $cache/cd/used.o ]] || fail "removed objects without a marker"

echo "prune_build_cache: ok"
