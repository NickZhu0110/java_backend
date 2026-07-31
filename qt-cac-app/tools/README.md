# Qt Preprocessing Tools

## Purpose
This directory contains developer/runtime helpers used by the Qt client rather than application UI code.

## Key files and entry points
- `convert_case_to_raw_volume.py`: SimpleITK command-line bridge for DICOM directories, ZIP input, and supported image files.
- The bridge writes signed 16-bit CT RAW data, unsigned 8-bit binary-mask RAW data, and JSON geometry metadata.
- `PythonSimpleItkPreprocessor` invokes it through the isolated interpreter selected by `CAC_PYTHON_EXECUTABLE`.
- `configure_homebrew_vtk.sh`: configures a Homebrew Qt/VTK development build and is not the Windows release launcher.

## Related directory
- [`../src/viewer`](../src/viewer) contains the C++ caller and raw-volume loader.
