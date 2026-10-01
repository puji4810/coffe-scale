#!/usr/bin/env bash
# Assemble the static GitHub Pages site into <outdir> from the files that
# already exist — wasm/dist must be built first (web/wasm/build.sh).
#   usage: web/site.sh _site

set -euo pipefail
# Resolve OUT before cd — callers may pass a relative path from anywhere.
OUT=$(realpath -m "${1:?usage: site.sh <outdir>}")
cd "$(dirname "$0")"

for f in dist/scale_screen.mjs dist/scale_screen.wasm; do
    [ -f "$f" ] || { echo "missing $f — run web/wasm/build.sh first" >&2; exit 1; }
done

mkdir -p "$OUT/vendor" "$OUT/dist" "$OUT/icons"
cp index.html app.js ble.js chart.js store.js manifest.webmanifest sw.js .nojekyll "$OUT/"
cp vendor/uPlot.iife.min.js vendor/uPlot.min.css vendor/logo.woff2 \
   vendor/barlow-semi-condensed-latin-400-normal.woff2 \
   vendor/barlow-semi-condensed-latin-500-normal.woff2 \
   vendor/barlow-semi-condensed-latin-600-normal.woff2 "$OUT/vendor/"
cp icons/*.png "$OUT/icons/"
cp dist/scale_screen.mjs dist/scale_screen.wasm "$OUT/dist/"

# Stamp the service-worker cache name with a hash of everything it will
# precache — a changed asset => different sw.js => browser updates.
HASH=$(find "$OUT" -type f ! -name sw.js -print0 | sort -z |
       xargs -0 sha256sum | sha256sum | cut -c1-12)
sed -i "s/__BUILD__/$HASH/" "$OUT/sw.js"

echo "site -> $OUT (sw cache $HASH)"
find "$OUT" -type f | sort
