# Windows portable stage contract

The release stage is assembled from an explicit whitelist. It is never created
by recursively copying the Git worktree, a user profile runtime-data folder, or
a model repository.

## Allowed top-level entries

- `CACLauncher.exe`
- `Start-CAC.cmd`
- `app`
- `backend`
- `runtime`
- `inference`
- `config`
- `data`
- `LICENSES`
- `README.txt`
- `manifest.json`
- `SHA256SUMS.txt`

`Test-ReleasePrivacy.ps1` rejects every other top-level entry unless the caller
adds one explicitly with `-AdditionalAllowedTopLevel`.

The shipped `data` subtree may contain its required directories, but no files.
Runtime state is created only after the user starts the package.

## Approved model

The sole default model allowlist entry is:

```text
inference/model/SegmentCACS_0001619_unet.pt
```

Use `-ExpectedCheckpointSha256` in release automation so a same-named,
unapproved file cannot pass.

## Approved source identifiers

Medical API identifiers such as `PatientName` and `PatientID` are allowed only
as source-code identifiers below the explicitly approved inference/runtime
source prefixes. Literal non-placeholder values remain forbidden. Medical
volume formats and generated artifacts are forbidden everywhere, including
under approved source prefixes.

XML files are allowed under `LICENSES`. Any required configuration XML must be
approved by exact stage-relative path using `-ApprovedXmlRelativePath`.

Audited upstream portability examples and required runtime XML resources may be
classified only through `-ApprovedFindingManifestPath`. Every entry is bound to
an exact stage-relative path, finding rule, and SHA-256. The scanner rejects
changed files, stale entries, duplicate entries, and attempts to approve
medical files, patient identifiers, credentials, caches, repositories,
packaging-machine paths, or development-source paths.

## Integrity scope

`New-ReleaseManifest.ps1` sorts and hashes every regular payload file. Both
integrity metadata files are deliberately excluded from both integrity lists:

- `manifest.json`
- `SHA256SUMS.txt`

The exclusion is also recorded inside `manifest.json`, eliminating recursive
self-hashing ambiguity.
