#!/bin/sh
# Builds the Linux Helper packages (#78) into OUT: phrased_<version>_<arch>.deb and
# phrased-<version>-linux-<arch>.tar.gz (for AUR phrased-bin). Usage: build-linux.sh VERSION ARCH OUT
# ARCH is a Go arch (amd64, arm64). Needs go and nfpm.
set -eu
VERSION=$1 ARCH=$2 OUT=$3
here=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT

(cd "$here/.." && CGO_ENABLED=0 GOOS=linux GOARCH="$ARCH" go build -trimpath -o "$stage/phrased" ./cmd/phrased)
cp "$here/phrased.service" "$stage/"
tar -C "$stage" -czf "$OUT/phrased-$VERSION-linux-$ARCH.tar.gz" phrased phrased.service

cd "$stage"
VERSION="$VERSION" ARCH="$ARCH" nfpm package --config "$here/nfpm.yaml" --packager deb --target "$OUT/phrased_${VERSION}_$ARCH.deb"
