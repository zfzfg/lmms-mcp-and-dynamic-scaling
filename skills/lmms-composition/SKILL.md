---
name: lmms-composition
description: "Compose melodies, basslines, chords, drum parts and arrangements in LMMS through MCP. Translate musical intent into tick positions, MIDI notes, existing pattern edits and verified changes while preserving the user's style, target tracks and existing material."
---

# LMMS Composition

Read the shared [session and transaction contract](../lmms-ai/references/session-and-transactions.md). Use [lmms-editor](../lmms-editor/SKILL.md) when the request involves existing note references, PatternStore or detailed clip edits.

## Turn the request into a musical target

1. Inspect the project, intended track and surrounding clips. Establish the requested span, placement, meter, style, key context and whether the user wants an addition, variation or replacement.
2. Infer usable context from existing notes and the request. When a missing choice would materially change the result, ask a focused question. An open-ended composition request otherwise permits reasonable musical choices; state them briefly.
3. Convert musical durations using the project's actual `ticksPerQuarter` and `ticksPerBar`. Keep arrangement placement separate from note positions inside the new clip.
4. Prepare a coherent phrase and preview it in bounded batches. Preserve existing material outside the requested region. Apply authorized changes and inspect the resulting clips and notes.

Do not change an existing project's tempo, sounds or arrangement merely to match an example. When composing on an existing instrument track, use its current sound unless a sound change was requested.

## Choose a realizable workflow

| Musical request | MCP approach |
| --- | --- |
| Melody or bassline on an existing track | Create a MIDI clip and add notes in one plan using `clientRef` |
| Harmony | Add simultaneous notes with independent lengths; retain the requested voicing and register |
| Variation | Duplicate a clip, then edit the returned clip with fresh references |
| Drum pattern in an existing beat setup | Inspect PatternStore MIDI clips, toggle their steps and place the existing song pattern |
| Drum parts without a PatternStore setup | If a song-track solution meets the request, create supported instrument/sample tracks and MIDI clips |
| Longer arrangement | Inspect existing sections, duplicate or place clips at calculated song ticks and verify boundaries |

For new sounds or sample-based drums, read [lmms-instruments-assets](../lmms-instruments-assets/SKILL.md). Creating an instrument track is a separate mutation; use its returned ID and new revision in the subsequent clip plan.

The current MCP cannot create a new PatternTrack or choose the PatternStore container when creating an instrument. If the user specifically requires that editor workflow and its prerequisites are absent, explain the missing capability instead of silently substituting a different arrangement.

## Apply musical detail through supported fields

- Treat `midiKey` as MIDI pitch, `volumePercent` as note volume in percent (not MIDI velocity), and `pan` as -100 through 100.
- Use regular notes for melodic MIDI content and the step command for existing step patterns. Keep positive note lengths and integer tick positions.
- Express swing or humanization through deliberately calculated positions, lengths and volumes. Respect the requested timing and keep notes within the intended phrase.
- If layering parts requires several transactions, report the actual results of each. Atomicity covers one applied plan, not the whole composition session.
- Read back note count, register, durations and clip placement. Playback or rendering can support review when requested, but do not claim to have listened unless audio was actually available and inspected.

Read [composition examples](references/composition-examples.md) for a four-bar phrase, chord and bass construction, a complete one-bar step pattern, and arrangement placement. Their pitches and style are examples, not defaults for every song.
