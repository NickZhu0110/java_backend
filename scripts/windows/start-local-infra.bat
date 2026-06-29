@echo off
setlocal
where docker >nul 2>nul
if errorlevel 1 (
  echo Docker Desktop is required for local MVP infrastructure.
  exit /b 1
)

cd /d "%~dp0"
docker compose -f docker-compose.local.yml up -d
