# Backend Entities

## Purpose

This package contains mutable MyBatis-Plus models for the backend database.
The classes contain fields only; lifecycle rules are implemented by services and Flyway constraints.

## Key files and persistence

- `AnalysisJob.java` maps `analysis_jobs` job state, paths, worker data, and timestamps.
- `CacResult.java` maps original and corrected scoring results.
- `CorrectedMaskVersion.java` maps immutable corrected-mask version records and SHA-256 values.
- Primary keys use `IdType.AUTO`.
- Local Flyway enforces one result per job and one row per job/version pair.
- Path and error fields can contain sensitive runtime information and must not be committed.

## Related directory

- [`../mapper`](../mapper/README.md) provides CRUD; [`../../../../../resources`](../../../../../resources/README.md) owns the schema.
