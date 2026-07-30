[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $PackageRoot
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

$BackendPort = 6006
$BackendStartupTimeoutSeconds = 120
$BackendShutdownTimeoutMilliseconds = 20000
$MutexName = "Local\CACPlatform.PortableFallback.v0.6.10.Windows.x64.CPU"

$mutex = $null
$ownsMutex = $false
$backend = $null
$backendStarted = $false
$qt = $null
$stdoutTask = $null
$stderrTask = $null
$streamsWritten = $false
$exitCode = 0

function Resolve-PortablePath {
    param([Parameter(Mandatory = $true)][string] $RelativePath)
    return [IO.Path]::GetFullPath((Join-Path $script:root $RelativePath))
}

function Quote-ProcessArgument {
    param([Parameter(Mandatory = $true)][AllowEmptyString()][string] $Value)
    if ($Value.Contains('"')) {
        throw "A portable path unexpectedly contains a double quote: $Value"
    }
    return '"' + $Value + '"'
}

function Assert-RequiredFile {
    param(
        [Parameter(Mandatory = $true)][string] $Label,
        [Parameter(Mandatory = $true)][string] $Path
    )
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Required $Label is missing or unreadable:`n$Path"
    }
}

function Assert-RequiredDirectory {
    param(
        [Parameter(Mandatory = $true)][string] $Label,
        [Parameter(Mandatory = $true)][string] $Path
    )
    if (-not (Test-Path -LiteralPath $Path -PathType Container)) {
        throw "Required $Label directory is missing or unreadable:`n$Path"
    }
}

function Test-BackendPortAvailable {
    $listener = $null
    try {
        $listener = [Net.Sockets.TcpListener]::new(
            [Net.IPAddress]::Loopback,
            $script:BackendPort)
        $listener.Server.ExclusiveAddressUse = $true
        $listener.Start()
    }
    catch {
        throw "Port $script:BackendPort is already occupied or unavailable. No existing process was stopped.`n$($_.Exception.Message)"
    }
    finally {
        if ($null -ne $listener) {
            $listener.Stop()
        }
    }
}

function Set-PortableChildEnvironment {
    param([Parameter(Mandatory = $true)][Diagnostics.ProcessStartInfo] $StartInfo)

    foreach ($nameObject in @($StartInfo.EnvironmentVariables.Keys)) {
        $name = [string] $nameObject
        if ($name.StartsWith("CAC_", [StringComparison]::OrdinalIgnoreCase) -or
            $name.StartsWith("SEGMENT_CACS_", [StringComparison]::OrdinalIgnoreCase) -or
            $name.StartsWith("SPRING_", [StringComparison]::OrdinalIgnoreCase) -or
            $name.StartsWith("QT_", [StringComparison]::OrdinalIgnoreCase)) {
            $StartInfo.EnvironmentVariables.Remove($name)
        }
    }

    foreach ($name in @(
        "JAVA_HOME",
        "JDK_HOME",
        "JAVA_TOOL_OPTIONS",
        "_JAVA_OPTIONS",
        "JDK_JAVA_OPTIONS",
        "PYTHONHOME",
        "PYTHONPATH",
        "VIRTUAL_ENV",
        "CONDA_PREFIX",
        "CONDA_DEFAULT_ENV",
        "QTDIR",
        "Qt6_DIR",
        "VTK_DIR"
    )) {
        $StartInfo.EnvironmentVariables.Remove($name)
    }

    $windowsDirectory = [IO.Path]::GetDirectoryName(
        [Environment]::SystemDirectory)
    $controlledPath = @(
        $script:paths.AppDirectory,
        (Join-Path $script:root "runtime\jre\bin"),
        (Join-Path $script:root "runtime\python-cac"),
        (Join-Path $script:root "runtime\python-cac\Scripts"),
        (Join-Path $script:root "runtime\python-vmtk"),
        (Join-Path $script:root "runtime\python-vmtk\Library\bin"),
        (Join-Path $script:root "runtime\python-vmtk\Scripts"),
        [Environment]::SystemDirectory,
        $windowsDirectory
    ) -join ";"

    $settings = [ordered]@{
        "Path"                              = $controlledPath
        "TEMP"                              = $script:paths.Temp
        "TMP"                               = $script:paths.Temp
        "PYTHONDONTWRITEBYTECODE"            = "1"
        "PYTHONNOUSERSITE"                  = "1"
        "PYTHONUTF8"                        = "1"
        "CAC_PACKAGE_ROOT"                  = $script:root
        "CAC_DATA_ROOT"                     = $script:paths.Data
        "CAC_PYTHON_EXECUTABLE"             = $script:paths.CacPython
        "CAC_VMTK_PYTHON_EXECUTABLE"        = $script:paths.VmtkPython
        "CAC_VESSEL_STRAIGHTENING_SCRIPT"   = $script:paths.VesselScript
        "SEGMENT_CACS_ROOT"                 = $script:paths.SegmentCacsRoot
        "SEGMENT_CACS_MODEL_PATH"           = $script:paths.Model
        "CAC_LOCAL_CLI_PATH"                = $script:paths.LocalCli
        "CAC_RECALCULATE_SCRIPT_PATH"       = $script:paths.RecalculateScript
        "CAC_CONVERT_SCRIPT_PATH"           = $script:paths.ConvertScript
        "CAC_INFERENCE_DEVICE"              = "cpu"
        "CAC_CPU_ONLY_RELEASE"              = "1"
        "CAC_BACKEND_PORT"                  = [string] $script:BackendPort
        "CAC_BACKEND_URL"                   = "http://127.0.0.1:$script:BackendPort"
        "CAC_WEBSOCKET_URL"                 = "ws://127.0.0.1:$script:BackendPort/ws/jobs"
        "SPRING_PROFILES_ACTIVE"             = "local-windows"
        "QT_PLUGIN_PATH"                    = $script:paths.AppDirectory
        "QT_QPA_PLATFORM_PLUGIN_PATH"       = $script:paths.PlatformDirectory
    }

    foreach ($entry in $settings.GetEnumerator()) {
        $StartInfo.EnvironmentVariables[$entry.Key] = $entry.Value
    }
}

function Invoke-LocalBackendRequest {
    param(
        [Parameter(Mandatory = $true)][ValidateSet("GET", "POST")][string] $Method,
        [Parameter(Mandatory = $true)][string] $Path
    )

    $response = $null
    $reader = $null
    try {
        $request = [Net.HttpWebRequest]::Create(
            "http://127.0.0.1:$script:BackendPort$Path")
        $request.Method = $Method
        $request.Proxy = $null
        $request.Timeout = 1500
        $request.ReadWriteTimeout = 1500
        $request.KeepAlive = $false
        if ($Method -eq "POST") {
            $request.ContentLength = 0
        }

        $response = [Net.HttpWebResponse] $request.GetResponse()
        $reader = [IO.StreamReader]::new(
            $response.GetResponseStream(),
            [Text.Encoding]::UTF8)
        return [pscustomobject]@{
            StatusCode = [int] $response.StatusCode
            Body = $reader.ReadToEnd()
        }
    }
    catch {
        return $null
    }
    finally {
        if ($null -ne $reader) {
            $reader.Dispose()
        }
        if ($null -ne $response) {
            $response.Dispose()
        }
    }
}

function Test-BackendHealth {
    $response = Invoke-LocalBackendRequest -Method GET -Path "/actuator/health"
    if ($null -eq $response -or $response.StatusCode -ne 200) {
        return $false
    }
    try {
        $health = $response.Body | ConvertFrom-Json
        return $health.status -eq "UP"
    }
    catch {
        return $false
    }
}

function Write-CapturedBackendStreams {
    if ($script:streamsWritten -or
        $null -eq $script:backend -or
        -not $script:backendStarted -or
        -not $script:backend.HasExited) {
        return
    }

    $script:streamsWritten = $true
    $encoding = [Text.UTF8Encoding]::new($false)
    try {
        $stdout = $script:stdoutTask.GetAwaiter().GetResult()
        if (-not [string]::IsNullOrWhiteSpace($stdout)) {
            [IO.File]::AppendAllText(
                $script:paths.BackendLog,
                "`r`n--- emergency fallback stdout ---`r`n$stdout",
                $encoding)
        }
    }
    catch {
        # Spring's live file appender still writes backend.log.
    }
    try {
        $stderr = $script:stderrTask.GetAwaiter().GetResult()
        if (-not [string]::IsNullOrWhiteSpace($stderr)) {
            [IO.File]::AppendAllText(
                $script:paths.BackendLog,
                "`r`n--- emergency fallback stderr ---`r`n$stderr",
                $encoding)
        }
    }
    catch {
        # Preserve the original startup/shutdown error.
    }
}

function Stop-ExactBackend {
    param([int] $TimeoutMilliseconds = $script:BackendShutdownTimeoutMilliseconds)

    if ($null -eq $script:backend -or
        -not $script:backendStarted -or
        $script:backend.HasExited) {
        Write-CapturedBackendStreams
        return $false
    }

    [void] (Invoke-LocalBackendRequest -Method POST -Path "/actuator/shutdown")
    if ($script:backend.WaitForExit($TimeoutMilliseconds)) {
        Write-CapturedBackendStreams
        return $false
    }

    # This is the exact Process object created by this script. Never enumerate
    # or kill Java/Python processes by executable name.
    $script:backend.Kill()
    [void] $script:backend.WaitForExit(5000)
    Write-CapturedBackendStreams
    return $true
}

try {
    $root = [IO.Path]::GetFullPath($PackageRoot)

    $createdNew = $false
    $mutex = [Threading.Mutex]::new(
        $true,
        $MutexName,
        [ref] $createdNew)
    if (-not $createdNew) {
        throw "Another emergency CAC fallback launcher is already running."
    }
    $ownsMutex = $true

    $paths = [ordered]@{
        Data                = Resolve-PortablePath "data"
        Logs                = Resolve-PortablePath "data\logs"
        Temp                = Resolve-PortablePath "data\temp"
        AppDirectory        = Resolve-PortablePath "app"
        PlatformDirectory   = Resolve-PortablePath "app\platforms"
        Qt                  = Resolve-PortablePath "app\qt-cac-app.exe"
        QtPlatformPlugin    = Resolve-PortablePath "app\platforms\qwindows.dll"
        Jar                 = Resolve-PortablePath "backend\cac-backend.jar"
        Java                = Resolve-PortablePath "runtime\jre\bin\java.exe"
        CacPython           = Resolve-PortablePath "runtime\python-cac\python.exe"
        VmtkPython          = Resolve-PortablePath "runtime\python-vmtk\python.exe"
        LocalCli            = Resolve-PortablePath "inference\scripts\local_cac_cli.py"
        RecalculateScript   = Resolve-PortablePath "inference\scripts\recalculate_agatston.py"
        ConvertScript       = Resolve-PortablePath "inference\scripts\convert_case_to_raw_volume.py"
        VesselScript        = Resolve-PortablePath "inference\vessel\vmtk_straighten_vessel.py"
        SegmentCacsRoot     = Resolve-PortablePath "inference\segment-cacs"
        Model               = Resolve-PortablePath "inference\model\SegmentCACS_0001619_unet.pt"
        BackendLog          = Resolve-PortablePath "data\logs\backend.log"
    }

    Assert-RequiredFile "Qt application" $paths.Qt
    Assert-RequiredFile "Qt Windows platform plugin" $paths.QtPlatformPlugin
    Assert-RequiredFile "backend JAR" $paths.Jar
    Assert-RequiredFile "private Java runtime" $paths.Java
    Assert-RequiredFile "CAC Python runtime" $paths.CacPython
    Assert-RequiredFile "VMTK Python runtime" $paths.VmtkPython
    Assert-RequiredFile "local inference CLI" $paths.LocalCli
    Assert-RequiredFile "recalculation script" $paths.RecalculateScript
    Assert-RequiredFile "volume conversion script" $paths.ConvertScript
    Assert-RequiredFile "vessel straightening script" $paths.VesselScript
    Assert-RequiredDirectory "SEGMENT-CACS source" $paths.SegmentCacsRoot
    Assert-RequiredFile "SEGMENT-CACS checkpoint" $paths.Model

    foreach ($relativeDirectory in @(
        "data",
        "data\db",
        "data\jobs",
        "data\logs",
        "data\cache",
        "data\qt-cache",
        "data\temp",
        "data\vessel-straightening"
    )) {
        [void] [IO.Directory]::CreateDirectory(
            (Resolve-PortablePath $relativeDirectory))
    }

    $writeProbe = Join-Path $paths.Temp (
        ".cac-fallback-write-test-{0}.tmp" -f $PID)
    try {
        [IO.File]::WriteAllText($writeProbe, "write-test")
    }
    finally {
        if (Test-Path -LiteralPath $writeProbe -PathType Leaf) {
            Remove-Item -LiteralPath $writeProbe -Force
        }
    }

    Test-BackendPortAvailable

    $backendStart = [Diagnostics.ProcessStartInfo]::new()
    $backendStart.FileName = $paths.Java
    $backendStart.WorkingDirectory = $root
    $backendStart.UseShellExecute = $false
    $backendStart.CreateNoWindow = $true
    $backendStart.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    $backendStart.RedirectStandardOutput = $true
    $backendStart.RedirectStandardError = $true
    Set-PortableChildEnvironment $backendStart

    $backendArguments = @(
        "-jar",
        $paths.Jar,
        "--spring.profiles.active=local-windows",
        "--server.address=127.0.0.1",
        "--server.port=$BackendPort",
        "--management.endpoint.shutdown.enabled=true",
        "--management.endpoints.web.exposure.include=health,shutdown",
        "--spring.lifecycle.timeout-per-shutdown-phase=20s"
    )
    $backendStart.Arguments = (
        $backendArguments |
            ForEach-Object { Quote-ProcessArgument ([string] $_) }
    ) -join " "

    $backend = [Diagnostics.Process]::new()
    $backend.StartInfo = $backendStart
    if (-not $backend.Start()) {
        throw "The private Java backend process did not start."
    }
    $backendStarted = $true
    $stdoutTask = $backend.StandardOutput.ReadToEndAsync()
    $stderrTask = $backend.StandardError.ReadToEndAsync()

    Write-Host "Starting local CAC backend PID $($backend.Id)..."
    $deadline = [DateTime]::UtcNow.AddSeconds($BackendStartupTimeoutSeconds)
    $ready = $false
    while ([DateTime]::UtcNow -lt $deadline) {
        if ($backend.HasExited) {
            Write-CapturedBackendStreams
            throw "The local backend exited before health became UP. Exit code: $($backend.ExitCode)`nLog: $($paths.BackendLog)"
        }
        if (Test-BackendHealth) {
            $ready = $true
            break
        }
        Start-Sleep -Milliseconds 500
    }
    if (-not $ready) {
        throw "The local backend did not become healthy within $BackendStartupTimeoutSeconds seconds.`nLog: $($paths.BackendLog)"
    }

    Write-Host "READY: http://127.0.0.1:$BackendPort/actuator/health is UP."

    $qtStart = [Diagnostics.ProcessStartInfo]::new()
    $qtStart.FileName = $paths.Qt
    $qtStart.WorkingDirectory = $root
    $qtStart.UseShellExecute = $false
    $qtStart.CreateNoWindow = $false
    Set-PortableChildEnvironment $qtStart

    $qt = [Diagnostics.Process]::new()
    $qt.StartInfo = $qtStart
    if (-not $qt.Start()) {
        throw "The CAC Qt application did not start."
    }

    $backendExitedEarly = $false
    while (-not $qt.HasExited) {
        if ($backend.HasExited) {
            $backendExitedEarly = $true
            Write-CapturedBackendStreams
            Write-Error "The backend stopped while Qt was open. Close the CAC window, then review $($paths.BackendLog)"
            break
        }
        Start-Sleep -Milliseconds 250
    }
    if (-not $qt.HasExited) {
        $qt.WaitForExit()
    }

    $forcedStop = $false
    if (-not $backendExitedEarly) {
        $forcedStop = Stop-ExactBackend
    }
    else {
        Write-CapturedBackendStreams
    }

    if ($forcedStop) {
        Write-Warning "Graceful shutdown timed out. Only backend PID $($backend.Id) was terminated."
    }
    if ($qt.ExitCode -ne 0) {
        throw "The CAC Qt application exited with code $($qt.ExitCode)."
    }
    if ($backendExitedEarly) {
        throw "The local backend exited unexpectedly while Qt was open."
    }
}
catch {
    $exitCode = 1
    Write-Error $_.Exception.Message
}
finally {
    if ($null -ne $backend -and
        $backendStarted -and
        -not $backend.HasExited) {
        try {
            [void] (Stop-ExactBackend -TimeoutMilliseconds 5000)
        }
        catch {
            # Last resort remains scoped to the exact child created above.
            try {
                if (-not $backend.HasExited) {
                    $backend.Kill()
                    [void] $backend.WaitForExit(5000)
                }
            }
            catch {
                Write-Warning "Could not confirm exit of backend PID $($backend.Id)."
            }
        }
    }
    Write-CapturedBackendStreams

    if ($null -ne $qt) {
        $qt.Dispose()
    }
    if ($null -ne $backend) {
        $backend.Dispose()
    }
    if ($ownsMutex -and $null -ne $mutex) {
        try {
            $mutex.ReleaseMutex()
        }
        catch {
        }
    }
    if ($null -ne $mutex) {
        $mutex.Dispose()
    }
}

exit $exitCode
