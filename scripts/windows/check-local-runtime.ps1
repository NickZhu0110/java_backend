$ErrorActionPreference = "Stop"

$runtimeRoot = "C:/cac-runtime"
$platformRoot = "$runtimeRoot/cac-platform"
$segmentRoot = "$runtimeRoot/SEGMENT-CACS"
$dataRoot = "$runtimeRoot/data/cac"
$workerVenv = "$platformRoot/python-worker/.venv/Scripts/python.exe"
$qtCmake = "C:/Qt/Tools/CMake_64/bin/cmake.exe"
$qtNinja = "C:/Qt/Tools/Ninja/ninja.exe"

function Write-Status {
    param(
        [string]$Label,
        [bool]$Ok,
        [string]$Detail
    )

    $state = if ($Ok) { "OK" } else { "MISSING" }
    Write-Host ("[{0}] {1}: {2}" -f $state, $Label, $Detail)
}

function Test-Tool {
    param(
        [string]$Name,
        [string]$FallbackPath = ""
    )

    $cmd = Get-Command $Name -ErrorAction SilentlyContinue
    if ($cmd) {
        Write-Status $Name $true $cmd.Source
        return
    }

    if ($FallbackPath -and (Test-Path $FallbackPath)) {
        Write-Status $Name $true "$FallbackPath (fallback)"
        return
    }

    Write-Status $Name $false "not on PATH"
}

function Test-Port {
    param([int]$Port)

    $listeners = @()
    try {
        $listeners = Get-NetTCPConnection -State Listen -LocalPort $Port -ErrorAction SilentlyContinue
    } catch {
        $listeners = @()
    }

    if ($listeners.Count -gt 0) {
        Write-Host ("[IN USE] Port {0}: listener detected" -f $Port)
    } else {
        Write-Host ("[FREE] Port {0}: no listener detected" -f $Port)
    }
}

Write-Host "Checking Windows local CAC runtime..."
Write-Status "Runtime root" (Test-Path $runtimeRoot) $runtimeRoot
Write-Status "Platform root" (Test-Path $platformRoot) $platformRoot
Write-Status "SEGMENT-CACS root" (Test-Path $segmentRoot) $segmentRoot
Write-Status "Data root" (Test-Path $dataRoot) $dataRoot
Write-Status "Model file" (Test-Path "$segmentRoot/data/model/SegmentCACS_0001619_unet.pt") "$segmentRoot/data/model/SegmentCACS_0001619_unet.pt"
Write-Status "Wrapper" (Test-Path "$segmentRoot/model-service-python/run_segmentcacs.py") "$segmentRoot/model-service-python/run_segmentcacs.py"
Write-Status "Upstream src" (Test-Path "$segmentRoot/src") "$segmentRoot/src"
Write-Status "Patient0 DICOM folder" (Test-Path "$dataRoot/cases/patient0/dicom") "$dataRoot/cases/patient0/dicom"
Write-Status "Python venv" (Test-Path $workerVenv) $workerVenv

Write-Host ""
Write-Host "Tool availability"
Test-Tool "python"
Test-Tool "py"
Test-Tool "java"
Test-Tool "mvn"
Test-Tool "docker"
Test-Tool "cmake" $qtCmake
Test-Tool "ninja" $qtNinja

Write-Host ""
Write-Host "Port availability"
6006, 5432, 6379, 9092 | ForEach-Object { Test-Port $_ }
