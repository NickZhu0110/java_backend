# Backend Mappers

## Purpose

This package exposes database CRUD through MyBatis-Plus `BaseMapper`.
Mapper discovery is enabled by `@MapperScan` in `CacBackendApplication`.

## Key files and behavior

- `AnalysisJobMapper.java` accesses `analysis_jobs`.
- `CacResultMapper.java` accesses `cac_results`.
- `CorrectedMaskVersionMapper.java` accesses `corrected_mask_versions`.
- There are no custom mapper XML files or declared custom SQL methods.
- Services use typed `LambdaQueryWrapper` queries for result and version lookup.
- Flyway, not MyBatis, creates and upgrades tables.

## Related directory

- [`../entity`](../entity/README.md) defines fields; [`../service`](../service/README.md) contains callers.
