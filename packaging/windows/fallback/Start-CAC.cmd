@echo off
setlocal

set "CAC_PACKAGE_ROOT=%~dp0."
set "CAC_FALLBACK_SCRIPT=%CAC_PACKAGE_ROOT%\config\Start-CAC.ps1"
set "CAC_POWERSHELL=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"

if not exist "%CAC_POWERSHELL%" (
  echo ERROR: Windows PowerShell is unavailable at:
  echo   %CAC_POWERSHELL%
  pause
  exit /b 1
)

if not exist "%CAC_FALLBACK_SCRIPT%" (
  echo ERROR: The portable fallback script is missing:
  echo   %CAC_FALLBACK_SCRIPT%
  echo Re-extract the complete release ZIP.
  pause
  exit /b 1
)

"%CAC_POWERSHELL%" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%CAC_FALLBACK_SCRIPT%" -PackageRoot "%CAC_PACKAGE_ROOT%"
set "CAC_EXIT_CODE=%ERRORLEVEL%"

if not "%CAC_EXIT_CODE%"=="0" (
  echo.
  echo Local CAC failed. Review the error and data\logs\backend.log.
  pause
)

exit /b %CAC_EXIT_CODE%
