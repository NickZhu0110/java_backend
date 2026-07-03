#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORKER_DIR="$ROOT_DIR/python-worker"

cd "$WORKER_DIR"

if [ -f .env ]; then
  echo "[run-worker] loading python-worker/.env"
  set -a
  # shellcheck disable=SC1091
  . .env
  set +a
else
  echo "[run-worker] warning: python-worker/.env missing; using worker defaults and environment variables"
fi

if [ ! -d .venv ]; then
  echo "[run-worker] error: python-worker/.venv missing. Create it with:"
  echo "  cd $WORKER_DIR && python3 -m venv .venv && source .venv/bin/activate && pip install -r requirements.txt"
  exit 1
fi

source .venv/bin/activate
exec python -u worker.py
