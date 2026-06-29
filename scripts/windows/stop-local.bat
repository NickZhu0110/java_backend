@echo off
setlocal

where docker >nul 2>nul
if errorlevel 1 (
  echo Docker Desktop is required for local MVP infrastructure.
  echo Close backend and worker consoles manually.
  exit /b 1
)

docker compose -f "%~dp0docker-compose.local.yml" down
echo Close backend and worker consoles manually if they are still running.
