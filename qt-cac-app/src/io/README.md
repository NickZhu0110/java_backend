# Backend Artifact I/O

## Purpose
This directory owns asynchronous HTTP transfer of job artifacts between the Qt client and the local backend.
It does not create jobs or render downloaded medical images.

## Key files and entry points
- `BackendFileClient.h`: declares download, corrected-mask upload, and recalculation signals and methods.
- `BackendFileClient.cpp`: uses `QNetworkAccessManager` for streamed downloads and multipart uploads.
- `downloadAiMask` and `downloadInputVolume`: retrieve viewer inputs for a completed job.
- `uploadCorrectedMask` and `requestRecalculation`: implement the Save Mask and recalculation boundary.

## Related directory
- [`../cache`](../cache) supplies local destinations; [`../viewer`](../viewer) consumes completed downloads.
