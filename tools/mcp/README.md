# LMMS MCP companion

The companion exposes 22 tools, five resource templates and a composition prompt over stdio. It uses the official SDK v2 (`@modelcontextprotocol/server` 2.3.1), supporting MCP revisions 2026-07-28 and 2025-11-25. Tool and resource discovery works without a running LMMS process; the selected instance is contacted on the first domain request.

Installed packages include the native `lmms-mcp` launcher, Node 24.19.0, production dependencies and a license/version manifest. They do not require global Node or npm. Select the instance's connection file explicitly; LMMS shows its actual path in the connection dialog. MCP is enabled by default; `--no-mcp` disables it.

```json
{"mcpServers":{"lmms":{"command":"/absolute/path/to/lmms-mcp","args":["--connection-file","/absolute/path/to/inst_UUID.json"]}}}
```

Use `--read-only` for an observer connection. Only one writer may connect to an instance. Changing native permissions revokes existing connections and previews immediately.

Development (Node 24.19.0 and npm):

```sh
npm ci --ignore-scripts
npm test
npm start -- --help
npm start -- --connection-file /absolute/path/to/inst_UUID.json
npm run package -- --out /absolute/path/to/generated-stage
```

The generated entry point is `dist/src/index.js`. CMake packages the companion when `WANT_MCP=ON`; `OFF` excludes it and Qt Network. Generated stages contain their own `bin/node[.exe]`. The wrappers in `packaging/` expect to be placed in the stage root; production installations use the native launcher.

Contracts originate in `src/contracts.ts` and are exported with `npm run contracts`. Native argument validation embeds the generated JSON schemas. `npm test` rejects schema drift and runs actual stdio sessions for both protocol revisions, plus IPC framing/reconnect and schema tests.

See [host setup](../../doc/mcp/host-configuration.md), [protocol](../../doc/mcp/protocol.md), [security](../../doc/mcp/security.md) and [current acceptance evidence](../../doc/mcp/implementation-status.md). Full application, audible rendering and platform package acceptance remain separate gates; unit tests alone do not establish those outcomes.

For AI agents operating LMMS, the English [LMMS MCP skill suite](../../skills/lmms-ai/SKILL.md) provides an entrypoint and seven focused skills for connection setup, editing, composition, instruments/assets, mixing/automation, rendering/export and troubleshooting. Install the eight sibling skill directories from `skills/` together in your agent's skill directory (for Codex, `$CODEX_HOME/skills` or `~/.codex/skills` when unset), preserving their relative layout. Automatic selection remains enabled; invoke `$lmms-ai` or a focused skill directly when needed. Skill examples and scenario checks do not replace native application or audio acceptance.
