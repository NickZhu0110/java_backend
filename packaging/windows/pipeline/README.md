# Windows release pipeline

## Purpose

This directory defines the whitelist, privacy checks, integrity metadata, and
observed acceptance record for the Windows x64 CPU portable release.

## Key files

- `STAGE_CONTRACT.md` defines the permitted package layout.
- `Test-ReleasePrivacy.ps1` rejects forbidden data and unapproved content.
- `New-ReleaseManifest.ps1` writes `manifest.json` and `SHA256SUMS.txt`.
- `BUILD_RECORD.md` records the observed v0.6.10 release gates and hashes.
- `RELEASE_README.txt` is copied into the packaged application.

## Related directory

See [`../README.md`](../README.md); never stage patient or existing runtime data.
