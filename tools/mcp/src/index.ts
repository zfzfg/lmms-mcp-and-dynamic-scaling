#!/usr/bin/env node

import { loadConnectionConfig, BridgeClient, getDefaultConnectionPath } from "./bridge-client.js";
import { LmmsMcpServer } from "./server.js";

function printHelp(): void {
  process.stderr.write(`LMMS MCP Companion Server
Usage:
  lmms-mcp [options]

Options:
  --connection-file <path>   Choose an instance using its connection file or LMMS_MCP_CONNECTION_FILE
  --read-only                Request a reader connection without occupying the writer slot
  -h, --help                 Show this help message
  -v, --version              Show version information
`);
}

async function main(): Promise<void> {
  const args = process.argv.slice(2);
  let connectionFilePath: string | undefined;
  let readOnly = false;

  for (let i = 0; i < args.length; i++) {
    const arg = args[i];
    if (arg === "--help" || arg === "-h") {
      printHelp();
      process.exit(0);
    } else if (arg === "--version" || arg === "-v") {
      process.stderr.write("lmms-mcp version 1.1.0\n");
      process.exit(0);
    } else if (arg === "--read-only") {
      readOnly = true;
    } else if (arg === "--connection-file") {
      if (i + 1 < args.length) {
        connectionFilePath = args[++i];
      } else {
        process.stderr.write("Error: --connection-file requires a file path argument.\n");
        process.exit(1);
      }
    } else {
      process.stderr.write(`Unknown argument: ${arg}\n`); process.exit(2);
    }
  }

  try {
    const config = () => { const value = loadConnectionConfig(connectionFilePath); if (readOnly) value.requestedPermissions = ["read"]; return value; };


    const bridgeClient = new BridgeClient(config);
    const mcpServer = new LmmsMcpServer(bridgeClient);

    await mcpServer.start();
  } catch (err: unknown) {
    const msg = (err as Error).message || String(err);
    process.stderr.write(`[lmms-mcp] Startup error: ${msg}\n`);
    process.stderr.write(`[lmms-mcp] Make sure LMMS is running and MCP control is active.\n`);
    process.stderr.write(`[lmms-mcp] Looked for connection file at: ${connectionFilePath || process.env.LMMS_MCP_CONNECTION_FILE || "(no instance selected)"}\n`);
    process.exit(1);
  }
}

main();
