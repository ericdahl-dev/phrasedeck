#!/bin/sh
# Renders phrased.rb from the template for a release (#66). Usage: render.sh VERSION ZIP OUT
set -eu
VERSION=$1 ZIP=$2 OUT=$3
here=$(cd "$(dirname "$0")" && pwd)
sum=$(shasum -a 256 "$ZIP" | cut -d' ' -f1)
sed -e "s/@VERSION@/$VERSION/" -e "s/@SHA256@/$sum/" "$here/phrased.rb.in" > "$OUT"
