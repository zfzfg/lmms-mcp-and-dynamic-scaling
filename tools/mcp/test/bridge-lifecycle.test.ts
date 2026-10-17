import { test } from "node:test";
import assert from "node:assert/strict";
import * as net from "node:net";
import * as os from "node:os";
import * as path from "node:path";
import { once } from "node:events";
import { randomUUID } from "node:crypto";
import { BridgeClient, ConnectionConfig } from "../src/bridge-client.js";
import { LmmsBridgeError, ErrorCode } from "../src/errors.js";
import { RESOURCES } from "../src/resources.js";

function frame(value: unknown): Buffer {
  const body = Buffer.from(JSON.stringify(value));
  const header = Buffer.alloc(4); header.writeUInt32BE(body.length);
  return Buffer.concat([header, body]);
}
const reply = (req: any, result: unknown) => ({ jsonrpc: "2.0", id: req.id, result });
const handshake = (req: any) => reply(req, { ok: true, instanceId: req.params.instanceId, ipcProtocolVersion: "1.0" });
const code = (expected: ErrorCode) => (error: unknown) => error instanceof LmmsBridgeError && error.code === expected;

async function withPeer(handler: (req: any, socket: net.Socket) => void,
  run: (config: ConnectionConfig) => Promise<void>) {
  const sockets = new Set<net.Socket>();
  const endpoint = process.platform === "win32" ? `\\\\.\\pipe\\lmms-lifecycle-${process.pid}-${randomUUID()}`
    : path.join(os.tmpdir(), `lmms-${randomUUID()}.sock`);
  const server = net.createServer(socket => {
    sockets.add(socket); socket.on("close", () => sockets.delete(socket)); socket.on("error", () => {});
    let buffer = Buffer.alloc(0);
    socket.on("data", chunk => {
      buffer = Buffer.concat([buffer, chunk]);
      while (buffer.length >= 4) {
        const length = buffer.readUInt32BE(0);
        if (buffer.length < length + 4) return;
        const request = JSON.parse(buffer.subarray(4, length + 4).toString());
        buffer = buffer.subarray(length + 4);
        handler(request, socket);
      }
    });
  });
  server.listen(endpoint); await once(server, "listening");
  try { await run({ instanceId: "original", endpoint, authToken: "private-token" }); }
  finally {
    for (const socket of sockets) socket.destroy();
    await new Promise<void>((resolve, reject) => server.close(error => error ? reject(error) : resolve()));
  }
}

test("closing during socket setup cancels the old attempt without closing its replacement", async () => {
  await withPeer((req, socket) => socket.write(frame(handshake(req))), async config => {
    const client = new BridgeClient(config);
    try {
      const first = client.connect();
      const rejected = assert.rejects(first, code(ErrorCode.NotConnected));
      client.close();
      await client.connect();
      await rejected;
      client.assertInstance(config.instanceId);
    } finally { client.close(); }
  });
});

test("closing a suspended handshake rejects it and parallel replacement calls connect once", async () => {
  let seen!: () => void;
  const received = new Promise<void>(resolve => { seen = resolve; });
  let handshakes = 0;
  await withPeer((req, socket) => {
    if (req.method === "auth.handshake") {
      if (++handshakes === 1) { seen(); return; }
      socket.write(frame(handshake(req)));
    } else socket.write(frame(reply(req, { ok: true })));
  }, async config => {
    const client = new BridgeClient(config);
    try {
      const first = client.connect();
      const rejected = assert.rejects(first, code(ErrorCode.NotConnected));
      await received; client.close();
      await Promise.all(Array.from({ length: 8 }, () => client.call("get_status")));
      await rejected;
      assert.equal(handshakes, 2);
    } finally { client.close(); }
  });
});

test("late handshake completion cannot authenticate an explicitly closed connection", async () => {
  await withPeer((req, socket) => socket.write(frame(handshake(req))), async config => {
    const client = new BridgeClient(config);
    // The data listener registered in open() resolves the handshake first.
    const connecting = client.connect();
    const rejected = assert.rejects(connecting, code(ErrorCode.NotConnected));
    const socket = (client as any).socket as net.Socket;
    socket.once("data", () => client.close());
    await rejected;
    assert.throws(() => client.assertInstance(config.instanceId), code(ErrorCode.PermissionDenied));
    client.close();
  });
});

for (const invalid of [null, [], { jsonrpc: "1.0", result: { ok: true } },
  { jsonrpc: "2.0", result: { ok: true }, error: { code: "BAD", message: "bad" } },
  { jsonrpc: "2.0", result: { ok: false } }, { jsonrpc: "2.0", error: {} },
  { jsonrpc: "2.0", result: null }, { jsonrpc: "2.0", id: "wrong", result: { ok: true } }]) {
  test(`invalid response rejects all pending requests: ${JSON.stringify(invalid)}`, async () => {
    let calls = 0;
    await withPeer((req, socket) => {
      if (req.method === "auth.handshake") socket.write(frame(handshake(req)));
      else if (++calls === 2) socket.write(frame(invalid && !Array.isArray(invalid) ? { id: req.id, ...invalid } : invalid));
    }, async config => {
      const client = new BridgeClient(config);
      try {
        await client.connect();
        await Promise.all([assert.rejects(client.call("get_status"), code(ErrorCode.NotConnected)),
          assert.rejects(client.call("get_project"), code(ErrorCode.NotConnected))]);
      } finally { client.close(); }
    });
  });
}

test("fragmented headers and combined responses preserve request IDs", async () => {
  const requests: any[] = [];
  await withPeer((req, socket) => {
    if (req.method === "auth.handshake") { socket.write(frame(handshake(req))); return; }
    requests.push(req);
    if (requests.length === 2) {
      const data = Buffer.concat(requests.reverse().map(request => frame(reply(request, { ok: true, action: request.params.action }))));
      socket.write(data.subarray(0, 2));
      setImmediate(() => socket.write(data.subarray(2)));
    }
  }, async config => {
    const client = new BridgeClient(config);
    try {
      const results = await Promise.all([client.call("get_status"), client.call("get_project")]);
      assert.deepEqual(results.map(result => result.action), ["get_status", "get_project"]);
    } finally { client.close(); }
  });
});

for (const length of [0, 1024 * 1024 + 1]) {
  test(`invalid frame length ${length} closes the connection immediately`, async () => {
    await withPeer((req, socket) => {
      if (req.method === "auth.handshake") { socket.write(frame(handshake(req))); return; }
      const header = Buffer.alloc(4); header.writeUInt32BE(length); socket.write(header);
    }, async config => {
      const client = new BridgeClient(config);
      try { await assert.rejects(client.call("get_status"), code(ErrorCode.NotConnected)); }
      finally { client.close(); }
    });
  });
}

test("invalid deadline and excess calls are rejected before connecting", async () => {
  let loaded = 0;
  const client = new BridgeClient(() => { ++loaded; throw new Error("must not connect"); });
  await assert.rejects(client.call("get_status", {}, undefined, 0), code(ErrorCode.ValidationError));
  assert.equal(loaded, 0);
  await withPeer(() => {}, async config => {
    const pendingClient = new BridgeClient(config);
    const calls = Array.from({ length: 64 }, () => assert.rejects(pendingClient.call("get_status"), code(ErrorCode.NotConnected)));
    await assert.rejects(pendingClient.call("get_status"), code(ErrorCode.Busy));
    pendingClient.close(); await Promise.all(calls);
  });
});

test("resource checks the newly authenticated instance before sending any domain call", async () => {
  let calls = 0;
  await withPeer((req, socket) => {
    if (req.method === "auth.handshake") socket.write(frame(handshake(req)));
    else { ++calls; socket.write(frame(reply(req, { ok: true }))); }
  }, async config => {
    let selected = config;
    const client = new BridgeClient(() => selected);
    const resource = RESOURCES.find(value => value.name === "LMMS Capabilities")!;
    const uri = `lmms://instances/${config.instanceId}/capabilities`;
    try {
      await client.connect(); client.close();
      selected = { ...config, instanceId: "replacement" };
      await assert.rejects(resource.read(client, uri, uri.match(resource.uriPattern)!), code(ErrorCode.PermissionDenied));
      assert.equal(calls, 0);
      const current = "lmms://instances/replacement/capabilities";
      await resource.read(client, current, current.match(resource.uriPattern)!);
      assert.equal(calls, 1);
    } finally { client.close(); }
  });
});
