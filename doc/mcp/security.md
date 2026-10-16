# Sicherheitskonzept für LMMS MCP

## 1. Bedrohungsmodell und Annahmen

LMMS wird als Desktop-Software lokal auf dem Benutzerrechner ausgeführt. Die MCP-Schnittstelle dient der kontrollierten Steuerung durch KI-Modelle.
Es gelten folgende Bedrohungsannahmen:
- **Unberechtigte Fremdprozesse:** Andere lokale Prozesse oder Netzwerkdienste dürfen sich nicht unberechtigt auf die LMMS-Bridge aufschalten.
- **Fehlgeleitete oder bösartige LLM-Anweisungen:** Ein LLM (z.B. durch Prompt Injection in Songtexten oder Projektkommentaren) darf kein unkontrolliertes Ausführen von Schadcode, kein Überschreiben beliebiger Systemdateien und keine Endlosschleifen im Audiothread verursachen.

## 2. Schutzmaßnahmen

### 2.1 Authentifizierung & Instanzbindung
- Jede LMMS-Instanz generiert beim Start eine zufällige GUID (`instanceId`) und ein kryptografisches Secret (`authToken`, 32 Bytes hex / CSPRNG).
- Dieses Token wird in einer temporären Verbindungsdatei gespeichert, deren Zugriffsrechte auf den aktuellen Betriebssystem-Benutzer beschränkt sind.
- Die lokale Named Pipe (`\\.\pipe\lmms-mcp-<instanceId>`) bzw. der Unix-Socket wird mit `QLocalServer::UserAccessOption` erstellt.
- Verbindungsversuche ohne gültiges Token werden sofort geschlossen.

### 2.2 Berechtigungsprofile (Permissions)
LMMS erzwingt Berechtigungsprüfungen nativ im Hauptthread vor jeder Operation:
1. `read`: Projektstatus, Spuren, Clips, Noten und Mixer lesen.
2. `write_project`: Erstellen von MIDI-Clips, Notenänderungen, Parametereinstellungen im geöffneten Projekt.
3. `assets`: Zugriff auf freigegebene Sample- und Preset-Kataloge.
4. `export`: Speichern von Projektkopien und Rendern von Audio in dedizierten Exportverzeichnissen.
5. `manage_files`: Datei überschreiben / Projekt neu öffnen (gesondert abzusichern).

### 2.3 Dateisystem-Richtlinie (AiFilePolicy)
- Zugriff ist ausschließlich auf explizit freigegebene Verzeichnisse (Projektordner, Sample-Bibliotheken, Export-Ordner) beschränkt.
- Alle Pfade werden vor Zugriff kanonisiert (`std::filesystem::canonical` / `QFileInfo::canonicalFilePath`).
- Erkennung und Verweigerung von:
  - Directory Traversal (`..`)
  - Symlinks / Reparse Points / Windows Junctions außerhalb des Zielbaums
  - Ungültigen Windows-Gerätenamen (`CON`, `PRN`, `AUX`, `NUL`, `COM1..9`, `LPT1..9`)
  - Alternate Data Streams (`file.txt:stream`)
- Standardmäßig wird kein Überschreiben existierender Dateien ohne atomare Publish-Prüfung zugelassen.

### 2.4 Audiothread-Schutz & DoS-Prävention
- JSON-Parsing, Dateisystemprüfungen und Validierungen finden ausschließlich im IPC-Thread oder Hauptthread statt.
- Begrenzte Queue: Maximal 64 ausstehende Befehle; bei Überlastung wird mit `BUSY` abgelehnt.
- Maximale Batch-Größe: Maximal 64 Befehle bzw. 512 Noten pro Transaktion.
- Frame-Limit: 1 MiB maximale Nachrichtengröße auf der IPC-Schnittstelle.
- Absolute Timeouts: Schreiboperationen besitzen eine standardmäßige Deadline von 5 Sekunden.
