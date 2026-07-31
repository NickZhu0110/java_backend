# Backend Configuration

## Purpose

This package declares Java configuration that is not contained solely in Spring YAML.

## Key files and behavior

- `LocalAnalysisConfiguration.java` creates the single-thread local analysis executor when `cac.execution.mode=local`.
- `WebSocketConfig.java` registers `JobWebSocketHandler` at `/ws/jobs`.
- The local executor has one worker and a queue capacity of two.
- The current WebSocket registration allows all origins; no security configuration is present here.

## Related directory

- [`../service`](../service/README.md) consumes the executor; [`../websocket`](../websocket/README.md) implements broadcasts.
- [`../../../../../resources`](../../../../../resources/README.md) contains profile properties.
