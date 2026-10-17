# LMMS: Gesamtplan für Darstellung, Performance, Fehlerbehebung und Stabilisierung

**Datum:** 17. Oktober 2026  
**Analysierter Stand:** e2740a65a, „Merge high-DPI scaling with MCP integration“  
**Programmversion laut CMake:** 1.3.0-alpha  
**Arbeitsverzeichnis:** lmms-merged; zu Analysebeginn keine Änderungen im Git-Arbeitsbaum  
**Status:** Umsetzung im Arbeitsbaum am 17. Oktober 2026. Der Nachweis steht in [doc/stabilization/](doc/stabilization/baseline.md). Nicht jeder Punkt ist zur Freigabe gemessen; offene Punkte sind dort und in Abschnitt 13 benannt.  
**Methode:** CocoIndex MCP, direkte Quellcodeprüfung, lokale Build-/Testversuche und ergänzende Prüfung offizieller technischer Dokumentation.

**Nachbesserung von `64bc33825`:** Dateischutz, XML-Kompatibilität, Recoveryclaims, Workerfehler/Deadlines und Testlauncher wurden korrigiert. Nachweise: [findings.md](doc/stabilization/findings.md). 8/8 Minimaltests und 25/25 DPI-Tests bestehen jeweils in Release/Debug. Vollbuild, Engineintegration und Produkt-/Langlaufabnahme bleiben offen; lokale Vollkonfigurationen scheitern an MSVC >= 19.51 gegenüber installiertem 19.44.

**MCP-Nachbesserung vom 17. Oktober 2026:** Englische, übersetzbare Oberfläche; korrekter Bereitschaftsstatus; abgesicherte Dienst-/Companiongenerationen und Ressourceninstanzen; korrigierte Instrumentenschlüssel und Presetknoten; gemeinsamer Rollback sowie behobene GUI-Lebensdauerfehler bei Wiederherstellung. Nachweis und Grenzen: [MCP-Untersuchungsbericht](doc/mcp/connection-incident-2026-10-17.md). Companion 27/27, native MCP-/Oberflächentests 17/17 mit vollständigem Qt-6-/MSVC-19.51-Link bestanden; vollständige Anwendungsbuilds mit MCP ON und OFF erfolgreich. Diese Ergebnisse ergänzen die historischen Toolchainangaben oben; installierte Anwendung und musikalische Sitzung wurden nicht verändert.

## 1. Ziel und wichtigste Entscheidung

LMMS soll im täglichen Arbeiten zuverlässig reagieren, Projekte sicher speichern, auch große Arrangements flüssig darstellen und bei Last oder fehlerhaften Plugins kontrolliert reagieren. Die vorhandenen Qt-Widgets-, Audio-, Journal- und MCP-Strukturen bieten dafür eine brauchbare Grundlage.

Die empfohlene Reihenfolge lautet:

1. Datenverlust und eindeutig erkennbare Fehlerpfade schließen.
2. Einen reproduzierbaren vollständigen Build und belastbare Integrationstests herstellen.
3. Audio- und GUI-Latenzen sichtbar machen.
4. Blockierende Arbeit aus zeitkritischen Pfaden entfernen.
5. Darstellung, Bedienbarkeit und Ressourcenverbrauch systematisch verbessern.
6. Die kombinierte Anwendung auf realen Geräten und in installierten Paketen abnehmen.

Eine komplette Neuentwicklung der Oberfläche oder Audioengine ist aus den Befunden nicht abzuleiten. Kleine, prüfbare Änderungen an vorhandenen Komponenten ermöglichen früh nutzbare Verbesserungen. Größere Umbauten werden erst durch Messungen begründet.

### 1.1 Umfang

Dieser Plan umfasst:

- Hauptfenster, Editoren, Browser, Mixer, Wellenformen, Instrumentansichten und Analyseanzeigen.
- High DPI, unterschiedliche Monitorskalierungen, Schrift, Layout, Fokus und Tastaturbedienung.
- Audioberechnung, Worker, Modelländerungen und den Datenaustausch mit Anzeigen.
- Projektladen, Speichern, Autosave, Recovery, Undo/Redo und Dateiformatgrenzen.
- Plugin-Hilfsprozesse, Renderprozesse und kontrolliertes Beenden.
- Die neue MCP-Integration als zusätzliche Belastung und als Teil des Anwendungslebenszyklus.
- Tests, CI, Diagnose, Packaging und Freigabekriterien.

Die Prüfung ist eine breite, risikoorientierte Quellcodeanalyse mit Stichproben in Plugins. Sie ist kein vollständiges Audit jedes DSP-Algorithmus, jedes Audio-Backends oder sämtlicher eingebundener Drittanbieterquellen.

### 1.2 Verhältnis zu bestehenden Plänen

Die Dokumente [HIGH_DPI_SCALING_PLAN.md](HIGH_DPI_SCALING_PLAN.md) und [MCP_INTEGRATIONSPLAN.md](MCP_INTEGRATIONSPLAN.md) bleiben fachliche Detailreferenzen. Ihr älterer Istzustand ist nicht automatisch der Istzustand dieses Merge-Commits.

Bereits vorhanden und bei weiteren Änderungen zu erhalten sind insbesondere:

- DpiContext mit fensterbezogener DPR-Ermittlung und zusammengefassten Aktualisierungen.
- DPR-bezogene Ressourcenbeschreibungen und begrenzte Icon-Metadaten in embed.cpp.
- Ein Windows-Manifest mit PerMonitorV2/PerMonitor.
- DPI-Tests für mehrere gebrochene Skalierungsfaktoren sowie ein nativer Windows-Test.
- MCP-Rechteprüfung, revisionsgebundene Änderungen, Journalgruppen, Rollback, begrenzte Queues und Renderworker.
- Journal-Speicherbudget und Schutzmechanismen für neue Exportdateien.
- Plattformübergreifende Build-Jobs und separate DPI-CI.

Diese Dinge werden hier nicht erneut als fehlende Grundfunktionen eingeplant. Die verbleibende Aufgabe ist ihre Vervollständigung und gemeinsame Abnahme.

## 2. Untersuchungsweg und Grenzen der Nachweise

### 2.1 Verwendung von CocoIndex MCP

CocoIndex wurde zunächst mit Indexaktualisierung und danach für mehrere semantische Suchen verwendet. Suchthemen waren:

| Suchthema | Anschließend direkt geprüfte Bereiche |
| --- | --- |
| Audioverarbeitung, Echtzeit-Callbacks, Mixer und Worker-Synchronisation | AudioEngine, AudioEngineWorkerThread, AudioEngineProfiler, zugehörige Header |
| DPI, Fensterkontext, Rasterressourcen und Monitoränderungen | DpiContext, embed, SubWindow, GUI-Widgets und DPI-Tests |
| Projektspeicherung, Backup und Recovery | Song, DataFile, MainWindow |
| MCP-Queue, Transaktionen, Shutdown und Renderjobs | AiControlService, AiCommandDispatcher, AiEditTransaction, AiProjectSnapshot, AiJobManager |
| Piano-Roll-Zeichnung und sichtbare Notenbereiche | PianoRoll, MidiClipView und angrenzende Ansichten |
| Undo/Redo und Wiederherstellung | ProjectJournal, JournallingObject, Transaktionscode |

Die Suchtreffer dienten zur Navigation. Einige Treffer bestanden nur aus Dateiköpfen. Ein semantischer Treffer oder sein Score wurde deshalb nicht als Fehlernachweis übernommen; die nachstehenden Befunde beruhen auf der direkten Prüfung des jeweiligen Codes.

Ergänzend wurden mit lokalen Werkzeugen CMake-Dateien, Workflows, bestehende Tests, Browser-Suche, SampleThumbnail, ThreadPool, Plugin-Kommunikation, Parserpfade und vorhandene Dokumentation geprüft.

### 2.2 Evidenzklassen

| Kennung | Bedeutung |
| --- | --- |
| **K** | Konkreter Fehler oder problematischer Kontrollfluss im aktuellen Quellcode nachweisbar. Die praktische Häufigkeit ist damit noch nicht gemessen. |
| **R** | Nachvollziehbares Architektur-/Laufzeitrisiko. Ein tatsächlicher Hänger, Race oder hörbarer Aussetzer muss gezielt reproduziert werden. |
| **V** | Verbesserungsvorschlag aus dem Istzustand; kein behaupteter Defekt. |
| **T** | In dieser Analyse tatsächlich ausgeführter Test oder Buildversuch. |

Ein K-Befund ist keine Behauptung, dass ein Absturz bereits in einer laufenden LMMS-Sitzung beobachtet wurde. Besonders bei Nebenläufigkeit und GUI-/Audio-Verhalten ist zwischen Codebefund und praktischer Reproduktion zu unterscheiden.

### 2.3 Tatsächlich ausgeführte Prüfungen

| Prüfung | Ergebnis am 17.10.2026 | Aussagegrenze |
| --- | --- | --- |
| npm --prefix tools/mcp test | **9/9 bestanden**; TypeScript-Build und Schemakonsistenz ebenfalls erfolgreich | Companion, IPC-Testpeer und stdio-Protokolltests; kein vollständiger LMMS-End-to-End-Nachweis |
| cmake --build build/dpi --config Release --parallel 2 | **Erfolgreich** mit bestehendem lokalem Qt-5.15.2-Buildverzeichnis | Isolierte Produktionsquellen; kein Gesamtanwendungslink |
| ctest --test-dir build/dpi -C Release --output-on-failure --parallel 2 | **25/25 bestanden**, nach Einrichten des Qt-Laufzeitpfades | Vier isolierte Tests bei sechs Faktoren plus ein nativer Windows-Test; DpiWidgetTest gehört nicht zu diesem isolierten Target |
| Build der fünf vorhandenen isolierten AI-Testtargets in build/merge-check/out | **Erfolgreich** | Vorhandener lokaler Prüfharness mit temporären Headern; kein portabler Referenzbuild |
| ctest --test-dir build/merge-check/out -C Release --output-on-failure | **5/5 bestanden**: FilePolicy, DomainContract, ChangePlanValidation, WavFile, SnapshotResources | Keine Ausführung der vollständigen Transaktions-, Dispatcher- und Anwendungstests |
| Neue Gesamtkonfiguration nach build/improvement-audit, Qt 5.15.2, WANT_MCP=ON | **Abgebrochen:** SndFile_LIBRARY und SndFile_INCLUDE_DIRS fehlen | Vollständiger Build, Link und Anwendungstest konnten so nicht erfolgen |

Beim ersten DPI-Teststart fehlten Qt-DLLs im Prozesssuchpfad; Windows meldete 0xc0000135. Nach Ergänzen von Qt-bin und Plattformpluginpfad bestanden alle 25 Tests. Das war ein lokales Laufzeitumgebungsproblem. Es zeigt zugleich, dass der Teststart und das Packaging ihre Laufzeitabhängigkeiten explizit behandeln sollten.

Der neue Gesamtkonfigurationsversuch verwendete den automatisch gewählten Generator Visual Studio 18 2026 und MSVC 19.51 mit Qt 5.15.2. Zusätzlich erschien eine Warnung zu einem nicht gefundenen Qt5Test-Paket. Es wurde kein Compiler-Kompatibilitätsnachweis für diese Kombination erbracht. Der frühere Prüfharness ist kein Ersatz für eine festgelegte Referenztoolchain.

**Nicht durchgeführt:** vollständiger LMMS-Start und interaktive Sichtprüfung; Messungen von CPU, Speicher, XRuns oder Paint-Zeiten; Hörtest; reale Monitorwechsel; Langzeittest; Plugin-Crash-Injektion; vollständige Sanitizer-Läufe; Prüfung installierter Pakete. Entsprechende Werte im folgenden Plan sind Zielwerte, keine gemessenen Ergebnisse.

## 3. Architektur und vorhandene Stärken

Die kritischen Pfade verlaufen vereinfacht so:

~~~text
GUI / lokale Bedienung / MCP
          |
          v
Song, Tracks, Clips, Modelle, Journal
          |
          | Modelländerungen und Synchronisation
          v
AudioEngine -> Instrument-Jobs -> Effekt-Jobs -> Mixer -> Audio-Backend
          |
          +-> Pegel / Wellenform / Analyseanzeigen

Projektdateien <-> DataFile / Song
MCP-Export    -> Projektsnapshot -> separater Renderprozess -> WAV / Artefakt
GUI-Ressourcen -> embed / DpiContext -> Widgets und Zeichen-Caches
~~~

Wichtige positive Ausgangspunkte:

- Der Audioengine-Profiler unterscheidet bereits NoteSetup, Instruments, Effects und Mixing. Darauf kann eine bessere Latenzdiagnose aufbauen.
- Die Piano Roll prüft vor dem eigentlichen Zeichnen bereits die Sichtbarkeit von Noten. Verbesserungen betreffen die Kandidatensuche und Invalidierung, nicht das erstmalige Einführen jeder Sichtbarkeitsprüfung.
- Die Dateibrowsersuche läuft grundsätzlich in einem ThreadPool. Ihr Abbruch und Ergebnisfluss sind die eigentlichen Schwachstellen.
- Der Spectrum Analyzer besitzt Aktivitätszustände für Spectrum und Waterfall. Eine verbesserte Sichtbarkeitssteuerung sollte diese vorhandenen Schnittstellen nutzen.
- Die MCP-Transaktionsanwendung lehnt Änderungen während Wiedergabe, Laden und Export bereits ab. Der Plan unterstellt keine beliebigen MCP-Batchedits während laufender Wiedergabe.
- Der aktuelle ProjectJournal enthält bereits ein Speicherbudget von 128 MiB; ein Transaktionssnapshot ist auf 64 MiB begrenzt. Zusätzliche Maßnahmen müssen diese bestehenden Grenzen berücksichtigen.

## 4. Befundkatalog

Zeilenangaben gelten für den analysierten Commit. Die Links öffnen die Quelldateien; die genannten Zeilen grenzen die relevanten Stellen ein.

### B01 — Erfolgsmeldung nach fehlgeschlagener Projektveröffentlichung

**K · P0 · Datensicherheit**  
**Beleg:** [DataFile.cpp](src/core/DataFile.cpp), Zeilen 389–437; [Song.cpp](src/core/Song.cpp), Zeilen 1303–1313.

DataFile schreibt mit QSaveFile zunächst auf den Namen mit Endung .new und prüft dessen commit(). Danach werden das bisherige Ziel entfernt oder zum Backup umbenannt und die .new-Datei auf den endgültigen Namen verschoben. Die Rückgabewerte dieser letzten remove()/rename()-Operationen werden ignoriert. Die Funktion liefert anschließend true.

Scheitert beispielsweise das abschließende Umbenennen durch eine Sperre oder ein verändertes Zielverzeichnis, kann der Aufrufer das Projekt als erfolgreich gespeichert betrachten. Song::guiSaveProjectAs() setzt daraufhin den Änderungsstatus zurück. Der Inhalt kann noch als .new oder .bak vorhanden sein; die garantierte Veröffentlichung am erwarteten Ziel fehlt dennoch.

**Maßnahme:** finalen Zielpfad atomar ersetzen, Backup als eigene abgesicherte Operation behandeln und jeden Fehler bis zur Oberfläche propagieren. **Arbeitspaket:** AP01.

### B02 — Fehler beim Projektladen hinterlässt Zustände

**K · P1 · Stabilität / Undo / Autosave / MCP**  
**Beleg:** [Song.cpp](src/core/Song.cpp), Zeilen 1030–1035, 1069–1076 und 1199–1225.

loadProject() setzt m_loadingProject=true und deaktiviert Journalling. Im Fehlerzweig für ein nicht ladbares Projekt wird bei einer bestehenden Sitzung der vorherige Dateiname wiederhergestellt und vor der regulären Bereinigung zurückgekehrt. Insbesondere ohne den Sonderpfad m_loadOnLaunch werden Ladeflag und Journalzustand dort nicht zurückgesetzt.

Davon hängen weitere Funktionen ab: Autosave überspringt ladende Projekte; der MCP-Dispatcher lehnt Anfragen bei isLoadingProject() ab; Transaktionen verlangen ein verfügbares Journal. Der Fehler kann deshalb deutlich mehr als den einzelnen Ladeversuch betreffen.

**Maßnahme:** RAII für Lade-/Journalzustand; Fehlerpfade erhalten das bisherige Projekt und seine Bedienbarkeit. **Arbeitspaket:** AP02.

### B03 — Ungültiger Iterator in der Wellenformanzeige

**K · P1 · Absturzrisiko / Anzeige**  
**Beleg:** [SampleThumbnail.cpp](src/gui/SampleThumbnail.cpp), Zeilen 119–160; Nutzer unter anderem [AudioFileProcessorWaveView.cpp](plugins/AudioFileProcessor/AudioFileProcessorWaveView.cpp), Zeilen 359–369, und SampleClipView/SlicerT.

Wenn keine ausreichend feine Thumbnailstufe existiert, ist finerThumbnail gleich rend(). useOriginalBuffer erkennt das. Im alternativen Zeichenpfad werden für beginIndex und endIndex trotzdem finerThumbnail->width() aufgerufen, bevor zwischen Thumbnail und Originalbuffer unterschieden wird.

Ein rechnerisch erreichbarer Fall: 100 Stereo-Sampleframes ergeben anfangs 20 Peaks; eine Zielbreite von 64 ist größer als jede Thumbnailstufe und ungleich 100. Damit wird der Originalbuffer gewählt, aber nicht der speziellen Einzelpunktpfad, und der ungültige Iterator wird benutzt. Das ist eine Ableitung aus dem Code, kein hier ausgeführter LMMS-Absturztest.

Weitere Randfälle sind leere/kurze Daten, leere Aggregationsintervalle und die uneinheitliche Verwendung von Frames gegenüber flach angeordneten Kanalwerten.

**Maßnahme:** Original- und Peak-Datenpfade mit jeweils gültigen Grenzen ausführen; Stereo-/Frame-Semantik vereinheitlichen. **Arbeitspaket:** AP03.

### B04 — Worker-Abbruch und Wartelogik sind nicht ausreichend abgesichert

**K/R · P1 · Nebenläufigkeit / Shutdown**  
**Beleg:** [AudioEngineWorkerThread.h](include/AudioEngineWorkerThread.h), Zeile 119; [AudioEngineWorkerThread.cpp](src/core/AudioEngineWorkerThread.cpp), Zeilen 97–99, 137–170; [AudioEngine.cpp](src/core/AudioEngine.cpp), Zeilen 119–134.

m_quit ist volatile bool, wird im Steuerthread geschrieben und im Worker gelesen. Volatile stellt dafür keine Thread-Synchronisation her. Die QWaitCondition wird mit je einem lokalen Mutex pro Worker benutzt; eine explizite Prädikats-/Generationsbedingung für Arbeit und Shutdown fehlt.

JobQueue::wait() dreht ohne Zeitbegrenzung, bis alle Jobs erledigt sind. Im Destruktor wird das Ergebnis von wait(500) nicht geprüft. Ein tatsächlicher Hänger ist hier nicht reproduziert, aber die Lebensdauerbedingungen sind nicht belastbar dokumentiert.

**Maßnahme:** synchronisierter Stopzustand, überprüfbares Jobprädikat, definierter Join und Stresstests. **Arbeitspaket:** AP04.

### B05 — Unbegrenztes Warten auf Plugin- und Serviceprozesse

**K/R · P1 · Hänger / Beenden / Audio**  
**Beleg:** [RemotePlugin.cpp](src/core/RemotePlugin.cpp), Zeilen 397–407; [RemotePluginBase.cpp](src/common/RemotePluginBase.cpp), Zeilen 136–191; [AiControlService.cpp](src/core/ai/AiControlService.cpp), Zeilen 146–164.

Die Remote-Plugin-Verarbeitung wartet auf IdProcessingDone. waitForMessage() besitzt dort keine eigene Deadline und läuft bis zur passenden Nachricht oder Invalidierung. Ein lebender, aber nicht antwortender Pluginprozess ist ein anderer Fehlerfall als ein beendeter Prozess.

Der Hauptthreadpfad verarbeitet während bestimmter Wartevorgänge zusätzliche Ereignisse; im Code steht bereits ein entsprechender Hang-FIXME. AiControlService::stopService() wartet ohne Timeout auf den IO-Thread und setzt anschließend das in diesen Thread verschobene Serverobjekt zurück. Das korrekte Zerstören im Besitzerthread ist gezielt zu prüfen.

**Maßnahme:** unterschiedliche Zeitbudgets für Initialisierung, GUI-Kommandos, Audioblöcke und Beenden; explizite Zustandsautomaten. **Arbeitspakete:** AP14 und AP15.

### B06 — Audio-CPU-Anzeige erfasst nicht die gesamte Deadline

**K/V · P1 · Messbarkeit / Performance**  
**Beleg:** [AudioEngine.cpp](src/core/AudioEngine.cpp), Zeilen 318–331; [AudioEngineProfiler.cpp](src/core/AudioEngineProfiler.cpp), Zeilen 43–70.

renderNextPeriod() wartet zuerst auf m_changeMutex und startet erst danach den Periodentimer. Die ausgewiesene Rechenlast enthält damit diese Wartezeit nicht. Zudem wird die CPU-Anzeige geglättet; einzelne Deadlineverletzungen benötigen eigene Zähler und Verteilungen.

Bei aktiviertem Profiler-Dateiexport wird innerhalb von finishPeriod() pro Periode Text formatiert und in eine Datei geschrieben. Diese Diagnose kann den gemessenen Pfad selbst belasten.

**Maßnahme:** Berechnungszeit, Sperrwartezeit, Aufruf-/Backendlatenz und tatsächliche Underruns getrennt erfassen; Logging asynchron. **Arbeitspaket:** AP06.

### B07 — Zeitkritischer Audiopfad enthält variable Aufwände

**K/R · P1 · Audioaussetzer unter Last**  
**Beleg:** [AudioEngine.cpp](src/core/AudioEngine.cpp), Zeilen 209–245 und 270–288; [AudioEngineWorkerThread.cpp](src/core/AudioEngineWorkerThread.cpp), Zeilen 54–66.

Im Periodenpfad werden PlayHandles gesucht, Container verändert und Nicht-Noten-Handles gelöscht. Die Entfernung enthält wiederholte lineare Suchen. Queueüberlauf protokolliert direkt mit qWarning() und zählt einen nicht eingereihten Job als abgearbeitet.

Das ist kein Nachweis eines aktuellen XRuns. Es sind Stellen mit variabler Ausführungszeit und einer bisher wenig sichtbaren Überlastreaktion.

**Maßnahme:** messen, dann vorallokierte Strukturen, gebündelte Entfernung, verzögerte Freigabe und eine hörbar/diagnostisch definierte Überlaststrategie einführen. **Arbeitspakete:** AP06 und AP07.

### B08 — Sample-Vorschau blockiert den GUI-Thread

**K · P1 · Reaktionsfähigkeit**  
**Beleg:** [FileBrowser.cpp](src/gui/FileBrowser.cpp), Zeilen 793–836.

Die Vorschau hält einen Mutex, lädt Samples synchron mit SampleBuffer::fromFile() und verwendet processEvents() als Zwischenlösung. Auch Preset-/Plugininitialisierung wird aus diesem Bedienpfad angestoßen.

Große Dateien, langsame Datenträger und schnelle Auswahlwechsel sind deshalb geeignete Reproduktionsszenarien für Verzögerungen und Wiedereintrittsprobleme.

**Maßnahme:** asynchrone, abbrechbare Samplevorbereitung mit Auswahlgeneration; Plugininitialisierung nur gemäß ihrer Threadanforderungen auslagern. **Arbeitspaket:** AP08.

### B09 — Suchneustart kann warten; Suchergebnisse sind nicht generationengebunden

**K/R · P1/P2 · Browser / Bedienbarkeit**  
**Beleg:** [FileSearchJob.cpp](src/gui/FileSearchJob.cpp), Zeilen 38–57 und 80–96; [FileBrowser.cpp](src/gui/FileBrowser.cpp), Zeilen 110–113 und 218–253.

Bei einem neuen Suchauftrag wird im aufrufenden Pfad m_task.get() auf den bisherigen Auftrag ausgeführt. Die Stopflagge wird erst in der Suchschleife geprüft; ein langsamer Verzeichniszugriff kann diesen Pfad verzögern.

Jeder Treffer erzeugt ein eigenes Signal und ein eigenes QTreeWidgetItem. Die Signale enthalten keine Suchgeneration; bereits zugestellte Ereignisse können nach dem Leeren der Ansicht zu einem älteren Suchlauf gehören. Jedes textEdited startet unmittelbar eine neue Suche.

Zusätzlich prüft Zeile 233 if (m_showHiddenContent), also den Zeiger auf die Checkbox statt ihren Haken. Sobald die Checkbox existiert, wird QDir::Hidden zur Suche hinzugefügt.

**Maßnahme:** nichtblockierender Abbruch, Generationen, Ergebnisbatches, kurzes Debounce und Korrektur der Hidden-Option. **Arbeitspaket:** AP08.

### B10 — ThreadPool behandelt Task-Ausnahmen und fehlende CPU-Anzahl nicht robust

**K/R · P1 · Allgemeine Stabilität**  
**Beleg:** [ThreadPool.h](include/ThreadPool.h), enqueue() und s_numWorkers; [ThreadPool.cpp](src/core/ThreadPool.cpp), Zeilen 29–37 und 60–79.

Der Taskwrapper setzt promise-Werte, fängt aber keine Ausnahme von std::apply() ab. run() ruft den Task ebenfalls ohne Ausnahmegrenze auf. Wirft eine Funktion, kann die Ausnahme den std::thread-Einstieg verlassen und den Prozess beenden, statt im Future anzukommen.

Die Workerzahl stammt direkt aus hardware_concurrency(); der Konstruktor besitzt nur ein assert(numWorkers > 0). Der Rückgabefall 0 braucht eine funktionsfähige Fallbackzahl. Für AudioEngine::m_numWorkers gilt die analoge Prüfung bei idealThreadCount()-1.

**Maßnahme:** set_exception(), Mindestzahl, definierter Shutdown und Warteschlangengrenzen. **Arbeitspaket:** AP05.

### B11 — Audioanzeige erhält einen Zeiger auf wiederverwendeten Speicher

**K/R · P1 · Anzeige / Threadgrenzen**  
**Beleg:** [AudioEngine.cpp](src/core/AudioEngine.cpp), Zeile 308; [Oscilloscope.cpp](src/gui/widgets/Oscilloscope.cpp), Zeilen 78–106.

nextAudioBuffer übergibt einen const SampleFrame*. Der Oscilloscope-Slot kopiert dessen Inhalt erst bei seiner Ausführung. Die Verbindung legt keinen expliziten Übertragungsvertrag für Besitz und Gültigkeitsdauer fest.

Bei einer QueuedConnection wird der Zeigerwert übertragen; die Audiodaten gehören dadurch nicht der Nachricht. Eine langsame GUI kann die Daten erst nach Wiederverwendung des Buffers lesen. Ein Wechsel auf DirectConnection würde umgekehrt das gemeinsame GUI-Pufferobjekt aus dem Audiothread beschreiben und braucht ebenfalls Synchronisation.

**Maßnahme:** vorallokierter Snapshot-/Ringpuffer für Messdaten und begrenzte GUI-Abholung. **Arbeitspaket:** AP09.

### B12 — Editorzeichnungen durchsuchen auch unsichtbare Noten

**K/V · P2 · Große Projekte**  
**Beleg:** [PianoRoll.cpp](src/gui/editors/PianoRoll.cpp), Zeilen 3723–3788; außerdem zahlreiche update()/repaint()-Aufrufe bei Bearbeitungen.

Die Piano Roll iteriert über alle Noten beziehungsweise Ghost Notes und prüft innerhalb der Schleife, ob sie sichtbar sind. Clipping ist vorhanden; bei sehr großen Clips bleibt die Kandidatensuche dennoch linear zur Gesamtmenge.

**Maßnahme:** zuerst Paint- und Hit-Testprofile; bei nachgewiesenem Bedarf ein zeitlicher Intervallindex einschließlich lang überlappender Noten und Detuningkurven. Dirty-Regionen und statische Raster separat behandeln. **Arbeitspaket:** AP10.

### B13 — DPI-Umsetzung ist fortgeschritten, aber nicht überall durchgängig

**K/V · P2 · Darstellung**  
**Beleg:** [DpiContext.cpp](src/gui/DpiContext.cpp), [embed.cpp](src/gui/embed.cpp), [CPULoadWidget.cpp](src/gui/widgets/CPULoadWidget.cpp); als Gegenbeispiel [Oscilloscope.cpp](src/gui/widgets/Oscilloscope.cpp), Zeilen 44–52.

Das Oscilloscope lädt einen alten Pixmap-Pfad, leitet die feste Widgetgröße aus width()/height() ab und registriert keinen Dpi::watch()-Refresh. Andere Widgets verwenden bereits logische Ressourcenmaße und DPR-bezogene Caches.

Die bestandenen isolierten Tests prüfen weder alle Widgets noch echte Wechsel zwischen unterschiedlich skalierten Monitoren. Der native Test prüft ein verstecktes Testfenster, nicht die komplette installierte Anwendung.

**Maßnahme:** vollständiges Inventar der verbliebenen Ressourcenverbraucher und gezielte Sichtprüfung. **Arbeitspaket:** AP11.

### B14 — Schrift und feste Layoutmaße begrenzen Anpassbarkeit

**K/V · P2 · Lesbarkeit / Bedienung**  
**Beleg:** [FontHelper.h](include/FontHelper.h), Zeilen 29–42; [MainWindow.cpp](src/gui/MainWindow.cpp), feste Toolbarhöhe; [PianoRoll.cpp](src/gui/editors/PianoRoll.cpp), Zeilen 5335–5394; [FileBrowser.cpp](src/gui/FileBrowser.cpp), Zeile 99.

Globale Schriftgrößen liegen bei 10/12/14 Pixeln, mehrere Toolbars und Auswahlfelder haben feste Größen. Das ist allein kein DPI-Fehler, kann aber bei größeren Schriften, langen Übersetzungen und kleinen Arbeitsflächen Platzprobleme verursachen.

Die Suche nach QAccessible und expliziten zugänglichen Namen ergab in src/gui und include keine Treffer. Daraus folgt kein generelles Fehlen von Accessibility: Standard-Qt-Widgets bieten bereits Verhalten. Die selbst gezeichneten Controls benötigen aber eine eigene Prüfung.

**Maßnahme:** textmetrische Layouts, konsistente Bedienzustände, Tastatur-/Fokus- und Accessibility-Audit. **Arbeitspaket:** AP12.

### B15 — Thumbnailcache begrenzt Einträge, aber nicht ihre Gesamtbytes

**K/V · P2 · Speicher / Anzeigeperformance**  
**Beleg:** [SampleThumbnail.cpp](src/gui/SampleThumbnail.cpp), Zeilen 34–104; [SampleThumbnail.h](include/SampleThumbnail.h), SampleThumbnailEntry und statische Map.

Die Map hat maximal 32 Einträge und verwendet use_count() als Verdrängungskriterium. Das ist keine Speichergrenze und kein zeitliches LRU. Große Dateien und von Widgets weiter gehaltene Caches können trotz begrenzter Map erheblichen Speicher belegen.

Die Erzeugung der Peakpyramide erfolgt synchron beim Aufbau. Schlüssel sind Pfad und Änderungszeit; bei In-Memory-Änderungen oder identischen Zeitstempeln ist eine eigene Inhalts-/Datenrevision sinnvoll.

**Maßnahme:** messbare Bytebudgets, Datenrevision, verzögerte Peakberechnung und klare Lebensdauer. **Arbeitspakete:** AP03 und AP17.

### B16 — Artefaktabruf ist für längere Musikstücke eingeschränkt und teuer

**K/V · P2 · MCP / GUI-Performance**  
**Beleg:** [AiJobManager.cpp](src/core/ai/AiJobManager.cpp), Zeilen 341–366; [AiCommandDispatcher.cpp](src/core/ai/AiCommandDispatcher.cpp), Zeilen 340–344.

readArtifact() lehnt Dateien über 16 MiB ab, obwohl das Rendering längere WAVs erzeugen kann. Bei Stereo/44,1 kHz/16 Bit entspricht die reine PCM-Datenmenge von 16 MiB ungefähr 95 Sekunden; Header verkürzen diese Grenze leicht.

Vor jedem Teilabruf wird die gesamte Datei unter dem Jobmanager-Mutex erneut gehasht. Bei Abholung in 256-KiB-Stücken wird dieselbe Datei vielfach gelesen. Der Dispatcher führt die Aktion im Anwendungsthread aus.

**Maßnahme:** Integritäts- und Größenstrategie für übliche Songs festlegen; Dateiarbeit aus dem GUI-Thread auslagern und wiederholte Vollhashes vermeiden, ohne nachträgliche Änderungen ungeprüft zu akzeptieren. **Arbeitspaket:** AP16.

### B17 — Harte Bestandslimits können lange Sitzungen dauerhaft blockieren

**K/V · P2 · Langzeitstabilität / Ressourcen**  
**Beleg:** [AiJobManager.cpp](src/core/ai/AiJobManager.cpp), Zeile 82; [AiCommandDispatcher.cpp](src/core/ai/AiCommandDispatcher.cpp), Zeilen 128–145; [AiCommandDispatcher.h](include/AiCommandDispatcher.h), MAX_OPERATION_HISTORY.

Der Jobmanager lehnt ab 128 gespeicherten Jobs neue Jobs ab; ein Aufräumen abgeschlossener Einträge ist im geprüften Pfad nicht vorhanden. Das Operationsregister ist auf 1024 Schlüssel begrenzt und entfernt abgeschlossene Einträge erst nach 24 Stunden.

Diese Grenzen schützen Ressourcen. Sie brauchen aber eine dokumentierte Aufbewahrung, verständlichen Status und eine Möglichkeit, nach Erreichen der Grenze weiterzuarbeiten. Die garantierte Wiederholbarkeit von Mutationen darf nicht durch willkürliches Löschen junger Schlüssel gebrochen werden.

**Maßnahme:** getrennte Budgets für aktive Jobs, Verlauf, Antwortdaten, Dateiartefakte und Idempotenznachweise. **Arbeitspaket:** AP17.

### B18 — Projektparser besitzt im geprüften Einstieg keine Arbeitsbudgets

**K/R · P1 · Speicher / Ladefehler**  
**Beleg:** [DataFile.cpp](src/core/DataFile.cpp), Zeilen 156–180 und loadData() ab ungefähr 2099.

Dateien werden vollständig mit readAll() eingelesen. Nach einem XML-Parsefehler wird qUncompress() versucht und das Ergebnis erneut als DOM geladen. In diesem Einstieg sind keine expliziten Grenzen für Dateigröße, entpackte Größe oder Modellkomplexität zu erkennen.

Das beweist keine konkrete ausnutzbare Schwachstelle. Es rechtfertigt aber Schutz gegen kaputte oder extrem große Dateien, besonders bevor aufwendige Modell- und Pluginobjekte entstehen.

**Maßnahme:** abgestufte Ressourcenlimits, strukturierte Parsefehler, geprüfte Upgradepfade und Fuzzing. **Arbeitspaket:** AP13.

### B19 — Integration und Produktabnahme reichen über die isolierten Tests hinaus

**T/V · P1 · Qualitätssicherung**  
**Beleg:** [tests/CMakeLists.txt](tests/CMakeLists.txt), [tests/dpi/CMakeLists.txt](tests/dpi/CMakeLists.txt), [build.yml](.github/workflows/build.yml), [high-dpi.yml](.github/workflows/high-dpi.yml), [MCP-Abnahmenachweise](doc/mcp/implementation-status.md).

Vollständige Plattformjobs und registrierte native Tests sind vorhanden. Lokal konnten jedoch nur die isolierten Teilmengen ausgeführt werden. Für die neuen Transaktionen, Dispatcherzustände und Widgetintegration liegt aus dieser Analyse kein vollständiger Testlauf vor.

Der isolierte DPI-Build umfasst DpiRasterTest, EmbedDpiTest, DpiCacheTest, WindowGeometryTest und unter Windows DpiNativeTest. DpiWidgetTest mit Engine-Initialisierung wird erst über den Hauptbuild eingebunden.

**Maßnahme:** Build-/Teststatus je Konfiguration sichtbar führen, installierte Pakete testen und native Integration als eigenes Freigabegate behandeln. **Arbeitspakete:** AP00, AP19 und AP20.

### 4.1 Priorisierung im Überblick

| Rang | Befunde | Nutzerwirkung | Nächster Schritt |
| --- | --- | --- | --- |
| Sofort | B01 | Möglicher falscher Speichererfolg und Verlust des erwarteten Zielstands | AP01 als erster Produktfix |
| Sehr hoch | B02, B03, B10 | Zurückbleibende Zustände, ungültiger Iterator, Prozessabbruch durch Task-Ausnahme | Kleine reproduzierende Tests und begrenzte Korrekturen |
| Hoch | B04, B05, B11, B18 | Hänger, unsichere Lebensdauer, problematische Eingaben | Reproduktion, Thread-/Besitzverträge und Fehlerstrategie |
| Hoch | B06–B09, B19 | Verdeckte Audioengpässe, GUI-Blockaden, fehlende Integrationsnachweise | Messbasis und nichtblockierende Browserabläufe |
| Danach | B12–B17 | Skalierung bei großen Projekten, Darstellung und lange Sitzungen | Profilgestützte Optimierung und Nutzbarkeitsverbesserung |

## 5. Qualitätsziele und Messverfahren

### 5.1 Referenzumgebung festlegen

Vor einer Leistungsbewertung werden CPU, Kernzahl, RAM, GPU, Betriebssystembuild, Qt-/Compilerstand, Audio-Backend, Treiber, Samplerate, Puffergröße, Buildtyp und Pluginbestand dokumentiert. Release-/RelWithDebInfo-Ergebnisse werden nicht mit Debug-/Sanitizerwerten vermischt.

Zwei Lastarten getrennt betrachten: ein reproduzierbarer Offline-/Null-Backend-Lauf für Softwarevergleiche und ein Echtzeittest mit wirklicher Audiohardware. Ein schneller Offline-Render beweist keine störungsfreie Echtzeitwiedergabe.

### 5.2 Zielwerte

Die folgenden Werte sind vorgeschlagene anfängliche Abnahmebudgets. AP06 kalibriert sie gegen eine dokumentierte Referenzmaschine; Abweichungen werden begründet, nicht durch nachträgliches Verschweigen von Messwerten beseitigt.

| Bereich | Vorgeschlagenes Ziel | Verfahren |
| --- | --- | --- |
| Speichern | Kein true und kein Zurücksetzen von Dirty, solange die finale Datei nicht erfolgreich veröffentlicht wurde | Fehler an jedem Dateioperationsschritt injizieren; Inhalt und Wiederherstellung prüfen |
| Audiodeadline | Keine zusätzlichen XRuns im Referenzprojekt durch typische GUI-/MCP-Leseaktionen; 30 Minuten Echtzeittest | Backendzähler plus monotone Zeitmessung; Grundlast separat |
| Audioreserve | p99 der gesamten hostseitigen Periodenarbeit inklusive Sperrwartezeit unter 70 % der Blockdauer im Referenzprojekt | Vorallokierte Messdaten; p50/p95/p99/Maximum und Überschreitungen |
| GUI-Reaktion | p95 unter 50 ms für normale Auswahl-/Editieraktionen; kein vermeidbarer GUI-Stillstand über 100 ms | Eventloop-Verzögerung und Zeit bis sichtbarer Rückmeldung |
| Zeichnen | p95 unter 16,7 ms für definierte Pan-/Zoom-Szenarien auf der Referenzmaschine | Paint-/Framezeiten getrennt nach Editor, Zoom, DPR und Notenanzahl |
| Browser | Sofortige sichtbare Lade-/Abbruchrückmeldung; alte Ergebnisse erscheinen nach Suchwechsel nicht mehr | Langsamer Datenträger, schnelle Auswahl-/Filterfolge |
| DPI | Gleiche logische Bediengeometrie und stimmige Hit-Tests bei 100/125/150/156,25/175/200 % | Automatisierte Tests plus reale Monitorwechsel |
| Speicher | Nach 100 Projekt-/Fenster-/Suchzyklen kein anhaltender Anstieg des lebenden Objektbestands | Heap-/Objektmessung; RSS, Allokatorretention und Shared Caches getrennt erklären |
| Shutdown | Normalfall unter 3 s; fehlerhafter externer Worker endet kontrolliert innerhalb eines dokumentierten Gesamtbudgets | Zeitmessung und Prozess-/Threadinventar |
| Export | Erfolgreiches WAV erfüllt Format, Dauer, Inhalt und Artefaktvertrag; Abbruch meldet niemals Erfolg | Validator, Reload, Hörtest und Fehler-Injektion |

Bei 48 kHz und 256 Frames dauert ein Block ungefähr 5,33 ms; bei 128 Frames ungefähr 2,67 ms. Prozentwerte müssen deshalb immer zusammen mit Samplerate und Blockgröße berichtet werden. Backend-XRuns, hostseitige Deadlineverletzungen und hörbare Fehler sind verwandte, aber nicht identische Messgrößen.

### 5.3 Referenzprojekte

| ID | Fixture | Zweck |
| --- | --- | --- |
| F0 | Leeres Projekt und kleines internes Instrument | Start-/Grundlast und deterministischer Smoke-Test |
| F1 | 16 Spuren, interne Instrumente, normale Effekte und Automation | Typischer Bearbeitungs- und Audiolauf |
| F2 | 64–128 Spuren, viele Clips und Mixer-Routen | Große Arrangementansicht, Routing und Scrollen |
| F3 | MIDI-Clips mit 1.000 / 10.000 / 100.000 Noten einschließlich langer Überlappungen | Paint, Hit-Test, Auswahl, Undo und Snapshots |
| F4 | Samples mit 0/1/wenigen Frames, Stereo-Impulsen, 10-minütigem Audio und Reverse/Trim | Wellenformkorrektheit und Cachebedarf |
| F5 | Defektes XML, abgeschnittene/übermäßig expandierende komprimierte Datei, fehlende Assets | Parser, Ladeabbruch und Zustandswiederherstellung |
| F6 | Kontrolliertes Testplugin: langsam, Startfehler, Crash, lebender Hänger | Prozessisolation und Deadlines |
| F7 | Realer Projektmix mit VST/LV2/ZynAddSubFx/Carla je nach Plattformverfügbarkeit | Kompatibilität; genaue Pluginversionen festhalten |

Fixtures sind klein generiert oder lizenzrechtlich frei nutzbar. Große Stressdaten werden über Seed und Generator reproduziert. Audio-Bitgleichheit nur bei deterministischen internen Quellen fordern; zufällige Synthese und Fremdplugins benötigen eigene Toleranzen und Hörprüfung.

## 6. Konkrete Arbeitspakete

Die Schätzungen sind **Personentage (PT)** für Implementierung, passende Tests und Review einer erfahrenen C++-/Qt-Entwicklung. Sie sind keine Zusage und beruhen noch nicht auf gemessenen Laufzeitprofilen. Die Verantwortlichen werden als Rollen angegeben, weil keine Teamzuordnung bekannt ist.

### AP00 — Vollständigen Referenzbuild herstellen

**Priorität:** P1, sofort beginnen · **Aufwand:** 2–4 PT · **Verantwortung:** Build/CI · **Bezug:** B19

**Umsetzung:**

1. Unterstützte Windows-Referenztoolchain ausdrücklich festlegen; Generator, Compiler, Architektur und Qt-Pakete reproduzierbar auswählen. Die schon in high-dpi.yml behandelte Qt-5.15.2/v142-Anforderung berücksichtigen.
2. SndFile und die weiteren tatsächlichen Hauptbuild-Abhängigkeiten über einen konsistenten vorhandenen Weg bereitstellen. vcpkg.json beziehungsweise vorhandene Plattformskripte verwenden, statt zufällige Header/DLLs aus lokalen Verzeichnissen zu mischen.
3. QtTest als explizite Testabhängigkeit und die Runtime-/Plattformpluginpfade korrekt bereitstellen. Ein Compile-Erfolg ohne startbare Tests reicht nicht.
4. CMake-Presets oder gleichwertige dokumentierte, versionierte Startbefehle für Entwicklung, Tests und Release ergänzen. Erst dabei die Baseline für externe Abhängigkeiten festlegen.
5. Getrennte Buildverzeichnisse für MCP ON/OFF und Qt 5/6 verwenden. Den Hauptbuild einschließlich DpiWidgetTest und nativer AI-Integration ausführen.
6. Ein kompaktes Buildmanifest mit Commit, Features, Toolchain und Abhängigkeiten erzeugen.

**Abnahme:** Auf einer frischen Referenzumgebung entstehen LMMS, Plugins und Testprogramme aus den versionierten Quellen. Alle verfügbaren registrierten Tests laufen; übersprungene Features/Tests sind ausdrücklich ausgewiesen. Keine temporären Ersatzheader im freigegebenen Hauptbuild.

**Abhängigkeit:** Keine. Kleine Produktfixes können parallel vorbereitet werden; die Integrationsfreigabe hängt von diesem Paket ab.

### AP01 — Speichern und Backups fehlersicher machen

**Priorität:** P0 · **Aufwand:** 3–6 PT · **Verantwortung:** Core/Dateien · **Bezug:** B01

**Umsetzung:**

1. Erfolgsvertrag von DataFile::writeFile() festlegen: Erfolg bedeutet vollständig und erfolgreich am finalen Ziel veröffentlicht.
2. Für normale Speicherung QSaveFile am tatsächlichen Ziel verwenden; DirectWriteFallback nicht unbemerkt zur Aufweichung des gewählten Schutzes einsetzen.
3. Backup getrennt vorbereiten und Fehler prüfen. Ein fehlgeschlagenes Backup oder ein fehlgeschlagener Austausch darf nicht zum ungeprüften Entfernen der letzten verlässlichen Projektfassung führen.
4. Schreib-/Streamstatus und commit() prüfen; konkrete Fehlerursache und betroffenen Pfad zurückgeben.
5. Projektbundle zunächst in einem ausschließlich diesem Vorgang gehörenden Stagingbereich aufbauen; Projekt und Ressourcen vor Veröffentlichung prüfen. Teilfehler hinterlassen keine scheinbar fertigen Bundles.
6. Altdateien mit .new/.bak beim Öffnen beziehungsweise in Recovery nachvollziehbar behandeln.
7. Dirty-Status und aktuellem Projektnamen erst nach wirklichem Erfolg den neuen Zustand zuweisen.

**Abnahme:** Tests mit vorhandener .bak, gesperrtem Ziel, fehlenden Rechten, vollem Medium, Schreibfehlern, fehlgeschlagenem Rename und Abbruch zwischen Schritten. Nach jedem Fehler bleibt mindestens die letzte erfolgreich gespeicherte Fassung lesbar; der aktuelle ungespeicherte Zustand bleibt erkennbar. Bundlefehler sind wiederholbar behebbar.

**Abhängigkeit:** Keine; native End-to-End-Abnahme nach AP00. QSaveFile unterstützt atomare Einzeldateiveröffentlichung; mehrere Backup-/Bundleoperationen benötigen weiterhin einen ausdrücklich definierten Ablauf. Siehe [Qt: QSaveFile](https://doc.qt.io/qt-6/qsavefile.html).

### AP02 — Projektlebenszyklus und Recovery konsistent halten

**Priorität:** P1 · **Aufwand:** 2–4 PT · **Verantwortung:** Core/Journal · **Bezug:** B02

**Umsetzung:**

1. Ladeflag, bisherigen Journalmodus und Dateinamen durch Scoped Guards sichern. Verschachtelte Vorgänge müssen den tatsächlichen vorherigen Zustand restaurieren.
2. Strukturprüfung möglichst vor destruktiver Änderung des alten Projekts durchführen; fehlgeschlagene Vorvalidierung lässt Inhalt und Historie bestehen.
3. Für Fehler nach Beginn der Modellübernahme eine ausdrücklich festgelegte Rückkehrstrategie einsetzen: gültiger alter Stand oder klarer, wiederherstellbarer Abbruchzustand.
4. Ladefortschritt, Abbruch, projectLoaded und MCP-Readiness in eine gemeinsame Erfolg-/Fehlersemantik bringen. Ein fehlgeschlagener Versuch ist kein erfolgreich geladener neuer Projektstand.
5. Autosave-Rückgabewert auswerten; wiederholte Fehler verständlich anzeigen und zeitlich begrenzt erneut versuchen.
6. Recovery-Dateien eindeutig einer Sitzung/Instanz zuordnen; mehrere LMMS-Instanzen dürfen einander die Wiederherstellung nicht entfernen. Die derzeitige Pfadstrategie dafür zuerst prüfen.

**Abnahme:** Gültiges Projekt → kaputtes/nicht lesbares Projekt öffnen → weiter editieren → Undo/Redo → Autosave → MCP-Lesen/-Ändern. Danach erfolgreicher weiterer Ladeversuch. Alle relevanten Flags, Journalmodi und Revisionsübergänge stimmen nach Erfolg, Fehler und Abbruch.

**Abhängigkeiten:** AP01 für sichere Autosave-/Recoveryveröffentlichung; AP00 für den Gesamttest.

### AP03 — Wellenform- und Thumbnail-Grenzen korrigieren

**Priorität:** P1 · **Aufwand:** 3–6 PT · **Verantwortung:** GUI/Audio-Daten · **Bezug:** B03, B15

**Umsetzung:**

1. Datenzugriff in zwei explizite gültige Quellen aufteilen: Sampleframes oder Peakstufe. Nach Wahl des Originalbuffers keinen Peakiterator mehr dereferenzieren.
2. Framezahl, Kanalzahl und flache Samplezahl sauber unterscheiden; gemeinsame Bereichshilfen für Anfang/Ende und Reverse verwenden.
3. Leere Rechtecke, Nullbreite, leere Audiodaten, ungültige/NaN-Trimwerte und vollständig außerhalb liegende Viewports früh behandeln.
4. Halb offene Intervalle so bilden, dass jede Aggregation gültig ist und Endpunkte weder fehlen noch außerhalb liegen.
5. Referenzanzeige aus einer einfachen, korrekten Peakberechnung gegen optimierte Pfade vergleichen.
6. Cacheweiterverwendung an eine Datenrevision binden; teure Erzeugung von der Anzeige entkoppeln, soweit AP17 dafür die Infrastruktur liefert.

**Abnahme:** Originalbuffer-Fallback bei Zielbreiten oberhalb der feinsten Peakstufe, die von der Framezahl abweichen; 0/1/2/kurze Samples, Stereo-Impulse nur links/rechts, Trim, Reverse und Extremzoom. AddressSanitizer/UBSan melden keine ungültigen Zugriffe; Bild-/Peakergebnisse entsprechen der Referenz.

**Abhängigkeit:** AP00 für Tests mit echten Sampletypen; die Kernkorrektur benötigt keinen GUI-Neuentwurf.

### AP04 — Audio-Worker und Jobqueue stabilisieren

**Priorität:** P1 · **Aufwand:** 6–12 PT · **Verantwortung:** Audio/Nebenläufigkeit · **Bezug:** B04, B07

**Umsetzung:**

1. m_quit durch einen ausdrücklich synchronisierten Zustand ersetzen. Atomics allein lösen nicht die gesamte Wakeup-/Lebensdauerlogik.
2. Für Arbeitsgeneration, Queuebereitschaft und Stop ein überprüfbares Prädikat festlegen; Wakeups und Zustandswechsel konsistent synchronisieren.
3. Jobzustände und Publikationsreihenfolge auch im Dynamic-Modus prüfen: reservierter Index, publizierter Job, verarbeiteter Job und Abschlusszähler müssen zueinander passen.
4. Threadanzahl sinnvoll begrenzen und Fallback für nicht verfügbare CPU-Anzahl ergänzen; kleine Projekte nicht durch unnötiges Aufwecken aller Kerne belasten.
5. Queueüberlauf als expliziten Zustand behandeln. Kein stiller Verlust eines bereits als queued markierten Jobs; geeignete Fallbackstrategie mit Abhängigkeitsgraph prüfen.
6. Stop/Join erst nach Stilllegung weiterer Audio-/Jobproduzenten durchführen. Ergebnisse von wait() prüfen; keine laufenden Threads mit ihren benötigten Daten zerstören.
7. Spin-/Sleep-/Wakeupstrategie unter geringer und hoher Last messen. Kein allgemeines Verbot jedes Spin-Waits, aber ein definiertes Verhalten bei ausgefallenen Jobs.

**Abnahme:** 1.000 kontrollierte Start-/Stop-/Generationswechsel, Queuekapazität ±1, dynamisch erzeugte Jobs, verzögerte Worker, Shutdown in unterschiedlichen Phasen. Kein verlorener Job, kein Hänger, kein Zugriff nach Freigabe; TSan-Stresslauf ohne neue eigene Datenrennen.

**Abhängigkeiten:** AP00 und AP06. Den Scheduler schrittweise ersetzen und nach jedem Teilstück Audioverhalten vergleichen.

### AP05 — Allgemeinen ThreadPool gegen Fehler absichern

**Priorität:** P1 · **Aufwand:** 2–4 PT · **Verantwortung:** Core/Nebenläufigkeit · **Bezug:** B10

**Umsetzung:**

1. Ausnahmegrenze im Taskwrapper setzen und std::current_exception() mit promise::set_exception() weitergeben.
2. Mindestworkerzahl und konfigurierbares Maximum einsetzen; CPU-Erkennung mit Rückgabe 0 gezielt testen.
3. Definieren, ob Shutdown wartende Tasks ausführt oder abbricht. Jeder angenommene Auftrag erhält genau ein Ergebnis oder eine dokumentierte Abbruchausnahme.
4. Enqueue während beginnendem Shutdown ablehnen; unbeschränktes Anwachsen der Queue verhindern.
5. Keine ungefangene Ausnahme aus Future::get() in GUI-Destruktoren zulassen. FileSearchJob und andere Nutzer an den neuen Vertrag anpassen.

**Abnahme:** Werfender Task lässt LMMS nicht abstürzen; Future enthält die Ausnahme. Erfolgreiche, fehlgeschlagene und abgebrochene Aufgaben werden beendet; kein Future bleibt wegen verworfener Arbeit unaufgelöst.

**Abhängigkeit:** Keine; Integration mit AP08.

### AP06 — Audio-, GUI- und Speichermessung ergänzen

**Priorität:** P1 · **Aufwand:** 3–5 PT · **Verantwortung:** Performance/Audio · **Bezug:** B06, B07, B12

**Umsetzung:**

1. Monotone Zeitpunkte vor Sperrzugriff, nach Sperrerwerb, pro Verarbeitungsstufe und am Backend erfassen.
2. Deadlineüberschreitungen und echte Backend-XRuns separat zählen; Histogramme mit p50/p95/p99/Maximum ergänzen.
3. Messdatensätze in einem vorallokierten, begrenzten Ring ablegen; Formatierung und Datei-I/O in einem Diagnoseworker durchführen.
4. Eventloop-Lag, Paintzeiten, Paintanzahl, sichtbare Widgetzahl, Cachebytes und Joblaufzeiten ergänzen.
5. Benchmark-Fixtures und ein maschinenlesbares Ergebnisformat mit Build-/Hardwaremetadaten anlegen.
6. Messkosten mit deaktivierter und aktivierter Erfassung vergleichen; volle Traceaufzeichnung gezielt zuschaltbar halten.

**Abnahme:** Künstliche Sperrwartezeit erscheint in der Diagnose. Ein absichtlich verzögerter Audioblock erhöht Überschreitungszähler, auch wenn die geglättete Anzeige niedrig bleibt. Im Audiopfad erfolgen keine Dateioperationen der neuen Diagnose.

**Abhängigkeit:** AP00 für den Gesamtbenchmark. Die bestehenden AudioEngineProfiler-Probes werden weiterverwendet.

### AP07 — Variable Arbeit im Audiopfad reduzieren

**Priorität:** P1 · **Aufwand:** 6–12 PT · **Verantwortung:** Audio/Core · **Bezug:** B07

**Umsetzung:**

1. Häufigkeit und Kosten von PlayHandle-Erzeugung/-Entfernung, Modelllocks und Containerwachstum aus AP06 auswerten.
2. Kapazitäten vor Wiedergabestart reservieren und wiederverwendbare Nicht-Noten-Handles/Arbeitsdaten prüfen. Der vorhandene NotePlayHandleManager wird nicht ignoriert.
3. Löschungen und aufwendige Destruktoren nach einem sicheren Epoch-/Besitzvertrag außerhalb der Periode ausführen; keine Freigabe, solange Worker noch Referenzen halten.
4. Entfernen mehrerer Handles in einem Durchlauf statt wiederholter linearer Suche prüfen.
5. Modelländerungen in kurze, klar begrenzte Übergaben aufteilen. Teure Datei-/Pluginvorbereitung außerhalb der Audio-Sperre durchführen, endgültige Übernahme klein halten.
6. Überlast und Queuefüllstände über Zähler melden; Ausgabeformatierung und Warnungen außerhalb des zeitkritischen Threads.

**Abnahme:** Allokations-/Lockprofil zeigt den beseitigten Hotspot; Referenzsound und Reihenfolge bleiben korrekt. p99 und XRun-Zahlen verbessern sich oder bleiben innerhalb des freigegebenen Budgets. Keine globale „lock-free“-Migration ohne Nutzenbeleg.

**Abhängigkeiten:** AP04 und AP06; Ownershipänderungen mit AP09/AP14 abstimmen.

### AP08 — Browser-Suche und Vorschau asynchron und abbrechbar machen

**Priorität:** P1 · **Aufwand:** 5–9 PT · **Verantwortung:** GUI/Datei-I/O · **Bezug:** B08, B09

**Umsetzung:**

1. Checkboxzustand für versteckte Inhalte korrekt prüfen und durch Verhaltenstest absichern.
2. Such-/Vorschauläufe mit monotoner Generation oder Request-ID kennzeichnen. Nur Ergebnisse der aktuellen Generation dürfen die Ansicht beziehungsweise Wiedergabe verändern.
3. Neues Suchen darf den GUI-Thread nicht mit future.get() auf den alten Auftrag warten lassen. Stopzustand und Jobbesitz unabhängig von der Lebensdauer des Widgets halten.
4. Eingaben um etwa 150–250 ms zusammenfassen; Treffer in begrenzten Batches liefern. Filterwechsel und Leeren beenden den logischen Lauf sofort.
5. Große Trefferlisten mit einem Model/View-Ansatz oder stückweiser Anzeige darstellen; aktuelle Auswahl und Verzeichniszustand dabei erhalten.
6. Sample-I/O und Decoding in geeignete Worker verschieben. Die GUI übernimmt fertige immutable Daten; neue Auswahl verwirft alte Resultate.
7. Bei Presets/Plugins zuerst Anforderungen an GUI-/Hauptthread und Audioengine prüfen. Nicht pauschal QObjects oder native Pluginfenster in Hintergrundthreads erzeugen.
8. Lade-/Fehler-/Abbruchzustand an derselben Stelle anzeigen; processEvents() aus dem alten Vorschau-Wartepfad entfernen.

**Abnahme:** Schnelles Tippen, 100.000 Dateieinträge, fehlende/netzwerkbasierte Laufwerke, Wechsel von A nach B während A lädt, Schließen während Suche/Vorschau. Keine alten Treffer und kein verspätetes Abspielen von A; Checkboxverhalten stimmt; Oberfläche bleibt bedienbar.

**Abhängigkeiten:** AP05; AP06 für Zeitnachweise. Unabbrechbare Betriebssystem-I/O braucht isolierten Jobbesitz statt einer unrealistischen Sofortabbruchgarantie.

### AP09 — Audioanzeigen von Audio- und GUI-Lebensdauer entkoppeln

**Priorität:** P1 · **Aufwand:** 4–8 PT · **Verantwortung:** Audio/GUI · **Bezug:** B11

**Umsetzung:**

1. Vertrag für Anzeigen definieren: Anzeige bekommt eigene stabile Messdaten; Audiopuffer bleiben im Besitz der Engine.
2. Für Wellenform/RMS/Peaks vorallokierte SPSC- oder Snapshotpuffer verwenden. Beim Überlaufen dürfen Anzeigeframes entfallen, Audioblöcke bleiben priorisiert.
3. Die GUI holt den neuesten vollständigen Stand in begrenzter Frequenz ab; sie muss nicht jede Audioperiode als Qt-Ereignis erhalten.
4. Sichtbarkeit/Aktivität koppeln: ausgeblendete oder minimierte Anzeigen reduzieren Arbeit. Vorhandene Spectrum-/Waterfall-Aktivitäts-APIs nutzen.
5. Ändert sich die Blockgröße, werden Puffer sicher neu aufgebaut; das Oscilloscope darf nicht von einer für immer unveränderten Startgröße ausgehen.
6. Verbindungslaufzeit und mehrfaches Aktivieren/Deaktivieren testen; keine doppelten Signalverbindungen ansammeln.

**Abnahme:** Künstlich blockierte GUI erhält später einen gültigen aktuellen Snapshot. Kein Rohzeiger auf wiederverwendeten Audiopuffer in wartenden Anzeigenachrichten; Raceprüfung und Audioreserve bestanden.

**Abhängigkeiten:** AP04/AP06. Qt-Widgets verbleiben im Hauptthread; siehe [Qt: Threads und QObjects](https://doc.qt.io/qt-6/threads-qobject.html).

### AP10 — Editorzeichnung und Interaktion für große Projekte optimieren

**Priorität:** P2 · **Aufwand:** 5–10 PT · **Verantwortung:** GUI/Performance · **Bezug:** B12

**Umsetzung:**

1. F2/F3 für Piano Roll, Song Editor, Automation und Clipvorschauen messen; Raster, Noten, Texte und Hit-Tests getrennt erfassen.
2. Nur bei relevantem Anteil zeitlichen Kandidatenindex ergänzen. Noten, die vor dem Viewport beginnen und hineinragen, sowie längere Detuningkurven bleiben auffindbar.
3. Modellrevisionen und Änderungen halten den Index inkrementell aktuell; Drag, Quantisierung, Undo/Redo, MIDI-Import und MCP lösen dieselben Invalidierungen aus.
4. Statisches Raster und dynamische Inhalte trennen; Cache nach Geometrie, Zoom, DPR, Theme und relevanten Datenrevisionen invalidieren.
5. Rechteckbegrenzte update()-Aufrufe bevorzugen und unnötige synchrone repaint()-Aufrufe anhand ihres sichtbaren Verhaltens ersetzen.
6. Unsichtbare Clips/Tracks nicht laufend vollständig neu rasterisieren; Platzhalter/Lazy-Aufbau beim ersten Sichtbarwerden prüfen.

**Abnahme:** Darstellung und Auswahl entsprechen vorherigen Ergebnissen; Noten an beiden Viewportkanten und überlange Noten bleiben korrekt. Ein kleiner sichtbarer Ausschnitt eines 100.000-Noten-Clips verursacht nach Indexaufbau deutlich weniger Kandidatenarbeit. Messprotokoll nennt auch Indexkosten und Speicher.

**Abhängigkeiten:** AP03, AP06 und AP09; keine Optimierung auf Kosten von Undo-/Modellkorrektheit.

### AP11 — DPI- und Grafikressourcen vervollständigen

**Priorität:** P2 · **Aufwand:** 4–8 PT · **Verantwortung:** GUI/Grafik · **Bezug:** B13

**Umsetzung:**

1. Inventar aller verbleibenden getIconPixmap()-Verbraucher, statischen QPixmaps, Spritezugriffe und selbst erzeugten QImage-/QPixmap-Caches erstellen.
2. Für jeden Verbraucher logische Geometrie, Rasterauflösung und Refreshquelle dokumentieren. Insbesondere Oscilloscope an die bereits verwendeten Ressourcen-/Dpi::watch-Muster anbinden.
3. DPR-, Theme-, Font-, Palette-, Style-, Resize- und Reparentereignisse nur soweit nötig invalidieren; redundante Rebuilds messen.
4. Fehlende Vektor- oder höher aufgelöste Ressourcen nach Nutzerhäufigkeit ergänzen, ohne Spriteformate blind zu skalieren.
5. Native Pluginfenster separat prüfen; eingebettet und abgelöst müssen Geometrie und Eingabe übereinstimmen.
6. Ergebnisse des älteren DPI-Plans gegen die aktuelle Implementierung aktualisieren; erledigte Änderungen nur mit Nachweis abhaken.

**Abnahme:** Alle Kernfenster und Plugins in F7 bei den Zielskalierungen geprüft, einschließlich untätiger Regler, Tooltip, Splash, Dialoge und abgelöster Editoren. 20 Monitorwechsel und Dock-/Undockzyklen ohne Größenwanderung, Unschärfewechsel durch veraltete Caches oder falsche Hit-Tests.

**Abhängigkeiten:** AP00 und AP06; bestehende DPI-Tests bleiben grün. Grundprinzipien geräteunabhängiger Koordinaten siehe [Qt: High DPI](https://doc.qt.io/qt-6/highdpi.html).

### AP12 — Layout, Lesbarkeit, Fokus und Bedienzustände verbessern

**Priorität:** P2 · **Aufwand:** 5–9 PT · **Verantwortung:** GUI/UX · **Bezug:** B14

**Umsetzung:**

1. Dialoge und Toolbars mit Deutsch/Englisch, langen Übersetzungen und vergrößerter Standardschrift prüfen.
2. Platzbedarf textmetrisch über sizeHint(), Layouts und QFontMetrics ermitteln. Feste Pixelmaße nur für bewusst gestaltete grafische Elemente beibehalten.
3. Trennbare Einstellungen für System-DPI, Editorzoom und gegebenenfalls Benutzeroberflächenschrift klar behandeln; keine doppelte DPR-Multiplikation.
4. Zu kleine Arbeitsflächen durch Scrollbereiche, sinnvolles Umbruch-/Overflowverhalten und erreichbare Bestätigungs-/Abbruchknöpfe unterstützen.
5. Fokusreihenfolge, sichtbaren Tastaturfokus, Enter/Escape, Tastaturregler und Kontextmenüs standardisieren.
6. Selbst gezeichnete Controls auf Rolle, Namen, aktuellen Wert und zugängliche Aktionen prüfen; Standard-Qt-Accessibility weiterverwenden.
7. Zustände Auswahl, Hover, deaktiviert, Fehler, Laden und verbunden anhand konkreter Screens konsistent darstellen. Farbe nicht als einzigen Statushinweis verwenden.

**Abnahme:** Standardarbeitsabläufe lassen sich per Tastatur durchführen; fokussierte Controls sind erkennbar. Keine abgeschnittenen wesentlichen Texte bei 125–200 % und größerer Schrift auf festgelegten Arbeitsflächen. Screenreader-Stichprobe für Transport, Regler, Trackauswahl und Dialoge.

**Abhängigkeit:** AP11 liefert das technische Koordinatenmodell; kein vollständiger Theme-Neuentwurf erforderlich.

### AP13 — Dateiladen und Formatmigration robust begrenzen

**Priorität:** P1 · **Aufwand:** 4–8 PT · **Verantwortung:** Core/Dateiformate · **Bezug:** B18

**Umsetzung:**

1. Eigene Limits für komprimierte Dateigröße, tatsächlich entpackte Daten, XML-Struktur und Anzahl musikalischer Objekte definieren.
2. Eine deklarierte entpackte Größe nicht allein vertrauen: Eingabekopf prüfen und einen tatsächlich begrenzten Dekompressionspfad verwenden.
3. Stammknoten, Dateityp, Versionsfelder und numerische Werte validieren, bevor das alte Projekt verworfen oder teure Objekte erzeugt werden.
4. Fehler aus Parser/Upgrades als strukturierte Ergebnisse an GUI und CLI weitergeben; nur die GUI entscheidet über Dialoge.
5. Upgrade-Fixtures aus unterstützten älteren Formaten ergänzen. Zweifelhafte Inhalte entweder verständlich ablehnen oder nach dokumentierter Regel reparieren.
6. Kleine Fuzzing-Harnesses für Parser, komprimierte Eingaben und kritische Upgradefunktionen mit ASan/UBSan betreiben.

**Abnahme:** Korrupte/extreme Dateien führen zu begrenztem Ressourcenverbrauch und einem eindeutigen Fehler. Bestehendes Projekt bleibt bedienbar; reguläre große Fixtures funktionieren innerhalb dokumentierter Grenzen.

**Abhängigkeiten:** AP02 für Zustandswiederherstellung, AP00 für Sanitizer-/Parserharness.

### AP14 — Pluginprozesse und ihre Fehlerzustände stabilisieren

**Priorität:** P1 · **Aufwand:** 6–12 PT · **Verantwortung:** Pluginhost/Audio · **Bezug:** B05

**Umsetzung:**

1. Zustände Starting, Ready, Processing, Failed, Stopping und Stopped mit erlaubten Übergängen definieren.
2. Separate monotone Deadlines für Initialisierung, GUI-Anfragen, Audioblock und Shutdown einführen; alle Transportpfade einschließlich Shared Memory berücksichtigen.
3. Ein lebendes, hängendes Plugin mit unabhängigem Watchdog erkennen. Die Audioengine darf nicht unbegrenzt auf seine Nachricht warten.
4. Verhalten bei Ausfall festlegen: kontrollierte Stille oder geeigneter Bypass, sichtbarer Fehler am betroffenen Kanal und Erhalt des übrigen Projekts.
5. Neustart aus dem letzten sicheren Pluginzustand als ausdrückliche Nutzeraktion anbieten, sofern der jeweilige Pluginvertrag das zulässt.
6. Kommunikation im Fehlerfall invalidieren; blockierte Leser/Schreiber freigeben und Prozesse mit begrenzter Eskalation beenden.
7. Verschachteltes processEvents()-Warten durch asynchrone Antworten ersetzen, wo Lebenszyklus und vorhandene APIs dies ermöglichen.
8. DPI-/Fensteränderungen aus AP11 mit Plugininitialisierung und Closing kombinieren.

**Abnahme:** F6 für Startfehler, Crash und lebenden Hänger; mehrfaches Öffnen/Schließen; 32-/64-Bit-Bridges soweit unterstützt. Übrige Tracks bleiben nutzbar und der Hauptprozess lässt sich beenden. Kein pauschales gewaltsames Beenden eigener Threads.

**Abhängigkeiten:** AP04, AP06 und AP09. Der Umfang beginnt bei vorhandenen Remote-Hosts; Isolation aller bislang im Prozess laufenden Plugins ist ein gesondertes Folgeprojekt.

### AP15 — MCP mit Anwendung, Journal und Shutdown integriert abnehmen

**Priorität:** P1 · **Aufwand:** 5–9 PT · **Verantwortung:** MCP/Core · **Bezug:** B02, B05, B19

**Umsetzung:**

1. Vorhandene native Dispatcher-, Transaktions-, Revisions-, Registry- und Jobtests im vollständigen Programm ausführen.
2. GUI-Gegenänderung zwischen Preview und Apply, Projektwechsel, Rechtewiderruf, Verbindungsverlust und wiederholte Operationsschlüssel prüfen.
3. Aktuelle Regel beibehalten: Batchtransaktionen ändern kein laufendes/ladendes/exportierendes Projekt. Andere Aktionen wie Lesen, Assetvorbereitung und Renderstart getrennt auf GUI-/Audioaufwand messen.
4. Undo/Redo für jede unterstützte Mutationsfamilie einschließlich Automation, Mixer, PatternStore und gelöschten Besitzern prüfen.
5. StopService als asynchronen, zeitlich begrenzten Shutdown mit definierter Queueablehnung implementieren; ausstehende Antworten und doppelte Wartende erhalten einen eindeutigen Abschluss.
6. AiLocalServer und seine Sockets im korrekten Besitzerthread schließen/zerstören; erst danach den Thread vollständig beenden und veröffentlichte Verbindung entfernen.
7. WANT_MCP=OFF und --no-mcp tatsächlich starten; kein versteckter Dienst-/Runtimebedarf in diesen Betriebsarten.

**Abnahme:** Lesen → Preview → Apply → Undo → Redo → Speichern → Reload funktioniert mit revisionsrichtigen Ergebnissen. Abgebrochene/abgelehnte Änderungen erzeugen keine Teilmutation oder falsche Historie. Shutdown funktioniert mit aktiven Readers/Writern, voller Queue und laufendem Renderjob.

**Abhängigkeiten:** AP00, AP02 und AP06; vorhandene Sicherheits-/Rechteverträge aus dem MCP-Plan erhalten.

### AP16 — Rendering und Artefaktzugriff für wirkliche Projekte nutzbar machen

**Priorität:** P1/P2 · **Aufwand:** 5–9 PT · **Verantwortung:** Export/MCP · **Bezug:** B16

**Umsetzung:**

1. Snapshot-Aufbau im Anwendungsthread und Ressourcenarbeit im Worker getrennt messen; besonders große Projekte und wiederholte Ressourcenpfade prüfen.
2. Exportstatus in klare terminale Zustände bringen; Timeout, FailedToStart, Crash, Abbruch und ungültiges WAV dürfen keinen Erfolg liefern.
3. Artefaktgröße und Transportbudget am üblichen Songumfang ausrichten; Grenzen im Toolvertrag und GUI-Status sichtbar machen. Download und Renderbarkeit nicht widersprüchlich gestalten.
4. Artefakte in einem kontrollierten unveränderlichen Bestand halten oder eine gleichwertig starke Integritätsstrategie festlegen. Externe Zielkopie und herunterladbare Quelle nötigenfalls trennen.
5. Hashing/Lesen außerhalb von GUI-Thread und globalem Jobmutex ausführen; einmalige Validierung mit sicherem Datei-/Generationsbezug statt Vollhash pro Chunk.
6. Große/mehrfache Abrufe abbrechbar machen und begrenzen; negative Offsets, Grenzgrößen, veränderte Dateien und erneute Abholung testen.
7. Ressourcenbehandlung für SF2/GIG/Zyn-/Pluginzustände dokumentieren. Nicht eingefrorene Abhängigkeiten müssen als solche erkennbar sein.
8. Bestehendes WAV-Mixdown stabilisieren, bevor neue Formate oder Stems ergänzt werden.

**Abnahme:** Mehrminütiger Stereoexport mit 16/24/32 Bit, erfolgreiches Abholen über mehrere Chunks und passende SHA-256-/Dauermetadaten. Ändern der Downloadquelle wird zuverlässig erkannt; GUI bleibt reaktionsfähig. Vorhandene Exportziele werden gemäß bestehendem Vertrag nicht ungewollt überschrieben.

**Abhängigkeiten:** AP01, AP06, AP15; AP17 für Aufbewahrung und Speicherbudgets.

### AP17 — Caches, Jobverläufe und Aufbewahrung budgetieren

**Priorität:** P2 · **Aufwand:** 3–6 PT · **Verantwortung:** Core/Performance · **Bezug:** B15, B17

**Umsetzung:**

1. Bytekosten für Thumbnailstufen, GUI-Raster, Journalstände, Operationsantworten und Artefakte erfassen.
2. Bestehende 128-MiB-Journal- und 64-MiB-Transaktionsgrenzen in Diagnose und Tests aufnehmen. Große bereits aktive Objekte nicht durch Cacheeviction beschädigen.
3. Thumbnailmap mit Bytebudget und nachvollziehbarem LRU-/Revisionsverhalten ausstatten. Von Widgets weiter gehaltene Daten gesondert messen.
4. Aktive Renderjobs von historischem Jobbestand trennen; terminale Jobs nach definierter Aufbewahrung oder expliziter Freigabe entfernen.
5. Operationsgarantie für 24 Stunden erhalten oder bewusst versioniert ändern. Bei Druck zuerst große Antwortdaten auslagern/kompakt speichern; Idempotenzidentität bleibt eindeutig.
6. Begrenzungen mit verständlichem Busy-/Limitstatus, aktuellen Werten und Rückkehrmöglichkeit melden.
7. Private abgebrochene Snapshots beim Start beziehungsweise nach Jobs sicher aufräumen; ausschließlich eigene, eindeutig markierte Verzeichnisse berücksichtigen.

**Abnahme:** Über 128 abgeschlossene Renderjobs in einer langen Sitzung führen bei geltender Freigaberegel nicht zum dauerhaften Stillstand. Wiederholung junger Operationsschlüssel bleibt korrekt. Speicher folgt den festgelegten Budgets; Cacheverdrängung verändert keine Audio-/Projektinhalte.

**Abhängigkeiten:** AP03, AP06 und AP16.

### AP18 — Diagnose und Fehlermeldungen vereinheitlichen

**Priorität:** P2 · **Aufwand:** 3–5 PT · **Verantwortung:** Core/Support · **Bezug:** querschnittlich

**Umsetzung:**

1. Fehlerklassen und Codes für Laden, Speichern, Audio-Backend, Plugin, Export und MCP in einer kleinen gemeinsamen Struktur abbilden.
2. Meldungen nennen Aktion, konkrete Ursache, betroffenen Gegenstand und sinnvollen nächsten Schritt. Hintergrundfehler nicht als unerwartete Dialogflut darstellen.
3. Logkategorien und begrenzte Logdateien einführen/vereinheitlichen; Echtzeitthreads liefern nur kleine Datensätze.
4. Diagnosepaket mit Buildmanifest, Audioeinstellungen, XRun-/Latenzzählern und letzten Fehlercodes erzeugen. Projektinhalte, Tokens und private Pfade vor Weitergabe kontrollierbar machen.
5. Plattformgeeignete Crash-/Hang-Diagnose mit Symbolen und Threadstacks für Entwicklerbuilds vorsehen; keine automatischen Uploads voraussetzen.

**Abnahme:** Die in AP01/AP02/AP14/AP16 injizierten Fehler sind ohne Debugger unterscheidbar. Diagnosefunktion beeinflusst Audiodeadlines nicht merklich und schreibt keine Verbindungsgeheimnisse ins Log.

**Abhängigkeit:** AP06 für Messdaten; Fehlerstrukturen schrittweise an vorhandene Schnittstellen anbinden.

### AP19 — Regressionstests und CI als Freigabegates ausbauen

**Priorität:** P1 · **Aufwand:** 5–10 PT · **Verantwortung:** QA/CI · **Bezug:** B19

**Umsetzung:**

1. Schnelle Tests für bekannte Fehlerpfade pro PR ausführen. Laufzeiten und geeignete Timeouts in CTest festlegen.
2. Vollständigen Hauptbuild mit allen nativen Tests einschließlich DpiWidgetTest regelmäßig und für Releases zwingend ausführen.
3. Mindestens ein vollständiger MCP-OFF-Build und ein CLI-Renderlauf ohne MCP in die Matrix aufnehmen; ON/OFF darf nicht nur vorkompilierte Einzeldateien bedeuten.
4. ASan/UBSan und TSan in getrennten geeigneten Debug-Jobs mit kontrollierten internen Plugins betreiben. Bestehende WANT_DEBUG_*-Optionen nutzen und wirksame Compiler-/Linkflags im Build nachweisen.
5. Snapshot-/Bildtests für kritische GUI-Screens ergänzen. Fonts, Plattformplugin und Theme fixieren; Antialiasingtoleranz von Geometrie-/Hit-Testfehlern trennen.
6. Audio-, Parser- und Fehlersimulationsfixtures versionieren. Tests mit absichtlich hängenden Prozessen besitzen eigene harte äußere Zeitlimits.
7. Performancevergleiche auf dedizierter Referenzhardware beziehungsweise stabiler kontrollierter Umgebung ausführen. Schwankende gehostete CI nur als groben Hinweis verwenden.
8. Reale Monitor-/Audio-/Fremdpluginabnahmen als manuelle Checkliste dokumentieren; sie nicht mit Offscreen-Tests als erledigt markieren.
9. Prüfen, ob bestehende Checks und Compilerwarnungen die neuen Dateien erfassen. Kritische Checks schrittweise für geänderten eigenen Code verpflichtend machen; Drittanbieterrauschen isolieren.

**Abnahme:** Jede P0/P1-Korrektur besitzt einen passenden reproduzierenden Test. CI zeigt Konfiguration, ausgeführte Tests und Skips eindeutig; keine Freigabe aufgrund alleiniger Companion-/DPI-Teiltests.

**Abhängigkeit:** AP00; die Regressionen entstehen jeweils mit den Produktfixes und werden in diesem Paket zusammengeführt.

### AP20 — Installierte Pakete, Langzeitbetrieb und Release absichern

**Priorität:** P1 vor Freigabe · **Aufwand:** 3–5 PT · **Verantwortung:** Release/QA · **Bezug:** B19 und alle Laufzeitpakete

**Umsetzung:**

1. Windows-/Linux-/macOS-Pakete in frischen Umgebungen ohne Entwickler-PATH testen; unterstützte Kombinationen konkret benennen.
2. Qt-Plugins, Audio-Backends, Remoteplugin-Helfer, Node-Runtime, Companion und Lizenz-/Integritätsdateien auf Vollständigkeit prüfen.
3. Tatsächliches Host-/Helfer-Manifest und Fensterverhalten im installierten Windows-Paket prüfen, nicht nur das DPI-Testprogramm.
4. Mindestens acht Stunden gemischten Betrieb mit Wiedergabe, Browser, Autosave, Projektwechseln und Renderjobs durchführen.
5. Wiederholtes Starten/Schließen, mehrere Instanzen, fehlende Audiogeräte und Monitor-Docking prüfen.
6. Kompatible Projektdateien, Konfiguration und zuletzt gespeicherte gute Fassung für den Rückweg erhalten. Neue stabile Funktionen schrittweise freigeben.
7. Releasehinweise nennen behobene Auslöser, verbleibende bekannte Probleme und nachgewiesene Plattformen.

**Abnahme:** Keine offenen P0 und keine unbegründet akzeptierten P1 im freizugebenden Umfang. Langzeit-, Hör-, Paket- und Wiederherstellungstests bestehen; bekannte Einschränkungen sind ausdrücklich dokumentiert.

**Abhängigkeiten:** AP01–AP19 gemäß gewähltem Releaseumfang. Ein kleiner Hotfix kann einen engeren, aber vollständig getesteten Umfang haben.

## 7. Umsetzungsreihenfolge und Aufwand

### 7.1 Phasen mit überprüfbaren Ergebnissen

| Phase | Inhalt | Ergebnis / Gate |
| --- | --- | --- |
| 0: Reproduzierbare Basis | AP00, Beginn AP06/AP19 | Vollständiger Referenzbuild, Testinventar, erste Baselineberichte |
| 1: Konkrete Fehler schließen | AP01, AP02, AP03, AP05; Hidden-Checkboxfix aus AP08 | Speicherung korrekt; Ladefehler hinterlassen keine blockierte Sitzung; Wellenformfallback und Task-Ausnahmen abgesichert |
| 2: Nebenläufigkeit und Hänger | AP04, AP09, AP14, AP15 | Verlässliche Besitzer-/Threadgrenzen; kontrollierter Plugin-/Serviceausfall und Shutdown |
| 3: Reaktionsfähigkeit und Last | AP07, AP08, AP10, AP16, AP17 | Geringere GUI-Blockaden, messbare Audiolatenzreserve, nutzbare lange Sitzungen und Exporte |
| 4: Darstellungsqualität | AP11, AP12 | Konsistente skalierte Kernoberfläche, lesbare Layouts und bedienbare Controls |
| 5: Breite Stabilisierung | AP13, AP18, Abschluss AP19/AP20 | Robuste Dateigrenzen, Diagnose, Langzeit- und Paketabnahme |

AP13 beginnt früher, sobald die Vorvalidierung aus AP02 steht; die Tabelle beschreibt die Schwerpunkte, keine künstliche Sperre bis Phase 5. AP19 begleitet jede Phase. Auch DPI-Regressionschecks laufen ab dem ersten Fix.

### 7.2 Kritische Abhängigkeiten

~~~text
AP00 Referenzbuild ---------> AP19 integrierte Tests ------------------+
         |                                                          |
         +-> AP01 Speicherfix -> AP02 Lifecycle -> AP15 MCP ---------+
         +-> AP03 Thumbnail -----------------------------------------+
         +-> AP06 Messung -> AP04 Worker -> AP07 Audiohotspots -------+
                         |            +-> AP09 Anzeigen -> AP10 Paint|
                         +-> AP14 Pluginfehler ----------------------+
AP05 ThreadPool -> AP08 Browser -------------------------------------+
AP11 DPI -> AP12 Layout ---------------------------------------------+
AP13 Parser / AP16 Export -> AP17 Budgets / AP18 Diagnose ------------+
                                                                    v
                                                            AP20 Release
~~~

Gemeinsame Basisklassen wie AudioEngine, ProjectJournal, DpiContext und der Dispatcher erhalten kleine, getrennt prüfbare Änderungen. Audio-Ownership und Schedulerumbau sollten nicht im selben unübersichtlichen Commit mit einer optischen Überarbeitung vermischt werden.

### 7.3 Aufwand und sinnvolle Lieferabschnitte

Die Summe der 21 Arbeitspakete beträgt **84–161 PT**. Mit 20–30 % Reserve für bislang nicht reproduzierte Nebenläufigkeitsprobleme und plattformspezifische Pluginfälle liegt der grobe Planungsrahmen bei **etwa 100–210 PT**.

Das ist eine initiale Bandbreite für den gesamten beschriebenen Umfang, keine gemessene Fertigstellungsprognose. Bei einer Person und fünf Entwicklungstagen pro Woche entspricht sie grob 20–42 Wochen; Reviews, Verfügbarkeit und externe Abhängigkeiten können die Kalenderzeit verlängern. Mehrere Personen können voneinander unabhängige Pakete übernehmen, die kritischen Audio-/Lifecycleabhängigkeiten bleiben dennoch bestehen.

Sinnvolle Lieferabschnitte:

| Lieferabschnitt | Umfang | Erfolgskriterium |
| --- | --- | --- |
| A: Stabilitäts-Hotfix | AP00, AP01–AP03, AP05, kleiner Checkboxfix, passende Regressionen | Keine falsche Speichermeldung; benannte konkrete Fehler geschlossen |
| B: Belastbare Laufzeit | AP04, AP06–AP09, AP14/AP15, wesentliche AP18-Diagnose | Keine unkontrollierten Warte-/Lebensdauerpfade im geprüften Kernumfang |
| C: Große Projekte und Darstellung | AP10–AP13, AP16/AP17 | Messbare Verbesserung der relevanten Lastfälle und vollständige Kern-DPI-Abnahme |
| D: Breite Freigabe | Abschluss AP19/AP20 | Installierbare, langzeitgeprüfte Plattformpakete |

AP01–AP03 und AP05 allein umfassen 10–20 PT; AP00 und gemeinsame Integration kommen hinzu. Dieser erste Abschnitt liefert bereits nützliche Verbesserungen, bevor der Gesamtplan abgeschlossen ist.

Nach AP00/AP06 werden Aufwand und Prioritäten erneut bewertet. Ein vermuteter Hotspot, der im Profil unbedeutend ist, wird zurückgestellt. Ein neu reproduzierter Datenverlust oder Crash rückt unmittelbar nach vorn.

## 8. Verbindliche Regressionen und Testmatrix

### 8.1 Testfälle für die konkreten Befunde

| ID | Szenario | Erwartetes Ergebnis | Paket |
| --- | --- | --- | --- |
| R01 | Speichern: letzter Rename/Austausch scheitert | false/strukturierter Fehler; Dirty bleibt; gute Altdatei verfügbar | AP01 |
| R02 | Backup existiert, ist gesperrt oder kann nicht ersetzt werden | Kein ungeprüftes Entfernen der letzten guten Fassung | AP01 |
| R03 | Bundle: zweite Ressourcenkopie schlägt fehl | Kein als fertig veröffentlichter Bundle; verständlicher Fehler | AP01 |
| R04 | Bestehendes Projekt, danach nicht lesbare/kaputte Projektdatei öffnen | Alter Projektinhalt, Journal und Bedienbarkeit erhalten; Ladeflag zurückgesetzt | AP02 |
| R05 | Ladeabbruch und anschließendes weiteres Laden | Erfolg-/Abbruchsignale, Dirty und Readiness konsistent | AP02 |
| R06 | 100 Stereo-Frames, Peakstufe mit 20 Peaks, Zielbreite 64 | Originaldatenpfad ohne Dereferenzieren von rend() | AP03 |
| R07 | 0/1/wenige Frames, Trim und Reverse, linke/rechte Impulse | Gültige Grenzen; richtige Peak-/Kanalinterpretation | AP03 |
| R08 | Worker wird vor/nach Wakeup verzögert und während Shutdown gestoppt | Vollständiger Abschluss oder definierter Abbruch; keine Zerstörung aktiver Worker | AP04 |
| R09 | Queuegrenze und dynamisch nachgelieferte Jobs | Keine still verlorenen queued-Zustände; messbare Überlastreaktion | AP04 |
| R10 | ThreadPool-Task wirft; danach normaler Task | Ausnahme im Future; Prozess und Pool bleiben nutzbar | AP05 |
| R11 | Künstliche Modelllock-Verzögerung im Audiopfad | Zeit wird erfasst; Deadlinezähler steigt passend | AP06 |
| R12 | Suchfilter schnell wechseln und alte Queued-Signale verzögern | Nur aktuelle Generation erscheint | AP08 |
| R13 | Versteckte Dateien mit Checkbox aus/an | Suchergebnis entspricht der Auswahl | AP08 |
| R14 | Sample A lädt langsam, Nutzer wählt B und schließt Browser | A startet später nicht; kein Zugriff auf gelöschtes Widget | AP08 |
| R15 | GUI absichtlich verzögern, Audio läuft weiter | Gültiger letzter Snapshot, kein Zeiger-Lebensdauerfehler | AP09 |
| R16 | Lange Noten/Detuning von außerhalb ragen in Viewport | Optimierter Index findet sie; Hit-Test und Bild korrekt | AP10 |
| R17 | Idle-Control nach DPR-/Theme-/Fontwechsel | Cache erneuert sich ohne Änderung des Musikparameters | AP11/AP12 |
| R18 | Lebender Pluginprozess antwortet nicht mehr | Begrenzte Fehlerreaktion; andere Spuren und Shutdown funktionieren | AP14 |
| R19 | MCP-Preview, lokale Gegenänderung, Apply | Konflikt; keine Teilmutation und kein falscher Undo-Eintrag | AP15 |
| R20 | Gleicher Operationsschlüssel bei verlorener Antwort | Genau eine Mutation; reproduzierbares Ergebnis | AP15/AP17 |
| R21 | Mehrminütiges WAV rendern und in Chunks abrufen | Vollständige Daten, richtige Metadaten und Integrität | AP16 |
| R22 | Mehr als 128 abgeschlossene Jobs / 1024 junge Operationsschlüssel | Dokumentierte Aufbewahrung/Limitreaktion und Rückkehrmöglichkeit | AP17 |
| R23 | Extreme komprimierte Datei oder komplexes XML | Begrenzter Verbrauch; alter Projektzustand bleibt nutzbar | AP13 |
| R24 | Beenden mit Client, Vorschau, Pluginwartefall und Renderjob | Jeder externe Prozess/Thread erreicht kontrollierten Abschluss | AP14/AP15/AP20 |

Nicht jeder Fall braucht einen großen GUI-End-to-End-Test. Rechen-/Bereichsfehler gehören in kleine Tests mit Produktionsquellen; Besitzer-/Journalfehler in native Integration; Treiber und Fensterwechsel in reale Anwendungsläufe.

### 8.2 Plattform- und Konfigurationsachsen

Die Matrix wird gezielt kombiniert; das vollständige kartesische Produkt wäre unnötig teuer.

| Achse | Pflichtabdeckung |
| --- | --- |
| Betriebssystem | Windows zuerst als lokale Referenz; Linux und macOS vor breiter Freigabe |
| Qt | Unterstützter Qt-5.15-Pfad und vorhandener Qt-6-Pfad, jeweils mit dokumentierter kompatibler Toolchain |
| MCP | Vollständiger Build ON und OFF; Laufzeit ON und --no-mcp; Reader, Writer und Reconnect |
| Anzeige | 100/125/150/156,25/175/200 %; zwei Monitore mit verschiedenen Faktoren; abgelöste Fenster; Theme-/Fontwechsel |
| Desktopprotokoll | Windows nativ, macOS Retina; Linux X11/Wayland entsprechend gebauter Unterstützung |
| Audio | Pro Plattform mindestens das tatsächlich genutzte Backend; zusätzlich vorhandene alternative Backends nach Verfügbarkeit |
| Audioformat | 44,1/48/96 kHz; mehrere unterstützte Blockgrößen, etwa 128/256/512 Frames |
| Plugins | Interne deterministische Quellen; ein kontrolliertes Fehlerplugin; reale VST/LV2/Zyn/Carla-Stichproben |
| Projektzustand | Neu, geladen, geändert, Autosave, Recovery, fehlgeschlagenes Laden, Undo/Redo, Export und Shutdown |
| Dateisystem | Lokaler Pfad, Unicode, lange Namen, fehlende Rechte, gesperrte Dateien; langsamer/fehlender Datenträger |
| Buildmodus | Release/RelWithDebInfo für Leistung; Debug plus geeigneter Sanitizer für Fehlerdiagnose |

Geräteverlust-/Hotplugtests beginnen mit einer Bestandsprüfung je Audio-Backend. Ist Wiederverbindung technisch noch nicht implementiert, muss zumindest eine sichere Fehlermeldung und erneute Geräteauswahl möglich sein; ein erfolgreicher automatischer Wiederanlauf wird nicht vorausgesetzt.

### 8.3 Hör- und Datenintegrität

- Interne Referenzklänge vor/nach Änderungen vergleichen: Dauer, Kanalzuordnung, Timing, Peak/RMS, endliche Samples und Stillebereiche.
- Bei deterministischen Renderpfaden Bitgleichheit beziehungsweise eng begründete numerische Toleranz prüfen.
- NaN/Inf nicht nur im Master verdecken: Quelle und Kanal diagnostizieren; vorhandene Mixsanitisierung erhalten.
- Fremdplugins, Zufallsquellen, Effektfahnen, Transportwechsel und Loops zusätzlich hören.
- Nach Undo/Redo speichern, neu laden und erneut rendern. Ein richtig aussehender Editor allein beweist keine richtige musikalische Wiederherstellung.
- WAV-Validierung und SHA-256 beweisen Dateistruktur beziehungsweise Gleichheit, nicht musikalische Qualität.

## 9. Technische Leitplanken

### 9.1 Klare Thread- und Besitzverträge

| Bereich | Vertrag |
| --- | --- |
| GUI | Widgets, QPixmap-Verwendung und UI-Zustandswechsel bleiben im Hauptthread |
| Hintergrund-I/O | Dateien/Decoding auf Worker; Ergebnisse haben eigene Lebensdauer und Request-ID |
| Audiopfad | Begrenzte Arbeit, vorallokierte Übergaben; keine Diagnose-Datei-I/O oder unerwartete modale Dialoge |
| Audioanzeigen | Stabiler eigener Snapshot; alte Anzeigeframes dürfen entfallen |
| Modelländerungen | Vorbereiten außerhalb kritischer Sperren; kurze, definierte Übernahme; gleiche Revisions-/Journalregeln für GUI und MCP |
| Plugin-/Renderprozess | Expliziter Zustandsautomat, Deadline, eindeutig terminales Ergebnis und überprüfter Cleanup |
| QObject-Lebensdauer | Affinität, Signalverbindungen und Zerstörung gehören zum Besitzerthreadvertrag |

Threadingregeln dürfen nicht nur in Kommentaren stehen. Kleine Assertions für erlaubten Aufrufthread und Debugdiagnose ergänzen sie; die Releaseimplementierung braucht weiterhin tatsächlich korrekte Synchronisation.

### 9.2 Änderungen begrenzen und rückführbar halten

- **Verbindliche Sprachregel für Codekommentare:** Keine deutschen Codekommentare erstellen. Neue oder inhaltlich überarbeitete Kommentare ausschließlich auf Englisch verfassen; dies gilt auch für Dokumentationskommentare, TODOs und FIXMEs in Quellcode, Tests, Skripten und Builddateien. Kommentare nur ergänzen, wenn sie einen hilfreichen Zweck erfüllen. Dieser deutschsprachige Plan ist von der Regel ausgenommen.
- Jeder Fix enthält Problem, reproduzierenden Fall, konkrete Änderung und angemessenen Nachweis.
- Vorhandene Projektformate und musikalische Bedeutung bleiben kompatibel. Nötige Formatänderungen benötigen versionierte Migration und Fixtures.
- Neuer Scheduler, neuer Cache und neue Grafikressourcen werden getrennt abgenommen.
- Kein globales Entfernen von Locks oder unkontrolliertes Verschieben vorhandener QObjects in Worker.
- Cachegrenzen und mehr Hintergrundthreads werden aus Messungen abgeleitet; höherer Parallelismus kann Audioreserve verringern.
- Funktionale Flags nur für größere riskante Verhaltensänderungen verwenden. Kritische Speicher-/Iteratorfehler brauchen keine dauerhaft abschaltbare Parallelimplementierung.
- Alte Implementierungen nach erfolgreicher Abnahme entfernen; keine unbegrenzt wachsende Sammlung von Sonderpfaden.

### 9.3 Review pro Arbeitspaket

Ein Paket gilt erst als abgeschlossen, wenn:

1. Der konkrete Auslöser im Test oder dokumentierten manuellen Ablauf belegt ist.
2. Die Lösung die benannten Besitzer-/Datei-/Revisionsverträge erfüllt.
3. Passende Tests und betroffene Builds erfolgreich sind.
4. Relevante Performancewerte gegenüber der Baseline vorliegen.
5. Einschränkungen, Skips und bekannte Risiken ausdrücklich dokumentiert sind.
6. Die Änderung im installierten Produkt geprüft wurde, sofern sie Packaging, native Fenster, Audio-Backend oder Hilfsprozesse betrifft.

## 10. Erste konkrete Umsetzungsschritte

Die folgenden Schritte bilden einen unmittelbar ausführbaren Einstieg:

- [x] Referenztoolchain in `CMakePresets.json` und Buildmanifest-Vorlage festgehalten. Vollständiger Link scheitert hier noch an fehlendem libsndfile/vcpkg.
- [x] Baseline der tatsächlich ausgeführten Checks in `doc/stabilization/baseline.md`. F0/F1-Hörlauf nicht ausgeführt.
- [x] AP01: Veröffentlichung über `QSaveFile::commit` am Zielpfad, Backup-Fehler stoppen vor dem Ersetzen.
- [x] AP02: Ladebereich stellt Flags zurück; Recovery ist instanzbezogen; Autosave wertet den Rückgabewert aus. GUI-Durchlauf offen.
- [x] AP03: Originalpfad ohne `rend()`-Dereferenz, Halbintervalle, Cachebudget.
- [x] AP05: Ausnahme im Future, Mindestworker, Queue-Limit. Vertragstest bestanden.
- [x] AP08: Hidden-Checkbox, Suchgeneration, Debounce, Samplevorschau außerhalb des GUI-Threads.
- [x] AP06: Sperrwartezeit, Deadlinezähler, asynchroner Export. Vertragstest bestanden.
- [ ] Lastprofil F1/F3/F4 auf einer laufenden Engine. AP04/AP07/AP10 sind im Code, nicht profiliert.
- [ ] Kombinierter Lauf aus Audio, Browser, Plugin und MCP-Shutdown.

### 10.1 Reproduktion der hier ausgeführten Teiltests

Die folgenden PowerShell-Befehle beschreiben die tatsächlich verwendete lokale Umgebung. Die C:/tmp-Pfade sind **kein vorgeschlagener portabler Projektstandard**:

~~~powershell
npm --prefix tools/mcp test

cmake --build build/dpi --config Release --parallel 2

$env:PATH = 'C:\tmp\lmms-qt5\5.15.2\msvc2019_64\bin;' + $env:PATH
$env:QT_QPA_PLATFORM_PLUGIN_PATH = 'C:\tmp\lmms-qt5\5.15.2\msvc2019_64\plugins\platforms'
ctest --test-dir build/dpi -C Release --output-on-failure --parallel 2

cmake --build build/merge-check/out --config Release --target AiFilePolicyTest AiDomainContractTest AiChangePlanValidationTest AiWavFileTest AiSnapshotResourcesTest --parallel 2
ctest --test-dir build/merge-check/out -C Release --output-on-failure
~~~

Der blockierte Gesamtkonfigurationsversuch:

~~~powershell
cmake -S . -B build/improvement-audit -DQt5_DIR=C:/tmp/lmms-qt5/5.15.2/msvc2019_64/lib/cmake/Qt5 -DBUILD_TESTING=ON -DWANT_MCP=ON
~~~

Für den späteren Referenzbuild wird ein ausdrücklich gewählter Generator samt kompatibler Toolchain und vollständiger Dependencyinstallation verwendet. Den obigen fehlgeschlagenen Versuch nach Installation einzelner Bibliotheken einfach als Produktionsnachweis weiterzuverwenden, wäre unzureichend.

## 11. Offene Untersuchungsfragen

Diese Fragen sind gezielte nächste Mess-/Reproduktionsaufgaben und wurden nicht als feststehende Fehler ausgegeben:

1. Wie groß sind Sperrwartezeiten gegenüber Instrument-/Effektkosten in realen Projekten?
2. Wie oft erreicht die Jobqueue ihre Kapazität, und welche dynamischen Abhängigkeiten entstehen in diesen Fällen?
3. Welcher Verbindungstyp und welche effektive Puffergültigkeit gelten im laufenden Produkt für alle nextAudioBuffer-Empfänger?
4. Welche sichtbaren DPI-/Themeprobleme bleiben im installierten Hauptprogramm auf gemischten Monitoren tatsächlich übrig?
5. Wie hoch sind Zeit und Spitzenspeicher beim Serialisieren großer Projekte, Journalgruppen und MCP-Snapshots?
6. Welche externen Ressourcen werden je Instrument/Plugin wirklich eingefroren, und welche bleiben vom ursprünglichen Pfad abhängig?
7. Welche Backends reagieren bei Geräteverlust, Samplerateänderung und Suspend/Resume bereits sicher?
8. Welche Fehlerpfade im MainWindow-/Engine-Shutdown benötigen weitere Reihenfolgekorrekturen?
9. Welche weiteren eigenen Controls brauchen explizite Accessibilityschnittstellen?
10. Welche Verbesserung bringt ein Notenindex gegenüber gezielterem Repaint allein?

Nach Abschluss von AP00/AP06 werden diese Fragen mit Messprotokollen beantwortet und die zugehörigen Pakete neu geschätzt. Der Plan bleibt ein überprüfbares Arbeitsdokument.

AP06 ist im Profiler angelegt. Die Fragen 1–10 sind weiterhin offen, weil kein Laufzeitprofil und kein installiertes Paket vorliegen. Antworten dürfen nicht aus dem Übersetzungseinheiten-Build abgeleitet werden.

## 13. Umsetzungsstand 17. Oktober 2026

| Paket | Stand im Arbeitsbaum | Nachweis | Offen |
| --- | --- | --- | --- |
| AP00 | Preset, Manifestvorlage, Abhängigkeiten benannt | `CMakePresets.json`, `cmake/build-manifest.json.in` | Vollständiger Referenzlink |
| AP01 | Atomare Veröffentlichung. Backup wird erst über `.bak.incoming` ersetzt. Ein fehlender Kopierschritt lässt Projekt und alte `.bak` liegen. | `DataFile.cpp` übersetzt. Test steht in `StabilizationTest`, nicht gelinkt. | Fehlerinjektion im Produkt |
| AP02 | RAII-Ladezustand, instanzbezogene Recovery, Autosave-Status | `Song.cpp`, `ConfigManager.cpp`, `MainWindow.cpp` übersetzt bzw. geprüft | Interaktiver Ladeabbruch. Kein Rollback nach `clearProject()`. |
| AP03 | Gültige Wellenformquellen, Bytebudget. Revision mischt einen Stride über den Puffer, nicht nur die Endframes. | `SampleThumbnail.cpp` übersetzt. Test steht in `StabilizationTest`, nicht gelinkt. | ASan und Bildvergleich |
| AP04 | Batch bleibt lebendig, bis `process()` zurück ist. Reset schlägt dann fehl. Wartebudget 2–50 ms. `masterMix` setzt nach einem Fehlschlag nicht erneut auf. Destruktor bricht ab, statt Jobs freizugeben. | Übersetzt. Test `workerBatchStaysLiveUntilProcessReturns` kompiliert, nicht gelinkt. | Stress- und TSan-Lauf |
| AP05 | Ausnahmen, Mindestworker, Queuegrenze | `CoreContractTest` 5/5 | — |
| AP06 | Lock-Wartezeit, Deadline, Ringexport | Vertragstest und `AudioEngineProfiler.cpp` | Echtzeit-Audiolauf |
| AP07 | Gebündeltes Entfernen, verzögerte Nicht-Noten-Freigabe | `AudioEngine.cpp` übersetzt | Profil gegen Baseline |
| AP08 | Generation, Debounce, asynchrone Samplevorschau, Hidden-Checkbox. Destruktor wartet nicht auf die Verzeichnissuche. | `FileSearchJob.cpp` übersetzt | Großer Verzeichnisbaum und Schließen während eines hängenden Lesezugriffs |
| AP09 | Drei Slots, Veröffentlichung per Index. Fehlgeschlagene Kopie lässt den GUI-Puffer unverändert. | Übersetzt. Überlappungstest kompiliert, nicht gelinkt. | Blockierte GUI bei laufendem Audio |
| AP10 | `NoteTimeIndex` für Überlappung inklusive früher startender Noten. Neuaufbau nur bei geänderter Geometriesignatur. | `PianoRoll.cpp` übersetzt. Query-Test kompiliert, nicht gelinkt. | 100.000-Noten-Messung |
| AP11 | Oscilloscope an `Dpi::watch` | Quellprüfung | Monitorwechsel |
| AP12 | Toolbar-Mindesthöhe, Suchfeld-Metrik, Accessible-Namen | Quellprüfung | Tastatur- und Screenreader-Lauf |
| AP13 | Größen- und DOM-Budget, deklarierte Entpackgröße | `DataFile.cpp` übersetzt | Fuzzer |
| AP14 | Nachrichten-Deadline, Audio-Obergrenze 50 ms, Shutdown 3 s | `RemotePlugin.cpp`, `SystemSemaphore.cpp` | Lebender Hänger |
| AP15 | `stopService` maximal 3 s, Shutdown lehnt neue Kommandos ab | `AiControlService.cpp` | Lauf mit Client und Renderjob |
| AP16 | 512-MiB-Artefakte, Hash einmal, Chunkprüfung über Größe/mtime. Dateilesen läuft im ThreadPool, nicht im GUI-Thread. | `AiJobManager.cpp`, `AiCommandDispatcher.cpp` übersetzt. Chunk-Test kompiliert, nicht gelinkt. | Mehrminütiger Export |
| AP17 | `releaseFinishedJobs`, junge Operationsschlüssel bleiben | `AiJobManager` / Dispatcher | Sitzung über dem Limit |
| AP18 | `DiagnosticReport` ohne Projektinhalt, mit Worker-Zählern und optionaler GUI-Verzögerung | `DiagnosticReport.cpp` übersetzt | Audiopfad-Messung der Funktion |
| AP19 | `StabilizationTest` registriert, Timeout 180 s. Die ältere 5/5-Angabe ist nicht dieser Teststand. | Übersetzungseinheit kompiliert. Nicht gelinkt, nicht ausgeführt. | Gesamtes `ctest` |
| AP20 | Checklisten | `doc/stabilization/manual-acceptance.md`, `release-gates.md` | Paket, 8 Stunden, Hören |

Bereits vorhandene DPI- und MCP-Verträge wurden nicht zurückgebaut. Der isolierte DPI-Lauf aus der Analyse (25/25) wurde in diesem Durchgang nicht wiederholt.

## 12. Quellen und Nachweisablage

### 12.1 Repository

- [CMakeLists.txt](CMakeLists.txt): Version, Featurepfade, Sanitizeroptionen und Abhängigkeiten.
- [tests/CMakeLists.txt](tests/CMakeLists.txt): Core-, GUI- und AI-Testregistrierung.
- [tests/dpi/CMakeLists.txt](tests/dpi/CMakeLists.txt): Umfang der isolierten DPI-Prüfungen.
- [.github/workflows/build.yml](.github/workflows/build.yml), [high-dpi.yml](.github/workflows/high-dpi.yml), [checks.yml](.github/workflows/checks.yml): vorhandene CI.
- [doc/mcp/implementation-status.md](doc/mcp/implementation-status.md): bisherige Implementierungs-/Abnahmegrenzen; nicht mit neuen Messungen gleichzusetzen.
- [HIGH_DPI_SCALING_PLAN.md](HIGH_DPI_SCALING_PLAN.md), [MCP_INTEGRATIONSPLAN.md](MCP_INTEGRATIONSPLAN.md): ergänzende Detailpläne.
- Die jeweiligen Primärbelege stehen direkt bei B01–B19.

### 12.2 Ergänzend geprüfte technische Primärquellen

- [Qt: QSaveFile](https://doc.qt.io/qt-6/qsavefile.html): atomare Veröffentlichung einer Einzeldatei und commit()-Vertrag, Grundlage für AP01.
- [Qt: Threads und QObjects](https://doc.qt.io/qt-6/threads-qobject.html): Threadaffinität, GUI-Thread, Queued-/DirectConnection und Objektlebensdauer, Grundlage für AP08/AP09/AP15.
- [Qt: High DPI](https://doc.qt.io/qt-6/highdpi.html): logische Geometrie und gerätebezogene Rasterauflösung, Grundlage für AP11.
- [C++-Arbeitsentwurf: Iteratoranforderungen](https://eel.is/c++draft/iterator.requirements.general): Unterschied zwischen gültig dereferenzierbaren und Past-the-end-Iteratoren, Einordnung von B03.

Die verlinkten Qt-Seiten beschreiben allgemeine Verträge. Verfügbarkeit und Verhalten konkreter APIs werden bei der Implementierung zusätzlich gegen die tatsächlich unterstützten Qt-5-/Qt-6-Versionen geprüft. Dieser Plan empfiehlt kein ungeprüftes Upgrade auf eine neuere Bibliotheksversion.

### 12.3 Nachweisablage

Die Dateien unter `doc/stabilization/` existieren. Ältere Fassungen dieses Plans haben sie als noch nicht angelegt beschrieben. Neue Messungen werden dort ergänzt und ersetzen keine offenen Hardware- oder Langlauftore:

| Datei / Bereich | Inhalt |
| --- | --- |
| baseline.md | Hardware, Toolchain, Buildkonfiguration und Projektfixtures |
| findings.md | Reproduktion, Status und verknüpfter Fix je B-/R-Kennung |
| benchmarks/ | Maschinenlesbare Vorher-/Nachhermessungen |
| manual-acceptance.md | Reale DPI-, Audio-, Plugin- und Paketabnahme |
| release-gates.md | Ausgeführte Prüfungen, offene Abweichungen und Freigabeentscheidung |

Die Dateien sind die Ablage. Ein Eintrag dort ist nur dann eine Abnahme, wenn er Befehl, Ergebnis und die nicht ausgeführten Läufe nennt.
