# Findings and fixes

## Follow-up to `64bc33825`, 17 October 2026

The following results apply to the uncommitted follow-up on base commit `64bc338250ae7732430fd183e7469fd5310c9ffe`. Older sections below describe earlier runs and do not certify the current application.

| Area | Implemented correction | Current evidence / remaining boundary |
| --- | --- | --- |
| AP-F08 / loading | Parse DTD tokens, permit the historical name-only LMMS doctypes, reject external DTDs/internal subsets. CDATA and comments are not searched as declarations. UTF-8 and UTF-16 BOMs classify correctly. Remove DataFile's premature binary-size diagnosis. Configure requires linkable zlib or verified Qt 5 prefixed zlib. Decode failures report the output peak actually reached. | `ProjectCodecTest` passes on all repository `.mmp`, `.mpt` and `.xpf` fixtures, BOMs, CDATA, compressed round trips, chunk boundaries and a 2 MiB XML body. Full Song/DataFile reload is registered in `EngineStabilizationTest`, not executed. AP-F08 remains open for application acceptance. |
| AP-F02 / bundles | Deep DOM clone without reload/upgrades. Strict version-1 manifest plus project/resource validation; no replacement based on filename alone. Resources use available destination names, including pre-suffixed collisions. Empty resource attributes remain empty. Canonical-parent target lock uses `QLockFile` with age-based stale detection disabled. External versioned JSON journal records Prepared, OldMoved, NewPublished and CleanupComplete. Recovery holds the same target lock and reconciles rename intent against actual trees. | `BundleTransactionTest` passes injected preparation/publish/rollback/cleanup faults, invalid XML/manifest/missing resource, two real concurrent publishers, independent targets, and process exit at Prepared, old-move intent, OldMoved, NewPublished and CleanupComplete. Unknown/legacy artifacts remain untouched. Full GUI export/open/normal-save still requires the integration build. |
| AP-F10 / recovery | A shared per-source `QLockFile` guard serializes claim replacement, release and deletion. Restore rechecks original session ownership. Candidates include claim liveness; live/unknown claims block foreign deletion. Deletion failures preserve ownership records and are reported. | `RecoveryIdentityTest` exercises the production deletion helper against a real claiming child process, reclaims after that child exits, and checks a held deletion guard, unreadable claims and live session locks. `EngineStabilizationTest` adds source retention after failed save and removal after successful save; this integration target has not run. |
| AP-F05 / lifetime | No fallback vector and no forced wrong-affinity deletion. The audio producer retains ownership when the fixed queue is full and pauses new period scheduling with failed/silent output until the owner drains it, bounding backlog growth. A 20 ms timer on the engine's owner thread drains after quiescence without model edits. Bound preview handles remain with their owner; borrowed SampleTrack handles have no owned bus to constrain destruction. Engine teardown stops the callback, joins workers, cancels unclaimed slots and drains before preview, song and mixer teardown. The audio device stop helper also aborts after its existing 30-second join limit instead of terminating a thread holding plugin state. | Queue primitive tests pass. Actual engine destructor-thread and periodic-drain and full-handoff tests are registered in `EngineStabilizationTest`, compiled in the supplemental syntax harness, not linked/run. Allocation/callback profiling and shutdown stress remain open. |
| AP-F06/F07/F09/F13/F15 | Capture exceptions at queue execution, finish reservations and job state, expose batch failure, fail late completed waits, stop starting additional inline jobs past the deadline, and cancel pending jobs when the caller is the only consumer. Final render checks precede successful output/snapshot publication. Diagnostic accounting is synchronized. | `WorkerDeadlineTest` links the actual worker implementation without engine/decoder stubs: inline, 1 and 3 workers, delayed/throwing jobs, overflow, expired batches, cancellation of unstarted shutdown slots and subsequent reset pass. A never-returning worker runs only in a child; its destructor must fail fast after the 3-second join limit under an outer watchdog. A marked previous buffer, repeated silent callbacks and snapshot checks are registered in the unexecuted engine integration target. A running synchronous plugin still cannot be preempted. |
| AP-F01 / Windows | Explicitly separated/sorted environment entries, Windows backslash/quote escaping, inherited standard handles, shorter internal watchdog, suspended child assigned to a kill-on-close Job Object before execution. | `WindowsLoaderTest` passes missing/present DLL, absent QT_PLUGIN_PATH, preserved sentinel, quoted/empty/trailing-backslash arguments and timeout with a real descendant process. DPI suite passes native Windows plus offscreen scales in Release and Debug. No installed-package deployment test. |
| AP-F04 / waveform | Separate immutable storage from decoder dependencies and use the same production thumbnail renderer through a buffer constructor. Existing Sample-based callers delegate to it. | `SampleThumbnailTest` renders a QWidget into an image: empty, 1/3/101/10000 frames, stereo tail impulses, reverse and trim. Revision test changes a previously unsampled middle frame. No sanitizer run. |
| AP-F14 / detune | Production negative-node load test plus span bounds added to the engine integration suite. | Source compiled only; piano-roll paint/undo and indexed-vs-linear GUI acceptance remain open. |

### Commands and results

Machine: Windows 10.0.26200, Visual Studio 17 2022, compiler MSVC 19.44.35229.0, CMake 4.4.3, Qt 5.15.2 (`C:/tmp/lmms-qt5/5.15.2/msvc2019_64`). Test runtime is supplied by the launcher; no manual Qt PATH was needed for CTest.

```powershell
cmake -S tests/minimal -B build/phase2-minimal -DLMMS_QT_MAJOR=5 "-DCMAKE_PREFIX_PATH=C:/tmp/lmms-qt5/5.15.2/msvc2019_64"
cmake --build build/phase2-minimal --config Release --parallel 2
ctest --test-dir build/phase2-minimal -C Release --output-on-failure
cmake --build build/phase2-minimal --config Debug --clean-first --parallel 2
ctest --test-dir build/phase2-minimal -C Debug --output-on-failure
cmake --build build/dpi --config Release --parallel 2
ctest --test-dir build/dpi -C Release --output-on-failure
cmake --build build/dpi --config Debug --parallel 2
ctest --test-dir build/dpi -C Debug --output-on-failure
```

Results: minimal **8/8 Release, 8/8 Debug**; DPI **25/25 Release, 25/25 Debug**. The minimal targets are SaveTransactionTest, ProjectCodecTest, BundleTransactionTest, RecoveryIdentityTest, WorkerDeadlineTest, ReleaseQueueTest, WindowsLoaderTest and SampleThumbnailTest. CTest counts executables, not individual Qt test rows.

Both full configurations were attempted with `cmake -S . -B build/stabilization-on` / `build/stabilization-off`, generator `Visual Studio 17 2022`, architecture `x64`, the Qt prefix above, `WANT_QT6=OFF`, `BUILD_TESTING=ON`, and respectively `WANT_MCP=ON` / `OFF`. Both stop at `cmake/modules/DetectMachine.cmake:67`: the repository requires MSVC >= 19.51; installed is 19.44. The requirement was not bypassed. Dependency discovery/linking was not reached.

A supplemental object-only compilation of modified engine, save, mixer, sample and integration-test sources succeeded through the pre-existing local `build/merge-check` harness. That harness contains decoder header stubs. It is only a syntax check, not a supported application build, linked integration test, or acceptance result.

### Open acceptance and compatibility

- `EngineStabilizationTest` and the existing `StabilizationTest` need a supported full MCP ON/OFF build with real dependencies. The new target checks actual DataFile saves/reloads/bundle cloning, loaded negative automation nodes, periodic handle release/affinity, stalled output/snapshots and recovery retention.
- Unmarked legacy bundle folders are preserved and require a new export destination. Old custom `.publish-lock` / staging artifacts are not silently migrated or deleted; uncertain records require manual inspection.
- Linux/macOS, Qt 6, sanitizer runs, an application audio-callback/plugin hang child, full callback timing, installed package, editor paint/undo, 1,000 transport cycles, 8-hour playback and listening remain open.

## Phase 2, 17 October 2026 (historical)

Source changes for `UMSETZUNGSPLAN_FEHLERBEHEBUNG_STABILISIERUNG.md` are in the tree. The table below records only commands that were run. A full LMMS link is still absent: this machine has Qt 5.15.2 and no complete libsndfile/vcpkg tree, so `StabilizationTest` was not linked.

| Package | What changed | Run on 17 October 2026 | Still open |
| --- | --- | --- | --- |
| AP-F01 | `cmake/LmmsTestRuntime.cmake` and `tests/runtime/lmms_test_launcher.cpp` prepend the Qt bin directory before the test process starts. CMake stays at 3.13; `ENVIRONMENT_MODIFICATION` is not required. | Without the launcher, minimal tests exited `0xc0000135` or sat on a loader dialog for 60 s. With the launcher, the same tests passed. `WindowsLoaderTest` copied a child without its import DLL and failed in 0.08 s, then passed when the DLL was copied beside it. | Native and offscreen DPI tests were not re-run. No DLL was removed from the Qt installation. |
| AP-F03 | `SaveTransaction` keeps backup B until project C commits. Rollback restores B. A failed cleanup is success plus the leftover path. | `SaveTransactionTest` 5/5 in `build/phase2-minimal` Release. | Not run inside the full application or with a Windows share-mode lock. |
| AP-F02 | Bundle publish identifies an LMMS directory, refuses symlinks and foreign folders, and records Prepared / OldMoved / NewPublished. Two renames are not described as atomic. | `BundleTransactionTest` passed: new bundle, foreign folder kept, second publish, failed move leaves the original. | No two-process run and no injected crash between the two renames. |
| AP-F08 | XML is not passed to the size header. Inflate grows in 64 KiB blocks and stops at the budget. Qt's prefixed zlib (`QtZlib`, `z_inflate`) is used when a separate zlib is absent. | `ProjectCodecTest`: XML line/column, BOM, bad header, declared 1 GiB rejected with peak 0, expansion past a forged small size, checksum, depth and element budgets, `qCompress` round trip. 0 skipped. | No multi-megabyte stock `.mmpz` load in the application. DOM rollback after `Song::clearProject()` is still the open item from the overall plan. |
| AP-F10 | Lock file version 2 stores pid and start token. Unknown is not dead. The startup dialog lists every candidate; Discard selected deletes one; Escape deletes nothing. The loaded source stays until a later recovery write or a real project save. | `RecoveryIdentityTest` passed, including a live claim that a second claim cannot steal. | The dialog was not clicked. PID reuse was not simulated with a second process. |
| AP-F11 | Minimal configure is `tests/minimal/CMakeLists.txt`. `StabilizationTest` AI cases are under `LMMS_HAVE_MCP`. The worker test joins its `std::thread` before that thread is destroyed. | Minimal ctest: Save, Codec, Bundle, Recovery, ReleaseQueue, WindowsLoader. | `StabilizationTest` itself was not linked. MCP ON and OFF were not configured as two application trees. |
| AP-F13 / F09 / F15 | Worker constructors no longer reset the queue. One `AudioEngine` owns it. Destructors join and abort instead of `QThread::terminate()`. `startAndWaitForJobs` uses one absolute deadline; inline time is measured and is not preempted. | Not executed. The decision is in `render-deadline.md`. | No worker-stall run, no child-process hang, no 0/1/N worker measurement. |
| AP-F07 / F06 | Mixer cleanup waits until `batchInFlight()` is false. Failed periods publish a silence buffer workers do not receive. Export aborts instead of writing that period. | Not executed. | No marked-period audio run. |
| AP-F05 | Non-note play handles move through a fixed 1024-slot queue. A full queue keeps the pointer on the engine list. | `ReleaseQueueTest` covers the queue and the negative-detune span helper only. | No destructor-thread or shutdown run. |
| AP-F04 | Empty thumbnails do not dereference a null frame pointer. Revision hashes every frame once when the buffer is built. | Not executed against `SampleThumbnail`. The hash helper's contract is the new `SampleBuffer::contentRevision`. | No painted widget, no ASan. |
| AP-F12 | `renderNextPeriod` restores the render-thread flag with RAII. `nextAudioBuffer` had no callers in this tree and was removed. | Signal search only. | No thrown-exception render test. |
| AP-F14 | `AutomationClip::loadSettings` can store a negative node. `NoteTimeIndex::spanBounds` includes that left edge. Paint uses the same helper. New editor nodes still clamp to 0. | `ReleaseQueueTest::negativeDetuneExtendsTheSpanStart` passed. | No project file with a negative node was loaded in the piano roll. |
| AP-F16 | Docs in this directory updated. | The minimal ctest above. | No package, no 1,000 start/stop cycles, no 8-hour run, no listening pass. |

Command, from `tests/minimal`, generator Visual Studio 17 2022, MSVC 19.44, Qt 5.15.2, CMake 4.4.3, Windows 10.0.26200:

```powershell
cmake -S tests/minimal -B build/phase2-minimal -DLMMS_QT_MAJOR=5 "-DCMAKE_PREFIX_PATH=C:/tmp/lmms-qt5/5.15.2/msvc2019_64"
cmake --build build/phase2-minimal --config Release --parallel 2
ctest --test-dir build/phase2-minimal -C Release --output-on-failure
```

Last run of that ctest: SaveTransactionTest, ProjectCodecTest, BundleTransactionTest, RecoveryIdentityTest, ReleaseQueueTest, and WindowsLoaderTest passed.

Status after the implementation pass on 17 October 2026. "Compiled" means the translation unit built in the standalone check in `baseline.md`. It is not a full LMMS link.

| ID | Fix | Evidence | Not verified |
| --- | --- | --- | --- |
| B01 / R01–R03 | `DataFile::publishFile` uses `QSaveFile` on the final path. A new backup is copied to `.bak.incoming` and only then replaces `.bak`. A failed copy or a non-file `.bak` leaves the project and the previous backup in place. Bundles publish only by renaming a private staging directory. | `DataFile.cpp` compiled. `failedBackupCopyKeepsPreviousBackup` is in `StabilizationTest.cpp`, which compiled but was not linked. | Injected full-disk and locked-target runs inside the full app. |
| B02 / R04–R05 | `Song::loadProject` restores the loading flag and journal mode on the early failure return. Recovery files are per process id. Autosave checks the write result. | `Song.cpp` and `ConfigManager.cpp` compiled. | Opening a damaged project in the GUI and then undo/MCP. |
| B03 / R06–R07 | Waveform columns take a half-open span from either frames or a live peak level. `rend()` is not dereferenced. The thumbnail revision mixes a stride of frames, so a middle-frame edit misses the old cache entry. | `SampleThumbnail.cpp` compiled. `thumbnailRevisionSeesMiddleFrames` is in `StabilizationTest.cpp`, which compiled but was not linked. | AddressSanitizer and a painted widget. |
| B04 / R08–R09 | A batch stays live until `process()` returns. `tryResetJobQueue` fails while a wait is open or a worker is inside `run()`. The audio callback waits at most two periods, clamped to 2–50 ms, then keeps the jobs. `masterMix` does not call `startAndWaitForJobs` again after a miss. The destructor joins workers and aborts if a join fails, instead of freeing their jobs. | `AudioEngineWorkerThread.cpp`, `AudioEngine.cpp`, and `Mixer.cpp` compiled. `StabilizationTest.cpp` compiled. The test executable was not linked. | 1,000 start/stop runs, TSan, and a run of `workerBatchStaysLiveUntilProcessReturns`. |
| B05 / R18 | Remote-plugin message waits take a deadline. Audio processing uses at most 50 ms and then silence. A state machine covers Starting, Ready, Processing, Failed, Stopping, and Stopped. Shutdown waits 3 s before terminating the helper process. | `RemotePlugin.cpp`, `RemotePluginBase.cpp`, and `SystemSemaphore.cpp` compiled together. | A live hung plugin process. |
| B06 / R11 | Lock-wait time is separate from smoothed CPU load. Deadline misses increment on the raw sample. File export is a ring drained off the audio thread. | Contract test passed: 10 ms lock wait, 100 µs processing, 1 ms limit, smoothed load under 50, one miss. | A running audio callback. |
| B07 | Finished non-note play handles are deleted on the next period, after workers have joined. Removal is one pass. | `AudioEngine.cpp` compiled. | Allocation profile and XRun comparison. |
| B08–B09 / R12–R14 | Hidden files follow the checkbox. Search results carry a generation, are debounced and batched, and do not block the GUI on the previous future. The search object destructor does not join filesystem work. Results are posted through `QPointer` and dropped if the browser is gone. Sample preview is generated off the GUI thread. | `FileSearchJob.cpp` compiled. | 100,000-file search, a slow disk, and closing the browser during that read. |
| B10 / R10 | Pool tasks store exceptions on the promise. Worker count 0 becomes 1. Shutdown finishes queued work and rejects new work. Queue limit is 4096 by default. | Contract test passed, including `QueueFull`. | File-search destruction during a stuck directory read. |
| B11 / R15 | Three slots. The audio thread writes a slot the GUI is not copying and publishes the index. A failed `copyLatest` does not change the caller buffer. The oscilloscope keeps the previous complete snapshot. | `AudioDisplayBuffer.h` and `Oscilloscope.cpp` compiled. `displayCopyLeavesDestinationUntouchedUntilClaimed` is in `StabilizationTest.cpp`, which compiled but was not linked. | A blocked GUI during playback. |
| B12 / R16 | `NoteTimeIndex` finds notes that start before the range and extend into it, including a longer detune span. Piano Roll uses that index from 256 notes upward and still rebuilds only when the geometry signature changes. | `PianoRoll.cpp` compiled. `noteIndexFindsOverlapsAndDetuneExtent` is in `StabilizationTest.cpp`, which compiled but was not linked. | 100,000-note timing. |
| B13 / R17 | Oscilloscope reloads `output_graph` through `Dpi::watch` and logical size. | Code review. Existing DPI tests were not re-run in this pass. | Monitor changes on hardware. |
| B14 | Main toolbar uses a minimum height. The browser search field uses font metrics and an accessible name. The oscilloscope has an accessible name. | Code review. | Screen reader and 125–200% layout pass. |
| B16 / R21 | Artifact reads allow up to 512 MiB. The file hash and chunk read run through `readArtifactFile`, and the dispatcher schedules that work on the thread pool instead of holding the GUI thread for the whole read. Later chunks compare size and mtime. | `AiJobManager.cpp` and `AiCommandDispatcher.cpp` compiled. `artifactFileReadsChunksAndRejectsChanges` compiled inside `StabilizationTest.cpp` but the test executable was not linked. | A multi-minute WAV download from a running MCP client. |
| B17 / R22 | Terminal jobs can be released. Active jobs stay capped. Young operation keys are not deleted. | `releaseFinishedJobs` and the dispatcher retention comments. | A 128-job GUI session. |
| B18 | `DiagnosticReport` writes version, audio counters, worker timeout and overflow counts, optional GUI lag, and no project path. Help â†’ Create diagnostic report. | `DiagnosticReport.cpp` compiled. | Deadline impact while playing. |
| B19 | `StabilizationTest` is registered with a 180 s timeout. Presets describe the Windows reference configure. | Contract subset executed. Full ctest needs the application link. | CI run of the new test. |

## Installation follow-up discovery, 17 October 2026

The installation request prompted a wider machine inventory. Visual Studio Build Tools 2026 with MSVC 19.51 is installed under Program Files (x86), alongside the previously tested VS 2022 installation. A Qt 6.8.3 SDK and real vcpkg dependencies are also available in the sibling high-DPI checkout. The earlier configure failures accurately describe the selected VS 2022 runs, but the inference that no supported local compiler exists was incorrect. A separate supported build and installation verification now follows.


## Verified local installation, 17 October 2026

The follow-up used Visual Studio Build Tools 2026 / MSVC 19.51, Qt 6.8.3 and the real vcpkg libraries from the sibling checkout. Source commit `5f4cc927f` builds successfully as a complete x64 Release application with MCP enabled and the selected instruments/plugins. This supersedes earlier statements that a full local link was unavailable; those earlier runs used a different toolchain.

- Full application CTest: **69/69 passed**, including `StabilizationTest`, `EngineStabilizationTest`, DPI widgets, Windows loader and the native Node-to-Qt MCP integration.
- Companion `npm test`: **9/9 passed**.
- Package version invocation, native Windows GUI initialization (hidden window titled `Untitled - LMMS 1.3.0-alpha`) and normal window-close shutdown succeeded with a clean system-only PATH. Loaded Qt, PortAudio and libsndfile DLLs came from the package.
- CLI WAV export succeeded both before and after relocation: stereo PCM16, 44,100 Hz, 151,040 frames (3.42494 seconds), peak 25,951 and RMS 6,497.60. The render report states success and exit code 0.
- Installed at `%LOCALAPPDATA%/Programs/LMMS-High-DPI`; previous binaries retained at `%LOCALAPPDATA%/Programs/LMMS-High-DPI.backup-20261017-5f4cc927f`. Existing shortcuts remain valid. The SHA-256 of the separate user configuration was unchanged during installation; user projects were not modified.

The full build exposed and fixed direct Qt include dependencies, MCP widget header paths, root CTest discovery, a C++17 requirement in the Windows test child, and two test-fixture assumptions. The native MCP test also exposed a real Windows sharing violation: `QTemporaryFile::close()` retains its native handle. The secured temporary connection file is now destroyed before atomic publication without replacement.

Evidence remains in the ignored local `build/` directory: `user-build.log`, `user-tests.log`, `user-mcp-test.log`, `dispatcher-result.txt`, `user-stage-install.log`, and `install-smoke/`.

This is a local installation acceptance, not completion of all release gates. A full MCP-OFF build, full Qt 5 reference build, sanitizers, 1,000 start/stop cycles, eight-hour playback, visual multi-monitor/hardware checks and listening acceptance remain open. The hidden GUI smoke test does not substitute for those checks.
