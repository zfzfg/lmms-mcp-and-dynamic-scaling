# LMMS MCP Protokoll-Spezifikation

## Übersicht

Die LMMS-MCP-Integration umfasst zwei Protokollebenen:
1. **Host <-> Begleitprozess:** Standardisiertes Model Context Protocol (MCP) über `stdio`.
2. **Begleitprozess <-> LMMS:** Internes, versioniertes JSON-RPC über lokalen IPC (Windows Named Pipe / Unix Domain Socket).

---

## 1. Host-Protokoll (MCP über stdio)

### Unterstützte Revisionen
- **2026-07-28 (Latest):** Unterstützt `server/discover`, Request-Metadaten, Typisierung.
- **2025-11-25 (Legacy):** Vollständig abwärtskompatibel mit `initialize` und `notifications/initialized`.

### Tools (Übersicht)
| Tool | Modus | Zweck |
| --- | --- | --- |
| `lmms_get_status` | Read | Abfrage von Version, Instanz-ID, Bereitschaft und Rechten |
| `lmms_get_project` | Read | Metadaten, Tempo, Taktart, Dirty-Flag, Revision |
| `lmms_list_tracks` | Read | Paginierte Liste der Spuren (Song oder PatternStore) |
| `lmms_list_clips` | Read | Paginierte Liste der Clips in einer Spur |
| `lmms_get_notes` | Read | Noten eines Clips mit clipRevision und kurzlebigen Referenzen |
| `lmms_list_parameters` | Read | Registrierte Kernparameter eines Objekts |
| `lmms_list_instruments` | Read | Verfügbare interne Synthesizer- und Plugin-Deskriptoren |
| `lmms_get_mixer` | Read | Mixerkanäle, Routing und Sends |
| `lmms_preview_changes` | Read/Plan | Validiert Änderungssatz und erzeugt gebundenen `planId` mit Hash |
| `lmms_apply_changes` | Write | Führt einen vorbereiteten Plan atomar aus (mit `operationKey`) |
| `lmms_set_transport` | Action | Steuert Transport (`play`, `pause`, `stop`, `seek`) |
| `lmms_undo` | Action | Macht die letzte Journalgruppe rückgängig |
| `lmms_redo` | Action | Wiederholt die rückgängig gemachte Journalgruppe |
| `lmms_get_operation` | Read | Abfrage des Status eines zuvor ausgeführten `operationKey` |
| `lmms_create_instrument_track`| Write | Erstellt eine neue Spur mit internem Instrument |
| `lmms_load_asset` | Action | Lädt katalogisiertes Preset oder Sample |
| `lmms_save_project_copy` | Action | Speichert Projektkopie im Exportordner |
| `lmms_start_render` | Action | Startet asynchronen Snapshot-Renderjob |
| `lmms_get_job` | Read | Status und Artefaktmetadaten eines Jobs |
| `lmms_cancel_job` | Action | Bricht einen laufenden Renderjob ab |

### Ressourcen
- `lmms://instances/{instanceId}/projects/{projectId}/summary`
- `lmms://instances/{instanceId}/projects/{projectId}/tracks/{trackId}`
- `lmms://instances/{instanceId}/projects/{projectId}/clips/{clipId}/summary`
- `lmms://instances/{instanceId}/capabilities`
- `lmms://instances/{instanceId}/artifacts/{artifactId}`

---

## 2. Internes IPC-Protokoll (Begleitprozess <-> LMMS)

### Framing
Jede Nachricht besteht aus:
- **Header:** 4 Bytes vorzeichenloser 32-Bit Integer (Big-Endian) für die Frame-Länge in Bytes.
- **Payload:** UTF-8-kodierter JSON-String der exakten Header-Länge.
- **Maximale Frame-Größe:** 1.048.576 Bytes (1 MiB). Größere Frames werden mit Fehler verworfen und die Verbindung getrennt.

### Handshake
Nach dem Verbindungsaufbau sendet der Client als ersten Frame:
```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "auth.handshake",
  "params": {
    "clientVersion": "1.0.0",
    "ipcProtocolVersion": "1.0",
    "instanceId": "inst_7f42",
    "authToken": "a8f3b49c..."
  }
}
```

Antwort von LMMS:
```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "result": {
    "ok": true,
    "instanceId": "inst_7f42",
    "bridgeVersion": "1.3.0-alpha",
    "permissions": ["read", "write_project", "export"],
    "ready": true
  }
}
```

### Request-Format
```json
{
  "jsonrpc": "2.0",
  "id": 2,
  "method": "domain.call",
  "params": {
    "action": "preview_changes",
    "deadlineMs": 5000,
    "payload": {
      "projectId": "proj_123",
      "expectedRevision": 42,
      "changes": [...]
    }
  }
}
```

### Fehlerbehandlung
Standardisierte Domänenfehler:
- `NOT_CONNECTED`: LMMS ist nicht erreichbar.
- `NOT_READY`: LMMS ist noch in der Initialisierung oder beendet sich.
- `PERMISSION_DENIED`: Fehlende Rechte für die angeforderte Aktion.
- `WRITER_BUSY`: Eine andere Schreiboperation oder exklusiver Lock ist aktiv.
- `REVISION_CONFLICT`: Das Projekt wurde zwischen Vorschau und Anwendung geändert.
- `VALIDATION_ERROR`: Ungültige Parameter oder Notenwerte außerhalb 0..127.
- `PLAYBACK_ACTIVE`: Strukturelle Änderungen sind während der Wiedergabe gesperrt.
- `PATH_NOT_ALLOWED`: Pfad liegt außerhalb freigegebener Verzeichnisse.
