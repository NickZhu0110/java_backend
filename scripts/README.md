# POSIX development scripts

## Purpose

These Bash helpers support the legacy Linux/server development topology with
PostgreSQL, Redis, Kafka, Spring Boot, and the Kafka Python worker.

## Key entry points

- `run-backend.sh` and `run-worker.sh` start individual processes.
- `start-all.sh`, `status-all.sh`, and `stop-all.sh` manage the general
  development stack.
- `start-server-stack.sh` targets an AutoDL-like server environment.
- `dev-reset.sh --yes` destructively recreates local service state.
- `git-safety-check.sh` is an advisory generated/sensitive-file scan.

## Related directory

For Windows local development, use [`../tools/`](../tools/) instead.
