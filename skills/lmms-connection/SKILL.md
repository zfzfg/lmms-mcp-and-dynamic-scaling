---
name: lmms-connection
description: "Configure or diagnose an MCP host connection to LMMS: choose the intended instance connection file, use the companion launcher, check readiness and understand reader, writer, asset and export permissions. Use for LMMS MCP setup, not implementing an MCP server."
---

# LMMS Connection

## Establish which layer is available

- If the host exposes LMMS tools, start with `lmms_get_status`. A successful discovery can coexist with `NOT_CONNECTED` on every domain call.
- If there are no LMMS tools, inspect the host's configured MCP servers and launcher diagnostics. Do not substitute another application's connector or implement a raw IPC client.
- If LMMS is connected but `ready` is false, inspect `state`, `lastError` and `diagnostics`. Wait for project initialization or loading to finish before project calls.
- Use [host setup](references/host-setup.md) for concrete launcher configuration, platform paths, read-only mode and reconnection.

## Select the intended instance

The current companion automatically follows the newest reachable session in its configured connection directory, including after LMMS restarts. Confirm the returned instance and project before editing. Use `--connection-directory` with the directory from LMMS or `--pin-instance --connection-file` when the user wants a fixed session. Legacy file arguments also discover newer sessions in the same directory unless pinned.

The file contains an authentication secret. The companion reads it; diagnostic output and examples should contain only the path and redacted information, never `authToken` or the complete file.

The bridge starts after project initialization. `--no-mcp` disables it; builds made with `WANT_MCP=OFF` do not include it. A skill cannot enable a bridge absent from the installed application.

## Check the effective permission profile

All domain calls require `read`. Additional native permissions are:

| Actions | Additional permissions |
| --- | --- |
| Apply edits, create an instrument, transport, undo/redo | `write_project` |
| List catalog assets | `assets` |
| Load a catalog asset | `assets` and `write_project` |
| Save a project copy, start/read/cancel a job, retrieve audio | `export` |

`lmms_preview_changes` validates without changing the project; a preview does not grant permission to apply it. `manage_files` is reserved and does not provide arbitrary filesystem access.

Only one connection per instance may hold `write_project`. Additional observers can use `--read-only`, which requests only `read`; asset and export operations may therefore be unavailable to that connection too.

Permission changes invalidate existing sessions and previews. Reconnect through the host and read status again; do not repeatedly resend rejected mutations or change native permissions without user authorization.

For an uncertain write during reconnection, read the shared [session and transaction contract](../lmms-ai/references/session-and-transactions.md). For error-specific decisions, read [lmms-troubleshooting](../lmms-troubleshooting/SKILL.md).
