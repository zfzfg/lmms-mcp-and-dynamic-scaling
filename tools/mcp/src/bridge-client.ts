import * as net from "node:net";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { ErrorCode, DomainError, LmmsBridgeError } from "./errors.js";

export interface ConnectionConfig {
  instanceId: string;
  endpoint: string;
  authToken: string;
  bridgeVersion?: string;
  createdAt?: string;
}

export function getDefaultConnectionPath(): string {
  if (process.env.LMMS_MCP_CONNECTION_FILE) {
    return process.env.LMMS_MCP_CONNECTION_FILE;
  }
  if (process.platform === "win32") {
    const localAppData = process.env.LOCALAPPDATA || path.join(os.homedir(), "AppData", "Local");
    return path.join(localAppData, "LMMS", "mcp", "connections", "studio.json");
  } else {
    return path.join(os.homedir(), ".local", "share", "LMMS", "mcp", "connections", "studio.json");
  }
}

export function loadConnectionConfig(filePath?: string): ConnectionConfig {
  const resolvedPath = filePath || getDefaultConnectionPath();
  if (!fs.existsSync(resolvedPath)) {
    throw new LmmsBridgeError({
      code: ErrorCode.NotConnected,
      message: `Connection file not found at: ${resolvedPath}. Ensure LMMS is running with MCP enabled.`
    });
  }

  try {
    const content = fs.readFileSync(resolvedPath, "utf-8");
    const json = JSON.parse(content) as ConnectionConfig;
    if (!json.instanceId || !json.endpoint || !json.authToken) {
      throw new Error("Missing required connection fields (instanceId, endpoint, authToken)");
    }
    return json;
  } catch (err: unknown) {
    throw new LmmsBridgeError({
      code: ErrorCode.ValidationError,
      message: `Failed to parse connection file: ${(err as Error).message}`
    });
  }
}

export class BridgeClient {
  private config: ConnectionConfig;
  private socket: net.Socket | null = null;
  private nextId = 1;
  private pendingRequests = new Map<number, {
    resolve: (val: any) => void;
    reject: (err: any) => void;
    timer: NodeJS.Timeout;
  }>();
  private receiveBuffer = Buffer.alloc(0);
  private isConnected = false;
  private isHandshakeComplete = false;

  constructor(config: ConnectionConfig) {
    this.config = config;
  }

  public get instanceId(): string {
    return this.config.instanceId;
  }

  public async connect(): Promise<void> {
    if (this.isConnected && this.isHandshakeComplete) {
      return;
    }

    return new Promise((resolve, reject) => {
      let pipePath: string;
      if (process.platform === "win32") {
        pipePath = `\\\\.\\pipe\\${this.config.endpoint}`;
      } else {
        pipePath = this.config.endpoint.startsWith("/")
          ? this.config.endpoint
          : path.join("/tmp", `${this.config.endpoint}.sock`);
      }

      const socket = net.createConnection(pipePath);
      this.socket = socket;

      socket.on("connect", async () => {
        this.isConnected = true;
        try {
          await this.performHandshake();
          this.isHandshakeComplete = true;
          resolve();
        } catch (err) {
          reject(err);
        }
      });

      socket.on("data", (chunk: Buffer) => {
        this.onDataReceived(chunk);
      });

      socket.on("error", (err: Error) => {
        if (!this.isConnected) {
          reject(new LmmsBridgeError({
            code: ErrorCode.NotConnected,
            message: `Could not connect to LMMS pipe/socket: ${err.message}`
          }));
        }
      });

      socket.on("close", () => {
        this.isConnected = false;
        this.isHandshakeComplete = false;
        this.socket = null;
        for (const [id, req] of this.pendingRequests.entries()) {
          clearTimeout(req.timer);
          req.reject(new LmmsBridgeError({
            code: ErrorCode.NotConnected,
            message: "Connection to LMMS was closed."
          }));
        }
        this.pendingRequests.clear();
      });
    });
  }

  public async call(action: string, payload: Record<string, unknown> = {}, operationKey?: string, deadlineMs = 5000): Promise<any> {
    if (!this.isConnected || !this.isHandshakeComplete) {
      await this.connect();
    }

    const id = this.nextId++;
    const request = {
      jsonrpc: "2.0",
      id,
      method: "domain.call",
      params: {
        action,
        payload,
        operationKey,
        deadlineMs
      }
    };

    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        this.pendingRequests.delete(id);
        reject(new LmmsBridgeError({
          code: ErrorCode.Timeout,
          message: `Request '${action}' timed out after ${deadlineMs}ms.`
        }));
      }, deadlineMs + 500);

      this.pendingRequests.set(id, { resolve, reject, timer });
      this.sendJson(request);
    });
  }

  public close(): void {
    if (this.socket) {
      this.socket.end();
      this.socket.destroy();
      this.socket = null;
    }
  }

  private async performHandshake(): Promise<void> {
    const id = this.nextId++;
    const handshakeReq = {
      jsonrpc: "2.0",
      id,
      method: "auth.handshake",
      params: {
        clientVersion: "1.0.0",
        ipcProtocolVersion: "1.0",
        instanceId: this.config.instanceId,
        authToken: this.config.authToken
      }
    };

    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        this.pendingRequests.delete(id);
        reject(new LmmsBridgeError({
          code: ErrorCode.Timeout,
          message: "Authentication handshake timed out."
        }));
      }, 3000);

      this.pendingRequests.set(id, {
        resolve: (result) => {
          if (result && result.ok) {
            resolve();
          } else {
            reject(new LmmsBridgeError({
              code: ErrorCode.PermissionDenied,
              message: "Handshake rejected by LMMS."
            }));
          }
        },
        reject,
        timer
      });

      this.sendJson(handshakeReq);
    });
  }

  private sendJson(obj: unknown): void {
    if (!this.socket || !this.isConnected) {
      throw new LmmsBridgeError({
        code: ErrorCode.NotConnected,
        message: "Socket is not connected."
      });
    }

    const jsonString = JSON.stringify(obj);
    const payloadBuf = Buffer.from(jsonString, "utf-8");
    const headerBuf = Buffer.alloc(4);
    headerBuf.writeUInt32BE(payloadBuf.length, 0);

    this.socket.write(headerBuf);
    this.socket.write(payloadBuf);
  }

  private onDataReceived(chunk: Buffer): void {
    this.receiveBuffer = Buffer.concat([this.receiveBuffer, chunk]);

    while (this.receiveBuffer.length >= 4) {
      const frameLen = this.receiveBuffer.readUInt32BE(0);

      if (frameLen > 1024 * 1024) {
        // Frame limit exceeded
        this.close();
        return;
      }

      if (this.receiveBuffer.length < 4 + frameLen) {
        // Incomplete frame, wait for more data
        break;
      }

      const frameData = this.receiveBuffer.subarray(4, 4 + frameLen);
      this.receiveBuffer = this.receiveBuffer.subarray(4 + frameLen);

      this.handleFrame(frameData);
    }
  }

  private handleFrame(frameData: Buffer): void {
    try {
      const message = JSON.parse(frameData.toString("utf-8"));
      const id = message.id;
      if (typeof id === "number" && this.pendingRequests.has(id)) {
        const { resolve, reject, timer } = this.pendingRequests.get(id)!;
        clearTimeout(timer);
        this.pendingRequests.delete(id);

        if (message.error) {
          reject(new LmmsBridgeError(message.error));
        } else if (message.result && message.result.ok === false && message.result.error) {
          reject(new LmmsBridgeError(message.result.error));
        } else {
          resolve(message.result);
        }
      }
    } catch (err) {
      // Discard malformed frames
    }
  }
}
