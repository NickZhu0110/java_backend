# Qt Job Cache

## Purpose
This directory defines deterministic local paths for downloaded job artifacts, preprocessed volumes, corrected masks, edit records, and metadata.
The cache contains patient-derived runtime data and must not be committed or included in a populated release.

## Key files and entry points
- `JobFileCacheManager.h`: declares cache-root, case-key, job-directory, and artifact-path helpers.
- `JobFileCacheManager.cpp`: resolves `CAC_DATA_ROOT`, creates directories, sanitizes case keys, and writes JSON metadata.
- `JobFileCacheManager::baseCacheDir`: returns `<CAC_DATA_ROOT>/qt-cache` when configured.
- `JobFileCacheManager::ensureCaseCacheDir`: creates the case input, AI-mask, corrected-mask, and edit-operation directories.

## Related directory
- [`../viewer`](../viewer) loads cached volumes; [`../io`](../io) downloads and uploads artifacts.
