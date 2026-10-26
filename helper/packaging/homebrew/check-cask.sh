#!/bin/sh
# Checks the rendered Homebrew cask (#66): valid Ruby, the version and the zip's SHA-256 filled in,
# and a URL that names this release's zip. Usage: check-cask.sh CASK VERSION ZIP
set -eu
CASK=$1 VERSION=$2 ZIP=$3
fail() { echo "check-cask: $*" >&2; exit 1; }
ruby -c "$CASK" >/dev/null || fail "not valid Ruby"
grep -q '@[A-Z0-9_]*@' "$CASK" && fail "placeholders left: $(grep -o '@[A-Z0-9_]*@' "$CASK" | tr '\n' ' ')"
grep -q "^  version \"$VERSION\"$" "$CASK" || fail "version isn't $VERSION"
sum=$(shasum -a 256 "$ZIP" | cut -d' ' -f1)
grep -q "^  sha256 \"$sum\"$" "$CASK" || fail "sha256 isn't the zip's ($sum)"
grep -q 'releases/download/v#{version}/phrased-#{version}-macos.zip' "$CASK" || fail "url doesn't name the release zip"
grep -q '^  binary "phrased"$' "$CASK" || fail "doesn't install the phrased binary"
echo "check-cask: ok"
