# MCP-Host konfigurieren

LMMS startet die Bridge nach der Projektinitialisierung. `--no-mcp` deaktiviert sie, `WANT_MCP=OFF` entfernt sie beim Build. Die Verbindungsansicht in der Hauptoberfläche zeigt Status, tatsächliche Adresse, Rechte und den Pfad der instanzeigenen Verbindungsdatei. Diese Datei enthält ein Secret und gehört ausschließlich dem aktuellen Benutzer.

Der Companion folgt standardmäßig der neuesten erreichbaren LMMS-Instanz. Er sortiert die instanzeigenen Verbindungsdateien nach Veröffentlichungszeit und prüft ihre Endpunkte mit einer authentifizierten Leseverbindung. Veraltete Dateien werden übersprungen. Bei mehreren offenen Fenstern ist die zuletzt veröffentlichte, noch laufende Sitzung das Ziel. Der nächste Domainaufruf prüft die Auswahl erneut; ein LMMS-Neustart benötigt keinen manuellen Dateipfadwechsel.

Ein installiertes Paket enthält den nativen Launcher neben dem LMMS-Programm und eine private Node-Runtime. Beispiel für Hosts mit `mcpServers`-Konfiguration:

```json
{
  "mcpServers": {
    "lmms": {
      "command": "C:\\Program Files\\LMMS\\lmms-mcp.exe",
      "args": ["--connection-directory", "C:\\Users\\BENUTZER\\AppData\\Roaming\\lmms\\mcp\\connections"]
    }
  }
}
```

Der tatsächliche Installationspfad richtet sich nach dem Paket. Unter Linux heißt das Programm `lmms-mcp`, im macOS-Bundle liegt es in `Contents/MacOS`. Für einen reinen Leser `--read-only` ergänzen. `--connection-directory` beziehungsweise `LMMS_MCP_CONNECTION_DIRECTORY` legt den Suchordner fest. Ohne Angabe verwendet der Companion den Plattformstandard. Die aus LMMS kopierte Hostkonfiguration enthält den tatsächlichen Ordner. Bestehende `--connection-file`-/`LMMS_MCP_CONNECTION_FILE`-Angaben dienen als Startdatei und wählen zusätzlich aus deren Ordner. Für eine feste Instanz `--pin-instance --connection-file <pfad>` verwenden; nur dieser Modus erfordert nach einem Neustart eine neue Datei.

Für Entwicklung:

```sh
npm --prefix tools/mcp ci --ignore-scripts
npm --prefix tools/mcp run build
node /absolute/repo/tools/mcp/dist/src/index.js --connection-file /absolute/instance.json
```

Discovery und Schemata sind auch bei ausgeschaltetem LMMS verfügbar. Ein Domainaufruf liefert dann `NOT_CONNECTED`. Mehrere gleichzeitige erste Aufrufe teilen den Verbindungsaufbau; nach Verbindungsverlust wird neu authentifiziert. Es wird kein Änderungsaufruf automatisch mit einem neuen Operationsschlüssel wiederholt.

Ist LMMS geschlossen, bleibt der Companion erreichbar und meldet `NOT_CONNECTED`; nach dem Öffnen findet der nächste Aufruf die neue Instanz automatisch. Bei belegtem Schreibzugang zur neuesten Instanz erfolgt kein Ausweichen auf ein älteres Projekt. Ein laufender Aufruf wird für einen Instanzwechsel nicht abgebrochen; weitere Aufrufe erhalten gegebenenfalls `BUSY`. Nach einem Wechsel Projekt und Revision neu lesen. Bereits gesendete Mutationen werden niemals automatisch wiederholt. Bei Verbindungsverlust bleibt deren Ergebnis zunächst unbekannt.

Pro Instanz ist eine Schreibverbindung reserviert; weitere Hosts können mit `--read-only` lesen. Die nativen Rechte sind maßgeblich. Ändert der Benutzer sie, verlieren bestehende Verbindungen und Vorschauen ihre Freigabe und müssen neu verbunden beziehungsweise erstellt werden.

Für einen verlorenen Commit-Rückkanal denselben `operationKey` und identische Argumente verwenden oder `lmms_get_operation` abfragen. Ergebnisse bleiben innerhalb derselben laufenden LMMS-Instanz mindestens 24 Stunden erhalten. Nach LMMS-Neustart gilt eine neue Instanzidentität. Für Undo/Redo immer die aktuell gelesene Projekt-/Revisionsbindung und den passenden Historienkopf verwenden.

Die Hostbeispiele sind Konfigurationsvorlagen. Die hier tatsächlich ausgeführten Referenztests und noch offenen Plattformabnahmen stehen im [Abnahmenachweis](implementation-status.md).
