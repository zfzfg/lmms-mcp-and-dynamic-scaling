# Instrument and asset examples

These recipes assume a connected, ready instance and a stopped song. Use actual catalog values and fresh bindings; do not execute every example as a sequence.

## Create a supported instrument track

```json
{"tool":"lmms_list_instruments","arguments":{"type":"internal","limit":100}}
```

Follow any `nextCursor`. Assume the response includes the permitted key `tripleoscillator` and current project data reports `proj_demo` at revision 200:

```json
{
  "tool":"lmms_create_instrument_track",
  "arguments":{
    "projectId":"proj_demo",
    "expectedRevision":200,
    "operationKey":"instrument-create-demo-001",
    "pluginKey":"tripleoscillator",
    "trackName":"Lead"
  }
}
```

Assume the response returns `trackId: trk_new_demo`, `newRevision: 201` and an `undoGroupId`. Retain all three and verify the track in the song container:

```json
{"tool":"lmms_list_tracks","arguments":{"container":"song","limit":100}}
```

Use that returned track ID in a subsequent clip plan. `create_midi_clip` does not create a track, and `create_instrument_track` is not a change command or a PatternStore-creation tool.

## Select and load a preset

```json
{"tool":"lmms_list_assets","arguments":{"rootId":"presets","limit":100}}
```

Find a suitable returned preset by name and follow pages as necessary. Assume the selected entry has `assetId: asset_preset_demo`. After refreshing project state, if it is still revision 201:

```json
{
  "tool":"lmms_load_asset",
  "arguments":{
    "projectId":"proj_demo",
    "expectedRevision":201,
    "operationKey":"asset-preset-demo-001",
    "assetId":"asset_preset_demo",
    "targetTrackId":"trk_new_demo"
  }
}
```

Retain `newRevision` and the top-level `undoGroupId`. Re-read the track's instrument key and parameters:

```json
{"tool":"lmms_list_parameters","arguments":{"ownerId":"trk_new_demo","limit":100}}
```

If loading fails, inspect the error and re-read the track before another attempt. A tool's human-readable success-style message is not a substitute for `ok` and the structured result.

## Alternative: configure a sample instrument

In an independent scenario the user requested a sample on a discovered instrument track `trk_sample_demo`:

```json
{"tool":"lmms_list_assets","arguments":{"rootId":"samples","limit":100}}
```

Suppose the selected returned sample ID is `asset_sample_demo`, and a fresh project read reports revision 210:

```json
{
  "tool":"lmms_load_asset",
  "arguments":{
    "projectId":"proj_demo",
    "expectedRevision":210,
    "operationKey":"asset-sample-demo-001",
    "assetId":"asset_sample_demo",
    "targetTrackId":"trk_sample_demo"
  }
}
```

This configures AudioFileProcessor with the catalog sample. It can replace the existing instrument. The asset ID is not the sample pathname, and this call does not place an audio clip on the song timeline.

Re-read track settings before adding notes. For a pitched sample or drum, choose note register based on the instrument and requested result; do not promise that arbitrary note 60 reproduces the sample at its original tuning.
