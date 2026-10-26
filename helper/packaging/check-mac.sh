#!/bin/sh
# Checks the Mac Helper zip from build-mac.sh (#66): one universal phrased (arm64 and x86_64)
# inside. With --signed it also needs a Developer ID signature with the hardened runtime and a
# secure timestamp, which notarization requires. Usage: check-mac.sh ZIP [--signed]
set -eu
ZIP=$1 SIGNED=${2:-}
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
fail() { echo "check-mac: $*" >&2; exit 1; }

ditto -x -k "$ZIP" "$dir"
[ "$(ls "$dir")" = phrased ] || fail "the zip must hold only phrased, has: $(ls "$dir" | tr '\n' ' ')"
[ -x "$dir/phrased" ] || fail "phrased isn't executable"
archs=$(lipo -archs "$dir/phrased")
for a in arm64 x86_64; do
	case " $archs " in *" $a "*) ;; *) fail "phrased lacks $a (has: $archs)" ;; esac
done

if [ "$SIGNED" = --signed ]; then
	codesign --verify --strict "$dir/phrased" || fail "signature doesn't verify"
	info=$(codesign -dvv "$dir/phrased" 2>&1)
	echo "$info" | grep -q '^Identifier=dev.ericdahl.phrased$' || fail "identifier isn't dev.ericdahl.phrased"
	echo "$info" | grep -q '^Authority=Developer ID Application: ' || fail "not signed with a Developer ID Application certificate"
	echo "$info" | grep -q 'flags=.*(runtime)' || fail "hardened runtime is off"
	echo "$info" | grep -q '^Timestamp=' || fail "no secure timestamp"
fi
echo "check-mac: ok ($archs${SIGNED:+, signed})"
