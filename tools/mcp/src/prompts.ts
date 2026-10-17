export interface PromptDefinition {
  name: string;
  description: string;
  arguments?: Array<{
    name: string;
    description: string;
    required?: boolean;
  }>;
  messages: (args: Record<string, string>) => Array<{
    role: "user" | "assistant";
    content: {
      type: "text";
      text: string;
    };
  }>;
}

export const PROMPTS: PromptDefinition[] = [
  {
    name: "analyze_project",
    description: "Inspect the current LMMS project, track arrangement, instruments and mixer routing.",
    messages: () => [
      {
        role: "user",
        content: {
          type: "text",
          text: "Please inspect my current LMMS project using `lmms_get_project`, `lmms_list_tracks`, and `lmms_get_mixer`. Explain the structure, tempo, key instruments, and clip layout."
        }
      }
    ]
  },
  {
    name: "compose_midi_clip",
    description: "Compose a new melodic or bassline MIDI clip on an existing instrument track.",
    arguments: [
      { name: "trackId", description: "Target track handle", required: true },
      { name: "bars", description: "Number of bars to compose (default: 4)", required: false },
      { name: "style", description: "Musical style or genre description", required: false }
    ],
    messages: (args) => [
      {
        role: "user",
        content: {
          type: "text",
          text: `Please compose a ${args.bars || "4"}-bar clip for track '${args.trackId}' in style '${args.style || "melodic"}'. First read project status with lmms_get_project, plan the notes with lmms_preview_changes, review the diff with me, and apply it with lmms_apply_changes.`
        }
      }
    ]
  },
  {
    name: "review_mix",
    description: "Audit mixer levels, mute/solo flags and send loops.",
    messages: () => [
      {
        role: "user",
        content: {
          type: "text",
          text: "Please review the LMMS mixer using `lmms_get_mixer`. Check for potential clipping, high send amounts, muted tracks, and verify channel routing."
        }
      }
    ]
  }
];
