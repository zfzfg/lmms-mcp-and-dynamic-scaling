---
name: lmms-mixing-automation
description: "Inspect and change LMMS mixer routing, track assignments, registered parameters and automation through MCP. Use for level adjustments, new mixer channels, sends and linear or discrete automation while respecting parameter ownership and replacement semantics."
---

# LMMS Mixing and Automation

Read the shared [session and transaction contract](../lmms-ai/references/session-and-transactions.md) before making changes.

## Inspect the signal path and units

- Use `lmms_get_mixer` for channel handles, display indices, master status and sends. Use `lmms_list_tracks` to match each instrument's mixer index to a returned channel.
- Pass a returned channel or track ID to `lmms_list_parameters.ownerId`. For tempo use owner `song`.
- Respect `unit`, `minValue`, `maxValue`, `step`, `isWritable` and `isAutomated`. Track volume percent and mixer gain are not interchangeable.
- A readable mute/solo field does not imply a matching mutation. The current interface exposes track mute, registered volume parameters and routing; it does not expose general mixer mute/solo setters, channel deletion or effect-chain editing.
- Static levels and send amounts can identify routing or gain concerns, but do not prove clipping, loudness, frequency balance or audible quality.

## Route and adjust levels

Use preview/apply with `create_mixer_channel`, `assign_mixer_channel`, `set_mixer_send`, `set_parameter` or `set_track_mute`.

New channels produce a handle in `data.createdObjects[clientRef]`. Commit creation first, then obtain current mixer state and build a second plan for assignments or sends: those commands require real channel handles, not a clientRef.

Send amounts range from 0 to 1. Self-routing and cycles are rejected. A zero-amount send is still a routing edge in native graph validation; do not treat it as a supported delete-send command.

`set_parameter` cannot overwrite a parameter controlled by automation or a controller. Inspect the controlling automation rather than repeatedly retrying the same value.

## Create or replace automation deliberately

Read existing clips with `lmms_get_automation`, following all pages. Retain positions, values, interpolation and returned parameter IDs before designing a replacement.

`set_automation_points` has two exclusive forms:

- Existing clip: provide `clipId`, `parameterId`, `points` and the intended `interpolation`.
- New clip: provide `clientRef`, `positionTicks`, `lengthTicks`, `parameterId`, `points` and the intended `interpolation`.

The command replaces the entire point map and all target bindings with the supplied points and one parameter. It is not a point patch. The create form also creates a song automation track and returns both clip and track handles.

Point positions are relative to the automation clip. Values use the target parameter's native units and bounds. Write interpolation is limited to `linear` and `discrete`; an existing clip can read back as `cubic`.

The write format cannot preserve multiple target bindings, cubic interpolation, or distinct incoming/outgoing point values. If the requested edit requires preserving such data, explain the limitation before making a lossy change. Do not assume the returned registered parameter IDs enumerate every possible native binding.

For concrete level, routing and automation examples, read [mixing examples](references/mixing-examples.md). Use [lmms-troubleshooting](../lmms-troubleshooting/SKILL.md) for controlled parameters and stale handles.
