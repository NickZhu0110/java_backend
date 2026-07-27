# Local Windows development startup

This development launcher starts only the local Spring Boot backend. It uses H2 and the isolated Python environment; PostgreSQL, Kafka, Redis, and Docker are not required.

From the integration worktree, run one command:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\Start-Local-CAC.ps1
```

For Explorer use, double-click `Start-Local-CAC.cmd` in the integration worktree root.

Wait for `READY: Local CAC backend health is UP.` Then launch:

```powershell
.\qt-cac-app\build\windows-devbase-integration-release\qt-cac-app.exe
```

Stop only the backend created by the launcher:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\Stop-Local-CAC.ps1
```

For Explorer use, double-click `Stop-Local-CAC.cmd` in the integration worktree root.

The launcher validates the JAR, isolated Python, SEGMENT-CACS source, checkpoint, worker scripts, data directory, Java executable, and port 6006 before starting. Runtime state and logs are stored below `%LOCALAPPDATA%\CAC\data`; nothing is installed globally.

## VMTK vessel straightening

The read-only multi-structure NIfTI workflow uses a separate Python 3.10
environment. It does not modify or import packages from the SEGMENT-CACS
environment.

The Qt client resolves these development defaults automatically:

```text
CAC_VMTK_PYTHON_EXECUTABLE=%LOCALAPPDATA%\CAC\vmtk-python\python.exe
CAC_VESSEL_STRAIGHTENING_SCRIPT=<worktree>\python-worker\vessel_straightening\vmtk_straighten_vessel.py
CAC_DATA_ROOT=%LOCALAPPDATA%\CAC\data
```

Override any value in the terminal before launching Qt when a development
machine uses a different absolute path. Generated runs are written below:

```text
%CAC_DATA_ROOT%\vessel-straightening\dataset-<hash>\<run-id>\
```

No files are written beside the source NIfTI images. The VMTK process is local
and does not use Spring Boot, H2, SEGMENT-CACS, Kafka, Redis, or Docker.
