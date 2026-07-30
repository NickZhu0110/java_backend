[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$StageRoot,

    [string]$ApplicationVersion = 'v0.6.10 win heart process',

    [ValidatePattern('^[0-9A-Fa-f]{40}$')]
    [string]$GitCommit = 'bab1caf4adeeb1481b2fab5125150f00ce502974',

    [string]$QtVersion = '6.8.3',
    [string]$VtkCppVersion = '9.6',
    [string]$JavaVersion = '21',
    [string]$CacPythonVersion = '3.10.11',
    [string]$PyTorchVersion = '2.1.2+cpu',
    [string]$VmtkPythonVersion = '3.10.16',
    [string]$VmtkVersion = '1.5',
    [string]$VtkPythonVersion = '9.2.6'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Resolve-StageDirectory {
    param([Parameter(Mandatory = $true)][string]$Path)

    if (-not (Test-Path -LiteralPath $Path -PathType Container)) {
        throw "StageRoot does not exist or is not a directory: $Path"
    }

    $resolved = (Resolve-Path -LiteralPath $Path).ProviderPath
    $fullPath = [System.IO.Path]::GetFullPath($resolved).TrimEnd('\', '/')
    $volumeRoot = [System.IO.Path]::GetPathRoot($fullPath).TrimEnd('\', '/')
    if ([string]::Equals(
            $fullPath,
            $volumeRoot,
            [System.StringComparison]::OrdinalIgnoreCase)) {
        throw 'Refusing to create a manifest for an entire filesystem volume.'
    }
    return $fullPath
}

function Get-RelativePath {
    param(
        [Parameter(Mandatory = $true)][string]$BasePath,
        [Parameter(Mandatory = $true)][string]$ChildPath
    )

    $baseFull = [System.IO.Path]::GetFullPath($BasePath).TrimEnd('\', '/') + '\'
    $childFull = [System.IO.Path]::GetFullPath($ChildPath)
    if (-not $childFull.StartsWith(
            $baseFull,
            [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "A manifest path escaped StageRoot: $childFull"
    }

    $baseUri = New-Object System.Uri($baseFull)
    $childUri = New-Object System.Uri($childFull)
    $relativeUri = $baseUri.MakeRelativeUri($childUri)
    return [System.Uri]::UnescapeDataString(
        $relativeUri.ToString()).Replace('\', '/')
}

$stage = Resolve-StageDirectory -Path $StageRoot
$manifestPath = Join-Path $stage 'manifest.json'
$sumsPath = Join-Path $stage 'SHA256SUMS.txt'
$excludedRelativePaths = @('manifest.json', 'SHA256SUMS.txt')

$payloadFiles = New-Object 'System.Collections.Generic.List[object]'
$relativePaths = New-Object 'System.Collections.Generic.List[string]'

foreach ($file in Get-ChildItem -LiteralPath $stage -Force -File -Recurse) {
    if (($file.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Reparse points are forbidden in the release stage: $($file.FullName)"
    }

    $relativePath = Get-RelativePath -BasePath $stage -ChildPath $file.FullName
    if ($relativePath -match '[\r\n]') {
        throw "A release filename contains a newline and cannot be manifested safely: $relativePath"
    }
    if ($excludedRelativePaths -contains $relativePath) {
        continue
    }
    $relativePaths.Add($relativePath) | Out-Null
}

$relativePathArray = $relativePaths.ToArray()
[System.Array]::Sort(
    $relativePathArray,
    [System.StringComparer]::OrdinalIgnoreCase)

$totalBytes = [int64]0
foreach ($relativePath in $relativePathArray) {
    $nativeRelativePath = $relativePath.Replace('/', '\')
    $fullPath = Join-Path $stage $nativeRelativePath
    $fileInfo = Get-Item -LiteralPath $fullPath
    $sha256 = (Get-FileHash -LiteralPath $fullPath -Algorithm SHA256).Hash.ToLowerInvariant()
    $totalBytes += [int64]$fileInfo.Length
    $payloadFiles.Add([pscustomobject][ordered]@{
            path   = $relativePath
            size   = [int64]$fileInfo.Length
            sha256 = $sha256
        }) | Out-Null
}

$manifest = [pscustomobject][ordered]@{
    schemaVersion      = 1
    applicationVersion = $ApplicationVersion
    source             = [pscustomobject][ordered]@{
        gitCommit = $GitCommit.ToLowerInvariant()
    }
    generatedAtUtc     = [DateTime]::UtcNow.ToString(
        'yyyy-MM-ddTHH:mm:ss.fffZ',
        [System.Globalization.CultureInfo]::InvariantCulture)
    platform           = 'Windows x64'
    inferenceDevice    = 'CPU'
    runtimeVersions    = [pscustomobject][ordered]@{
        qt          = $QtVersion
        vtkCpp      = $VtkCppVersion
        java        = $JavaVersion
        pythonCac   = $CacPythonVersion
        pytorch     = $PyTorchVersion
        pythonVmtk  = $VmtkPythonVersion
        vmtk        = $VmtkVersion
        vtkPython   = $VtkPythonVersion
    }
    integrityScope     = [pscustomobject][ordered]@{
        algorithm     = 'SHA-256'
        includes      = 'All regular payload files below the package root.'
        excludes      = $excludedRelativePaths
        exclusionNote = 'manifest.json and SHA256SUMS.txt are metadata and are intentionally excluded from both integrity lists to avoid self-reference.'
    }
    payload            = [pscustomobject][ordered]@{
        fileCount = $payloadFiles.Count
        totalBytes = $totalBytes
        files     = $payloadFiles.ToArray()
    }
}

$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
$manifestJson = $manifest | ConvertTo-Json -Depth 8
[System.IO.File]::WriteAllText(
    $manifestPath,
    $manifestJson + [Environment]::NewLine,
    $utf8NoBom)

$sumLines = New-Object 'System.Collections.Generic.List[string]'
foreach ($fileRecord in $payloadFiles) {
    $sumLines.Add("$($fileRecord.sha256) *$($fileRecord.path)") | Out-Null
}
$sumText = [string]::Join("`n", $sumLines.ToArray())
if ($sumLines.Count -gt 0) {
    $sumText += "`n"
}
[System.IO.File]::WriteAllText($sumsPath, $sumText, $utf8NoBom)

Write-Host "Release manifest written: $manifestPath" -ForegroundColor Green
Write-Host "SHA-256 sums written: $sumsPath" -ForegroundColor Green
Write-Host "Payload: $($payloadFiles.Count) file(s), $totalBytes byte(s)."

[pscustomobject][ordered]@{
    manifestPath  = $manifestPath
    sumsPath      = $sumsPath
    fileCount     = $payloadFiles.Count
    totalBytes    = $totalBytes
    gitCommit     = $GitCommit.ToLowerInvariant()
}
