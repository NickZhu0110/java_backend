#!/usr/bin/env bash
set -euo pipefail

export JAVA_HOME="${JAVA_HOME:-/usr/lib/jvm/zulu21-ca-amd64}"
export PATH="/opt/kafka/bin:$PATH"

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SEGMENTCACS_SRC="${SEGMENTCACS_SRC:-/root/autodl-tmp/SEGMENT-CACS/src}"
MODEL_PATH="${MODEL_PATH:-/root/autodl-tmp/SEGMENT-CACS/data/model/SegmentCACS_0001619_unet.pt}"

log() {
  printf '[server-check] %s\n' "$*"
}

warn() {
  printf '[server-check] WARN: %s\n' "$*" >&2
}

check_cmd() {
  local name="$1"
  if command -v "$name" >/dev/null 2>&1; then
    printf '[server-check] %-18s FOUND %s\n' "$name" "$(command -v "$name")"
    return 0
  fi
  printf '[server-check] %-18s MISSING\n' "$name"
  return 1
}

choose_data_root() {
  if mkdir -p /data >/dev/null 2>&1 && [ -w /data ]; then
    echo /data
  else
    echo "$HOME/autodl-tmp/data"
  fi
}

create_runtime_dirs() {
  local data_root="$1"
  log "runtime data root: $data_root"
  mkdir -p \
    "$data_root/cac/cases" \
    "$data_root/cac/jobs" \
    "$data_root/cac/masks" \
    "$data_root/cac/reports" \
    "$data_root/cac/results" \
    "$data_root/models"

  log "runtime directories:"
  printf '  %s\n' \
    "$data_root/cac/cases" \
    "$data_root/cac/jobs" \
    "$data_root/cac/masks" \
    "$data_root/cac/reports" \
    "$data_root/cac/results" \
    "$data_root/models"
}

log "repo root: $ROOT_DIR"
log "OS info:"
if [ -f /etc/os-release ]; then
  cat /etc/os-release | sed 's/^/[server-check]   /'
else
  uname -a | sed 's/^/[server-check]   /'
fi

log "tool checks:"
check_cmd java || true
if command -v java >/dev/null 2>&1; then java -version 2>&1 | sed 's/^/[server-check]   /'; fi
check_cmd mvn || true
if [ -x "$ROOT_DIR/cac-backend/mvnw" ]; then
  log "maven wrapper FOUND: cac-backend/mvnw"
else
  warn "maven wrapper missing or not executable: cac-backend/mvnw"
fi
check_cmd python3 || true
if command -v python3 >/dev/null 2>&1; then python3 --version 2>&1 | sed 's/^/[server-check]   /'; fi
check_cmd pip3 || true
check_cmd psql || true
check_cmd postgres || true
check_cmd redis-cli || true
check_cmd redis-server || true
check_cmd kafka-topics.sh || true
check_cmd kafka-server-start.sh || true

log "process checks:"
ps aux | grep -E '[p]ostgres|[r]edis|[k]afka|[z]ookeeper' || true

log "port 8080 check:"
if command -v ss >/dev/null 2>&1; then
  ss -ltnp | grep ':8080 ' || log "port 8080 not listening"
elif command -v netstat >/dev/null 2>&1; then
  netstat -ltnp 2>/dev/null | grep ':8080 ' || log "port 8080 not listening"
else
  warn "ss/netstat not found; cannot check port 8080"
fi

log "SEGMENT-CACS path check: $SEGMENTCACS_SRC"
if [ -e "$SEGMENTCACS_SRC" ]; then
  log "SEGMENT-CACS path exists"
else
  warn "SEGMENT-CACS path missing"
fi

log "model path check: $MODEL_PATH"
if [ -f "$MODEL_PATH" ]; then
  log "model path exists"
else
  warn "model path missing"
fi

DATA_ROOT="$(choose_data_root)"
create_runtime_dirs "$DATA_ROOT"

log "done"
