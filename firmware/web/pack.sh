#!/usr/bin/env bash
# Assemble web/root/ — the littlefs web root baked by idf.py via
# littlefs_create_partition_image (see main/CMakeLists.txt).
#
# Layout mirrors this dir so URLs are identical on device and when serving
# web/ locally for development:
#   /index.html  /app.js  /dist/scale_screen.{mjs,wasm}
#
# Every file ships both plain and .gz — the handler prefers .gz
# (Content-Encoding: gzip), plain stays for non-gzip clients and debugging.

set -euo pipefail
cd "$(dirname "$0")"

./wasm/build.sh            # dist/ must be fresh

ROOT=root
rm -rf "$ROOT"
mkdir -p "$ROOT/dist" "$ROOT/vendor"
cp index.html app.js chart.js "$ROOT/"
cp dist/scale_screen.mjs dist/scale_screen.wasm "$ROOT/dist/"
cp vendor/uPlot.iife.min.js vendor/uPlot.min.css vendor/logo.woff2 "$ROOT/vendor/"
find "$ROOT" -type f ! -name '*.gz' -exec gzip -9 -kf {} +

echo "--- web/root:"
find "$ROOT" -type f | sort
du -sh "$ROOT"
echo "packed — now: idf.py -B build-esp32s3 build flash"
