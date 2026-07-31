# Backend Controllers

## Purpose

This package maps HTTP requests to job and result services.
Controllers are transport adapters; inference, persistence, and filesystem policy live in `service`.

## Key files and entry points

- `JobController.java`: `/api/jobs`, job polling/status updates, and `/api/jobs/{id}/recalculate`.
- `CacResultController.java`: `GET` and `POST /api/jobs/{jobId}/result`.
- `JobFileController.java`: CT/AI-mask downloads and corrected-mask multipart upload.
- Job creation returns a bare numeric job ID.
- Corrected upload requires multipart parts named `mask` and `metadata`.
- The current application has no Spring Security layer.

## Related directory

- [`../dto`](../dto/README.md) defines payloads; [`../service`](../service/README.md) implements operations.
