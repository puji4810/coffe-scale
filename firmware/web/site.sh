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

mkdir -p "$OUT/vendor" "$OUT/dist"
cp index.html app.js ble.js chart.js .nojekyll "$OUT/"
cp vendor/uPlot.iife.min.js vendor/uPlot.min.css vendor/logo.woff2 "$OUT/vendor/"
cp dist/scale_screen.mjs dist/scale_screen.wasm "$OUT/dist/"

echo "site -> $OUT"
find "$OUT" -type f | sort
