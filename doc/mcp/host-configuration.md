# MCP-Host konfigurieren

LMMS startet die Bridge nach der Projektinitialisierung. `--no-mcp` deaktiviert sie, `WANT_MCP=OFF` entfernt sie beim Build. Die Verbindungsansicht in der Hauptoberfläche zeigt Status, tatsächliche Adresse, Rechte und den Pfad der instanzeigenen Verbindungsdatei. Diese Datei enthält ein Secret und gehört ausschließlich dem aktuellen Benutzer.

Wähle ausdrücklich die gewünschte Instanzdatei `inst_<UUID>.json`. Der Standardordner liegt unter dem von Qt ermittelten `AppDataLocation/mcp/connections`; der genaue Pfad hängt von Plattform und Anwendungskonfiguration ab. Mit `lmms --mcp-connection-file <neuer-absoluter-pfad>` lässt sich ein fester Dateiname vorgeben. Vorhandene Dateien werden nicht ersetzt. Beim Neustart wechselt die Instanzidentität; verwende die neu veröffentlichte Datei.

Ein installiertes Paket enthält den nativen Launcher neben dem LMMS-Programm und eine private Node-Runtime. Beispiel für Hosts mit `mcpServers`-Konfiguration:

```json
{
  "mcpServers": {
    "lmms": {
      "command": "C:\\Program Files\\LMMS\\lmms-mcp.exe",
      "args": ["--connection-file", "C:\\Users\\BENUTZER\\lmms-studio-connection.json"]
    }
  }
}
```

Der tatsächliche Installationspfad richtet sich nach dem Paket. Unter Linux heißt das Programm `lmms-mcp`, im macOS-Bundle liegt es in `Contents/MacOS`. Für einen reinen Leser `--read-only` ergänzen. Ohne CLI-Argument kann `LMMS_MCP_CONNECTION_FILE` den Dateipfad setzen. Es gibt keine automatische Auswahl einer beliebigen laufenden Instanz.

Für Entwicklung:

```sh
npm --prefix tools/mcp ci --ignore-scripts
npm --prefix tools/mcp run build
node /absolute/repo/tools/mcp/dist/src/index.js --connection-file /absolute/instance.json
```

Discovery und Schemata sind auch bei ausgeschaltetem LMMS verfügbar. Ein Domainaufruf liefert dann `NOT_CONNECTED`. Mehrere gleichzeitige erste Aufrufe teilen den Verbindungsaufbau; nach Verbindungsverlust wird neu authentifiziert. Es wird kein Änderungsaufruf automatisch mit einem neuen Operationsschlüssel wiederholt.

Pro Instanz ist eine Schreibverbindung reserviert; weitere Hosts können mit `--read-only` lesen. Die nativen Rechte sind maßgeblich. Ändert der Benutzer sie, verlieren bestehende Verbindungen und Vorschauen ihre Freigabe und müssen neu verbunden beziehungsweise erstellt werden.

Für einen verlorenen Commit-Rückkanal denselben `operationKey` und identische Argumente verwenden oder `lmms_get_operation` abfragen. Ergebnisse bleiben innerhalb derselben laufenden LMMS-Instanz mindestens 24 Stunden erhalten. Nach LMMS-Neustart gilt eine neue Instanzidentität. Für Undo/Redo immer die aktuell gelesene Projekt-/Revisionsbindung und den passenden Historienkopf verwenden.

Die Hostbeispiele sind Konfigurationsvorlagen. Die hier tatsächlich ausgeführten Referenztests und noch offenen Plattformabnahmen stehen im [Abnahmenachweis](implementation-status.md).
