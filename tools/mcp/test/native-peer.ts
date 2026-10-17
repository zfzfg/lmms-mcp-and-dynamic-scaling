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
  if (process.argv[3] === "--instruments") {
    assert.equal(project.data.name, "Untitled");
    const catalog = await reader.call("list_instruments", { type: "internal", limit: 500 });
    for (const pluginKey of ["organic", "tripleoscillator"]) {
      assert.ok(catalog.instruments.some((instrument: any) => instrument.key === pluginKey), `${pluginKey} must be built for native acceptance`);
      const before = await reader.call("get_project");
      const count = (await reader.call("list_tracks", { container: "song", limit: 500 })).items.length;
      const created = await writer.call("create_instrument_track", { pluginKey, trackName: `MCP test ${pluginKey}`,
        projectId: before.projectId, expectedRevision: before.projectRevision }, `create-${pluginKey}`);
      assert.equal(created.ok, true);
      const afterCreate = (await reader.call("list_tracks", { container: "song", limit: 500 })).items;
      assert.equal(afterCreate.length, count + 1);
      assert.equal(afterCreate.find((track: any) => track.trackId === created.trackId).instrumentKey, pluginKey);
      await writer.call("undo", { projectId: before.projectId, expectedRevision: (await reader.call("get_project")).projectRevision,
        undoGroupId: created.undoGroupId }, `undo-${pluginKey}`);
      assert.equal((await reader.call("list_tracks", { container: "song", limit: 500 })).items.length, count);
      await writer.call("redo", { projectId: before.projectId, expectedRevision: (await reader.call("get_project")).projectRevision,
        undoGroupId: created.undoGroupId }, `redo-${pluginKey}`);
      assert.equal((await reader.call("list_tracks", { container: "song", limit: 500 })).items.length, count + 1);
      await writer.call("undo", { projectId: before.projectId, expectedRevision: (await reader.call("get_project")).projectRevision,
        undoGroupId: created.undoGroupId }, `cleanup-${pluginKey}`);
    }
    const beforeAsset = await reader.call("get_project");
    const target = await writer.call("create_instrument_track", { pluginKey: "tripleoscillator",
     projectId: beforeAsset.projectId, expectedRevision: beforeAsset.projectRevision }, "asset-target");
    const assets = await writer.call("list_assets", { rootId: "presets", limit: 500 });
    const asset = assets.items.find((value: any) => value.name === "test-organic.xpf");
    assert.ok(asset, "isolated preset must be in the catalog");
    const malformed = assets.items.find((value: any) => value.name === "malformed.xpf");
    assert.ok(malformed);
    const beforeFailure = await reader.call("get_project");
    const tracksBeforeFailure = await reader.call("list_tracks", { container: "song", limit: 500 });
    await assert.rejects(writer.call("load_asset", { assetId: malformed.assetId, targetTrackId: target.trackId,
     projectId: beforeFailure.projectId, expectedRevision: beforeFailure.projectRevision }, "malformed-preset"),
     (error: any) => error.code === "VALIDATION_ERROR");
    const afterFailure = await reader.call("get_project");
    assert.equal(afterFailure.projectRevision, beforeFailure.projectRevision);
    assert.equal(typeof beforeFailure.data.modified, "boolean");
    assert.equal(afterFailure.data.modified, beforeFailure.data.modified);
    assert.deepEqual((await reader.call("list_tracks", { container: "song", limit: 500 })).items, tracksBeforeFailure.items);
    const loaded = await writer.call("load_asset", { assetId: asset.assetId, targetTrackId: target.trackId,
     projectId: beforeAsset.projectId, expectedRevision: (await reader.call("get_project")).projectRevision }, "load-organic-preset");
    let listed = (await reader.call("list_tracks", { container: "song", limit: 500 })).items;
    assert.equal(listed.find((track: any) => track.trackId === target.trackId).instrumentKey, "organic");
    await writer.call("undo", { projectId: beforeAsset.projectId,
     expectedRevision: (await reader.call("get_project")).projectRevision, undoGroupId: loaded.undoGroupId }, "undo-preset");
    listed = (await reader.call("list_tracks", { container: "song", limit: 500 })).items;
    assert.ok(listed.some((track: any) => track.instrumentKey === "tripleoscillator"));
    await writer.call("redo", { projectId: beforeAsset.projectId,
     expectedRevision: (await reader.call("get_project")).projectRevision, undoGroupId: loaded.undoGroupId }, "redo-preset");
    listed = (await reader.call("list_tracks", { container: "song", limit: 500 })).items;
    assert.ok(listed.some((track: any) => track.instrumentKey === "organic"));
  }
  process.stdout.write(JSON.stringify({ ok: true, instanceId: status.instanceId }) + "\n");
} finally { reader.close(); writer.close(); secondWriter.close(); }
