#!/usr/bin/env bash
# Archives Phrasedeck and uploads it to TestFlight (internal testers only).
#   ios/scripts/testflight.sh             archive and upload
#   ios/scripts/testflight.sh --no-upload archive and write the .ipa to ios/build/export
# Needs ASC_KEY_ID, ASC_ISSUER_ID and ASC_KEY_PATH (the .p8 file) in the environment.
set -euo pipefail

cd "$(dirname "$0")/.."
TEAM=5HR8E5CWR7
# Always grows: UTC date and time to the minute, one integer (CFBundleVersion).
BUILD_NUMBER="${BUILD_NUMBER:-$(date -u +%Y%m%d%H%M)}"
ARCHIVE=build/Phrasedeck.xcarchive
OPTIONS=ExportOptions.plist

: "${ASC_KEY_ID:?set ASC_KEY_ID (App Store Connect API key ID)}"
: "${ASC_ISSUER_ID:?set ASC_ISSUER_ID (App Store Connect issuer ID)}"
: "${ASC_KEY_PATH:?set ASC_KEY_PATH (path to AuthKey_<id>.p8)}"
[ -f "$ASC_KEY_PATH" ] || { echo "no key file at $ASC_KEY_PATH" >&2; exit 1; }
AUTH=(-allowProvisioningUpdates
  -authenticationKeyPath "$ASC_KEY_PATH"
  -authenticationKeyID "$ASC_KEY_ID"
  -authenticationKeyIssuerID "$ASC_ISSUER_ID")

if [ "${1:-}" = --no-upload ]; then
  mkdir -p build
  OPTIONS=build/ExportOptions-export.plist
  cp ExportOptions.plist "$OPTIONS"
  plutil -replace destination -string export "$OPTIONS"
fi

echo "Phrasedeck build $BUILD_NUMBER"
xcodegen generate
rm -rf "$ARCHIVE"
xcodebuild archive \
  -project Phrasedeck.xcodeproj -scheme Phrasedeck -configuration Release \
  -destination generic/platform=iOS -archivePath "$ARCHIVE" \
  DEVELOPMENT_TEAM="$TEAM" CODE_SIGN_STYLE=Automatic \
  CURRENT_PROJECT_VERSION="$BUILD_NUMBER" \
  "${AUTH[@]}"
xcodebuild -exportArchive \
  -archivePath "$ARCHIVE" -exportOptionsPlist "$OPTIONS" -exportPath build/export \
  "${AUTH[@]}"

if [ "$OPTIONS" = ExportOptions.plist ]; then
  echo "Uploaded build $BUILD_NUMBER. It shows in TestFlight once Apple has processed it (5-15 minutes)."
else
  echo "Wrote build/export/Phrasedeck.ipa (build $BUILD_NUMBER). Not uploaded."
fi
