#!/bin/sh
# Writes PKGBUILD and phrased-bin.install for phrased-bin into DIR, which holds both release tarballs from
# build-linux.sh (#78). Usage: render.sh VERSION DIR
set -eu
VERSION=$1 DIR=$2
here=$(cd "$(dirname "$0")" && pwd)
sum() { sha256sum "$DIR/phrased-$VERSION-linux-$1.tar.gz" | cut -d' ' -f1; }
sed -e "s/@VERSION@/$VERSION/" -e "s/@SHA256_AMD64@/$(sum amd64)/" -e "s/@SHA256_ARM64@/$(sum arm64)/" \
	"$here/PKGBUILD.in" > "$DIR/PKGBUILD"
cp "$here/phrased-bin.install" "$DIR/"
