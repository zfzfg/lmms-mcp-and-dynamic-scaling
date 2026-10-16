import { test, describe } from "node:test";
import * as assert from "node:assert";
import { BridgeClient } from "../src/bridge-client.js";
import { LmmsMcpServer } from "../src/server.js";
import { TOOLS } from "../src/tools/index.js";

describe("MCP Server Endpoints and Protocol", () => {
  test("tools, resources and prompts handlers execute properly", async () => {
    const mockClient = {
      instanceId: "inst_test_server",
      call: async (action: string, payload: any) => {
        if (action === "get_status") {
          return { ok: true, instanceId: "inst_test_server", ready: true, version: "1.3.0-alpha" };
        }
        return { ok: true, action, payload };
      }
    } as unknown as BridgeClient;

    const mcpServer = new LmmsMcpServer(mockClient);
    assert.ok(mcpServer, "Server initialized");

    // Test tool list completeness
    assert.strictEqual(TOOLS.length, 20);

    // Call tool directly through tool handler
    const statusTool = TOOLS.find((t) => t.name === "lmms_get_status")!;
    const res = await statusTool.handler(mockClient, {});
    assert.strictEqual(res.structuredContent?.ok, true);
    assert.strictEqual(res.structuredContent?.instanceId, "inst_test_server");
  });
});
