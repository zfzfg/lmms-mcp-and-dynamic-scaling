# MCP- und IPC-Vertrag

Der Begleitprozess verwendet das offizielle SDK v2, festgeschrieben auf `@modelcontextprotocol/server` 2.3.1. Echte stdio-Referenztests mit `@modelcontextprotocol/client` 2.3.1 decken MCP 2026-07-28 und den Legacy-Pfad 2025-11-25 ab. Discovery benötigt keine LMMS-Verbindung.

## Gemeinsame Schemata

`tools/mcp/src/tools/index.ts` und `tools/mcp/src/contracts.ts` definieren Toolargumente, Ergebnisse und Änderungsbefehle. `npm --prefix tools/mcp run contracts` exportiert die versionierten Fixtures:

- [Toolargumente](schemas/mcp-tools-input.schema.json)
- [Toolergebnisse](schemas/mcp-tools-output.schema.json)
- [Änderungsbefehle](schemas/mcp-commands.schema.json)

CMake bettet dieselben Argument-/Befehlsschemata in den nativen Validator ein. Unbekannte Felder/Befehle, falsche Typen und ungültige Werte werden abgelehnt. Alle 22 Tools veröffentlichen `inputSchema`, `outputSchema` und Annotationen; Domainfehler liefern `isError`, Text und strukturiertes `{ok:false,error:{code,message,retryable}}`.

Neben Projekt-, Spur-, Clip-, Noten-, Parameter-, Instrument- und Mixerabfragen gibt es Assetkatalog und Automation. Produktionsänderungen erfolgen über Preview/Apply oder die dedizierten Instrument-/Assetaktionen. Die vollständigen Argumente stehen in den Schemata; das vermeidet auseinanderlaufende Kopien dieses Vertrags.

## Instanz, Projekt und Historie

Die Verbindungsdatei enthält `instanceId`, die tatsächliche `endpoint`-Adresse, `authToken`, `bridgeVersion` und `ipcProtocolVersion: "1.0"`. Der Client wählt diese Datei ausdrücklich. Er führt parallele Verbindungsversuche zusammen und setzt Empfangspuffer beim Reconnect zurück.

Schreibende Tools verlangen `operationKey`, `projectId` und `expectedRevision`; Jobabbruch benötigt seinen Operationsschlüssel und Job-ID. Preview bindet seinen Plan an Projekt, Revision, Hash und aktuelle Freigabe. Undo/Redo benötigen außerdem `undoGroupId` als erwarteten Kopf des jeweiligen Stacks. Neu/Laden/Recovery invalidiert Projektgeneration, Handles, Notenreferenzen und Pläne.

Pagination verwendet undurchsichtige Cursor, gebunden an Projekt, Revision, Abfrage und Index. Sie sind keine numerischen Offsets. Ungültige/veraltete Cursor werden abgelehnt.

Ressourcen prüfen Instanz und, soweit projektbezogen, Projektidentität. Unterstützt werden Projektübersicht, Spur, Clipübersicht, Fähigkeiten und WAV-Artefakte. Ressourcen verwenden dieselben nativen Rechteprüfungen wie Tools. Artefakte tragen Projektrevision, SHA-256 und tatsächliche WAV-Metadaten.

## IPC 1.0

Jeder Frame besteht aus vier Bytes Big-Endian-Länge und genau dieser Anzahl UTF-8-JSON-Bytes (maximal 1 MiB). Ein Handshake muss vor Domainaufrufen erfolgreich sein:

```json
{"jsonrpc":"2.0","id":1,"method":"auth.handshake","params":{"ipcProtocolVersion":"1.0","clientVersion":"1.1.0","instanceId":"inst_UUID","authToken":"SECRET","requestedPermissions":["read"]}}
```

Ohne `requestedPermissions` wird das native Profil angefordert. Nur eine Verbindung mit `write_project` wird zugelassen. Maximal acht Verbindungen, 64 ausstehende Anfragen je Verbindung und 64 wartende Domainaufträge gelten. Ein Handshake muss innerhalb des nativen Zeitlimits fertig werden.

```json
{"jsonrpc":"2.0","id":2,"method":"domain.call","params":{"action":"get_project","payload":{},"deadlineMs":5000}}
```

Mutationen tragen `operationKey` im Envelope; ein optionaler Schlüssel im Payload muss übereinstimmen. Bei `get_operation` steht der **gesuchte** Schlüssel ausschließlich im Payload und reserviert keinen neuen Auftrag. Deadlines sind 1–30000 ms einschließlich Queuezeit; die Rechte und Verbindungsgültigkeit werden unmittelbar vor Ausführung erneut geprüft.

Schlüssel werden vor dem Einreihen reserviert. Gleiche Argumente teilen Auftrag/Ergebnis; andere Argumente erzeugen `OPERATION_KEY_REUSED`. Laufende und höchstens 1024 innerhalb der letzten 24 Stunden bestätigte Ergebnisse bleiben in derselben LMMS-Instanz erhalten; ist das Register voll, werden neue Aufträge abgelehnt. Das Register ist kein persistentes Protokoll über einen LMMS-Neustart hinweg. Ein Companion-Neustart kann Ergebnisse derselben laufenden Instanz weiter abfragen. Ergebnisabfragen verlangen auch die Rechte der ursprünglichen Aktion.

## Rendering und Limits

Änderungspläne enthalten maximal 64 Befehle und 512 betroffene Noten. Snapshot-Ressourcen sind auf 2000 Einträge, 64 MiB je Datei und 256 MiB insgesamt begrenzt. Ressourcen werden im isolierten Worker kopiert und gehasht; Änderungen seit Vorbereitung führen zum Fehler. Worker starten mit eigener Konfiguration und `--no-mcp`.

Rendering bietet derzeit WAV-Mixdown mit 16/24/32 Bit und 8000–192000 Hz. Höchstens zwei Worker und 128 Jobs sind zugelassen; das Workerbudget beträgt zehn Minuten. Startfehler, Absturz, Abbruch, Timeout und ungültige WAVs sind terminale Fehlerzustände. Die WAV-Prüfung kontrolliert RIFF, Format, Kanäle, Samplerate, Bittiefe, Blockausrichtung, Datenmenge und plausible Dauer. Ein bestehendes Exportziel wird nie ersetzt.

Artefaktdaten sind auf 256 KiB Rohdaten je IPC-Chunk und 16 MiB pro Ressourcenabruf begrenzt. Der Companion prüft die zusammengesetzte Prüfsumme. Größere verifizierte Exporte bleiben als lokale Dateien nutzbar. Stem-Rendering wird nicht als verfügbar angeboten; dessen eigene Routing-/Muteabnahme steht noch aus.

Nachweise und verbleibende Freigabegates: [Implementierungsstatus](implementation-status.md).
