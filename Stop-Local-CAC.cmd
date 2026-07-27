@echo off
setlocal
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\Stop-Local-CAC.ps1"
set "CAC_EXIT_CODE=%ERRORLEVEL%"
echo.
pause
exit /b %CAC_EXIT_CODE%
