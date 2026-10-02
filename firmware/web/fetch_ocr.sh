#!/usr/bin/env bash
# Fetch the OCR runtime + models into vendor/ — not committed to git (large
# binaries); run once for local dev and once in the Pages workflow.
#   usage: web/fetch_ocr.sh
#
# vendored layout:
#   vendor/ort/  onnxruntime-web 1.30.0 webgpu+wasm ESM build
#                (ort.webgpu.min.mjs lazily loads the asyncify wasm which
#                carries both wasm-EP and webgpu-EP kernels)
#   vendor/ocr/  PP-OCRv4 mobile det/rec ONNX (RapidOCR mirrors on HF)
#                + ppocr_keys_v1.txt CTC dictionary (PaddleOCR, Apache-2.0)

set -euo pipefail
cd "$(dirname "$0")"

ORT_VER=1.30.0
HF=https://huggingface.co/SWHL/RapidOCR/resolve/main
PD=https://raw.githubusercontent.com/PaddlePaddle/PaddleOCR/main

dl() { # dl <url> <dest>
    local url=$1 dest=$2
    if [ -f "$dest" ]; then echo "have $dest"; return; fi
    echo "  -> $dest"
    curl -fL --retry 3 --connect-timeout 15 -o "$dest.tmp" "$url"
    mv "$dest.tmp" "$dest"
}

mkdir -p vendor/ort vendor/ocr /tmp/ort-pkg
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

dl "$HF/PP-OCRv4/ch_PP-OCRv4_det_infer.onnx" vendor/ocr/det.onnx
dl "$HF/PP-OCRv4/ch_PP-OCRv4_rec_infer.onnx" vendor/ocr/rec.onnx
dl "$PD/ppocr/utils/ppocr_keys_v1.txt"      vendor/ocr/keys.txt
echo "ocr:"; ls -la vendor/ocr/
