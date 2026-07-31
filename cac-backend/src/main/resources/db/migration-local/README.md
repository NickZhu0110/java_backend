# Local H2 Migrations

## Purpose

This Flyway history creates the file-backed database for the `local-windows` profile.

## Key files

- `V1__create_local_schema.sql` creates `analysis_jobs`, `cac_results`, and `corrected_mask_versions`.
- `V2__add_analysis_job_output_name.sql` adds the user-selected export folder name.
- Results are unique by `job_id`.
- Corrected-mask versions are unique by `(job_id, version)`.
- Result and version rows cascade when their job row is deleted.
- The database normally resides at `${CAC_DATA_ROOT}/db/cac.mv.db`.

## Related directory

- [`../migration`](../migration/README.md) contains server migrations; [`../..`](../../README.md) documents H2 settings.
