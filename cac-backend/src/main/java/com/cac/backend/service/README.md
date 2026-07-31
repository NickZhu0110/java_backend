# Backend Services

## Purpose

This package owns job lifecycle, dispatch, local Python execution, result persistence, file handling, and recalculation.

## Key files and entry points

- `JobService` and `JobLifecycleService` create jobs and publish status changes.
- `LocalAnalysisRunner` runs `local_cac_cli.py` with `ProcessBuilder` in local mode.
- `AnalysisEventProducer` publishes `cac.analysis.requested` in Kafka mode.
- `CacResultService` stores results, corrected masks, exports, and score-only recalculation.
- Cache implementations are profile-specific; `OutputNamePolicy` validates Windows folder names.
- Local states progress from `PENDING` through validation/inference to `COMPLETED`, or to `FAILED`.
- Local inference is DICOM-directory and CPU only.
- Filesystem writes and database transactions are not one atomic operation.

## Related directory

- [`../controller`](../controller/README.md) calls services; [`../mapper`](../mapper/README.md) persists; [`../websocket`](../websocket/README.md) broadcasts.
