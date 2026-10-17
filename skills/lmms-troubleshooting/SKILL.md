---
name: lmms-troubleshooting
description: "Diagnose LMMS MCP connection, readiness, permission, revision, handle, history, instrument, asset and render failures. Recover uncertain mutations through their existing operation keys and distinguish protocol discovery from a working native LMMS session."
---

# LMMS Troubleshooting

## Identify the failing layer

Record the failing tool, its structured error code and message, the selected instance/project, and any operation or job ID. Retain the exact arguments of uncertain mutations. Do not include connection-file secrets.

- No LMMS tools: inspect host registration and launcher diagnostics using [lmms-connection](../lmms-connection/SKILL.md).
- Discovery works but calls fail: test `lmms_get_status`; discovery does not require LMMS to be running.
- Connected but not ready: inspect native loading/startup diagnostics.
- Reads succeed but changes fail: inspect effective permissions, playback state, project revision and the specific target.
- A render started: inspect its existing job rather than starting another.

Before retrying any uncertain write, read the shared [session and transaction contract](../lmms-ai/references/session-and-transactions.md). A `retryable` flag alone does not authorize replay with a new key.

## Choose recovery from the actual error

| Code or observation | Recovery |
| --- | --- |
| `NOT_CONNECTED` | Check the selected connection-file path, LMMS process and companion diagnostics. Reconnect the same intended instance; confirm its identity before resuming. |
| `NOT_READY` | Let initialization/loading finish; re-read status and project. Avoid writes while the project is unavailable. |
| `PERMISSION_DENIED` | Read effective permissions. Identify the missing permission or revoked session; do not bypass the native profile. |
| `WRITER_BUSY` | Another connection owns the writer slot. Use read-only mode for observation or have the intended existing writer released; do not terminate an unrelated host. |
| `BUSY` / `PLAYBACK_ACTIVE` | Check `get_project.isPlaying` and `isPaused`, exporting, journal activity and queue/job limits. Stop playback when needed for the authorized edit. A paused song is not stopped. Status `busy` alone does not summarize playback. |
| `PROJECT_CHANGED` | Re-read the active project and re-identify targets. Discard old-generation plans, references and cursors. Do not redirect edits silently into a newly loaded document. |
| `REVISION_CONFLICT` | Re-read affected material and rebuild the intended delta. Create a new preview and, after the old attempt is known not to have committed, a new operation key. |
| `STALE_HANDLE` | Re-list the owner and target; re-read notes or parameters. Never derive a replacement handle from the old string. |
| `STALE_CURSOR` | Restart the same query from its first page with current project state. Do not interpret the cursor as an offset or combine old and new pages. |
| `PLAN_EXPIRED` | Inspect current state and preview again. If a previous apply response was lost, resolve that operation first. |
| `TIMEOUT` / connection loss during mutation | Query `lmms_get_operation` with the original key. Inspect the nested result; retry only according to the shared contract. |
| `OPERATION_KEY_REUSED` | Compare the exact original tool and arguments. Do not alter a revision or plan ID under an old key; resolve its result before starting a different intent. |
| `HISTORY_CONFLICT` | Stop history retries. Preserve newer user work; do not guess group IDs or undo additional groups to reach an old edit. |
| `PARAMETER_CONTROLLED` | Inspect parameter ownership and existing automation/controller state. A direct value write cannot override it. |
| `VALIDATION_ERROR` | Compare against the live schema and native message: required bindings, allowed fields, integer ticks, note ranges, batch budget or command-specific constraints. Correct the cause instead of repeating the same payload. |
| `UNSUPPORTED_OPERATION` | Use only currently exposed tools and commands. Explain the unsupported capability; do not invent a similarly named action. |
| `PATH_NOT_ALLOWED` | Use a safe new basename in the permitted export directory or a catalog asset ID. Existing export targets are not replaced. |
| `ASSET_NOT_FOUND` | Re-list the relevant permitted catalog. The library may have changed; an asset ID is not a path. |
| `PLUGIN_UNAVAILABLE` / `PLUGIN_LOAD_FAILED` | Refresh the internal catalog, inspect the selected asset and report failure. Do not claim a sound change or select an unrelated replacement silently. |
| `RENDER_FAILED` / `EXPORT_VALIDATION_FAILED` | Read the existing job's state/error. Inspect missing or changed assets, worker failure, output validation or artifact modification; report the terminal failure. |
| `CANCELLED` | Determine whether this is a job cancellation, revoked connection or queued command rejection. Establish the operation/job result before treating work as complete. |
| `INTERNAL_ERROR` | Inspect current state and native diagnostics. Resolve any associated operation before another write; avoid an unbounded retry loop. |

## Report what is established

`lmms_get_operation` returning `ok: true` means the record was found. Its `data` may itself contain a failed result; an empty or inconclusive record does not prove a commit.

After an LMMS restart, old operation records and object identities do not carry over. Reconcile the requested change against freshly read state and explain any remaining uncertainty; do not assume exactly-once recovery across restarts.

For render-specific limits and resource delivery, read [lmms-render-export](../lmms-render-export/SKILL.md). For automation replacement constraints, read [lmms-mixing-automation](../lmms-mixing-automation/SKILL.md).

If the installed binary cannot exercise a feature, distinguish a source/schema finding from an observed runtime result. Full LMMS, audio and platform-package acceptance remain separate from skill validation.
