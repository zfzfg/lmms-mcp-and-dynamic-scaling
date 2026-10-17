# Composition examples

Use these recipes only when they match the user's task. Each scenario stipulates illustrative values from preceding live reads; replace IDs and revisions with actual responses. The example key and rhythm are not universal musical defaults.

## Four-bar phrase on an existing track

Assume the user requested a new four-bar melody at the start of bar 5, on `trk_lead_demo`. Fresh project data gives:

| Field | Example value |
| --- | --- |
| Project | `proj_demo`, revision 100 |
| Meter | 4/4 |
| `ticksPerQuarter` | 48 |
| `ticksPerBar` | 192 |

Bar 5 starts at `(5 - 1) * 192 = 768` song ticks. Four bars span 768 ticks. The following C-major phrase uses four quarter-note onsets per bar, with a short gap between notes:

```json
{
  "tool":"lmms_preview_changes",
  "arguments":{
    "projectId":"proj_demo",
    "expectedRevision":100,
    "changes":[
      {"kind":"create_midi_clip","trackId":"trk_lead_demo","clientRef":"phrase","positionTicks":768,"lengthTicks":768},
      {
        "kind":"add_notes",
        "clip":{"clientRef":"phrase"},
        "notes":[
          {"midiKey":60,"positionTicks":0,"lengthTicks":42,"volumePercent":90},
          {"midiKey":64,"positionTicks":48,"lengthTicks":42,"volumePercent":84},
          {"midiKey":67,"positionTicks":96,"lengthTicks":42,"volumePercent":92},
          {"midiKey":64,"positionTicks":144,"lengthTicks":42,"volumePercent":82},
          {"midiKey":62,"positionTicks":192,"lengthTicks":42,"volumePercent":88},
          {"midiKey":65,"positionTicks":240,"lengthTicks":42,"volumePercent":83},
          {"midiKey":69,"positionTicks":288,"lengthTicks":42,"volumePercent":91},
          {"midiKey":65,"positionTicks":336,"lengthTicks":42,"volumePercent":82},
          {"midiKey":64,"positionTicks":384,"lengthTicks":42,"volumePercent":89},
          {"midiKey":67,"positionTicks":432,"lengthTicks":42,"volumePercent":84},
          {"midiKey":71,"positionTicks":480,"lengthTicks":42,"volumePercent":92},
          {"midiKey":67,"positionTicks":528,"lengthTicks":42,"volumePercent":82},
          {"midiKey":62,"positionTicks":576,"lengthTicks":42,"volumePercent":88},
          {"midiKey":67,"positionTicks":624,"lengthTicks":42,"volumePercent":86},
          {"midiKey":64,"positionTicks":672,"lengthTicks":42,"volumePercent":84},
          {"midiKey":60,"positionTicks":720,"lengthTicks":48,"volumePercent":94}
        ]
      }
    ]
  }
}
```

Assume preview returns `plan_phrase_demo`, bound to the same revision, with the expected target and two commands:

```json
{
  "tool":"lmms_apply_changes",
  "arguments":{
    "projectId":"proj_demo",
    "expectedRevision":100,
    "planId":"plan_phrase_demo",
    "operationKey":"compose-phrase-demo-001"
  }
}
```

On success use the actual `projectRevision` and `data.createdObjects.phrase`. If that returned handle is `clip_phrase_demo`:

```json
{"tool":"lmms_get_notes","arguments":{"clipId":"clip_phrase_demo","limit":100}}
```

Check for 16 notes, local starts 0 through 720, final end at 768, and unchanged note volumes where no further change was intended. Read the track's clips too: the new clip must start at song tick 768 and span 768 ticks. One-based bar 5 did not become the notes' local origin.

For 3/4 at the same quarter resolution, a bar is 144 ticks and four bars are 576 ticks. Recalculate the phrase for that meter instead of copying these positions unchanged.

## Chords and bass

Suppose the user requested a one-bar C-major chord at bar 1 on an existing harmony track. At a fresh revision 110, using the same 4/4 resolution:

```json
{
  "tool":"lmms_preview_changes",
  "arguments":{
    "projectId":"proj_demo",
    "expectedRevision":110,
    "changes":[
      {"kind":"create_midi_clip","trackId":"trk_harmony_demo","clientRef":"chord","positionTicks":0,"lengthTicks":192},
      {"kind":"add_notes","clip":{"clientRef":"chord"},"notes":[
        {"midiKey":60,"positionTicks":0,"lengthTicks":192,"volumePercent":76},
        {"midiKey":64,"positionTicks":0,"lengthTicks":192,"volumePercent":72},
        {"midiKey":67,"positionTicks":0,"lengthTicks":192,"volumePercent":74}
      ]}
    ]
  }
}
```

A bass accompaniment can use a separate discovered instrument track and a new clip with local onsets 0 and 96, length 90 each, and a suitable root such as MIDI 36 for this example. Use the user's harmony and register, not a blind octave shift of every chord tone.

When modifying an existing progression, read its note references and preserve notes outside the requested chord or span. The interface has no chord-name command; construct the explicit pitches and durations.

## A complete one-bar step pattern in an existing setup

First list song tracks and identify the intended `type: "pattern"` entry, including its `patternId` and zero-based `patternIndex`:

```json
{"tool":"lmms_list_tracks","arguments":{"container":"song","limit":100}}
```

Discover the existing drum instrument in PatternStore:

```json
{"tool":"lmms_list_tracks","arguments":{"container":"pattern_store","limit":100}}
```

```json
{"tool":"lmms_list_clips","arguments":{"trackId":"trk_kick_store_demo","limit":100}}
```

Inspect its MIDI clips/notes and match the intended pattern slot. In the normal store layout, the clip for song `patternIndex: i` starts at `i * ticksPerBar` in PatternStore; verify its returned position and content. For example, slot 0 starts at 0 and slot 2 at 384 in this 4/4 scenario. These positions do not place anything in the song arrangement.

At 4/4 with 48 ticks per quarter, one bar has 16 sixteenth-note steps spaced 12 ticks apart. The current step command uses zero-based indices.

Assume the user wants a complete one-bar four-on-the-floor kick pattern on `clip_kick_store_demo`, and fresh state is revision 120:

```json
{
  "tool":"lmms_preview_changes",
  "arguments":{
    "projectId":"proj_demo",
    "expectedRevision":120,
    "changes":[
      {"kind":"set_pattern_steps","clipId":"clip_kick_store_demo","steps":[
        {"stepIndex":0,"enabled":true},
        {"stepIndex":1,"enabled":false},
        {"stepIndex":2,"enabled":false},
        {"stepIndex":3,"enabled":false},
        {"stepIndex":4,"enabled":true},
        {"stepIndex":5,"enabled":false},
        {"stepIndex":6,"enabled":false},
        {"stepIndex":7,"enabled":false},
        {"stepIndex":8,"enabled":true},
        {"stepIndex":9,"enabled":false},
        {"stepIndex":10,"enabled":false},
        {"stepIndex":11,"enabled":false},
        {"stepIndex":12,"enabled":true},
        {"stepIndex":13,"enabled":false},
        {"stepIndex":14,"enabled":false},
        {"stepIndex":15,"enabled":false}
      ]}
    ]
  }
}
```

This supplies explicit disabled steps because a complete replacement was requested. For a request to add one hit, send only that step and preserve unmentioned steps. Inspect longer patterns before claiming the first bar's command cleared their later hits.

After applying, read the song tracks. Find the `type: "pattern"` entry and returned `patternId` corresponding to that pattern slot; do not use the kick instrument handle.

## Place an existing song pattern

Assume a fresh read after the previous edit gives revision 121 and the desired song pattern ID is `trk_pattern_demo`. To place one bar at bar 5:

```json
{
  "tool":"lmms_preview_changes",
  "arguments":{
    "projectId":"proj_demo",
    "expectedRevision":121,
    "changes":[
      {"kind":"assign_pattern","patternId":"trk_pattern_demo","clientRef":"patternPlacement","positionTicks":768,"lengthTicks":192}
    ]
  }
}
```

Apply its returned plan, retain `data.createdObjects.patternPlacement`, and inspect the song track's clip layout. PatternStore sound content and song arrangement placement are different operations.

To arrange longer sections, calculate each destination from the current meter and inspect overlapping clips before duplicating or assigning. Only use the pattern recipe when its existing setup satisfies the requested task.
