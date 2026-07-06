@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
cd /d "%SCRIPT_DIR%"

set "JAR_PATH=%SCRIPT_DIR%target\cac-backend-0.0.1-SNAPSHOT.jar"

if not exist "%JAR_PATH%" (
  echo Missing backend jar:
  echo   %JAR_PATH%
  echo.
  echo Build first from this directory:
  echo   mvnw.cmd clean package -DskipTests
  exit /b 1
)

java -jar "%JAR_PATH%" --spring.profiles.active=local %*
