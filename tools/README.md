# Windows local development tools

## Purpose

These PowerShell scripts start and stop only the Spring Boot `local-windows`
development backend; Qt is launched separately.

## Key entry points

- `Start-Local-CAC.ps1` validates the JAR, isolated CAC Python, external
  SEGMENT-CACS assets, writable data root, and port 6006 before startup.
- It prints `READY` only after loopback actuator health is `UP`.
- `Stop-Local-CAC.ps1` verifies recorded PID identity before stopping it.
- `LOCAL_WINDOWS_DEV.md` contains the exact quick-start commands.

## Related directory

See [`../python-worker/`](../python-worker/) for the invoked Python adapters.
