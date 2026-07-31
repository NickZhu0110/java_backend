# Backend Resources

## Purpose

This directory contains Spring Boot profiles and Flyway database migrations.

## Key files and directories

- `application.yaml` retains PostgreSQL, Kafka, Redis, port 8080, and common multipart/MyBatis settings.
- `application-local-windows.yaml` selects loopback port 6006, file H2, local Python execution, and health exposure.
- [`db/migration`](db/migration/README.md) is the PostgreSQL-oriented migration history.
- [`db/migration-local`](db/migration-local/README.md) is the H2 migration history used by `local-windows`.
- `CAC_DATA_ROOT` controls the local H2, log, and job-artifact root.
- Python, SEGMENT-CACS, checkpoint, and timeout properties are supplied through environment variables.
- The H2 web console is disabled and Flyway clean is disabled locally.

## Related directory

- [`../java/com/cac/backend`](../java/com/cac/backend/README.md) consumes these settings.
