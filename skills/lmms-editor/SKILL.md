---
name: lmms-editor
description: "Inspect and edit LMMS projects, tracks, MIDI clips, notes, existing PatternStore steps and song arrangement through MCP preview/apply transactions. Control transport and perform guarded undo/redo. Use for score and editor operations; use focused skills for instruments, mixing or rendering."
---

# LMMS Editor

Read the shared [session and transaction contract](../lmms-ai/references/session-and-transactions.md) before edits or transport calls. It defines bindings, operation keys, conflict recovery and history handling.

## Inspect before selecting targets

- Read `lmms_get_project`; list `song` and, when relevant, `pattern_store` with `lmms_list_tracks`. These are different containers.
- Use `lmms_list_clips` for a returned `trackId`, then `lmms_get_notes` for a MIDI `clipId`. Track names and display indices are not identities.
- Clip range queries use container timeline positions. Note range queries use positions relative to the MIDI clip. Results can overlap query boundaries; filter further when the user selected note onsets or a precise subset.
- Clip lists classify non-MIDI clips as `other`. When needed, use the discovered clip-summary resource to distinguish automation; there is no advertised `lmms_get_clip_summary` tool.
- Keep pagination within the same query and revision. Do not read an entire song's notes when the task concerns one clip.

## Build a bounded change plan

Use `lmms_preview_changes`, then `lmms_apply_changes`, for these command groups:

| Purpose | Change commands |
| --- | --- |
| Tempo and track properties | `set_tempo`, `rename_track`, `set_track_mute` |
| MIDI content | `create_midi_clip`, `add_notes`, `update_notes`, `remove_notes`, `transpose_notes` |
| Clip arrangement | `move_clip`, `resize_clip`, `duplicate_clip`, `delete_clip` |
| Track removal | `delete_track` |
| Existing patterns | `set_pattern_steps`, `assign_pattern` |

Parameter, routing and automation commands are covered by [lmms-mixing-automation](../lmms-mixing-automation/SKILL.md). New instrument tracks use a dedicated tool described in [lmms-instruments-assets](../lmms-instruments-assets/SKILL.md).

For exact call shapes and returned-ID flow, read [editing examples](references/editing-examples.md).

## Preserve note and clip meaning

- `update_notes` requires `noteRef`, `midiKey`, `positionTicks` and `lengthTicks` on each note. Read the unchanged required values from the current note. Include optional volume, pan or type only when changing them.
- Do not send the complete note-read DTO back as an update: fields such as `hasDetuning` are not accepted write arguments. Updating an existing note preserves its detuning data.
- For selected transposition, include only the selected `noteRefs`. Omitting `noteRefs` transposes the whole clip. Re-read references after edits that change the clip.
- Native transpose validation counts the source clip's notes toward the batch budget even for a subset. For a large clip, use bounded `update_notes` batches with freshly read references if a targeted transpose cannot validate.
- Moving a clip changes its timeline start, not each note's local position. Resizing does not time-stretch notes and rejects truncating regular notes. No destination-track field is exposed for moving or duplicating clips.
- `add_notes.clip` accepts exactly one of `clipId` or `clientRef`. A local `clientRef` must refer to a MIDI clip created or duplicated earlier in that same plan.

## Existing patterns and playback

`set_pattern_steps` operates on a MIDI clip in `pattern_store`, with zero-based step indices 0 through 255. It toggles specified steps; it does not replace unmentioned steps.

`assign_pattern.patternId` is the returned handle of a song `PatternTrack`, not an instrument track, clip ID or numeric pattern index. The current API does not create a PatternTrack or an instrument directly in PatternStore.

Use the song entry's `patternIndex` to identify the corresponding PatternStore slot. In the normal store layout, slot i starts at `i * ticksPerBar` on each instrument track. Verify that returned clip position and content rather than choosing the first clip or interpreting the store position as a song arrangement position.

Use `lmms_set_transport` with `play`, `pause`, `stop` or `seek`; seek positions are song ticks. A paused song is not stopped. Structural changes and history operations require a stopped song; stop as needed for the authorized editing task unless the user required playback to remain running.

For undo/redo use the known group ID from a successful mutation and a fresh project revision. Follow the shared contract; the API does not expose a general history-head query.
