[CmdletBinding()]
param(
    [string]$DataRoot
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

$localAppData = [Environment]::GetFolderPath("LocalApplicationData")
if ([string]::IsNullOrWhiteSpace($DataRoot)) {
    $DataRoot = Join-Path $localAppData "CAC\data"
}
if (-not (Test-IsAbsolutePath $DataRoot)) {
    Stop-WithError "CAC_DATA_ROOT must be an absolute path: $DataRoot"
}

$pidFile = Join-Path $DataRoot "run\local-cac-backend.pid.json"
if (-not (Test-Path -LiteralPath $pidFile -PathType Leaf)) {
    Write-Host "Local CAC backend is not recorded as running. No process was stopped."
    exit 0
}

try {
    $state = Get-Content -LiteralPath $pidFile -Raw | ConvertFrom-Json
    $pidValue = [int]$state.pid
    $expectedStartTime = [DateTimeOffset]::Parse([string]$state.processStartTimeUtc).UtcDateTime
    $expectedJava = [System.IO.Path]::GetFullPath([string]$state.javaExecutable)
}
catch {
    Stop-WithError "The backend PID file is invalid. Refusing to stop any process: $pidFile"
}

$process = Get-Process -Id $pidValue -ErrorAction SilentlyContinue
if ($null -eq $process) {
    Remove-Item -LiteralPath $pidFile -Force
    Write-Host "The recorded backend process is already stopped. Removed stale PID file."
    exit 0
}

try {
    $actualStartTime = $process.StartTime.ToUniversalTime()
    $actualExecutable = [System.IO.Path]::GetFullPath($process.Path)
}
catch {
    Stop-WithError "Cannot verify PID $pidValue. Refusing to stop it."
}

$startTimeDifference = [Math]::Abs(($actualStartTime - $expectedStartTime).TotalSeconds)
if ($startTimeDifference -gt 1 -or
    -not $actualExecutable.Equals($expectedJava, [System.StringComparison]::OrdinalIgnoreCase)) {
    Stop-WithError "PID $pidValue no longer matches the backend created by Start-Local-CAC.ps1. Refusing to stop it."
}

Stop-Process -Id $pidValue -ErrorAction Stop
$null = $process.WaitForExit(15000)
if (-not $process.HasExited) {
    Stop-WithError "PID $pidValue did not stop. The PID file was preserved."
}

Remove-Item -LiteralPath $pidFile -Force
Write-Host "STOPPED: Local CAC backend PID $pidValue. No other Java process was touched." -ForegroundColor Green
