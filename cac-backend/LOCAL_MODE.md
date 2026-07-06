# Local Backend Mode

This backend supports two runtime modes:

- Default/server mode keeps the original PostgreSQL + Redis + Kafka flow.
- `local` profile keeps the same REST and WebSocket API, but runs analysis through a local Python process instead of publishing to Kafka.

## Build

From `cac-backend`:

```bat
mvnw.cmd clean package -DskipTests
```

If `mvnw.cmd` is unavailable in your shell, use any Maven 3.9+ installation and run:

```bat
mvn clean package -DskipTests
```

## Start local profile

From `cac-backend`:

```bat
start-local-backend.bat
```

Equivalent manual command:

```bat
java -jar target\cac-backend-0.0.1-SNAPSHOT.jar --spring.profiles.active=local
```

## application-local.yml

Local mode reads settings from [application-local.yml](/abs/C:/Users/hyong/Desktop/java_backend/cac-backend/src/main/resources/application-local.yml).

Fields:

- `worker.python-exe` equivalent: `app.local-analysis.python-exe`
  Path to the Python executable that should launch the local wrapper.
- `worker.wrapper-path` equivalent: `app.local-analysis.wrapper-path`
  Path to `run_segmentcacs.py`.
- `worker.segmentcacs-src` equivalent: `app.local-analysis.segmentcacs-src`
  Path to the SEGMENT-CACS `src` directory.
- `worker.model-path` equivalent: `app.local-analysis.model-path`
  Path to the model weight file used by the wrapper.
- `storage.data-root` equivalent: `app.local-analysis.data-root`
  Root folder for local backend runtime data.
- `app.local-analysis.file-type`
  Input file type passed to the wrapper, for example `dcm`.
- `app.local-analysis.device`
  Runtime device passed to the wrapper, for example `cpu`.
- `app.local-analysis.use-zero-module`
  Whether to append `--use-zero-module` to the wrapper command.

The default `application-local.yml` values use a portable backend-local data root at `./local-data`. The SEGMENT-CACS executable paths still need to point to a valid local runtime. Override any value with environment variables if your package uses a different layout:

- `LOCAL_PYTHON_EXE`
- `LOCAL_SEGMENTCACS_WRAPPER`
- `LOCAL_SEGMENTCACS_SRC`
- `LOCAL_SEGMENTCACS_MODEL`
- `LOCAL_DATA_ROOT`
- `LOCAL_FILE_TYPE`
- `LOCAL_DEVICE`
- `LOCAL_USE_ZERO_MODULE`

## Local outputs

- H2 database file:
  `${app.local-analysis.data-root}/db/cac-platform`
- Per-job process log:
  `${app.local-analysis.data-root}/logs/jobs/{jobId}.log`
- Expected analysis result JSON:
  `${job.outputPath}/result.json`

If a submitted job uses a relative `inputPath` or `outputPath`, local mode resolves it under `app.local-analysis.data-root`.

## Behavior

- REST API paths stay unchanged.
- WebSocket path stays unchanged.
- In default/server mode, `AnalysisEventProducer` still publishes Kafka messages.
- In `local` profile, `AnalysisEventProducer` dispatches `LocalAnalysisRunner` instead of Kafka.
- Redis cache write failures are logged and ignored in local mode so missing Redis does not crash the backend.
