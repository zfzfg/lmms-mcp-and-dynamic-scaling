# Session and transaction contract

Read this once before changes, exports or recovery, and re-read the relevant section when the session changes. The host's live tool schema is authoritative for argument shape.

Examples use objects with `tool` and `arguments` to label a call. Invoke that host tool with the `arguments` object only. Example IDs and revisions are illustrative: replace them with values returned by the same live session. Recipes are independent unless explicitly chained; execute only those matching the user's task.

## Identity and compact reads

`lmms_get_status` establishes the selected `instanceId`, readiness, effective permissions and capabilities. `lmms_get_project` returns `projectId`, `projectRevision` and project data. These identities are not filenames.

Use returned track, clip, channel, parameter and asset IDs as opaque values. Display names and indices help identify intent, but do not replace handles. New/load/recovery changes the project generation; undo/redo and rollback can also invalidate handles. Re-read affected owners and notes rather than attempting to repair an ID string.

List tools can return `nextCursor`. Pass it unchanged with the same filters and owner to the next call. It is bound to query, project and revision, not a numeric page. Restart on `STALE_CURSOR`. For a multi-call snapshot where consistency matters, read the project revision before and after collection; discard mixed data if it changed.

`lmms_get_notes` returns `clipRevision` and short-lived `noteRef` values. A note reference identifies that read's note, not a persistent musical event. Re-read notes after edits or restoration.

## Resources and optional prompts

If the host supports resource reads, discover the actual templates. Use IDs from the same connected instance/project:

| Resource URI template | Current content |
| --- | --- |
| `lmms://instances/{instanceId}/capabilities` | Status, effective permissions and capabilities |
| `lmms://instances/{instanceId}/projects/{projectId}/summary` | Project summary and revision |
| `lmms://instances/{instanceId}/projects/{projectId}/tracks/{trackId}` | Clip-list response for that track, not a complete track/instrument dump |
| `lmms://instances/{instanceId}/projects/{projectId}/clips/{clipId}/summary` | Clip identity, placement, type and MIDI note count where applicable |
| `lmms://instances/{instanceId}/artifacts/{artifactId}` | Verified WAV content within the resource retrieval limit |

Resource reads enforce instance/project identity and the same native permissions as the corresponding tools. If a track response contains `nextCursor`, use the list tool to continue collecting clips.

The current optional MCP prompts are `analyze_project`, `compose_midi_clip` and `review_mix`. Discover their live arguments if using them; they are request templates, not additional capabilities or prerequisites for calling tools. Preserve the actual user's instructions and existing authorization instead of introducing an extra review step merely because a template mentions one.

## Musical units

- Read `ticksPerQuarter` and `ticksPerBar` from the project rather than assuming 48 and 192. The current implementation reports 48 ticks per quarter, but different meters change bar length.
- A note value with denominator D has `ticksPerQuarter * 4 / D` ticks. A meter beat has `ticksPerQuarter * 4 / timeSignature.denominator` ticks.
- For a one-based user bar B, the zero-based bar start is `(B - 1) * ticksPerBar`. A clip spanning N bars has `N * ticksPerBar` ticks.
- Song clips and transport seek use song timeline positions. PatternStore clips use their container positions. Notes and automation points are relative to their own clip.
- Tick positions are nonnegative integers; new regular note and clip lengths are positive integers. Quantize fractional calculations deliberately to the supported resolution.
- MIDI pitch ranges from 0 to 127; MIDI C4 is 60. Note `volumePercent` is 0 through 200, and `pan` is -100 through 100. Do not substitute MIDI velocity values or normalized pan.

## Required mutation fields

The following tools require `operationKey`, `projectId` and `expectedRevision`, in addition to their specific arguments:

`lmms_apply_changes`, `lmms_set_transport`, `lmms_undo`, `lmms_redo`, `lmms_create_instrument_track`, `lmms_load_asset`, `lmms_save_project_copy`, `lmms_start_render`.

`lmms_cancel_job` requires `operationKey` and `jobId`, without project/revision fields. Undo and redo additionally require a known `undoGroupId`.

Use a fresh, unique operation key of 1 through 128 characters for each new intent, and retain it with the exact tool and arguments before calling. Never reuse a key for changed arguments. Even changing only `expectedRevision` under an existing key is a different request.

Dedicated instrument, asset, transport and export tools are separate operations, not commands inside a change plan. Read fresh project state before each dependent operation and use the revision it actually returns; required revision fields are not a reason to assume all dedicated actions form a single atomic transaction.

## Preview, apply, verify

1. Read the current project and the target content. Identify the requested delta and existing authorization.
2. Prepare `changes` for `lmms_preview_changes` with `projectId` and `expectedRevision`. It validates and stores an expiring plan without editing the project.
3. Inspect the returned `planId`, `expectedRevision`, `summary`, `affectedObjects` and `expiresAt`. Do not add a `planHash` argument to apply; its schema does not accept it.
4. Apply that exact plan using its binding and a retained operation key. Preview is technical validation, not a blanket requirement for another user confirmation.
5. Check `ok` and the structured result. `data.status: "committed"` carries the new revision, `createdObjects`, `addedNotes` and `undoGroupId`. `"unchanged"` creates no new history group or project revision; its undo group can be empty.
6. Re-read the affected state to verify the musical result. Refresh references before a dependent edit. A revision conflict requires a fresh read and preview, not substitution of a newer revision into an old plan.

One applied plan is atomic and, if changed, produces one undo group. Multiple plans and dedicated tool calls are separate commits. Do not promise to undo a whole multi-step task in one operation.

Use runtime `maxBatchCommands` and `maxBatchNotes` when provided. Current limits are 64 commands and 512 affected notes per plan, with additional command-specific limits. Split larger tasks at meaningful musical boundaries and refresh state between commits. Pagination limits do not enlarge write budgets.

Use a unique `clientRef` for objects created inside a plan. Only argument forms explicitly supporting it can reference that local name. In particular, `add_notes.clip.clientRef` can refer to a MIDI clip created or duplicated earlier in the same plan. After apply, use `data.createdObjects[clientRef]` as its real handle; do not send local names to commands expecting existing channel or track IDs.

Apply, instrument creation, asset loading and history changes need a stopped song. Pause is insufficient. For an authorized editing task, stop playback when needed unless the user required it to remain running. Do not repeatedly retry a busy edit while leaving that condition unchanged.

## Recover an uncertain mutation

After a timeout or lost response, keep the original tool and arguments unchanged. Call `lmms_get_operation` with only the original `operationKey`; it looks up that operation rather than reserving a new one.

- If `data` contains the original successful response, consume it and verify the result. Do not execute the intent again.
- If it contains a terminal failure, inspect current state and correct the cause. A revised intent or new preview uses a new key only after the original result is established.
- If the record is absent or inconclusive and the same LMMS instance is still running, an exact retry with the original key and arguments can share or retrieve the existing operation. Do not change any fields, including defaults or revision.
- If the instance changed, the record is outside its retention, or the result remains uncertain, reconcile against current project state before deciding on another mutation. Do not claim cross-restart exactly-once behavior.

The outer operation lookup's `ok` means the record was found; inspect the nested original response's `ok` and `error` too. A `retryable` flag does not make a new-key mutation safe.

Operation results are held in the same running LMMS instance for at least 24 hours, subject to its bounded registry accepting the operation. A companion reconnect/restart can look them up; an LMMS restart loses that registry.

## History handling

Keep `data.undoGroupId` from an applied plan or top-level `undoGroupId` from instrument creation and asset loading. The current MCP has no general history-head listing tool.

Before undo/redo, read the active project and revision. Use the group that is known to be the expected head of the appropriate stack. The same group can be redone after its undo if no intervening edit changed history. Supply the shared mutation fields, including a new operation key.

On `HISTORY_CONFLICT`, do not guess a group ID, undo extra groups, or overwrite newer user work. If the needed head is unknown, explain that MCP cannot establish it and leave that history action to the user.

After successful undo/redo, use `newRevision` and re-list targets, parameters and notes before further editing.
