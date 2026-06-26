#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BACKEND_DIR="$ROOT_DIR/cac-backend"

if [ -f "$BACKEND_DIR/.env" ]; then
  echo "[run-backend] loading $BACKEND_DIR/.env"
  set -a
  # shellcheck disable=SC1091
  . "$BACKEND_DIR/.env"
  set +a
fi

export JAVA_HOME="${JAVA_HOME:-/usr/lib/jvm/zulu21-ca-amd64}"
export SERVER_ADDRESS="${SERVER_ADDRESS:-0.0.0.0}"
export SERVER_PORT="${SERVER_PORT:-6006}"
export SPRING_DATASOURCE_URL="${SPRING_DATASOURCE_URL:-jdbc:postgresql://localhost:5432/cac_platform}"
export SPRING_DATASOURCE_USERNAME="${SPRING_DATASOURCE_USERNAME:-postgres}"
export SPRING_DATASOURCE_PASSWORD="${SPRING_DATASOURCE_PASSWORD:-}"
export SPRING_REDIS_HOST="${SPRING_REDIS_HOST:-localhost}"
export SPRING_REDIS_PORT="${SPRING_REDIS_PORT:-6379}"
export SPRING_DATA_REDIS_HOST="${SPRING_DATA_REDIS_HOST:-$SPRING_REDIS_HOST}"
export SPRING_DATA_REDIS_PORT="${SPRING_DATA_REDIS_PORT:-$SPRING_REDIS_PORT}"
export SPRING_KAFKA_BOOTSTRAP_SERVERS="${SPRING_KAFKA_BOOTSTRAP_SERVERS:-localhost:9092}"

cd "$BACKEND_DIR"
chmod +x mvnw

jar="$(find target -maxdepth 1 -type f -name '*.jar' ! -name '*.original' ! -name '*sources.jar' ! -name '*javadoc.jar' 2>/dev/null | sort | head -1 || true)"

if [ -z "$jar" ]; then
  echo "[run-backend] no built jar found; building with ./mvnw clean package -DskipTests"
  ./mvnw clean package -DskipTests
  jar="$(find target -maxdepth 1 -type f -name '*.jar' ! -name '*.original' ! -name '*sources.jar' ! -name '*javadoc.jar' 2>/dev/null | sort | head -1 || true)"
fi

if [ -n "$jar" ]; then
  echo "[run-backend] running jar: $jar"
  exec java -jar "$jar"
fi

echo "[run-backend] jar still missing; falling back to ./mvnw spring-boot:run"
exec ./mvnw spring-boot:run
