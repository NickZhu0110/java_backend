#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BACKEND_DIR="$ROOT_DIR/cac-backend"
WORKER_DIR="$ROOT_DIR/python-worker"
LOG_DIR="$ROOT_DIR/logs"
PID_DIR="$ROOT_DIR/.dev-pids"

DB_NAME="${DB_NAME:-cac_platform}"
DB_USER="${DB_USER:-nick}"
DB_MAINTENANCE_NAME="${DB_MAINTENANCE_NAME:-postgres}"
REDIS_KEY_PATTERN="${REDIS_KEY_PATTERN:-job:*}"
KAFKA_BOOTSTRAP_SERVERS="${KAFKA_BOOTSTRAP_SERVERS:-localhost:9092}"
KAFKA_TOPIC="${KAFKA_TOPIC:-cac.analysis.requested}"
KAFKA_GROUP_ID="${KAFKA_GROUP_ID:-segment-cacs-worker-dev}"
BACKEND_PORT="${BACKEND_PORT:-8080}"

CONFIRM="false"
START_AFTER_RESET="false"

usage() {
  cat <<EOF
Usage:
  scripts/dev-reset.sh --yes [--start]

What this resets:
  - Stops local backend/worker processes for this project
  - Kills any process listening on port $BACKEND_PORT
  - Drops and recreates PostgreSQL database: $DB_NAME
  - Deletes Redis keys matching: $REDIS_KEY_PATTERN
  - Deletes and recreates Kafka topic: $KAFKA_TOPIC
  - Deletes Kafka consumer group if possible: $KAFKA_GROUP_ID

Options:
  --yes      Required. Confirms destructive local reset.
  --start    After reset, start Spring Boot backend and Python worker in background.
  --help     Show this help.

Environment overrides:
  DB_NAME, DB_USER, DB_MAINTENANCE_NAME, REDIS_KEY_PATTERN,
  KAFKA_BOOTSTRAP_SERVERS, KAFKA_TOPIC, KAFKA_GROUP_ID, BACKEND_PORT
EOF
}

log() {
  printf '[dev-reset] %s\n' "$*"
}

warn() {
  printf '[dev-reset] WARN: %s\n' "$*" >&2
}

need_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    warn "missing command: $1"
    return 1
  fi
}

kafka_cmd() {
  local name="$1"

  if command -v "$name" >/dev/null 2>&1; then
    command -v "$name"
    return 0
  fi

  if [[ -n "${KAFKA_HOME:-}" && -x "$KAFKA_HOME/bin/$name" ]]; then
    printf '%s\n' "$KAFKA_HOME/bin/$name"
    return 0
  fi

  return 1
}

stop_pid_file() {
  local label="$1"
  local file="$2"

  if [[ ! -f "$file" ]]; then
    return 0
  fi

  local pid
  pid="$(cat "$file")"

  if [[ -n "$pid" ]] && kill -0 "$pid" >/dev/null 2>&1; then
    log "stopping $label pid=$pid"
    kill "$pid" >/dev/null 2>&1 || true
    sleep 2

    if kill -0 "$pid" >/dev/null 2>&1; then
      warn "$label pid=$pid still running; forcing stop"
      kill -9 "$pid" >/dev/null 2>&1 || true
    fi
  fi

  rm -f "$file"
}

stop_processes() {
  mkdir -p "$PID_DIR"

  stop_pid_file "backend" "$PID_DIR/backend.pid"
  stop_pid_file "worker" "$PID_DIR/worker.pid"

  if command -v lsof >/dev/null 2>&1; then
    local pids
    pids="$(lsof -tiTCP:"$BACKEND_PORT" -sTCP:LISTEN || true)"

    if [[ -n "$pids" ]]; then
      log "stopping process(es) listening on port $BACKEND_PORT: $pids"
      # shellcheck disable=SC2086
      kill $pids >/dev/null 2>&1 || true
      sleep 2

      local remaining
      remaining="$(lsof -tiTCP:"$BACKEND_PORT" -sTCP:LISTEN || true)"
      if [[ -n "$remaining" ]]; then
        warn "port $BACKEND_PORT still busy; forcing stop: $remaining"
        # shellcheck disable=SC2086
        kill -9 $remaining >/dev/null 2>&1 || true
      fi
    fi
  else
    warn "lsof not found; skipping port cleanup"
  fi

  if command -v pgrep >/dev/null 2>&1; then
    local worker_pids
    worker_pids="$(pgrep -f "$WORKER_DIR/worker.py" || true)"

    if [[ -n "$worker_pids" ]]; then
      log "stopping python worker process(es): $worker_pids"
      # shellcheck disable=SC2086
      kill $worker_pids >/dev/null 2>&1 || true
    fi
  fi
}

reset_postgres() {
  need_cmd psql >/dev/null
  need_cmd dropdb >/dev/null
  need_cmd createdb >/dev/null

  log "resetting PostgreSQL database: $DB_NAME"
  psql -U "$DB_USER" -d "$DB_MAINTENANCE_NAME" -v ON_ERROR_STOP=1 -c \
    "SELECT pg_terminate_backend(pid) FROM pg_stat_activity WHERE datname = '$DB_NAME' AND pid <> pg_backend_pid();" >/dev/null

  dropdb -U "$DB_USER" --if-exists "$DB_NAME"
  createdb -U "$DB_USER" "$DB_NAME"
}

reset_redis() {
  need_cmd redis-cli >/dev/null

  log "deleting Redis keys matching: $REDIS_KEY_PATTERN"
  local deleted=0

  while IFS= read -r key; do
    [[ -z "$key" ]] && continue
    redis-cli del "$key" >/dev/null
    deleted=$((deleted + 1))
  done < <(redis-cli --scan --pattern "$REDIS_KEY_PATTERN")

  log "deleted Redis keys: $deleted"
}

reset_kafka() {
  local kafka_topics
  local kafka_groups

  kafka_topics="$(kafka_cmd kafka-topics || true)"
  kafka_groups="$(kafka_cmd kafka-consumer-groups || true)"

  if [[ -z "$kafka_topics" ]]; then
    warn "kafka-topics not found; skipping Kafka topic reset"
    warn "Add Kafka bin to PATH or set KAFKA_HOME."
    return 0
  fi

  log "resetting Kafka topic: $KAFKA_TOPIC"
  "$kafka_topics" --bootstrap-server "$KAFKA_BOOTSTRAP_SERVERS" \
    --delete --topic "$KAFKA_TOPIC" >/dev/null 2>&1 || true

  for _ in {1..20}; do
    if ! "$kafka_topics" --bootstrap-server "$KAFKA_BOOTSTRAP_SERVERS" \
      --list | grep -qx "$KAFKA_TOPIC"; then
      break
    fi
    sleep 1
  done

  "$kafka_topics" --bootstrap-server "$KAFKA_BOOTSTRAP_SERVERS" \
    --create --if-not-exists --topic "$KAFKA_TOPIC" \
    --partitions 1 --replication-factor 1 >/dev/null

  if [[ -n "$kafka_groups" ]]; then
    log "deleting Kafka consumer group if possible: $KAFKA_GROUP_ID"
    "$kafka_groups" --bootstrap-server "$KAFKA_BOOTSTRAP_SERVERS" \
      --delete --group "$KAFKA_GROUP_ID" >/dev/null 2>&1 || true
  fi
}

start_backend() {
  mkdir -p "$LOG_DIR" "$PID_DIR"

  log "starting Spring Boot backend on port $BACKEND_PORT"
  (
    cd "$BACKEND_DIR"
    ./mvnw spring-boot:run -Dspring-boot.run.arguments="--server.port=$BACKEND_PORT"
  ) >"$LOG_DIR/backend.log" 2>&1 &

  echo $! >"$PID_DIR/backend.pid"
  log "backend pid=$(cat "$PID_DIR/backend.pid"), log=$LOG_DIR/backend.log"
}

start_worker() {
  mkdir -p "$LOG_DIR" "$PID_DIR"

  if [[ ! -d "$WORKER_DIR/.venv" ]]; then
    log "creating python worker virtualenv"
    python3 -m venv "$WORKER_DIR/.venv"
  fi

  log "installing python worker requirements"
  "$WORKER_DIR/.venv/bin/pip" install -r "$WORKER_DIR/requirements.txt" >/dev/null

  log "starting python worker"
  (
    cd "$WORKER_DIR"
    source .venv/bin/activate
    BACKEND_BASE_URL="http://localhost:$BACKEND_PORT" \
      KAFKA_BOOTSTRAP_SERVERS="$KAFKA_BOOTSTRAP_SERVERS" \
      KAFKA_TOPIC="$KAFKA_TOPIC" \
      KAFKA_GROUP_ID="$KAFKA_GROUP_ID" \
      python worker.py
  ) >"$LOG_DIR/worker.log" 2>&1 &

  echo $! >"$PID_DIR/worker.pid"
  log "worker pid=$(cat "$PID_DIR/worker.pid"), log=$LOG_DIR/worker.log"
}

for arg in "$@"; do
  case "$arg" in
    --yes)
      CONFIRM="true"
      ;;
    --start)
      START_AFTER_RESET="true"
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    *)
      warn "unknown argument: $arg"
      usage
      exit 1
      ;;
  esac
done

if [[ "$CONFIRM" != "true" ]]; then
  usage
  echo
  warn "refusing to reset without --yes"
  exit 1
fi

log "project root: $ROOT_DIR"
stop_processes
reset_postgres
reset_redis
reset_kafka

if [[ "$START_AFTER_RESET" == "true" ]]; then
  start_backend
  start_worker
fi

log "done"
