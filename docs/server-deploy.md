# Server Deployment Notes

This document describes a clean server layout for the CAC analysis platform.

## Recommended Server Layout

Application code cloned from GitHub:

```text
~/cac-platform/
├── cac-backend/
├── python-worker/
└── scripts/
```

Runtime data outside Git:

```text
/data/
├── SEGMENT-CACS/
├── models/
│   └── segment-cacs.pt
└── cac/
    ├── cases/
    ├── jobs/
    ├── masks/
    ├── reports/
    └── results/
```

## What Lives Where

On the server:

- `cac-backend` and `python-worker` are pulled from GitHub.
- PostgreSQL, Redis, and Kafka run on the server.
- SEGMENT-CACS source is stored outside Git, usually under `/data/SEGMENT-CACS`.
- Model files are stored outside Git, usually under `/data/models`.
- DICOM data and output files are stored outside Git, usually under `/data/cac`.

On the client machine:

- The Qt app runs locally.
- The Qt app connects to the server at `http://server-ip:8080`.
- WebSocket connects to `ws://server-ip:8080/ws/jobs`.

SSH is for deployment only. Runtime job submission should use REST API and WebSocket.

## Backend Deployment

```bash
git clone <repo-url> cac-platform
cd cac-platform/cac-backend
chmod +x mvnw
./mvnw clean package
java -jar target/*.jar
```

Use environment variables or server-local config for real credentials. Do not commit production config.

Example variables:

```bash
export SPRING_DATASOURCE_URL=jdbc:postgresql://localhost:5432/cac_platform
export SPRING_DATASOURCE_USERNAME=cac_user
export SPRING_DATASOURCE_PASSWORD=change_me
export SPRING_REDIS_HOST=localhost
export SPRING_REDIS_PORT=6379
export SPRING_KAFKA_BOOTSTRAP_SERVERS=localhost:9092
```

## Python Worker Deployment

```bash
cd ../python-worker
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
python worker.py
```

Example variables:

```bash
export BACKEND_BASE_URL=http://localhost:8080
export KAFKA_BOOTSTRAP_SERVERS=localhost:9092
export SEGMENTCACS_SRC=/data/SEGMENT-CACS/src
export MODEL_PATH=/data/models/segment-cacs.pt
export DEVICE=cuda
export USE_ZERO_MODULE=false
```

## Data Safety

Do not commit these to GitHub:

- `/data/SEGMENT-CACS`
- `/data/models`
- `/data/cac`
- DICOM files
- masks
- reports
- result outputs
- `.env`
- passwords
- build folders

Before pushing, run:

```bash
scripts/git-safety-check.sh
git status
```
