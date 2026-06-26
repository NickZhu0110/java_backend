# Server Runtime

This document describes how to run the CAC backend pipeline inside the AutoDL server/container.

Qt is not started on the server. The Qt desktop client runs on the client machine and connects to:

```text
http://server-ip:8080
ws://server-ip:8080/ws/jobs
```

## Runtime Components

The real backend runtime requires these processes to keep running:

```text
PostgreSQL
Redis
Kafka
Java Spring Boot backend
Python Worker
```

The setup/check stage only verifies the environment. It does not mean services will keep running after the terminal exits or after AutoDL/container restart.

The Python Worker is required for jobs to progress through the pipeline. Without the worker, the Java backend can create jobs and publish Kafka events, but analysis jobs will stay unfinished because nothing consumes `cac.analysis.requested` and calls the backend status/result APIs.

Qt should connect to the Java backend only. Qt does not talk to the worker directly.

## Start Everything

From the project root:

```bash
cd ~/autodl-tmp/cac-platform
./scripts/start-all.sh
```

If your PostgreSQL credentials are different from local defaults, pass them as environment variables:

```bash
cd ~/autodl-tmp/cac-platform
SPRING_DATASOURCE_URL=jdbc:postgresql://localhost:5432/cac_platform \
SPRING_DATASOURCE_USERNAME=your_user \
SPRING_DATASOURCE_PASSWORD=your_password \
./scripts/start-all.sh
```

The script starts:

```text
PostgreSQL
Redis
Kafka
Java backend
Python Worker
```

It is safe to run multiple times. If a process is already running, it prints that status and avoids starting duplicate copies.

Logs and pid files are written under:

```text
scripts/logs/
```

Useful logs:

```text
scripts/logs/postgres.log
scripts/logs/redis.log
scripts/logs/kafka.log
scripts/logs/backend.log
scripts/logs/worker.log
```

## Check Status

```bash
cd ~/autodl-tmp/cac-platform
./scripts/status-all.sh
```

This checks:

```text
PostgreSQL
Redis
Kafka
Java Backend
Python Worker
Port 8080
Port 5432
Port 6379
Port 9092
Kafka topic cac.analysis.requested
```

## Stop Project Processes

```bash
cd ~/autodl-tmp/cac-platform
./scripts/stop-all.sh
```

`stop-all.sh` only stops processes that were started by this project script and have pid files under `scripts/logs/`.

It does not blindly kill system services. In particular, PostgreSQL is not force-stopped by this script.

## Backend Only

```bash
cd ~/autodl-tmp/cac-platform
./scripts/run-backend.sh
```

`run-backend.sh` uses these environment variables if provided:

```text
SPRING_DATASOURCE_URL
SPRING_DATASOURCE_USERNAME
SPRING_DATASOURCE_PASSWORD
SPRING_REDIS_HOST
SPRING_REDIS_PORT
SPRING_KAFKA_BOOTSTRAP_SERVERS
```

If they are missing, it uses local development defaults.

## Worker Only

```bash
cd ~/autodl-tmp/cac-platform
./scripts/run-worker.sh
```

`run-worker.sh` enters `python-worker`, loads `python-worker/.env` if it exists, activates `.venv`, and runs:

```bash
python worker.py
```

If `.env` is missing, it prints a warning and uses worker defaults/environment variables.

## Test Pipeline

Start all services first:

```bash
cd ~/autodl-tmp/cac-platform
./scripts/start-all.sh
```

Create a job:

```bash
curl -X POST http://localhost:8080/api/jobs \
  -H "Content-Type: application/json" \
  -d '{
    "modelName": "SEGMENT-CACS",
    "inputPath": "/root/autodl-tmp/data/cac/cases/case001/dicom",
    "outputPath": "/root/autodl-tmp/data/cac/jobs/job001",
    "fileType": "dcm",
    "device": "cuda"
  }'
echo
```

Then query the job and result. Replace `1` with the returned job id:

```bash
curl http://localhost:8080/api/jobs/1
curl http://localhost:8080/api/jobs/1/result
```

If the worker is running correctly, the job should eventually move to `SUCCESS`, and result metadata should become available.

## Notes

- Do not run Qt on the server.
- Do not store real secrets in Git.
- Do not commit generated logs or pid files.
- Do not store model weights, DICOM data, masks, reports, or runtime outputs in Git.
- AutoDL/container restarts usually stop runtime processes. Run `./scripts/start-all.sh` again after restart.
