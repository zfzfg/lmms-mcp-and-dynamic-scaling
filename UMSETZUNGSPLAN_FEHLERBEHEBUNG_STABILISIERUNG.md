# LMMS: Umsetzungsplan für Fehlerbehebung und Stabilisierung – Phase 2

**Überarbeitet:** 17. Oktober 2026  
**Referenzstand:** `ac688bd42e982977ca0f75796c2a54d6b8c3455b` (`Stabilize save, audio jobs, and long-session limits`)  
**Zielgruppe:** Entwickler und nachfolgende Code-Agenten  
**Status:** Geprüfter Arbeitsplan; die nachstehenden Maßnahmen sind noch nicht umgesetzt oder abgenommen.  
**Übergeordneter Plan:** [VERBESSERUNGSPLAN_GESAMT.md](VERBESSERUNGSPLAN_GESAMT.md)

## 1. Ziel, Prüfgrundlage und Grenzen

Phase 2 soll vorhandene Nutzerdaten schützen, die Tests reproduzierbar ausführbar machen und die Fehlerpfade von Audio, Worker und Recovery vervollständigen. Die Umsetzung muss Fehler gezielt auslösen und den Zustand nach dem Fehler nachweisen können. Ein erfolgreicher Übersetzungsschritt allein reicht dafür nicht.

Diese Überarbeitung beruht auf einer Quellprüfung des Referenzcommits, der Testregistrierung, der Buildpresets und der vorhandenen Nachweise unter [doc/stabilization](doc/stabilization). Die im ursprünglichen Plan erwähnten Screenshots liegen dieser Prüfung nicht separat vor. DLL-Meldungen werden als berichtetes Symptom behandelt; eine neue Reproduktion fand hier nicht statt. Für diese Planprüfung wurden keine Anwendungs-, Audio- oder CTest-Läufe ausgeführt.

Die Angaben „MCP 9/9“, „DPI 25/25“ und „Vertragstests 5/5“ stammen aus früheren Arbeiten und sind keine aktuellen Testergebnisse. Der frühere CoreContractTest ist insbesondere nicht der aktuelle StabilizationTest. Laut [baseline.md](doc/stabilization/baseline.md) fehlt der vollständige Anwendungslink; ein sndfile-Headerstub diente nur Übersetzungsprüfungen.

### 1.1 Korrekturen gegenüber der Vorfassung

- DLL-Regeln müssen konfigurationsabhängig, Qt-5-/Qt-6-fähig und mit der unterstützten CMake-Version kompatibel sein. Hart codierte Release-DLLs reichen nicht.
- `SetErrorMode` innerhalb eines Testprogramms verhindert dessen Import-DLL-Fehler nicht: Dann wird `main()` noch gar nicht erreicht. Dafür ist gegebenenfalls ein vorgeschalteter Launcher nötig.
- Zwei Verzeichnis-Renames sind kein einzelner atomarer Bundleaustausch. Rollbackfehler, Absturzfenster und parallele Instanzen müssen behandelt werden.
- Beim Backupproblem geht es um die ältere Backupgeneration. Die letzte gute Projektdatei wird durch QSaveFile mit deaktiviertem Direct-Write-Fallback geschützt; „alle Backups verloren“ wäre zu pauschal.
- Die behauptete Halbierung der Wellenform trifft auf den aktuellen Aufruf nicht zu: Er verwendet bereits `m_buffer->size() * DEFAULT_CHANNELS`.
- Recovery ist bereits in `main.cpp` angebunden. Es fehlen vor allem die Auswahl mehrerer Sitzungen und sichere Aufbewahrungs-/Löschregeln.
- `QThread::terminate()` ist keine sichere Reparatur für einen hängenden Audioworker. Timeout bedeutet nicht, dass Job oder Puffer freigegeben werden dürfen.
- Ein sofortiger RAII-Reset aller Mixerkanäle nach Timeout wäre gefährlich, solange Worker diese verwenden.
- Eine Mutex-/vector-Aufräumqueue im Audiothread führt neue Blockier-/Allokationsrisiken ein. Besitz und Affinität müssen zuerst geklärt werden.
- Headerprüfung und Größenprüfung nach `qUncompress()` schaffen noch keine harte Grenze während des Entpackens.
- D15 und D17 erhalten eigene Arbeitspakete. Weitere Lücken sind als D18–D22 erfasst.

### 1.2 Statusbegriffe

| Status | Bedeutung |
| --- | --- |
| Quellbefund | Zweig oder Vertragslücke im Code belegt; Laufzeitwirkung gegebenenfalls noch offen. |
| Hypothese | Plausibler Fehler, der zuerst durch einen erreichbaren Fall oder Gegenbeweis geklärt wird. |
| Implementiert | Änderung vorhanden; noch keine Abnahme. |
| Übersetzt / gelinkt | Übersetzungseinheit beziehungsweise ausführbares Ziel gebaut; Tests separat nennen. |
| Getestet | Konkreter Lauf mit Befehl, Ergebnis, Commit, Konfiguration und Datum abgelegt. |
| Abgenommen | Alle Paketkriterien erfüllt; verbleibende Grenzen sichtbar dokumentiert. |

## 2. Überprüfter Befundkatalog

**P0:** Schutz vor Verlust vorhandener Nutzerdaten. **P1:** Absturz, Hänger, Audiokorrektheit oder blockierte Verifikation. **P2:** Robustheit, Diagnose oder Bereinigung. Priorität und Belegstärke sind getrennt zu bewerten. D01–D17 bleiben zur Rückverfolgung erhalten, auch wenn die ursprüngliche Behauptung korrigiert wurde.

| ID | Priorität / Beleg | Befund im Referenzstand | Paket |
| --- | --- | --- | --- |
| D01 | P1 / Quellbefund | `publishFile` lehnt vorhandene Bundleordner mit `save.bundle.exists` ab. Funktionale Einschränkung, kein Thread-Deadlock. Normales Speichern eines geladenen Bundleprojekts ist gesondert zu prüfen. | AP-F02 |
| D02 | P0 / Quellbefund | `writeAtomic` entfernt `.bak.previous` vor Öffnen/Schreiben/Commit der neuen Projektdatei. Bei späterem Fehler fehlt die ältere Backupgeneration. | AP-F03 |
| D03 | P2 / Behauptung widerlegt | `flatBufferSize = frameCount * DEFAULT_CHANNELS` deckt die Daten ab. Frame-/Peak-Semantik und leere Puffer brauchen Tests; ein Halbierungsfix ist unbegründet. | AP-F04 |
| D04 | P1 / Quellbefund | `renderStageNoteSetup` zerstört zurückgestellte Nicht-Noten-Handles im Renderthread. Kosten und Affinitäten der Destruktoren sind noch zu messen. | AP-F05 |
| D05 | P1 / Hypothese zur Wirkung | `finishBlockedPeriod(true)` nullt den Lesepuffer nicht. Nach dem Swap ist dies aber die vorherige fertige Periode. Ein Loop-Glitch ist damit allein nicht bewiesen. | AP-F06 |
| D06 | P1 / eingeschränkter Quellbefund | Workerdestruktor entfernt nur die Registrierung. Engine-Destruktor fordert bereits Stop an, wartet und bricht bei Fehlschlag ab. Abweichende direkte Zerstörung bleibt ungesichert. | AP-F09 |
| D07 | P1 / Behauptung widerlegt | `main.cpp` fragt bereits `orphanedRecoveryFiles()` ab und öffnet einen Dialog. Kein zweiter Startupdialog in MainWindow nötig. | AP-F10 |
| D08 | P1 / Quellbefund | Reguläre Tests einschließlich StabilizationTest linken gegen `lmmsobjs`. Lokale Minimalprüfungen ersetzen keinen vollständigen Link. | AP-F11 |
| D09 | P1 / Symptom berichtet, Lücke belegt | Eigenständige Windows-DLL-Umgebung fehlt in Test-CMake. Vorhandene QPA-/Skalierungsvariablen müssen erhalten bleiben. | AP-F01 |
| D10 | P1 / Quellbefund | Frühe Rückkehr aus `masterMix` überspringt finalen Reset. Reset während aktiver Jobs wäre ebenfalls falsch. `prepareMasterMix` und sicheren nächsten Übergang mitprüfen. | AP-F07 |
| D11 | P2 / Quellbefund | Nach XML-Parsefehler werden beliebige erste vier Bytes als Entpackgröße interpretiert; irreführende Kompressionsdiagnosen möglich. | AP-F08 |
| D12 | P1 / Quellbefund | Recoveryeigentum beruht auf PID statt Startidentität. Fehlende Berechtigung kann als „tot“ gelten. Prozessname schützt nicht vor einer neuen LMMS-Instanz mit derselben PID. | AP-F10 |
| D13 | P0 / Quellbefund | Startup bietet nur `.first()` an; „Discard“ durchläuft alle gefundenen Recoverydateien. Nicht einzeln gezeigte Sitzungen können gelöscht werden. Legacydatei wird vorangestellt statt global nach Aktualität sortiert. | AP-F10 |
| D14 | P2 / Quellbefund | Render-TLS wird manuell zurückgesetzt; Scope-Wiederherstellung bei Ausnahmen fehlt. Ob danach weitergerendert werden darf, ist separat zu entscheiden. | AP-F12 |
| D15 | P1 / Quellbefund | Jeder Worker-Konstruktor ruft globales `reset()` auf. Engine startet Worker bereits während derselben Konstruktionsschleife. Queueinitialisierung und Laufzeitaufbau brauchen einen Besitzervertrag. | AP-F13 |
| D16 | P2 / Quellbefund | `nextAudioBuffer` bleibt deklariert; publiziert wird nun ein Display-Snapshot. Entfernen erst nach Verbraucher-/Kompatibilitätsprüfung. | AP-F12 |
| D17 | P2 / Hypothese | Index berücksichtigt nur das rechte Detuningende. `putValue/putValues` begrenzt Zeiten allerdings auf mindestens 0. Erreichbare negative Offsets zuerst über Lade-/Importpfade nachweisen. | AP-F14 |
| D18 | P1 / Schutzlücke | Größenchecks vor/nach `qUncompress` begrenzen Wachstum nicht während des Entpackens. Rekursive DOM-Nachzählung erfolgt erst nach Parse; Tiefe und Parseraufwand bleiben unzureichend begrenzt. | AP-F08 |
| D19 | P0 / Quellbefund | `removeAbandonedBundleStaging` löscht markierte Verzeichnisse ohne Prüfung auf lebenden Besitzer. Paralleler Speichervorgang kann betroffen sein. | AP-F02 |
| D20 | P1 / Quellbefund, Buildwirkung prüfen | StabilizationTest verwendet AiJobManager bedingungslos, ist aber auch bei MCP OFF registriert. Includes/Linkabhängigkeiten müssen getrennt werden. | AP-F11 |
| D21 | P0 / Quellbefund | Recoveryquelle wird nach erfolgreichem Laden bereits gelöscht. Crashschutz bis zum nächsten dauerhaften Save/Autosave ist nicht nachgewiesen. | AP-F10 |
| D22 | P1 / Quellbefund | `startAndWaitForJobs` führt Queuearbeit inline vor `waitFor(budget)` aus. Budget begrenzt nicht die gesamte Aufrufzeit; synchrone Mixerfallbacks ebenso berücksichtigen. | AP-F15 |

Primärbelege: [DataFile.cpp](src/core/DataFile.cpp), [SampleThumbnail.cpp](src/gui/SampleThumbnail.cpp), [AudioEngine.cpp](src/core/AudioEngine.cpp), [AudioEngineWorkerThread.cpp](src/core/AudioEngineWorkerThread.cpp), [Mixer.cpp](src/core/Mixer.cpp), [ConfigManager.cpp](src/core/ConfigManager.cpp), [main.cpp](src/core/main.cpp), [Song.cpp](src/core/Song.cpp), [NoteTimeIndex.h](include/NoteTimeIndex.h), [PianoRoll.cpp](src/gui/editors/PianoRoll.cpp), [AutomationClip.cpp](src/core/AutomationClip.cpp), [StabilizationTest.cpp](tests/src/core/StabilizationTest.cpp).

## 3. Reihenfolge und gemeinsame Verträge

AP-F-Nummern dienen der Nachverfolgung, nicht einer starren Reihenfolge. Infrastruktur und unmittelbar drohender Datenverlust haben Vorrang. Abhängigkeiten dürfen nicht durch vorgezogene Einzelpatches umgangen werden.

| Etappe | Pakete | Abschlussbedingung |
| --- | --- | --- |
| A: Verifikation | AP-F01, AP-F11 | Reproduzierbarer Teststart; kleine Produktions- und vollständige Integrationstests getrennt. |
| B: Dateien | AP-F03, AP-F02, AP-F10 | Save-, Bundle- und Recoveryfehler sichern vorhandene Fassungen. |
| C: Audiobesitz | AP-F13, AP-F09, AP-F15; danach AP-F07/AP-F06 | Kein Reset/Free aktiver Daten; definierter Fehleroutput; Inlinegrenze dokumentiert. |
| D: Robustheit | AP-F08, AP-F05, AP-F04, AP-F12, AP-F14 | Eingabebudgets, Freigabevertrag und bestätigte GUI-Probleme abgesichert. |
| E: Produkt | AP-F16 | Vollbuild, Paketstart, integrierte Regressionen und Langlauf belegt. |

Ist bereits Infrastruktur lauffähig, darf ein kleiner datenrettender Fix vor Abschluss A erfolgen. Seine Abnahme bleibt an die anschließende Verifikation gebunden.

### 3.1 Nicht verletzbare Verträge

1. **Dateien:** Vorhandene Nutzerdaten vor erfolgreichem Ersatz nicht ungeprüft entfernen. Rollbackfehler mit den tatsächlich verbliebenen Pfaden melden.
2. **Bundles:** Nur positiv identifizierte und freigegebene LMMS-Bundles ersetzen. Ein gleichnamiger fremder Ordner ist kein löschbares Staging.
3. **Worker:** Timeout beendet das Warten, nicht den Job. Queue/Puffer erst nach vollständiger Quieszenz ohne aktive Zugriffe recyceln.
4. **Ausgabe:** Sicherer Fehlerpuffer mit eindeutigem Besitzer; GUI bekommt vollständige Snapshots statt zeitlich ungültiger Rohzeiger.
5. **Recovery:** Unbekannter Prozessstatus erlaubt kein automatisches Löschen. Nach Wiederherstellung bis zum dauerhaften Save erneut Crashschutz sicherstellen.
6. **Tests:** Auch ein fehlgeschlagener Assert muss Release und Join garantieren, bevor Thread-/Jobobjekte zerstört werden.
7. **Kommentare:** Neue oder inhaltlich überarbeitete Codekommentare ausschließlich auf Englisch, auch in Tests, Skripten und Builddateien. Deutsche Dokumentation ist ausgenommen.

## 4. Detaillierte Arbeitspakete

### AP-F01 – Windows-Testlaufzeit und Loaderfehler beherrschen

**Priorität:** P1. **Befund:** D09. **Abhängigkeit:** keine.  
**Dateien:** [tests/CMakeLists.txt](tests/CMakeLists.txt), [tests/dpi/CMakeLists.txt](tests/dpi/CMakeLists.txt), neuer gemeinsamer CMake-Helfer und gegebenenfalls nativer Testlauncher unter Versionskontrolle.

**Ziel:** Tests starten ohne manuelles Qt-PATH; Release/Debug verwenden passende DLLs. Bestehende native und fraktionale DPI-Fälle behalten ihre Aussagekraft.

**Umsetzungsschritte:**

1. Compiler, Architektur, Konfiguration, Qt-Major und importierte Targets erfassen. Fehlende Qt-DLL, Compiler-Runtime und QPA-Plugin unterscheiden. `0xc0000135` allein benennt keine konkrete fehlende Abhängigkeit.
2. Registrierung mit gemeinsamem Helfer ergänzen. Qt-5-/Qt-6-Target explizit bestimmen; konfigurationsabhängige Generatorausdrücke wie `$<TARGET_FILE_DIR:Qt5::Core>` nutzen. Keine festen Release-Dateinamen und keine ungeprüfte `LOCATION`-Abfrage.
3. CMake-Kompatibilität beachten: Root verlangt derzeit 3.13, isolierter DPI-Build 3.16. `ENVIRONMENT_MODIFICATION` gibt es erst ab 3.22. Versionsabhängigen Fallback mit Wrapper vorsehen oder Mindestversion ausdrücklich repositoryweit begründet anheben.
4. Auf geeignetem CMake `PATH=path_list_prepend:...` verwenden. Im älteren Fallback PATH-Semikolons korrekt erhalten und die Umgebung zur Testlaufzeit übernehmen. Den alten Ausdruck `ENVIRONMENT "PATH=${QT_BIN_DIR};$ENV{PATH};..."` nicht ungeschützt übernehmen: Semikolons trennen auch CMake-Listeneinträge.
5. Bestehende Testumgebung ergänzen statt überschreiben. `DpiNativeTest` behält `QT_QPA_PLATFORM=windows`; offscreen und Skalierungsvariablen nur für entsprechende Tests. Pluginpfad aus gewähltem Qt ableiten und prüfen.
6. Direktstart im Debugger/Explorer gesondert unterstützen: passendes `windeployqt` oder nachgewiesene Target-Runtimeabhängigkeiten plus dynamische Plugins. QPA-/Imageformatplugins werden durch bloße Link-DLL-Kopien nicht vollständig erfasst. Gemeinsame Ausgabeverzeichnisse bei parallelen Deploymentregeln koordinieren.
7. Falls unbeaufsichtigte Läufe Loaderdialoge unterdrücken müssen: Launcher ohne Qt-Importabhängigkeit, Error-Mode vor Childstart setzen, Exitcode weitergeben, äußeren Timeout und Prozesscleanup gewährleisten. Child darf Modus nicht verlieren. `SetErrorMode` im Test selbst ist nur ergänzend nach erfolgreichem Laden wirksam.
8. Den nicht versionierten lokalen `build/merge-check/CMakeLists.txt` als Auditharness behandeln. Dauerhafte Regeln nach `tests/`/`cmake/` verschieben; Buildverzeichnisse nicht als Quellablage etablieren.

**Prüfung / Abnahme:** R-F01/R-F02. Saubere Shell, Release und Debug, nativer Windows-DPI-Test und offscreen-Test. Fehlende DLL nur in privater Testkopie simulieren: zeitnaher Fehler ohne Dialogblockade, verwertbarer Exitcode. Niemals DLLs aus der produktiven Qt-Installation entfernen. Linux/macOS erhalten keine Windowspfade.

### AP-F02 – Bundleexport transaktional und mehrinstanzfähig machen

**Priorität:** P0 für Cleanup/Datenintegrität, P1 für Ersetzen. **Befunde:** D01/D19. **Abhängigkeiten:** AP-F03 und Datei-Fehlerinjektion aus AP-F11.  
**Dateien:** DataFile.cpp/.h, Song.cpp, Save-Dialoge und Bundletests.

**Vorentscheidung:** `guiSaveProjectAs` behandelt `withResources` als Export: ursprünglicher Dateiname und Dirtyzustand bleiben erhalten. Dieses Verhalten nicht nebenbei ändern. Normales Speichern eines im Bundle geladenen Projekts und erneuter Bundleexport sind zwei getrennte Reproduktionen. Namensableitung über `section('.', 0, 0)` auf Kollisionen wie `mix.v1.mmp`/`mix.v2.mmp` prüfen; Bestandskompatibilität berücksichtigen.

**Umsetzungsschritte:**

1. Zielbesitz über Bundleidentität/Manifest und erwartete Projektstruktur positiv prüfen. Fremde Ordner, Symlinks/Junctions nicht rekursiv ersetzen. Existenz allein ist kein Besitznachweis.
2. Exklusiven Schreibbesitz pro kanonischem Ziel zwischen Prozessen vereinbaren. Bei Konflikt nachvollziehbar abbrechen/Retry; Besitz vor destruktiven Operationen absichern.
3. Eindeutiges Staging als Geschwisterverzeichnis auf demselben Dateisystem mit Transaktions-ID und Besitzerinformationen anlegen. DOM vor Ressourcenumschreibung klonen, damit Exportfehler das wiederverwendete DataFile nicht auf gelöschtes Staging umbiegen.
4. Ressourcen vollständig kopieren, Basenamekonflikte und fehlende Quellen prüfen. Projekt erneut parsebar machen und relative Referenzen gegen erlaubte Bundlepfade auflösen. „Nicht leere Projektdatei“ ist kein ausreichender Vollständigkeitsnachweis.
5. Beim Ersetzen alte Fassung unter eindeutigem transaktionsbezogenem Sicherungsnamen behalten. Keine pauschale Löschung einer vorhandenen `.bundle-backup`.
6. Zustände festhalten: `Prepared → OldMoved → NewPublished → CleanupComplete`. Zwei Renames lassen ein Fenster ohne kanonischen Zielordner. Nicht als atomaren Verzeichnistausch bezeichnen; Wiederaufnahme nach Crash über erhaltene Marker ermöglichen.
7. Scheitert neuer Rename, Rückverschiebung versuchen und Ergebnis prüfen. Bei Rollbackfehler beide sicheren Verzeichnisse erhalten, eigenen Fehler mit tatsächlichen Pfaden melden. Keine Aussage „restored“ ohne Nachweis.
8. Stagingcleanup nur bei eindeutig eigenem oder sicher verwaistem Besitzer. Lebende/fremde/unklare Transaktionen erhalten; Alter allein beweist keine Verwaisung. Unterbrochene Veröffentlichung mit Sicherungsordner benötigt Recovery statt pauschalem Entfernen.
9. Fehlgeschlagener Cleanup nach erfolgreicher Veröffentlichung ergibt Erfolg plus Warnung/Artefaktpfad. Gute Altversion erst nach erfolgreicher Prüfung beziehungsweise gemäß dokumentierter Retention entfernen.

**Prüfung / Abnahme:** R-F03/R-F04. Neu exportieren, erneut exportieren, öffnen, normal speichern, Ressourcen laden. Copy-/Rename-/Rollback-/Cleanupfehler an jedem Übergang und Prozessabbruch injizieren; Baum nach Neustart prüfen. Zwei Prozesse auf gleiches Ziel sowie parallele Exporte auf verschiedene Ziele. Ausschließlich temporäre Testordner.

### AP-F03 – Backupgeneration bis zum Commit erhalten

**Priorität:** P0. **Befund:** D02. **Abhängigkeit:** testbare Dateioperationen.  
**Dateien:** DataFile.cpp/.h und Savetests.

**Vertrag:** Vorher ist Projekt A und vorhandenes Backup B; neu gespeichert wird C. Bei Erfolg: Ziel C, Backup A. Bei fehlgeschlagenem Save bleibt Ziel A erhalten; bestehendes B darf nicht still verschwinden. War kein Backup vorhanden, keine künstliche Altgeneration versprechen.

**Umsetzungsschritte:**

1. Explizit verfolgen: altes Backup vorhanden, verschoben, neues Backup veröffentlicht, Projekt committed. Eindeutige temporäre Namen oder exklusiven Zielbesitz nutzen; stale `.incoming`/`.previous` nicht blind überschreiben.
2. A vollständig in privaten Backupkandidaten kopieren. Kopierfehler lässt A und B unverändert.
3. B sichern und Backup A veröffentlichen. Alle Remove-/Renameergebnisse auswerten. B bleibt bis zum erfolgreichen Projektcommit zurückrollbar.
4. QSaveFile mit deaktiviertem Direct-Write-Fallback erhalten. Rollback für Open-, Serialisierungs-, Kompressions-, Write- und Commitfehler vorsehen, nicht nur für `commit()`.
5. Gemeinsamen Abschluss-/Rollbackpfad verwenden. B nur über geprüfte Schritte zurück nach `.bak` bringen; bei Rollbackfehler keine letzte gute Generation entfernen. Verbliebene Dateien und Fehlerstatus ehrlich melden.
6. Nach erfolgreichem Projektcommit wegen misslungenem Cleanup nicht auf A zurückrollen. Erfolg mit Cleanupwarnung liefern; sichere Artefakte auffindbar lassen.
7. `existingSaveArtifacts()` um relevante Transaktionsreste ergänzen oder strukturierten Ersatz anbieten. Folgesaves dürfen rettbare Datenreste nicht ungeprüft vernichten.
8. Einzeldateiatomizität von QSaveFile nicht mit gemeinsamer Atomizität von Ziel und Backup oder allgemeiner Stromausfallsicherheit auf jedem Dateisystem gleichsetzen.

**Prüfung / Abnahme:** R-F05/R-F06. A/B/C mit unterschiedlichen Inhalten/Hashes; alle Dateioperationen einschließlich Rollback/Cleanup gezielt scheitern lassen. Ziel-/Backupinhalte und Artefakte prüfen. Dirtyzustand bleibt bei Fehler gesetzt. Windows-Dateisperren ergänzen deterministische Injektion, ersetzen sie nicht.

### AP-F04 – Wellenformsemantik belegen, bestätigte Fehler gezielt ändern

**Priorität:** P2. **Befund:** D03 korrigiert. **Abhängigkeit:** AP-F11.  
**Dateien:** SampleThumbnail.cpp/.h, SampleFrame.h, Thumbnailtests.

1. Aktuellen Vertrag als Ausgangspunkt festhalten: Floatanzahl = Frameanzahl × Kanalzahl. Keine weitere Multiplikation und kein ungeprüftes Halbieren der Peakbreite.
2. Tatsächlichen Konstruktor-/Zeichenpfad prüfen: leerer Puffer, 1 Frame, ungerade Frameanzahl, nur L/R und Signal ausschließlich im letzten Fünftel. Cache für kontrollierte Referenzfälle leeren.
3. Framebasierten Konstruktor nur bei belegtem Problem oder sinnvoll beseitigter Typunsicherheit einführen. Dann Aggregation, Zoomstufen und `samplesPerPeak` konsistent in Frames definieren und Auflösungsänderungen berücksichtigen.
4. Leeren Puffer schon vor `m_buffer->data()->data()` behandeln, sofern der Puffervertrag keinen dereferenzierbaren Dummy garantiert. Spätere Nullprüfung im Thumbnailkonstruktor schützt diese frühere Dereferenz nicht.
5. Trim, Reverse, Clipping und Wechsel Originalframes/Peakcache auf gleiche zeitliche Zuordnung prüfen. Geeignete ASan-/UBSan-Läufe ergänzen.
6. Separater Prüfpunkt: `contentRevisionOf` liest nur einen Stride. Das ist kein vollständiger Änderungsnachweis. Falls Bufferimmutabilität nicht garantiert ist, echte Revision erwägen und einen nicht abgetasteten mittleren Edit testen.

**Abnahme:** R-F07. Produktionspfad geprüft; allein `peakOverFrames()` beweist keinen korrekten Cacheaufbau. Bleibt D03 widerlegt, mit Tests schließen statt unnötigem Umbau.

### AP-F05 – PlayHandle-Freigabe außerhalb des Renderpfads absichern

**Priorität:** P1. **Befund:** D04. **Abhängigkeiten:** AP-F09/AP-F13; bei geänderten Batchzuständen AP-F15.  
**Dateien:** AudioEngine.cpp/.h, PlayHandle-Unterklassen, AudioBusHandle, gegebenenfalls Freigabehelfer.

1. Typen/Destruktoren inventarisieren: Allokator, QObject-Anteile, Affinität, Plugin-/Busreferenzen und letzter Workerzugriff. `affinityMatters()` nicht beim Verschieben umgehen.
2. Destruktorkosten messen. Auch `NotePlayHandleManager::release` und andere Freigaben betrachten; Entfernen einer `delete`-Zeile beweist keine gesamte Echtzeitsicherheit.
3. Besitzübergang nach abgeschlossenem Batch definieren. Frei verschiebbare Typen über vorallokierte begrenzte Übergabe mit korrekter Produzenten-/Konsumentenzahl freigeben. Affinitätsgebundene Typen im erlaubten Kontext behandeln.
4. Im Audiothread keine neue Mutexsperre, kein unbegrenztes vector-Wachstum, keine unkontrollierte `deleteLater`-Flut. Auch Qt-Ereignisübergaben können allokieren; Notification und Kapazität berücksichtigen.
5. Queueüberlaufvertrag: sichere Rückhaltung unter eindeutigem Besitzer, keine verlorenen Pointer und kein vorzeitiges Löschen. Unbegrenzte Speicheransammlung durch kontrollierte Lastbegrenzung vermeiden.
6. Außerhalb Audio regelmäßig leeren, nicht erst beim Beenden. Shutdown: Produzenten/Worker stoppen, Übergabe drainen, erst danach benötigte Engine-/Bus-/Pluginobjekte zerstören.

**Prüfung / Abnahme:** R-F08. Instrumentierter Destruktorthread, Überlauf, Stop/Start, Projektwechsel und Shutdown. Kein Leak/UAF, kein wachsender Rückstand; Callbackzeiten und Allokationen vor/nach Änderung messen.

### AP-F06 – Ausgabe bei Worker-Stall eindeutig definieren

**Priorität:** P1. **Befund:** D05. **Abhängigkeiten:** AP-F07/AP-F09/AP-F13/AP-F15.  
**Dateien:** AudioEngine.cpp/.h, AudioDisplayBuffer, Integrationstest mit steuerbaren Jobs.

**Entscheidung:** Bei erkanntem fehlgeschlagenem Renderdurchlauf digitale Stille ausgeben. Dies ist ein Fehlervertrag; eine vorhandene vorherige fertige Periode beweist allein keinen Loopfehler.

1. Bufferbesitz vor/nach Swap für Instrument-, Effekt- und Mixerfehler kartieren. Verbleibende Schreibziele aktiver Worker bestimmen.
2. Bevorzugt eigenen vorallokierten Stillepuffer verwenden, den kein Worker beschreibt. Wird Lesepuffer genullt, dessen Exklusivität für jeden Fehlerzweig nachweisen. Nicht blind gemeinsam benutzte Daten verändern.
3. Fehlerzustand halten, solange der alte Batch lebt. Keine neuen Stufen, Swaps oder Modellfreigaben, welche dessen Daten verändern. Weitere Callbacks bekommen sicheren Fehleroutput.
4. Erst nach Quieszenz und einmaligem Cleanup aus AP-F07 normal rendern. Late Writes alter Generationen dürfen nicht im neuen Output landen.
5. Display zeigt tatsächlich ausgegebene Stille; Profiler-/Indexabschluss genau einmal. TLS über AP-F12 absichern.
6. Transport-/Offlineexportvertrag separat festlegen: Timeout darf keine unbemerkt „erfolgreiche“ WAV mit eingefügten Nullperioden erzeugen. Definierter Fehler/Abbruch oder dokumentierter Retry nötig.

**Prüfung / Abnahme:** R-F09. Unterschiedlich markierte Perioden; Fehler in jeder Stufe und mehrere folgende Fehlercallbacks. Output samplegenau null, Snapshot korrekt, danach richtiger Renderzustand. Eigener Stillepuffer behebt keine Inlineblockade; AP-F15 bleibt erforderlich.

### AP-F07 – Mixer erst nach Batchende bereinigen

**Priorität:** P1. **Befund:** D10. **Abhängigkeiten:** AP-F09/AP-F13/AP-F15.  
**Dateien:** Mixer.cpp/.h, MixerChannel, AudioEngine-Fehlerzustand.

1. Initialisierung, `prepareMasterMix`, dynamische Send-Abhängigkeiten und frühe Returns kartieren. Tatsächliche Zustandsreste gegenüber nächster regulärer Vorbereitung bestimmen.
2. Fehlgeschlagene Generation als „Cleanup ausstehend“ markieren. Während aktiver Jobs weder Puffer nullen noch ProcessingState, `m_queued` oder Zähler ändern. RAII-Komplettreset der Vorfassung entfällt.
3. Nach nachgewiesener Quieszenz einmalig Puffer, ProcessingState, `m_queued`, `m_dependenciesMet` und weitere verwendete Send-/Abhängigkeitszustände zurücksetzen.
4. Neue Generation erst danach zulassen. Überholte/doppelte dynamische Enqueues eindeutig behandeln. Queueüberlauf und synchronen Fallback in denselben Vertrag aufnehmen.
5. Endet Suche ohne wartende Jobs, obwohl Master nicht Done ist, keinen Teilmix als Erfolg publizieren. Unvollständigen Graphlauf diagnostizieren und kontrolliert bereinigen.

**Prüfung / Abnahme:** R-F10. Latch hält Kanal aktiv: kein vorzeitiger Reset; nach Freigabe vollständiger Reset und normale Folgeperiode. Sendkette, Mute, verzögerter Job, Überlauf, unerfüllte Abhängigkeit abdecken. Audioinhalt und Zähler statt nur bool-Rückgabe prüfen.

### AP-F08 – Formaterkennung, Dekompression und XML begrenzen

**Priorität:** P1 für Budget, P2 für Diagnose. **Befunde:** D11/D18. **Abhängigkeit:** AP-F11.  
**Dateien:** DataFile.cpp, Parser-/Kompressionshelfer, CMake-Dependencies, Testdateien für gültige und beschädigte Eingaben.

1. Erkennung, Entpacken und XML-Parsefehler getrennt modellieren. Klarer XML-Text samt unterstützter BOM/Encodings behält XMLdiagnose. Endung nur als Hinweis; interne und historische Dateien bleiben relevant.
2. Qt-Längenheader plus zlib-Header prüfen: Deflate, zulässige Fensterkennung, FCHECK, Dictionarybehandlung. Plausibler Header ist Kandidat, kein Gültigkeitsbeweis.
3. Deklarierte Größe vorab prüfen, aber nicht vertrauen. Inkrementelles `inflate` oder gleichwertig begrenzte API mit festen Ausgabeblöcken; vor jedem Wachstum Gesamtbudget prüfen. Größencheck nach `qUncompress` kommt für den Spitzenspeicher zu spät.
4. Streamende, Prüfsummenfehler, abgeschnittene Daten, fehlenden Fortschritt und Restbytes unterscheiden. Abweichende deklarierte Größe anhand Altdateien bewerten. Keine stillschweigende Akzeptanz angehängter Zweitstreams ohne Vertrag.
5. Vor/während DOM-Aufbau Tiefe, Element-/Attributzahl und Textvolumen begrenzen, etwa durch begrenzten Streaming-Vorpass. Externe Entity-/Ressourcenauflösung kontrollieren. Rekursive Nachzählung schützt weder Parser-Vorabverbrauch noch sich selbst vor extremer Tiefe.
6. Bestehende 64 MiB Eingabe, 256 MiB dekodiert und 2 Mio. Knoten als Ausgangsgrenzen dokumentieren; mit realen Großprojekten abgleichen. Tests dürfen kleine injizierte Budgets verwenden.
7. Gültige .mmp/.mmpz/.xptz und Bestandsprojekte erhalten. Ladefehler dürfen bestehendes Projekt, Journal und Readiness nicht beschädigen. Fehlendes allgemeines Rollback nach `clearProject()` aus dem Gesamtplan bleibt offen, bis separat nachgewiesen.

**Prüfung / Abnahme:** R-F11–R-F13. XMLsyntaxfehler mit Zeile/Spalte, gültige Kompression, kurze/falsche Header, deklarierte Übergröße, klein deklarierter stark expandierender Stream, Prüfsummenfehler, tiefe Struktur und Knotengrenze. Verbrauch während des Vorgangs messen; kleines reproduzierbares Fuzzkorpus ergänzen.

### AP-F09 – Stop, Join und Workerzerstörung vereinheitlichen

**Priorität:** P1. **Befund:** D06. **Abhängigkeit:** Lifecycleentwurf zusammen mit AP-F13.  
**Dateien:** AudioEngineWorkerThread.cpp/.h, AudioEngine.cpp, Workertests.

1. Vorhandenen Engine-Join erhalten, bis vollständiger Ersatz vorliegt. Klassen-`quit()` setzt den Stop für die eigene Condition-Variable-Schleife. Bloßes `QThread::quit()` beendet keinen solchen Job.
2. Stop idempotent, Wartende wecken, neue Batches nach Shutdownbeginn verhindern. Laufende Jobs behalten alle benötigten Objekte.
3. Joinbesitzer bestimmen. Destruktor darf erst nach Join fortfahren beziehungsweise muss Zerstörung des lebenden Threads sicher verhindern. Selbst-Join abweisen.
4. Kein `QThread::terminate()` als Fallback. Unterbrochener Job kann Locks halten oder halbe Modell-/Pluginänderungen hinterlassen. Weitere kurze `wait()` repariert das nicht.
5. In-Process-Grenze offenlegen: Stopflag/Deadline können unendlichen Fremdcode nicht sicher präemptieren. Fail-Fast oder spätere Prozessisolation entscheiden. Vorhandenen harten Abbruch nicht als sauberen Shutdown ausgeben.
6. Globale Registrierung erst nach beendetem `run()` entfernen; Jobs erst danach freigeben. Gesamte Shutdownfrist statt unbemerkt neuer 3 Sekunden pro Worker bewerten.

**Prüfung / Abnahme:** R-F14. Idle, endlicher Job, Shutdown im Batch, direkte kontrollierte Zerstörung, nie gestarteter Inlineworker, wiederholter Stop. Unendlichen Job nur im Kindprozess mit äußerem Watchdog testen.

### AP-F10 – Recoveryauswahl, Prozessidentität und dauerhafte Sicherung

**Priorität:** P0 für Löschung/Aufbewahrung, P1 für Besitzerprüfung. **Befunde:** D07 korrigiert, D12/D13/D21. **Abhängigkeit:** AP-F03.  
**Dateien:** ConfigManager.cpp/.h, main.cpp, MainWindow.cpp/.h, Song-Lade-/Savepfade, Recoverytests.

1. Vorhandenen Startupdialog erweitern. Alle Kandidaten einschließlich Legacydatei mit Datum, Größe und verständlicher Kennung listen; global sortieren. Nicht unnötig Nutzerpfade in Diagnoseberichte übernehmen.
2. Pro Auswahl wiederherstellen, später entscheiden oder ausdrücklich ausgewählten Kandidaten verwerfen. Schließen/Escape löscht nichts. Eine Gesamtaktion muss als solche benannt sein; Einzelbutton darf nicht alle Dateien löschen.
3. Prozessstatus dreistufig: lebend, sicher beendet, unbekannt. Windows PID plus Erstellungszeit vergleichen, etwa über `GetProcessTimes`; Name allein reicht nicht. Unix PID-Recycling und `EPERM` bei `kill(pid, 0)` berücksichtigen. Zugriff verweigert ist kein Todesnachweis.
4. Metadaten-/Lockformat versionieren und atomar schreiben: UUID, PID und verfügbare Startidentität. PID-only-Altdaten konservativ lesen; aktive Altsitzungen nicht für tot erklären.
5. Zwischen Anzeige, Lesen und Löschen Identität/Besitz erneut prüfen. Zwei startende Instanzen dürfen nicht dieselbe Recovery gleichzeitig beanspruchen; prozessübergreifendes Claim-/Lockverfahren festlegen.
6. Recovery in eigene Sitzung laden; Quelle bei Fehler/Abbruch behalten. Erst nach neuer dauerhafter Recovery oder bestätigtem Projektsave als erledigt markieren. Crash direkt nach Laden darf die einzige rettbare Fassung nicht beseitigen.
7. Löschung auf validierte Recoverypfade im vorgesehenen Verzeichnis begrenzen; fremde Dateien und Symlinks/Junctions nicht durch ähnliche Namen freigeben. Löschfehler melden, andere Kandidaten erhalten.
8. Expliziten Projektstart, Autosave OFF, mehrere Instanzen und normalen Abschluss prüfen. Wiederherstellung darf kein unbeabsichtigtes späteres Überschreiben der Quelle voraussetzen.

**Prüfung / Abnahme:** R-F15–R-F17. Drei verwaiste, eine lebende Sitzung und Legacydatei. Nur Auswahl ändert sich. PID-Recycling, unbekannte Berechtigung und konkurrierende Claims simulieren. Crash vor erstem Autosave: mindestens eine vollständige Fassung beim Neustart auffindbar.

### AP-F11 – Kleine Produktionstests und vollständige Integration trennen

**Priorität:** P1. **Befunde:** D08/D20. **Abhängigkeit:** AP-F01 für Windowslaufzeit.  
**Dateien:** tests/CMakeLists.txt, StabilizationTest.cpp, neue Testtargets/kleine Produktionsbibliotheken.

1. Nach echten Dependencies aufteilen: ThreadPool/Profiler, DisplayBuffer, NoteTimeIndex, Waveform, Datei-/Backuptransaktionen, Worker-/Engineintegration, MCP-Artefakte. Eindeutige CTestnamen.
2. Kleine Tests linken dieselben Produktionsquellen beziehungsweise extrahierte Bibliotheken, die auch Anwendung nutzt. Keine im Test nachgebaute Save-/Schedulerlogik; sndfile-Stubs nicht als Laufzeitersatz.
3. StabilizationTest nicht einfach mit weniger Objekten und No-op-Stubs linkbar machen. Engine-/Song-/Mixerfälle behalten vollständigen Integrationstest samt echter Initialisierung und Dependencies.
4. AI-Artefaktfälle in MCP-ON-Target verschieben oder über gleiche Featuredefinitionen schützen. OFF darf keine AiJobManager-Symbole benötigen. ON/OFF in getrennten Buildverzeichnissen.
5. Schmalen Adapter oder gleichwertige deterministische Injektion für Copy/Rename/Open/Write/Commit/Rollback schaffen. Fehler gezielt ohne OS-Zufall auslösen.
6. Threadtest-Cleanup korrigieren: In `workerBatchStaysLiveUntilProcessReturns` wird Cleanup vor waiter deklariert. Bei früher Rückkehr wird daher der joinbare std::thread zuerst zerstört. Scope-/Besitzreihenfolge so ändern, dass Release und Join vor jeder Threadzerstörung garantiert sind. Alle QVERIFY-Abbruchpfade prüfen.
7. Labels/Timeouts für kleine Tests, Integration, Windows und manuelle Läufe definieren. Globale Queue-/Config-Zustände isolieren; temporäre Verzeichnisse statt Nutzerkonfiguration.
8. Minimal-Configurepfad unter Versionskontrolle schaffen. Lokaler `build/merge-check` ist keine portable Voraussetzung. Vollständige Dependencies für Integration separat dokumentieren.

**Prüfung / Abnahme:** R-F18/R-F19. Frischer Minimalbuild ohne Anwendungslink und separater echter Integrationstest. Absichtlicher Workerassert führt zu Testfehler statt terminate/Hänger. `ctest -N` listet; CTest baut fehlende EXEs nicht. Build separat nachweisen.

### AP-F12 – Render-Scope und alte Signalschnittstelle bereinigen

**Priorität:** P2. **Befunde:** D14/D16. **Abhängigkeit:** AP-F06/AP-F07.  
**Dateien:** AudioEngine.h/.cpp, Signalverbraucher, Scopetests.

1. RAII stellt vorherigen TLS-Wert beim Verlassen wieder her. Verbotene Rekursion gegebenenfalls im Debug erkennen.
2. Profiler-/Ausgabeabschluss genau einmal. Scope-Wiederherstellung bedeutet nicht, dass ein teilweise ausgeführter DSP-Job erfolgreich ist.
3. Ausnahmegrenzen definieren: nichts unkontrolliert über Backend-/C-Grenzen entweichen lassen. Beim Abfangen sicheren Fehlerzustand setzen und Diagnose außerhalb Audio ausgeben; kein pauschales catch-and-continue bei möglicherweise beschädigtem Modell.
4. `nextAudioBuffer` repositoryweit prüfen: alte/neue Signal-Slot-Syntax, Stringverbindungen, Metaobject, Plugins, dokumentierte API. Nur ohne unterstützte Verbraucher oder mit geplanter Migration entfernen.
5. DisplayBuffer-Vertrag erhalten, keinen Rohzeigerfallback für neue Verbraucher einführen.

**Abnahme:** R-F20. Kontrollierte Testausnahme stellt TLS korrekt wieder her und liefert definierten Fehlerstatus. Verbraucher/MOC-Targets bauen; Signalsuche dokumentiert.

### AP-F13 – Globale Queue einmalig initialisieren

**Priorität:** P1. **Befund:** D15. **Abhängigkeit:** Entwurf mit AP-F09.  
**Dateien:** AudioEngineWorkerThread.cpp/.h, Enginekonstruktion, Queuetests.

1. Initialisierung vom zyklischen Reset trennen. Queue unter eindeutigem Lifecyclebesitzer initialisieren, bevor Verarbeitung möglich ist.
2. Konstruktorreset entfernen. Poolaufbau als Phase mit nötiger Bereitschaftsbarriere gestalten. Nie gestarteten zusätzlichen Inlineworker berücksichtigen.
3. Registrierung, Start, Stop und Zerstörung gegen aktive Generationen absichern. workerThreads, s_alive und s_expected brauchen einheitliche Synchronisation.
4. Jeder Laufzeitreset bleibt an Quieszenz/drain gebunden. Später hinzugefügter Worker darf Epoch/Queue nicht nebenbei verändern.
5. Engineabbau/Neuaufbau und mehrere Engineinstanzen ausdrücklich erlauben oder überprüfbar verbieten. Statische Zustände zwischen Tests zurücksetzen.

**Abnahme:** R-F21. Mehrere Worker aufbauen/starten, verzögerten dynamischen Job ausführen, abbauen/neu aufbauen. Keine verlorenen Jobs, künstlichen Konstruktor-Epochen oder falschen Fertigmeldungen.

### AP-F14 – Detuninggrenzen erst nach Erreichbarkeitsprüfung ändern

**Priorität:** P2, zunächst Untersuchung. **Befund:** D17. **Abhängigkeit:** AP-F11.  
**Dateien:** NoteTimeIndex.h, src/gui/editors/PianoRoll.cpp, AutomationClip, Index-/Zeichentests.

1. Negative Knoten über Laden/Import/Bestandsdateien untersuchen. UI-/API-Einfügepfade klemmen aktuell auf 0; keinen Umbau auf Basis unerreichbarer Zustände.
2. Vertrag wählen: Negative Zeiten ungültig und kontrolliert normalisieren/abweisen, oder unterstützt und konsistent in Index, Zeichenvorfilter und Hit-Test berücksichtigen.
3. Bei Unterstützung genügt größere Länge nicht: Spanstart um linken Kurvenrand erweitern; Ende umfasst Notenkörper/rechten Rand. Beide Extrema in Geometriesignatur.
4. Paint-Vorauswahl `x <= viewportRight` und Detuninglängenprüfung ebenfalls anpassen. Indexfix allein hilft nicht bei anschließendem Verwerfen im Renderer.
5. Grenzrechnung in breitem Integer, danach geprüfte Begrenzung. Identität/Reihenfolge, Undo/Redo und reine Extremaänderung prüfen.

**Abnahme:** R-F22. Lineare Referenzauswahl gegen Index: lange Note von links, Kurve rechts, Grenzberührung, Null-/Negativlänge und gegebenenfalls negativer Kurvenstart bei Note rechts außerhalb. Unter/über 256 Noten gleiche Bildbedeutung. D17 darf mit Gegenbeweis geschlossen werden.

### AP-F15 – Tatsächliche Renderdeadline und Inlinearbeit offenlegen

**Priorität:** P1. **Befund:** D22. **Abhängigkeiten:** AP-F09/AP-F13.  
**Dateien:** Workerqueue, Mixer, Profiler, Deadline-/Integrationstests.

1. Gesamtzeit getrennt messen: Befüllung, Inlinearbeit, Workerwarten, dynamische Runden, Abschluss. Heutige 2–50-ms-Frist begrenzt nicht diese Summe und garantiert keine Audio-Deadline.
2. Absolute gemeinsame Deadline statt erneuter voller Frist pro Phase/Runde erwägen. Nach Ablauf keine neue teure Arbeit starten; Inlinejob und Mixerfallback explizit erfassen.
3. Verzögerten Job gezielt im aufrufenden Thread ausführen. Vorhandener Latch-Test mit `runInline=false` deckt diesen Fall nicht ab.
4. Architekturentscheidung dokumentieren: akzeptierte gemessene In-Process-Grenze oder asynchrone/prozessisolierte Grenze für unzuverlässige Arbeit. Uhrprüfung bricht keinen bereits hängenden synchronen Pluginaufruf ab.
5. Workerzahl 0/1/N, Überlauf und dynamische Mixerjobs abdecken. Entfernen von Inlinearbeit darf keine Queue ohne Konsumenten erzeugen.
6. Zähler unterscheiden: Deadline verfehlt, Wartebudget erreicht, Queue voll, Job unvollständig. Danach sichere Cleanup-/Ausgabeverträge aus AP-F07/AP-F06.

**Abnahme:** R-F23. Endliche Verzögerungen vor/während/nach Inlinearbeit, absolute Dauer messen. Unendliche Jobs nur im Child. Harte Gesamtfrist erst behaupten, wenn keine unbegrenzte synchrone Arbeit innerhalb der Grenze möglich ist.

### AP-F16 – Vollständige Produktabnahme

**Priorität:** P1. **Abhängigkeit:** die geänderten Pakete.  
**Dateien:** Nachweise unter doc/stabilization, CI und gegebenenfalls Verweise im Gesamtplan.

1. Vollständigen Referenzbuild mit echten Dependencies linken/starten. Generator, Toolset, Qt, Compiler, Architektur, Flags erfassen. Fehlende v142-/VS-Installation nicht durch unbemerkten Generatorwechsel verdecken.
2. MCP ON/OFF getrennt bauen/testen. Qt 6/Linux/macOS für betroffene unterstützte Targets; isolierte DPI-Prüfung nicht als Vollbuild ausgeben.
3. Installiertes Paket ohne Entwickler-PATH prüfen: DLLs, QPA, Plugin-/MCP-Hilfsprozesse. Nativer Monitorwechsel und offscreen-Paint sind unterschiedliche Nachweise.
4. Audiofehlerlauf mit Browservorschau, Sends, Plugin, MCP-Client, Projektwechsel und Shutdown. Deterministische Quellen mit markierten Perioden verwenden.
5. Geplante Gates: mindestens 1.000 Start-/Stopzyklen und 8 Stunden Langlauf auf vereinbarter Maschine. Noch keine gemessenen Ergebnisse. Speicher, Handle-/Threadzahl, XRuns und Cleanupreste protokollieren.
6. Hör-/Integritätsprüfung: Referenzrender, Dauer, Timing, Kanäle, endliche Samples, unerwartete Stille; Save/Reload und Undo/Redo. Bei Fremdplugins Toleranzen und Hörurteil getrennt.
7. findings.md, baseline.md, manual-acceptance.md, release-gates.md aktualisieren. Diese Dateien existieren bereits; gegenteilige Aussage im Gesamtplan bei nächster Synchronisierung korrigieren.

**Abnahme:** R-F24. Keine offene Datenverlustregression. Fehlende Geräte-/Plattformläufe bleiben sichtbar offen und werden nicht durch Quellprüfung ersetzt.

## 5. Verbindliche Regressionsmatrix

Dies sind geplante Szenarien, keine Behauptung bereits vorhandener Targets. Kleine Produktionsfunktionstests bevorzugen; echte Engine-/Prozess-/Dateiverträge brauchen Integration.

| ID | Szenario | Erwartung | Paket |
| --- | --- | --- | --- |
| R-F01 | Windows Release/Debug ohne manuelles PATH, native/offscreen DPI | Passende DLLs/Plugins; Backend und Skalierung korrekt. | AP-F01 |
| R-F02 | DLL fehlt in privater Kopie, Launcher/Watchdog | Fehlercode statt unbegrenzter Dialogblockade. | AP-F01 |
| R-F03 | Bundle neu, erneut exportieren, geladenes Projekt normal speichern | Definierte Save-Semantik und ladbare Ressourcen. | AP-F02 |
| R-F04 | Bundlefehler je Übergang, Crash, parallele Instanzen | Gute Fassung auffindbar; keine fremde/aktive Transaktion gelöscht. | AP-F02 |
| R-F05 | A/B/C-Save mit Copy/Rename/Open/Write/Commitfehler | A und bisherige Backupgeneration gesichert; Dirty bleibt. | AP-F03 |
| R-F06 | Rollback-/Cleanupfehler, alte Transaktionsreste | Keine letzte gute Fassung gelöscht; Status/Artefakte korrekt. | AP-F03 |
| R-F07 | Stereoimpulse am Ende, leer/1/ungerade Frames, Trim/Reverse/Zoom | Vollständige Zeitabbildung im Produktionspfad, keine ungültige Dereferenz. | AP-F04 |
| R-F08 | Handlefreigabe, volle Übergabe, Shutdown | Erlaubter Thread, kein Leak/UAF, begrenzter Rückstand. | AP-F05 |
| R-F09 | Stall je Renderstufe, mehrere Fehlercallbacks | Sicherer Stilleoutput/Snapshot; keine alte Generation im Folgeoutput. | AP-F06 |
| R-F10 | Aktiver Mixerjob nach Timeout, dann Release | Kein vorzeitiger Reset; anschließend genau einmal vollständig bereinigt. | AP-F07 |
| R-F11 | Kaputtes XML, BOM, kurze/falsche Header | Zutreffende Fehlerklasse und XMLdiagnose. | AP-F08 |
| R-F12 | Klein deklarierter großer Stream, Prüfsummenfehler | Abbruch während begrenzten Entpackens. | AP-F08 |
| R-F13 | Tiefe Struktur, viele Elemente/Attribute, gültiges Großprojekt | Tatsächliches Budget, kompatibles Laden zulässiger Daten. | AP-F08 |
| R-F14 | Idle/endlicher/nie gestarteter Worker, Hänger im Child | Stop/Join korrekt, keine Terminierung/Freigabe aktiver Daten. | AP-F09 |
| R-F15 | Mehrere Recoverys, Legacy, lebende Sitzung, Einzelverwerfen | Nur Auswahl verändert. | AP-F10 |
| R-F16 | PID-Recycling, Zugriffsfehler, konkurrierender Claim | Unbekannt nicht als tot behandelt; keine fremde Löschung. | AP-F10 |
| R-F17 | Crash nach Recoveryladen vor Autosave | Mindestens eine vollständige Fassung bleibt. | AP-F10 |
| R-F18 | Frischer kleiner Build plus vollständige Integration | Produktionslogik gelinkt, keine lokalen Stubs vorausgesetzt. | AP-F11 |
| R-F19 | MCP ON/OFF, absichtlicher Threadtest-Assert | OFF ohne AI-Symbole; Fehler ohne terminate/Hänger. | AP-F11 |
| R-F20 | Ausnahme im Render-Scope, Signalsuche und Build | TLS/Fehlerstatus korrekt, Verbraucher/MOC berücksichtigt. | AP-F12 |
| R-F21 | Poolaufbau, verzögerter dynamischer Job, Neuaufbau | Keine verlorenen Jobs oder Konstruktorresets auf aktiver Queue. | AP-F13 |
| R-F22 | Linearer Geometrieoracle gegen Index, Extrema und Undo | Gleiche Kandidaten/Bilder gemäß entschiedenem Zeitvertrag. | AP-F14 |
| R-F23 | Verzögerte Inlinearbeit, mehrere Runden, Überlauf | Gesamtdauer sichtbar; ehrliche Deadlinegrenze, sichere Folgegeneration. | AP-F15 |
| R-F24 | Paket, kombinierter Fehlerlauf, Stress, Langlauf, Hören/Reload | Releasegates durch echte Protokolle belegt. | AP-F16 |

### 5.1 Konfigurationsumfang

| Achse | Mindestumfang |
| --- | --- |
| Windows | Vollbuild, Release und relevante Debugtests; installierter Start ohne Entwicklerumgebung. |
| Qt | Bestehender Qt-5.15-Referenzpfad; Qt 6 für betroffene unterstützte Targets. DPI-CI belegt keinen Gesamtbuild. |
| CMake | Bisherige Mindestversion oder ausdrücklich beschlossener Upgradepfad. |
| Flags | MCP ON/OFF separat; Laufzeit ohne MCP zusätzlich bei Serviceänderungen. |
| Audio | 44,1/48/96 kHz, repräsentative 128/256/512 Frames soweit unterstützt; 0/1/N Zusatzworker. |
| Dateien | Unicode/Leerzeichen/mehrere Punkte, Lock, Rechte, belegte Sicherungsnamen, Parallelität, langsames Laufwerk. |
| GUI | Offscreen für Bilder; Windows-QPA für nativen Test; reale gemischte Monitore für Produktabnahme. |
| Andere Plattformen | Linux/macOS für portable Änderungen; Prozess-/Locksemantik bei Recovery gesondert. |
| Diagnose | ASan/UBSan/TSan auf geeigneter Toolchain; fehlende Abdeckung dokumentieren. |

Kein vollständiges kartesisches Produkt: Auswahl pro Vertragsänderung begründen. Sanitizerläufe sind keine Performancebaseline; Callbackzeiten in Release/RelWithDebInfo separat messen.

## 6. Build- und Testablauf

### 6.1 Bestandsaufnahme

```powershell
git status --short
git rev-parse HEAD
cmake --version
cmake --list-presets
```

Generatoren/Toolsets, Qt- und Dependencypräfixe tatsächlich ermitteln. Schmutzigen Arbeitsbaum dokumentieren und fremde Änderungen erhalten. Alte Buildverzeichnisse beweisen keine vollständige Installation.

### 6.2 Vollständiger Referenzbuild

Bestehende Presets erwarten `LMMS_QT5_PREFIX` und `LMMS_DEPS_PREFIX` mit echten Installationen. Preset verlangt Visual Studio 2019/v142; zuerst Verfügbarkeit prüfen. Anderer kompatibler Generator benötigt nachvollziehbaren eigenen Preset und getrenntes Verzeichnis.

```powershell
cmake --preset windows-qt5-msvc-v142
if ($LASTEXITCODE -ne 0) { throw 'Configure failed' }
cmake --build --preset windows-qt5-release --parallel 2
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
ctest --test-dir build/reference-qt5 -C Release -N
ctest --test-dir build/reference-qt5 -C Release --output-on-failure --parallel 2
if ($LASTEXITCODE -ne 0) { throw 'Tests failed' }
```

Ohne manuelle Qt-Laufzeitvariablen erst nach AP-F01 erwartet. Dependencyintegration samt gegebenenfalls nötiger Toolchaindatei im Configure-Nachweis erfassen; zwei Präfixe allein garantieren keinen Vollbuild.

MCP OFF separat:

```powershell
cmake --preset windows-qt5-msvc-v142-no-mcp
if ($LASTEXITCODE -ne 0) { throw 'Configure failed' }
cmake --build --preset windows-qt5-release-no-mcp --parallel 2
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
ctest --test-dir build/reference-qt5-no-mcp -C Release --output-on-failure --parallel 2
if ($LASTEXITCODE -ne 0) { throw 'Tests failed' }
```

Gesamtbuild muss Testtargets tatsächlich erzeugen. Bei gezielten Builds nur passende Tests auswählen. `ctest -R Stabilization` baut keinen fehlenden Test nach.

### 6.3 Vorhandener isolierter DPI-Pfad

Beispiel mit gültigem gesetztem `LMMS_QT5_PREFIX` und neuem Verzeichnis; Generator/Toolset gegebenenfalls ausdrücklich ergänzen:

```powershell
cmake -S tests/dpi -B build/phase2-dpi -DDPI_QT_MAJOR=5 "-DCMAKE_PREFIX_PATH=$env:LMMS_QT5_PREFIX"
if ($LASTEXITCODE -ne 0) { throw 'DPI configure failed' }
cmake --build build/phase2-dpi --config Release --parallel 2
if ($LASTEXITCODE -ne 0) { throw 'DPI build failed' }
ctest --test-dir build/phase2-dpi -C Release --output-on-failure --parallel 2
if ($LASTEXITCODE -ne 0) { throw 'DPI tests failed' }
```

Vor AP-F01 kann explizite DLL-Umgebung nötig sein. Kein Nachweis für Save/Audio/Recovery. Neue Minimaltesttargets aus AP-F11 erhalten nach Implementierung eigene geprüfte Befehle; noch nicht existierende Presets werden hier nicht als ausführbar ausgegeben.

### 6.4 MCP-Verträge bei betroffenem Companioncode

```powershell
npm --prefix tools/mcp test
if ($LASTEXITCODE -ne 0) { throw 'MCP tests failed' }
```

Node-/Paketvoraussetzungen zuerst erfüllen. Testzahl aus aktuellem Lauf übernehmen. TypeScripttests ersetzen keine native Engine-/MCP-Integration.

## 7. Nachweise, Aufwand und Änderungsdisziplin

### 7.1 Nachweis pro Paket

Eintrag in [findings.md](doc/stabilization/findings.md) oder verlinkter eigener Datei:

| Feld | Inhalt |
| --- | --- |
| Identität | AP-F-/D-/R-F-IDs, Commit plus lokaler Diff, Datum, Bearbeiter. |
| Reproduktion | Minimales Fixture, Auslöser, Vorzustand, erwarteter/beobachteter Effekt. |
| Entscheidung | Gewählter Datei-/Besitz-/Threadvertrag, relevante Abwägung. |
| Build | OS, Architektur, Compiler/Toolset, Qt, CMake, Generator, Flags, Dependencies. |
| Prüfung | Befehle, Exitcodes, Testzahl, Logs, gegebenenfalls Bild/Audio/Hashvergleich. |
| Fehlerzustand | Dateien, Dirty-/Journalstatus, Jobs/Threads, Recoverymöglichkeit. |
| Grenzen | Skips, ungeprüfte Plattformen, verbleibende Risiken/Folgeschritte. |

Vorhandene Ablage: [baseline.md](doc/stabilization/baseline.md), [manual-acceptance.md](doc/stabilization/manual-acceptance.md), [release-gates.md](doc/stabilization/release-gates.md). Neue Messdaten können unter `doc/stabilization/benchmarks/` abgelegt werden. Logs ohne unnötige Nutzerpfade/Projektinhalte.

### 7.2 Aufwand zur Neuplanung

Grobe Spannen für vertraute Entwickler bei funktionierender Toolchain, keine Zusagen. Dependencyaufbau und schwer reproduzierbare Hardwarefehler kommen hinzu.

| Block | Pakete | Schätzung | Größte Unsicherheit |
| --- | --- | --- | --- |
| Infrastruktur | F01/F11 | 2–5 Arbeitstage | Vollbuild, Deployment, Dependencyaufteilung. |
| Save/Bundle | F03/F02 | 3–7 Arbeitstage | Crashwiederaufnahme, Dateifehler, Parallelität. |
| Recovery | F10 | 2–5 Arbeitstage | Startidentität, Claims, Sicherung nach Laden. |
| Audiolifecycle | F13/F09/F15/F07/F06 | 5–12 Arbeitstage | Inline-/Pluginhänger, Zustände über mehrere Callbacks. |
| Weitere Robustheit | F05/F08/F04/F12/F14 | 4–10 Arbeitstage | Affinitäten, Parserbudget, reale Geometrieprobleme. |
| Produktabnahme | F16 | 2–5 aktive Arbeitstage plus Langläufe | Plattform-/Geräteverfügbarkeit und Integration. |

Nach Infrastruktur und ersten Fehlerläufen neu schätzen. Gegenbeweis in F14 spart Umbau. Erfordert F15 Architekturänderungen, diese als eigenen Folgeumfang konkretisieren; harte Deadlineversprechen bleiben bis dahin offen.

### 7.3 Review- und Freigaberegeln

- Kleine fachlich zusammenhängende Änderungen; Dateitransaktionen und Workerzustände getrennt reviewbar. Keine großflächigen Refactorings ohne Fehlerbezug.
- Erst Regression oder reproduzierbarer Fehlerablauf, dann Implementierung/Nachweis. Tests prüfen Produktionsverträge, nicht nur private Implementierungsdetails.
- Keine Threadterminierung, kein Entfernen aktiver Locks, keine rekursive Bereinigung ohne Besitzprüfung.
- Projektformat/musikalische Bedeutung erhalten; neue Marker und Locks versionieren, Bestandsfälle prüfen.
- Fail-Fast bei Workerhänger bleibt sichtbares Produktlimit bis zur getesteten sicheren Alternative.
- Gesamtplan bleibt für offene AP00–AP20 maßgeblich. Phase 2 erklärt ältere ungemessene Performance-/DPI-/MCP-Ziele nicht automatisch für erfüllt.

## 8. Umsetzungscheckliste

Haken setzt Paketabnahme und Nachweislink voraus. Der Lauf vom 17. Oktober 2026 steht in [doc/stabilization/findings.md](doc/stabilization/findings.md). Abgehakt sind nur Pakete, deren automatisierte Fälle in diesem Lauf bestanden haben. Audio-, Dialog- und Langlauftore bleiben offen.

- [ ] AP-F01: Windowsruntime, negativer Loaderfall, native DPI-Umgebung erhalten.
- [ ] AP-F11: Kleine Produktions-/vollständige Integrationstests, MCP OFF, sichere Threadtest-Cleanups.
- [x] AP-F03: Backup bei allen Savefehlern erhalten, Rollback/Cleanup geprüft. Nachweis: `SaveTransactionTest` in [findings.md](doc/stabilization/findings.md). Voller Anwendungslink fehlt.
- [ ] AP-F02: Bundleexport/-ersetzung, Zielbesitz, Parallelität, Crashwiederaufnahme.
- [ ] AP-F10: Mehrfachauswahl, konservative Besitzeridentität, dauerhafter Schutz nach Laden.
- [ ] AP-F13: Einmalige Queueinitialisierung und eindeutige Generationen.
- [ ] AP-F09: Stop/Join ohne Threadterminierung, Hängergrenze dokumentiert.
- [ ] AP-F15: Inlinezeit/Gesamtdeadline gemessen, Architekturentscheidung abgelegt.
- [ ] AP-F07: Mixercleanup erst nach Quieszenz, vollständiger Neustartzustand.
- [ ] AP-F06: Stilleoutput und definierter Offlineexportfehler.
- [x] AP-F08: Formatdiagnosen und tatsächliche Entpack-/XMLbudgets. Nachweis: `ProjectCodecTest` inkl. `qCompress`-Rundlauf, 0 übersprungen. Voller Projektload fehlt.
- [ ] AP-F05: Affinitätsgerechte begrenzte Freigabe samt Überlauf/Shutdown.
- [ ] AP-F04: Halbierungsbehauptung geschlossen, reale Stereo-/Leer-/Zoomfälle geprüft.
- [ ] AP-F12: Render-Scope und gegebenenfalls Signalbereinigung.
- [ ] AP-F14: Negative Zeiten belegt/widerlegt, Geometriepfade konsistent.
- [ ] AP-F16: Vollbuild, Paket, Fehlerlauf, Langlauf, Hören und Reload belegt.
- [ ] Befundkatalog/Nachweise/Gesamtplan synchronisiert; historische Zahlen nicht als aktuelle Tests ausgegeben.

## 9. Technische Primärquellen

Für die Planprüfung am 17. Oktober 2026 geprüft. Allgemeine Verträge; konkrete API-Verfügbarkeit gegen eingesetzte Version prüfen.

- [CMake: ENVIRONMENT_MODIFICATION](https://cmake.org/cmake/help/latest/prop_test/ENVIRONMENT_MODIFICATION.html): ab 3.22, plattformgerechte PATH-Ergänzung.
- [CMake: Generatorausdrücke](https://cmake.org/cmake/help/latest/manual/cmake-generator-expressions.7.html): konfigurationsabhängige Targetpfade und Quoting.
- [Microsoft: SetErrorMode](https://learn.microsoft.com/en-us/windows/win32/api/errhandlingapi/nf-errhandlingapi-seterrormode): Fehlermodus und Vererbung an Kindprozesse; Grundlage des Launchers.
- [Qt 5.15: QThread](https://doc.qt.io/archives/qt-5.15/qthread.html): Lebensdauer, Warten und Terminierungsrisiken.
- [Qt 5.15: QSaveFile](https://doc.qt.io/archives/qt-5.15/qsavefile.html): Einzeldateiveröffentlichung und Direct-Write-Fallback.
- [Qt 5.15: QByteArray/qUncompress](https://doc.qt.io/archives/qt-5.15/qbytearray.html): Qt-Kompressionsformat; Längeninformation ist kein eigenständiges vertrauenswürdiges Budget.
- [zlib: Handbuch](https://www.zlib.net/manual.html): inkrementelle Dekompression, Puffer und Fehlerzustände.
- [Microsoft: GetProcessTimes](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getprocesstimes): Erstellungszeit als Teil der Windows-Prozessidentität.
