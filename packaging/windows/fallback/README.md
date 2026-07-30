# Emergency fallback

The native `CACLauncher.exe` is the supported entry point. These files provide
a small emergency fallback using only Windows PowerShell already present on
Windows 10/11.

During staging:

- copy `Start-CAC.cmd` to the package root;
- copy `Start-CAC.ps1` to `config/Start-CAC.ps1`.

The CMD file passes its own directory as `PackageRoot`; the PowerShell script
does not infer or depend on a development worktree path.

The fallback validates the same whitelist as the native launcher, refuses an
occupied port 6006, sets the same package-only environment and controlled
`PATH`, starts the exact private Java process, waits for health, and then starts
Qt. It requests actuator shutdown and calls `Kill()` only on the exact
`System.Diagnostics.Process` instance it created. It never enumerates or kills
Java or Python by process name.

Spring writes live application diagnostics to `data/logs/backend.log`.
PowerShell drains both redirected console streams asynchronously to avoid pipe
deadlock and appends their captured text to the same file after the backend
exits. This fallback deliberately keeps a console window visible so startup
errors remain readable.
