#!/bin/bash
# Fetch the pinned Dynarmic and apply DH2Work's own patches to it.
#
#   fetch-dynarmic.sh [destination]
#
# Default destination: <repo>/third_party/dynarmic
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REPO="$(cd "$HERE/.." && pwd)"
PIN_FILE="$REPO/third_party/dynarmic.pin"
PATCH_DIR="$REPO/patches/dynarmic"
DEST="${1:-$REPO/third_party/dynarmic}"

if [ ! -f "$PIN_FILE" ]; then
  echo "fetch-dynarmic: missing $PIN_FILE" >&2
  exit 1
fi

REPOSITORY="$(sed -n 's/^repository=//p' "$PIN_FILE" | head -1)"
COMMIT="$(sed -n 's/^commit=//p' "$PIN_FILE" | head -1)"
if [ -z "$REPOSITORY" ] || [ -z "$COMMIT" ]; then
  echo "fetch-dynarmic: $PIN_FILE does not name a repository and a commit" >&2
  exit 1
fi

if [ ! -d "$DEST/.git" ]; then
  echo "fetch-dynarmic: cloning $REPOSITORY into $DEST"
  mkdir -p "$(dirname "$DEST")"
  git clone --quiet "$REPOSITORY" "$DEST"
fi

echo "fetch-dynarmic: checking out $COMMIT"
git -C "$DEST" fetch --quiet origin "$COMMIT" 2>/dev/null || true
git -C "$DEST" checkout --quiet "$COMMIT"
if [ -f "$DEST/.gitmodules" ]; then
  git -C "$DEST" submodule update --init --recursive --quiet
fi

# Our own patches, applied in name order. They are ours, not the upstream project's.
shopt -s nullglob
patches=("$PATCH_DIR"/*.patch)
if [ ${#patches[@]} -eq 0 ]; then
  echo "fetch-dynarmic: no patches under $PATCH_DIR (fetching pristine upstream)"
else
  for patch in "${patches[@]}"; do
    echo "fetch-dynarmic: applying $(basename "$patch")"
    git -C "$DEST" apply --check "$patch"
    git -C "$DEST" apply "$patch"
  done
fi

echo "fetch-dynarmic: ready at $DEST"
git -C "$DEST" --no-pager log --oneline -1
