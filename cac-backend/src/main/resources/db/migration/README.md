# Server Database Migrations

## Purpose

This Flyway history targets the default PostgreSQL-backed server profile.

## Key files

- `V1` creates `analysis_jobs`; `V2` adds runtime fields.
- `V3` creates one `cac_results` row per job.
- Later files add output-name, corrected-score, and CT-volume columns.

## Important limitation

The current directory contains two migrations numbered `V4`, which normally conflicts with Flyway version uniqueness.
It also lacks the `corrected_mask_versions` table used by current Java services.

## Related directory

- [`../migration-local`](../migration-local/README.md) contains local H2 migrations; [`../..`](../../README.md) documents profiles.
