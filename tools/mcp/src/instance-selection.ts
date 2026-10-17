import * as fs from "node:fs";
import * as path from "node:path";
import * as os from "node:os";
import { BridgeClient, ConnectionConfig, loadConnectionConfig } from "./bridge-client.js";
import { ErrorCode, LmmsBridgeError } from "./errors.js";

export function defaultConnectionDirectory(): string {
  if (process.env.LMMS_MCP_CONNECTION_DIRECTORY) return process.env.LMMS_MCP_CONNECTION_DIRECTORY;
  if (process.platform === "win32") return path.join(process.env.APPDATA ?? path.join(os.homedir(), "AppData", "Roaming"), "lmms", "mcp", "connections");
  if (process.platform === "darwin") return path.join(os.homedir(), "Library", "Application Support", "lmms", "mcp", "connections");
  return path.join(process.env.XDG_DATA_HOME ?? path.join(os.homedir(), ".local", "share"), "lmms", "mcp", "connections");
}

// Validate liveness by authenticating as a reader, never by taking the writer slot.
export async function selectLatestInstance(directory: string, seedFile?: string): Promise<ConnectionConfig> {
  const files = new Set<string>();
  if (seedFile) files.add(path.resolve(seedFile));
  try {
    for (const name of fs.readdirSync(directory)) {
      if (/^inst_[\w-]+\.json$/.test(name)) files.add(path.resolve(directory, name));
    }
  } catch { /* A custom seed file may still be usable. */ }
  const candidates: Array<{ file: string; modified: number }> = [];
  for (const file of files) {
    try {
      const info = fs.lstatSync(file);
      if (info.isFile() && !info.isSymbolicLink() && info.size <= 64 * 1024)
        candidates.push({ file, modified: info.mtimeMs });
    } catch { /* Files may disappear during LMMS shutdown. */ }
  }
  candidates.sort((a, b) => b.modified - a.modified || a.file.localeCompare(b.file));
  for (const candidate of candidates) {
    let probe: BridgeClient | undefined;
    try {
      const config = loadConnectionConfig(candidate.file);
      probe = new BridgeClient({ ...config, requestedPermissions: ["read"] });
      await probe.connect();
      return config;
    } catch { /* Skip stale endpoints and invalid files, without printing secrets. */ }
    finally { probe?.close(); }
  }
  throw new LmmsBridgeError({ code: ErrorCode.NotConnected,
    message: "No live LMMS instance found. Open LMMS with MCP enabled; the next call will discover it automatically." });
}
