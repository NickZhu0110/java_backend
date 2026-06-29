$ErrorActionPreference = "Stop"

$pythonExe = "C:/cac-runtime/cac-platform/python-worker/.venv/Scripts/python.exe"
$wrapper = "C:/cac-runtime/SEGMENT-CACS/model-service-python/run_segmentcacs.py"
$src = "C:/cac-runtime/SEGMENT-CACS/src"
$model = "C:/cac-runtime/SEGMENT-CACS/data/model/SegmentCACS_0001619_unet.pt"
$inputDir = "C:/cac-runtime/data/cac/cases/patient0/dicom"
$outputDir = "C:/cac-runtime/data/cac/jobs/debug_patient0_cpu"

if (-not (Test-Path $pythonExe)) {
    Write-Host "USER_RUN_THIS_IN_POWERSHELL:"
    Write-Host "cd C:/cac-runtime/cac-platform/python-worker"
    Write-Host "py -3.10 -m venv .venv"
    Write-Host ".\.venv\Scripts\activate"
    Write-Host "python -m pip install --upgrade pip setuptools wheel"
    Write-Host "pip install torch==2.1.2"
    Write-Host "pip install SimpleITK==2.3.1 numpy==1.26.3 scipy==1.11.4 scikit-image==0.24.0 pillow imageio networkx tqdm pydicom requests python-dotenv kafka-python"
    if (Test-Path "C:/cac-runtime/SEGMENT-CACS/requirements.txt") {
        Write-Host "pip install -r C:/cac-runtime/SEGMENT-CACS/requirements.txt"
    }
    exit 1
}

& $pythonExe $wrapper `
  --segmentcacs-src $src `
  --model $model `
  --input $inputDir `
  --output $outputDir `
  --file-type dcm `
  --device cpu

Write-Host "C:/cac-runtime/data/cac/jobs/debug_patient0_cpu/result.json"
