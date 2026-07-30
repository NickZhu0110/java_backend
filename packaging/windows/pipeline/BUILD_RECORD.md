# Portable CAC v0.6.10 build and acceptance record

This record contains only observed release results. Machine-specific absolute
paths and medical identifiers are deliberately omitted.

## Frozen source and release scope

| Field | Value |
|---|---|
| Application | `v0.6.10 win heart process` |
| Packaging branch | `release/v0.6.10-portable-windows` |
| Frozen application commit | `bab1caf4adeeb1481b2fab5125150f00ce502974` |
| Vessel/CPR status | Frozen; Experimental |
| Release target | Windows x64, CPU-only |
| External services required | None; no PostgreSQL, Kafka, Redis, or Docker |
| Push status | Not pushed |

No medical algorithm, scoring algorithm, viewer algorithm, centerline
extraction, vessel selection, or CPR behavior was changed. Source changes are
limited to packaging, package-relative configuration, the native launcher, and
an environment-gated CPU-only device label.

## Toolchain and runtime baseline

| Component | Observed value |
|---|---|
| Compiler | MSVC 19.44.35222.0; toolset 14.44.35207, x64 |
| Qt | 6.8.3, `msvc2022_64` |
| CMake | 3.30.5 |
| Ninja | 1.12.1 |
| C++ VTK | 9.6.2 |
| Build/private JDK | Microsoft OpenJDK 21.0.11 |
| CAC Python | CPython 3.10.11, Windows x64 |
| PyTorch | 2.1.2+cpu; CUDA unavailable |
| VMTK Python | CPython 3.10.16 |
| VMTK | 1.5.0 |
| Python VTK | 9.2.6 |

## Path-independent command record

Build variables resolve to approved local tools and are never embedded into the
package:

```powershell
$SourceRoot = (Resolve-Path '.').ProviderPath
$QtRoot = $env:CAC_BUILD_QT_ROOT
$VtkRoot = $env:CAC_BUILD_VTK_ROOT
$VmtkPython = $env:CAC_BUILD_VMTK_PYTHON
$CMake = Join-Path $env:CAC_BUILD_CMAKE_ROOT 'bin\cmake.exe'
$Ninja = Join-Path $env:CAC_BUILD_NINJA_ROOT 'ninja.exe'
```

Commands used, expressed with those symbolic variables:

```text
Qt configure: $CMake -S qt-cac-app -B qt-cac-app/build/portable-release-p1
              -G Ninja -DCMAKE_BUILD_TYPE=Release
              -DCMAKE_PREFIX_PATH=$QtRoot
              -DVTK_DIR=$VtkRoot/lib/cmake/vtk-9.6
Qt build:     $CMake --build qt-cac-app/build/portable-release-p1
Backend test: cac-backend/mvnw.cmd clean test
Backend JAR:  cac-backend/mvnw.cmd clean package
CAC tests:    real relocated patient-5 inference and official recalculation
VMTK tests:   $VmtkPython -m pytest
              python-worker/vessel_straightening/tests -q
Diff check:   git diff --cached --check
```

## Gate P1: clean build and source verification

| Check | Result | Evidence |
|---|---|---|
| Qt Release build | PASS | CMake/Ninja Release build |
| Backend tests | PASS | Maven wrapper: 4 tests, 0 failures |
| Backend JAR build | PASS | Maven wrapper `clean package` |
| SEGMENT-CACS integration | PASS | Real CPU inference and official recalculation |
| Frozen vessel tests | PASS | 9 tests and relocated VMTK pipeline |
| Final staged diff check | PASS | `git diff --cached --check` on the exact 20-file source set |
| Medical algorithm diff | PASS | No algorithm file was modified |

The Maven Windows wrapper required a temporary junction to the existing Maven
repository because normal wrapper path resolution returned a null target. The
wrapper still executed the clean test/package lifecycle; no source file was
changed for that workaround.

## Gates P2-P3: whitelist staging and private runtimes

| Component | Files | Size | Result |
|---|---:|---:|---|
| Private jlink JRE | 341 | 86,458,034 bytes | PASS |
| CAC Python runtime | 16,615 | 1,434,071,316 bytes | PASS |
| VMTK Python runtime | 27,512 | 1,803,875,620 bytes | PASS |
| Qt/VTK/VC application runtime | 100 | 135,241,258 bytes | PASS |

CAC Python passed `pip check`, imports PyTorch 2.1.2+cpu, reports
`torch.cuda.is_available() == False`, and loaded the approved checkpoint with
`map_location="cpu"`. Both Python environments were tested after relocation to
paths containing spaces. No global installation or PATH modification is
required.

## Gate P4: real local DICOM inference

The approved patient-5 DICOM case completed through the packaged local backend:

- CT and mask: `512 x 512 x 57`
- spacing: `0.3359375 x 0.3359375 x 3.0 mm`
- AI-mask foreground voxels: `4,767`
- Agatston score: `2045.48`
- risk grade: `moderate`
- official corrected-mask recalculation: `2045.4788208007812`, `moderate`
- recalculation confirmed `modelInferenceSkipped=true` and used official
  SEGMENT-CACS scoring.

## Gate P5: packaged VMTK and frozen vessel workflow

The private VMTK runtime passed imports, 9 automated tests, and real pipeline
execution on two drive letters and paths containing spaces. On the approved
NIfTI CT/mask pair, label 2000 component analysis produced 28 candidates
(15 primary and 13 minor). A selected path produced:

- centerline length: `160.452 mm`
- straightened volume: `65 x 65 x 321`
- output spacing: `0.5 mm`

This verifies packaged-runtime invocation only; CPR remains the frozen
Experimental implementation.

## Gates P6-P7: deployment, privacy, and licenses

- Qt deployment used official `windeployqt`.
- App-local Qt, VTK 9.6.2, OpenGL fallback, platform plugin, and required VC143
  runtime DLLs were included.
- Minimal package-only PATH launch passed.
- Final privacy scan: PASS, 45,422 files scanned, zero unapproved violations.
- Exactly 131 reviewed upstream portability findings were classified by
  path/rule/SHA-256; medical/privacy rules cannot be waived.
- Checkpoint SHA-256 was pinned and verified.
- Runtime `data` contained 0 files before manifest and archive creation.
- No reparse points or medical volumes were present.

The final `LICENSES` tree contains 826 files / 39,705,066 bytes:

- 4 Qt license files and 10 official Qt SPDX tag-value SBOMs;
- 50 VTK legal files;
- 197 private-JRE legal files;
- Python, PyTorch, VMTK, Python VTK, SimpleITK, NumPy, SciPy, scikit-image,
  Conda, backend, H2, SEGMENT-CACS, and Microsoft runtime evidence;
- Microsoft BuildTools redistribution list and official installed third-party
  notices.

Qt's redundant `.spdx.json` representations were not packaged because they
repeat the tag-value SBOM content and contain upstream test-fixture absolute
paths rejected by the privacy gate.

License limitations:

- checkpoint redistribution rights remain unconfirmed; public distribution is
  blocked pending written authorization;
- 22 backend JARs contain no embedded notice text, although H2 is separately
  covered by its official 2.3.232 license;
- 5 Conda split packages have no independent cached license text and remain
  explicitly inventoried.

These limitations do not block the prompt-authorized internal/local technical
ZIP and must not be interpreted as public redistribution permission.

## Gate P8: relocated acceptance

The complete package was copied to a writable path containing spaces and tested
without Qt, VTK, Java, Python, PostgreSQL, Kafka, Redis, or Docker from the host
PATH.

Automated results:

- launcher validation, loopback health, Qt launch, and CPU-only backend: PASS;
- DICOM inference, external output naming, Save/Corrected Mask upload, and
  Recalculate: PASS;
- close/reopen and H2 job/result persistence: PASS;
- normal Qt close produced exit code 0;
- private backend stopped gracefully without forced termination;
- launcher, Qt, and private Java left no orphan process;
- duplicate launch was refused without affecting the primary instance.

User-performed mouse/visual acceptance on 2026-07-30: **PASS**:

- CT MPR, AI mask, 3D CAC, and colored slice cards;
- Move Planes and direct plane dragging;
- Add/Erase brush, Undo/Redo, Save, and Recalculate;
- multi-structure NIfTI load;
- label visibility/opacity and vessel label/component selection;
- frozen Experimental vessel/CPR workflow invocation.

The network adapter was not programmatically disabled because that would
modify host-wide state. Runtime resolution was constrained to package-private
executables and loopback `127.0.0.1:6006`; no external service is required.

## Final manifest, archive, and fresh extraction

| Artifact/measurement | Observed value |
|---|---|
| Payload | 45,420 files / 3,571,501,754 bytes |
| Complete staged regular files | 45,422 files / 3,594,743,046 bytes |
| `manifest.json` | 16,865,075 bytes; SHA-256 `a55b0f6a75c3e39413be723874260caa5c82dd5090616bab092e18fd8320ad74` |
| `SHA256SUMS.txt` | 6,376,217 bytes; SHA-256 `729808081d50f3b83c75b76a12a4da0daf17622a276dd1ce953f4d4c65c325c1` |
| Archive | `Portable-CAC-v0.6.10-Windows-x64-CPU.zip` |
| Archive size | 1,058,669,276 bytes |
| Archive SHA-256 | `dbb8cad10f2871cbe84ad590c2ce26fd4ca2df07bd456e4bc96471be9917644d` |

Fresh extraction into a new writable path containing spaces passed:

- all 45,420 payload paths, sizes, and SHA-256 digests;
- exact manifest and sums metadata hashes;
- zero unexpected payload files;
- all required empty `data` directories and zero runtime data files;
- private backend health `UP`, Qt process launch, and package-relative launcher
  log creation.

## Frozen artifact hashes

| Artifact | SHA-256 | Size |
|---|---|---:|
| `qt-cac-app.exe` | `63c488c07b82c15865d85ef9d0a949b2fbb2a392d2dac9e14e14692d5793a5ee` | 729,600 bytes |
| `cac-backend.jar` | `0b2b0417c342049dd418fea36d00d0ed2ac7389c2e014ea1889cac04d85040ad` | 65,764,287 bytes |
| `SegmentCACS_0001619_unet.pt` | `da2901df83f33b1ba27335073d24781d0da4627455333f5f3c5000ee1ddbd4f3` | 5,918,747 bytes |
| `CACLauncher.exe` | `5aa90612bf699278d75cec5595e4e138abe883e157a5504423bf7120ac375ed6` | 305,152 bytes |

## Final decision

- Gates P1-P8: PASS.
- Privacy gate: PASS.
- Internal/local technical portable ZIP: PASS.
- Public redistribution: **NOT AUTHORIZED BY THIS RECORD**.
- Medical/viewer algorithm changes: none.
- Recorded UTC date: 2026-07-30.
