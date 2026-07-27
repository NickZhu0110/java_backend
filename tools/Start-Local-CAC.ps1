[CmdletBinding()]
param(
    [string]$DataRoot,
    [string]$PythonExecutable,
    [string]$SegmentCacsRoot = "E:\files\java_backend_local_assets_20260724\inference\SEGMENT-CACS",
    [string]$ModelPath,
    [ValidateSet("cpu", "cuda")]
    [string]$InferenceDevice = "cpu",
    [int]$BackendPort = 6006,
    [int]$HealthTimeoutSeconds = 90
)

$ErrorActionPreference = "Stop"

function Stop-WithError {
    param([string]$Message)
    Write-Error $Message
    exit 1
}

function Test-IsAbsolutePath {
    param([string]$Path)
    if ([string]::IsNullOrWhiteSpace($Path)) {
        return $false
    }
    $root = [System.IO.Path]::GetPathRoot($Path)
    return ($root -match '^[A-Za-z]:\\$' -or $root -match '^\\\\[^\\]+\\[^\\]+\\$')
}

function Resolve-RequiredPath {
    param(
        [string]$Path,
        [string]$Label,
        [ValidateSet("File", "Directory")]
        [string]$Kind
    )

    if ([string]::IsNullOrWhiteSpace($Path)) {
        Stop-WithError "$Label is not configured."
    }
    if (-not (Test-IsAbsolutePath $Path)) {
        Stop-WithError "$Label must be an absolute path: $Path"
    }

    try {
        $resolved = (Resolve-Path -LiteralPath $Path -ErrorAction Stop).ProviderPath
    }
    catch {
        Stop-WithError "$Label does not exist: $Path"
    }

    if ($Kind -eq "File" -and -not (Test-Path -LiteralPath $resolved -PathType Leaf)) {
        Stop-WithError "$Label is not a file: $resolved"
    }
    if ($Kind -eq "Directory" -and -not (Test-Path -LiteralPath $resolved -PathType Container)) {
        Stop-WithError "$Label is not a directory: $resolved"
    }
    return $resolved
}

function Test-PortAvailable {
    param([int]$Port)
    $listener = $null
    try {
        $listener = [System.Net.Sockets.TcpListener]::new(
            [System.Net.IPAddress]::Loopback,
            $Port
        )
        $listener.Start()
        return $true
    }
    catch {
        return $false
    }
    finally {
        if ($null -ne $listener) {
            $listener.Stop()
        }
    }
}

$toolsDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
$worktreeCandidate = [System.IO.Path]::GetFullPath((Join-Path $toolsDirectory ".."))
$worktree = Resolve-RequiredPath $worktreeCandidate "Integration worktree" "Directory"

$requiredMarkers = @(
    (Join-Path $worktree ".git"),
    (Join-Path $worktree "cac-backend\pom.xml"),
    (Join-Path $worktree "python-worker\local_cac_cli.py"),
    (Join-Path $worktree "python-worker\recalculate_agatston.py")
)
foreach ($marker in $requiredMarkers) {
    if (-not (Test-Path -LiteralPath $marker)) {
        Stop-WithError "The script is not inside the expected integration worktree; missing: $marker"
    }
}

$localAppData = [Environment]::GetFolderPath("LocalApplicationData")
if ([string]::IsNullOrWhiteSpace($DataRoot)) {
    $DataRoot = Join-Path $localAppData "CAC\data"
}
if ([string]::IsNullOrWhiteSpace($PythonExecutable)) {
    $PythonExecutable = Join-Path $localAppData "CAC\python310\python.exe"
}
if ([string]::IsNullOrWhiteSpace($ModelPath)) {
    $ModelPath = Join-Path $SegmentCacsRoot "data\model\SegmentCACS_0001619_unet.pt"
}

if (-not (Test-IsAbsolutePath $DataRoot)) {
    Stop-WithError "CAC_DATA_ROOT must be an absolute path: $DataRoot"
}
New-Item -ItemType Directory -Force -Path $DataRoot | Out-Null
$DataRoot = Resolve-RequiredPath $DataRoot "CAC_DATA_ROOT" "Directory"

$writeProbe = Join-Path $DataRoot (".startup-write-test-{0}.tmp" -f [Guid]::NewGuid().ToString("N"))
try {
    [System.IO.File]::WriteAllText($writeProbe, "ok")
    Remove-Item -LiteralPath $writeProbe -Force
}
catch {
    Stop-WithError "CAC_DATA_ROOT is not writable: $DataRoot"
}

$PythonExecutable = Resolve-RequiredPath $PythonExecutable "CAC_PYTHON_EXECUTABLE" "File"
$SegmentCacsRoot = Resolve-RequiredPath $SegmentCacsRoot "SEGMENT_CACS_ROOT" "Directory"
$ModelPath = Resolve-RequiredPath $ModelPath "SEGMENT_CACS_MODEL_PATH" "File"
$localCliPath = Resolve-RequiredPath (Join-Path $worktree "python-worker\local_cac_cli.py") "CAC_LOCAL_CLI_PATH" "File"
$recalculateScriptPath = Resolve-RequiredPath (Join-Path $worktree "python-worker\recalculate_agatston.py") "CAC_RECALCULATE_SCRIPT_PATH" "File"
$backendJar = Resolve-RequiredPath (Join-Path $worktree "cac-backend\target\cac-backend-0.0.1-SNAPSHOT.jar") "Backend JAR" "File"

try {
    $javaCommand = @(Get-Command java.exe -CommandType Application -All -ErrorAction Stop)[0]
    $javaExecutable = (Resolve-Path -LiteralPath $javaCommand.Source -ErrorAction Stop).ProviderPath
}
catch {
    Stop-WithError "java.exe was not found. Install/configure the development JDK before starting the backend."
}

if ($BackendPort -lt 1 -or $BackendPort -gt 65535) {
    Stop-WithError "CAC_BACKEND_PORT must be between 1 and 65535."
}

$runDirectory = Join-Path $DataRoot "run"
$logDirectory = Join-Path $DataRoot "logs"
New-Item -ItemType Directory -Force -Path $runDirectory, $logDirectory | Out-Null
$pidFile = Join-Path $runDirectory "local-cac-backend.pid.json"

if (-not (Test-PortAvailable $BackendPort)) {
    $recordedBackendIsHealthy = $false
    $existingState = $null
    try {
        if (Test-Path -LiteralPath $pidFile -PathType Leaf) {
            $existingState = Get-Content -LiteralPath $pidFile -Raw | ConvertFrom-Json
            $existingProcess = Get-Process -Id ([int]$existingState.pid) -ErrorAction Stop
            $expectedStartTime = [DateTimeOffset]::Parse(
                [string]$existingState.processStartTimeUtc
            ).UtcDateTime
            $startTimeDifference = [Math]::Abs(
                ($existingProcess.StartTime.ToUniversalTime() - $expectedStartTime).TotalSeconds
            )
            $sameJava = $existingProcess.Path.Equals(
                [string]$existingState.javaExecutable,
                [System.StringComparison]::OrdinalIgnoreCase
            )
            $sameJar = $backendJar.Equals(
                [string]$existingState.backendJar,
                [System.StringComparison]::OrdinalIgnoreCase
            )
            $samePort = ([int]$existingState.port -eq $BackendPort)
            $health = Invoke-RestMethod `
                -Uri "http://127.0.0.1:$BackendPort/actuator/health" `
                -TimeoutSec 2
            $recordedBackendIsHealthy = (
                $startTimeDifference -le 1 -and
                $sameJava -and
                $sameJar -and
                $samePort -and
                $health.status -eq "UP"
            )
        }
    }
    catch {
        $recordedBackendIsHealthy = $false
    }

    if ($recordedBackendIsHealthy) {
        Write-Host "ALREADY READY: Local CAC backend health is UP; no second backend was started." -ForegroundColor Green
        Write-Host "PID: $($existingState.pid)"
        Write-Host "Health: http://127.0.0.1:$BackendPort/actuator/health"
        Write-Host "Log: $($existingState.backendLog)"
        exit 0
    }

    Stop-WithError "Port $BackendPort is occupied by an unverified process. Refusing to start a second backend."
}

if (Test-Path -LiteralPath $pidFile) {
    try {
        $oldState = Get-Content -LiteralPath $pidFile -Raw | ConvertFrom-Json
        $oldProcess = Get-Process -Id ([int]$oldState.pid) -ErrorAction SilentlyContinue
        if ($null -ne $oldProcess) {
            Stop-WithError "The PID file refers to a live process (PID $($oldState.pid)). Run Stop-Local-CAC.ps1 or inspect it manually."
        }
    }
    catch {
        if ($_.Exception.Message -like "The PID file refers*") {
            throw
        }
    }
    Remove-Item -LiteralPath $pidFile -Force
}

$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$stdoutLog = Join-Path $logDirectory "backend-startup-$timestamp.out.log"
$stderrLog = Join-Path $logDirectory "backend-startup-$timestamp.err.log"
$backendLog = Join-Path $logDirectory "backend.log"

$env:SPRING_PROFILES_ACTIVE = "local-windows"
$env:CAC_BACKEND_PORT = [string]$BackendPort
$env:CAC_DATA_ROOT = $DataRoot
$env:CAC_PYTHON_EXECUTABLE = $PythonExecutable
$env:SEGMENT_CACS_ROOT = $SegmentCacsRoot
$env:SEGMENT_CACS_MODEL_PATH = $ModelPath
$env:CAC_INFERENCE_DEVICE = $InferenceDevice
$env:CAC_LOCAL_CLI_PATH = $localCliPath
$env:CAC_RECALCULATE_SCRIPT_PATH = $recalculateScriptPath

try {
    $process = Start-Process `
        -FilePath $javaExecutable `
        -ArgumentList @("-jar", ('"{0}"' -f $backendJar)) `
        -WorkingDirectory $worktree `
        -WindowStyle Hidden `
        -RedirectStandardOutput $stdoutLog `
        -RedirectStandardError $stderrLog `
        -PassThru
}
catch {
    Stop-WithError "Failed to start the backend: $($_.Exception.Message)"
}

$state = [ordered]@{
    pid = $process.Id
    processStartTimeUtc = $process.StartTime.ToUniversalTime().ToString("o")
    javaExecutable = $javaExecutable
    backendJar = $backendJar
    worktree = $worktree
    port = $BackendPort
    healthUrl = "http://127.0.0.1:$BackendPort/actuator/health"
    backendLog = $backendLog
    stdoutLog = $stdoutLog
    stderrLog = $stderrLog
}
$state | ConvertTo-Json | Set-Content -LiteralPath $pidFile -Encoding UTF8

$deadline = (Get-Date).AddSeconds($HealthTimeoutSeconds)
$ready = $false
while ((Get-Date) -lt $deadline) {
    if ($process.HasExited) {
        break
    }
    try {
        $health = Invoke-RestMethod -Uri $state.healthUrl -TimeoutSec 2
        if ($health.status -eq "UP") {
            $ready = $true
            break
        }
    }
    catch {
        # Startup is still in progress.
    }
    Start-Sleep -Seconds 1
    $process.Refresh()
}

if (-not $ready) {
    if (-not $process.HasExited) {
        Stop-Process -Id $process.Id -ErrorAction SilentlyContinue
    }
    Remove-Item -LiteralPath $pidFile -Force -ErrorAction SilentlyContinue
    Stop-WithError "Backend did not become healthy within $HealthTimeoutSeconds seconds. Inspect: $stdoutLog and $stderrLog"
}

Write-Host "READY: Local CAC backend health is UP." -ForegroundColor Green
Write-Host "PID: $($process.Id)"
Write-Host "Health: $($state.healthUrl)"
Write-Host "Log: $backendLog"
Write-Host "Startup stdout: $stdoutLog"
Write-Host "Startup stderr: $stderrLog"
