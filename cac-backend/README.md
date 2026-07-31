# CAC Backend

## Purpose

This Spring Boot module persists CAC jobs and results, exposes REST and WebSocket APIs, and dispatches analysis.
The `local-windows` profile uses H2 and a managed Python subprocess; the default profile retains PostgreSQL, Kafka, and Redis integration.

## Key entry points

- `src/main/java/com/cac/backend/CacBackendApplication.java` starts Spring Boot and scans MyBatis mappers.
- `src/main/resources/application-local-windows.yaml` configures the portable loopback runtime.
- `pom.xml` targets Java 21 and builds `cac-backend-0.0.1-SNAPSHOT.jar`.

## Related directories

- [`src/main/java/com/cac/backend`](src/main/java/com/cac/backend/README.md) contains application code.
- [`src/main/resources`](src/main/resources/README.md) contains profiles and Flyway migrations.
- [`src/test/java/com/cac/backend`](src/test/java/com/cac/backend/README.md) contains backend tests.
