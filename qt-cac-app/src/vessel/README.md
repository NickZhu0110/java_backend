# Experimental Vessel and CPR UI

> The vessel/CPR workflow is experimental and has not been established as a clinically validated CPR implementation.

## Purpose
This directory launches isolated VMTK processing and displays generated curved CPR, straightened MPR, thin-slab MIP, and cross-section views.
Numeric label/component selection and 3D centerline actors remain in the viewer module.

## Key files and entry points
- `VesselStraighteningController.cpp`: resolves the VMTK runtime, launches analyze/straighten `QProcess` jobs, and parses JSON events.
- `StraightenedVesselWindow.cpp`: reads generated NRRD/JSON artifacts and renders the result modes.
- `VesselPathSelectionDialog.cpp`: contains an older modal selector; the current workflow uses the inline selector in `CTViewerWidget`.
- `VesselStraighteningController::startAnalysis` and `startStraightening`: begin the two processing phases.

## Related directory
- [`../viewer`](../viewer) owns vessel selection and 3D preview; the Python implementation is under `python-worker/vessel_straightening`.
