#!/bin/sh
# Runs in an archlinux container as root (#78): builds phrased-bin from DIR (render.sh's PKGBUILD,
# phrased-bin.install and the local release tarballs, so nothing is downloaded), then checks it.
# Usage: makepkg-check.sh DIR VERSION
set -eu
DIR=$1 VERSION=$2
here=$(cd "$(dirname "$0")" && pwd)
# pacman's sandbox fails under emulation (an arm64 Mac); harmless on a native runner.
grep -qx 'DisableSandbox' /etc/pacman.conf || sed -i '0,/^\[options\]/s//[options]\nDisableSandbox/' /etc/pacman.conf
pacman -Syu --noconfirm --needed base-devel >/dev/null
id builder >/dev/null 2>&1 || useradd -m builder
build=$(mktemp -d)
cp "$DIR/PKGBUILD" "$DIR/phrased-bin.install" "$DIR"/phrased-"$VERSION"-linux-*.tar.gz "$build"
chown -R builder "$build"
su builder -c "cd $build && PACKAGER='Eric Dahl <eric@skeyelab.com>' makepkg --nodeps -f && makepkg --printsrcinfo > .SRCINFO"
cp "$build/.SRCINFO" "$DIR/"
"$here/check-pkg.sh" "$build/phrased-bin-$VERSION-1-x86_64.pkg.tar.zst"
