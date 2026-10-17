# Baseline

Recorded on 17 October 2026 while implementing `VERBESSERUNGSPLAN_GESAMT.md`.

## Machine used for the checks in this workspace

| Item | Value |
| --- | --- |
| OS | Windows 10.0.26200 |
| Compiler | MSVC 19.44.35207 (Visual Studio 17 2022) |
| CMake | 4.4.3 |
| Qt used for compile checks | 5.15.2 at `C:\tmp\lmms-qt5\5.15.2\msvc2019_64` |
| Qt ABI note | That Qt build is the MSVC 2019 package. The local compiler is newer. The isolated DPI build in `build/dpi` already succeeded with Visual Studio 17 2022. |
| Reference preset | `windows-qt5-msvc-v142` in `CMakePresets.json` names the CI toolset `v142` from `.github/workflows/high-dpi.yml`. It was not the generator that configured on this machine. |
| sndfile | Not installed as a complete vcpkg tree. `C:\tmp\lmms-mcp-syntax\sndfile.h` is only a stub for translation-unit checks. |
| Full application link | Not produced. Configure of a full tree still needs libsndfile, the other `vcpkg.json` libraries, and a chosen generator. |

## What was compiled here

Object compilation with Qt 5.15.2, the repository headers, and the stub directory above succeeded for:

- `ThreadPool.cpp`
- `AudioEngineProfiler.cpp`
- `DataFile.cpp`
- `ConfigManager.cpp`
- `DiagnosticReport.cpp`
- `SampleThumbnail.cpp`
- `Song.cpp`
- `AudioEngine.cpp`
- `AudioEngineWorkerThread.cpp`

An earlier harness named CoreContractTest linked some of those pieces and reported 5 passed. That run is not the current `StabilizationTest`. On this follow-up, `StabilizationTest.cpp` and the changed translation units compiled with MSVC, but the test executable was not linked because the full tree still has no libsndfile. Do not treat the older 5/5 as a run of the current tests.

## Phase 2 minimal tests, same day

`tests/minimal` was configured with Visual Studio 17 2022 and the Qt prefix above. zlib was not a separate library; project decoding used Qt's exported `z_inflate` via `QtZlib/zlib.h`. `ctest -C Release` then passed:

- SaveTransactionTest
- ProjectCodecTest, including a real `qCompress` round trip
- BundleTransactionTest
- RecoveryIdentityTest
- ReleaseQueueTest
- WindowsLoaderTest

`StabilizationTest` was not part of that build and was not linked.

## What was not measured

CPU model, RAM, audio interface, XRuns, paint times, and the F0–F7 listening fixtures were not measured on a running LMMS process. The numbers in section 5 of the plan stay targets.

## Follow-up baseline, 17 October 2026

The follow-up on `64bc33825` builds and runs `tests/minimal` (8 targets) and `tests/dpi` (25 cases) in Release and Debug with MSVC 19.44.35229.0 / Qt 5.15.2. Both full MCP ON/OFF configurations stop earlier than dependency discovery: `DetectMachine.cmake` requires MSVC >= 19.51. No supported compiler or dependency was substituted. The historical header-stub harness was used only for supplemental source compilation, never as an application or integration-test result. Detailed commands and boundaries are in `findings.md`.
