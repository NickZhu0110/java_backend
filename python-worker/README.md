# Python integration

## Purpose

This directory contains project-owned Python adapters around the external
SEGMENT-CACS implementation and the separate VMTK vessel workflow.

## Key entry points

- `local_cac_cli.py` is the CPU-only local DICOM inference CLI used by the
  Windows-local Spring Boot backend.
- `recalculate_agatston.py` scores an existing CT and corrected mask without
  rerunning model inference.
- `worker.py` is the legacy Kafka worker and is not used by the portable flow.
- `requirements-windows-cpu.txt` lists direct CAC runtime dependencies.

## Related directories

See [`tests/`](tests/) and [`vessel_straightening/`](vessel_straightening/).
SEGMENT-CACS source and its checkpoint are external assets, not project code.
