# LMMS MCP Companion Server (`lmms-mcp`)

The Model Context Protocol (MCP) companion server provides a standard stdio interface for AI hosts (Claude Desktop, Antigravity IDE, Cursor, Open-WebUI, etc.) to inspect and control a running LMMS session.

## Features
- **Project Inspection:** Read project metadata, tempo, time signatures, tracks, MIDI clips, notes, parameters and mixer routing.
- **Musical Edits:** Plan and commit note edits, transposition, clip creation and parameter adjustments with atomic single-step Undo/Redo.
- **Idempotency:** Unique `operationKey` guarantees safe retries without duplicating notes.
- **Isolated Rendering:** Asynchronous background snapshot rendering to verified WAV files.

## Setup & Running

1. Enable "KI-Steuerung / MCP" in LMMS settings or run LMMS with `--mcp`.
2. Connect your MCP host using the generated connection file:
   ```json
   {
     "mcpServers": {
       "lmms": {
         "command": "node",
         "args": [
           "<path-to-lmms>/tools/mcp/dist/index.js",
           "--connection-file",
           "<path-to-appdata>/LMMS/mcp/connections/studio.json"
         ]
       }
     }
   }
   ```
