---
name: lmms-ai
description: "Inspect, compose, edit, mix and export music in a live LMMS instance through its MCP tools. Route connection, editor, composition, instrument, automation, export and recovery tasks to focused skills. Use for operating LMMS through MCP, not developing LMMS source code or controlling another DAW."
---

# LMMS AI

Use this suite to operate the selected LMMS instance. Discover the host's actual tools and their schemas; a host may add a namespace to names such as `lmms_get_project`. Never invent a tool because a similar feature exists in the LMMS UI.

## Start with the current session

1. Call `lmms_get_status`. Check `instanceId`, `ready`, `permissions`, `capabilities` and any diagnostics. Tool discovery alone does not establish a working LMMS connection.
2. When the project is ready, call `lmms_get_project`. Retain `projectId`, `projectRevision`, tick resolution, time signature and playback state.
3. Inspect only the tracks, clips, notes, parameters or mixer channels needed for the request. Follow returned pagination cursors.
4. Load the relevant skill below. Read the shared [session and transaction contract](references/session-and-transactions.md) before making changes, exporting, or recovering an uncertain operation, unless it is already loaded.

Prefer `structuredContent` over a human-readable summary. Check `isError` and `ok`; a tool returning text or an operation record being found does not establish that the requested change succeeded.

## Choose the relevant skill

| Task | Read |
| --- | --- |
| Configure a host, select an instance, inspect permissions | [lmms-connection](../lmms-connection/SKILL.md) |
| Inspect or change tracks, clips, notes, patterns or transport; undo/redo | [lmms-editor](../lmms-editor/SKILL.md) |
| Turn musical intent into melodies, basslines, chords, beats or arrangements | [lmms-composition](../lmms-composition/SKILL.md) |
| Select an internal instrument, create its track, load a preset or sample | [lmms-instruments-assets](../lmms-instruments-assets/SKILL.md) |
| Adjust registered parameters, mixer routing or automation | [lmms-mixing-automation](../lmms-mixing-automation/SKILL.md) |
| Save a project copy, render WAV audio, inspect jobs or retrieve audio | [lmms-render-export](../lmms-render-export/SKILL.md) |
| Diagnose a failed call, stale state or uncertain result | [lmms-troubleshooting](../lmms-troubleshooting/SKILL.md) |

Read only the skills and references relevant to the request. These are sibling folders installed together; no source checkout is needed during normal use.

## Work within the actual interface

- Preserve the user's musical intent, selected project and requested editing scope. Do not switch instances or change unrelated tracks to make an operation easier.
- Preview validates a proposed change; it is not an extra user approval requirement. Continue already authorized work. Ask only when a material musical choice, missing target or authorization remains unresolved.
- Use live catalogs for instruments and assets, and live parameter descriptors for values and units. A readable UI state is not necessarily writable through MCP.
- The current interface supports WAV snapshot mixdown and project copies. It does not expose stems, arbitrary plugin controls, effect-chain editing, general file management or script execution.
- Playback status proves transport state, not that the agent listened to the audio. Describe structural observations separately from findings supported by actual audio analysis.
- Installed tools and runtime responses determine availability. Existing implementation and schema checks do not establish full application or audible-render acceptance.

For suite updates and acceptance exercises, read [maintenance and validation](references/maintenance-and-validation.md).
