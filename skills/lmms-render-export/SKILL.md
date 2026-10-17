---
name: lmms-render-export
description: "Save new LMMS project copies, render snapshot WAV mixdowns, monitor or cancel asynchronous jobs and retrieve verified audio artifacts through MCP. Use for LMMS audio delivery and export failures; stems and other output codecs are not currently exposed."
---

# LMMS Render and Export

Read the shared [session and transaction contract](../lmms-ai/references/session-and-transactions.md). Saving, starting a render and cancellation require operation keys; saving and starting a render also require project/revision fields even though they do not edit the score.

## Save a project copy

Use `lmms_save_project_copy` with a new safe basename ending in `.mmp`. The destination is LMMS's permitted export directory; the tool does not accept an arbitrary directory or overwrite an existing file.

Report the returned `savedPath`. A copy is not a command to save over the user's open project, and the tool does not package all external assets into a self-contained archive.

## Start and follow a snapshot render

1. Read current project state and effective `export` permission. Choose a new `.wav` basename and retain the operation key before starting.
2. Call `lmms_start_render`. Current format support is `wav`, sample rates 8000 through 192000 Hz, and bit depths 16, 24 or 32. Unless requested otherwise, use the API defaults: 44100 Hz and 16 bit.
3. Retain the returned `jobId`. Poll `lmms_get_job` at a moderate interval, such as every two seconds; increase the interval if nothing changes.
4. Treat `queued`, `preparing_snapshot`, `running` and `validating` as in progress. Only `succeeded` establishes a completed export. `failed` and `cancelled` are terminal outcomes to report.

Snapshot rendering uses separate workers and the captured project revision. Later project edits are not added to that snapshot. Report the job's actual `projectRevision` and output metadata rather than assuming it matches the latest editor state.

The current server limits concurrent workers to two and retains at most 128 jobs, with a ten-minute worker budget. Avoid duplicate starts; a start-response timeout calls for operation lookup, not another render.

For user-requested cancellation call `lmms_cancel_job` with the job ID and a fresh operation key; it does not take project/revision fields. Read the job again to establish its resulting state, including a possible completion race.

## Deliver the verified result

On success inspect `artifactId`, `artifactPath`, `sha256`, `sizeBytes`, `durationSeconds`, `sampleRate` and `bitDepth`.

Read audio using the discovered resource template `lmms://instances/{instanceId}/artifacts/{artifactId}`. Use the actual instance and artifact IDs. There is no advertised artifact-download tool; raw chunk assembly is the companion's responsibility.

Resource retrieval is limited to 16 MiB and checks the assembled checksum. Larger verified exports remain available at the returned local artifact path on the LMMS machine. Report that path when the resource cannot be returned; do not claim a file was attached or uploaded unless the host actually did so.

Render validation establishes file integrity and WAV metadata, not musical quality. Do not claim audible review without inspecting available audio.

Read [export examples](references/export-examples.md) for complete arguments, operation recovery, cancellation and failure handling. The current interface does not offer stems, MP3 or direct remote publishing.
