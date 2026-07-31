# Experimental vessel processing

## Purpose

This module analyzes a manually selected categorical label/component with
VMTK, creates selectable centerline paths, and samples straightened CT and
binary-mask volumes for the Qt vessel viewer.

The vessel/CPR workflow is experimental and has not been established as a
clinically validated CPR implementation.

## Key entry point

- `vmtk_straighten_vessel.py` provides `analyze` and `straighten` CLI modes,
  emits JSON-line progress, and writes centerline JSON/VTP plus NRRD outputs.
- CT uses linear interpolation; the component mask uses nearest-neighbor
  interpolation.
## Related directories

See [`../tests/`](../tests/) and [`../../qt-cac-app/src/vessel/`](../../qt-cac-app/src/vessel/).
