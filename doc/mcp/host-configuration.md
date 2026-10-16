# Host-Konfiguration für LMMS MCP

## Übersicht
Der LMMS-Begleitprozess `lmms-mcp` kommuniziert über Standard-I/O (`stdio`) mit dem jeweiligen MCP-Host.

## 1. Voraussetzungen
- Ein laufendes LMMS mit aktivierter Option „KI-Steuerung / MCP“.
- Die von LMMS automatisch generierte Verbindungsdatei (z.B. `%LOCALAPPDATA%\LMMS\mcp\connections\studio.json` unter Windows bzw. `~/.local/share/LMMS/mcp/connections/studio.json` unter Linux).

## 2. Konfigurationsbeispiele

### Claude Desktop (`claude_desktop_config.json`)
```json
{
  "mcpServers": {
    "lmms": {
      "command": "node",
      "args": [
        "c:/Users/zfzfg/Documents/HammerMegaProjekte/lmms/lmms-mcp/tools/mcp/dist/index.js",
        "--connection-file",
        "c:/Users/zfzfg/AppData/Local/LMMS/mcp/connections/studio.json"
      ]
    }
  }
}
```

### Antigravity IDE (`mcp_config.json`)
```json
{
  "mcpServers": {
    "lmms": {
      "command": "node",
      "args": [
        "${workspaceFolder}/tools/mcp/dist/index.js",
        "--connection-file",
        "${env:LOCALAPPDATA}/LMMS/mcp/connections/studio.json"
      ]
    }
  }
}
```

### Eigenständiger Standalone-Launcher (Windows Paket)
Im installierten Zustand wird die mitgelieferte `lmms-mcp.exe` oder `launcher.cmd` verwendet:
```json
{
  "mcpServers": {
    "lmms": {
      "command": "C:\\Program Files\\LMMS\\lmms-mcp.exe",
      "args": [
        "--connection-file",
        "C:\\Users\\BENUTZER\\AppData\\Local\\LMMS\\mcp\\connections\\studio.json"
      ]
    }
  }
}
```

## 3. Umgebungsvariablen
- `LMMS_MCP_CONNECTION_FILE`: Pfad zur Standard-Verbindungsdatei, falls `--connection-file` nicht in argv übergeben wurde.
- `LMMS_MCP_DEBUG`: Wenn auf `1` gesetzt, werden erweiterte Trace-Logs auf `stderr` ausgegeben. `stdout` bleibt strikt für MCP-Frames reserviert.
