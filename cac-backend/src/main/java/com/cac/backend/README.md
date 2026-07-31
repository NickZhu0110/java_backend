# Backend Java Package

## Purpose

`com.cac.backend` is the Spring component-scan root for the CAC backend.
It groups HTTP transport, persistence models, lifecycle services, and WebSocket delivery.

## Key files and directories

- `CacBackendApplication.java` calls `SpringApplication.run` and applies `@MapperScan`.
- [`controller`](controller/README.md) and [`dto`](dto/README.md) define HTTP contracts.
- [`service`](service/README.md) owns job execution and result workflows.
- [`entity`](entity/README.md) and [`mapper`](mapper/README.md) provide persistence.
- [`config`](config/README.md) and [`websocket`](websocket/README.md) configure infrastructure.

## Related directory

- [`../../../../../../resources`](../../../../resources/README.md) contains profiles and database migrations.
- [`../../../../../../README.md`](../../../../../../README.md) documents the backend module.
