import * as net from "node:net";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { ErrorCode, LmmsBridgeError } from "./errors.js";

export interface ConnectionConfig {
  instanceId: string;
  endpoint: string;
  authToken: string;
  ipcProtocolVersion?: string;
  bridgeVersion?: string;
  createdAt?: string;
  requestedPermissions?: string[];
}
const MAX_FRAME = 1024 * 1024;
function bridgeError(code: ErrorCode, message: string): LmmsBridgeError { return new LmmsBridgeError({ code, message }); }
export function getDefaultConnectionPath(): string {
  if (process.env.LMMS_MCP_CONNECTION_FILE) return process.env.LMMS_MCP_CONNECTION_FILE;
  throw bridgeError(ErrorCode.NotConnected, "Choose an LMMS instance using --connection-file or LMMS_MCP_CONNECTION_FILE. Copy the path from LMMS's MCP settings.");
}
export function loadConnectionConfig(filePath?: string): ConnectionConfig {
  const resolved = filePath || getDefaultConnectionPath();
  try {
    const config = JSON.parse(fs.readFileSync(resolved, "utf8")) as ConnectionConfig;
    if (![config.instanceId, config.endpoint, config.authToken].every(value => typeof value === "string" && value.length > 0)) throw new Error("Missing connection fields");
    return config;
  } catch (error) {
    throw bridgeError(ErrorCode.NotConnected, `Cannot load connection file ${resolved}: ${(error as Error).message}`);
  }
}

export class BridgeClient {
  private socket: net.Socket | null = null;
  private connecting?: Promise<void>;
  private config?: ConnectionConfig;
  private nextId = 1;
  private authenticated = false;
  private receiveBuffer: Buffer<ArrayBufferLike> = Buffer.alloc(0);
  private pending = new Map<number, { resolve: (value: any) => void; reject: (error: unknown) => void; timer: NodeJS.Timeout }>();
  constructor(private source: ConnectionConfig | (() => ConnectionConfig)) { if (typeof source !== "function") this.config = source; }
  public get instanceId(): string { return this.config?.instanceId ?? "unavailable"; }
  public assertInstance(instanceId: string): void {
    const config = this.config ?? (typeof this.source === "function" ? this.source() : this.source);
    if (config.instanceId !== instanceId) throw bridgeError(ErrorCode.PermissionDenied, "Resource belongs to another instance.");
  }
  public async connect(): Promise<void> {
    if (this.authenticated) return;
    if (this.connecting) return this.connecting;
    this.connecting = this.open().finally(() => { this.connecting = undefined; });
    return this.connecting;
  }
  private async open(): Promise<void> {
    this.config = typeof this.source === "function" ? this.source() : this.source;
    const config = this.config;
    if (process.platform !== "win32" && !path.isAbsolute(config.endpoint)) throw bridgeError(ErrorCode.ValidationError, "LMMS must publish an absolute socket address.");
    const address = process.platform === "win32" && !config.endpoint.startsWith("\\\\") ? `\\\\.\\pipe\\${config.endpoint}` : config.endpoint;
    this.receiveBuffer = Buffer.alloc(0);
    const socket = net.createConnection(address); this.socket = socket;
    socket.on("data", (chunk: Buffer) => { if (this.socket === socket) this.onData(chunk); });
    socket.on("error", () => {});
    socket.on("close", () => { if (this.socket === socket) this.reset(bridgeError(ErrorCode.NotConnected, "Connection to LMMS closed.")); });
    try {
      await new Promise<void>((resolve, reject) => {
        const timer = setTimeout(() => { socket.destroy(); reject(bridgeError(ErrorCode.Timeout, "Connection timed out.")); }, 3000);
        socket.once("connect", () => { clearTimeout(timer); resolve(); });
        socket.once("error", error => { clearTimeout(timer); reject(bridgeError(ErrorCode.NotConnected, error.message)); });
        socket.once("close", () => { clearTimeout(timer); reject(bridgeError(ErrorCode.NotConnected, "Connection closed.")); });
      });
      const handshake = await this.request("auth.handshake", { instanceId: config.instanceId, authToken: config.authToken,
        ipcProtocolVersion: "1.0", clientVersion: "1.1.0", requestedPermissions: config.requestedPermissions }, 3000);
      if (!handshake?.ok || handshake.instanceId !== config.instanceId || handshake.ipcProtocolVersion !== "1.0")
        throw bridgeError(ErrorCode.PermissionDenied, "Unexpected instance or IPC version in handshake.");
      this.authenticated = true;
    } catch (error) { this.close(); throw error; }
  }
  public async call(action: string, payload: Record<string, unknown> = {}, operationKey?: string, deadlineMs = 5000): Promise<any> {
    await this.connect();
    if (!Number.isInteger(deadlineMs) || deadlineMs <= 0 || deadlineMs > 30000) throw bridgeError(ErrorCode.ValidationError, "Invalid deadline.");
    if (this.pending.size >= 64) throw bridgeError(ErrorCode.Busy, "Too many pending requests.");
    const writes = new Set(["apply_changes", "set_transport", "undo", "redo", "create_instrument_track", "load_asset", "save_project_copy", "start_render", "cancel_job"]);
    const key = operationKey ?? (writes.has(action) ? payload.operationKey : undefined);
    return this.request("domain.call", { action, payload, operationKey: key, deadlineMs }, deadlineMs + 500);
  }
  private request(method: string, params: Record<string, unknown>, timeout: number): Promise<any> {
    return new Promise((resolve, reject) => {
      const id = this.nextId++;
      const timer = setTimeout(() => { this.pending.delete(id); reject(bridgeError(ErrorCode.Timeout, "Request timed out; query get_operation before retrying a mutation.")); }, timeout);
      this.pending.set(id, { resolve, reject, timer });
      try {
        const data = Buffer.from(JSON.stringify({ jsonrpc: "2.0", id, method, params }));
        if (data.length > MAX_FRAME) throw bridgeError(ErrorCode.ValidationError, "IPC frame exceeds 1 MiB.");
        const socket = this.socket;
        if (!socket || socket.destroyed) throw bridgeError(ErrorCode.NotConnected, "Socket closed.");
        if (socket.writableLength > MAX_FRAME * 2) throw bridgeError(ErrorCode.Busy, "IPC write buffer full.");
        const header = Buffer.alloc(4); header.writeUInt32BE(data.length);
        socket.write(Buffer.concat([header, data]));
      } catch (error) { clearTimeout(timer); this.pending.delete(id); reject(error); }
    });
  }
  private onData(chunk: Buffer): void {
    this.receiveBuffer = Buffer.concat([this.receiveBuffer, chunk]);
    while (this.receiveBuffer.length >= 4) {
      const size = this.receiveBuffer.readUInt32BE(0);
      if (size === 0 || size > MAX_FRAME) { this.close(); return; }
      if (this.receiveBuffer.length < size + 4) return;
      const frame = this.receiveBuffer.subarray(4, size + 4); this.receiveBuffer = this.receiveBuffer.subarray(size + 4);
      try {
        const response = JSON.parse(frame.toString("utf8")); const request = this.pending.get(response.id);
        if (!request) continue;
        clearTimeout(request.timer); this.pending.delete(response.id);
        const error = response.error ?? (response.result?.ok === false ? response.result.error : undefined);
        if (error) request.reject(new LmmsBridgeError(error)); else request.resolve(response.result);
      } catch { this.close(); return; }
    }
  }
  private reset(error: Error): void {
    this.authenticated = false; this.socket = null; this.receiveBuffer = Buffer.alloc(0);
    for (const pending of this.pending.values()) { clearTimeout(pending.timer); pending.reject(error); }
    this.pending.clear();
  }
  public close(): void { const socket = this.socket; this.reset(bridgeError(ErrorCode.NotConnected, "Bridge closed.")); socket?.destroy(); }
}
