# Backend DTOs

## Purpose

This package defines JSON and Kafka-event contracts used at backend boundaries.
Lombok generates accessors for these mutable data classes.

## Key files and contracts

- `CreateJobRequest.java` and `UpdateJobStatusRequest.java` define job inputs.
- `CreateOrUpdateCacResultRequest.java` defines result upserts.
- `CacResultResponse.java` returns original, corrected, diagnostic, and export fields.
- `CorrectedMaskUploadResponse.java` reports stored mask versions and paths.
- `AnalysisRequestedEvent.java` is the legacy Kafka dispatch payload.
- `outputName` is required and limited to 100 characters before service-level Windows-name validation.
- Status and risk grade are strings rather than enums.

## Related directory

- [`../controller`](../controller/README.md) uses these DTOs; [`../entity`](../entity/README.md) holds persisted models.
