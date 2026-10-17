# Release gates

This is not a release approval. The product changes below are in the tree. The gates that need hardware, installers, or sanitizers are open.

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

The file contracts in `tests/minimal` passed on 17 October 2026. See `findings.md`. That does not close the gates below. In particular there is still no full link, no MCP ON/OFF pair, no audio stall run, and no 8-hour or listening session.

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
