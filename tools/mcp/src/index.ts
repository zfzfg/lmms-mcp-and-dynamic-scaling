#!/usr/bin/env node

import { loadConnectionConfig, BridgeClient, getDefaultConnectionPath } from "./bridge-client.js";
import { LmmsMcpServer } from "./server.js";
import * as path from "node:path";
import { defaultConnectionDirectory, selectLatestInstance } from "./instance-selection.js";

function printHelp(): void {
  process.stderr.write(`LMMS MCP Companion Server
Usage:
  lmms-mcp [options]

Options:
  --connection-directory <path>  Follow the newest live instance in this directory
  --connection-file <path>       Use this file and its directory for automatic discovery
  --pin-instance                 Use only --connection-file or LMMS_MCP_CONNECTION_FILE
  --read-only                Request a reader connection without occupying the writer slot
  -h, --help                 Show this help message
  -v, --version              Show version information
`);
}

async function main(): Promise<void> {
  const args = process.argv.slice(2);
  let connectionFilePath: string | undefined;
  let readOnly = false;
  let connectionDirectory: string | undefined;
  let pinned = false;

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
    } else if (arg === "--pin-instance") {
      pinned = true;
    } else if (arg === "--connection-directory") {
      if (!args[i + 1] || args[i + 1].startsWith("--")) {
        process.stderr.write("Error: --connection-directory requires a directory.\n"); process.exit(1);
      }
      connectionDirectory = args[++i];
    } else if (arg === "--connection-file") {
      if (i + 1 < args.length && !args[i + 1].startsWith("--")) {
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
    if (pinned && connectionDirectory) throw new Error("--pin-instance cannot be combined with --connection-directory.");
    if (connectionDirectory && connectionFilePath) throw new Error("Choose --connection-directory or --connection-file.");
    const seed = connectionFilePath ?? process.env.LMMS_MCP_CONNECTION_FILE;
    const directory = connectionDirectory ?? (seed ? path.dirname(path.resolve(seed)) : defaultConnectionDirectory());
    if (pinned && !seed) getDefaultConnectionPath();
    const config = async () => {
      const value = pinned ? loadConnectionConfig(seed) : await selectLatestInstance(directory, seed);
      if (readOnly) value.requestedPermissions = ["read"];
      return value;
    };


    const bridgeClient = new BridgeClient(config, !pinned);
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
