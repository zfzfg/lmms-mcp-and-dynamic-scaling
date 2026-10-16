import { test } from "node:test";
import assert from "node:assert/strict";
import { Client } from "@modelcontextprotocol/client";
import { StdioClientTransport } from "@modelcontextprotocol/client/stdio";
import { fileURLToPath } from "node:url";

for (const version of ["2026-07-28", "2025-11-25"]) {
 test(`real stdio discovery and validation on ${version}, without LMMS`, { timeout: 10000 }, async () => {
  const launcher = process.env.LMMS_MCP_TEST_LAUNCHER;
  const transport = new StdioClientTransport({ command: launcher ?? process.execPath,
   args: [...(launcher ? [] : [fileURLToPath(new URL("../src/index.js", import.meta.url))]), "--connection-file", "missing-instance.json"], stderr: "pipe" });
  transport.stderr?.on("data", () => {});
  const client = new Client({ name: "lmms-acceptance", version: "1.0.0" }, { supportedProtocolVersions: [version], versionNegotiation: { mode: version === "2026-07-28" ? { pin: version } : "legacy" } });
  try {
   await client.connect(transport);
   const tools = await client.listTools();
   assert.ok(tools.tools.length >= 20);
   for (const tool of tools.tools) { assert.ok(tool.outputSchema, `${tool.name} output schema`); assert.ok(tool.annotations); }
   const result = await client.callTool({ name: "lmms_get_status", arguments: {} });
   assert.equal(result.isError, true);
   assert.equal((result.structuredContent as any)?.error.code, "NOT_CONNECTED");
   const invalid = await client.callTool({ name: "lmms_apply_changes", arguments: {} });
   assert.equal(invalid.isError, true);
   const templates = await client.listResourceTemplates();
   assert.ok(templates.resourceTemplates.length >= 3);
  } finally { await client.close(); await transport.close(); }
 });
}
