# Editing examples

These are independent recipes unless a sequence is explicitly stated. Execute only the behavior requested by the user. Call objects label the host tool and its argument object; all IDs and revisions must come from the live session.

## Inspect one target

Start with project state, then select a track by its returned identity:

```json
{"tool":"lmms_get_project","arguments":{}}
```

```json
{"tool":"lmms_list_tracks","arguments":{"container":"song","limit":100}}
```

Assume the response identifies the intended instrument as `trk_lead_demo`. Follow `nextCursor` if the needed track is not on the first page.

```json
{"tool":"lmms_list_clips","arguments":{"trackId":"trk_lead_demo","startTick":0,"endTick":768,"limit":100}}
```

After selecting the returned MIDI clip `clip_theme_demo`, inspect its local note positions:

```json
{"tool":"lmms_get_notes","arguments":{"clipId":"clip_theme_demo","startTick":0,"endTick":384,"limit":100}}
```

Use the returned `clipRevision` and note references. Reading a clip at song tick 768 does not make its first note's local position 768.

## Transpose a selected pair

Assume fresh reads established `projectId: proj_demo`, revision 12, and the selected refs `note_demo_a` and `note_demo_b`. The user requested moving only those notes up two semitones.

```json
{
  "tool":"lmms_preview_changes",
  "arguments":{
    "projectId":"proj_demo",
    "expectedRevision":12,
    "changes":[
      {"kind":"transpose_notes","clipId":"clip_theme_demo","noteRefs":["note_demo_a","note_demo_b"],"semitones":2}
    ]
  }
}
```

Assume the preview returns `planId: plan_transpose_demo`, still bound to revision 12:

```json
{
  "tool":"lmms_apply_changes",
  "arguments":{
    "projectId":"proj_demo",
    "expectedRevision":12,
    "planId":"plan_transpose_demo",
    "operationKey":"edit-transpose-demo-001"
  }
}
```

Retain the returned `projectRevision` and `data.undoGroupId`. Read the notes again and check that only the chosen pitches changed. The old note refs are not reusable just because note count stayed constant.

## Update timing while preserving dynamics

This separate scenario starts from fresh revision 20. `note_timing_demo` currently has pitch 64, local start 48 and length 48. The requested change shortens it to 36 ticks.

```json
{
  "tool":"lmms_preview_changes",
  "arguments":{
    "projectId":"proj_demo",
    "expectedRevision":20,
    "changes":[
      {
        "kind":"update_notes",
        "clipId":"clip_theme_demo",
        "notes":[{"noteRef":"note_timing_demo","midiKey":64,"positionTicks":48,"lengthTicks":36}]
      }
    ]
  }
}
```

The unchanged pitch and position are required. Omitting optional `volumePercent`, `pan` and `noteType` preserves them. Do not include `hasDetuning`, which is read-only metadata.

To remove a different selected note at the same starting revision, the command instead is:

```json
{
  "tool":"lmms_preview_changes",
  "arguments":{
    "projectId":"proj_demo",
    "expectedRevision":20,
    "changes":[{"kind":"remove_notes","clipId":"clip_theme_demo","noteRefs":["note_remove_demo"]}]
  }
}
```

Use the returned plan and shared apply procedure for the requested recipe; do not apply both alternatives using the same revision.

## Duplicate and arrange

At fresh revision 30, duplicate an existing clip to song tick 768:

```json
{
  "tool":"lmms_preview_changes",
  "arguments":{
    "projectId":"proj_demo",
    "expectedRevision":30,
    "changes":[{"kind":"duplicate_clip","clipId":"clip_theme_demo","clientRef":"variation","positionTicks":768}]
  }
}
```

After apply, obtain the real duplicate ID from `data.createdObjects.variation`, then re-read state. In a separate scenario where this returned ID is `clip_variation_demo` and the new revision is 31:

```json
{
  "tool":"lmms_preview_changes",
  "arguments":{
    "projectId":"proj_demo",
    "expectedRevision":31,
    "changes":[
      {"kind":"move_clip","clipId":"clip_variation_demo","positionTicks":1536},
      {"kind":"resize_clip","clipId":"clip_variation_demo","lengthTicks":768},
      {"kind":"rename_track","trackId":"trk_lead_demo","newName":"Lead variations"}
    ]
  }
}
```

This resize is appropriate only after inspecting that it will not truncate notes. The clip stays on its current track.

## Explicit track, tempo and removal requests

For a user-requested tempo of 120 BPM and muting a specific track:

```json
{
  "tool":"lmms_preview_changes",
  "arguments":{
    "projectId":"proj_demo",
    "expectedRevision":40,
    "changes":[
      {"kind":"set_tempo","bpm":120},
      {"kind":"set_track_mute","trackId":"trk_lead_demo","muted":true}
    ]
  }
}
```

Tempo must be an integer from 10 through 999. There is no corresponding time-signature command.

Removal recipes require matching user intent. For example, removing a redundant clip and a separate unused track, whose handles have just been read:

```json
{
  "tool":"lmms_preview_changes",
  "arguments":{
    "projectId":"proj_demo",
    "expectedRevision":41,
    "changes":[
      {"kind":"delete_clip","clipId":"clip_redundant_demo"},
      {"kind":"delete_track","trackId":"trk_unused_demo"}
    ]
  }
}
```

Read the resulting track/clip lists after applying. Do not use removal as cleanup outside the requested scope.

## Transport

With a freshly read project at revision 50, stop before structural edits:

```json
{"tool":"lmms_set_transport","arguments":{"projectId":"proj_demo","expectedRevision":50,"operationKey":"transport-stop-demo-001","action":"stop"}}
```

For a requested audition, independently refresh project state before seeking or playing. If the revision is still 50:

```json
{"tool":"lmms_set_transport","arguments":{"projectId":"proj_demo","expectedRevision":50,"operationKey":"transport-seek-demo-001","action":"seek","tickPosition":768}}
```

```json
{"tool":"lmms_set_transport","arguments":{"projectId":"proj_demo","expectedRevision":50,"operationKey":"transport-play-demo-001","action":"play"}}
```

Use a new key for each action. The transport response supplies `action`, `isPlaying` and `positionTicks`; none of these establishes that the agent heard the result.

## Guarded undo and redo

In this independent scenario the agent knows the most recent committed group is `undo_edit_demo`, the song is stopped, and a fresh read reports revision 60:

```json
{"tool":"lmms_undo","arguments":{"projectId":"proj_demo","expectedRevision":60,"undoGroupId":"undo_edit_demo","operationKey":"history-undo-demo-001"}}
```

If it succeeds with `newRevision: 61`, re-read the project and objects. If no other history operation intervened and the user requests redo:

```json
{"tool":"lmms_redo","arguments":{"projectId":"proj_demo","expectedRevision":61,"undoGroupId":"undo_edit_demo","operationKey":"history-redo-demo-001"}}
```

A history conflict ends this recipe. Do not guess a newer head or undo unrelated user work.
