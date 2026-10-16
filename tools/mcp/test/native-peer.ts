// Invoked by the native Qt integration test against the real LMMS service.
import assert from "node:assert/strict";
import { loadConnectionConfig, BridgeClient } from "../src/bridge-client.js";
const config = loadConnectionConfig(process.argv[2]);
const reader = new BridgeClient({ ...config, requestedPermissions: ["read"] });
const writer = new BridgeClient(config);
const secondWriter = new BridgeClient(config);
try {
  const status = await reader.call("get_status"); assert.equal(status.ready, true);
  const project = await reader.call("get_project"); assert.ok(project.projectId);
  await assert.rejects(reader.call("apply_changes", { projectId: project.projectId, expectedRevision: project.projectRevision,
    planId: "must-not-execute", operationKey: "read-only-denied" }), (error: any) => error.code === "PERMISSION_DENIED");
  await writer.connect();
  await assert.rejects(secondWriter.connect(), (error: any) => error.code === "WRITER_BUSY");
  const tracks = await reader.call("list_tracks", { container: "song", limit: 10 }); assert.ok(Array.isArray(tracks.items));
  process.stdout.write(JSON.stringify({ ok: true, instanceId: status.instanceId }) + "\n");
} finally { reader.close(); writer.close(); secondWriter.close(); }
