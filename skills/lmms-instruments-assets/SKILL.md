---
name: lmms-instruments-assets
description: "Find available internal LMMS instruments, create song instrument tracks and load catalog presets or samples through MCP. Use returned plugin keys and opaque asset IDs, handle loading failures and inspect resulting track settings."
---

# LMMS Instruments and Assets

Read the shared [session and transaction contract](../lmms-ai/references/session-and-transactions.md) before creating tracks or loading assets.

## Choose from the live catalogs

- Call `lmms_list_instruments` with `type: "internal"` and follow `nextCursor`. Choose a returned `key` whose status permits use; installed UI plugins outside that catalog are not necessarily supported adapters.
- Inspect existing tracks before creating another one. `lmms_create_instrument_track` accepts a `pluginKey` and optional `trackName`, plus the shared mutation fields.
- Track creation always targets the song container. It does not accept a container, insertion index or initial notes. After creation retain `trackId`, `newRevision` and the returned `undoGroupId`.
- To add content, create a clip in a subsequent preview/apply plan at the new revision. Use [lmms-editor](../lmms-editor/SKILL.md) or [lmms-composition](../lmms-composition/SKILL.md).

## Load a preset or sample

1. List the appropriate catalog with `lmms_list_assets` and `rootId: "presets"` or `"samples"`. Search the returned names locally and follow pagination; there is no search or arbitrary-path argument.
2. Select a returned opaque `assetId` and a current instrument `targetTrackId`. Refresh project state before loading.
3. Call `lmms_load_asset`. Retain its new revision and undo group. Re-read the track and registered parameters afterward.

Asset catalogs are restricted to the configured user libraries. An empty catalog does not authorize scanning unrelated directories, downloading replacements or accepting a raw filesystem path as an asset ID.

Loading a sample configures an AudioFileProcessor instrument on the target track. This can replace the previous instrument; it does not insert a sample clip into an audio lane. Presets can also change instrument and track settings, so do not promise that the previous sound or parameter values remain the same.

The current preset path supports allowed internal instruments; presets with local plugin paths or unsupported instruments can be rejected. Report `PLUGIN_UNAVAILABLE`, `PLUGIN_LOAD_FAILED` or asset errors with the selected catalog item. Do not claim success or switch to an unrelated sound silently.

## Work with exposed controls

`lmms_list_parameters` exposes registered owner parameters. Current built-ins cover song tempo, instrument-track volume/pan/pitch, and mixer volume; they do not provide a general synthesizer patch editor.

Use the returned parameter IDs, ranges, steps, units and writable/automated flags. For those changes, read [lmms-mixing-automation](../lmms-mixing-automation/SKILL.md).

For complete call examples covering track creation and a preset or sample load, read [instrument and asset examples](references/instrument-asset-examples.md).
