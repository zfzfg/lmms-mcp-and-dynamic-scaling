import { test, describe } from "node:test";
import * as assert from "node:assert";
import { TOOLS } from "../src/tools/index.js";
import { fromJsonSchema } from "@modelcontextprotocol/server";
import { CHANGE_COMMAND_SCHEMA } from "../src/contracts.js";
import { BridgeClient } from "../src/bridge-client.js";

describe("Tool Definitions and Schemas", () => {
  test("all 22 tools are defined with valid schemas", () => {
    assert.strictEqual(TOOLS.length, 22);

    const expectedTools = [
      "lmms_get_status",
      "lmms_get_project",
      "lmms_list_tracks",
      "lmms_list_clips",
      "lmms_get_notes",
      "lmms_list_parameters",
      "lmms_list_instruments",
      "lmms_get_mixer",
      "lmms_preview_changes",
      "lmms_apply_changes",
      "lmms_set_transport",
      "lmms_undo",
      "lmms_redo",
      "lmms_get_operation",
      "lmms_create_instrument_track",
      "lmms_load_asset",
      "lmms_save_project_copy",
      "lmms_start_render",
      "lmms_get_job",
      "lmms_cancel_job"
    ];

    for (const name of expectedTools) {
      const tool = TOOLS.find((t) => t.name === name);
      assert.ok(tool, `Tool ${name} should exist`);
      assert.ok(tool.description.length > 5, `Tool ${name} should have a description`);
      assert.strictEqual(tool.inputSchema.type, "object");
    }
  });

  test("tool handlers format structured content and text summary", async () => {
    const mockClient = {
      call: async (action: string, payload: any) => {
        if (action === "get_status") {
          return { ok: true, instanceId: "inst_123", ready: true };
        }
        if (action === "get_project") {
          return {
            ok: true,
            projectRevision: 5,
            data: {
              name: "Demo Project",
              tempoBpm: 128,
              timeSignature: { numerator: 4, denominator: 4 },
              modified: false
            }
          };
        }
        return { ok: true, echo: payload };
      }
    } as unknown as BridgeClient;

    const statusTool = TOOLS.find((t) => t.name === "lmms_get_status")!;
    const statusRes = await statusTool.handler(mockClient, {});
    assert.ok(statusRes.content[0].text.includes("Instance inst_123"));
    assert.strictEqual(statusRes.structuredContent?.ready, true);

    const projectTool = TOOLS.find((t) => t.name === "lmms_get_project")!;
    const projRes = await projectTool.handler(mockClient, {});
    assert.ok(projRes.content[0].text.includes("Demo Project"));
    assert.ok(projRes.content[0].text.includes("128 BPM"));
  });
  test("command schemas reject unknown commands, invalid numbers and ambiguous clip references", async () => {
    const schema = fromJsonSchema(CHANGE_COMMAND_SCHEMA);
    for (const value of [
      { kind: "update_notes", clipId: "clip", notes: [] },
      { kind: "set_tempo", bpm: 120.5 }, { kind: "execute_script", script: "anything" },
      { kind: "set_track_mute", trackId: "track", muted: "false" },
      { kind: "add_notes", clip: { clipId: "clip", clientRef: "new" }, notes: [{ midiKey: 60, positionTicks: 0, lengthTicks: 48 }] }
    ]) assert.ok((await schema["~standard"].validate(value)).issues, JSON.stringify(value));
    assert.ok(!(await schema["~standard"].validate({ kind: "set_tempo", bpm: 120 })).issues);
  });

});
