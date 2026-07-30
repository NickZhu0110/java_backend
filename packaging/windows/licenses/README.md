# Windows release license collection

`New-ReleaseLicenses.ps1` builds the `LICENSES` directory for the portable
Windows release from explicitly supplied, trusted local sources. It does not
download anything, modify a runtime, or infer legal rights that are not present
in the source material.

The collector:

- inventories every `BOOT-INF/lib/*.jar` entry in the Spring Boot fat JAR;
- records each nested JAR's size, SHA-256 digest, Maven coordinates when
  available, and embedded notice count;
- safely extracts embedded `LICENSE`, `LICENCE`, `NOTICE`, `COPYING`,
  `COPYRIGHT`, and third-party notice material without allowing an archive path
  to escape the destination;
- copies the configured Qt licenses and installed Qt SPDX tag-value SBOMs,
  plus VTK, private-JRE, Python, Conda, SEGMENT-CACS, H2, and Microsoft
  Visual C++ redistribution license material;
- records Python and Conda package inventories without copying source-machine
  absolute paths; and
- emits `COLLECTION-SUMMARY.txt` plus the checkpoint redistribution warning.

This is an evidence collector, not legal advice. A dependency with no embedded
notice is still included in the complete backend inventory and is called out in
the summary. Its licensing must be reviewed before public distribution.

## Required inputs

All source arguments are mandatory. This is deliberate: omitting a runtime
must stop the release instead of silently producing an incomplete `LICENSES`
tree.

- `BackendJar`: the exact Spring Boot fat JAR shipped in the release.
- `QtLicenseRoot`: the installed Qt `Licenses` directory.
- `QtSbomRoot`: the installed Qt kit's `sbom` directory. The collector copies
  every canonical `.spdx` tag-value file. It deliberately omits the redundant
  `.spdx.json` representations because those contain upstream test-fixture
  absolute paths forbidden by the release privacy policy.
- `VtkLicenseRoot`: the installed C++ VTK license directory.
- `JreRoot`: the exact private jlink JRE root; it must contain `bin\java.exe`
  and `legal`.
- `CacPythonRoot`: the exact isolated CAC Python root.
- `VmtkPythonRoot`: the source VMTK Python/Conda environment root.
- `CondaEnvironmentRoot`: the intact VMTK Conda environment containing
  `conda-meta`.
- `CondaPackageCacheRoot`: one or more trusted Conda package-cache roots. Every
  environment record must resolve to a package under one of these roots.
- `SegmentCacsRoot`: the SEGMENT-CACS source root containing its license.
- `H2LicenseFile`: an approved H2 license text matching the bundled H2 version.
- `VcRedistLicenseFile`: the installed Microsoft Visual C++ redistribution
  license/list file applicable to the bundled runtime DLLs.
- `CheckpointPath`: the exact checkpoint intended for the package. The
  collector records its filename, size, and SHA-256 only; it never copies it.
- `ExpectedCheckpointSha256`: the approved 64-character checkpoint digest.
  Collection stops before writing output if the selected checkpoint differs.

`OutputRoot` must not exist, or must be an empty directory. The collector never
deletes or overwrites an existing license tree.

## Example

Use variables resolved by the release pipeline rather than hard-coded
developer-machine paths:

```powershell
$licenseArgs = @{
    OutputRoot              = Join-Path $StageRoot 'LICENSES'
    BackendJar              = $BackendJar
    QtLicenseRoot           = $QtLicenseRoot
    QtSbomRoot              = $QtSbomRoot
    VtkLicenseRoot          = $VtkLicenseRoot
    JreRoot                 = Join-Path $StageRoot 'runtime\jre'
    CacPythonRoot           = $CacPythonRoot
    VmtkPythonRoot          = $VmtkSourceEnvironment
    CondaEnvironmentRoot    = $VmtkSourceEnvironment
    CondaPackageCacheRoot   = @($CondaPackageCache)
    SegmentCacsRoot         = $SegmentCacsRoot
    H2LicenseFile           = $H2LicenseFile
    VcRedistLicenseFile     = $VcRedistLicenseFile
    CheckpointPath          = $CheckpointPath
    ExpectedCheckpointSha256 = $ExpectedCheckpointSha256
}

& .\packaging\windows\licenses\New-ReleaseLicenses.ps1 @licenseArgs
```

The command fails if a mandatory path is absent, if a source tree contains a
reparse point used by the collector, if any nested JAR is unsafe or unreadable,
or if a Conda package record cannot be matched to its trusted package cache.

## Checkpoint hard stop

`CHECKPOINT-REDISTRIBUTION-WARNING.txt` must remain in every release. Successful
collection does **not** prove permission to redistribute model weights. Public
release remains blocked until written checkpoint redistribution rights are
confirmed.
