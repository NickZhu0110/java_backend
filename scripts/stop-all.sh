#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LOG_DIR="$ROOT_DIR/scripts/logs"
mkdir -p "$LOG_DIR"

log() { printf '[stop-all] %s\n' "$*"; }

stop_pid_file() {
  local name="$1"
  local pid_file="$2"
  if [ ! -f "$pid_file" ]; then
    log "$name skipped: no pid file"
    return 0
  fi
  local pid
  pid="$(cat "$pid_file" 2>/dev/null || true)"
  if [ -z "$pid" ] || ! kill -0 "$pid" >/dev/null 2>&1; then
    log "$name skipped: pid not running"
    rm -f "$pid_file"
    return 0
  fi
  log "stopping $name pid=$pid"
  kill "$pid" >/dev/null 2>&1 || true
  for _ in $(seq 1 10); do
    if ! kill -0 "$pid" >/dev/null 2>&1; then
      rm -f "$pid_file"
      log "$name stopped"
      return 0
    fi
    sleep 1
  done
  log "$name still running after SIGTERM; leaving it alone"
}

stop_pid_file 'Java backend' "$LOG_DIR/backend.pid"
stop_pid_file 'Python Worker' "$LOG_DIR/worker.pid"
stop_pid_file 'Redis started by this project' "$LOG_DIR/redis.pid"
stop_pid_file 'Kafka started by this project' "$LOG_DIR/kafka.pid"
log 'PostgreSQL skipped: not force-stopped by this script'
log 'done'
