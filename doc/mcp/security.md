# Sicherheitsgrenzen der MCP-Bridge

Die Bridge ist ein lokaler Zugang zur laufenden LMMS-Instanz. Ihre Verbindungsdatei enthält ein Secret. Prozesse desselben angemeldeten Benutzers sind keine voneinander isolierten Sicherheitsdomänen; die Bridge schafft keine Sandbox gegen einen bereits kompromittierten Benutzeraccount.

Der lokale Server verwendet `QLocalServer::UserAccessOption`, eine zufällige Instanzidentität und ein pro Start erneuertes Token. Verbindungsdateien werden vor dem Schreiben des Secrets auf Besitzerzugriff beschränkt (Unix 0600, Windows geschützte Benutzer-DACL) und atomar ohne Ersetzen veröffentlicht. Beenden entfernt nur die zur eigenen Instanz passende Datei. Die tatsächliche Qt-Endpunktadresse wird veröffentlicht.

Native Profile bestehen aus `read`, `write_project`, `assets` und `export`. Standardmäßig bleiben die im Fork vorgesehenen Rechte aktiv. `manage_files` ist reserviert und eröffnet keinen unbeschränkten Dateizugriff. Die Verbindungseinstellungen zeigen den aktuellen Zustand; Änderungen widerrufen bestehende Sessions und Pläne. Der Handshake kann ein kleineres Profil anfordern, insbesondere `--read-only`. Nur eine Schreibverbindung ist erlaubt. Verbindungsgültigkeit, aktuelle Rechte, Deadline und Projektbindung werden vor der Ausführung erneut geprüft.

IPC läuft im eigenen Thread. Modelloperationen laufen im Anwendungsthread; Antworten verwenden schwache QObject-Handles und einen Session-Gültigkeitszustand. Beim Shutdown werden neue Aufträge gesperrt, wartende verworfen und Renderworker beendet. Limits schützen Frames, Puffer, Verbindungen, wartende Anfragen, Operationsregister, Pläne, Notenbatches und Jobs; siehe [Protokollvertrag](protocol.md).

Assets werden ausschließlich über undurchsichtige Katalog-IDs aus den konfigurierten Benutzer-Preset-/Samplebibliotheken geladen. Pfade und Metadaten werden erneut geprüft. XML und interne Pluginfreigabe werden vor dem Laden validiert; externe Plugins werden über diesen Ladepfad nicht freigegeben. Ein fehlgeschlagener Instrumentaufbau wird zurückgerollt.

Projektkopien und Exporte erhalten neue Namen im freigegebenen Exportverzeichnis. Pfadkomponenten, Windows-Sondernamen/ADS und kanonische Wurzeln werden geprüft. Bei Veröffentlichung wird der tatsächlich geöffnete Zielhandle beziehungsweise Verzeichnisdeskriptor verifiziert und exklusiv neu angelegt; vorhandene Ziele werden nicht verändert. Die eigene temporäre Datei wird bei einem Fehler entfernt. Snapshot-Ressourcen werden privat kopiert und gehasht. Diese Grenzen sind Teil der Tests; plattformübergreifende Junction-/Race-Abnahme ist noch offen.

Der Companion übermittelt Projekt-/Audiodaten nur an den angeschlossenen MCP-Host. Ob dessen KI-Anbieter Daten extern verarbeitet, hängt vom Host ab. `stdout` ist ausschließlich MCP vorbehalten; Diagnose geht nach `stderr` und in den begrenzten nativen Statuspuffer. Tokens werden nicht geloggt.

Die Integration bietet weder beliebige Skriptausführung noch allgemeine Dateiverwaltung. Vollständige Audio-, Lebensdauer- und Paketabnahme wird erst nach den im [Abnahmenachweis](implementation-status.md) dokumentierten Prüfungen behauptet.
