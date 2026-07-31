# CT, NIfTI, and VTK Viewer

## Purpose
This directory owns axial, coronal, and sagittal CT views; mask overlays and editing; the VTK 3D scene; and direct NIfTI review.
It also manages categorical-label/component selection and the Qt-facing controls for the experimental vessel workflow.

## Key files and entry points
- `CTViewerWidget.cpp`: coordinates loading, MPR rendering, brush edits, undo/redo, Save Mask, NIfTI review, and vessel controls.
- `Mask3DViewerWidget.cpp`: renders mask/label surfaces, colored slice cards, centerlines, and direct 3D plane dragging.
- `CaseVolumeLoader.cpp`: locates cached CT and mask artifacts and invokes preprocessing when raw arrays are stale or absent.
- `MultiStructureNiftiLoader.cpp`: loads CT plus categorical NIfTI and validates their physical geometry.
- `NiftiVesselSelectionState.cpp`: ranks six-connected components and stores the selected label/component.
- `PythonSimpleItkPreprocessor.cpp` and `RawVolumeLoader.cpp`: bridge supported medical inputs to native raw CT/mask models.

## Related directories
- [`../data`](../data) defines native volume models; [`../cache`](../cache) defines artifact locations; [`../vessel`](../vessel) owns VMTK process/results UI.
