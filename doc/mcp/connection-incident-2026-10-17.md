# Verbindungsverlust beim Anlegen einer Instrumentenspur

## Bericht und Einordnung

Am 17. Oktober 2026 wurde ein Verbindungsverlust beim ersten `create_instrument_track` mit `organic` berichtet. Vorher waren Status, Projekt, Spuren und Instrumentenkatalog lesbar; das Projekt hatte Revision 5 und 140 BPM. Danach meldete der Companion zunächst eine geschlossene Verbindung und anschließend `connect ENOENT` für den alten Instanz-Endpunkt. Bei einer späteren Prozessprüfung fehlte laut Bericht LMMS. Nach erneutem Start bestanden alte Companion-Verbindungen fort. Die Begleitung zu „Light Through the Rain“ wurde nicht erstellt; das Ergebnis des Schreibversuchs ist unbekannt.

Zum Zeitpunkt der ersten Prüfung waren der native Absturz und eine besondere Ursache in `organic` Hypothesen. Die Quellprüfung bestätigt dagegen die Bindung an die ausdrücklich konfigurierte Instanzdatei: `BridgeClient.open()` lädt diese bei jeder Wiederverbindung neu, sucht aber keine andere Datei. Der native Dienst vergibt beim Start eine neue UUID und einen neuen Endpunkt. Das Ausbleiben einer automatischen Sitzungswahl entspricht dem dokumentierten Vertrag.

## Lokale Prüfung

- LMMS war bei dieser Prüfung mit PID 22892 aktiv; zusätzlich waren vier `lmms-mcp.exe`-Prozesse vorhanden. Prozessanzahl allein belegt keine defekte Verbindung.
- Eine Abfrage des Windows-Anwendungsprotokolls nach Ereignissen 1000/1001 mit „lmms“ im Meldungstext innerhalb der letzten zwei Tage lieferte keinen Treffer. Das schließt einen Absturz nicht aus; ein genauer Fehlerzeitpunkt lag nicht vor.
- Der native Aufruf führt über Dispatcher und `AiApplicationFacade::createInstrumentTrack()` zu `Track::create()` und `InstrumentTrack::loadInstrument()`. Aus dieser statischen Prüfung wurde keine konkrete Absturzursache abgeleitet.

## Erste Änderung und Verifikation

Der Companion erklärt bei einem nicht erreichbaren Endpunkt jetzt die Auswahl der neuen Verbindungsdatei und den erneuten Verbindungsaufbau im Host. Bei Verbindungsabbruch weist er auf ein möglicherweise bereits ausgeführtes Schreiben hin. Es gibt weder einen automatischen Instanzwechsel noch eine automatische Wiederholung der Mutation.

`npm --prefix tools/mcp test`: **11/11 bestanden**, einschließlich TypeScript-Build, Schemakonsistenz, IPC- und stdio-Protokolltests. Zwei neue IPC-Tests prüfen den fehlenden Endpunkt samt Wiederherstellungshinweis ohne Auth-Token sowie den Abbruch einer Mutation ohne Wiederholung beim nächsten Leseaufruf. Dies sind Companion-Tests mit Testpeer, keine native Absturzreproduktion. Die installierte Anwendung wurde nicht ersetzt.

## Nachuntersuchung und behobene Fehler

Die anschließende Implementierung wurde mit MSVC 19.51.36260, Qt 6.8.3 und Node 24.19.0 geprüft. Der bereits vorhandene vollständige Build unter `build/user-install` konnte verwendet werden. Die frühere Toolchainblockade war für diese Umgebung nicht mehr aktuell. Die installierte Anwendung wurde nicht ersetzt.

| Befund | Korrektur und Nachweis |
| --- | --- |
| `instrumentName()` liefert den Anzeigenamen, beispielsweise `Organic`. MCP verglich diesen mit dem Schlüssel `organic` und behandelte einen erfolgreichen Ladevorgang als Fehler. Auch `instrumentKey` in der Spurliste enthielt den Anzeigenamen. | Vergleich und Ausgabe verwenden den Schlüssel des tatsächlich geladenen Plugin-Deskriptors. Separate Anwendung: Organic und TripleOscillator erstellen, Spurliste auf Schlüssel prüfen, Undo/Redo ausführen. |
| Beim Zurücksetzen beziehungsweise Wiederherstellen von Spuren konnten GUI-Objekte ihre Modelle überleben. Die periodischen Aktualisierungen von Instrument- und Samplespuren sowie eine verzögerte Clipansicht griffen dann auf ungültige Objekte zu. | Modellprüfungen, besitzende MIDI-Menüs und bei Signalauslösung erfasste `QPointer` für verzögerte Spur-/Clipansichten. |
| Die Patternansicht verwendete nach Entfernen ihres Modells `s_infoMap[track]`. Der Lesezugriff fügte einen verwaisten Eintrag erneut ein. Nachfolgende Patternerstellung konnte in `PatternStore::updateComboBox()` einen Nullzeiger dereferenzieren. | Nicht einfügende Suche und Prüfung der Modelllebensdauer. Der isolierte Anwendungstest umfasst das Standardprojekt mit Pattern-, Instrument-, Sample- und Automationsspuren. |
| Presetladen suchte die Instrumentspur unter dem äußeren Dokumentelement statt unter dem von `DataFile` ermittelten Inhaltsknoten und parste zuvor zusätzlich ohne dessen Eingabebudgets. | Inhaltsknoten auswerten und nur den begrenzten DataFile-Parser verwenden. Ein neuer Konstruktor-Overload erlaubt strukturierte Ladefehler ohne modale Dialoge; der bisherige Konstruktor behält sein Verhalten. Ein temporäres Organic-Preset wird über den echten Katalog geladen; Undo/Redo und Ablehnung eines fehlerhaften Presets ohne Zustandsänderung werden geprüft. |
| Bei C++-Ausnahmen konnten Instrument-/Presetladen Spursperren halten; der direkte Instrumentwechsel löschte den bisherigen Besitzer zu früh. | Spursperren und Modelländerungssperren über RAII freigeben; Ersatzinstrument zunächst erstellen. Gemeinsamer Abschluss für Instrument- und Assetänderungen einschließlich Rollback, Dirty-/Revisions-/Journalling-Wiederherstellung und Handleinvalidierung. Ein fehlgeschlagener Rollback meldet einen eigenen Fehler und lässt den Zustand als geändert markiert. |
| Verzögerte Dienststartantworten konnten sich auf eine neue Sitzung beziehen. IPC las veränderliche Sitzungsstrings über Threadgrenzen; ein Rechtewiderruf griff verzögert auf `m_server` zu. | Dienstgenerationen und konkrete Serverreferenzen erfassen; Instanz und Token unveränderlich im Server speichern. Neustart, Veröffentlichungskollision, Erholung und Widerruf einer echten authentifizierten Verbindung prüfen. |
| Schließen während des Companion-Aufbaus und verspätete Handshakes waren nicht generationengebunden. Ressourcen prüften die Instanz vor der tatsächlichen Wiederverbindung. Antworten wurden vor vollständiger Validierung aus der Pendingliste entfernt. | Verbindungsgenerationen, Anfragegrenze vor Aufbau, Instanzprüfung pro authentifiziertem Ressourcenaufruf und vollständige Envelopeprüfung vor Auflösung der Anfrage. Fehlerhafte Antworten weisen ausstehende Anfragen unmittelbar zurück. |
| Deutsche MCP-Originaltexte und eine unzutreffende Bereitschaftsanzeige waren fest eingebaut. | Englische `tr()`-Originaltexte, gezielte deutsche Übersetzungen, Statussignal für Dialog/Anzeige und Kopierfunktion erst nach Veröffentlichung. Projektbereitschaft wird beim Dialogstart nicht überschrieben. |

### Absturzbeleg und historische Grenze

Im separaten Anwendungsprozess wurde eine Speicherzugriffsverletzung `0xc0000005` reproduziert und ein lokaler Minidump erfasst. Die Fehleradressen wurden gegen die zum ausführbaren Build erzeugte Linkermap abgeglichen. Nachgewiesene Fehlerstellen waren `PatternStore::updateComboBox()`, die periodischen Aktualisierungen von `InstrumentTrackView` und `SampleTrackView` sowie `TrackView::createClipView()`. Nach den Korrekturen besteht derselbe Instrument-/Historienablauf.

Dies ist ein reproduzierter Fehlerpfad des untersuchten Codes. Ohne Dump der ursprünglichen Nutzersitzung wird nicht behauptet, dass deren Absturzadresse damit rückwirkend bewiesen ist. Ein eigenständiger DSP-Defekt in Organic wurde nicht nachgewiesen. Dump und Verbindungsdateien sind keine eingecheckten Artefakte, da sie Sitzungsdaten und Secrets enthalten können.

### Ausgeführte Prüfungen

- `npm --prefix tools/mcp test`: **27/27 bestanden**, einschließlich TypeScript-Build, Schemakonsistenz, beiden stdio-Protokollversionen, Verbindungsrennen, ungültigen Antworten/Framegrößen, Fragmentierung und Ressourceninstanzwechsel.
- `ctest --test-dir build/user-install -C Release -R '^Ai' --output-on-failure`: **17/17 bestanden**. Zehn native AI-Testprogramme sowie der Oberflächentest mit Standardlauf und sechs Skalierungen. Alle verwenden vollständig gelinkte Produktionsquellen; keine Headerstubs.
- `AiCommandDispatcherTest` startet zusätzlich ein eigenes `lmms.exe` mit temporärem Profil, Dummy-Audio/MIDI, deaktiviertem Autosave und `QT_QPA_PLATFORM=offscreen`. Plugins müssen unter Windows im echten Programm laufen, da sie dessen Exporte importieren. Instrumenterstellung und Presetladen werden über den TypeScript-Companion und echten IPC-Dispatcher ausgeführt.
- Acht Fehlerinjektionsfälle im Transaktionstest kombinieren Song-/Spurcheckpoint, Rückgabefehler/Ausnahme sowie ein-/ausgeschaltetes Journalling. Sie prüfen Spurinhalt, Historie, Revision, Dirtyflag, Handleinvalidierung und geschlossene Bearbeitungsgruppe.
- Der Oberflächentest prüft englische Texte, Start-/Bereitschafts-/Fehlerzustände, kopierte Konfiguration sowie einen tatsächlich geladenen deutschen QM-Katalog. Die QM-Datei wird ausschließlich im Buildordner erzeugt.
- MCP-ON: Anwendung, Organic, TripleOscillator und sämtliche oben genannten Tests in Release gelinkt. MCP-OFF: separater vollständiger Release-Build einschließlich Anwendungslink unter `build/mcp-stability-off-qt6` ebenfalls erfolgreich. Die Abschlussprotokolle liegen lokal unter `build/mcp-companion-final-tests.log`, `build/mcp-native-final-tests.log`, `build/mcp-final-application-build.log` und `build/mcp-off-final-build.log`.
- Die Windows-Presets `windows-qt6-msvc-v145` und `windows-qt6-msvc-v145-no-mcp` verwenden Qt 6.8.3 und Visual Studio 2026/v145 passend zur geltenden Compileranforderung und zur ausgeführten Abnahme. Sie ersetzen die v142-/Qt-5-Presets; der Qt-Prefix wird über `LMMS_QT6_PREFIX` gesetzt. Ein zusätzlicher Qt-5-Gesamtbuild wird damit nicht behauptet.

### Verbleibende Grenzen

Kein automatischer Wechsel zu einer anderen Instanz und keine automatische Wiederholung einer Mutation. Nach einem Neustart muss der Host die neue Verbindungsdatei verwenden. Eine Produktabnahme mit realer Audiowiedergabe, anderen Betriebssystemen und installierten Paketen ist nicht Teil dieser gezielten Reparatur. Die laufende Gesangssitzung wurde nicht verändert; die Begleitung zu „Light Through the Rain“ wurde nicht erstellt.
