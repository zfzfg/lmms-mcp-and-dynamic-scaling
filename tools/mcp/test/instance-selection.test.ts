import { test } from "node:test";
import assert from "node:assert/strict";
import * as net from "node:net";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { randomUUID } from "node:crypto";
import { once } from "node:events";
import { BridgeClient, ConnectionConfig } from "../src/bridge-client.js";
import { selectLatestInstance } from "../src/instance-selection.js";

async function peer(instanceId: string) {
  const endpoint = process.platform === "win32" ? `\\\\.\\pipe\\lmms-auto-${randomUUID()}` : path.join(os.tmpdir(), `lmms-${randomUUID()}.sock`);
  const sockets = new Set<net.Socket>();
  const requests: any[] = [];
  let writerBusy = false;
  let held: (() => void) | undefined;
  const server = net.createServer(socket => {
    sockets.add(socket); socket.on("close", () => sockets.delete(socket)); socket.on("error", () => {});
    let buffer = Buffer.alloc(0);
    const send = (req: any, result: any) => {
      const body = Buffer.from(JSON.stringify({ jsonrpc: "2.0", id: req.id, result }));
      const header = Buffer.alloc(4); header.writeUInt32BE(body.length); socket.write(Buffer.concat([header, body]));
    };
    socket.on("data", chunk => {
      buffer = Buffer.concat([buffer, chunk]);
      while (buffer.length >= 4) {
        const size = buffer.readUInt32BE(0); if (buffer.length < size + 4) return;
        const req = JSON.parse(buffer.subarray(4, size + 4).toString()); buffer = buffer.subarray(size + 4);
        requests.push(req);
        if (req.method === "auth.handshake") {
          if (req.params.instanceId !== instanceId || req.params.authToken !== "test-token")
            send(req, { ok: false, error: { code: "PERMISSION_DENIED", message: "Bad identity" } });
          else if (writerBusy && !req.params.requestedPermissions?.includes("read"))
            send(req, { ok: false, error: { code: "WRITER_BUSY", message: "Writer occupied" } });
          else send(req, { ok: true, instanceId, ipcProtocolVersion: "1.0" });
        } else if (req.params.action === "hold") held = () => send(req, { ok: true, instanceId });
        else if (req.params.action === "create_instrument_track") socket.destroy();
        else send(req, { ok: true, instanceId });
      }
    });
  });
  server.listen(endpoint); await once(server, "listening");
  return { config: { instanceId, endpoint, authToken: "test-token" } as ConnectionConfig, requests,
    busy: () => { writerBusy = true; }, release: () => held?.(),
    async close() {
      for (const socket of sockets) socket.destroy();
      await new Promise<void>((resolve, reject) => server.close(error => error ? reject(error) : resolve()));
    } };
}

function publish(directory: string, config: ConnectionConfig, age: number) {
  const file = path.join(directory, `${config.instanceId}.json`);
  fs.writeFileSync(file, JSON.stringify(config));
  fs.utimesSync(file, new Date(100000 + age), new Date(100000 + age));
  return file;
}
async function fixture(run: (directory: string) => Promise<void>) {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), "lmms-auto-"));
  try { await run(directory); } finally { fs.rmSync(directory, { recursive: true, force: true }); }
}

test("latest selection skips newer stale and malformed files and probes only as reader", async () => {
  await fixture(async directory => {
    const first = await peer("inst_first"); const second = await peer("inst_second");
    try {
      publish(directory, first.config, 1); publish(directory, second.config, 2);
      publish(directory, { ...second.config, instanceId: "inst_stale", endpoint: `${second.config.endpoint}-missing` }, 3);
      fs.writeFileSync(path.join(directory, "inst_broken.json"), "{invalid");
      const selected = await selectLatestInstance(directory);
      assert.equal(selected.instanceId, "inst_second");
      assert.ok(second.requests.every(req => req.method === "auth.handshake" && req.params.requestedPermissions.join() === "read"));
    } finally { await first.close(); await second.close(); }
  });
});

test("one companion follows a newer open instance and returns to an older live instance after it closes", async () => {
  await fixture(async directory => {
    const first = await peer("inst_first"); let second: Awaited<ReturnType<typeof peer>> | undefined;
    const client = new BridgeClient(() => selectLatestInstance(directory), true);
    try {
      publish(directory, first.config, 1);
      assert.equal((await client.call("get_status")).instanceId, "inst_first");
      second = await peer("inst_second"); publish(directory, second.config, 2);
      assert.equal((await client.call("get_status")).instanceId, "inst_second");
      await second.close(); second = undefined;
      await new Promise<void>(resolve => setImmediate(resolve));
      assert.equal((await client.call("get_status")).instanceId, "inst_first");
    } finally { client.close(); if (second) await second.close(); await first.close(); }
  });
});

test("no LMMS running is recoverable on the next call without restarting the companion", async () => {
  await fixture(async directory => {
    const client = new BridgeClient(() => selectLatestInstance(directory), true);
    let app: Awaited<ReturnType<typeof peer>> | undefined;
    try {
      await assert.rejects(client.call("get_status"), (error: any) => error.code === "NOT_CONNECTED");
      app = await peer("inst_started"); publish(directory, app.config, 1);
      assert.equal((await client.call("get_status")).instanceId, "inst_started");
    } finally { client.close(); if (app) await app.close(); }
  });
});

test("explicit close cancels asynchronous discovery and a subsequent call can reconnect", async () => {
  const app = await peer("inst_selected");
  let release!: (config: ConnectionConfig) => void;
  let first = true;
  const client = new BridgeClient(() => {
    if (!first) return app.config;
    first = false;
    return new Promise<ConnectionConfig>(resolve => { release = resolve; });
  }, true);
  try {
    const pending = client.connect(); client.close(); release(app.config);
    await assert.rejects(pending, (error: any) => error.code === "NOT_CONNECTED");
    assert.equal(app.requests.length, 0);
    assert.equal((await client.call("get_status")).instanceId, "inst_selected");
  } finally { client.close(); await app.close(); }
});

test("a busy writer on the newest instance does not redirect writes to an older project", async () => {
  await fixture(async directory => {
    const first = await peer("inst_first"); const second = await peer("inst_second");
    const client = new BridgeClient(() => selectLatestInstance(directory), true);
    try {
      publish(directory, first.config, 1); publish(directory, second.config, 2); second.busy();
      await assert.rejects(client.call("get_status"), (error: any) => error.code === "WRITER_BUSY");
      assert.equal(first.requests.filter(req => req.method === "domain.call").length, 0);
    } finally { client.close(); await first.close(); await second.close(); }
  });
});

test("instance changes neither interrupt an in-flight call nor send a second command to its old project", async () => {
  await fixture(async directory => {
    const first = await peer("inst_first"); const second = await peer("inst_second");
    const client = new BridgeClient(() => selectLatestInstance(directory), true);
    try {
      publish(directory, first.config, 1); await client.connect();
      const pending = client.call("hold");
      while (!first.requests.some(req => req.params.action === "hold")) await new Promise<void>(resolve => setImmediate(resolve));
      publish(directory, second.config, 2);
      await assert.rejects(client.call("get_status"), (error: any) => error.code === "BUSY");
      first.release(); assert.equal((await pending).instanceId, "inst_first");
      assert.equal((await client.call("get_status")).instanceId, "inst_second");
    } finally { client.close(); await first.close(); await second.close(); }
  });
});

test("automatic discovery rejects stale resource identities and never replays a disconnected mutation", async () => {
  await fixture(async directory => {
    const first = await peer("inst_first"); const second = await peer("inst_second");
    const client = new BridgeClient(() => selectLatestInstance(directory), true);
    try {
      publish(directory, first.config, 1);
      await assert.rejects(client.call("create_instrument_track", {}, "only-once"), (error: any) => error.code === "NOT_CONNECTED");
      publish(directory, second.config, 2);
      await assert.rejects(client.callForInstance("inst_first", "get_status"), (error: any) => error.code === "PERMISSION_DENIED");
      assert.equal((await client.call("get_status")).instanceId, "inst_second");
      const mutations = [...first.requests, ...second.requests].filter(req => req.params.action === "create_instrument_track");
      assert.equal(mutations.length, 1);
    } finally { client.close(); await first.close(); await second.close(); }
  });
});
