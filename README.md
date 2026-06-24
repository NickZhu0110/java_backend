# CAC Analysis Platform

This repository is a monorepo for a Coronary Artery Calcium (CAC) analysis platform. It stores source code and safe templates only.

## Components

- `cac-backend`: Java Spring Boot backend for job APIs, PostgreSQL persistence, Redis realtime status, WebSocket updates, and Kafka task publishing.
- `python-worker`: Kafka consumer and SEGMENT-CACS runner. In development it can simulate inference and update backend job status/results.
- `qt-cac-app`: Qt C++ doctor desktop app source code.
- `scripts`: local development helper scripts.
- `DEV_INSTRUCTIONS.md`: local reset and development instructions.

## Runtime Layout

The server runs:

- Java backend
- PostgreSQL
- Redis
- Kafka
- Python worker
- SEGMENT-CACS
- model files
- `/data/cac` storage

The client machine runs:

- Qt Doctor App

The Qt app connects to the server through:

- REST API
- WebSocket

SSH is for deployment and server maintenance only. Runtime job submission should go through the backend REST API and WebSocket, not SSH.

## GitHub Policy

GitHub stores code and templates only.

Do not commit:

- DICOM
- model weights
- result outputs
- passwords
- `.env`
- build folders
- local runtime data

Use example config files as templates:

- `cac-backend/.env.example`
- `python-worker/.env.example`
- `qt-cac-app/config.example.ini`

Create real local config files outside Git or with ignored names such as `.env` and `config.ini`.

## Local Development

See:

```text
DEV_INSTRUCTIONS.md
```

Useful reset command:

```bash
scripts/dev-reset.sh --yes --start
```

Before committing, run:

```bash
scripts/git-safety-check.sh
git status
```
