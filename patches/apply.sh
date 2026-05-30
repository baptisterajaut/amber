#!/bin/bash
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT" || exit 1
echo "Applying patches/changes.patch to Amber Video Editor..."
if git apply --check patches/changes.patch >/dev/null 2>&1; then
  git apply patches/changes.patch
  echo "Success: Patch applied successfully!"
else
  echo "git apply check failed. Attempting fallback via 'patch' command..."
  if patch -p1 --dry-run < patches/changes.patch >/dev/null 2>&1; then
    patch -p1 < patches/changes.patch
    echo "Success: Patch applied successfully via fallback!"
  else
    echo "Error: Patch failed to apply cleanly."
    exit 1
  fi
fi
