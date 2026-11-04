#!/usr/bin/env bash
# Removes the objects one build didn't use from a PlatformIO (SCons) build cache, so CI's saved
# cache doesn't grow with every commit (#175). SCons touches each object it retrieves and writes
# new ones, so anything not newer than a marker made before the build went unused.
#
# Usage: prune_build_cache.sh CACHE_DIR MARKER    (touch MARKER before `pio run`)
set -euo pipefail
cache=$1 marker=$2
[[ -f $marker ]] || { echo "prune_build_cache: no marker file $marker" >&2; exit 2; }
[[ -d $cache ]] || exit 0
# SCons writes config when it makes a cache: without it, this is a wrong path, not a cache.
[[ -f $cache/config ]] || { echo "prune_build_cache: $cache has no SCons config file" >&2; exit 2; }
find "$cache" -type f ! -name config ! -newer "$marker" -delete
find "$cache" -mindepth 1 -type d -empty -delete
echo "prune_build_cache: $(find "$cache" -type f | wc -l) objects, $(du -sh "$cache" | cut -f1)"
