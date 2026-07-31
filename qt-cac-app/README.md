# Qt CAC Desktop Client

## Purpose
This Qt 6 application submits CAC jobs to the local Spring Boot backend and displays the returned CT, mask, score, and job progress.
It also provides direct read-only NIfTI review and an experimental vessel/CPR workflow that does not run SEGMENT-CACS.

## Key files and entry points
- `main.cpp`: creates `QApplication`, applies the QVTK surface format, and opens `MainWindow`.
- `mainwindow.cpp` / `mainwindow.ui`: own analysis submission, job state, artifact downloads, and the top-level layout.
- `BackendClient.cpp`: implements health, job, and result REST requests.
- `JobWebSocketClient.cpp`: receives job-progress events while HTTP polling remains available.
- `ServerSettingsDialog.cpp`: stores and tests backend and WebSocket addresses.

## Related directories
- [`src/viewer`](src/viewer), [`src/vessel`](src/vessel), [`src/cache`](src/cache), [`src/io`](src/io), and [`tools`](tools).
