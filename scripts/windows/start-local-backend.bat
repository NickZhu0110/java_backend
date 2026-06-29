@echo off
setlocal

set "RUNTIME_ROOT=C:/cac-runtime"
set "PLATFORM_ROOT=%RUNTIME_ROOT%/cac-platform"
set "BACKEND_ROOT=%PLATFORM_ROOT%/cac-backend"
set "JAR_PATH=%BACKEND_ROOT%/target/cac-backend-0.0.1-SNAPSHOT.jar"
set "MVNW_CMD=%BACKEND_ROOT%/mvnw.cmd"
set "JAVA21_BIN=C:\Program Files\Microsoft\jdk-21.0.11.10-hotspot\bin"

set "SERVER_PORT=6006"
set "SERVER_ADDRESS=0.0.0.0"
set "CAC_STORAGE_ROOT=C:/cac-runtime/data/cac"
set "APP_ANALYSIS_MODE=local_direct"
set "SPRING_DATASOURCE_URL=jdbc:postgresql://localhost:5432/cac_platform"
set "SPRING_DATASOURCE_USERNAME=nick"
set "SPRING_DATASOURCE_PASSWORD=nick_local_dev"
set "SPRING_DATA_REDIS_HOST=localhost"
set "SPRING_DATA_REDIS_PORT=16379"
set "SPRING_KAFKA_BOOTSTRAP_SERVERS=localhost:9092"
set "SEGMENTCACS_PYTHON_EXE=C:/cac-runtime/cac-platform/python-worker/.venv/Scripts/python.exe"
set "SEGMENTCACS_WRAPPER=C:/cac-runtime/SEGMENT-CACS/model-service-python/run_segmentcacs.py"
set "SEGMENTCACS_SRC=C:/cac-runtime/SEGMENT-CACS/src"
set "MODEL_PATH=C:/cac-runtime/SEGMENT-CACS/data/model/SegmentCACS_0001619_unet.pt"
set "DEVICE=cpu"
set "USE_ZERO_MODULE=false"

if exist "%JAVA21_BIN%\java.exe" (
  set "PATH=%JAVA21_BIN%;%PATH%"
)

if exist "%JAR_PATH%" (
  cd /d "%BACKEND_ROOT%"
  java -jar "%JAR_PATH%"
  exit /b %errorlevel%
)

echo USER_RUN_THIS_IN_POWERSHELL:
echo cd C:/cac-runtime/cac-platform/cac-backend
if exist "%MVNW_CMD%" (
  echo .\mvnw.cmd clean package -DskipTests
) else (
  echo mvn clean package -DskipTests
)
echo.
echo Re-run start-local-backend.bat after the jar has been built.
exit /b 1
