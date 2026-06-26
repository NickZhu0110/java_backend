#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LOG_DIR="$ROOT_DIR/scripts/logs"
TOPIC="${KAFKA_TOPIC:-cac.analysis.requested}"
KAFKA_HOME="${KAFKA_HOME:-}"
KAFKA_CONFIG="${KAFKA_CONFIG:-}"
mkdir -p "$LOG_DIR"

log() { printf '[start-all] %s\n' "$*"; }

is_pid_running() {
  local pid_file="$1"
  [ -f "$pid_file" ] || return 1
  local pid
  pid="$(cat "$pid_file" 2>/dev/null || true)"
  [ -n "$pid" ] && kill -0 "$pid" >/dev/null 2>&1
}

wait_for() {
  local name="$1"
  local command="$2"
  local attempts="${3:-30}"
  local delay="${4:-2}"
  for _ in $(seq 1 "$attempts"); do
    if bash -c "$command" >/dev/null 2>&1; then
      log "$name is ready"
      return 0
    fi
    sleep "$delay"
  done
  log "$name is not ready after $((attempts * delay))s"
  return 1
}

port_listening() {
  ss -lntp 2>/dev/null | grep -q ":$1 "
}

find_kafka_home() {
  if [ -n "$KAFKA_HOME" ] && [ -d "$KAFKA_HOME" ]; then
    echo "$KAFKA_HOME"
    return 0
  fi
  for dir in /opt/kafka /usr/local/kafka /root/kafka "$HOME/kafka" /root/autodl-tmp/kafka; do
    if [ -d "$dir" ]; then
      echo "$dir"
      return 0
    fi
  done
  return 1
}

setup_kafka_path() {
  local home
  home="$(find_kafka_home || true)"
  if [ -n "$home" ]; then
    export KAFKA_HOME="$home"
    export PATH="$KAFKA_HOME/bin:$PATH"
  fi
}

kafka_cmd() {
  local name="$1"
  if command -v "$name" >/dev/null 2>&1; then
    command -v "$name"
    return 0
  fi
  if command -v "$name.sh" >/dev/null 2>&1; then
    command -v "$name.sh"
    return 0
  fi
  return 1
}

start_postgres() {
  if port_listening 5432 || ps aux | grep '[p]ostgres' >/dev/null 2>&1; then
    log 'PostgreSQL already running'
    return 0
  fi

  log 'starting PostgreSQL'
  if command -v pg_ctlcluster >/dev/null 2>&1; then
    pg_ctlcluster 10 main start > "$LOG_DIR/postgres.log" 2>&1 || true
  elif command -v service >/dev/null 2>&1; then
    service postgresql start > "$LOG_DIR/postgres.log" 2>&1 || true
  elif command -v pg_ctl >/dev/null 2>&1; then
    for data_dir in /var/lib/postgresql/data /var/lib/postgresql/10/main "$HOME/postgres-data"; do
      if [ -d "$data_dir" ]; then
        pg_ctl -D "$data_dir" -l "$LOG_DIR/postgres.log" start || true
        break
      fi
    done
  fi

  if port_listening 5432 || ps aux | grep '[p]ostgres' >/dev/null 2>&1; then
    log 'PostgreSQL started'
  else
    log 'PostgreSQL not started automatically. Please start it manually.'
  fi
}

start_redis() {
  if command -v redis-cli >/dev/null 2>&1 && redis-cli ping 2>/dev/null | grep -q PONG; then
    log 'Redis already running'
    return 0
  fi

  if ! command -v redis-server >/dev/null 2>&1; then
    log 'Redis not started automatically. redis-server command is missing.'
    return 0
  fi

  log 'starting Redis'
  nohup redis-server --dir "$LOG_DIR" --dbfilename redis-dump.rdb > "$LOG_DIR/redis.log" 2>&1 &
  echo $! > "$LOG_DIR/redis.pid"
  wait_for Redis "redis-cli ping | grep -q PONG" 15 1 || true
}

prepare_kafka_config() {
  if [ -n "$KAFKA_CONFIG" ] && [ -f "$KAFKA_CONFIG" ]; then
    echo "$KAFKA_CONFIG"
    return 0
  fi

  for cfg in \
    /data/kafka/server.properties \
    "$KAFKA_HOME/config/kraft/server.properties" \
    "$KAFKA_HOME/config/server.properties" \
    /usr/local/kafka/config/kraft/server.properties \
    /usr/local/kafka/config/server.properties \
    /root/kafka/config/kraft/server.properties \
    /root/kafka/config/server.properties \
    /root/autodl-tmp/kafka/config/kraft/server.properties \
    /root/autodl-tmp/kafka/config/server.properties; do
    if [ -f "$cfg" ]; then
      echo "$cfg"
      return 0
    fi
  done
  return 1
}

format_kraft_if_needed() {
  local cfg="$1"
  grep -q '^process.roles=' "$cfg" 2>/dev/null || return 0
  local log_dirs
  log_dirs="$(grep '^log.dirs=' "$cfg" | head -1 | cut -d= -f2- || true)"
  [ -n "$log_dirs" ] || return 0
  local first_log_dir="${log_dirs%%,*}"
  mkdir -p "$first_log_dir"
  if [ ! -f "$first_log_dir/meta.properties" ]; then
    local storage_cmd
    storage_cmd="$(kafka_cmd kafka-storage || true)"
    if [ -n "$storage_cmd" ]; then
      log "formatting Kafka KRaft storage at $first_log_dir"
      "$storage_cmd" format -t "$("$storage_cmd" random-uuid)" -c "$cfg" >/dev/null
    fi
  fi
}

start_kafka() {
  setup_kafka_path
  if port_listening 9092 || ps aux | grep '[k]afka' >/dev/null 2>&1; then
    log 'Kafka already running'
  else
    local server_cmd cfg
    server_cmd="$(kafka_cmd kafka-server-start || true)"
    cfg="$(prepare_kafka_config || true)"
    if [ -z "$server_cmd" ] || [ -z "$cfg" ]; then
      log 'Kafka not started automatically. Please start Kafka manually and ensure localhost:9092 is available.'
      return 0
    fi
    format_kraft_if_needed "$cfg"
    log "starting Kafka with config: $cfg"
    nohup "$server_cmd" "$cfg" > "$LOG_DIR/kafka.log" 2>&1 &
    echo $! > "$LOG_DIR/kafka.pid"
    wait_for Kafka "ss -lntp | grep -q ':9092 '" 30 2 || true
  fi

  local topics_cmd
  topics_cmd="$(kafka_cmd kafka-topics || true)"
  if [ -n "$topics_cmd" ] && (port_listening 9092); then
    "$topics_cmd" --bootstrap-server localhost:9092 --create --if-not-exists \
      --topic "$TOPIC" --partitions 1 --replication-factor 1 >/dev/null 2>&1 || true
    log "Kafka topic ready or already exists: $TOPIC"
  fi
}

backend_reachable() {
  local code
  code="$(curl -s -o /dev/null -w '%{http_code}' http://localhost:6006/api/jobs/1 || true)"
  [ "$code" = 200 ] || [ "$code" = 404 ] || [ "$code" = 500 ]
}

start_backend() {
  if is_pid_running "$LOG_DIR/backend.pid" || port_listening 6006; then
    log 'Java backend already running'
    return 0
  fi
  log 'starting Java backend'
  nohup "$ROOT_DIR/scripts/run-backend.sh" > "$LOG_DIR/backend.log" 2>&1 &
  echo $! > "$LOG_DIR/backend.pid"
  wait_for 'Java backend' 'code=$(curl -s -o /dev/null -w "%{http_code}" http://localhost:6006/api/jobs/1 || true); [ "$code" = 200 ] || [ "$code" = 404 ] || [ "$code" = 500 ]' 45 2 || true
}

ensure_worker_venv() {
  cd "$ROOT_DIR/python-worker"
  if [ ! -d .venv ]; then
    log 'python-worker/.venv missing; creating it'
    python3 -m venv .venv
  fi
  # shellcheck disable=SC1091
  source .venv/bin/activate
  pip install -r requirements.txt
}

start_worker() {
  if is_pid_running "$LOG_DIR/worker.pid" || pgrep -f 'python.*worker.py' >/dev/null 2>&1; then
    log 'Python Worker already running'
    return 0
  fi
  ensure_worker_venv
  log 'starting Python Worker'
  nohup "$ROOT_DIR/scripts/run-worker.sh" > "$LOG_DIR/worker.log" 2>&1 &
  echo $! > "$LOG_DIR/worker.pid"
  sleep 2
  if is_pid_running "$LOG_DIR/worker.pid"; then
    log 'Python Worker started'
  else
    log 'Python Worker may have exited; check scripts/logs/worker.log'
  fi
}

start_postgres
start_redis
start_kafka
start_backend
start_worker
log 'done'
