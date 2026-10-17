import { test, describe, before, after } from "node:test";
import * as assert from "node:assert";
import * as net from "node:net";
import * as path from "node:path";
import * as os from "node:os";
import { BridgeClient, ConnectionConfig } from "../src/bridge-client.js";
import { ErrorCode, LmmsBridgeError } from "../src/errors.js";

describe("BridgeClient IPC and Framing", () => {
  let mockServer: net.Server;
  let connections = 0;
  let pipeName: string;
  let socketPath: string;

  before((_, done) => {
    pipeName = `test-lmms-pipe-${Date.now()}`;
    socketPath = process.platform === "win32"
      ? `\\\\.\\pipe\\${pipeName}`
      : path.join(os.tmpdir(), `${pipeName}.sock`);

    mockServer = net.createServer((sock) => {
      connections++;
      let buffer = Buffer.alloc(0);

      sock.on("data", (chunk) => {
        buffer = Buffer.concat([buffer, chunk]);
        while (buffer.length >= 4) {
          const frameLen = buffer.readUInt32BE(0);
          if (buffer.length < 4 + frameLen) break;

          const frame = buffer.subarray(4, 4 + frameLen);
          buffer = buffer.subarray(4 + frameLen);

          const req = JSON.parse(frame.toString("utf-8"));
          if (req.method === "auth.handshake") {
            const resp = {
              jsonrpc: "2.0",
              id: req.id,
              result: {
                ok: true,
                instanceId: req.params.instanceId,
                ipcProtocolVersion: "1.0",
                permissions: ["read", "write_project"]
              }
            };
            const payload = Buffer.from(JSON.stringify(resp));
            const header = Buffer.alloc(4);
            header.writeUInt32BE(payload.length, 0);
            sock.write(header);
            sock.write(payload);
          } else if (req.method === "domain.call") {
            const resp = {
              jsonrpc: "2.0",
              id: req.id,
              result: {
                ok: true,
                action: req.params.action,
                echo: req.params.payload
              }
            };
            const payload = Buffer.from(JSON.stringify(resp));
            const header = Buffer.alloc(4);
            header.writeUInt32BE(payload.length, 0);
            sock.write(header);
            sock.write(payload);
          }
        }
      });
    });

    mockServer.listen(socketPath, () => {
      done();
    });
  });

  after((_, done) => {
    mockServer.close(() => done());
  });

  test("connects, handshakes and executes domain.call successfully", async () => {
    const config: ConnectionConfig = {
      instanceId: "inst_test_1",
      endpoint: socketPath,
      authToken: "secret123"
    };

    const client = new BridgeClient(config);
    await client.connect();

    const res = await client.call("get_status", { foo: "bar" });
    assert.strictEqual(res.ok, true);
    assert.strictEqual(res.action, "get_status");
    assert.deepStrictEqual(res.echo, { foo: "bar" });

    client.close();
  });
  test("parallel first calls share a single authenticated connection", async () => {
    const before = connections;
    const client = new BridgeClient({ instanceId: "parallel", endpoint: socketPath, authToken: "test" });
    try {
      const results = await Promise.all(Array.from({ length: 12 }, (_, index) => client.call("get_status", { index })));
      assert.strictEqual(connections - before, 1);
      for (let i = 0; i < results.length; i++) assert.strictEqual(results[i].echo.index, i);
    } finally { client.close(); }
  });
  test("closing rejects pending calls and allows reconnect", async () => {
    const client = new BridgeClient({ instanceId: "reconnect", endpoint: socketPath, authToken: "test" });
    await client.connect();
    const pending = client.call("get_status"); client.close();
    await assert.rejects(pending, (error: unknown) => error instanceof LmmsBridgeError && error.code === ErrorCode.NotConnected);
    const result = await client.call("get_status", { reconnected: true });
    assert.strictEqual(result.echo.reconnected, true); client.close();
  });

});
