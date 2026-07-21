#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LOG_DIR="$ROOT_DIR/scripts/logs"
KAFKA_HOME="${KAFKA_HOME:-/opt/kafka}"
TOPIC="${KAFKA_TOPIC:-cac.analysis.requested}"
export PATH="$KAFKA_HOME/bin:$PATH"

status_line() {
  printf '%-34s %s\n' "$1" "$2"
}

port_status() {
  local port="$1"
  if ss -lntp 2>/dev/null | grep -q ":$port "; then
    status_line "Port $port" RUNNING
  else
    status_line "Port $port" NOT_LISTENING
  fi
}

pid_file_status() {
  local name="$1"
  local pid_file="$2"
  if [ -f "$pid_file" ]; then
    local pid
    pid="$(cat "$pid_file" 2>/dev/null || true)"
    if [ -n "$pid" ] && kill -0 "$pid" >/dev/null 2>&1; then
      status_line "$name pid file" "RUNNING pid=$pid"
    else
      status_line "$name pid file" STALE_OR_STOPPED
    fi
  else
    status_line "$name pid file" MISSING
  fi
}

printf '[status-all] repo: %s\n' "$ROOT_DIR"

if ss -lntp 2>/dev/null | grep -q ':5432 ' || ps aux | grep '[p]ostgres' >/dev/null 2>&1; then
  status_line PostgreSQL RUNNING
else
  status_line PostgreSQL NOT_RUNNING
fi

if command -v redis-cli >/dev/null 2>&1 && redis-cli ping 2>/dev/null | grep -q PONG; then
  status_line Redis RUNNING
else
  status_line Redis NOT_RUNNING
fi

if ss -lntp 2>/dev/null | grep -q ':9092 ' || ps aux | grep '[k]afka' >/dev/null 2>&1; then
  status_line Kafka RUNNING
else
  status_line Kafka NOT_RUNNING
fi

code="$(curl -s -o /dev/null -w '%{http_code}' http://localhost:6006/api/jobs/1 || true)"
case "$code" in
  200|404|500) status_line 'Java Backend' "REACHABLE http=$code" ;;
  000) status_line 'Java Backend' NOT_REACHABLE ;;
  *) status_line 'Java Backend' "UNKNOWN http=$code" ;;
esac

if pgrep -f 'python.*worker.py' >/dev/null 2>&1; then
  status_line 'Python Worker' RUNNING
else
  status_line 'Python Worker' NOT_RUNNING
fi

port_status 6006
port_status 5432
port_status 6379
port_status 9092

pid_file_status 'Java backend' "$LOG_DIR/backend.pid"
pid_file_status 'Python Worker' "$LOG_DIR/worker.pid"
pid_file_status 'Redis' "$LOG_DIR/redis.pid"
pid_file_status 'Kafka' "$LOG_DIR/kafka.pid"

if command -v kafka-topics.sh >/dev/null 2>&1; then
  if kafka-topics.sh --bootstrap-server localhost:9092 --list 2>/dev/null | grep -qx "$TOPIC"; then
    status_line "Kafka topic $TOPIC" EXISTS
  else
    status_line "Kafka topic $TOPIC" MISSING_OR_KAFKA_DOWN
  fi
elif command -v kafka-topics >/dev/null 2>&1; then
  if kafka-topics --bootstrap-server localhost:9092 --list 2>/dev/null | grep -qx "$TOPIC"; then
    status_line "Kafka topic $TOPIC" EXISTS
  else
    status_line "Kafka topic $TOPIC" MISSING_OR_KAFKA_DOWN
  fi
else
  status_line "Kafka topic $TOPIC" 'UNKNOWN kafka-topics missing'
fi
