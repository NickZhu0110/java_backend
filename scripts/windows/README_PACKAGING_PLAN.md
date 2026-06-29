# Windows Portable Packaging Plan

Do not build the final installer yet. This document only describes the next packaging phase.

## Future portable folder

```text
CAC-MVP-Windows/
|- start-cac-mvp.bat
|- stop-cac-mvp.bat
|- qt-app/
|- backend/
|- python-worker/
|- SEGMENT-CACS/
|- data/
`- infra/
```

## Reality of the current MVP

The realistic MVP still requires Docker Desktop for PostgreSQL, Redis, and Kafka unless the architecture is simplified later.

## Likely packaging choices

- `windeployqt` for the Qt app
- `jpackage` or a bundled JRE for the Java backend
- Python `.venv`, embeddable Python, or `conda-pack` for the Python worker
- Docker Compose for infrastructure

## Packaging notes

- Keep model weights, DICOM data, outputs, reports, masks, logs, and `.env` files out of Git.
- Preserve forward-slash Windows paths inside `.env` and JSON payloads.
- Keep server Linux scripts unchanged; Windows-local launchers should remain under `scripts/windows`.
