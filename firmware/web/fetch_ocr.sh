#!/usr/bin/env bash
# Fetch the OCR runtime + models into vendor/ — not committed to git (large
# binaries); run once for local dev and once in the Pages workflow.
#   usage: web/fetch_ocr.sh
#
# vendored layout:
#   vendor/ort/     onnxruntime-web 1.30.0 webgpu+wasm ESM build
#                   (ort.webgpu.min.mjs lazily loads the asyncify wasm which
#                   carries both wasm-EP and webgpu-EP kernels)
#   vendor/ocr/v5/  PP-OCRv5 mobile det/rec ONNX (official PaddlePaddle HF
#                   org) + ppocrv5_dict.txt, PLUS PP-OCRv4 rec + its dict as
#                   a lazily-fetched second opinion for weak lines.
#                   The directory name IS the version: bump v5→v6 to change
#                   the set — stale SW caches can never poison new models.
#   vendor/ocr/v5/manifest.json — byte sizes of every vendored file,
#                   generated below; the worker uses it for accurate
#                   download progress (content-length lies under gzip).
set -euo pipefail
cd "$(dirname "$0")"

ORT_VER=1.30.0
OCR_DIR=vendor/ocr/v5
HF=https://huggingface.co
PD=https://raw.githubusercontent.com/PaddlePaddle/PaddleOCR

dl() { # dl <url> <dest>
    local url=$1 dest=$2
    if [ -f "$dest" ]; then echo "have $dest"; return; fi
    echo "  -> $dest"
    curl -fL --retry 3 --connect-timeout 15 -o "$dest.tmp" "$url"
    mv "$dest.tmp" "$dest"
}

mkdir -p vendor/ort "$OCR_DIR" /tmp/ort-pkg
# drop stale versions and the legacy unversioned layout
for old in vendor/ocr/v*/; do
    [ "$old" = "$OCR_DIR/" ] || rm -rf "$old"
done
rm -f vendor/ocr/det.onnx vendor/ocr/rec.onnx vendor/ocr/keys.txt \
      vendor/ocr/manifest.json

if [ ! -f /tmp/ort-pkg/ort-$ORT_VER.tgz ]; then
    dl "https://registry.npmjs.org/onnxruntime-web/-/onnxruntime-web-$ORT_VER.tgz" \
       "/tmp/ort-pkg/ort-$ORT_VER.tgz"
fi
for f in ort.webgpu.min.mjs \
         ort-wasm-simd-threaded.asyncify.mjs \
         ort-wasm-simd-threaded.asyncify.wasm; do
    if [ ! -f "vendor/ort/$f" ]; then
        tar xzf "/tmp/ort-pkg/ort-$ORT_VER.tgz" -C /tmp/ort-pkg \
            "package/dist/$f"
        cp "/tmp/ort-pkg/package/dist/$f" "vendor/ort/$f"
    fi
done
echo "ort:"; ls -la vendor/ort/

# primary: PP-OCRv5 mobile (better on bold/display faces, current flagship)
dl "$HF/PaddlePaddle/PP-OCRv5_mobile_det_onnx/resolve/main/inference.onnx" \
   "$OCR_DIR/det.onnx"
dl "$HF/PaddlePaddle/PP-OCRv5_mobile_rec_onnx/resolve/main/inference.onnx" \
   "$OCR_DIR/rec.onnx"
dl "$PD/release/3.3/ppocr/utils/dict/ppocrv5_dict.txt" "$OCR_DIR/keys.txt"
# second opinion: PP-OCRv4 rec reads italic/decorative latin that v5 drops
dl "$HF/SWHL/RapidOCR/resolve/main/PP-OCRv4/ch_PP-OCRv4_rec_infer.onnx" \
   "$OCR_DIR/rec4.onnx"
dl "$PD/main/ppocr/utils/ppocr_keys_v1.txt" "$OCR_DIR/keys4.txt"

# real byte sizes for worker download progress — content-length under
# compression reports the transfer size, not the decoded bytes we count
{
    echo '{'
    first=1
    for f in vendor/ort/* "$OCR_DIR"/*; do
        [ -f "$f" ] || continue
        [ "$f" = "$OCR_DIR/manifest.json" ] && continue
        [ $first = 0 ] && printf ',\n'
        printf '  "%s": %s' "$f" "$(stat -c%s "$f")"
        first=0
    done
    printf '\n}\n'
} > "$OCR_DIR/manifest.json"
echo "ocr:"; ls -la "$OCR_DIR/"
