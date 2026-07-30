CAC Platform v0.6.10 - Windows x64 CPU Portable Release
========================================================

START
-----

1. Extract the complete ZIP into an arbitrary writable folder.
2. Double-click CACLauncher.exe.
3. Wait while the private local backend starts. The application opens only
   after the backend health check succeeds.

Do not move individual files out of the extracted folder.

RUNTIME
-------

This release is CPU-only. CUDA/GPU inference is not available in this package.

Qt, VTK, Java, Python, PyTorch, SimpleITK, VMTK, and H2 are included as private
package runtimes. No system installation, administrator permission, PATH
change, or environment-variable configuration is required.

The backend listens only on 127.0.0.1:6006. PostgreSQL, Kafka, Redis, Docker,
an external server, and an Internet connection are not required.

DATA AND OUTPUTS
----------------

Private application state is stored below:

  <extracted package>\data

This includes the embedded H2 database, job state, logs, temporary files,
viewer cache, and vessel-processing runtime outputs. Keep this folder if you
want local job history to persist between launches.

Analysis output is written to the output folder and output name selected in
the application.

Do not distribute the data folder after using the application. It may contain
patient-derived information.

EXPERIMENTAL VESSEL FEATURE
---------------------------

Vessel selection, centerline extraction, Curved CPR, and straightened vessel
views are Experimental in v0.6.10. They are not represented as clinically
validated output.

SHUTDOWN
--------

Close the CAC Analysis Client normally. CACLauncher then stops only the private
backend process that it started. Do not start a second copy while one is
already running.

REDISTRIBUTION NOTICE
---------------------

Public redistribution rights for the bundled SEGMENT-CACS model checkpoint
have not yet been confirmed. This technical package must not be represented as
approved for public redistribution until the checkpoint's applicable license
or written permission has been verified.

See the LICENSES folder for bundled third-party notices.
