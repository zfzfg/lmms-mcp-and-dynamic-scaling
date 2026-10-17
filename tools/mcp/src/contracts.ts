// Domain contracts shared by the advertised tool catalog and exported JSON fixtures.
const string = { type: "string", minLength: 1 };
const tick = { type: "integer", minimum: 0, maximum: 2147483647 };
const note = { type: "object", required: ["positionTicks", "lengthTicks", "midiKey"], additionalProperties: false,
 properties: { noteRef: string, positionTicks: tick, lengthTicks: { ...tick, minimum: 1 }, midiKey: { type: "integer", minimum: 0, maximum: 127 },
 volumePercent: { type: "integer", minimum: 0, maximum: 200 }, pan: { type: "integer", minimum: -100, maximum: 100 }, noteType: { enum: ["regular", "step"] } } };
const refs = { type: "array", minItems: 1, maxItems: 512, uniqueItems: true, items: string };
function command(kind: string, required: string[], properties: Record<string, unknown>) {
 return { type: "object", additionalProperties: false, required: ["kind", ...required], properties: { kind: { const: kind }, ...properties } };
}
export const CHANGE_COMMAND_SCHEMA = { oneOf: [
 command("set_tempo", ["bpm"], { bpm: { type: "integer", minimum: 10, maximum: 999 } }),
 command("rename_track", ["trackId", "newName"], { trackId: string, newName: { ...string, maxLength: 256 } }),
 command("set_track_mute", ["trackId", "muted"], { trackId: string, muted: { type: "boolean" } }),
 command("set_parameter", ["parameterId", "value"], { parameterId: string, value: { type: "number" } }),
 command("create_midi_clip", ["trackId", "clientRef", "positionTicks", "lengthTicks"], { trackId: string, clientRef: string, positionTicks: tick, lengthTicks: { ...tick, minimum: 1 } }),
 command("add_notes", ["clip", "notes"], { clip: { oneOf: [
  { type: "object", required: ["clipId"], properties: { clipId: string }, additionalProperties: false },
  { type: "object", required: ["clientRef"], properties: { clientRef: string }, additionalProperties: false }
 ] }, notes: { type: "array", minItems: 1, maxItems: 512, items: note } }),
 command("update_notes", ["clipId", "notes"], { clipId: string, notes: { type: "array", minItems: 1, maxItems: 512, items: { ...note, required: [...note.required, "noteRef"] } } }),
 command("remove_notes", ["clipId", "noteRefs"], { clipId: string, noteRefs: refs }),
 command("transpose_notes", ["clipId", "semitones"], { clipId: string, noteRefs: refs, semitones: { type: "integer", minimum: -127, maximum: 127, not: { const: 0 } } }),
 command("move_clip", ["clipId", "positionTicks"], { clipId: string, positionTicks: tick }),
 command("resize_clip", ["clipId", "lengthTicks"], { clipId: string, lengthTicks: { ...tick, minimum: 1 } }),
 command("duplicate_clip", ["clipId", "clientRef", "positionTicks"], { clipId: string, clientRef: string, positionTicks: tick }),
 command("delete_clip", ["clipId"], { clipId: string }),
 command("delete_track", ["trackId"], { trackId: string }),
 command("create_mixer_channel", [], { clientRef: string }),
 command("set_mixer_send", ["fromChannelId", "toChannelId", "amount"], { fromChannelId: string, toChannelId: string, amount: { type: "number", minimum: 0, maximum: 1 } }),
 command("assign_mixer_channel", ["trackId", "channelId"], { trackId: string, channelId: string }),
 command("assign_pattern", ["patternId", "positionTicks", "lengthTicks"], { patternId: string, clientRef: string, positionTicks: tick, lengthTicks: { ...tick, minimum: 1 } }),
 command("set_pattern_steps", ["clipId", "steps"], { clipId: string, steps: { type: "array", minItems: 1, maxItems: 512, items: {
  type: "object", required: ["stepIndex", "enabled"], additionalProperties: false,
  properties: { stepIndex: { type: "integer", minimum: 0, maximum: 255 }, enabled: { type: "boolean" } }
 } } }),
 command("set_automation_points", ["parameterId", "points"], { clipId: string, clientRef: string, positionTicks: tick, lengthTicks: { ...tick, minimum: 1 }, parameterId: string, interpolation: { enum: ["linear", "discrete"] }, points: {
  type: "array", minItems: 1, maxItems: 512, items: { type: "object", required: ["positionTicks", "value"], additionalProperties: false,
   properties: { positionTicks: tick, value: { type: "number" } } }
 } })
] };
const automation = CHANGE_COMMAND_SCHEMA.oneOf.find(schema => schema.properties.kind.const === "set_automation_points")!;
Object.assign(automation, { oneOf: [
 { type: "object", required: ["clipId"], properties: { clientRef: { not: {} }, positionTicks: { not: {} }, lengthTicks: { not: {} } } },
 { type: "object", required: ["clientRef", "positionTicks", "lengthTicks"], properties: { clipId: { not: {} } } }
] });

export const SUCCESS_FIELDS: Record<string, Record<string, unknown>> = {
 get_status: { instanceId: string, ready: { type: "boolean" }, permissions: { type: "array", items: string }, capabilities: { type: "object" } },
 get_project: { projectId: string, projectRevision: tick, data: { type: "object" } },
 list_tracks: { items: { type: "array", items: { type: "object" } } }, list_clips: { items: { type: "array", items: { type: "object" } } },
 get_notes: { clipId: string, clipRevision: tick, notes: { type: "array", items: { ...note, additionalProperties: true } } },
 list_parameters: { parameters: { type: "array", items: { type: "object" } } }, list_instruments: { instruments: { type: "array", items: { type: "object" } } },
 get_automation: { clipId: string, points: { type: "array", items: { type: "object" } }, parameterIds: { type: "array", items: string }, interpolation: string },
 get_mixer: { channels: { type: "array", items: { type: "object" } } }, preview_changes: { plan: { type: "object" } },
 apply_changes: { projectId: string, projectRevision: tick, data: { type: "object" } },
 undo: { newRevision: tick }, redo: { newRevision: tick }, set_transport: { action: string, isPlaying: { type: "boolean" }, positionTicks: tick },
 create_instrument_track: { trackId: string, newRevision: tick }, load_asset: { newRevision: tick },
 save_project_copy: { savedPath: string, projectRevision: tick }, start_render: { jobId: string }, get_job: { job: { type: "object" } },
 cancel_job: { jobId: string }, get_operation: { data: { type: "object" } }, list_assets: { items: { type: "array", items: { type: "object" } } }
};
const text = { type: "string" }, number = { type: "number" }, boolean = { type: "boolean" }, integer = { type: "integer" };
const strings = { type: "array", items: text };
function dto(properties: Record<string, unknown>, required = Object.keys(properties)) {
 return { type: "object", properties, required, additionalProperties: true };
}
SUCCESS_FIELDS.get_project.data = dto({ projectId: string, projectRevision: integer, name: text, modified: boolean, tempoBpm: integer,
 timeSignature: dto({ numerator: integer, denominator: integer }), ticksPerQuarter: integer, ticksPerBar: integer,
 lengthTicks: integer, playMode: text, isPlaying: boolean, isPaused: boolean });
SUCCESS_FIELDS.list_tracks.items = { type: "array", items: dto({ trackId: string, name: text, type: text, container: text,
 muted: boolean, solo: boolean, volume: number, panning: number, clipCount: integer, mixerChannel: integer }) };
SUCCESS_FIELDS.list_clips.items = { type: "array", items: dto({ clipId: string, trackId: string, type: text, name: text,
 positionTicks: integer, lengthTicks: integer, muted: boolean, noteCount: integer }) };
SUCCESS_FIELDS.list_parameters.parameters = { type: "array", items: dto({ parameterId: string, ownerId: text, key: text, displayName: text,
 value: number, minValue: number, maxValue: number, step: number, unit: text, isAutomated: boolean, isWritable: boolean }) };
SUCCESS_FIELDS.list_instruments.instruments = { type: "array", items: dto({ key: string, name: text, isInternal: boolean, status: text }) };
SUCCESS_FIELDS.get_mixer.channels = { type: "array", items: dto({ channelId: string, channelIndex: integer, name: text, muted: boolean,
 solo: boolean, volume: number, isMaster: boolean, sends: { type: "array", items: dto({ targetChannelIndex: integer, amount: number }) } }) };
SUCCESS_FIELDS.preview_changes.plan = dto({ planId: string, projectId: string, expectedRevision: integer, planHash: string, expiresAt: string,
 summary: text, operationsCount: integer, affectedObjects: strings });
SUCCESS_FIELDS.apply_changes.data = dto({ status: { enum: ["committed", "unchanged"] }, createdObjects: { type: "object", additionalProperties: text },
 addedNotes: integer, undoGroupId: text });
SUCCESS_FIELDS.get_job.job = dto({ jobId: string, type: text, state: { enum: ["queued", "preparing_snapshot", "running", "validating", "succeeded", "failed", "cancelled"] },
 progress: number, projectRevision: integer, sha256: text, sizeBytes: integer, durationSeconds: number, sampleRate: integer, bitDepth: integer });
SUCCESS_FIELDS.list_assets.items = { type: "array", items: dto({ assetId: string, name: text, rootId: text, type: { enum: ["sample", "preset"] } }) };
SUCCESS_FIELDS.get_automation.points = { type: "array", items: dto({ positionTicks: integer, value: number, outValue: number }) };

export function outputSchema(action: string): Record<string, unknown> {
 const properties = SUCCESS_FIELDS[action] ?? {};
 return { type: "object", properties: { ok: { type: "boolean" }, ...properties, error: {
  type: "object", required: ["code", "message"], properties: { code: string, message: { type: "string" }, details: { type: "object" } }
 } }, required: ["ok"], additionalProperties: true,
 allOf: [{ if: { properties: { ok: { const: true } } }, then: { required: Object.keys(properties) }, else: { required: ["error"] } }] };
}
