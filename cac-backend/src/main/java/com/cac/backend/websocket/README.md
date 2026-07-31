# Backend WebSocket

## Purpose

This package pushes job status changes to connected Qt clients.
It is a raw text WebSocket channel rather than a STOMP broker.

## Key file and behavior

- `JobWebSocketHandler.java` maintains active sessions and broadcasts `JOB_STATUS` JSON.
- `WebSocketConfig` registers the handler at `/ws/jobs`.
- Messages contain `type`, `jobId`, `status`, and `progress`.
- Every session receives every job update; Qt filters its current job ID.
- Messages are not persisted or replayed, so HTTP polling remains authoritative.

## Related directory

- [`../config`](../config/README.md) registers the handler; [`../service`](../service/README.md) publishes updates.
