# Windows Demo Packaging

This `winpack` branch is for producing a stable Windows demo package from the current local repository state.

## Scope

Included in this branch:

- Main Qt Doctor App window
- Backend connection settings
- REST API job submission
- WebSocket progress updates
- Result display
- Basic configuration needed to connect to the Java Spring Boot backend

Intentionally disabled for Windows demo packaging:

- CT viewer integration
- `CTViewerWidget`
- `CaseVolumeLoader`
- `RawVolumeLoader`
- `PythonSimpleItkPreprocessor`
- `VolumeData` / mask volume runtime pipeline
- SimpleITK / Python preprocessing bridge
- New DICOM or raw volume loading pipeline
- Manual mask overlay editing
- Unfinished CT viewer UI entry points

This branch does not permanently delete CT-related work. For packaging, `ENABLE_CT_VIEWER` defaults to `OFF`.

## Build Notes

The Qt app lives at:

- `qt-cac-app/`

The default local backend endpoints on this branch are:

- Backend URL: `http://localhost:16006`
- WebSocket URL: `ws://localhost:16006/ws/jobs`

These are only local defaults. They can still be changed from the app's Server Settings dialog for real server deployments.

## Build In Qt Creator

Recommended:

1. Open `qt-cac-app/CMakeLists.txt` in Qt Creator.
2. Select the `Qt 6.8.3 MSVC 2022 64-bit` kit.
3. Build `Release`.
4. Run the executable from Qt Creator once to confirm startup.

## Manual CMake Build On Windows

Use a Visual Studio 2022 x64 developer shell or a terminal where the MSVC compiler is already available.

Example configure command:

```powershell
cmake -S qt-cac-app -B build\winpack-release -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DENABLE_CT_VIEWER=OFF `
  -DCMAKE_PREFIX_PATH=C:\Qt\6.8.3\msvc2022_64
```

Example build command:

```powershell
cmake --build build\winpack-release --config Release
```

## Deploy With windeployqt

After a successful Release build, run:

```powershell
C:\Qt\6.8.3\msvc2022_64\bin\windeployqt.exe build\winpack-release\qt-cac-app.exe
```

If your generator places the executable in a subfolder, point `windeployqt` at that `.exe` instead.

## Delivery Folder

Zip the folder that contains:

- `qt-cac-app.exe`
- Qt runtime DLLs added by `windeployqt`
- platform plugins and other deployed Qt files

Recommended delivery source:

- `build\winpack-release\`

## Packaging Expectation

The Windows demo package should:

- open normally
- allow backend connection setup
- submit jobs to the Java backend
- receive WebSocket progress
- display job results

The package should not require CT viewer runtime dependencies for normal startup.
