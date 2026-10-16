#!/usr/bin/env node

import { loadConnectionConfig, BridgeClient, getDefaultConnectionPath } from "./bridge-client.js";
import { LmmsMcpServer } from "./server.js";

function printHelp(): void {
  process.stderr.write(`LMMS MCP Companion Server
Usage:
  lmms-mcp [options]

Options:
  --connection-file <path>   Path to LMMS connection file (defaults to AppData/LMMS/mcp/connections/studio.json)
  -h, --help                 Show this help message
  -v, --version              Show version information
`);
}

async function main(): Promise<void> {
  const args = process.argv.slice(2);
  let connectionFilePath: string | undefined;

  for (let i = 0; i < args.length; i++) {
    const arg = args[i];
    if (arg === "--help" || arg === "-h") {
      printHelp();
      process.exit(0);
    } else if (arg === "--version" || arg === "-v") {
      process.stderr.write("lmms-mcp version 1.0.0\n");
      process.exit(0);
    } else if (arg === "--connection-file") {
      if (i + 1 < args.length) {
        connectionFilePath = args[++i];
      } else {
        process.stderr.write("Error: --connection-file requires a file path argument.\n");
        process.exit(1);
      }
    }
  }

  try {
    const config = loadConnectionConfig(connectionFilePath);
    process.stderr.write(`[lmms-mcp] Loaded configuration for instance ${config.instanceId}\n`);

    const bridgeClient = new BridgeClient(config);
    const mcpServer = new LmmsMcpServer(bridgeClient);

    await mcpServer.start();
  } catch (err: unknown) {
    const msg = (err as Error).message || String(err);
    process.stderr.write(`[lmms-mcp] Startup error: ${msg}\n`);
    process.stderr.write(`[lmms-mcp] Make sure LMMS is running and MCP control is active.\n`);
    process.stderr.write(`[lmms-mcp] Looked for connection file at: ${connectionFilePath || getDefaultConnectionPath()}\n`);
    process.exit(1);
  }
}

main();
