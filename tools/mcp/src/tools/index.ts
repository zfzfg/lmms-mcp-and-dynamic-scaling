import { CHANGE_COMMAND_SCHEMA, outputSchema } from "../contracts.js";
import { BridgeClient } from "../bridge-client.js";

export interface ToolDefinition {
  name: string;
  description: string;
  inputSchema: {
    type: "object";
    properties: Record<string, unknown>;
    required?: string[];
    additionalProperties?: boolean;
  };
  outputSchema?: Record<string, unknown>;
  annotations?: { readOnlyHint: boolean; destructiveHint: boolean; idempotentHint: boolean; openWorldHint: boolean };
  handler: (client: BridgeClient, args: any) => Promise<{
    content: Array<{ type: "text"; text: string }>;
    structuredContent?: Record<string, unknown>;
    isError?: boolean;
  }>;
}

export const TOOLS: ToolDefinition[] = [
  {
    name: "lmms_get_status",
    description: "Returns connection status, LMMS readiness, bridge version, permissions and available features.",
    inputSchema: {
      type: "object",
      properties: {}
    },
    handler: async (client) => {
      const res = await client.call("get_status");
      return {
        content: [{ type: "text", text: `LMMS Status: Instance ${res.instanceId}, Ready: ${res.ready}` }],
        structuredContent: res
      };
    }
  },
  {
    name: "lmms_get_project",
    description: "Returns compact project summary including tempo, time signature, tick resolution, revision and state.",
    inputSchema: {
      type: "object",
      properties: {
        projectId: { type: "string", description: "Optional expected project ID" }
      }
    },
    handler: async (client, args) => {
      const res = await client.call("get_project", args);
      const data = res.data || {};
      return {
        content: [{
          type: "text",
          text: `Project '${data.name}' (Rev ${res.projectRevision}): ${data.tempoBpm} BPM, ${data.timeSignature?.numerator}/${data.timeSignature?.denominator}, Modified: ${data.modified}`
        }],
        structuredContent: res
      };
    }
  },
  {
    name: "lmms_list_tracks",
    description: "Lists tracks in the song or pattern store with pagination.",
    inputSchema: {
      type: "object",
      properties: {
        container: { type: "string", enum: ["song", "pattern_store"], description: "Track container (default: song)" },
        cursor: { type: "string", description: "Pagination cursor" },
        limit: { type: "integer", minimum: 1, maximum: 500, default: 100 }
      }
    },
    handler: async (client, args) => {
      const res = await client.call("list_tracks", args);
      const items = res.items || [];
      return {
        content: [{ type: "text", text: `Found ${items.length} tracks.` }],
        structuredContent: res
      };
    }
  },
  {
    name: "lmms_list_clips",
    description: "Lists clips in a specific track with optional tick range filtering and pagination.",
    inputSchema: {
      type: "object",
      required: ["trackId"],
      properties: {
        trackId: { type: "string", description: "Opaque track handle" },
        startTick: { type: "integer", minimum: 0 },
        endTick: { type: "integer", minimum: 0 },
        cursor: { type: "string" },
        limit: { type: "integer", minimum: 1, maximum: 500, default: 100 }
      }
    },
    handler: async (client, args) => {
      const res = await client.call("list_clips", args);
      const items = res.items || [];
      return {
        content: [{ type: "text", text: `Found ${items.length} clips for track ${args.trackId}.` }],
        structuredContent: res
      };
    }
  },
  {
    name: "lmms_get_notes",
    description: "Reads notes from a MidiClip including clipRevision and short-lived note references.",
    inputSchema: {
      type: "object",
      required: ["clipId"],
      properties: {
        clipId: { type: "string", description: "Opaque clip handle" },
        startTick: { type: "integer", minimum: 0 },
        endTick: { type: "integer", minimum: 0 },
        cursor: { type: "string" },
        limit: { type: "integer", minimum: 1, maximum: 500, default: 100 }
      }
    },
    handler: async (client, args) => {
      const res = await client.call("get_notes", args);
      const notes = res.notes || [];
      return {
        content: [{ type: "text", text: `Retrieved ${notes.length} notes (ClipRev: ${res.clipRevision}).` }],
        structuredContent: res
      };
    }
  },
  {
    name: "lmms_list_parameters",
    description: "Lists registered automatable parameters for an owner object.",
    inputSchema: {
      type: "object",
      required: ["ownerId"],
      properties: {
        ownerId: { type: "string", description: "Owner ID (e.g. 'song' or track handle)" },
        cursor: { type: "string" },
        limit: { type: "integer", minimum: 1, maximum: 500, default: 100 }
      }
    },
    handler: async (client, args) => {
      const res = await client.call("list_parameters", args);
      return {
        content: [{ type: "text", text: `Found ${res.parameters?.length || 0} parameters.` }],
        structuredContent: res
      };
    }
  },
  {
    name: "lmms_list_instruments",
    description: "Lists available internal synthesizer plugin descriptors.",
    inputSchema: {
      type: "object",
      properties: {
        type: { type: "string" },
        cursor: { type: "string" },
        limit: { type: "integer", minimum: 1, maximum: 500, default: 100 }
      }
    },
    handler: async (client, args) => {
      const res = await client.call("list_instruments", args);
      return {
        content: [{ type: "text", text: `Available instruments: ${(res.instruments || []).map((i: any) => i.key).join(", ")}` }],
        structuredContent: res
      };
    }
  },
  {
    name: "lmms_get_mixer",
    description: "Retrieves mixer channels, volume, mute, solo and send routing graph.",
    inputSchema: {
      type: "object",
      properties: {
        channelIndex: { type: "integer", minimum: 0, description: "Optional specific channel index" }
      }
    },
    handler: async (client, args) => {
      const res = await client.call("get_mixer", args);
      return {
        content: [{ type: "text", text: `Mixer has ${(res.channels || []).length} channels.` }],
        structuredContent: res
      };
    }
  },
  {
    name: "lmms_preview_changes",
    description: "Validates a structured batch of music changes and creates an atomic bound plan without mutating the project.",
    inputSchema: {
      type: "object",
      required: ["projectId", "expectedRevision", "changes"],
      properties: {
        projectId: { type: "string" },
        expectedRevision: { type: "integer", minimum: 0 },
        changes: {
          type: "array",
          items: { type: "object" },
          description: "List of change commands (set_tempo, create_midi_clip, add_notes, transpose_notes, etc.)"
        }
      }
    },
    handler: async (client, args) => {
      const res = await client.call("preview_changes", args);
      const plan = res.plan || {};
      return {
        content: [{
          type: "text",
          text: `Plan created: ${plan.planId} (Hash: ${plan.planHash?.substring(0, 12)}...). Summary: ${plan.summary}. Expires at: ${plan.expiresAt}`
        }],
        structuredContent: res
      };
    }
  },
  {
    name: "lmms_apply_changes",
    description: "Executes an approved plan atomically with an idempotent operationKey, creating a single journal undo group.",
    inputSchema: {
      type: "object",
      required: ["projectId", "expectedRevision", "planId", "operationKey"],
      properties: {
        projectId: { type: "string" },
        expectedRevision: { type: "integer", minimum: 0 },
        planId: { type: "string" },
        operationKey: { type: "string", description: "Unique client repetition key for idempotency" }
      }
    },
    handler: async (client, args) => {
      const res = await client.call("apply_changes", args, args.operationKey);
      const data = res.data || {};
      return {
        content: [{
          type: "text",
          text: `Plan applied successfully! New Revision: ${res.projectRevision}, UndoGroupId: ${data.undoGroupId}, Added Notes: ${data.addedNotes}`
        }],
        structuredContent: res
      };
    }
  },
  {
    name: "lmms_set_transport",
    description: "Controls song playback transport explicitly (play, pause, stop, seek).",
    inputSchema: {
      type: "object",
      required: ["action"],
      properties: {
        action: { type: "string", enum: ["play", "pause", "stop", "seek"] },
        tickPosition: { type: "integer", minimum: 0 }
      }
    },
    handler: async (client, args) => {
      const res = await client.call("set_transport", args);
      return {
        content: [{ type: "text", text: `Transport: ${res.action}. Playing: ${res.isPlaying}, Pos: ${res.positionTicks} ticks.` }],
        structuredContent: res
      };
    }
  },
  {
    name: "lmms_undo",
    description: "Reverts the last journal group atomically.",
    inputSchema: {
      type: "object",
      required: ["expectedRevision"],
      properties: {
        expectedRevision: { type: "integer", minimum: 0 },
        undoGroupId: { type: "string" }
      }
    },
    handler: async (client, args) => {
      const res = await client.call("undo", args);
      return {
        content: [{ type: "text", text: `Undo successful. New revision: ${res.newRevision}` }],
        structuredContent: res
      };
    }
  },
  {
    name: "lmms_redo",
    description: "Reapplies the last undone journal group.",
    inputSchema: {
      type: "object",
      required: ["expectedRevision"],
      properties: {
        expectedRevision: { type: "integer", minimum: 0 },
        undoGroupId: { type: "string" }
      }
    },
    handler: async (client, args) => {
      const res = await client.call("redo", args);
      return {
        content: [{ type: "text", text: `Redo successful. New revision: ${res.newRevision}` }],
        structuredContent: res
      };
    }
  },
  {
    name: "lmms_get_operation",
    description: "Checks status and cached result of a previously executed operationKey.",
    inputSchema: {
      type: "object",
      required: ["operationKey"],
      properties: {
        operationKey: { type: "string" }
      }
    },
    handler: async (client, args) => {
      const res = await client.call("get_operation", args);
      return {
        content: [{ type: "text", text: `Operation ${args.operationKey} found.` }],
        structuredContent: res
      };
    }
  },
  {
    name: "lmms_create_instrument_track",
    description: "Creates a new track with an internal synthesizer plugin.",
    inputSchema: {
      type: "object",
      required: ["pluginKey"],
      properties: {
        pluginKey: { type: "string", description: "Internal plugin key (e.g. 'tripleoscillator')" },
        trackName: { type: "string", description: "Optional track display name" }
      }
    },
    handler: async (client, args) => {
      const res = await client.call("create_instrument_track", args);
      return {
        content: [{ type: "text", text: `Instrument track created: ${res.trackId} (Rev ${res.newRevision})` }],
        structuredContent: res
      };
    }
  },
  {
    name: "lmms_load_asset",
    description: "Loads a preset or sample from allowed library directories onto a target track.",
    inputSchema: {
      type: "object",
      required: ["assetId", "targetTrackId"],
      properties: {
        assetId: { type: "string", description: "Opaque assetId returned by lmms_list_assets" },
        targetTrackId: { type: "string", description: "Target track handle" }
      }
    },
    handler: async (client, args) => {
      const res = await client.call("load_asset", args);
      return {
        content: [{ type: "text", text: `Asset loaded successfully onto track ${args.targetTrackId}.` }],
        structuredContent: res
      };
    }
  },
  {
    name: "lmms_save_project_copy",
    description: "Saves a copy of the current project state into the allowed export directory.",
    inputSchema: {
      type: "object",
      required: ["fileName"],
      properties: {
        fileName: { type: "string", description: "Target filename in exports folder (e.g. 'backup.mmp')" }
      }
    },
    handler: async (client, args) => {
      const res = await client.call("save_project_copy", args);
      return {
        content: [{ type: "text", text: `Project copy saved to: ${res.savedPath}` }],
        structuredContent: res
      };
    }
  },
  {
    name: "lmms_start_render",
    description: "Starts an asynchronous background render job of a snapshot of the current project.",
    inputSchema: {
      type: "object",
      required: ["outputFileName"],
      properties: {
        outputFileName: { type: "string", description: "Filename in export directory (e.g. 'song.wav')" },
        format: { type: "string", enum: ["wav"], default: "wav" },
        sampleRate: { type: "integer", minimum: 8000, maximum: 192000, default: 44100 },
        bitDepth: { type: "integer", enum: [16, 24, 32], default: 16 }
      }
    },
    handler: async (client, args) => {
      const res = await client.call("start_render", args);
      return {
        content: [{ type: "text", text: `Render job started: ${res.jobId}. Poll with lmms_get_job.` }],
        structuredContent: res
      };
    }
  },
  {
    name: "lmms_get_job",
    description: "Checks progress, state, errors, and verified audio artifact of an asynchronous job.",
    inputSchema: {
      type: "object",
      required: ["jobId"],
      properties: {
        jobId: { type: "string" }
      }
    },
    handler: async (client, args) => {
      const res = await client.call("get_job", args);
      const job = res.job || {};
      return {
        content: [{
          type: "text",
          text: `Job ${job.jobId}: State=${job.state}, Progress=${job.progress}%, Artifact=${job.artifactId || "none"}`
        }],
        structuredContent: res
      };
    }
  },
  {
    name: "lmms_cancel_job",
    description: "Requests cancellation of an active asynchronous job.",
    inputSchema: {
      type: "object",
      required: ["jobId"],
      properties: {
        jobId: { type: "string" }
      }
    },
    handler: async (client, args) => {
      const res = await client.call("cancel_job", args);
      return {
        content: [{ type: "text", text: `Job ${args.jobId} cancelled.` }],
        structuredContent: res
      };
    }
  }
];

TOOLS.push({ name: "lmms_list_assets", description: "List samples or presets in a permitted catalog. Use returned opaque assetId to load.",
 inputSchema: { type: "object", required: ["rootId"], properties: { rootId: { enum: ["samples", "presets"] }, cursor: { type: "string" }, limit: { type: "integer", minimum: 1, maximum: 500 } } },
 handler: async (client, args) => { const result = await client.call("list_assets", args); return { content: [{ type: "text", text: `${result.items.length} catalog assets` }], structuredContent: result }; }
});

TOOLS.push({ name: "lmms_get_automation", description: "Read automation points, interpolation and target parameters with revision-bound pagination.",
 inputSchema: { type: "object", required: ["clipId"], properties: { clipId: { type: "string" }, cursor: { type: "string" }, limit: { type: "integer", minimum: 1, maximum: 500 } } },
 handler: async (client, args) => { const result = await client.call("get_automation", args); return { content: [{ type: "text", text: `${result.points.length} automation points` }], structuredContent: result }; }
});

const writes = new Set(["apply_changes", "set_transport", "undo", "redo", "create_instrument_track", "load_asset", "save_project_copy", "start_render", "cancel_job"]);
for (const tool of TOOLS) {
 const action = tool.name.replace(/^lmms_/, "");
 tool.inputSchema.additionalProperties = false;
 tool.annotations = { readOnlyHint: !writes.has(action), destructiveHint: ["apply_changes", "undo", "redo", "load_asset"].includes(action), idempotentHint: true, openWorldHint: false };
 tool.outputSchema = outputSchema(action);
 if (writes.has(action)) {
  tool.inputSchema.properties.operationKey = { type: "string", minLength: 1, maxLength: 128 };
  tool.inputSchema.required = [...new Set([...(tool.inputSchema.required ?? []), "operationKey"])];
  if (action !== "cancel_job") {
   tool.inputSchema.properties.projectId = { type: "string", minLength: 1 };
   tool.inputSchema.properties.expectedRevision = { type: "integer", minimum: 1, maximum: Number.MAX_SAFE_INTEGER };
   tool.inputSchema.required = [...new Set([...tool.inputSchema.required, "projectId", "expectedRevision"])];
  }
 }
 if (action === "undo" || action === "redo") { tool.inputSchema.required!.push("undoGroupId"); }
 if (action === "preview_changes") { Object.assign(tool.inputSchema.properties.changes as object, { minItems: 1, maxItems: 64, items: CHANGE_COMMAND_SCHEMA }); }
}
