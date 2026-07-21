#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

cd "${PROJECT_ROOT}"

if ! command -v cmake >/dev/null 2>&1; then
  echo "cmake was not found on PATH." >&2
  exit 1
fi

if ! command -v ninja >/dev/null 2>&1; then
  echo "ninja was not found on PATH. Install it with: brew install ninja" >&2
  exit 1
fi

if [[ ! -x /opt/homebrew/bin/qmake6 ]]; then
  echo "Homebrew qmake6 was not found at /opt/homebrew/bin/qmake6." >&2
  exit 1
fi

if [[ ! -d /opt/homebrew/lib/cmake/Qt6 ]]; then
  echo "Homebrew Qt6 CMake package was not found at /opt/homebrew/lib/cmake/Qt6." >&2
  exit 1
fi

if [[ ! -d /opt/homebrew/lib/cmake/vtk-9.6 ]]; then
  echo "Homebrew VTK CMake package was not found at /opt/homebrew/lib/cmake/vtk-9.6." >&2
  exit 1
fi

cmake --preset macos-homebrew-vtk-debug
cmake --build --preset macos-homebrew-vtk-debug-build
