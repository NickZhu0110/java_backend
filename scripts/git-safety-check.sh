#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

cd "$ROOT_DIR"

echo "[git-safety-check] scanning repository for suspicious files"
echo "[git-safety-check] this script does not delete anything"
echo

patterns=(
  "*.env"
  ".env"
  ".env.*"
  "*.pt"
  "*.pth"
  "*.onnx"
  "*.ckpt"
  "*.safetensors"
  "*DICOM*"
  "*dicom*"
  "data"
  "data/*"
  "datasets"
  "datasets/*"
  "outputs"
  "outputs/*"
  "results"
  "results/*"
  "reports"
  "reports/*"
  "masks"
  "masks/*"
  "storage"
  "storage/*"
  "qt-cac-app/build"
  "qt-cac-app/build/*"
  "cac-backend/target"
  "cac-backend/target/*"
  ".venv"
  ".venv/*"
  "python-worker/.venv"
  "python-worker/.venv/*"
)

found=0

is_allowed_template() {
  case "$1" in
    *.env.example|*/.env.example|*.example.ini|*/config.example.ini)
      return 0
      ;;
    *)
      return 1
      ;;
  esac
}

echo "[git-safety-check] tracked suspicious files:"
for pattern in "${patterns[@]}"; do
  while IFS= read -r path; do
    [[ -z "$path" ]] && continue
    is_allowed_template "$path" && continue
    echo "  tracked: $path"
    found=1
  done < <(git ls-files "$pattern")
done

echo
echo "[git-safety-check] untracked suspicious files:"
for pattern in "${patterns[@]}"; do
  while IFS= read -r path; do
    [[ -z "$path" ]] && continue
    is_allowed_template "$path" && continue
    echo "  untracked: $path"
    found=1
  done < <(git ls-files --others --exclude-standard "$pattern")
done

echo
if [[ "$found" -eq 0 ]]; then
  echo "[git-safety-check] OK: no suspicious tracked or untracked files found"
else
  echo "[git-safety-check] review the files above before committing"
  echo "[git-safety-check] if a generated file is already tracked, remove it from Git only:"
  echo "  git rm --cached <path>"
fi
