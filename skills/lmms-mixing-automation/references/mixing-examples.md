# Mixing and automation examples

All identifiers below stand for values obtained from preceding live reads. Recipes are independent unless explicitly chained. Apply each returned plan using the shared transaction procedure, then inspect the changed state.

## Inspect channels and parameter units

```json
{"tool":"lmms_get_mixer","arguments":{}}
```

Match the intended track's `mixerChannel` display index to a returned `channelId`. Do not put a numeric display index in `assign_mixer_channel.channelId`.

```json
{"tool":"lmms_list_parameters","arguments":{"ownerId":"trk_lead_demo","limit":100}}
```

Suppose the returned volume descriptor identifies `prm_track_volume_demo` with unit `%`, bounds 0 through 200 and writable, uncontrolled state. To set it to 75 percent at fresh revision 250:

```json
{
  "tool":"lmms_preview_changes",
  "arguments":{
    "projectId":"proj_demo",
    "expectedRevision":250,
    "changes":[{"kind":"set_parameter","parameterId":"prm_track_volume_demo","value":75}]
  }
}
```

For a mixer-channel level, first list that channel's parameters and use its returned gain units. Do not assume a normalized gain parameter accepts the same value 75.

## Create a bus, then assign and route

This separate scenario begins at fresh revision 260:

```json
{
  "tool":"lmms_preview_changes",
  "arguments":{
    "projectId":"proj_demo",
    "expectedRevision":260,
    "changes":[{"kind":"create_mixer_channel","clientRef":"bus"}]
  }
}
```

After applying, retain `data.createdObjects.bus`. Assume that real handle is `mix_bus_demo` and a fresh read gives revision 261. Read the mixer again and identify its master handle, here `mix_master_demo`.

```json
{
  "tool":"lmms_preview_changes",
  "arguments":{
    "projectId":"proj_demo",
    "expectedRevision":261,
    "changes":[
      {"kind":"assign_mixer_channel","trackId":"trk_lead_demo","channelId":"mix_bus_demo"},
      {"kind":"set_mixer_send","fromChannelId":"mix_bus_demo","toChannelId":"mix_master_demo","amount":0.8}
    ]
  }
}
```

The local name `bus` cannot replace either channel handle in this second plan. A newly created channel normally has a master route; inspect the actual graph before proposing further sends.

Read the resulting mixer and track assignment. A send-cycle error requires a realizable routing change, not a zero-amount edge presented as deletion.

## Create volume automation

Assume a fresh descriptor for `prm_track_volume_demo` confirms percent units, values 60 through 100 are in range, and no controller prevents the requested automation. Project revision is 270, meter is 4/4, and there are 192 ticks per bar.

The user requested a one-bar rise at bar 5:

```json
{
  "tool":"lmms_preview_changes",
  "arguments":{
    "projectId":"proj_demo",
    "expectedRevision":270,
    "changes":[
      {
        "kind":"set_automation_points",
        "clientRef":"fade",
        "positionTicks":768,
        "lengthTicks":192,
        "parameterId":"prm_track_volume_demo",
        "interpolation":"linear",
        "points":[
          {"positionTicks":0,"value":60},
          {"positionTicks":96,"value":80},
          {"positionTicks":191,"value":100}
        ]
      }
    ]
  }
}
```

This creates an automation track and clip. After apply, obtain the clip from `data.createdObjects.fade` and its track from `data.createdObjects["fade.track"]`. The song start is 768; the point starts remain local 0, 96 and 191.

## Replace an existing automation map

Suppose the created clip's real ID is `clip_fade_demo`:

```json
{"tool":"lmms_get_automation","arguments":{"clipId":"clip_fade_demo","limit":100}}
```

Read all pages. If it has one intended registered target, linear interpolation and equal incoming/outgoing values, a requested midpoint adjustment can preserve endpoints by sending the complete new map. At fresh revision 271:

```json
{
  "tool":"lmms_preview_changes",
  "arguments":{
    "projectId":"proj_demo",
    "expectedRevision":271,
    "changes":[
      {
        "kind":"set_automation_points",
        "clipId":"clip_fade_demo",
        "parameterId":"prm_track_volume_demo",
        "interpolation":"linear",
        "points":[
          {"positionTicks":0,"value":60},
          {"positionTicks":96,"value":75},
          {"positionTicks":191,"value":100}
        ]
      }
    ]
  }
}
```

Sending only the midpoint would remove the endpoints. Existing-clip arguments must not include the create form's `clientRef`, `positionTicks` or `lengthTicks`.

If the read reveals cubic interpolation, multiple/native unexposed targets, or `outValue` different from `value`, the write shape cannot preserve that structure. Explain that constraint for a preservation request; use this replacement recipe only when its single-target linear/discrete result is intended.
