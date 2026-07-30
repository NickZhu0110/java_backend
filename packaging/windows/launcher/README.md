# CACLauncher

`CACLauncher.exe` is the native GUI-subsystem entry point for the frozen
v0.6.10 Windows x64 CPU package. It has no Qt, Java, Python, or .NET runtime
dependency of its own.

## Staging contract

The built executable is copied to the package root. It refuses to start unless
the following package-relative assets exist:

```text
app/qt-cac-app.exe
app/platforms/qwindows.dll
backend/cac-backend.jar
runtime/jre/bin/java.exe
runtime/python-cac/python.exe
runtime/python-vmtk/python.exe
inference/scripts/local_cac_cli.py
inference/scripts/recalculate_agatston.py
inference/scripts/convert_case_to_raw_volume.py
inference/vessel/vmtk_straighten_vessel.py
inference/segment-cacs/
inference/model/SegmentCACS_0001619_unet.pt
```

It creates the empty writable directories beneath `data/`, constructs a
package-only runtime `PATH`, and supplies all CAC/Spring/Qt child-process
settings through an explicit Unicode environment block.

The backend is started suspended, assigned to a kill-on-close Job Object, and
then resumed. Its exact process handle is retained. Standard output and error
both append to `data/logs/backend.log`; package lifecycle events append to
`data/logs/launcher.log`. The launcher enables Spring Boot's shutdown actuator
only for this loopback-bound child process, polls health with WinHTTP, starts
Qt, and requests graceful shutdown when Qt exits. Only its own child process is
terminated if graceful shutdown times out.

## Build

Configure from an x64 MSVC 2022 developer environment:

```powershell
cmake -S packaging/windows/launcher -B build/launcher -A x64
cmake --build build/launcher --config Release
```

The packaging pipeline, rather than this CMake project, copies the resulting
`CACLauncher.exe` into the release staging root.
