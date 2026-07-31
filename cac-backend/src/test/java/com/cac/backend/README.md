# Backend Test Package

## Purpose

This package contains the Spring context smoke test and service-level unit-test subpackage.

## Key file and coverage

- `CacBackendApplicationTests.java` loads the `local-windows` profile with an in-memory H2 datasource.
- Confirms the local Spring context and local Flyway schema can initialize.
- Does not run a real HTTP server, Python inference, DICOM processing, or WebSocket exchange.

## Entry point

- From `cac-backend`, run `.\mvnw.cmd test` on Windows.

## Related directory

- [`service`](service/README.md) contains focused service-package tests.
- [`../../../../../main/java/com/cac/backend`](../../../../../main/java/com/cac/backend/README.md) contains the application.
