#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BACKEND_DIR="$ROOT_DIR/cac-backend"
KAFKA_HOME="${KAFKA_HOME:-/opt/kafka}"
KAFKA_CONFIG="${KAFKA_CONFIG:-/data/kafka/server.properties}"
KAFKA_LOG="${KAFKA_LOG:-/tmp/kafka.log}"
BACKEND_LOG="${BACKEND_LOG:-/tmp/cac-backend.log}"
WORKER_LOG="${WORKER_LOG:-/tmp/cac-worker.log}"
TOPIC="${KAFKA_TOPIC:-cac.analysis.requested}"
WITH_WORKER=0

export JAVA_HOME="${JAVA_HOME:-/usr/lib/jvm/zulu21-ca-amd64}"
export PATH="$KAFKA_HOME/bin:$PATH"
export SPRING_DATASOURCE_URL="${SPRING_DATASOURCE_URL:-jdbc:postgresql://localhost:5432/cac_platform}"
export SPRING_DATASOURCE_USERNAME="${SPRING_DATASOURCE_USERNAME:-nick}"
export SPRING_DATASOURCE_PASSWORD="${SPRING_DATASOURCE_PASSWORD:-cac_dev_password}"
export SPRING_KAFKA_BOOTSTRAP_SERVERS="${SPRING_KAFKA_BOOTSTRAP_SERVERS:-localhost:9092}"

for arg in "$@"; do
  case "$arg" in
    --with-worker)
      WITH_WORKER=1
      ;;
    -h|--help)
      cat <<USAGE
Usage: scripts/start-server-stack.sh [--with-worker]

Starts local development services on the AutoDL server/container:
  PostgreSQL, Redis, Kafka, Java backend.

Options:
  --with-worker    Also start python-worker in background.

Useful environment overrides:
  JAVA_HOME, KAFKA_HOME, KAFKA_TOPIC, SPRING_DATASOURCE_PASSWORD,
  KAFKA_CONFIG, KAFKA_LOG, BACKEND_LOG, WORKER_LOG
USAGE
      exit 0
      ;;
    *)
      echo "[start-stack] unknown argument: $arg" >&2
      exit 1
      ;;
  esac
done

log() {
  printf '[start-stack] %s\n' "$*"
}

wait_for() {
  local name="$1"
  local command="$2"
  local attempts="${3:-30}"
  local delay="${4:-2}"

  for i in $(seq 1 "$attempts"); do
    if bash -c "$command" >/dev/null 2>&1; then
      log "$name is ready"
      return 0
    fi
    sleep "$delay"
  done

  log "$name is not ready after $((attempts * delay))s"
  return 1
}

start_postgres() {
  log "starting PostgreSQL"
  pg_ctlcluster 10 main start >/dev/null 2>&1 || true

  log "ensuring PostgreSQL database/user"
  su - postgres -c "psql -tc \"SELECT 1 FROM pg_roles WHERE rolname='nick'\" | grep -q 1 || createuser nick" >/dev/null
  su - postgres -c "psql -tc \"SELECT 1 FROM pg_database WHERE datname='cac_platform'\" | grep -q 1 || createdb -O nick cac_platform" >/dev/null
  su - postgres -c "psql -c \"ALTER USER nick WITH PASSWORD '${SPRING_DATASOURCE_PASSWORD}';\"" >/dev/null
  su - postgres -c "psql -c \"GRANT ALL PRIVILEGES ON DATABASE cac_platform TO nick;\"" >/dev/null

  wait_for PostgreSQL "PGPASSWORD='${SPRING_DATASOURCE_PASSWORD}' psql -h localhost -U nick -d cac_platform -c 'select 1'"
}

start_redis() {
  log "starting Redis"
  if ! redis-cli ping >/dev/null 2>&1; then
    redis-server --daemonize yes >/dev/null 2>&1 || true
  fi
  wait_for Redis "redis-cli ping | grep -q PONG"
}

prepare_kafka_config() {
  mkdir -p /data/kafka

  if [ ! -f "$KAFKA_CONFIG" ]; then
    cp "$KAFKA_HOME/config/kraft/server.properties" "$KAFKA_CONFIG"
    sed -i 's#^log.dirs=.*#log.dirs=/data/kafka/kraft-combined-logs#' "$KAFKA_CONFIG"
    if grep -q '^advertised.listeners=' "$KAFKA_CONFIG"; then
      sed -i 's#^advertised.listeners=.*#advertised.listeners=PLAINTEXT://localhost:9092#' "$KAFKA_CONFIG"
    else
      printf '\nadvertised.listeners=PLAINTEXT://localhost:9092\n' >> "$KAFKA_CONFIG"
    fi
  fi

  if [ ! -f /data/kafka/kraft-combined-logs/meta.properties ]; then
    local cluster_id
    cluster_id="$(kafka-storage.sh random-uuid)"
    kafka-storage.sh format -t "$cluster_id" -c "$KAFKA_CONFIG" >/dev/null
  fi
}

start_kafka() {
  log "starting Kafka"
  prepare_kafka_config

  if ! pgrep -f 'kafka.Kafka' >/dev/null 2>&1; then
    nohup kafka-server-start.sh "$KAFKA_CONFIG" > "$KAFKA_LOG" 2>&1 &
    log "Kafka pid=$!, log=$KAFKA_LOG"
  else
    log "Kafka already running"
  fi

  wait_for Kafka "kafka-topics.sh --bootstrap-server localhost:9092 --list"
  kafka-topics.sh --bootstrap-server localhost:9092 --create --if-not-exists \
    --topic "$TOPIC" --partitions 1 --replication-factor 1 >/dev/null
  log "Kafka topic ready: $TOPIC"
}

start_backend() {
  log "starting Java backend"
  cd "$BACKEND_DIR"
  chmod +x mvnw

  if [ ! -f target/cac-backend-0.0.1-SNAPSHOT.jar ]; then
    log "backend jar missing; building with Maven wrapper"
    ./mvnw clean package -DskipTests
  fi

  if ! pgrep -f 'cac-backend-0.0.1-SNAPSHOT.jar|spring-boot:run' >/dev/null 2>&1; then
    nohup "$ROOT_DIR/scripts/run-backend.sh" > "$BACKEND_LOG" 2>&1 &
    log "backend pid=$!, log=$BACKEND_LOG"
  else
    log "backend already running"
  fi

  wait_for Backend "curl -fsS http://localhost:8080/actuator/health"
}

start_worker() {
  log "starting Python worker"
  cd "$ROOT_DIR/python-worker"

  if [ ! -d .venv ]; then
    log "worker .venv missing; creating with python3"
    python3 -m venv .venv
  fi

  source .venv/bin/activate
  pip install -r requirements.txt

  if ! pgrep -f 'python worker.py' >/dev/null 2>&1; then
    nohup "$ROOT_DIR/scripts/run-worker.sh" > "$WORKER_LOG" 2>&1 &
    log "worker pid=$!, log=$WORKER_LOG"
  else
    log "worker already running"
  fi
}

start_postgres
start_redis
start_kafka
start_backend

if [ "$WITH_WORKER" = 1 ]; then
  start_worker
else
  log "worker not started; run scripts/run-worker.sh in a separate terminal when needed"
fi

log "done"
log "backend: http://localhost:8080"
log "logs: backend=$BACKEND_LOG kafka=$KAFKA_LOG worker=$WORKER_LOG"
