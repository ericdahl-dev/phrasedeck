#!/bin/sh
# Builds the Mac Helper (#66) into OUT/phrased-<version>-macos.zip: one universal phrased (arm64
# and x86_64; cgo, for CGEventPost). With MAC_SIGN_IDENTITY set (a "Developer ID Application: ..."
# identity in the keychain) it's signed with the hardened runtime and a secure timestamp, ready for
# notarytool. Unset, it stays unsigned, as in pull-request CI. Usage: build-mac.sh VERSION OUT
set -eu
VERSION=$1 OUT=$2
here=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT

# Go 1.25 needs macOS 12, so that's the floor for both slices.
for arch in arm64 amd64; do
	(cd "$here/.." && CGO_ENABLED=1 GOOS=darwin GOARCH="$arch" MACOSX_DEPLOYMENT_TARGET=12.0 \
		go build -trimpath -o "$stage/phrased-$arch" ./cmd/phrased)
done
lipo -create -output "$stage/phrased" "$stage/phrased-arm64" "$stage/phrased-amd64"

if [ -n "${MAC_SIGN_IDENTITY:-}" ]; then
	# The fixed identifier keeps the Accessibility grant across upgrades.
	codesign --force --options runtime --timestamp --sign "$MAC_SIGN_IDENTITY" \
		--identifier dev.ericdahl.phrased "$stage/phrased"
fi
ditto -c -k "$stage/phrased" "$OUT/phrased-$VERSION-macos.zip"
