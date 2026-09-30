#!/usr/bin/env bash
# Deploy firmware/web/ as a static site on the gh-pages branch.
# The directory is fully self-contained: index.html + app.js + chart.js +
# vendor/uPlot + dist/*.mjs/.wasm — same files the device serves.
#
# One-time setup: commit the repo, then in GitHub → Settings → Pages →
# "Deploy from a branch" → gh-pages / (root).
set -euo pipefail
cd "$(dirname "$0")"
./wasm/build.sh        # ensure dist/ is fresh before publishing
cd "$(git rev-parse --show-toplevel)"

# everything under web/ must be committed for subtree to see it
if ! git diff --quiet -- firmware/web || \
   [ -n "$(git status --porcelain -- firmware/web)" ]; then
    echo "commit firmware/web first" >&2; exit 1
fi
git subtree push --prefix firmware/web origin gh-pages
