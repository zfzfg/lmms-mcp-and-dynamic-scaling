# Release gates

This is not a release approval. The product changes below are in the tree. The gates that need hardware, installers, or sanitizers are open.

## Follow-up on base `64bc33825`, 17 October 2026

- Minimal production contracts: **8/8 Release and 8/8 Debug**, including real process contention/crash recovery, worker exceptions/late completion and painted waveform widgets.
- Isolated DPI: **25/25 Release and 25/25 Debug**, native Windows and offscreen scale cases.
- Full MCP ON/OFF configure attempts: **blocked** before dependency discovery because the source requires MSVC >= 19.51 and the local compiler is 19.44.
- `EngineStabilizationTest` is registered for full integration. Its added save/reload, recovery-retention, destructor-affinity and marked-output tests have not been linked/run. See `findings.md`.
- AP-F08 is reopened for application acceptance despite the passing codec regression tests. No release approval follows from this change.

## Done in this workspace

- P0 save publication no longer reports success before `QSaveFile::commit` on the final path.
- Load failure restores the loading flag and journal mode.
- Waveform fallback does not dereference an end iterator.
- Thread pool stores task exceptions and has a worker-count fallback.
- Audio periods record lock-wait time and deadline misses without writing the profile file on the audio thread.
- Oscilloscope reads a snapshot instead of a pointer to the live mix buffer.
- Browser search is generation-tagged and the hidden-file checkbox is the checked state.
- Remote plugin audio waits are bounded.
- MCP artifact and job retention rules are in `AiJobManager` / `AiCommandDispatcher`.
- `CMakePresets.json` records the intended Windows reference configure.
- `cmake/build-manifest.json.in` is written to the build tree at configure time.

## Phase 2 still required

The current file/worker/renderer contract runs are recorded in `findings.md`. There is still no full link or completed MCP ON/OFF build pair, no running-application audio stall test, and no 8-hour or listening session.

## Still required before a release

- [ ] One full Windows link with Qt 5.15.2, vcpkg dependencies from `vcpkg.json`, and `ctest` including `StabilizationTest` and `DpiWidgetTest`
- [ ] The same tree with `-DWANT_MCP=OFF`
- [ ] Linux and macOS jobs already in `.github/workflows/build.yml` on this commit
- [ ] ASan/UBSan and TSan on the worker, thumbnail, and loader tests
- [ ] The checklist in `manual-acceptance.md`
- [ ] An installed package started with a clean `PATH`
- [ ] No open P0. Every accepted P1 has a named skip in this file.

## Local commands that did run

```text
CoreContractTest: 5 passed, 0 failed
```

That executable covers the thread pool, profiler accounting, and display snapshot only.

## Local commands that did not run

- `npm --prefix tools/mcp test` was not repeated in this pass. The plan's earlier run was 9/9 on the previous tree.
- `ctest` of `build/dpi` was not repeated.
- A full `cmake -S . -B build/reference-qt5` was not completed because libsndfile and the rest of the vcpkg set are not installed here.
