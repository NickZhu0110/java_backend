# Windows Local MVP Runtime

This folder prepares a local Windows CPU MVP runtime for the CAC platform.

## Target layout

Use the following runtime root:

```text
C:/cac-runtime
```

Expected assets outside Git:

```text
C:/cac-runtime/SEGMENT-CACS
C:/cac-runtime/data/cac
```

The local platform code should live at:

```text
C:/cac-runtime/cac-platform
```

## Required runtime assets

Copy the following into place before running the real local pipeline:

```text
C:/cac-runtime/SEGMENT-CACS/model-service-python/run_segmentcacs.py
C:/cac-runtime/SEGMENT-CACS/src
C:/cac-runtime/SEGMENT-CACS/data/model/SegmentCACS_0001619_unet.pt
C:/cac-runtime/data/cac/cases/patient0/dicom
```

If `patient0/dicom` is missing or empty, copy a verified patient0 DICOM study there.

## Scripts

- `check-local-runtime.ps1`: lightweight existence and tool checks.
- `start-local-infra.bat`: starts PostgreSQL, Redis, and Kafka with Docker Compose.
- `start-local-backend.bat`: runs the backend jar on `http://localhost:6006`.
- `start-local-worker.bat`: activates `.venv` and starts `worker.py`.
- `status-local.bat`: checks Docker services and local ports.
- `stop-local.bat`: stops Docker infrastructure.
- `run-direct-wrapper-test.ps1`: runs the Windows CPU wrapper test for patient0.
- `submit-local-patient0-job.ps1`: posts a local CPU backend job.

## Required tools

- Python 3.10 recommended
- Java 21
- Maven
- Docker Desktop
- Qt 6.8.3 MSVC 2022 64-bit
- CMake
- Ninja
- Visual Studio 2022 Build Tools with Desktop development with C++

## Windows worker dependency note

For the Windows local MVP, the worker can fall back to `kafka-python` if `confluent-kafka` is not available.

Use the explicit Windows install commands in the batch and PowerShell scripts instead of blindly installing `python-worker/requirements.txt`.

For backend builds, prefer the project-local Maven wrapper:

```text
.\mvnw.cmd clean package -DskipTests
```

## Startup order

1. Start Docker Desktop.
2. Run `start-local-infra.bat`.
3. Run `start-local-backend.bat`.
4. Run `start-local-worker.bat`.
5. Run `submit-local-patient0-job.ps1`.
6. Open Qt with Backend URL `http://localhost:6006` and WebSocket URL `ws://localhost:6006/ws/jobs`.

Qt test values:

```text
Backend URL:
http://localhost:6006

WebSocket URL:
ws://localhost:6006/ws/jobs

inputPath:
C:/cac-runtime/data/cac/cases/patient0/dicom

outputPath:
C:/cac-runtime/data/cac/jobs/job_qt_patient0_cpu

device:
cpu

useZeroModule:
false
```

## Direct wrapper CPU test

Run this manually:

```text
USER_RUN_THIS_IN_POWERSHELL:
cd C:/cac-runtime/cac-platform
.\scripts\windows\run-direct-wrapper-test.ps1
```

Expected result:

```text
totalAgatstonScore around 5.86
riskGrade minimal
```
