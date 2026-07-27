@echo off
setlocal
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\Start-Local-CAC.ps1"
set "CAC_EXIT_CODE=%ERRORLEVEL%"
echo.
if not "%CAC_EXIT_CODE%"=="0" (
    echo Local CAC backend failed to start. Review the error above.
) else (
    echo Local CAC backend is ready. You may now launch the Qt client.
)
pause
exit /b %CAC_EXIT_CODE%
