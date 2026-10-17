# MCP: Implementierung und Abnahmenachweise

Stand: 16. Oktober 2026. Geprüfter Ausgangspunkt: `7c432d420`, `709873202`, `78cb19fe3`. Änderungen liegen im Arbeitsbaum. **Phasen 0–6 sind noch nicht vollständig abgenommen.** Die frühere pauschale Fertigmeldung wurde zurückgenommen. Phase 7 ist nicht Bestandteil dieser Änderung.

## Ergänzung vom 17. Oktober 2026

Die gezielte [MCP-Stabilisierung](connection-incident-2026-10-17.md) ergänzt die folgenden historischen Nachweise: 27/27 Companion-Tests und 17/17 native MCP-/GUI-CTest-Läufe mit vollständig gelinkter Qt-6.8.3-/MSVC-19.51-Anwendung bestanden. Die vollständigen Release-Anwendungsbuilds mit MCP ON und OFF sind erfolgreich. Eine getrennte Anwendung prüft Organic, TripleOscillator und Presetladen über echten IPC einschließlich Undo/Redo. Der Bericht enthält die reproduzierten Absturzpfade und Korrekturen. Die früher genannten offenen Produkt-/Plattformabnahmen werden dadurch nicht pauschal geschlossen.

## Umgesetzte Arbeitspakete

| Phase | Implementierung im Arbeitsbaum | Nachweis / verbleibendes Gate |
| --- | --- | --- |
| 0: Architektur und Verträge | SDK v2 2.3.1, Node 24.19.0, Lockfile, gemeinsame Argument-/Ergebnisschemata, native Einbettung; relative native Runtime-Launcher und Lizenz-/Integritätsmanifest | Companion-Tests und Windows-Staging mit privater Runtime bestanden. Vollständiger Anwendungsbuild und installierte Plattformpakete offen. Eine Upstream-Abstimmung wird nicht als erfolgt behauptet. |
| 1: Lifecycle und Lesen | Ein Dienststart nach Projektinitialisierung; beide Parser behandeln `--no-mcp`; separater IPC-Thread, tatsächliche Endpunktadresse, atomare instanzeigene Verbindungsdatei mit Besitzerrechten; ein Writer; aktuelle Rechte vor Ausführung; Projektgenerationen; Registry; revisionsgebundene Pagination; Projekt-/Track-/Clip-/Noten-/Parameter-/Mixer-/Instrument-/Asset-/Automationabfragen | Native Quellen/MOC isoliert mit Qt 5.15 kompiliert; echte stdio-Discovery ohne LMMS und Client-Reconnect bestanden. Native Session-/Shutdown-/GUI-Tests im vollständigen Programm offen. |
| 2: Transaktionen | Vollständige Vorvalidierung, Batchlimits, Routinggraphprüfung, vorbereitete normalisierte Besitzersnapshots, RAII-Rollback, Dirty- und Historienerhalt, eine Revision/Journalgruppe bei Änderungen, keine bei No-op; Projekt-/Revisionsbindung und Historienkopfprüfung; Operationsreservierung vor Queue, Ergebnisaufbewahrung, Rechte-/Deadlineprüfung | Native Schematests bestanden. Transaktions-, Fehlerinjektions-, Journal- und Dispatcher-Tests hinzugefügt und kompiliert; mangels vollständigem Link noch nicht ausgeführt. Vollständige GUI-Änderungsbeobachtung und Audiozeitbudget müssen im laufenden Programm bewiesen werden. |
| 3: Schreib-MVP und UI | Noten hinzufügen/aktualisieren/entfernen/gezielt transponieren; Zusatzdaten und nicht angegebene Dynamik erhalten; Kernparameter, Spurparameter, Transport; Verbindungseinstellungen und Status in Hauptoberfläche | Compilerprüfung bestanden. Echter Host mit manueller Gegenbearbeitung, hörbarer Wiedergabe und zusammenhängendem Undo/Redo offen. |
| 4: Produktionsfunktionen | Clip verschieben/skalieren/duplizieren/löschen, Spuren beider Container löschen; Pattern-Steps/Arrangement, Automation erstellen/bearbeiten, Mixerhandles und zyklusfreie Sends/Zuweisungen; PluginFactory-Katalog mit interner Freigabeliste; opaque Assets über Preset-/Sample-Ladepfade; neue Projektkopien über sichere Veröffentlichung | Dateipolicy und Schemafehler getestet; zusätzliche native Integrationstests vorhanden. Erfolgs-/Fehler-/Undo-/Lifetime-Abnahme pro Modellfunktion und Plugin muss im vollständigen Build folgen. |
| 5: Audio | Private Snapshots und Ressourcenmanifest; Ressourcenprüfung/Kopie/Hash im Renderworker; getrennte Konfiguration ohne Bridge; 16/24/32-Bit-WAV, tatsächlicher RenderOutcome; Jobstartfehler/Abbruch/Timeout; WAV-Inhaltsprüfung, unveränderte vorhandene Ziele; Artefaktmetadaten/SHA-256 und begrenzter Abruf | WAV- und Snapshot-Ressourcen-Tests bestanden. Vollständiger Render-/Abbruch-/Assetfehler-/Reload-Ablauf und hörbare Abnahme offen. Stem-Rendering wird nicht angeboten und ist weiterhin offen. |
| 6: Auslieferung | CMake-Integration, native Windows-/Unix-/macOS-Launcher, gebündelte Runtime/Produktionsabhängigkeiten/Lizenzen/Versionen, CI-Companiontests in vorhandener Plattformmatrix, korrigierte Host-/Entwicklerdokumentation | Windows-Staging und beide echten stdio-Protokolle über nativen Launcher mit leerem PATH bestanden. Installierte Windows-/Linux-/macOS-Pakete, Plattformmatrix, Last-/Audiotests, Fuzzing und vollständige lokale Rechte-/Raceabnahme offen. |

Checkboxen im Hauptplan bezeichnen die vollständige Abnahme des jeweiligen Arbeitspakets. Vorhandene Implementierung allein setzt sie nicht auf erledigt.

## Tatsächlich ausgeführte Prüfungen

1. `npm --prefix tools/mcp test`: **9/9 bestanden**. TypeScript-Build, Schemakonsistenz, 22 Tools mit Eingabe-/Ausgabeschemata und Annotationen, abgelehnte ungültige Befehle, IPC-Framing, zusammengeführter Erstverbindungsaufbau und Reconnect; echte stdio-Sitzungen für 2026-07-28 und 2025-11-25 ohne LMMS.
2. Isolierte Qt-Testprogramme: **5/5 bestanden** (`AiFilePolicyTest`, `AiDomainContractTest`, `AiChangePlanValidationTest`, `AiWavFileTest`, `AiSnapshotResourcesTest`). Sie verwenden die Produktionsquellen. Geprüft werden u. a. neue Dateien ohne Überschreiben, Verzeichnisgrenzen/Sondernamen, ungültige Schematypen, WAV-Format/Alignment/Trunkierung sowie Ressourcen-Kopie/Hash und geänderte Quellen. Symlink-Erstellung darf auf Windows vom Betriebssystem verweigert werden; der entsprechende Test kennzeichnet dies als Skip.
3. Isolierte MSVC-Kompilierung mit Qt 5.15.2: AI-Quellen und MOC, Start-/GUI-Anbindung, Journal/Mixer sowie Renderer kompiliert; Start-/GUI- und Journal-/Mixerquellen auch mit deaktiviertem MCP kompiliert; native Transaktions-/Dispatcher-/Revisions-Testquellen kompiliert. Dies war ein separater Prüfharness mit temporären Konfigurations-/Exportheadern und offiziellen Drittanbieterheadern. Für den lokal neueren MSVC wurde ausschließlich im temporären Qt-Header ein Adapter für entfernte `stdext`-Iteratoren benötigt. **Kein vollständiger LMMS-Link, kein unveränderter Referenztoolchain-Build.**
4. Runtime-Staging mit Node 24.19.0: Produktionsabhängigkeiten, Runtime, Versions-/SHA-256-/Lizenzmanifest erstellt; `--help` mit leerem PATH erfolgreich. Beide echten stdio-Protokolltests über den kompilierten nativen Windows-Launcher bei leerem PATH: **2/2 bestanden**.
5. Vollständige CMake-Konfiguration `WANT_MCP=ON` mit Qt 5.15.2: **blockiert durch fehlende SndFile-Entwicklungsbibliothek**. Deshalb wurde kein vollständiges LMMS-Binary erzeugt und kein nativer End-to-End-Test als bestanden gewertet.

## Ergänzte native Integrationstests

`AiEditTransactionTest` prüft Tempo + Clip + Noten mit einem Undo/Redo; injizierte Fehler nach jedem der drei Mutationsschritte, unveränderte Historienköpfe/Revision/Dirty-Status; Vorvalidierungsfehler; Journalabbruch mit erhaltenem Redo; GUI-Notenkonflikt; No-op; Notenupdate/-entfernung; Erhalt nicht angegebener Dynamik; getrennte Song-/PatternStore-Besitzerscopes. Der Fehler-Observer ist eine interne Testnaht und kein IPC-Argument.

`AiCommandDispatcherTest` ergänzt doppelte wartende Aufträge, Rechtewiderruf während des Wartens und verlorene Verbindungen. Der Node-Testpeer verbindet sich mit dem echten nativen Endpunkt, prüft Readerrechte und die zweite Schreibverbindung. Diese Tests sind im Repository-Build registriert, lokal aber nur kompiliert.

## Verbleibende Freigabereihenfolge

1. Vollständige Abhängigkeiten bereitstellen; LMMS mit `WANT_MCP=ON` und `OFF` unter der Qt-5.15-Referenztoolchain bauen und alle nativen Tests ausführen. Starten/Neu/Laden/Recovery/Shutdown sowie `--no-mcp` tatsächlich testen.
2. Die neuen Transaktions-/Dispatcher-Tests ausführen und erweitern: Konflikte für Spur/Mixer/Tempo/Projekt, verlorene Antworten und Companion-Neustart, Automationsverbindungen nach Restore, alle Fehlerstellen mit erhaltenem Undo/Redo. Registry-Invalidierung nach Wiederherstellung ist bewusst konservativ; Referenzen müssen neu gelesen werden.
3. Audio messen: GUI-/MCP-Gegenbearbeitung, Plugininitialisierung, Modelloperationen unter Last, XRuns und begrenzte Commit-Latenz. Quellcodeprüfung ersetzt diese Messung nicht.
4. Pro Produktionsfunktion echte Erfolgs-/Fehler-/Undo-/Lifetime-Tests durchführen. Stem-Rendering mit eigener Snapshot-Mute-/Routingstrategie und Tests ergänzen, bevor es angeboten wird.
5. Vollständiger Ablauf Lesen → Preview → Commit → hörbare Wiedergabe → Undo/Redo → Projektkopie neu laden → Snapshot rendern → Artefakt abrufen. Exportstartfehler, Crash, Timeout, Abbruch, fehlende Assets, korrupte WAVs, vorhandene Ziele, Junction- und konkurrierende Zieländerungen abnehmen.
6. Die bestehenden Plattformjobs und installierten Pakete ohne Entwicklerumgebung abnehmen; dazu ON/OFF-Matrix, Lizenz-/Architekturprüfung der Runtime, echte Hostversionen dokumentieren und Schema-Fuzzing ausführen.

Diese verbleibenden Arbeiten sind Freigabebedingungen; die aktuelle Änderung behauptet keine vollständige Produktionsfreigabe.
