@echo off
setlocal

echo Checking Docker...
where docker >nul 2>nul
if errorlevel 1 (
  echo docker: MISSING
) else (
  docker compose -f "%~dp0docker-compose.local.yml" ps
)

echo.
echo Checking ports 6006 5432 6379 9092...
for %%P in (6006 5432 6379 9092) do (
  powershell -NoProfile -Command "if (Get-NetTCPConnection -State Listen -LocalPort %%P -ErrorAction SilentlyContinue) { Write-Host 'PORT %%P: IN USE' } else { Write-Host 'PORT %%P: FREE' }"
)
