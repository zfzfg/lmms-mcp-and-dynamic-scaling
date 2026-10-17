# Host setup and connection checks

Use this reference when tools are missing, the host is being configured, or the intended LMMS instance cannot be reached.

## Installed launcher

The companion is an MCP stdio server. Installed packages include a native launcher, private Node runtime and dependencies; a global Node installation is not required for those packages.

The following is a configuration template for hosts supporting `mcpServers`, not a universal host configuration format. Use the actual installation and connection-file paths:

```json
{
  "mcpServers": {
    "lmms": {
      "command": "C:/Program Files/LMMS/lmms-mcp.exe",
      "args": [
        "--connection-directory",
        "C:/Users/USER/AppData/Roaming/lmms/mcp/connections"
      ]
    }
  }
}
```

On Linux, point `command` at the installed `lmms-mcp` executable. In a macOS bundle, it is under `LMMS.app/Contents/MacOS/lmms-mcp`. The actual installation directory depends on the package.

For an observer add `--read-only` to the args. Without the CLI connection-file argument, the companion can read the path from `LMMS_MCP_CONNECTION_FILE`. Do not assume a host automatically supplies that environment variable.

## Obtain the instance file

LMMS shows its actual endpoint, granted permissions and connection-file path in the MCP connection dialog. The default directory is Qt's application-specific `AppDataLocation/mcp/connections`; do not hard-code an assumed OS folder.

Each running instance publishes its own `inst_<UUID>.json`. The companion selects the newest reachable session by publication time and authenticated reader probe on each domain call. Stale files are skipped. Use `--pin-instance --connection-file <path>` to opt into a fixed session. A busy newest writer never redirects writes to an older project.

LMMS also accepts `--mcp-connection-file` with a new absolute filename at startup. Existing files are not replaced. This is an application-startup option, distinct from the companion's `--connection-file`.

Do not read or print the secret merely to test connectivity. Let the companion open the file and authenticate. Record only redacted diagnostics and the path.

## Confirm the connection through domain calls

Example call notation labels the tool; only `arguments` are passed to the host tool.

```json
{
  "tool": "lmms_get_status",
  "arguments": {}
}
```

Check the returned instance identity, `ready`, effective permissions and capabilities. If not ready, inspect native state and diagnostics before reading project data.

```json
{
  "tool": "lmms_get_project",
  "arguments": {}
}
```

A successful response supplies the current `projectId` and `projectRevision`. Bind subsequent work to these, not to a project filename.

Discovery can work while LMMS is closed. An MCP server appearing in the host, or its launcher starting, does not establish that these calls will succeed.

## Reconnect without duplicating work

The companion contacts LMMS on its first domain request and reauthenticates after disconnects. Native permission changes revoke existing sessions and previews. Use the host's reconnect/restart mechanism when necessary and read status again.

After an LMMS restart the instance ID and token change. Automatic mode discovers the new session on the next call, without restarting the companion. Only pinned mode needs a new file. Read project identity and revision again before editing. A companion restart against the same still-running instance can query retained operations.

Resolve a possibly executed mutation using its original operation key before continuing. Read [the recovery contract](../../lmms-ai/references/session-and-transactions.md) for exact retry decisions.

## Development-only launch

Use an installed launcher when available. If the user is deliberately working from a source checkout, the documented development entry point is:

```sh
npm --prefix tools/mcp ci --ignore-scripts
npm --prefix tools/mcp run build
node /absolute/repo/tools/mcp/dist/src/index.js --connection-file /absolute/instance.json
```

The commands require the checkout and its supported Node/npm environment. They are setup instructions, not a required dependency for the installed skill suite.

Protocol stdout is reserved for MCP. Check launcher/host stderr or LMMS diagnostics for failures; do not add logging to protocol stdout or expose authentication tokens.
