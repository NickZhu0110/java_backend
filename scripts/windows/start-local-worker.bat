@echo off
setlocal

set "RUNTIME_ROOT=C:/cac-runtime"
set "PLATFORM_ROOT=%RUNTIME_ROOT%/cac-platform"
set "WORKER_ROOT=%PLATFORM_ROOT%/python-worker"
set "VENV_PYTHON=%WORKER_ROOT%/.venv/Scripts/python.exe"

if not exist "%VENV_PYTHON%" (
  echo USER_RUN_THIS_IN_POWERSHELL:
  echo cd C:/cac-runtime/cac-platform/python-worker
  echo py -3.10 -m venv .venv
  echo .\.venv\Scripts\activate
  echo python -m pip install --upgrade pip setuptools wheel
  echo pip install torch==2.1.2
  echo pip install SimpleITK==2.3.1 numpy==1.26.3 scipy==1.11.4 scikit-image==0.24.0 pillow imageio networkx tqdm pydicom requests python-dotenv kafka-python
  if exist "C:/cac-runtime/SEGMENT-CACS/requirements.txt" (
    echo pip install -r C:/cac-runtime/SEGMENT-CACS/requirements.txt
  )
  exit /b 1
)

cd /d "%WORKER_ROOT%"
call ".venv\Scripts\activate.bat"
python worker.py
