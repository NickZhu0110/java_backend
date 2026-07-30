[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$StageRoot,

    [string[]]$AdditionalAllowedTopLevel = @(),

    [string[]]$ApprovedXmlRelativePath = @(),

    [string[]]$ApprovedIdentifierPrefix = @(
        'inference\segment-cacs\src',
        'inference\segment-cacs\model-service-python',
        'inference\vessel',
        'runtime'
    ),

    [string]$ApprovedCheckpointRelativePath =
        'inference\model\SegmentCACS_0001619_unet.pt',

    [ValidatePattern('^[0-9A-Fa-f]{64}$')]
    [string]$ExpectedCheckpointSha256,

    [string]$ApprovedFindingManifestPath,

    [string]$JsonReportPath,

    [switch]$RequireCompleteLayout
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-NormalizedRelativePath {
    param([Parameter(Mandatory = $true)][string]$Path)

    $normalized = $Path.Replace('/', '\').Trim()
    while ($normalized.StartsWith('.\', [System.StringComparison]::Ordinal)) {
        $normalized = $normalized.Substring(2)
    }
    return $normalized.TrimStart('\')
}

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
        throw 'Refusing to scan an entire filesystem volume as a release stage.'
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
        throw "A scanned path escaped StageRoot: $childFull"
    }

    $baseUri = New-Object System.Uri($baseFull)
    $childUri = New-Object System.Uri($childFull)
    $relativeUri = $baseUri.MakeRelativeUri($childUri)
    return [System.Uri]::UnescapeDataString(
        $relativeUri.ToString()).Replace('/', '\')
}

function Test-RelativePrefix {
    param(
        [Parameter(Mandatory = $true)][string]$RelativePath,
        [Parameter(Mandatory = $true)][string[]]$Prefixes
    )

    foreach ($prefix in $Prefixes) {
        $normalizedPrefix = (Get-NormalizedRelativePath -Path $prefix).TrimEnd('\')
        if ([string]::IsNullOrWhiteSpace($normalizedPrefix)) {
            continue
        }
        if ([string]::Equals(
                $RelativePath,
                $normalizedPrefix,
                [System.StringComparison]::OrdinalIgnoreCase) -or
            $RelativePath.StartsWith(
                $normalizedPrefix + '\',
                [System.StringComparison]::OrdinalIgnoreCase)) {
            return $true
        }
    }
    return $false
}

function Add-Violation {
    param(
        [Parameter(Mandatory = $true)][string]$Rule,
        [Parameter(Mandatory = $true)][string]$RelativePath,
        [Parameter(Mandatory = $true)][string]$Detail
    )

    $script:Violations.Add([pscustomobject][ordered]@{
            rule   = $Rule
            path   = $RelativePath
            detail = $Detail
        }) | Out-Null
}

function Test-ProbablyTextFile {
    param([Parameter(Mandatory = $true)][string]$Path)

    $stream = $null
    try {
        $stream = [System.IO.File]::Open(
            $Path,
            [System.IO.FileMode]::Open,
            [System.IO.FileAccess]::Read,
            [System.IO.FileShare]::Read)
        $buffer = New-Object byte[] 4096
        $count = $stream.Read($buffer, 0, $buffer.Length)
        if ($count -eq 0) {
            return $true
        }
        if ($count -ge 2 -and
            (($buffer[0] -eq 0xFF -and $buffer[1] -eq 0xFE) -or
             ($buffer[0] -eq 0xFE -and $buffer[1] -eq 0xFF))) {
            return $true
        }

        $controlBytes = 0
        for ($index = 0; $index -lt $count; $index++) {
            $value = $buffer[$index]
            if ($value -eq 0) {
                return $false
            }
            if ($value -lt 9 -or ($value -gt 13 -and $value -lt 32)) {
                $controlBytes++
            }
        }
        return (($controlBytes / [double]$count) -lt 0.02)
    }
    finally {
        if ($null -ne $stream) {
            $stream.Dispose()
        }
    }
}

$stage = Resolve-StageDirectory -Path $StageRoot
$approvedCheckpoint = Get-NormalizedRelativePath -Path $ApprovedCheckpointRelativePath
$approvedXml = [System.Collections.Generic.HashSet[string]]::new(
    [System.StringComparer]::OrdinalIgnoreCase)
foreach ($relativePath in $ApprovedXmlRelativePath) {
    $normalized = Get-NormalizedRelativePath -Path $relativePath
    if (-not [string]::IsNullOrWhiteSpace($normalized)) {
        $approvedXml.Add($normalized) | Out-Null
    }
}

$allowedTopLevel = [System.Collections.Generic.HashSet[string]]::new(
    [System.StringComparer]::OrdinalIgnoreCase)
@(
    'CACLauncher.exe',
    'Start-CAC.cmd',
    'app',
    'backend',
    'runtime',
    'inference',
    'config',
    'data',
    'LICENSES',
    'README.txt',
    'manifest.json',
    'SHA256SUMS.txt'
) + $AdditionalAllowedTopLevel | ForEach-Object {
    if ([string]::IsNullOrWhiteSpace($_) -or $_ -match '[\\/]') {
        throw "Invalid top-level allowlist entry: '$_'"
    }
    $allowedTopLevel.Add($_) | Out-Null
}

$topLevelFiles = [System.Collections.Generic.HashSet[string]]::new(
    [System.StringComparer]::OrdinalIgnoreCase)
@(
    'CACLauncher.exe',
    'Start-CAC.cmd',
    'README.txt',
    'manifest.json',
    'SHA256SUMS.txt'
) | ForEach-Object { $topLevelFiles.Add($_) | Out-Null }

$topLevelDirectories = [System.Collections.Generic.HashSet[string]]::new(
    [System.StringComparer]::OrdinalIgnoreCase)
@(
    'app',
    'backend',
    'runtime',
    'inference',
    'config',
    'data',
    'LICENSES'
) | ForEach-Object { $topLevelDirectories.Add($_) | Out-Null }

$requiredLayout = @(
    'CACLauncher.exe',
    'Start-CAC.cmd',
    'app',
    'backend',
    'runtime\jre',
    'runtime\python-cac',
    'runtime\python-vmtk',
    'inference\segment-cacs',
    'inference\vessel',
    'inference\model',
    'config',
    'data\db',
    'data\jobs',
    'data\logs',
    'data\cache',
    'data\temp',
    'data\vessel-straightening',
    'LICENSES',
    'README.txt'
)

$forbiddenDirectoryNames = [System.Collections.Generic.HashSet[string]]::new(
    [System.StringComparer]::OrdinalIgnoreCase)
@(
    '.git',
    '.svn',
    '.hg',
    '.pytest_cache',
    '__pycache__',
    '.mypy_cache',
    '.ruff_cache',
    '.cache',
    'local-data',
    'node_modules',
    'pkgs',
    'predictions',
    'outputs'
) | ForEach-Object { $forbiddenDirectoryNames.Add($_) | Out-Null }

$forbiddenSuffixes = @(
    '.dcm',
    '.dicom',
    '.nii',
    '.nii.gz',
    '.nrrd',
    '.raw',
    '.mhd',
    '.mha',
    '.mv.db',
    '.trace.db',
    '.lock.db',
    '.env',
    '.log',
    '.pid',
    '.tgz',
    '.tar.gz'
)

$modelSuffixes = @('.pt', '.pth', '.ckpt', '.onnx')
$textExtensions = [System.Collections.Generic.HashSet[string]]::new(
    [System.StringComparer]::OrdinalIgnoreCase)
@(
    '.txt', '.md', '.json', '.yaml', '.yml', '.ini', '.cfg', '.conf',
    '.properties', '.xml', '.csv', '.tsv', '.ps1', '.psm1', '.psd1',
    '.cmd', '.bat', '.py', '.pyi', '.java', '.kt', '.c', '.cc', '.cpp',
    '.cxx', '.h', '.hh', '.hpp', '.cmake', '.qrc', '.ui', '.qml',
    '.license', '.notice'
) | ForEach-Object { $textExtensions.Add($_) | Out-Null }

$forbiddenTextRules = [ordered]@{
    'absolute-user-profile' = '(?i)\b[A-Z]:[\\/]+Users[\\/]+[^\\/\s"'';]+'
    'qt-development-path'   = '(?i)\bC:[\\/]+Qt(?:[\\/]|$)'
    'vtk-development-path'  = '(?i)\bC:[\\/]+dev(?:[\\/]|$)'
    'medical-source-path'   = '(?i)\bE:[\\/]+files(?:[\\/]|$)'
    'unix-root-path'        = '(?i)(?<![A-Za-z0-9_.-])/(?:root|data|mnt)(?:/|$)'
    'legacy-backend-url'    = '(?i)(?:localhost|127\.0\.0\.1):8080'
    'wildcard-bind-address' = '(?<![0-9])0\.0\.0\.0(?![0-9])'
}

$Violations = New-Object 'System.Collections.Generic.List[object]'

foreach ($entry in Get-ChildItem -LiteralPath $stage -Force) {
    if (-not $allowedTopLevel.Contains($entry.Name)) {
        Add-Violation -Rule 'top-level-allowlist' -RelativePath $entry.Name `
            -Detail 'Entry is not in the portable release top-level allowlist.'
    }
    elseif ($topLevelFiles.Contains($entry.Name) -and $entry.PSIsContainer) {
        Add-Violation -Rule 'top-level-entry-type' -RelativePath $entry.Name `
            -Detail 'This top-level entry must be a file.'
    }
    elseif ($topLevelDirectories.Contains($entry.Name) -and -not $entry.PSIsContainer) {
        Add-Violation -Rule 'top-level-entry-type' -RelativePath $entry.Name `
            -Detail 'This top-level entry must be a directory.'
    }
}

if ($RequireCompleteLayout) {
    foreach ($requiredRelativePath in $requiredLayout) {
        if (-not (Test-Path -LiteralPath (Join-Path $stage $requiredRelativePath))) {
            Add-Violation -Rule 'required-layout' `
                -RelativePath $requiredRelativePath `
                -Detail 'Required release entry is missing.'
        }
    }
}

$entries = @(Get-ChildItem -LiteralPath $stage -Force -Recurse)
foreach ($entry in $entries) {
    $relativePath = Get-RelativePath -BasePath $stage -ChildPath $entry.FullName

    if (($entry.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        Add-Violation -Rule 'reparse-point' -RelativePath $relativePath `
            -Detail 'Symlinks and junctions are not allowed in the release stage.'
        continue
    }

    if ($entry.PSIsContainer) {
        if ($forbiddenDirectoryNames.Contains($entry.Name)) {
            Add-Violation -Rule 'runtime-cache-or-repository' `
                -RelativePath $relativePath `
                -Detail "Forbidden directory '$($entry.Name)' is present."
        }
        continue
    }

    if ($relativePath.StartsWith(
            'data\',
            [System.StringComparison]::OrdinalIgnoreCase)) {
        Add-Violation -Rule 'seeded-runtime-data' -RelativePath $relativePath `
            -Detail 'Shipped data directories must be empty.'
    }

    if ($relativePath -match
        '(?i)^inference\\(?:data|datasets?|train(?:ing)?|tests?|testdata|fixtures|predictions?|outputs?)(?:\\|$)') {
        Add-Violation -Rule 'inference-dataset' -RelativePath $relativePath `
            -Detail 'Training, test, prediction, and generated inference data are forbidden.'
    }

    $lowerName = $entry.Name.ToLowerInvariant()
    $isRuntimePthFile =
        $lowerName.EndsWith('.pth', [System.StringComparison]::Ordinal) -and
        $relativePath.StartsWith(
            'runtime\',
            [System.StringComparison]::OrdinalIgnoreCase) -and
        (Test-ProbablyTextFile -Path $entry.FullName)
    foreach ($suffix in $forbiddenSuffixes) {
        if ($lowerName.EndsWith($suffix, [System.StringComparison]::Ordinal)) {
            Add-Violation -Rule 'forbidden-file-type' -RelativePath $relativePath `
                -Detail "Files ending in '$suffix' are forbidden."
            break
        }
    }

    if ($lowerName.EndsWith('.xml', [System.StringComparison]::Ordinal)) {
        $isLicenseXml = $relativePath.StartsWith(
            'LICENSES\',
            [System.StringComparison]::OrdinalIgnoreCase)
        if (-not $isLicenseXml -and -not $approvedXml.Contains($relativePath)) {
            Add-Violation -Rule 'unapproved-xml' -RelativePath $relativePath `
                -Detail 'XML is allowed only under LICENSES or by exact relative-path approval.'
        }
    }

    foreach ($modelSuffix in $modelSuffixes) {
        if ($lowerName.EndsWith($modelSuffix, [System.StringComparison]::Ordinal)) {
            if ($modelSuffix -eq '.pth' -and $isRuntimePthFile) {
                break
            }
            if (-not [string]::Equals(
                    $relativePath,
                    $approvedCheckpoint,
                    [System.StringComparison]::OrdinalIgnoreCase)) {
                Add-Violation -Rule 'unapproved-model-file' `
                    -RelativePath $relativePath `
                    -Detail 'Only the explicitly approved checkpoint path is allowed.'
            }
            break
        }
    }

    $isApprovedSource = Test-RelativePrefix -RelativePath $relativePath `
        -Prefixes $ApprovedIdentifierPrefix
    if (-not $isApprovedSource -and
        $entry.BaseName -match
        '(?i)(patient|dicom|corrected[_-]?mask|ai[_-]?mask|centerline|straightened|(?:^|[_-])cpr(?:[_-]|$))' -and
        $entry.Extension -match
        '(?i)^\.(json|csv|tsv|txt|png|jpe?g|bmp|tiff?|vtk|vtp|vti)$') {
        Add-Violation -Rule 'medical-artifact-name' -RelativePath $relativePath `
            -Detail 'A likely patient, mask, centerline, or CPR artifact is present.'
    }

    if ([string]::Equals(
            $relativePath,
            $approvedCheckpoint,
            [System.StringComparison]::OrdinalIgnoreCase) -and
        -not [string]::IsNullOrWhiteSpace($ExpectedCheckpointSha256)) {
        $actualCheckpointHash = (Get-FileHash -LiteralPath $entry.FullName `
                -Algorithm SHA256).Hash
        if (-not [string]::Equals(
                $actualCheckpointHash,
                $ExpectedCheckpointSha256,
                [System.StringComparison]::OrdinalIgnoreCase)) {
            Add-Violation -Rule 'checkpoint-hash' -RelativePath $relativePath `
                -Detail 'Checkpoint SHA-256 does not match the approved value.'
        }
    }

    if ([string]::Equals(
            $relativePath,
            'manifest.json',
            [System.StringComparison]::OrdinalIgnoreCase) -or
        [string]::Equals(
            $relativePath,
            'SHA256SUMS.txt',
            [System.StringComparison]::OrdinalIgnoreCase)) {
        continue
    }

    $isExtensionlessText =
        [string]::IsNullOrEmpty($entry.Extension) -and
        (Test-ProbablyTextFile -Path $entry.FullName)
    if (-not $textExtensions.Contains($entry.Extension) -and
        -not $isRuntimePthFile -and
        -not $isExtensionlessText) {
        continue
    }

    $reader = $null
    try {
        $reader = New-Object System.IO.StreamReader(
            $entry.FullName,
            [System.Text.Encoding]::UTF8,
            $true)
        $lineNumber = 0
        while (-not $reader.EndOfStream) {
            $line = $reader.ReadLine()
            $lineNumber++

            foreach ($ruleName in $forbiddenTextRules.Keys) {
                if ($line -match $forbiddenTextRules[$ruleName]) {
                    Add-Violation -Rule $ruleName -RelativePath $relativePath `
                        -Detail "Forbidden text found at line $lineNumber."
                }
            }

            if ($line -match '(?i)Patient[_-]?(?:Name|ID)') {
                if (-not $isApprovedSource) {
                    Add-Violation -Rule 'patient-identifier' `
                        -RelativePath $relativePath `
                        -Detail "PatientName/PatientID found outside approved source at line $lineNumber."
                }
                else {
                    $literalMatch = [System.Text.RegularExpressions.Regex]::Match(
                        $line,
                        '(?i)Patient[_-]?(?:Name|ID)["'']?\s*[:=]\s*["''](?<value>[^"'']+)["'']')
                    if ($literalMatch.Success) {
                        $literalValue = $literalMatch.Groups['value'].Value.Trim()
                        if ($literalValue -notmatch
                            '(?i)^(anonymous|anonymized|unknown|none|null|test|example|sample|notset)$') {
                            Add-Violation -Rule 'literal-patient-identifier' `
                                -RelativePath $relativePath `
                                -Detail "A non-placeholder patient identifier literal was found at line $lineNumber."
                        }
                    }
                }
            }

            $isConfigurationText =
                -not $isApprovedSource -and
                ($relativePath.StartsWith(
                        'config\',
                        [System.StringComparison]::OrdinalIgnoreCase) -or
                 -not $relativePath.Contains('\'))
            if ($isConfigurationText -and
                $line -match
                '(?i)(?:password|secret|api[_-]?key|access[_-]?token)\s*[:=]\s*["'']?[^"''\s;]+') {
                Add-Violation -Rule 'credential-like-value' `
                    -RelativePath $relativePath `
                    -Detail "A credential-like value was found at line $lineNumber."
            }
        }
    }
    catch {
        Add-Violation -Rule 'text-scan-error' -RelativePath $relativePath `
            -Detail "Could not scan text safely: $($_.Exception.Message)"
    }
    finally {
        if ($null -ne $reader) {
            $reader.Dispose()
        }
    }
}

$approvedFindings = New-Object 'System.Collections.Generic.List[object]'
$effectiveViolations = New-Object 'System.Collections.Generic.List[object]'

if (-not [string]::IsNullOrWhiteSpace($ApprovedFindingManifestPath)) {
    if (-not (Test-Path -LiteralPath $ApprovedFindingManifestPath -PathType Leaf)) {
        throw "Approved finding manifest does not exist: $ApprovedFindingManifestPath"
    }

    $approvalDocument = Get-Content -LiteralPath $ApprovedFindingManifestPath `
        -Raw | ConvertFrom-Json
    if ($approvalDocument.schemaVersion -ne 1 -or
        $null -eq $approvalDocument.entries) {
        throw 'Approved finding manifest must use schemaVersion 1 and contain entries.'
    }

    # These are portability-only findings that can occur in audited upstream
    # code, metadata, examples, or required runtime XML resources. Medical
    # files, patient identifiers, credentials, repository/cache artifacts,
    # packaging-machine paths, and development-source paths are intentionally
    # absent and can never be approved through this mechanism.
    $approvableRules = [System.Collections.Generic.HashSet[string]]::new(
        [System.StringComparer]::OrdinalIgnoreCase)
    @(
        'absolute-user-profile',
        'unix-root-path',
        'legacy-backend-url',
        'wildcard-bind-address',
        'unapproved-xml'
    ) | ForEach-Object { $approvableRules.Add($_) | Out-Null }

    $approvedKeys = [System.Collections.Generic.HashSet[string]]::new(
        [System.StringComparer]::OrdinalIgnoreCase)
    $usedApprovedKeys = [System.Collections.Generic.HashSet[string]]::new(
        [System.StringComparer]::OrdinalIgnoreCase)
    $approvedEntriesByKey = @{}

    foreach ($approval in @($approvalDocument.entries)) {
        $approvalPath = Get-NormalizedRelativePath -Path ([string]$approval.path)
        $approvalRule = [string]$approval.rule
        $approvalHash = ([string]$approval.sha256).ToUpperInvariant()
        if ([string]::IsNullOrWhiteSpace($approvalPath) -or
            -not $approvableRules.Contains($approvalRule) -or
            $approvalHash -notmatch '^[0-9A-F]{64}$') {
            throw "Invalid or non-approvable finding entry: $($approval | ConvertTo-Json -Compress)"
        }

        $approvalFullPath = Join-Path $stage $approvalPath
        if (-not (Test-Path -LiteralPath $approvalFullPath -PathType Leaf)) {
            throw "Approved finding file is missing: $approvalPath"
        }
        $actualApprovalHash = (Get-FileHash -LiteralPath $approvalFullPath `
                -Algorithm SHA256).Hash.ToUpperInvariant()
        if ($actualApprovalHash -ne $approvalHash) {
            throw "Approved finding hash mismatch: $approvalPath"
        }

        $approvalKey = "$approvalPath|$approvalRule"
        if (-not $approvedKeys.Add($approvalKey)) {
            throw "Duplicate approved finding entry: $approvalKey"
        }
        $approvedEntriesByKey[$approvalKey] = $approval
    }

    foreach ($violation in $Violations) {
        $violationKey = "$($violation.path)|$($violation.rule)"
        if ($approvedKeys.Contains($violationKey)) {
            $usedApprovedKeys.Add($violationKey) | Out-Null
            $approvedFindings.Add($violation) | Out-Null
        }
        else {
            $effectiveViolations.Add($violation) | Out-Null
        }
    }

    foreach ($approvalKey in $approvedKeys) {
        if (-not $usedApprovedKeys.Contains($approvalKey)) {
            $approval = $approvedEntriesByKey[$approvalKey]
            $effectiveViolations.Add([pscustomobject][ordered]@{
                rule   = 'stale-approved-finding'
                path   = [string]$approval.path
                detail = "Approval no longer matches a scan finding for rule '$([string]$approval.rule)'."
            }) | Out-Null
        }
    }
}
else {
    foreach ($violation in $Violations) {
        $effectiveViolations.Add($violation) | Out-Null
    }
}

if (-not [string]::IsNullOrWhiteSpace($JsonReportPath)) {
    $reportDirectory = [System.IO.Path]::GetDirectoryName(
        [System.IO.Path]::GetFullPath($JsonReportPath))
    if (-not [string]::IsNullOrWhiteSpace($reportDirectory)) {
        [System.IO.Directory]::CreateDirectory($reportDirectory) | Out-Null
    }
    $report = [pscustomobject][ordered]@{
        stageRoot           = $stage
        totalFindingCount   = $Violations.Count
        approvedCount       = $approvedFindings.Count
        violationCount      = $effectiveViolations.Count
        approvedFindings    = $approvedFindings.ToArray()
        violations          = $effectiveViolations.ToArray()
    }
    [System.IO.File]::WriteAllText(
        [System.IO.Path]::GetFullPath($JsonReportPath),
        ($report | ConvertTo-Json -Depth 8),
        [System.Text.UTF8Encoding]::new($false))
}

if ($effectiveViolations.Count -gt 0) {
    Write-Host "RELEASE PRIVACY SCAN FAILED: $($effectiveViolations.Count) violation(s)." `
        -ForegroundColor Red
    $effectiveViolations |
        Sort-Object path, rule |
        Format-Table rule, path, detail -AutoSize |
        Out-String -Width 240 |
        Write-Host
    throw 'Release privacy scan failed. No package may be created from this stage.'
}

$fileCount = @($entries | Where-Object { -not $_.PSIsContainer }).Count
Write-Host "RELEASE PRIVACY SCAN PASSED: $fileCount file(s), no violations; " `
    "$($approvedFindings.Count) hash-pinned upstream finding(s) classified." `
    -ForegroundColor Green

[pscustomobject][ordered]@{
    stageRoot            = $stage
    fileCount            = $fileCount
    approvedCheckpoint   = $approvedCheckpoint
    approvedFindingCount = $approvedFindings.Count
    completeLayoutTested = [bool]$RequireCompleteLayout
    passed               = $true
}
