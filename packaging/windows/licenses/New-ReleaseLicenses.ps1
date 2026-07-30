[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$OutputRoot,

    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$BackendJar,

    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$QtLicenseRoot,

    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$QtSbomRoot,

    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$VtkLicenseRoot,

    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$JreRoot,

    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$CacPythonRoot,

    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$VmtkPythonRoot,

    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$CondaEnvironmentRoot,

    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string[]]$CondaPackageCacheRoot,

    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$SegmentCacsRoot,

    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$H2LicenseFile,

    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$VcRedistLicenseFile,

    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$CheckpointPath,

    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[0-9A-Fa-f]{64}$')]
    [string]$ExpectedCheckpointSha256,

    [string[]]$AdditionalLicenseFile = @()
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

$script:Utf8NoBom = New-Object System.Text.UTF8Encoding($false)
$script:WrittenFileCount = 0

function Resolve-RequiredFile {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Description
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Missing mandatory $Description file: $Path"
    }
    $item = Get-Item -LiteralPath $Path -Force
    if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Mandatory $Description file is a reparse point: $Path"
    }
    return [System.IO.Path]::GetFullPath($item.FullName)
}

function Resolve-RequiredDirectory {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Description
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Container)) {
        throw "Missing mandatory $Description directory: $Path"
    }
    $item = Get-Item -LiteralPath $Path -Force
    if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Mandatory $Description directory is a reparse point: $Path"
    }
    return [System.IO.Path]::GetFullPath($item.FullName).TrimEnd('\', '/')
}

function Assert-TextFileContains {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Pattern,
        [Parameter(Mandatory = $true)][string]$Description
    )

    $item = Get-Item -LiteralPath $Path -Force
    if ($item.Length -lt 100) {
        throw "Mandatory $Description is unexpectedly short: $Path"
    }
    $reader = New-Object System.IO.StreamReader(
        $Path,
        [System.Text.Encoding]::UTF8,
        $true)
    try {
        $content = $reader.ReadToEnd()
    }
    finally {
        $reader.Dispose()
    }
    if ($content -notmatch $Pattern) {
        throw "Mandatory $Description does not contain recognizable license text: $Path"
    }
}

function Test-PathWithinRoot {
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string]$Candidate
    )

    $rootFull = [System.IO.Path]::GetFullPath($Root).TrimEnd('\', '/') + '\'
    $candidateFull = [System.IO.Path]::GetFullPath($Candidate)
    return $candidateFull.StartsWith(
        $rootFull,
        [System.StringComparison]::OrdinalIgnoreCase)
}

function Get-RelativePath {
    param(
        [Parameter(Mandatory = $true)][string]$BasePath,
        [Parameter(Mandatory = $true)][string]$ChildPath
    )

    if (-not (Test-PathWithinRoot -Root $BasePath -Candidate $ChildPath)) {
        throw "Path escaped its declared source root: $ChildPath"
    }
    $baseFull = [System.IO.Path]::GetFullPath($BasePath).TrimEnd('\', '/') + '\'
    $childFull = [System.IO.Path]::GetFullPath($ChildPath)
    $baseUri = New-Object System.Uri($baseFull)
    $childUri = New-Object System.Uri($childFull)
    return [System.Uri]::UnescapeDataString(
        $baseUri.MakeRelativeUri($childUri).ToString()).Replace('/', '\')
}

function Assert-SafeRelativePath {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Description
    )

    if ([string]::IsNullOrWhiteSpace($Path) -or
        [System.IO.Path]::IsPathRooted($Path) -or
        $Path.IndexOf([char]0) -ge 0 -or
        $Path -match '^[\\/]' -or
        $Path -match '^[A-Za-z]:' -or
        $Path -match '[\r\n]') {
        throw "Unsafe $Description path: '$Path'"
    }

    $parts = $Path.Replace('\', '/').Split('/')
    $invalidFileNameCharacters = [System.IO.Path]::GetInvalidFileNameChars()
    foreach ($part in $parts) {
        if ([string]::IsNullOrWhiteSpace($part) -or
            $part -eq '.' -or
            $part -eq '..' -or
            $part.IndexOf(':') -ge 0 -or
            $part.IndexOfAny($invalidFileNameCharacters) -ge 0 -or
            $part.EndsWith(
                '.',
                [System.StringComparison]::Ordinal) -or
            $part.EndsWith(
                ' ',
                [System.StringComparison]::Ordinal)) {
            throw "Unsafe $Description path component in '$Path'"
        }
    }
    return ($parts -join '\')
}

function Assert-NoReparsePointInTree {
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string]$Description
    )

    foreach ($entry in Get-ChildItem -LiteralPath $Root -Force -Recurse) {
        if (($entry.Attributes -band
                [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Reparse point found in mandatory $Description`: $($entry.FullName)"
        }
    }
}

function Get-ObjectPropertyValue {
    param(
        [Parameter(Mandatory = $true)]$Object,
        [Parameter(Mandatory = $true)][string]$Name
    )

    $property = $Object.PSObject.Properties[$Name]
    if ($null -eq $property) {
        return $null
    }
    return $property.Value
}

function Get-SafeFileName {
    param([Parameter(Mandatory = $true)][string]$Name)

    $safe = [System.Text.RegularExpressions.Regex]::Replace(
        $Name,
        '[^A-Za-z0-9._-]',
        '_')
    $safe = $safe.Trim('.', '_')
    if ([string]::IsNullOrWhiteSpace($safe)) {
        throw "Cannot derive a safe filename from '$Name'."
    }
    return $safe
}

function Write-Utf8File {
    param(
        [Parameter(Mandatory = $true)][string]$RelativePath,
        [Parameter(Mandatory = $true)][AllowEmptyString()][string]$Content
    )

    $safeRelative = Assert-SafeRelativePath -Path $RelativePath `
        -Description 'output file'
    $destination = [System.IO.Path]::GetFullPath(
        (Join-Path $script:ResolvedOutput $safeRelative))
    if (-not (Test-PathWithinRoot -Root $script:ResolvedOutput `
            -Candidate $destination)) {
        throw "Output file escaped OutputRoot: $RelativePath"
    }
    $parent = Split-Path -Parent $destination
    if (-not (Test-Path -LiteralPath $parent)) {
        New-Item -ItemType Directory -Path $parent | Out-Null
    }
    if (Test-Path -LiteralPath $destination) {
        throw "Refusing to overwrite license output: $destination"
    }
    [System.IO.File]::WriteAllText(
        $destination,
        $Content,
        $script:Utf8NoBom)
    $script:WrittenFileCount++
}

function Copy-TrustedFile {
    param(
        [Parameter(Mandatory = $true)][string]$SourcePath,
        [Parameter(Mandatory = $true)][string]$RelativeDestination,
        [Parameter(Mandatory = $true)][string]$SourceRoot
    )

    $source = Resolve-RequiredFile -Path $SourcePath `
        -Description 'license source'
    if (-not (Test-PathWithinRoot -Root $SourceRoot -Candidate $source)) {
        throw "License source escaped its trusted root: $source"
    }
    $safeRelative = Assert-SafeRelativePath -Path $RelativeDestination `
        -Description 'license destination'
    $destination = [System.IO.Path]::GetFullPath(
        (Join-Path $script:ResolvedOutput $safeRelative))
    if (-not (Test-PathWithinRoot -Root $script:ResolvedOutput `
            -Candidate $destination)) {
        throw "License destination escaped OutputRoot: $RelativeDestination"
    }
    $parent = Split-Path -Parent $destination
    if (-not (Test-Path -LiteralPath $parent)) {
        New-Item -ItemType Directory -Path $parent | Out-Null
    }
    if (Test-Path -LiteralPath $destination) {
        throw "Refusing to overwrite license output: $destination"
    }
    [System.IO.File]::Copy($source, $destination, $false)
    $script:WrittenFileCount++
}

function Copy-CompleteLegalTree {
    param(
        [Parameter(Mandatory = $true)][string]$SourceRoot,
        [Parameter(Mandatory = $true)][string]$DestinationPrefix,
        [Parameter(Mandatory = $true)][string]$Description
    )

    $root = Resolve-RequiredDirectory -Path $SourceRoot `
        -Description $Description
    Assert-NoReparsePointInTree -Root $root -Description $Description
    $files = @(Get-ChildItem -LiteralPath $root -File -Force -Recurse)
    if ($files.Count -eq 0) {
        throw "Mandatory $Description contains no files: $root"
    }
    foreach ($file in $files) {
        $relative = Get-RelativePath -BasePath $root `
            -ChildPath $file.FullName
        Copy-TrustedFile -SourcePath $file.FullName `
            -RelativeDestination (Join-Path $DestinationPrefix $relative) `
            -SourceRoot $root
    }
    return $files.Count
}

function Copy-QtSpdxSbomTree {
    param(
        [Parameter(Mandatory = $true)][string]$SourceRoot,
        [Parameter(Mandatory = $true)][string]$DestinationPrefix
    )

    $root = Resolve-RequiredDirectory -Path $SourceRoot `
        -Description 'Qt SBOM source'
    Assert-NoReparsePointInTree -Root $root -Description 'Qt SBOM source'

    # Qt ships equivalent SPDX tag-value and JSON representations. Package the
    # canonical *.spdx files only: the JSON copies are redundant and include
    # upstream test-fixture absolute paths that violate the release privacy
    # policy without adding license coverage.
    $files = @(Get-ChildItem -LiteralPath $root -File -Force -Recurse |
            Where-Object {
                $_.Name.EndsWith(
                    '.spdx',
                    [System.StringComparison]::OrdinalIgnoreCase)
            })
    if ($files.Count -eq 0) {
        throw "Mandatory Qt SBOM source contains no .spdx files: $root"
    }
    foreach ($file in $files) {
        $relative = Get-RelativePath -BasePath $root `
            -ChildPath $file.FullName
        Copy-TrustedFile -SourcePath $file.FullName `
            -RelativeDestination (Join-Path $DestinationPrefix $relative) `
            -SourceRoot $root
    }
    return $files.Count
}

function Test-IsLegalFileName {
    param([Parameter(Mandatory = $true)][string]$Name)

    if ($Name -notmatch
        '^(?i)(LICENSE|LICENCE|NOTICE|COPYING|COPYRIGHT|LEGAL|AUTHORS|DEPENDENC(?:Y|IES)|THIRD[-_. ]?PARTY(?:[-_. ]?(?:LICENSES?|NOTICES?))?)(?:$|[._-].*)') {
        return $false
    }

    # Do not mistake source modules such as dependency_groups.py or an
    # importable package named "licenses" for distributable legal text.
    return [System.IO.Path]::GetExtension($Name) -notmatch
        '^(?i)\.(py|pyc|pyo|pyi|pyd|dll|exe|obj|lib)$'
}

function Test-IsArchiveLegalEntry {
    param([Parameter(Mandatory = $true)][string]$EntryName)

    $parts = $EntryName.Replace('\', '/').Split('/')
    foreach ($part in $parts) {
        if ($part -match '^(?i)(LICENSES?|LICENCES?|NOTICES?)$') {
            return $true
        }
    }
    return (Test-IsLegalFileName -Name $parts[$parts.Length - 1])
}

function Copy-RecognizedLegalFiles {
    param(
        [Parameter(Mandatory = $true)][string]$SourceRoot,
        [Parameter(Mandatory = $true)][string]$DestinationPrefix,
        [Parameter(Mandatory = $true)][string]$Description
    )

    $root = Resolve-RequiredDirectory -Path $SourceRoot `
        -Description $Description
    Assert-NoReparsePointInTree -Root $root -Description $Description
    $copied = 0
    foreach ($file in Get-ChildItem -LiteralPath $root -File -Force -Recurse) {
        $relative = Get-RelativePath -BasePath $root `
            -ChildPath $file.FullName
        $isLicenseDirectory = $false
        $isCacheDirectory = $false
        foreach ($segment in $relative.Split('\')) {
            if ($segment -match '^(?i)(__pycache__|\.pytest_cache|\.cache)$') {
                $isCacheDirectory = $true
                break
            }
            if ($segment -match '^(?i)(licenses?|licences?|notices?)$') {
                $isLicenseDirectory = $true
            }
        }
        if ($isCacheDirectory -or
            [System.IO.Path]::GetExtension($file.Name) -match
                '^(?i)\.(py|pyc|pyo|pyi|pyd|dll|exe|obj|lib)$') {
            continue
        }
        if (-not $isLicenseDirectory -and
            -not (Test-IsLegalFileName -Name $file.Name)) {
            continue
        }
        Copy-TrustedFile -SourcePath $file.FullName `
            -RelativeDestination (Join-Path $DestinationPrefix $relative) `
            -SourceRoot $root
        $copied++
    }
    if ($copied -eq 0) {
        throw "No recognizable legal files were found in mandatory $Description`: $root"
    }
    return $copied
}

function Get-ByteSha256 {
    param([Parameter(Mandatory = $true)][byte[]]$Bytes)

    $algorithm = [System.Security.Cryptography.SHA256]::Create()
    try {
        return ([System.BitConverter]::ToString(
                $algorithm.ComputeHash($Bytes))).Replace(
            '-',
            '').ToLowerInvariant()
    }
    finally {
        $algorithm.Dispose()
    }
}

function Get-StreamText {
    param([Parameter(Mandatory = $true)][System.IO.Stream]$Stream)

    $reader = New-Object System.IO.StreamReader(
        $Stream,
        [System.Text.Encoding]::UTF8,
        $true,
        4096,
        $true)
    try {
        return $reader.ReadToEnd()
    }
    finally {
        $reader.Dispose()
    }
}

function Get-PomCoordinate {
    param(
        [Parameter(Mandatory = $true)]
        [System.IO.Compression.ZipArchive]$Archive
    )

    $coordinates = New-Object 'System.Collections.Generic.List[string]'
    $pomEntries = @($Archive.Entries | Where-Object {
            $_.FullName -match
            '^(?i)META-INF/maven/[^/]+/[^/]+/pom\.properties$' -and
            -not [string]::IsNullOrWhiteSpace($_.Name)
        })
    foreach ($entry in $pomEntries) {
        if ($entry.Length -gt 1048576) {
            throw "Unreasonably large pom.properties in nested JAR: $($entry.FullName)"
        }
        $stream = $entry.Open()
        try {
            $text = Get-StreamText -Stream $stream
        }
        finally {
            $stream.Dispose()
        }
        $values = @{}
        foreach ($line in $text -split '\r?\n') {
            if ($line -match '^\s*([^#!][^=:\s]*)\s*[=:]\s*(.*?)\s*$') {
                $values[$matches[1]] = $matches[2]
            }
        }
        if ($values.ContainsKey('groupId') -and
            $values.ContainsKey('artifactId') -and
            $values.ContainsKey('version')) {
            $coordinates.Add(
                "$($values['groupId']):$($values['artifactId']):$($values['version'])") |
                Out-Null
        }
    }
    if ($coordinates.Count -eq 0) {
        return ''
    }
    return (($coordinates.ToArray() | Sort-Object -Unique) -join ';')
}

function Export-ArchiveLegalEntry {
    param(
        [Parameter(Mandatory = $true)]
        [System.IO.Compression.ZipArchiveEntry]$Entry,
        [Parameter(Mandatory = $true)][string]$DestinationPrefix
    )

    $safeArchivePath = Assert-SafeRelativePath -Path $Entry.FullName `
        -Description 'archive license'
    $safeDestination = Assert-SafeRelativePath `
        -Path (Join-Path $DestinationPrefix $safeArchivePath) `
        -Description 'archive license destination'
    $destination = [System.IO.Path]::GetFullPath(
        (Join-Path $script:ResolvedOutput $safeDestination))
    if (-not (Test-PathWithinRoot -Root $script:ResolvedOutput `
            -Candidate $destination)) {
        throw "Archive license escaped OutputRoot: $($Entry.FullName)"
    }
    if ($Entry.Length -gt 52428800) {
        throw "Unreasonably large embedded license entry: $($Entry.FullName)"
    }
    $parent = Split-Path -Parent $destination
    if (-not (Test-Path -LiteralPath $parent)) {
        New-Item -ItemType Directory -Path $parent | Out-Null
    }
    if (Test-Path -LiteralPath $destination) {
        throw "Duplicate embedded license destination: $safeDestination"
    }
    $inputStream = $Entry.Open()
    $outputStream = [System.IO.File]::Open(
        $destination,
        [System.IO.FileMode]::CreateNew,
        [System.IO.FileAccess]::Write,
        [System.IO.FileShare]::None)
    try {
        $inputStream.CopyTo($outputStream)
    }
    finally {
        $outputStream.Dispose()
        $inputStream.Dispose()
    }
    $script:WrittenFileCount++
}

function Export-BackendLicenses {
    param([Parameter(Mandatory = $true)][string]$JarPath)

    $records = New-Object 'System.Collections.Generic.List[object]'
    $missingNotice = New-Object 'System.Collections.Generic.List[string]'
    $jarStream = [System.IO.File]::OpenRead($JarPath)
    $archive = $null
    try {
        $archive = New-Object System.IO.Compression.ZipArchive(
            $jarStream,
            [System.IO.Compression.ZipArchiveMode]::Read,
            $false)

        $applicationLegalEntries = @($archive.Entries | Where-Object {
                -not [string]::IsNullOrWhiteSpace($_.Name) -and
                -not $_.FullName.StartsWith(
                    'BOOT-INF/lib/',
                    [System.StringComparison]::OrdinalIgnoreCase) -and
                (Test-IsArchiveLegalEntry -EntryName $_.FullName)
            })
        foreach ($entry in $applicationLegalEntries) {
            Export-ArchiveLegalEntry -Entry $entry `
                -DestinationPrefix 'backend\application'
        }

        $nestedEntries = @($archive.Entries | Where-Object {
                -not [string]::IsNullOrWhiteSpace($_.Name) -and
                $_.FullName.StartsWith(
                    'BOOT-INF/lib/',
                    [System.StringComparison]::OrdinalIgnoreCase) -and
                $_.FullName.EndsWith(
                    '.jar',
                    [System.StringComparison]::OrdinalIgnoreCase)
            } | Sort-Object FullName)
        if ($nestedEntries.Count -eq 0) {
            throw "Backend artifact is not a Spring Boot fat JAR with BOOT-INF/lib entries: $JarPath"
        }

        $index = 0
        foreach ($nestedEntry in $nestedEntries) {
            $index++
            if ($nestedEntry.Length -gt 536870912) {
                throw "Nested dependency is unreasonably large: $($nestedEntry.FullName)"
            }
            $safeEntryPath = Assert-SafeRelativePath `
                -Path $nestedEntry.FullName -Description 'nested JAR'
            $memory = New-Object System.IO.MemoryStream
            $nestedInput = $nestedEntry.Open()
            try {
                $nestedInput.CopyTo($memory)
            }
            finally {
                $nestedInput.Dispose()
            }
            $bytes = $memory.ToArray()
            $sha256 = Get-ByteSha256 -Bytes $bytes
            $memory.Position = 0
            $nestedArchive = $null
            try {
                $nestedArchive = New-Object System.IO.Compression.ZipArchive(
                    $memory,
                    [System.IO.Compression.ZipArchiveMode]::Read,
                    $true)
                $coordinates = Get-PomCoordinate -Archive $nestedArchive
                $noticeEntries = @($nestedArchive.Entries | Where-Object {
                        -not [string]::IsNullOrWhiteSpace($_.Name) -and
                        (Test-IsArchiveLegalEntry -EntryName $_.FullName)
                    } | Sort-Object FullName)
                $dependencyName = Get-SafeFileName -Name $nestedEntry.Name
                $dependencyKey = '{0:d3}-{1}-{2}' -f
                    $index,
                    $dependencyName,
                    $sha256.Substring(0, 12)
                foreach ($noticeEntry in $noticeEntries) {
                    Export-ArchiveLegalEntry -Entry $noticeEntry `
                        -DestinationPrefix (
                            Join-Path 'backend\dependencies' $dependencyKey)
                }
                if ($noticeEntries.Count -eq 0) {
                    $missingNotice.Add($nestedEntry.Name) | Out-Null
                }
                $records.Add([pscustomobject][ordered]@{
                        index               = $index
                        entry               = $safeEntryPath.Replace('\', '/')
                        fileName            = $nestedEntry.Name
                        size                = [int64]$bytes.Length
                        sha256              = $sha256
                        mavenCoordinates    = $coordinates
                        embeddedNoticeCount = $noticeEntries.Count
                    }) | Out-Null
            }
            catch {
                throw "Cannot inspect nested dependency '$($nestedEntry.FullName)': $($_.Exception.Message)"
            }
            finally {
                if ($null -ne $nestedArchive) {
                    $nestedArchive.Dispose()
                }
                $memory.Dispose()
            }
        }
    }
    finally {
        if ($null -ne $archive) {
            $archive.Dispose()
        }
        $jarStream.Dispose()
    }

    $tsv = New-Object 'System.Collections.Generic.List[string]'
    $tsv.Add(
        "index`tentry`tfileName`tsize`tsha256`tmavenCoordinates`tembeddedNoticeCount") |
        Out-Null
    foreach ($record in $records) {
        $fields = @(
            $record.index,
            $record.entry,
            $record.fileName,
            $record.size,
            $record.sha256,
            $record.mavenCoordinates,
            $record.embeddedNoticeCount
        ) | ForEach-Object {
            ([string]$_).Replace("`t", ' ').Replace("`r", ' ').Replace("`n", ' ')
        }
        $tsv.Add(($fields -join "`t")) | Out-Null
    }
    Write-Utf8File -RelativePath 'backend\backend-nested-jars.tsv' `
        -Content (($tsv.ToArray() -join "`n") + "`n")
    Write-Utf8File -RelativePath 'backend\backend-nested-jars.json' `
        -Content (($records.ToArray() | ConvertTo-Json -Depth 5) + "`n")

    return [pscustomobject][ordered]@{
        DependencyCount    = $records.Count
        MissingNoticeCount = $missingNotice.Count
        MissingNoticeNames = $missingNotice.ToArray()
    }
}

function Export-PythonRuntimeLicenses {
    param(
        [Parameter(Mandatory = $true)][string]$PythonRoot,
        [Parameter(Mandatory = $true)][string]$RuntimeName
    )

    $root = Resolve-RequiredDirectory -Path $PythonRoot `
        -Description "$RuntimeName Python runtime"
    Resolve-RequiredFile -Path (Join-Path $root 'python.exe') `
        -Description "$RuntimeName Python interpreter" | Out-Null
    $count = Copy-RecognizedLegalFiles -SourceRoot $root `
        -DestinationPrefix (Join-Path 'python' $RuntimeName) `
        -Description "$RuntimeName Python legal material"

    $records = New-Object 'System.Collections.Generic.List[object]'
    $sitePackages = Join-Path $root 'Lib\site-packages'
    if (Test-Path -LiteralPath $sitePackages -PathType Container) {
        foreach ($metadataDirectory in Get-ChildItem `
                -LiteralPath $sitePackages -Directory -Force | Where-Object {
                    $_.Name -match '(?i)\.(dist-info|egg-info)$'
                } | Sort-Object Name) {
            $metadataFile = Join-Path $metadataDirectory.FullName 'METADATA'
            $name = ''
            $version = ''
            $license = ''
            if (Test-Path -LiteralPath $metadataFile -PathType Leaf) {
                foreach ($line in Get-Content -LiteralPath $metadataFile) {
                    if ([string]::IsNullOrWhiteSpace($name) -and
                        $line -match '^Name:\s*(.+?)\s*$') {
                        $name = $matches[1]
                    }
                    elseif ([string]::IsNullOrWhiteSpace($version) -and
                        $line -match '^Version:\s*(.+?)\s*$') {
                        $version = $matches[1]
                    }
                    elseif ([string]::IsNullOrWhiteSpace($license) -and
                        $line -match '^License:\s*(.+?)\s*$') {
                        $license = $matches[1]
                    }
                    if (-not [string]::IsNullOrWhiteSpace($name) -and
                        -not [string]::IsNullOrWhiteSpace($version) -and
                        -not [string]::IsNullOrWhiteSpace($license)) {
                        break
                    }
                }
            }
            $records.Add([pscustomobject][ordered]@{
                    metadataDirectory = $metadataDirectory.Name
                    name              = $name
                    version           = $version
                    declaredLicense   = $license
                }) | Out-Null
        }
    }

    $lines = New-Object 'System.Collections.Generic.List[string]'
    $lines.Add("metadataDirectory`tname`tversion`tdeclaredLicense") |
        Out-Null
    foreach ($record in $records) {
        $fields = @(
            $record.metadataDirectory,
            $record.name,
            $record.version,
            $record.declaredLicense
        ) | ForEach-Object {
            ([string]$_).Replace("`t", ' ').Replace("`r", ' ').Replace("`n", ' ')
        }
        $lines.Add(($fields -join "`t")) | Out-Null
    }
    Write-Utf8File -RelativePath (
        Join-Path (Join-Path 'python' $RuntimeName) 'python-packages.tsv') `
        -Content (($lines.ToArray() -join "`n") + "`n")

    return [pscustomobject][ordered]@{
        LicenseFileCount = $count
        PackageCount     = $records.Count
    }
}

function Get-TrustedCondaPackageDirectory {
    param(
        [Parameter(Mandatory = $true)]$Record,
        [Parameter(Mandatory = $true)][string[]]$CacheRoots
    )

    $candidates = New-Object 'System.Collections.Generic.List[string]'
    $extractedPackageDirectory = Get-ObjectPropertyValue `
        -Object $Record -Name 'extracted_package_dir'
    if ($null -ne $extractedPackageDirectory -and
        -not [string]::IsNullOrWhiteSpace(
            [string]$extractedPackageDirectory)) {
        $candidates.Add([string]$extractedPackageDirectory) | Out-Null
    }
    $link = Get-ObjectPropertyValue -Object $Record -Name 'link'
    $linkSource = $null
    if ($null -ne $link) {
        $linkSource = Get-ObjectPropertyValue -Object $link -Name 'source'
    }
    if ($null -ne $linkSource -and
        -not [string]::IsNullOrWhiteSpace([string]$linkSource)) {
        $candidates.Add([string]$linkSource) | Out-Null
    }

    foreach ($candidate in $candidates) {
        if (-not (Test-Path -LiteralPath $candidate -PathType Container)) {
            continue
        }
        $candidateItem = Get-Item -LiteralPath $candidate -Force
        if (($candidateItem.Attributes -band
                [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Conda package source is a reparse point: $candidate"
        }
        $resolvedCandidate = [System.IO.Path]::GetFullPath(
            $candidateItem.FullName).TrimEnd('\', '/')
        foreach ($cacheRoot in $CacheRoots) {
            if (Test-PathWithinRoot -Root $cacheRoot `
                    -Candidate $resolvedCandidate) {
                return $resolvedCandidate
            }
        }
    }
    return $null
}

function Assert-CondaPackageCacheComplete {
    param(
        [Parameter(Mandatory = $true)][string]$EnvironmentRoot,
        [Parameter(Mandatory = $true)][string[]]$CacheRoots
    )

    $environment = Resolve-RequiredDirectory -Path $EnvironmentRoot `
        -Description 'Conda environment'
    $metadataRoot = Resolve-RequiredDirectory `
        -Path (Join-Path $environment 'conda-meta') `
        -Description 'Conda metadata'
    $resolvedCaches = New-Object 'System.Collections.Generic.List[string]'
    foreach ($cache in $CacheRoots) {
        $resolvedCaches.Add(
            (Resolve-RequiredDirectory -Path $cache `
                -Description 'Conda package cache')) | Out-Null
    }
    $metadataFiles = @(Get-ChildItem -LiteralPath $metadataRoot `
        -Filter '*.json' -File -Force | Sort-Object Name)
    if ($metadataFiles.Count -eq 0) {
        throw "Mandatory Conda metadata directory has no package records: $metadataRoot"
    }

    $missingPackages = New-Object 'System.Collections.Generic.List[string]'
    foreach ($metadataFile in $metadataFiles) {
        try {
            $record = Get-Content -LiteralPath $metadataFile.FullName -Raw |
                ConvertFrom-Json
        }
        catch {
            throw "Invalid Conda metadata '$($metadataFile.Name)': $($_.Exception.Message)"
        }
        $identity = New-Object 'System.Collections.Generic.List[string]'
        foreach ($requiredProperty in @('name', 'version', 'build')) {
            $value = Get-ObjectPropertyValue -Object $record `
                -Name $requiredProperty
            if ($null -eq $value -or
                [string]::IsNullOrWhiteSpace([string]$value)) {
                throw "Conda metadata '$($metadataFile.Name)' lacks '$requiredProperty'."
            }
            $identity.Add([string]$value) | Out-Null
        }
        if ($null -eq (Get-TrustedCondaPackageDirectory -Record $record `
                    -CacheRoots $resolvedCaches.ToArray())) {
            $missingPackages.Add(($identity.ToArray() -join ' ')) | Out-Null
        }
    }
    if ($missingPackages.Count -gt 0) {
        throw (
            "Conda package cache is incomplete. Missing package source(s):`n" +
            (($missingPackages.ToArray() | Sort-Object) -join "`n"))
    }
    return $metadataFiles.Count
}

function Export-CondaLicenses {
    param(
        [Parameter(Mandatory = $true)][string]$EnvironmentRoot,
        [Parameter(Mandatory = $true)][string[]]$CacheRoots
    )

    $environment = Resolve-RequiredDirectory -Path $EnvironmentRoot `
        -Description 'Conda environment'
    $metadataRoot = Resolve-RequiredDirectory `
        -Path (Join-Path $environment 'conda-meta') `
        -Description 'Conda metadata'
    $resolvedCaches = New-Object 'System.Collections.Generic.List[string]'
    foreach ($cache in $CacheRoots) {
        $resolvedCaches.Add(
            (Resolve-RequiredDirectory -Path $cache `
                -Description 'Conda package cache')) | Out-Null
    }

    $metadataFiles = @(Get-ChildItem -LiteralPath $metadataRoot `
        -Filter '*.json' -File -Force | Sort-Object Name)
    if ($metadataFiles.Count -eq 0) {
        throw "Mandatory Conda metadata directory has no package records: $metadataRoot"
    }

    $records = New-Object 'System.Collections.Generic.List[object]'
    $missingPackages = New-Object 'System.Collections.Generic.List[string]'
    $withoutLicenseText = New-Object 'System.Collections.Generic.List[string]'
    foreach ($metadataFile in $metadataFiles) {
        try {
            $record = Get-Content -LiteralPath $metadataFile.FullName -Raw |
                ConvertFrom-Json
        }
        catch {
            throw "Invalid Conda metadata '$($metadataFile.Name)': $($_.Exception.Message)"
        }
        foreach ($requiredProperty in @('name', 'version', 'build')) {
            $requiredValue = Get-ObjectPropertyValue -Object $record `
                -Name $requiredProperty
            if ($null -eq $requiredValue -or
                [string]::IsNullOrWhiteSpace(
                    [string]$requiredValue)) {
                throw "Conda metadata '$($metadataFile.Name)' lacks '$requiredProperty'."
            }
        }

        $recordName = [string](Get-ObjectPropertyValue `
            -Object $record -Name 'name')
        $recordVersion = [string](Get-ObjectPropertyValue `
            -Object $record -Name 'version')
        $recordBuild = [string](Get-ObjectPropertyValue `
            -Object $record -Name 'build')
        $packageDirectory = Get-TrustedCondaPackageDirectory -Record $record `
            -CacheRoots $resolvedCaches.ToArray()
        $noticeCount = 0
        if ($null -eq $packageDirectory) {
            $missingPackages.Add(
                "$recordName $recordVersion $recordBuild") |
                Out-Null
        }
        else {
            $packageKey = Get-SafeFileName -Name (
                "$recordName-$recordVersion-$recordBuild")
            $infoRoot = Join-Path $packageDirectory 'info'
            if (Test-Path -LiteralPath $infoRoot -PathType Container) {
                Assert-NoReparsePointInTree -Root $infoRoot `
                    -Description "Conda package '$recordName' legal material"
                foreach ($file in Get-ChildItem -LiteralPath $infoRoot `
                        -File -Force -Recurse) {
                    $relative = Get-RelativePath -BasePath $infoRoot `
                        -ChildPath $file.FullName
                    $isLegalDirectory = $false
                    foreach ($segment in $relative.Split('\')) {
                        if ($segment -match
                            '^(?i)(licenses?|licences?|notices?)$') {
                            $isLegalDirectory = $true
                            break
                        }
                    }
                    if (-not $isLegalDirectory -and
                        -not (Test-IsLegalFileName -Name $file.Name)) {
                        continue
                    }
                    Copy-TrustedFile -SourcePath $file.FullName `
                        -RelativeDestination (
                            Join-Path (
                                Join-Path 'conda\packages' $packageKey) $relative) `
                        -SourceRoot $infoRoot
                    $noticeCount++
                }
            }
            if ($noticeCount -eq 0) {
                $withoutLicenseText.Add(
                    "$recordName $recordVersion $recordBuild") |
                    Out-Null
            }
        }

        $declaredLicense = [string](Get-ObjectPropertyValue `
            -Object $record -Name 'license')
        $subdir = [string](Get-ObjectPropertyValue `
            -Object $record -Name 'subdir')
        $sha256 = [string](Get-ObjectPropertyValue `
            -Object $record -Name 'sha256')
        $records.Add([pscustomobject][ordered]@{
                name              = $recordName
                version           = $recordVersion
                build             = $recordBuild
                subdir            = $subdir
                declaredLicense   = $declaredLicense
                packageSha256     = $sha256
                copiedNoticeCount = $noticeCount
                packageCacheFound = ($null -ne $packageDirectory)
            }) | Out-Null
    }

    $lines = New-Object 'System.Collections.Generic.List[string]'
    $lines.Add(
        "name`tversion`tbuild`tsubdir`tdeclaredLicense`tpackageSha256`tcopiedNoticeCount`tpackageCacheFound") |
        Out-Null
    foreach ($record in $records) {
        $fields = @(
            $record.name,
            $record.version,
            $record.build,
            $record.subdir,
            $record.declaredLicense,
            $record.packageSha256,
            $record.copiedNoticeCount,
            $record.packageCacheFound
        ) | ForEach-Object {
            ([string]$_).Replace("`t", ' ').Replace("`r", ' ').Replace("`n", ' ')
        }
        $lines.Add(($fields -join "`t")) | Out-Null
    }
    Write-Utf8File -RelativePath 'conda\conda-packages.tsv' `
        -Content (($lines.ToArray() -join "`n") + "`n")

    if ($missingPackages.Count -gt 0) {
        $message =
            "Conda package cache is incomplete. Missing package source(s):`n" +
            (($missingPackages.ToArray() | Sort-Object) -join "`n")
        throw $message
    }

    return [pscustomobject][ordered]@{
        PackageCount            = $records.Count
        WithoutLicenseTextCount = $withoutLicenseText.Count
        WithoutLicenseTextNames = $withoutLicenseText.ToArray()
    }
}

$backend = Resolve-RequiredFile -Path $BackendJar `
    -Description 'Spring Boot backend'
$qtLicenses = Resolve-RequiredDirectory -Path $QtLicenseRoot `
    -Description 'Qt license source'
$qtSbom = Resolve-RequiredDirectory -Path $QtSbomRoot `
    -Description 'Qt SBOM source'
$vtkLicenses = Resolve-RequiredDirectory -Path $VtkLicenseRoot `
    -Description 'VTK license source'
$jre = Resolve-RequiredDirectory -Path $JreRoot `
    -Description 'private JRE'
Resolve-RequiredFile -Path (Join-Path $jre 'bin\java.exe') `
    -Description 'private JRE interpreter' | Out-Null
$jreLegal = Resolve-RequiredDirectory -Path (Join-Path $jre 'legal') `
    -Description 'private JRE legal material'
$cacPython = Resolve-RequiredDirectory -Path $CacPythonRoot `
    -Description 'CAC Python runtime'
$vmtkPython = Resolve-RequiredDirectory -Path $VmtkPythonRoot `
    -Description 'VMTK Python runtime'
$condaEnvironment = Resolve-RequiredDirectory -Path $CondaEnvironmentRoot `
    -Description 'Conda environment'
$segmentCacs = Resolve-RequiredDirectory -Path $SegmentCacsRoot `
    -Description 'SEGMENT-CACS source'
$h2License = Resolve-RequiredFile -Path $H2LicenseFile `
    -Description 'H2 license'
Assert-TextFileContains -Path $h2License `
    -Pattern '(?is)(?=.*\bH2\b)(?=.*(?:H2\s+(?:Database\s+)?License|Mozilla Public License|Eclipse Public License))' `
    -Description 'H2 license'
$vcLicense = Resolve-RequiredFile -Path $VcRedistLicenseFile `
    -Description 'Microsoft Visual C++ redistribution license'
$checkpoint = Resolve-RequiredFile -Path $CheckpointPath `
    -Description 'SEGMENT-CACS checkpoint'
$checkpointHash = (Get-FileHash -LiteralPath $checkpoint `
    -Algorithm SHA256).Hash.ToLowerInvariant()
if (-not [string]::Equals(
        $checkpointHash,
        $ExpectedCheckpointSha256,
        [System.StringComparison]::OrdinalIgnoreCase)) {
    throw (
        "SEGMENT-CACS checkpoint SHA-256 mismatch. Expected " +
        "$ExpectedCheckpointSha256 but found $checkpointHash.")
}
$condaPreflightCount = Assert-CondaPackageCacheComplete `
    -EnvironmentRoot $condaEnvironment `
    -CacheRoots $CondaPackageCacheRoot

$outputFull = [System.IO.Path]::GetFullPath($OutputRoot).TrimEnd('\', '/')
$volumeRoot = [System.IO.Path]::GetPathRoot($outputFull).TrimEnd('\', '/')
if ([string]::Equals(
        $outputFull,
        $volumeRoot,
        [System.StringComparison]::OrdinalIgnoreCase)) {
    throw 'Refusing to use an entire filesystem volume as OutputRoot.'
}
if (Test-Path -LiteralPath $outputFull) {
    if (-not (Test-Path -LiteralPath $outputFull -PathType Container)) {
        throw "OutputRoot exists and is not a directory: $outputFull"
    }
    if (@(Get-ChildItem -LiteralPath $outputFull -Force).Count -ne 0) {
        throw "OutputRoot must be new or empty; existing content will not be overwritten: $outputFull"
    }
}
else {
    New-Item -ItemType Directory -Path $outputFull | Out-Null
}
$script:ResolvedOutput = [System.IO.Path]::GetFullPath(
    (Get-Item -LiteralPath $outputFull -Force).FullName).TrimEnd('\', '/')

$qtCount = Copy-CompleteLegalTree -SourceRoot $qtLicenses `
    -DestinationPrefix 'qt' -Description 'Qt license source'
$qtSbomCount = Copy-QtSpdxSbomTree -SourceRoot $qtSbom `
    -DestinationPrefix 'qt\sbom'
$vtkCount = Copy-CompleteLegalTree -SourceRoot $vtkLicenses `
    -DestinationPrefix 'vtk' -Description 'VTK license source'
$jreCount = Copy-CompleteLegalTree -SourceRoot $jreLegal `
    -DestinationPrefix 'jre\legal' `
    -Description 'private JRE legal material'
$cacPythonResult = Export-PythonRuntimeLicenses -PythonRoot $cacPython `
    -RuntimeName 'cac'
$vmtkPythonResult = Export-PythonRuntimeLicenses -PythonRoot $vmtkPython `
    -RuntimeName 'vmtk'
$segmentCount = Copy-RecognizedLegalFiles -SourceRoot $segmentCacs `
    -DestinationPrefix 'segment-cacs' `
    -Description 'SEGMENT-CACS legal material'

Copy-TrustedFile -SourcePath $h2License `
    -RelativeDestination (
        Join-Path 'backend\required-external' (
            'H2-' + (Get-SafeFileName -Name (
                [System.IO.Path]::GetFileName($h2License))))) `
    -SourceRoot (Split-Path -Parent $h2License)
Copy-TrustedFile -SourcePath $vcLicense `
    -RelativeDestination (
        Join-Path 'microsoft-vc-redist' (
            Get-SafeFileName -Name (
                [System.IO.Path]::GetFileName($vcLicense)))) `
    -SourceRoot (Split-Path -Parent $vcLicense)

$additionalIndex = 0
foreach ($additionalFile in $AdditionalLicenseFile) {
    $additionalIndex++
    $resolvedAdditional = Resolve-RequiredFile -Path $additionalFile `
        -Description 'additional license'
    $additionalName = '{0:d3}-{1}' -f
        $additionalIndex,
        (Get-SafeFileName -Name (
            [System.IO.Path]::GetFileName($resolvedAdditional)))
    Copy-TrustedFile -SourcePath $resolvedAdditional `
        -RelativeDestination (Join-Path 'additional' $additionalName) `
        -SourceRoot (Split-Path -Parent $resolvedAdditional)
}

$scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
$warningSource = Resolve-RequiredFile `
    -Path (Join-Path $scriptDirectory `
        'CHECKPOINT-REDISTRIBUTION-WARNING.txt') `
    -Description 'checkpoint redistribution warning'
Copy-TrustedFile -SourcePath $warningSource `
    -RelativeDestination 'CHECKPOINT-REDISTRIBUTION-WARNING.txt' `
    -SourceRoot $scriptDirectory

$backendResult = Export-BackendLicenses -JarPath $backend
$backendInfo = Get-Item -LiteralPath $backend
$backendHash = (Get-FileHash -LiteralPath $backend `
    -Algorithm SHA256).Hash.ToLowerInvariant()
$backendRecord = @(
    'Spring Boot backend artifact evidence',
    '=====================================',
    '',
    "File name: $($backendInfo.Name)",
    "Size: $($backendInfo.Length) bytes",
    "SHA-256: $backendHash",
    "Nested dependencies: $($backendResult.DependencyCount)"
) -join "`n"
Write-Utf8File -RelativePath 'backend\BACKEND-ARTIFACT.txt' `
    -Content ($backendRecord + "`n")
$condaResult = Export-CondaLicenses -EnvironmentRoot $condaEnvironment `
    -CacheRoots $CondaPackageCacheRoot

$checkpointInfo = Get-Item -LiteralPath $checkpoint
$checkpointRecord = @(
    'SEGMENT-CACS checkpoint evidence',
    '================================',
    '',
    "File name: $($checkpointInfo.Name)",
    "Size: $($checkpointInfo.Length) bytes",
    "SHA-256: $checkpointHash",
    '',
    'Redistribution rights: NOT ESTABLISHED BY THIS TOOL.',
    'See CHECKPOINT-REDISTRIBUTION-WARNING.txt.'
) -join "`n"
Write-Utf8File -RelativePath 'segment-cacs\CHECKPOINT-EVIDENCE.txt' `
    -Content ($checkpointRecord + "`n")

$summaryLines = New-Object 'System.Collections.Generic.List[string]'
$summaryLines.Add('CAC portable Windows license collection summary') |
    Out-Null
$summaryLines.Add('==============================================') |
    Out-Null
$summaryLines.Add('') | Out-Null
$summaryLines.Add(
    "Backend nested dependencies: $($backendResult.DependencyCount)") |
    Out-Null
$summaryLines.Add(
    "Backend dependencies without embedded notice text: $($backendResult.MissingNoticeCount)") |
    Out-Null
$summaryLines.Add("Qt legal files copied: $qtCount") | Out-Null
$summaryLines.Add("Qt SBOM files copied: $qtSbomCount") | Out-Null
$summaryLines.Add("VTK legal files copied: $vtkCount") | Out-Null
$summaryLines.Add("JRE legal files copied: $jreCount") | Out-Null
$summaryLines.Add(
    "CAC Python legal files copied: $($cacPythonResult.LicenseFileCount)") |
    Out-Null
$summaryLines.Add(
    "CAC Python packages inventoried: $($cacPythonResult.PackageCount)") |
    Out-Null
$summaryLines.Add(
    "VMTK Python legal files copied: $($vmtkPythonResult.LicenseFileCount)") |
    Out-Null
$summaryLines.Add(
    "VMTK Python packages inventoried: $($vmtkPythonResult.PackageCount)") |
    Out-Null
$summaryLines.Add(
    "Conda packages inventoried: $($condaResult.PackageCount)") |
    Out-Null
$summaryLines.Add(
    "Conda package sources validated before collection: $condaPreflightCount") |
    Out-Null
$summaryLines.Add(
    "Conda packages without cached license text: $($condaResult.WithoutLicenseTextCount)") |
    Out-Null
$summaryLines.Add(
    "SEGMENT-CACS legal files copied: $segmentCount") |
    Out-Null
$summaryLines.Add("Additional license files copied: $additionalIndex") |
    Out-Null
$summaryLines.Add('') | Out-Null
$summaryLines.Add(
    'Checkpoint redistribution rights remain unconfirmed; public distribution is blocked pending written authorization.') |
    Out-Null

if ($backendResult.MissingNoticeCount -gt 0) {
    $summaryLines.Add('') | Out-Null
    $summaryLines.Add('Backend dependencies without embedded notice text:') |
        Out-Null
    foreach ($name in $backendResult.MissingNoticeNames | Sort-Object) {
        $summaryLines.Add("  - $name") | Out-Null
    }
}
if ($condaResult.WithoutLicenseTextCount -gt 0) {
    $summaryLines.Add('') | Out-Null
    $summaryLines.Add('Conda packages without cached license text:') |
        Out-Null
    foreach ($name in $condaResult.WithoutLicenseTextNames | Sort-Object) {
        $summaryLines.Add("  - $name") | Out-Null
    }
}
Write-Utf8File -RelativePath 'COLLECTION-SUMMARY.txt' `
    -Content (($summaryLines.ToArray() -join "`n") + "`n")

Write-Host 'LICENSE COLLECTION COMPLETED' -ForegroundColor Green
Write-Host "Output: $script:ResolvedOutput"
Write-Host "Files written: $script:WrittenFileCount"
Write-Host (
    "Backend dependencies inventoried: $($backendResult.DependencyCount)")
Write-Host "Conda packages inventoried: $($condaResult.PackageCount)"
Write-Warning (
    'SEGMENT-CACS checkpoint redistribution rights remain unconfirmed.')

[pscustomobject][ordered]@{
    outputRoot                     = $script:ResolvedOutput
    filesWritten                   = $script:WrittenFileCount
    backendDependencyCount         = $backendResult.DependencyCount
    backendMissingNoticeCount      = $backendResult.MissingNoticeCount
    qtLicenseFileCount             = $qtCount
    qtSbomFileCount                = $qtSbomCount
    condaPackageCount              = $condaResult.PackageCount
    condaWithoutLicenseTextCount   =
        $condaResult.WithoutLicenseTextCount
    checkpointSha256               = $checkpointHash
    checkpointRedistributionRights = 'UNCONFIRMED'
}
