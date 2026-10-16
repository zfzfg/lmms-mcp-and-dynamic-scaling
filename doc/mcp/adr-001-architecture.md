# ADR 001: Architektur der MCP-Integration in LMMS

## Status
Angenommen (Phase 0)

## Kontext
LMMS (Linux MultiMedia Studio) ist eine plattformübergreifende digitale Audio-Workstation (DAW) auf Basis von C++20 und Qt.
Es soll eine bidirektionale Steuerung und Statusabfrage durch Sprachmodelle und KI-Agenten über das Model Context Protocol (MCP) ermöglicht werden.
Folgende Herausforderungen bestehen:
1. **Audiothread-Echtzeitgarantien:** Audioverarbeitung darf keinesfalls durch Netzwerk-/IPC-I/O, JSON-Parsing oder synchrone Sperren blockiert werden.
2. **Protokoll-Evolution:** Die MCP-Spezifikation entwickelt sich stetig (Revision 2026-07-28 mit `server/discover`, Legacy 2025-11-25 mit `initialize`). Das offizielle TypeScript-SDK bietet maximale Konformität und Wartbarkeit.
3. **Prozessarchitektur:** LMMS ist unter Windows als GUI-Subsystem kompiliert und schreibt intern Debug-Ausgaben nach `stdout`. MCP erfordert strikte Einhaltung des `stdio`-Transports ohne vermischte Log-Ausgaben.
4. **Sicherheit und Datenintegrität:** Beliebige Skriptausführung oder unbeschränkter Dateisystemzugriff sind unzulässig. Änderungen müssen transaktional, idempotent und reversibel (Undo/Redo) sein.

## Entscheidung
Wir implementieren eine **zweistufige Hybrid-Architektur**:
1. **LMMS Native Bridge (C++ / Qt):**
   - Läuft innerhalb des LMMS-Prozesses.
   - Bietet einen abgesicherten, lokalen IPC-Endpunkt (`QLocalServer` als Windows Named Pipe `\\.\pipe\lmms-mcp-<instance>` bzw. Unix Domain Socket `/tmp/lmms-mcp-<instance>.sock`).
   - Authentifizierung via kryptografischem Einmal-Token in einer benutzerbeschränkten Verbindungsdatei (`connection.json`).
   - Verwaltet Domänen-Modelle (`Song`, `Track`, `Clip`, `Note`, `Mixer`, `ProjectJournal`).
   - Führt alle Modellmutationen im Qt-Hauptthread mit minimaler RAII-Synchronisation (`AudioEngine::requestChangesGuard()`) aus.
   - Bietet ein atomares Änderungsplan- und Transaktionssystem mit einheitlicher Journalgruppe.

2. **Begleitprozess (`lmms-mcp`, TypeScript / Node):**
   - Dient als externer MCP-Server für den KI-Host über `stdio`.
   - Übersetzt MCP-Tool-Aufrufe und Ressourcen-URIs in interne, typisierte IPC-Aufrufe an LMMS.
   - Unterstützt die Spezifikationsrevisionen 2026-07-28 (`server/discover`) und 2025-11-25 (`initialize`) unter Verwendung des TypeScript-SDKs.
   - Hält `stdout` garantiert frei von Diagnosemeldungen (Diagnose nur auf `stderr`).

3. **IPC-Transportprotokoll:**
   - 32-Bit Big-Endian-Längenpräfix + UTF-8-JSON.
   - Maximal 1 MiB Frame-Größe.
   - Verbindliche Domänen-Fehlercodes (`NOT_READY`, `PERMISSION_DENIED`, `REVISION_CONFLICT`, etc.).

## Konsequenzen
### Positiv
- Klare Trennung von MCP-Protokollhülle und LMMS-Audiointerna.
- Keine Gefährdung der Audio-Engine durch Skripte oder Protokoll-Parsing.
- Vollständige Undo/Redo-Fähigkeit für alle durchgeführten KI-Edits.
- LMMS-GUI bleibt uneingeschränkt nutzbar und synchronisiert sich nahtlos mit KI-Aktionen.

### Negativ / Mehraufwand
- Begleitprozess erfordert gebündelte Auslieferung (z.B. Node-LTS-Runtime oder Standalone-Executable).
- IPC-Synchronisation erfordert sorgfältiges Deadline- und Timeout-Management.
