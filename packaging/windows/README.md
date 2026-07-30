# Windows portable release tooling

This directory contains portability-only sources for CAC Platform v0.6.10.
Medical algorithms, viewer behavior, Curved CPR, centerline extraction, vessel
selection, and VMTK processing logic remain frozen.

## Directory roles

- `launcher/`: native Win32 launcher source.
- `fallback/`: package-relative emergency launcher.
- `pipeline/`: privacy gate, manifest generation, release README template, and
  reproducible-build record.
- `build/`: local generated output; never copy it recursively into a release.

The final release stage must be outside Git and must be assembled item by item
from the whitelist in [pipeline/STAGE_CONTRACT.md](pipeline/STAGE_CONTRACT.md).

## Privacy gate

Run the privacy gate as soon as staging begins and again immediately before ZIP
creation:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\packaging\windows\pipeline\Test-ReleasePrivacy.ps1 `
  -StageRoot $StageRoot
```

For the final scan, require the complete directory contract and pin the approved
checkpoint hash:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\packaging\windows\pipeline\Test-ReleasePrivacy.ps1 `
  -StageRoot $StageRoot `
  -RequireCompleteLayout `
  -ExpectedCheckpointSha256 $ModelSha256 `
  -ApprovedFindingManifestPath `
    .\packaging\windows\pipeline\APPROVED_UPSTREAM_FINDINGS.json
```

The script performs no deletion. A violation fails the gate and must be fixed
in the whitelist staging process. The optional approval manifest cannot waive
medical/privacy rules; it classifies only reviewed upstream portability text
and runtime XML, and every classification is pinned to the exact file SHA-256.

## Release metadata

After the payload is final and the privacy gate passes:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\packaging\windows\pipeline\New-ReleaseManifest.ps1 `
  -StageRoot $StageRoot
```

This creates `manifest.json` and `SHA256SUMS.txt`. The two metadata files are
explicitly excluded from the payload hash scope to avoid self-reference.

Copy `pipeline/RELEASE_README.txt` into the stage as `README.txt`. Complete
`pipeline/BUILD_RECORD.md` with the observed P1-P8, privacy, archive, and fresh
extraction results; do not place machine-specific absolute paths in committed
records.

Do not create a ZIP until all packaging gates and the final privacy scan pass.
