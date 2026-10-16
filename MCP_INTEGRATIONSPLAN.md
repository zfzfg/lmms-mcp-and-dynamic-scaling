# MCP-Integration für LMMS: vollständiger Architektur- und Umsetzungsplan

**Stand:** 16. Oktober 2026  
**Untersuchter Repository-Stand:** `a2f57e70ce9c3468b4b6d21955bbe65a0989048a`, Branch `master`  
**Programm:** LMMS, im untersuchten CMake-Projekt `1.3.0-alpha`  
**Status:** Vollständig implementiert auf Branch `feat/mcp-integration` (Phasen 0–6).

## Überblick

LMMS soll über MCP von einer KI gelesen und gesteuert werden können: Projektstruktur verstehen, musikalische Änderungen vorbereiten, Noten und Parameter bearbeiten, Wiedergabe steuern und überprüfbare Projekt- und Audioartefakte erzeugen. Die KI läuft im gewählten MCP-Host. LMMS stellt dafür eine klar begrenzte Anwendungsschnittstelle bereit.

**Empfohlene Architektur:** eine native C++/Qt-Steuerschicht innerhalb von LMMS und ein mitgelieferter MCP-Begleitprozess auf Basis des offiziellen TypeScript-SDKs. Der Begleitprozess spricht mit dem KI-Host über MCP/stdio und mit LMMS über einen abgesicherten lokalen IPC-Kanal. LMMS bleibt die maßgebliche Instanz für Berechtigungen, Objektauflösung und Änderungen am Projekt.

Das ist eine echte Integration in das laufende Programm: Änderungen verwenden die LMMS-Modelle und erscheinen in den vorhandenen Editoren. Der Begleitprozess übernimmt das wechselnde MCP-Protokoll, während die native Steuerschicht die Besonderheiten von LMMS behandelt.

### Inhaltsverzeichnis

1. [Ziele, Umfang und Releases](#ziele)
2. [Ergebnisse der Repository-Untersuchung](#repo)
3. [Architekturentscheidung](#architektur)
4. [MCP-Versionen und Protokollumfang](#protokoll)
5. [Komponenten und konkrete Dateien](#dateien)
6. [Start, Verbindung und Lebenszyklus](#lebenszyklus)
7. [Threads und Echtzeitschutz](#threads)
8. [IDs, Revisionen und konkurrierende Änderungen](#revisionen)
9. [Musikalischer Datenvertrag](#daten)
10. [Tools, Ressourcen und Prompts](#tools)
11. [Vorschau, Transaktionen und Undo](#transaktionen)
12. [Beispiele für Aufrufe und Ergebnisse](#beispiele)
13. [Spuren, Instrumente, Patterns, Mixer und Automation](#funktionen)
14. [Projektdateien, Assets und Speicheroperationen](#dateienzugriff)
15. [Rendering und asynchrone Jobs](#rendering)
16. [Oberfläche und Einstellungen](#oberflaeche)
17. [Berechtigungen und Datenschutz](#sicherheit)
18. [Fehlerbehandlung, Limits und Diagnose](#betrieb)
19. [Tests und Qualitätskriterien](#tests)
20. [Phasen, Arbeitspakete und Aufwand](#umsetzung)
21. [Abnahmeszenarien und Fertigkriterien](#abnahme)
22. [Risiken und offene Entscheidungen mit Vorgaben](#risiken)
23. [Quellen und Einstiegspunkte](#quellen)

<a id="ziele"></a>
## 1. Ziele, Umfang und Releases

### 1.1 Konkrete Benutzerabläufe

- „Erkläre mein aktuelles Projekt und nenne die Instrumente, das Tempo und die Clip-Struktur.“
- „Erzeuge in dieser Instrumentenspur eine viertaktige Basslinie und zeige vorher die Änderungen.“
- „Transponiere den ausgewählten Melodieclip um eine Oktave; mache das bei Bedarf mit einem Undo rückgängig.“
- „Erstelle eine neue Spur mit einem verfügbaren internen Synthesizer.“
- „Setze eine Lautstärkeautomation und prüfe das Mixer-Routing.“
- „Speichere eine Kopie und rendere eine WAV-Vorschau dieses Projektstands.“
- „Überprüfe, ob der Export wirklich erfolgreich war, und gib mir das erzeugte Artefakt.“

Die KI erzeugt musikalische Vorschläge außerhalb von LMMS und übergibt strukturierte Noten oder Änderungen. Ein MCP-Server allein enthält weder ein Sprachmodell noch eine musikalische Audio-KI. Für die lokale MCP-Steuerung benötigt LMMS deshalb keinen eigenen Modell-API-Key.

### 1.2 Gestaffelter Umfang

| Stufe | Enthalten | Freigabebedingung |
| --- | --- | --- |
| R0: Machbarkeitsnachweis | Ein Host liest den Status einer ausdrücklich gewählten laufenden LMMS-Instanz. | Protokoll und IPC unter Windows nachgewiesen. |
| R1: Lesezugriff | Projekt, Song- und PatternStore-Spuren, Clips, Noten, Kernparameter, Mixer und Instrumentkatalog. | Datenvertrag, Berechtigungen und vollständige Seitennavigation getestet. |
| R2: Schreib-MVP | Vorhandene Instrumentenspuren verwenden; Melodieclips anlegen; Noten hinzufügen, ändern und transponieren; Tempo und einfache Spurparameter ändern; Transport; Vorschau und Undo. | Gruppiertes Undo, Revisionsschutz und begrenzte Commit-Zeiten funktionieren. |
| R3: Produktionsfunktionen | Interne Instrumentenspuren anlegen, Presets/Samples aus freigegebenen Bibliotheken, Pattern-Bearbeitung, Kern-Automation, Mixer-Änderungen, Projektkopien. | Je Funktion sichere Lebensdauer, Fehlerpfad und Undo beziehungsweise Job-Semantik bewiesen. |
| R4: Audio und Auslieferung | Snapshot-Rendering, Artefaktprüfung, Windows-/Linux-/macOS-Pakete, Host-Anleitungen. | Installiertes Paket besteht die Ende-zu-Ende-Abnahme. |
| R5: Erweiterungen | Kontrolliertes Projektöffnen, zusätzliche Plugin-Adapter, Abonnements, optional Streamable HTTP. | Eigene Abnahme und dokumentierte Kompatibilität. |

Für R2 reicht eine vorhandene, spielbare Instrumentenspur. Das Anlegen oder Ersetzen eines Plugins ist absichtlich ein späteres Arbeitspaket mit eigener Fehlerbehandlung.

### 1.3 Abgrenzung des ersten Releases

Zum ersten stabilen Umfang gehören keine frei ausführbaren Skripte, Shell-Kommandos, beliebigen C++-Methodenaufrufe oder ungeprüften XML-Einspeisungen. Netzwerkzugriff, Installation von Plugins und Austausch von Audiodateien mit einem Modell sind eigenständige Funktionen mit gesondertem Umfang. Ein eingebautes Chatfenster ist für die MCP-Anbindung nicht erforderlich.

<a id="repo"></a>
## 2. Ergebnisse der Repository-Untersuchung

Die Architektur wurde mit CocoIndex-Suchen nach Engine-Start, Projektverwaltung, Modelländerungen, Notenverwaltung, Instrumenten und Mixer-Routing erkundet. Relevante Treffer wurden anschließend direkt im lokalen Quellcode geprüft. Die folgenden Aussagen beziehen sich auf diesen Checkout und müssen beim Implementierungsstart gegen den dann aktuellen Stand abgeglichen werden.

| Bereich | Vorhandener Einstiegspunkt | Konsequenz für die Integration |
| --- | --- | --- |
| Build | [CMakeLists.txt](CMakeLists.txt), [src/CMakeLists.txt](src/CMakeLists.txt) | CMake ab 3.13; Anwendung verwendet C++20. Neue Komponenten in diese Struktur aufnehmen. |
| Qt | [CMakeLists.txt](CMakeLists.txt), etwa Zeile 264 | Standardpfad Qt 5.15; Qt 6 ist über `WANT_QT6` experimentell. Keine ausschließlich Qt-6-spezifische Bridge entwickeln. |
| Netzwerkmodul | Qt-Komponentenliste im Haupt-CMake | `Core`, `Gui`, `Widgets`, `Xml`, `Svg` sind eingebunden; `Network` muss für `QLocalServer`/`QLocalSocket` optional ergänzt werden. |
| Prozessstart | [src/core/main.cpp](src/core/main.cpp) | GUI-Start, CLI-Argumente und Renderbetrieb sind vorhanden. Der normale Prozess schreibt an mehreren Stellen nach stdout und eignet sich nicht unverändert als MCP/stdio-Prozess. |
| Engine | [include/Engine.h](include/Engine.h), [src/core/Engine.cpp](src/core/Engine.cpp), Zeilen 58 und 95 | `Engine::getSong()`, `patternStore()`, `mixer()`, `audioEngine()` und `projectJournal()` liefern die zentralen Modelle. Initialisierung und Abbau geben den Lebenszyklus vor. |
| GUI | [src/gui/GuiApplication.cpp](src/gui/GuiApplication.cpp), etwa Zeile 152 | `Engine::init(false)` erfolgt vor dem Aufbau aller Editoren. Die Bridge darf Schreibbereitschaft erst nach vollständigem Projekt- und GUI-Start melden. |
| Song | [include/Song.h](include/Song.h), [src/core/Song.cpp](src/core/Song.cpp) | Tempo, Taktart, Wiedergabe, Dateiname, Lade-/Exportzustand und Projektverwaltung sind zugänglich. |
| Spuren | [include/TrackContainer.h](include/TrackContainer.h), [src/core/Track.cpp](src/core/Track.cpp), etwa Zeile 82 | `Track::create`, `TrackContainer::tracks`, `trackAdded`, `trackRemoved`, `trackMoved` sind nutzbar. Elterncontainer und Audio-Synchronisierung beachten. |
| Clips/Noten | [include/Clip.h](include/Clip.h), [include/MidiClip.h](include/MidiClip.h), [src/tracks/MidiClip.cpp](src/tracks/MidiClip.cpp), etwa Zeile 180 | `createClip`, `MidiClip::addNote`, `removeNote`, `clearNotes` und `rearrangeAllNotes` sind vorhanden. Für KI-Noten `addNote(..., false)` verwenden, damit kein GUI-Quantisierungszustand einfließt. |
| Notenidentität | [include/Note.h](include/Note.h) | `Note` ist ein `SerializingObject`, kein `QObject` und kein `JournallingObject`. Persistente Noten-UUIDs sind nicht vorhanden. |
| Musikalische Einheiten | [include/TimePos.h](include/TimePos.h), [include/Note.h](include/Note.h), [include/volume.h](include/volume.h), [include/panning.h](include/panning.h) | 192 Basisticks entsprechen im 4/4-Takt einem Takt, also 48 Ticks pro Viertelnote. Notentasten 0–127; Notenlautstärke 0–200, Standard 100; Panorama −100 bis 100. |
| Audio-Synchronisierung | [include/AudioEngine.h](include/AudioEngine.h), [src/core/AudioEngine.cpp](src/core/AudioEngine.cpp), Zeilen 567–577 | `requestChangeInModel`, `doneChangeInModel` und der RAII-Zugang `requestChangesGuard()` existieren. Sie sind keine vollständige API für beliebige Thread-Zugriffe. |
| Undo | [include/ProjectJournal.h](include/ProjectJournal.h), [src/core/ProjectJournal.cpp](src/core/ProjectJournal.cpp) | Checkpoints speichern den Zustand eines `JournallingObject`. Eine fertige allgemeine Mehrfachobjekt-Transaktion ist nicht vorhanden. `Song::insertBar` enthält dazu selbst einen FIXME. |
| Änderungsanzeige | [src/core/Song.cpp](src/core/Song.cpp), etwa Zeile 455 | `Song::modified()` wird nur beim Wechsel des Dirty-Flags ausgelöst. Das Signal ist kein vollständiger Änderungszähler. |
| Instrumente | [include/PluginFactory.h](include/PluginFactory.h), [src/tracks/InstrumentTrack.cpp](src/tracks/InstrumentTrack.cpp), etwa Zeile 1040 | `getPluginFactory()->descriptors(...)`, `pluginInfos()` und `loadInstrument(...)` bieten Ansatzpunkte. Ein Anzeigename ist kein verlässlicher Plugin-Schlüssel. |
| Patterns | [include/PatternStore.h](include/PatternStore.h), [include/PatternTrack.h](include/PatternTrack.h) | PatternStore enthält die eigentlichen Pattern-Spuren und Clips; PatternTracks im Song referenzieren Patterns. Beide Ebenen müssen getrennt adressierbar sein. |
| Automation/Mixer | [include/AutomationClip.h](include/AutomationClip.h), [include/Mixer.h](include/Mixer.h), [src/core/Mixer.cpp](src/core/Mixer.cpp) | Automation verwendet `AutomatableModel`-Verbindungen; Mixer besitzt Routenerstellung und `isInfiniteLoop`. Keine ungeprüften Kanalindizes nach außen geben. |
| Dateien | [src/core/Song.cpp](src/core/Song.cpp), etwa Zeilen 1008, 1215 und 1277; [src/core/DataFile.cpp](src/core/DataFile.cpp), etwa Zeile 315 | `loadProject` liefert `void`; `saveProjectFile` liefert `bool`. Lade- und Schreibpfade können modale GUI-Fehler anzeigen und brauchen für Remote-Aufrufe einen strukturierten Fehlerpfad. |
| Projekt-Sicherheit | [src/core/DataFile.cpp](src/core/DataFile.cpp), etwa Zeile 530; `Song::loadProject` | Vorhandene Prüfung auf lokale Plugin-Pfade in Projekten erhalten. MCP darf diesen Schutz nicht durch eine eigene Laderoutine umgehen. |
| Rendering | [include/RenderManager.h](include/RenderManager.h), [src/core/RenderManager.cpp](src/core/RenderManager.cpp), [include/ProjectRenderer.h](include/ProjectRenderer.h) | Mixdown und Track-Rendering existieren. `RenderManager` tauscht das Audiogerät; Track-Rendering verändert temporär Mute-Zustände. `finished()` allein bescheinigt keinen erfolgreichen Export. |
| Tests | [tests/CMakeLists.txt](tests/CMakeLists.txt), [tests/src/tracks/AutomationTrackTest.cpp](tests/src/tracks/AutomationTrackTest.cpp) | QtTest und CTest vorhanden; bestehende Tests initialisieren `Engine::init(true)`. Neue Domain-Tests daran anlehnen. |
| Auslieferung | [src/CMakeLists.txt](src/CMakeLists.txt), [cmake/install](cmake/install), [.github/workflows/build.yml](.github/workflows/build.yml) | `lmms` ist unter Windows außerhalb von Debug ein GUI-Executable. Ein stdio-Launcher benötigt ein eigenes Konsolen-Target und eigene Paketierungsregeln. |

In den untersuchten eigenen Quellverzeichnissen wurde keine vorhandene MCP- oder `QLocalServer`-Integration gefunden. Die bestehende Remote-Plugin-Infrastruktur dient dem Pluginbetrieb; sie ist kein dokumentierter allgemeiner Projektsteuerungsdienst.

<a id="architektur"></a>
## 3. Architekturentscheidung

### 3.1 Empfohlener Aufbau

```mermaid
flowchart LR
    U[Benutzer] --> H[KI-Host mit MCP-Client]
    H <-->|MCP über stdio| M[lmms-mcp Begleitprozess]
    M <-->|Lokales IPC mit Berechtigung| B[Native LMMS-Bridge]
    B --> Q[Begrenzte Command-Queue]
    Q --> F[ApplicationFacade im Qt-Hauptthread]
    F --> S[Song und PatternStore]
    F --> P[Tracks, Clips und Parameter]
    F --> J[ProjectJournal]
    F --> R[Snapshot- und Render-Jobs]
    S --> A[Bestehende AudioEngine]
    P --> A
    R --> W[Separater LMMS-Renderprozess]
```

Der Begleitprozess ist der MCP-Server aus Sicht des Hosts. Die native Bridge ist die LMMS-interne Steuerungs- und Berechtigungsschicht. Ihr IPC-Protokoll ist ein eigener, versionierter Domain-Vertrag und wird nicht als neuer öffentlicher MCP-Transport beworben.

### 3.2 Verantwortlichkeiten

**LMMS/C++:** Modelle lesen, IDs auflösen, Rechte prüfen, Revisionskonflikte erkennen, Änderungen validieren und ausführen, Undo verwalten, Jobs und Artefakte registrieren. Alle Sicherheitsprüfungen, die Projektzustand oder Dateizugriff betreffen, sind hier verbindlich.

**Begleitprozess/TypeScript:** offizielles SDK, MCP-Discovery, Tool-Schemas, Ressourcen und optionale Prompts, stdio, Protokollversionen, Umwandlung der Domain-Ergebnisse in MCP-Ergebnisse. Er besitzt keine direkte Referenz auf LMMS-Objekte und bearbeitet keine geöffneten Projektdateien auf eigene Faust.

**KI-Host:** Modell, Unterhaltung, Auswahl der Tools und Darstellung der Ergebnisse. Der Benutzer wählt seinen Host und dessen Modell. Die Integration setzt keine bestimmte KI-Marke voraus.

### 3.3 Technologieentscheidung und Alternativen

| Variante | Vorteile | Aufwand/Risiko | Entscheidung |
| --- | --- | --- | --- |
| Native Bridge + offizielles TypeScript-SDK | Gute Qt-Anbindung; SDK übernimmt Protokolländerungen; Hostprozess und Audio sind getrennt. | Zusätzliche Node-Laufzeit und Begleitprozess müssen reproduzierbar paketiert werden. | Empfohlen. |
| MCP vollständig in C++/Qt + nativer stdio-Proxy | Einheitlicher nativer Stack; keine Node-Laufzeit. | Eigenpflege des Protokolls oder zusätzliche SDK-Auswahl. Das offizielle SDK-Verzeichnis führt derzeit kein C++-SDK auf. | Alternative, falls das Projekt eine zusätzliche Laufzeit ablehnt. |
| LMMS selbst als stdio-Server | Ein einzelner Prozess. | Bereits vorhandene stdout-Ausgaben, GUI-Subsystem unter Windows, schwierige Anbindung an eine bereits laufende Instanz. | Für dieses Vorhaben nicht als Standard verwenden. |
| Nur externe `.mmp`-/MIDI-Bearbeitung | Schnell für Offline-Experimente. | Laufende Sitzung, Undo, nicht gespeicherte Änderungen und Modellebensdauer bleiben ungelöst. | Kein Ersatz für die Integration. |
| Nur HTTP-Server im Programm | Direkt mit URL erreichbar. | Zusätzliche Netzwerk-, Authentifizierungs- und Host-Kompatibilitätsarbeit. | Optional nach lokaler Version. |

Die SDK-Auswahl stützt sich auf das [offizielle SDK-Verzeichnis](https://modelcontextprotocol.io/docs/2026-07-28/sdk). Das [TypeScript-SDK v2](https://ts.sdk.modelcontextprotocol.io/v2/) dokumentiert die stabile Linie für die aktuelle Protokollrevision. Exakte Paket- und Runtime-Versionen werden in Phase 0 geprüft und anschließend festgeschrieben.

<a id="protokoll"></a>
## 4. MCP-Versionen und Protokollumfang

### 4.1 Aktuellen und älteren Lebenszyklus ausdrücklich unterscheiden

Am Planungsdatum verweist die offizielle `latest`-Spezifikation auf **2026-07-28**. Diese Revision verwendet selbstbeschreibende Requests und `server/discover`; die ältere Revision **2025-11-25** verwendet `initialize` und `notifications/initialized`. Eine neue Implementierung darf nicht jeden Host mit demselben Handshake behandeln. Quelle: [Versionierung und Kompatibilität](https://modelcontextprotocol.io/specification/2026-07-28/basic/versioning).

| Protokollpfad | Verpflichtung im Begleitprozess |
| --- | --- |
| 2026-07-28 | SDK übernimmt `server/discover`, Versionsprüfung und die versionsspezifischen Metadaten pro Request. |
| 2025-11-25 | SDK beantwortet `initialize`, verarbeitet `notifications/initialized` und hält die vereinbarte Legacy-Verbindung korrekt. |
| Weitere ältere Versionen | Nur als unterstützt ausweisen, wenn mit dem festgeschriebenen SDK und einem Referenzclient getestet. |
| Unbekannte Version | Versionsgerechten Fehler liefern; keine erfolgreiche Unterstützung vortäuschen. |

Im TypeScript-SDK v2 kann `serveStdio` aktuelle und Legacy-Clients bedienen. Diese Konfiguration muss im Projekt explizit getestet werden. Domain-Zustand und Auftragsregister liegen außerhalb kurzlebiger SDK-Serverobjekte in der nativen Bridge. Quelle: [Legacy-Unterstützung des SDKs](https://ts.sdk.modelcontextprotocol.io/v2/serving/legacy-clients.html).

### 4.2 Transport und Nachrichten

- Hostseitiger Standard ist **stdio**. Es funktioniert ohne freizugebenden TCP-Port.
- stdout enthält ausschließlich MCP-Nachrichten. Diagnoseausgaben gehen nach stderr.
- Framing, UTF-8, IDs, Notifications und protokollspezifische Abbruchregeln übernimmt das SDK.
- Client-EOF beendet den Begleitprozess kontrolliert. Ein Verbindungsende beendet nicht die vom Benutzer gestartete LMMS-Anwendung.
- Die Verbindung zum laufenden LMMS wird nach dem Start beziehungsweise beim ersten Tool-Aufruf hergestellt. MCP-Discovery soll auch bei nicht erreichbarem LMMS eine verständliche Diagnose ermöglichen.

Diese Transportregeln folgen der [stdio-Spezifikation](https://modelcontextprotocol.io/specification/2026-07-28/basic/transports/stdio).

### 4.3 Zunächst angebotene MCP-Funktionen

R1 bietet Tools und lesbare Ressourcen. Die Tool-Liste ist pro Paketversion deterministisch; `listChanged` und Ressourcen-Abonnements werden zunächst nicht zugesagt. Berechtigungen und aktuelle Betriebszustände werden bei jedem Zugriff geprüft. Ein Renderjob oder ein Projektwechsel verändert nicht spontan den statischen Tool-Katalog.

`inputSchema` und `outputSchema` werden für jedes Tool definiert. Maschinenlesbare Ergebnisse erscheinen in `structuredContent`; zusätzlich gibt es eine kurze Textdarstellung. Das SDK erzeugt die äußere, versionsgerechte MCP-Antwort. Hinweise wie `readOnlyHint` und `destructiveHint` beschreiben das Verhalten, ersetzen aber keine Rechteprüfung. Quelle: [MCP Tools](https://modelcontextprotocol.io/specification/2026-07-28/server/tools).

Prompts werden nach R2 ergänzt. Sampling, Elicitation, experimentelle Tasks und MCP Apps sind keine Voraussetzung. Für lang laufende Vorgänge verwendet die erste Version eigene Domain-Jobs mit `jobId` und Status-Tool; diese sind ausdrücklich keine Zusage der MCP-Tasks-Erweiterung.

<a id="dateien"></a>
## 5. Komponenten und konkrete Dateien

### 5.1 Neue native Komponenten – vorgeschlagene Namen

Die Namen sind Arbeitsvorschläge, keine bereits vorhandenen APIs. Vor dem Anlegen die Namespace- und Headerkonventionen des Projekts prüfen.

| Vorgeschlagene Komponente | Aufgabe |
| --- | --- |
| `include/AiControlService.h`, `src/core/ai/AiControlService.cpp` | Lebenszyklus, Bereitschaft, Verbindung zwischen Bridge und ApplicationFacade. |
| `include/AiLocalServer.h`, `src/core/ai/AiLocalServer.cpp` | `QLocalServer`, IPC-Framing, Anmeldung, Limits, Verbindungsabbruch. |
| `include/AiApplicationFacade.h`, `src/core/ai/AiApplicationFacade.cpp` | Typisierte Anwendungsoperationen auf Song, Track, Clip, Mixer und Journal. |
| `include/AiCommandDispatcher.h`, `src/core/ai/AiCommandDispatcher.cpp` | Begrenzte Queue, Berechtigungen, Deadlines, Schreibserialisierung. |
| `include/AiObjectRegistry.h`, `src/core/ai/AiObjectRegistry.cpp` | Opaque Handles, Objekttypen, Generationen und Invalidierung. |
| `include/AiProjectRevision.h`, `src/core/ai/AiProjectRevision.cpp` | Revisionen und Beobachtung unterstützter Modelländerungen. |
| `include/AiChangePlan.h`, `src/core/ai/AiChangePlan.cpp` | Vorschau, normalisierte Änderungen, Plan-Hash, Ablaufzeit. |
| `include/AiEditTransaction.h`, `src/core/ai/AiEditTransaction.cpp` | Atomare begrenzte Modelländerungen und Journal-Gruppen. |
| `include/AiParameterRegistry.h`, `src/core/ai/AiParameterRegistry.cpp` | Ausdrücklich freigegebene Parameter und ihre Metadaten. |
| `include/AiFilePolicy.h`, `src/core/ai/AiFilePolicy.cpp` | Freigegebene Verzeichnisse, Asset- und Export-Handles, Pfadprüfung. |
| `include/AiJobManager.h`, `src/core/ai/AiJobManager.cpp` | Ladevorgänge, Renderprozesse, Auftragszustand und Abbruch. |
| `include/AiProjectSnapshot.h`, `src/core/ai/AiProjectSnapshot.cpp` | Konsistenter Projektstand und auflösbare Asset-Verweise für Rendering. |
| `src/gui/modals/AiConnectionDialog.cpp` und zugehöriger Header | Verbindungsfreigabe und Rechteprofil. |
| `src/gui/widgets/AiControlStatus.cpp` und zugehöriger Header | Sichtbarer Verbindungs- und Änderungsstatus. |

### 5.2 Begleitprozess und gemeinsame Verträge

```text
tools/mcp/
  package.json
  package-lock.json
  tsconfig.json
  src/index.ts
  src/server.ts
  src/bridge-client.ts
  src/tools/*.ts
  src/resources.ts
  src/prompts.ts
  src/errors.ts
  test/*.test.ts
  test/fixtures/*
  packaging/*
  README.md
doc/mcp/
  protocol.md
  host-configuration.md
  security.md
  schemas/*.schema.json
tests/src/ai/
  AiObjectRegistryTest.cpp
  AiProjectRevisionTest.cpp
  AiCommandDispatcherTest.cpp
  AiEditTransactionTest.cpp
  AiFilePolicyTest.cpp
  AiJobManagerTest.cpp
  AiDomainContractTest.cpp
tests/fixtures/mcp/
  minimal.mmp
  melody.mmp
  patterns.mmp
  automation.mmp
```

JSON-Schemas für IPC und Domain-Daten sind die gemeinsame Vertragsgrundlage. TypeScript validiert vor dem Versand, C++ validiert vor jeder Ausführung erneut. Ein gemeinsam verwendeter Fixture-Korpus verhindert, dass beide Implementierungen unter identischen Feldnamen verschiedene Werte akzeptieren. Generierten Code nur einsetzen, wenn Wartungsaufwand und Buildintegration im Spike vertretbar sind.

### 5.3 Bestehende Dateien, die voraussichtlich angepasst werden

| Datei/Bereich | Geplante Änderung |
| --- | --- |
| `CMakeLists.txt` | `WANT_MCP` standardmäßig `OFF`; `Qt${QT_VERSION_MAJOR}::Network` nur bei aktiviertem Feature suchen. |
| `src/CMakeLists.txt`, `src/core/CMakeLists.txt`, `src/gui/CMakeLists.txt` | Native Dateien und Header bedingt registrieren; vorhandenes `lmmsobjs`-Modell beachten. |
| `src/lmmsconfig.h.in` | Bedingtes Feature-Makro entsprechend bestehender Konventionen. |
| `src/core/main.cpp`, `src/gui/GuiApplication.cpp`, `src/core/Engine.cpp` | Start/Abbau des Dienstes, Renderworker-Abgrenzung, geplante CLI-Schalter. |
| `Song`, `TrackContainer`, `Track`, `Clip`, `MidiClip`, `AutomatableModel`, `Mixer` | Erforderliche Änderungsereignisse und sichere Batch-Operationen; keine Protokolldetails in Audioklassen. |
| `ProjectJournal`, `JournallingObject` | Gruppierte Checkpoints und definierte Restore-Ereignisse. |
| `DataFile`, `Song` | Strukturierte Fehlerbehandlung für automatisierte Dateioperationen ohne modale Dialoge. |
| `RenderManager`, `ProjectRenderer`, Render-CLI | Eindeutiger Erfolgs-/Fehlerstatus und maschinenlesbarer Ergebnisbericht. |
| `ConfigManager`, `SetupDialog`, `MainWindow` | Rechteprofile, Verbindungsanzeige und Abschalten des Dienstes. |
| `tests/CMakeLists.txt` | Native Tests in die vorhandene QtTest-/CTest-Struktur aufnehmen. |
| `cmake/install`, plattformspezifische Paketierung, CI | Begleitprozess, optionale Runtime, Launcher und Qt Network installieren und prüfen. |

Node ist für einen normalen Build ohne `WANT_MCP` keine neue Voraussetzung. Für aktivierte Release-Pakete werden SDK, Abhängigkeiten und eine unterstützte Node-LTS-Laufzeit mit exakten Versionen, Prüfsummen und Lizenzdateien festgeschrieben. Endanwender sollen weder npm ausführen noch Pakete beim ersten Start herunterladen müssen.

<a id="lebenszyklus"></a>
## 6. Start, Verbindung und Lebenszyklus

### 6.1 Aktivierung

1. Benutzer aktiviert in LMMS „KI-Steuerung über MCP“ und wählt ein Rechteprofil.
2. Der Dienst erzeugt eine zufällige `instanceId`, einen lokalen Endpoint und eine eingeschränkte Verbindungsfreigabe.
3. LMMS erzeugt eine geschützte Verbindungsdatei für den Host. Der Benutzer kann die passende Startkonfiguration kopieren.
4. Der Host startet `lmms-mcp` über stdio.
5. Der Begleitprozess liest die gewählte Verbindungsdatei, authentifiziert sich an genau dieser LMMS-Instanz und prüft die IPC-Version.
6. Leseoperationen werden freigegeben, sobald der Dienst `ready` meldet. Schreiboperationen benötigen zusätzlich einen vollständig aufgebauten und bearbeitbaren Projektzustand.

Entwicklungsmodus: Host startet eine installierte Node-Laufzeit mit dem gebauten TypeScript-Einstiegspunkt. Release-Modus: `lmms-mcp.exe` beziehungsweise `lmms-mcp` startet die mitgelieferte Runtime. Der Windows-Launcher ist ein Konsolenprogramm ohne sichtbares Zusatzfenster und leitet stdin, stdout, stderr, EOF und Exitstatus korrekt weiter.

### 6.2 Lokales IPC

`QLocalServer`/`QLocalSocket` eignen sich für Unix-Sockets und Windows Named Pipes. Die Gegenstelle nutzt `node:net`. Die tatsächliche Endpoint-Adresse wird aus der LMMS-Laufzeit exportiert; der Begleitprozess errät keine Plattformpräfixe. Windows-/Unix-Interoperabilität und Zugriffsrechte sind ein ausdrückliches Phase-0-Gate. Grundlagen: [Qt QLocalServer](https://doc.qt.io/archives/qt-5.15/qlocalserver.html), [Node net](https://nodejs.org/api/net.html).

Für das interne Protokoll vorsehen:

- UTF-8-JSON mit einem 32-Bit-Längenpräfix in festgelegter Byte-Reihenfolge; maximale Framegröße vor der Allokation prüfen.
- Getrennter IPC-Handshake mit `bridgeProtocolVersion`, Paketversion, `instanceId` und Freigabenachweis. Er ist unabhängig vom MCP-Lebenszyklus.
- Request-ID, Operation, typisierte Argumente, Berechtigungskontext und Deadline.
- Ausschließlich freigegebene Domain-Operationen; keine dynamische Qt-Metacall-Schnittstelle.
- Heartbeat beziehungsweise liveness-Prüfung und begrenzte Schreibpuffer.
- Versionierte, eindeutige Fehlerantworten statt stillschweigender Feldinterpretation.

### 6.3 Mehrere Instanzen und Verbindungen

Jede LMMS-Instanz hat einen eigenen Endpoint und eine eigene Freigabe. Eine Verbindungsdatei benennt genau eine Instanz. Bei mehreren Kandidaten wird keine beliebige „erste“ Sitzung automatisch ausgewählt. Ein bequemes `--list-instances` darf nur erlaubte Metadaten zeigen; ein `--instance` muss eine ausdrücklich gewählte Instanz adressieren.

R1 erlaubt mehrere Leser. R2 erlaubt höchstens eine aktive Schreibfreigabe pro Instanz; alle Schreibaufträge durchlaufen dieselbe Queue. Benutzeraktionen in der LMMS-Oberfläche bleiben über Revisionen berücksichtigt. Eine zweite KI-Schreibverbindung erhält `WRITER_BUSY`.

### 6.4 Geordneter Abbau

Dienst vor `Engine::destroy()` in `stopping` versetzen, Annahme neuer Aufträge stoppen, ausstehende Aufträge vor ihrem Commit abbrechen und Worker kontrolliert beenden. Endpoint und Objektregistry schließen, bevor Song oder Mixer zerstört werden. Verbindungen müssen auf „LMMS wird beendet“ reagieren können, ohne bereits gelöschte Modelle anzufassen.

Projektwechsel invalidiert projektbezogene Handles und Pläne. Verbindungsverlust zum Host widerruft dessen temporäre Schreibreservierung. Bereits abgeschlossene Modelländerungen bleiben im Journal; das Trennen ist kein automatisches Undo.

<a id="threads"></a>
## 7. Threads und Echtzeitschutz

### 7.1 Verbindliche Threadregeln

1. JSON, MCP, IPC, Dateiprüfungen und große Serialisierungen laufen außerhalb der Audio-Callbacks.
2. Die ApplicationFacade lebt im Qt-Hauptthread. Nur sie löst Modell-Handles auf und führt projektbezogene Operationen aus.
3. IPC-I/O läuft auf einem eigenen Qt-Thread mit Eventloop. Übergabe per expliziter Queued Connection; Antworten ebenfalls asynchron.
4. Zwischen Threads werden unveränderliche Wertobjekte oder eindeutig übergebene Datenpakete transportiert, keine ungeschützten `Track*`, `Clip*` oder `Note*`.
5. Audiothreads warten niemals auf eine MCP-Antwort oder den IPC-Thread.
6. Keine `BlockingQueuedConnection` für diese Schnittstelle. GUI-/Audio-/IPC-Locks dürfen keinen zyklischen Wartegraphen bilden.
7. Ein nach Timeout noch wartender Queue-Auftrag darf später nicht unbemerkt ausgeführt werden.

Die Wahl des Hauptthreads orientiert sich an der Qt-Objektzugehörigkeit und den existierenden GUI-/Modellpfaden. Quelle: [QObject und Thread-Zugehörigkeit](https://doc.qt.io/archives/qt-5.15/qobject.html). Sie ersetzt nicht die zusätzlichen Audio-Locks von LMMS.

### 7.2 Ablauf eines Schreibauftrags

```text
Host → SDK validiert Schema → IPC prüft Frame und Freigabe
  → Dispatcher legt begrenzten Auftrag an
  → Hauptthread prüft Deadline, Rechte, Projekt-ID und Revision
  → Facade erstellt vollständig validierten Änderungsplan
  → kurze Synchronisierung mit Audio nach festgelegter Lock-Reihenfolge
  → begrenzter Commit + Journalgruppe + Revision
  → Locks freigeben → UI-Signale → Wertobjekt als Antwort
```

`requestChangesGuard()` ist ein vorhandener RAII-Baustein. Vor Verwendung pro Operation untersuchen, welche Locks bestehende Methoden selbst nehmen und welche Signale sie synchron auslösen. Ein großer äußerer Guard um beliebige LMMS-Aufrufe ist keine akzeptable Standardlösung.

### 7.3 Betriebszustände und große Änderungen

R2 führt strukturelle Änderungen und umfangreiche Notenänderungen nur bei gestoppter Wiedergabe aus. Bei laufender Wiedergabe kommen `PLAYBACK_ACTIVE` und ein verständlicher Hinweis zurück; der Server stoppt die Musik nicht ungefragt. Transport und einzeln freigegebene Echtzeitparameter dürfen nach eigener Messung auch während der Wiedergabe bedient werden.

Große Notenlisten werden vor dem Commit in vorbereitete Datenstrukturen überführt. Das wiederholte Aufrufen von `addNote` mit Sortierung, Längenberechnung und UI-Signalen für jede einzelne Note kann teuer werden. Für Batches einen begrenzten Modellpfad vorsehen, der Eigentum, Sortierung, Clip-Länge, Typwechsel und Signale gemeinsam korrekt behandelt.

Keine Dateizugriffe oder Plugin-Initialisierungen unter einer allgemeinen Audio-Änderungssperre ergänzen. Bestehende teure Pfade müssen separat bewertet werden. Im ersten Release kleine Operationen zulassen und größere Anfragen mit einer expliziten Grenze ablehnen, statt einen scheinbar atomaren, minutenlangen GUI-Block zu erzeugen.

<a id="revisionen"></a>
## 8. IDs, Revisionen und konkurrierende Änderungen

### 8.1 Externe Identität

| Feld | Bedeutung und Lebensdauer |
| --- | --- |
| `instanceId` | Identifiziert genau den laufenden LMMS-Prozess; wechselt beim Neustart. |
| `projectId` | Laufzeit-ID des geöffneten Dokuments; wechselt bei Neu, Öffnen, Recovery oder vollständigem Austausch. |
| `projectRevision` | Monotoner Zähler für relevante persistierbare Bearbeitungen einschließlich GUI, Undo und Redo. |
| `objectId` | Opaques, typisiertes Handle auf Spur, Clip oder Parameter innerhalb einer Objektgeneration. |
| `clipRevision` | Revision einer Notenliste; bindet kurzlebige Notenreferenzen an einen konkreten Clipzustand. |
| `transportSequence` | Gesonderte Sequenz für Laufzeit-/Wiedergabestatus; tickt nicht die Projektbearbeitungsrevision hoch. |
| `operationKey` | Vom Client gewählter Wiederholungsschlüssel für eine logische schreibende Operation. |
| `jobId`, `artifactId` | Vom Dienst ausgestellte Handles für Aufträge und überprüfte Ausgabedateien. |

Namen und Listenindizes dienen der Anzeige, nicht der Identität. Journal-IDs dürfen intern beim Auflösen helfen, sind aber ohne Generation und Lebensdauerprüfung kein öffentliches Handle. Opaque IDs sind keine Berechtigung und müssen mit dem Freigabekontext geprüft werden.

Für `QObject`-Modelle eine validierte Registry mit `QPointer` beziehungsweise passenden Destroy-Signalen verwenden. Für MixerChannel und Note, die nicht entsprechend als QObject verfügbar sind, kontrollierte Zuordnung und Invalidierung an den besitzenden Strukturen verwenden. Niemals eine Speicheradresse als Handle ausgeben.

### 8.2 Notenreferenzen

Eine Notenreferenz gilt für `(clipId, clipRevision, noteRef)`. Sobald die Notenliste geändert, geladen oder durch Undo wiederhergestellt wird, sind ältere Referenzen ungültig. Der nächste Read liefert neue Referenzen. Dadurch müssen im ersten Release keine dauerhaften Noten-UUIDs in das `.mmp`-Format eingeführt werden.

Noten mit identischer Tonhöhe, Position und Länge bleiben unterscheidbar. Eine Suche nur nach musikalischen Werten wäre dafür ungeeignet. Batch-Aufträge können lokale Referenzen auf gerade angelegte Clips/Noten verwenden; diese sind nur innerhalb des Plans gültig.

### 8.3 Vollständige Änderungserkennung

`Song::modified()` reicht nicht: Nach der ersten Bearbeitung ist das Projekt bereits dirty, weitere Bearbeitungen erzeugen deshalb kein neues solches Signal. Eine Revision muss an tatsächlichen Modellmutationen beziehungsweise bestätigten Bearbeitungsgrenzen hängen.

Arbeitspakete:

- Änderungen an TrackContainer, Track, Clip, Notenbestand, Kernparametern, Mixer und Journal erfassen.
- Bestehende Signalwege prüfen; fehlende Ereignisse gezielt ergänzen. Eine bloße Verbindung mit `Song::dataChanged()` garantiert keine vollständige Abdeckung.
- Unterstützte Piano-Roll- und sonstige GUI-Bearbeitungswege einbeziehen. UI-Änderungen an einer einzelnen `Note` dürfen nicht unbemerkt bleiben.
- MIDI-/Audio-Automation während der Wiedergabe von persistierbaren Benutzerbearbeitungen unterscheiden; dafür Änderungsursache beziehungsweise passende Bearbeitungsgrenzen einführen.
- Während eines eigenen Commits Ereignisse zusammenfassen: eine erfolgreiche Transaktion erhöht die Revision genau einmal. No-op erhöht sie nicht.
- Nach Restore von Objekten Registry neu abgleichen; bei unsicherer Identitätszuordnung die Objektgeneration konservativ invalidieren.

Als zusätzliche Prüfung kann ein Plan einen Fingerprint seiner betroffenen Modellbereiche erhalten. Dieser wird vor dem Commit erneut geprüft. Das ersetzt nicht die Revisionsabdeckung. Für zunächst nicht zuverlässig beobachtbare Bereiche Schreibzugriff gesperrt lassen.

### 8.4 Konfliktverhalten

Jede Vorschau bindet sich an `projectId` und `projectRevision`. Hat der Benutzer danach etwas Relevantes geändert, wird der Commit mit `REVISION_CONFLICT` abgelehnt und der aktuelle Stand zurückgegeben. Kein automatisches Überschreiben einer neueren Bearbeitung.

Undo/Redo führen zu einer neuen Revision, niemals zurück zu einer alten Revisionsnummer. Eine wiederhergestellte Spur kann ein neues externes Handle erhalten. Diese Semantik muss der Host kennen und vor Folgeoperationen erneut lesen.

<a id="daten"></a>
## 9. Musikalischer Datenvertrag

### 9.1 Zeit, Tonhöhe und Werte

- Kanonische Zeitangabe: ganzzahlige LMMS-Ticks. Clippositionen sind song- beziehungsweise containerbezogen; Notenpositionen sind cliprelativ.
- `ticksPerQuarter` ist im untersuchten Stand 48. `ticksPerBar` wird aus der aktuellen Taktart ermittelt. 192 ist kein universeller Taktwert für jede Taktart.
- Benutzeroberflächen dürfen Takt/Schlag anzeigen; die API benennt ausdrücklich, ob eine Anzeige 1-basiert ist. Der maschinenlesbare Zeitursprung ist Tick 0.
- Tonhöhe: `midiKey` von 0 bis 127. Im untersuchten `Note.h` beginnt die Oktavzählung bei C−1; MIDI 60 ist damit C4. Ein optionaler Notenname ist Anzeigeinformation.
- Notenlautstärke: `volumePercent` entsprechend LMMS 0–200, Standard 100. Sie wird nicht als MIDI-Velocity 0–127 bezeichnet.
- Notenpanorama: `pan` von −100 bis 100.
- Tempo: derzeit ganzzahlig, `MinTempo = 10`, `MaxTempo = 999`. Tatsächliche Grenzen über Metadaten zurückgeben und im nativen Modell prüfen.
- Sonstige Parameter tragen Datentyp, Einheit, Minimum, Maximum, Schrittweite und Skalentyp. Mixer-Gain, Prozent und dB nicht miteinander verwechseln.
- Step-Noten und reguläre Melodienoten erhalten ein ausdrückliches `noteType`. R2-Schreibzugriff umfasst zunächst reguläre Noten; Step-Operationen folgen mit dem Pattern-Paket.
- Alle Zahlen auf Endlichkeit, exakten Typ und Überlauf prüfen. Addition von Position und Länge muss in den nativen Zeitbereich passen. Ungültige Werte werden abgelehnt statt still geklemmt.

### 9.2 Minimale DTOs

`ProjectSummary`: Projekt-ID, Revision, Anzeige-/Dateiname nach Freigabe, Dirty-Status, Tempo, Taktart, Tickauflösung, Länge, verfügbare Funktionen, Lade-/Speicher-/Exportzustand.

`TrackSummary`: ID, Container (`song` oder `pattern_store`), Typ, Name, Reihenfolge, Mute/Solo, Instrument-Schlüssel soweit vorhanden, Mixerzuweisung, Clipanzahl.

`ClipSummary`: ID, Track-ID, konkreter Typ, Position, Länge, Name, Mute, automatische Größenanpassung und gegebenenfalls Patternbezug.

`NoteData`: kurzlebige Notenreferenz, Typ, Tickposition, Ticklänge, MIDI-Taste, Lautstärke, Panorama und Hinweis auf vorhandenes Detuning. Änderungen dürfen vorhandene Detuning-/Mikrotuningdaten nicht unbemerkt entfernen.

`ParameterInfo`: ID, Besitzer-ID, stabiler unterstützter Parameter-Schlüssel, Anzeigename, Wert, Einheit, Grenzen, Skalierung, Schreibbarkeit und Automatisierbarkeit.

`MixerSummary`: Channel-Handles, Anzeigenamen, aktuelle Positionen, Volumen-/Mute-/Solo-Metadaten, Zuweisungen und gerichtete Sends. Der Masterkanal wird ausdrücklich gekennzeichnet.

### 9.3 Mengen und Seiten

Spuren, Clips, Noten, Parameter und Instrumente werden paginiert. Voreinstellung 100 Einträge, maximal 500 pro Seite. Cursor binden sich an eine Revision und Filter; bei geändertem Snapshot kommt `STALE_CURSOR`. Eine Übersicht enthält keine unbeschränkten Notenarrays oder vollständigen Plugin-Zustände.

Filter nach Container, Track-ID, Clip-ID und Tickbereich begrenzen Modellzugriffe und Kontextbedarf. Der Host soll gezielt lesen können, statt bei jedem Aufruf den gesamten Song übertragen zu müssen.

<a id="tools"></a>
## 10. Tools, Ressourcen und Prompts

Die Namen sind vorgeschlagene öffentliche Tool-Namen. Ihre Schemas bleiben über beide MCP-Protokollpfade gleich; die SDK-Hülle ist versionsabhängig.

### 10.1 Lesende Tools

| Tool | Zentrale Argumente | Ergebnis | Ab |
| --- | --- | --- | --- |
| `lmms_get_status` | keine | Instanz, Bereitschaft, Versionen, Rechteprofil, Busy-Zustand und verfügbare Funktionen. | R0 |
| `lmms_get_project` | optionale `projectId` | Kompakte Projektübersicht mit Revision und Einheiten. | R1 |
| `lmms_list_tracks` | `container`, `cursor`, `limit` | Spuren einschließlich klarer Song-/PatternStore-Zuordnung. | R1 |
| `lmms_list_clips` | `trackId`, optional Tickbereich, Seitennavigation | Clips einer Spur. | R1 |
| `lmms_get_notes` | `clipId`, optional Tickbereich, Seitennavigation | Noten einschließlich `clipRevision` und kurzlebigen Referenzen. | R1 |
| `lmms_list_parameters` | `ownerId`, Seitennavigation | Unterstützte Parameter samt echten Wertebereichen. | R1 |
| `lmms_list_instruments` | Typfilter, Seitennavigation | Verfügbare interne Plugin-Schlüssel und Freigabestatus. | R1 |
| `lmms_get_mixer` | optionale Channel-Auswahl | Kanäle, Zuweisungen und Sends. | R1 |
| `lmms_get_operation` | `operationKey` | Status/Ergebnis eines möglicherweise bereits ausgeführten Schreibauftrags. | R2 |
| `lmms_get_job` | `jobId` | Zustand, Fortschritt, Fehler und Artefakte. | R3/R4 |

### 10.2 Schreibende und steuernde Tools

| Tool | Bedeutung | Ab |
| --- | --- | --- |
| `lmms_preview_changes` | Validiert einen begrenzten, typisierten Änderungssatz und erstellt einen gebundenen Plan. Keine Mutation. | R2 |
| `lmms_apply_changes` | Führt genau einen gültigen Plan mit `operationKey` atomar im unterstützten Modellumfang aus. | R2 |
| `lmms_set_transport` | Explizite Aktionen `play`, `pause`, `stop`, `seek`; kein mehrdeutiges Toggle. | R2 |
| `lmms_undo` | Macht eine Journalgruppe rückgängig; erwartet aktuelle Revision. | R2 |
| `lmms_redo` | Wiederholt eine rückgängig gemachte Journalgruppe; erwartet aktuelle Revision. | R2 |
| `lmms_create_instrument_track` | Legt eine interne Instrumentenspur über einen geprüften Plugin-Schlüssel an; eigene Operation außerhalb allgemeiner Batches. | R3 |
| `lmms_load_asset` | Lädt ein freigegebenes Sample oder Preset über einen Asset-Katalogeintrag. | R3 |
| `lmms_save_project_copy` | Speichert eine revisionsgebundene Kopie in einem freigegebenen Exportverzeichnis. | R3 |
| `lmms_start_render` | Startet einen Snapshot-Renderjob mit festgelegtem Ausgabeformat. | R4 |
| `lmms_cancel_job` | Fordert den Abbruch eines eigenen aktiven Jobs an. | R3/R4 |
| `lmms_open_project` | Kontrollierter Dokumentwechsel über einen freigegebenen Projekteingang; eigener Zustandsautomat. | R5 |

`lmms_apply_changes` ist kein allgemeiner Code-Ausführer. Der Inhalt ist eine geschlossene `oneOf`-Liste fachlicher Befehle mit vollständigen Schemas.

### 10.3 Geplante Befehle im Änderungssatz

| Befehl | Voraussetzungen und Wirkung | Ab |
| --- | --- | --- |
| `set_tempo` | Unterstützter Integerbereich; Tempoautomation bei Konflikt kenntlich machen. | R2 |
| `rename_track`, `set_track_mute` | Existierende Spur; expliziter Zielwert. | R2 |
| `set_parameter` | Nur registrierte, beschreibbare Kernparameter. | R2 |
| `create_midi_clip` | Existierende Instrumentenspur; gültige Position/Länge; lokaler `clientRef` für Folgebefehle. | R2 |
| `add_notes` | Reguläre Noten auf existierendem oder im selben Plan angelegtem Clip. | R2 |
| `update_notes`, `remove_notes` | Gültige cliprevisionsgebundene Referenzen; vorhandene Zusatzdaten erhalten. | R2 |
| `transpose_notes` | Explizite Auswahl und Halbtonzahl; alle Resultate vorab auf 0–127 prüfen. | R2 |
| `move_clip`, `resize_clip`, `duplicate_clip` | Länge und automatische Größenanpassung berücksichtigen; keine stille Notenkürzung. | R3 |
| `set_pattern_steps`, `assign_pattern` | PatternStore-/PatternTrack-Semantik und explizite Pattern-ID. | R3 |
| `set_automation_points` | Registrierter automatisierbarer Parameter; definierte Interpolation. | R3 |
| `create_mixer_channel`, `set_mixer_send`, `assign_mixer_channel` | Vollständig validiertes finales Routing ohne Zyklen. | R3 |
| `delete_clip`, `delete_track` | Vorschau mit betroffenen Kindern/Verbindungen und getesteter Wiederherstellung. | R3 |

Wenn ein Paket einen Befehl noch nicht unterstützt, wird er nicht im akzeptierten Schema angeboten. Ein vermeintlich erfolgreicher No-op ist kein zulässiger Ersatz.

### 10.4 Ressourcen

Vorgeschlagene URIs:

- `lmms://instances/{instanceId}/projects/{projectId}/summary`
- `lmms://instances/{instanceId}/projects/{projectId}/tracks/{trackId}`
- `lmms://instances/{instanceId}/projects/{projectId}/clips/{clipId}/summary`
- `lmms://instances/{instanceId}/capabilities`
- `lmms://instances/{instanceId}/artifacts/{artifactId}`

Ressourcen verwenden dieselbe Facade und Rechteprüfung wie die Tools. Sie liefern kompakte JSON-/Textdaten mit Revision. Noten-Massenabfragen bleiben paginierte Tools. Ressourcen-Templates werden dokumentiert; eine URI ist kein beliebiger Dateipfad. Audioartefakte werden erst nach erfolgreicher Prüfung und mit Größenlimit lesbar. Grundlage: [MCP Resources](https://modelcontextprotocol.io/specification/2026-07-28/server/resources).

Für Binärartefakte einen gesonderten internen Lesepfad über `artifactId` vorsehen: höchstens 256 KiB Rohdaten pro IPC-Chunk, geprüftes Offset und Gesamtlänge, unveränderliches Artefakt und dieselbe Freigabe bei jedem Zugriff. Der Begleitprozess darf bis zum zulässigen Gesamtlimit zusammensetzen. So bleibt jeder base64-kodierte IPC-Frame unter 1 MiB. Das tatsächliche SDK-/Hostlimit kann den maximalen MCP-Ressourceninhalt weiter senken; größere Exporte bleiben über Artefaktmetadaten und den lokalen Exportort erreichbar.

### 10.5 Optionale Prompts

Nach R2 können `analyze_project`, `compose_midi_clip` und `review_mix` als Prompt-Vorlagen angeboten werden. Sie beschreiben einen Arbeitsablauf aus Lesen, Vorschau, Anwenden und Überprüfen. Ein Prompt erweitert keine Berechtigung und darf keinen bereits autorisierten Schritt durch eine bloße Behauptung ersetzen.

<a id="transaktionen"></a>
## 11. Vorschau, Transaktionen und Undo

### 11.1 Vorschau als konkretes Änderungsobjekt

`lmms_preview_changes` normalisiert den vollständigen Änderungssatz und liefert:

- `planId`, `projectId`, Ausgangsrevision, Ablaufzeit und Hash der normalisierten Befehle.
- Fachliche Differenz: beispielsweise „1 Clip anlegen, 8 Noten hinzufügen, Tempo 140 → 120“.
- Betroffene Objekte, erzeugte lokale Referenzen, Konflikte, benötigte Rechte und erwarteter Undo-Umfang.
- Tatsächlich zu verwendende musikalische Einheiten und gegebenenfalls begrenzende Betriebszustände.

Pläne sind standardmäßig fünf Minuten gültig und an die Instanz, Projektgeneration, Revision und Verbindungsfreigabe gebunden. Der Client darf beim Commit keine abweichenden Befehle unter derselben `planId` nachreichen.

Vorschau ist keine erneute Nachfrage für jede Routineänderung. Innerhalb einer vom Benutzer eingerichteten Bearbeitungsfreigabe darf der Host einen gültigen Plan direkt ausführen. Für besondere Rechte, etwa ein ungesichertes Dokument zu ersetzen, muss eine tatsächliche Benutzerfreigabe vorliegen. Ein vom Modell gesetztes `approved: true` zählt nicht.

### 11.2 Atomarität des unterstützten Modellumfangs

1. Vollständige Schema- und Mengenprüfung vor der Mutation.
2. Rechte, IDs, Typen, Revision und Betriebszustand im Hauptthread erneut prüfen.
3. Abhängigkeiten des Batches auflösen; der gesamte resultierende Zustand muss gültig sein.
4. Änderungen, Speicherbedarf und Wiederherstellungsdaten vorbereiten.
5. Zusammenhängenden, kurzen Commit ohne verschachtelte Eventloops ausführen.
6. Genau eine Journalgruppe und eine neue Revision erzeugen.
7. Ergebnisobjekte und Handles aus dem tatsächlich entstandenen Modell zurücklesen.

Fehler vor dem Commit verändern weder Projekt noch Undo-/Redo-Stacks. Ein Fehler während des unterstützten Commits stellt den vorherigen Zustand einschließlich Journalzustand wieder her. Ein teilweise ausgeführter Batch darf nicht als Erfolg zurückkehren.

Dateisystemoperationen, Plugininitialisierung und Rendering sind keine Bestandteile dieser allgemeinen Modelltransaktion. Sie erhalten eigene Operationen mit explizitem Status. Eine fehlgeschlagene Plugininitialisierung oder ein Prozessabsturz lässt sich nicht durch das Versprechen einer allgemeinen Undo-Transaktion absichern.

### 11.3 ProjectJournal erweitern

Das vorhandene Journal speichert einzelne `CheckPoint`-Einträge. Für „ein KI-Auftrag = ein Undo“ ist eine Erweiterung nötig:

- Einen `JournalEntry` einführen, der einen einzelnen Checkpoint oder eine zusammengehörige Gruppe enthält; alte GUI-Operationen bleiben als Einzelgruppe darstellbar.
- Vorher-Zustände an besitzenden, wiederherstellbaren Objekten erfassen: beispielsweise `MidiClip` für Noten und TrackContainer für erzeugte/gelöschte Spuren.
- Überlappende Eltern-/Kind-Snapshots normalisieren, damit Restore keine doppelten oder widersprüchlichen Zustände erzeugt.
- Tempo, Mixer, Song-Tracks, PatternStore und Controller nicht fälschlich als automatisch vom selben Song-Snapshot abgedeckt betrachten.
- Für Undo zuerst die aktuellen Zustände der ganzen Gruppe für Redo sichern; anschließend in getesteter Abhängigkeitsreihenfolge restaurieren.
- Journalzustand per RAII erhalten; unterdrückte innere Checkpoints dürfen keine globalen Nebenwirkungen auf andere Bearbeitungen haben.
- Automationsverbindungen nach Restore mit den vorhandenen Mechanismen, unter anderem `AutomationClip::resolveAllIDs`, korrekt wiederherstellen.
- Registry-Invalidierung und Revisionsereignis nach abgeschlossener Gruppenwiederherstellung auslösen.
- Speicherlimit zusätzlich zur bestehenden Anzahlgrenze von 100 Undo-Zuständen vorsehen; eine Gruppe mit großem Clip darf nicht unbegrenzt wachsen.

Vorhandene Undo-/Redo-Tests ergänzen, bevor mehr als eine unabhängige Modelländerung pro MCP-Auftrag freigegeben wird. Falls die Journalerweiterung noch nicht bereit ist, bleibt R1 die auslieferbare Stufe; Mehrfachänderungen dürfen nicht bereits als atomar beworben werden.

Undo-/Redo-Tools binden sich zusätzlich an den erwarteten Kopf der betreffenden Historie, beispielsweise über `expectedHistoryHead` und die zurückgegebene `undoGroupId`. Liegt inzwischen eine manuelle Bearbeitung darüber, wird ein gezieltes Rückgängigmachen der älteren KI-Gruppe mit `HISTORY_CONFLICT` abgelehnt. Der Server läuft nicht selbstständig durch mehrere fremde Bearbeitungen, um eine bestimmte Gruppe zu erreichen.

### 11.4 Wiederholungen und unklare Ergebnisse

`operationKey` wird in LMMS vor dem Commit registriert. Derselbe Schlüssel mit denselben normalisierten Argumenten liefert denselben Zustand beziehungsweise dasselbe Ergebnis. Derselbe Schlüssel mit anderem Inhalt wird abgelehnt.

Das Ergebnisregister überlebt einen Neustart des Begleitprozesses, solange die LMMS-Instanz läuft. Vorgabe: maximal 1024 Einträge und bis zu 24 Stunden Aufbewahrung innerhalb der Instanz, mit dokumentierter Verdrängungsregel. Bei verlorener Antwort zuerst `lmms_get_operation` abfragen. Nach einem LMMS-Absturz gibt es keine zugesicherte prozessübergreifende Exactly-once-Semantik; Projekt und Recovery müssen neu gelesen werden.

Schlüssel an Instanz und Freigabekontext binden. Aktive Aufträge niemals verdrängen; die garantierte Aufbewahrung bestätigter Schlüssel im Status veröffentlichen. Ist das Register voll, neue Schreibaufträge ablehnen oder nur bereits abgelaufene Einträge entfernen. Ein nach Ablauf unbekannter Schlüssel ist kein Nachweis, dass seine Operation nie ausgeführt wurde: erst den Projektzustand abgleichen, bevor eine neue Operation geplant wird.

Ein Abbruch vor dem Commit verhindert die Änderung. Ab Commit-Beginn wird der kleine Commit vollständig beendet oder intern zurückgerollt. Falls das MCP-Protokoll nach einem Abbruch keine weitere Antwort erlaubt, bleibt der Ausgang über das Operationsregister ermittelbar.

<a id="beispiele"></a>
## 12. Beispiele für Aufrufe und Ergebnisse

Die Beispiele zeigen **Domain-Argumente beziehungsweise `structuredContent`**, keine vollständigen MCP-Wire-Nachrichten. Das SDK ergänzt die Hülle und erforderliche Metadaten der jeweiligen Protokollrevision. IDs sind Platzhalter.

### 12.1 Projekt lesen

Beispiel für den fachlichen Inhalt von `lmms_get_project`:

```json
{
  "ok": true,
  "instanceId": "inst_7f42",
  "projectId": "project_9a31",
  "projectRevision": 42,
  "data": {
    "name": "Neues Projekt",
    "modified": true,
    "tempoBpm": 140,
    "timeSignature": { "numerator": 4, "denominator": 4 },
    "ticksPerQuarter": 48,
    "ticksPerBar": 192,
    "state": "ready",
    "capabilities": {
      "editMidi": true,
      "createInternalInstrument": false,
      "renderSnapshot": false
    }
  }
}
```

### 12.2 Konkreten Plan erstellen

Argumente für `lmms_preview_changes`:

```json
{
  "projectId": "project_9a31",
  "expectedRevision": 42,
  "changes": [
    { "kind": "set_tempo", "bpm": 120 },
    {
      "kind": "create_midi_clip",
      "trackId": "track_bass",
      "clientRef": "new_bass_clip",
      "positionTicks": 0,
      "lengthTicks": 768
    },
    {
      "kind": "add_notes",
      "clip": { "clientRef": "new_bass_clip" },
      "notes": [
        { "positionTicks": 0, "lengthTicks": 48, "midiKey": 36, "volumePercent": 85, "pan": 0, "noteType": "regular" },
        { "positionTicks": 96, "lengthTicks": 48, "midiKey": 43, "volumePercent": 80, "pan": 0, "noteType": "regular" },
        { "positionTicks": 192, "lengthTicks": 48, "midiKey": 39, "volumePercent": 85, "pan": 0, "noteType": "regular" },
        { "positionTicks": 288, "lengthTicks": 48, "midiKey": 46, "volumePercent": 80, "pan": 0, "noteType": "regular" }
      ]
    }
  ]
}
```

`track_bass` muss aus einem vorherigen Read stammen und eine bestehende Instrumentenspur bezeichnen. `clientRef` referenziert ausschließlich Objekte innerhalb dieses Plans. Die Clip-Länge von 768 Ticks entspricht hier vier 4/4-Takten. Falls automatische Clip-Größenanpassung das Ziel verkürzen würde, muss der neue Modellpfad die ausdrücklich angefragte Mindestlänge erhalten oder die Anfrage klar ablehnen.

### 12.3 Anwenden

Argumente für `lmms_apply_changes`:

```json
{
  "projectId": "project_9a31",
  "expectedRevision": 42,
  "planId": "plan_41e8",
  "operationKey": "op_805d9e0e"
}
```

Fachliches Ergebnis:

```json
{
  "ok": true,
  "instanceId": "inst_7f42",
  "projectId": "project_9a31",
  "projectRevision": 43,
  "operationKey": "op_805d9e0e",
  "data": {
    "status": "committed",
    "createdObjects": { "new_bass_clip": "clip_52ac" },
    "addedNotes": 4,
    "undoGroupId": "undo_113a"
  }
}
```

### 12.4 Revisionskonflikt

```json
{
  "ok": false,
  "projectId": "project_9a31",
  "projectRevision": 44,
  "error": {
    "code": "REVISION_CONFLICT",
    "message": "Das Projekt wurde seit der Vorschau bearbeitet.",
    "retryable": false,
    "details": { "expectedRevision": 42, "actualRevision": 44 }
  }
}
```

Der Host liest den aktuellen Stand und erstellt bei Bedarf einen neuen Plan. Er wiederholt den veralteten Commit nicht blind.

<a id="funktionen"></a>
## 13. Spuren, Instrumente, Patterns, Mixer und Automation

### 13.1 Spuren und Clips

`Track::create` und die jeweiligen `createClip`-Funktionen verwenden. Konstruktoren können ein Objekt bereits in den Container eintragen; „zuerst unsichtbar anlegen und später einhängen“ ist daher ohne eigene Modellunterstützung keine sichere Annahme.

Erzeugen, Löschen, Duplizieren und Verschieben brauchen getrennte Prüfungen für Song und PatternStore. Versteckte Automation und nicht implementierte Tracktypen nicht über rohe Enum-Werte beschreibbar machen. Vor Löschung die betroffenen Clips, Parameterbindungen, MIDI-/Mixerzuweisungen und Journalbesitzer bestimmen.

### 13.2 Noten und musikalische Transformationen

Bei `MidiClip::addNote` Quantisierung ausdrücklich deaktivieren. Für Positions- und Tonhöhenänderungen Sortierung und Clip-Länge über den vorgesehenen Modellpfad aktualisieren. Copy-/Transpose-Operationen müssen Detuning und weitere vorhandene Notendaten erhalten. Eine vereinfachte JSON-Ansicht darf bei einer Transformation keine nicht dargestellten Daten löschen.

Für Transposition gilt: Der ganze Satz wird abgelehnt, wenn eine Zielnote außerhalb des gültigen Bereichs liegt. Kein stilles Abschneiden an MIDI 0 oder 127. Quantisieren und Humanisieren später als eigene, deterministische Befehle mit Raster und Zufalls-Seed anbieten.

### 13.3 Instrumente und Presets

Katalog über PluginFactory ermitteln; einen vom Modell erfundenen Namen nicht ungeprüft an `loadInstrument` übergeben. Im ersten Umfang nur installierte, getestete interne Instrumente erlauben, etwa `tripleoscillator`, sofern im konkreten Build vorhanden.

Instrumentwechsel kann Pluginzustände und Parameter-IDs ersetzen und ist teurer als eine Notenänderung. Als eigene Operation mit Vorprüfung, reserviertem Zielobjekt und anschließender Registry-Aktualisierung ausführen. Das Anlegen einer neuen Spur darf nicht unbeabsichtigt den vom Benutzer gewählten Namen behalten oder überschreiben: `loadInstrument` setzt selbst einen Instrument-Anzeigenamen, deshalb den gewünschten Spurnamen danach gezielt anwenden.

Presets nur über katalogisierte IDs laden. XML und darin enthaltene Ressourcen-/Pluginverweise durch dieselben Schutzmechanismen wie lokale LMMS-Vorgänge prüfen. Drittanbieter-VST-/LV2-/LADSPA-Parameter werden erst über getestete Adapter freigegeben; der interne Modelbaum ist keine automatisch stabile öffentliche Plugin-API.

### 13.4 Parameter

Zunächst Tempo, Track-Mute, unterstützte Track-Lautstärke und Mixerzuweisung ausdrücklich registrieren. Für jede ID festlegen: Besitzer, fachlicher Schlüssel, Datentyp, Einheit, Wertebereich, Automatisierbarkeit und Lebensdauer.

Persistierbaren Parameterwert und aktuell durch Automation oder Controller erzeugten effektiven Wert unterscheiden. Eine KI darf eine aktive Automation nicht versehentlich dauerhaft ersetzen. Bei Konflikt entweder eine explizite Bearbeitungsart verlangen oder den Schreibzugriff mit `PARAMETER_CONTROLLED` ablehnen.

### 13.5 Patterns und Steps

PatternStore als eigene Tabelle verstehen: Spuren sind Zeilen, Patterns entsprechen den Clip-Spalten. PatternTracks im Song und ihre PatternClips sind die Arrangementreferenzen. Ein Befehl muss ausdrücklich sagen, ob er Patterninhalt, Patternzuweisung oder Arrangementposition ändert.

Pattern-IDs über die Registry verwalten und bei Reihenfolgeänderungen abgleichen. Löschen oder Tauschen eines Patterns muss alle Referenzen erhalten beziehungsweise gezielt aktualisieren. Step-Anzahl nicht als generell 16 voraussetzen; mit `TimePos::stepsPerBar()` und dem aktuellen Patternmodell arbeiten.

### 13.6 Automation

Mit registrierten Parameter-IDs und `AutomationClip::addObject` arbeiten, nicht mit beliebigen vom Host gelieferten Journal-IDs. Punkte sind cliprelativ; Anfangs-/Endwerte, diskrete und lineare Progression sowie eine später unterstützte Kurvenform werden ausdrücklich benannt.

Vor jeder Mutation vollständige Punktliste, Parametergrenzen und finale Bindungen prüfen. Mehrere Parameter in einer Automation nur zulassen, wenn deren Modellsemantik unterstützt und getestet ist. Nach Undo/Redo und Projektladen müssen Verbindungen auf gültige Modelle zeigen.

### 13.7 Mixer

Channel-Handles im Hauptthread auf aktuelle Indizes auflösen. `Mixer::isInfiniteLoop` und eine Prüfung des resultierenden Gesamtgraphen verwenden. Bei mehreren Send-Änderungen kann ein Zyklus erst durch die Kombination entstehen; Einzelprüfungen gegen den alten Graphen genügen dann nicht.

Masterkanal gegen Löschen schützen. Kanalverschiebungen dürfen Spurzuweisungen und Sends nicht auf den falschen Kanal umbiegen. Pegelanzeigen sind Laufzeitdaten und werden getrennt von persistierbaren Änderungen bereitgestellt; sie dürfen nicht pro Audioframe MCP-Nachrichten erzeugen.

<a id="dateienzugriff"></a>
## 14. Projektdateien, Assets und Speicheroperationen

### 14.1 Standard: Arbeitskopien und explizite Dateibereiche

R3 startet mit `lmms_save_project_copy`. Ziel ist ein zuvor vom Benutzer freigegebenes Exportverzeichnis plus sicherer Dateiname. Das Tool liefert ein Artefakt und die gespeicherte Revision. Es muss Dateiname und Dirty-Status der geöffneten Sitzung erhalten.

`Song::saveProjectFile` ist dafür der fachlich passendere Ausgangspunkt als `guiSaveProjectAs`, weil letztere den Dateinamen und Dirty-Zustand der Sitzung aktualisiert. Dennoch den bestehenden Serializer auf Seitenwirkungen und Fehlerdialoge prüfen; die Methode nicht ungeprüft aus einem Worker aufrufen.

Für automatisierte Aufrufe einen Dienstpfad in `Song`/`DataFile` schaffen, der strukturierte Fehler ohne modale Dialoge zurückgibt. Die interaktive GUI darf weiterhin ihre vorhandenen Dialoge anzeigen. Dateierweiterung, `.new`-/`.bak`-Verhalten und Bundle-Verzeichnisse aus `DataFile::writeFile` berücksichtigen: Die Dateipolicy muss alle tatsächlich erzeugten Dateien abdecken.

### 14.2 Dateipolicy

- Getrennte freigegebene Bereiche für Projekteingang, Sample-/Preset-Bibliotheken und Export.
- Vom Host bevorzugt Asset-/Root-IDs und relative Namen akzeptieren, nicht beliebige absolute Dateipfade.
- Kanonische Elternpfade prüfen, da eine neue Zieldatei selbst noch keinen kanonischen Pfad besitzen muss.
- `..`, unerlaubte absolute Pfade, UNC-/Netzwerkpfade außerhalb der Freigabe, Windows-Gerätenamen und Alternate Data Streams ablehnen.
- Symlinks, Windows-Junctions/Reparse-Points und den tatsächlichen Zielpfad berücksichtigen. Ein einfacher Stringpräfixvergleich reicht nicht.
- Rechte unmittelbar beim Öffnen beziehungsweise finalen Veröffentlichen prüfen; Plattformmechanismen gegen Pfadtausch verwenden, soweit verfügbar.
- Bestehende Ziele standardmäßig nicht ersetzen. Ein erlaubtes Überschreiben benötigt eine dafür gültige Freigabe und einen getesteten atomaren Veröffentlichungsweg.
- Keine willkürlichen Dateien über eine frei formulierbare Ressourcen-URI lesen.

### 14.3 Projektöffnen erst nach sauberem Ladevertrag

`Song::loadProject` liefert derzeit kein strukturiertes Ergebnis und kann Fehlerdialoge oder Folgezustände auslösen. Vor R5 einen `ProjectLoadResult` und eine garantierte Aufräumlogik für Lade-/Journalflags ergänzen. Fehler sollen weder eine dauerhaft „ladende“ Sitzung noch einen fälschlich erfolgreichen Projektwechsel hinterlassen.

Projektwechsel als Zustandsautomat behandeln: Eingang prüfen → eventuell erforderliche Sicherung/Freigabe → Wechsel reservieren → laden → Ergebnis prüfen → neue `projectId` veröffentlichen. Bei ungespeicherten Änderungen keine stillschweigende Verwerfung. Die bereits vorhandene Prüfung lokaler Plugin-Pfade und andere LMMS-Ladeprüfungen erhalten.

<a id="rendering"></a>
## 15. Rendering und asynchrone Jobs

### 15.1 Empfohlener Renderpfad: Snapshot und separater Prozess

Das aktive `Engine`-Modell ist global. Ein weiterer unabhängiger Song mit eigenem Mixer ist im selben Prozess nicht einfach vorauszusetzen. `RenderManager` verändert außerdem Audiogerät und beim Stem-Rendering Mute-Zustände. Deshalb soll R4 standardmäßig einen konsistenten Snapshot in einem separaten LMMS-Renderprozess verwenden.

1. Rechte, Ausgabeformat, Zielbereich und erwartete Projektrevision prüfen.
2. Snapshot der Sitzung mit klar definierter Revision aufnehmen. Die kurze Aufnahmephase gegen gleichzeitige Modellbearbeitung schützen.
3. Relative Sample-/Preset-Pfade für den Snapshot korrekt auflösen oder Ressourcen in einem isolierten Arbeitsverzeichnis bündeln. Snapshot nicht bloß an einen anderen Ort kopieren und auf identische Pfadauflösung hoffen.
4. Snapshot auf Disk schreiben und nötige Assets/Plugin-Schlüssel in einem Manifest festhalten.
5. Renderworker über `QProcess` mit Argumentliste starten. Vorhandene CLI als Basis: `lmms render <snapshot> --format wav --output <temp-output> --samplerate 44100`.
6. Worker mit dedizierter Konfiguration starten und KI-Bridge dort zwingend deaktivieren. Die entsprechenden Schalter/Ergebnisoptionen sind noch einzuführen.
7. Fortschritt, Prozessende und strukturierten Renderbericht auswerten.
8. Ausgabedatei öffnen und Header, Format, Samplerate, Kanäle, Dauer und lesbare Audiodaten prüfen.
9. Erst danach die Datei im freigegebenen Zielbereich atomar veröffentlichen und als `artifactId` registrieren.

Der Benutzer kann nach der Snapshot-Aufnahme weiterarbeiten. Das Ergebnis benennt die **gerenderte Revision** und wird nicht nachträglich als Abbild des inzwischen geänderten Liveprojekts ausgegeben. Aufnahme und Snapshot-Schreiben dürfen keinen Zugriff auf laufend veränderliche C++-Modelle in einem beliebigen Worker hinterlassen.

### 15.2 Render-Ergebnis muss belastbar sein

`RenderManager::finished()` bedeutet nur, dass die Verarbeitungskette beendet ist. In `RenderManager::render` führt auch ein fehlendes Dateigerät derzeit weiter zu `renderNextTrack`. Für automatisierte Verwendung deshalb ein strukturiertes `RenderOutcome` ergänzen: erfolgreich, abgebrochen, fehlgeschlagen; pro Ausgabedatei Pfad, Format und Fehler.

CLI-Exitstatus muss Fehler zuverlässig abbilden. Zusätzlich einen maschinenlesbaren Ergebnisbericht vorsehen, beispielsweise über eine neue `--render-report`-Option. Nicht die menschenlesbare Konsolenausgabe parsen und nicht allein auf Existenz einer Datei vertrauen.

Ein Headless-Worker kann sich bei einzelnen Plugins anders verhalten als die GUI-Sitzung. R4 unterstützt zunächst eine nachgewiesene Liste interner Instrumente. Fehlende Samples oder Plugins sind sichtbare Jobfehler beziehungsweise ausdrücklich freigegebene unvollständige Ergebnisse, niemals stiller Gesamterfolg.

### 15.3 Auftragsmodell

```text
queued → preparing_snapshot → running → validating → succeeded
              ↘ failed          ↘ failed        ↘ failed
queued/preparing/running → cancelling → cancelled
```

Vorgaben:

- `start_render` antwortet zügig mit `jobId`; der Host pollt `get_job`.
- Status enthält Fortschritt, Phase, Startzeit, Snapshotrevision, begrenzte Diagnose und terminales Ergebnis.
- Höchstens ein Renderjob pro Instanz gleichzeitig; weitere Jobs werden begrenzt angenommen oder mit `BUSY` abgelehnt.
- Abbruch beendet nur den zugehörigen Worker samt kontrolliertem Prozessbaum. Die Live-LMMS-Instanz bleibt unangetastet.
- Timeout, Hostdisconnect und expliziter Abbruch haben getrennte Semantik. R4-Vorgabe: Ein bereits gestarteter Renderjob läuft bei bloßem Hostdisconnect weiter; nach Reconnect ist er auffindbar.
- Temporäre Dateien gehören dem Job. Auf Fehler oder Abbruch nur diese Dateien bereinigen; fertige Benutzerartefakte nicht nachträglich löschen.
- Artefakte enthalten Größe, Hash, Format, Revision und Aufbewahrungsfrist. Automatisches Bereinigen beschränkt sich auf den eigenen temporären Bereich.

Ein späterer In-Process-Renderpfad kann für spezielle Plugins ergänzt werden. Er erfordert exklusive Bearbeitungssperre auch für die GUI und vollständige Wiederherstellung von Audiogerät, Transport, Mutes, Exportflags und Journalzustand auf jedem Fehlerpfad.

<a id="oberflaeche"></a>
## 16. Oberfläche und Einstellungen

In `SetupDialog` einen Bereich „KI-Steuerung / MCP“ ergänzen:

- Aktivieren/Deaktivieren des Dienstes; standardmäßig aus.
- Profil „Nur lesen“ oder „Aktuelles Projekt bearbeiten“.
- Freigegebene Asset- und Exportverzeichnisse auswählen.
- Host-Konfiguration anzeigen/kopieren; Verbindung neu ausstellen und widerrufen.
- Aktive Verbindung, Instanz und aktuelle Rechte anzeigen.

Im Hauptfenster eine kompakte Statusanzeige: „KI getrennt“, „KI liest“ oder „KI bearbeitet“. Bei einer Änderung eine kurze fachliche Zusammenfassung zeigen; technische Request-IDs gehören in die Diagnoseansicht. Für gewöhnliche freigegebene Bearbeitungen keine Folge modaler Rückfragen einbauen.

Ein gut erreichbarer Schalter beendet die KI-Steuerung und verwirft noch nicht ausgeführte Aufträge. Das Stoppen von Playback und der Abbruch eines Renderjobs bleiben getrennte sichtbare Aktionen. Die Undo-Historie soll KI-Änderungen durch einen verständlichen Titel erkennbar machen.

Einstellungen über die vorhandenen `ConfigManager`-Mechanismen verwalten. Geheimnisse und kurzlebige Endpoints getrennt von exportierbaren Projekt-/Konfigurationsdateien halten. Übersetzbare Texte in die bestehende Qt-Übersetzungsstruktur aufnehmen.

### 16.1 Beispiel einer generischen Host-Konfiguration

Diese Vorlage ist für Hosts gedacht, die ein `mcpServers`-Objekt unterstützen. Die genaue Eingabemaske beziehungsweise Konfigurationsdatei hängt vom Host ab; `LMMS`-Pfade und Benutzername werden bei der Einrichtung eingesetzt. Der Launcher und seine Schalter sind Teil der geplanten Umsetzung.

```json
{
  "mcpServers": {
    "lmms": {
      "command": "C:\\Program Files\\LMMS\\lmms-mcp.exe",
      "args": [
        "--connection-file",
        "C:\\Users\\BENUTZER\\AppData\\Local\\LMMS\\mcp\\connections\\studio.json"
      ]
    }
  }
}
```

Die Verbindungsdatei wird von LMMS erzeugt. Token nicht in das Beispiel, eine Projektdatei oder eine Versionsverwaltung kopieren. Ein sauber installiertes Paket muss auch mit Leerzeichen und nicht-ASCII-Zeichen in Installations- und Benutzerpfaden funktionieren.

<a id="sicherheit"></a>
## 17. Berechtigungen und Datenschutz

### 17.1 Rechteprofile

| Profil/Freigabe | Erlaubt |
| --- | --- |
| Nur lesen | Freigegebene Metadaten und musikalische Daten des aktuellen Projekts. |
| Aktuelles Projekt bearbeiten | R2-Modelländerungen, Transport und deren Undo/Redo innerhalb der gewählten Sitzung. |
| Bibliotheken verwenden | Zusätzlich gewählte Preset-/Samplebereiche; kein allgemeines Dateisystemrecht. |
| Kopien/Audio ausgeben | Erzeugen neuer Artefakte in expliziten Exportbereichen. |
| Dokument ersetzen/Datei überschreiben | Eigene, eng begrenzte Freigabe mit geprüften Konsequenzen. |

Die Rechteprüfung erfolgt in LMMS vor jedem Read und Commit. Auch ein veränderter Begleitprozess darf keine weitergehenden Befehle ausführen können. Schemafelder, Clientname und Beschreibungen verleihen keine Berechtigung.

### 17.2 Lokale Anmeldung

Endpoint nur für den aktuellen Benutzer zugänglich machen: Unix-Verzeichnis und Socket mit passenden Rechten; Windows Named Pipe mit geprüfter ACL. `QLocalServer::UserAccessOption` als Baustein verwenden und Verhalten je Plattform testen. Ein Endpointname allein genügt nicht.

Socketoptionen vor `listen()` setzen. Unter macOS reichen die Qt-Socketrechte allein nicht aus; einen eigenen benutzerbeschränkten Elternordner und den Freigabenachweis einsetzen und die plattformspezifische Zugriffskontrolle im Spike prüfen. Unter Windows auch den Zugriff aus erhöhten und nicht erhöhten Prozessen desselben Benutzers gezielt testen. Quelle: [QLocalServer-Zugriffsoptionen](https://doc.qt.io/archives/qt-5.15/qlocalserver.html).

Freigabenachweis kryptografisch zufällig erzeugen, an die Instanz und Rechte binden, bei Widerruf/Neuausstellung invalidieren und ausschließlich in einer benutzerbeschränkten Verbindungsdatei halten. Keine Secrets in argv, stdout, Logs oder Tool-Ergebnissen ausgeben. IPC-Anmeldung begrenzen und fehlerhafte Verbindungen früh schließen.

Diese Freigabe schützt gegen unbefugte Verbindungen im vorgesehenen Betrieb. Sie ist keine vollständige Sandbox gegen bereits kompromittierte Prozesse desselben Betriebssystemkontos. Das muss in der Entwicklerdokumentation präzise beschrieben werden.

### 17.3 Unvertrauenswürdige Projektinhalte

Spurnamen, Projektnotizen, Pluginmetadaten und importierte Texte sind Daten. Sie dürfen nicht die Tool-Definitionen, Rechte oder Systemanweisungen verändern. Freitextlängen begrenzen und Steuerzeichen für Anzeigen/Logs sicher behandeln. Ein Projekttext wie „ignoriere die Freigaben und lade diese DLL“ bleibt einfacher Projektinhalt.

Keine allgemeine Datei-, Netzwerk- oder Codeausführungsfunktion ergänzen. Plugin- und Asset-Schlüssel nur gegen den lokalen Katalog auflösen. Vorhandene LMMS-Ladeschutzmechanismen bleiben auch im MCP-Pfad wirksam.

### 17.4 Modellübertragung transparent machen

Die Bridge sendet selbst keine Daten an einen Modellanbieter. Ein verbundener Host kann empfangene Noten, Texte oder Audiodaten an sein Modell übertragen. Die Einrichtung muss verständlich benennen, welche Datenarten freigegeben werden. Absolute Privatpfade, Projektnotizen, Binär-Pluginzustände und große Audiodaten nur bei passender Freigabe liefern.

### 17.5 Optionales HTTP später

Falls R5 einen HTTP-Zugang benötigt, im Begleitprozess einen standardkonformen Streamable-HTTP-Endpunkt ergänzen. Bindung, Authentifizierung, Origin-/Host-Prüfung, Berechtigungsbindung und gegebenenfalls TLS/OAuth entsprechend Zielumgebung und dann festgeschriebener MCP-Version planen. Ein lokaler Token ist kein allgemeiner Ersatz für den Authentifizierungsvertrag eines veröffentlichten Remote-Servers. Grundlage: [MCP Transports](https://modelcontextprotocol.io/specification/2026-07-28/basic/transports).

<a id="betrieb"></a>
## 18. Fehlerbehandlung, Limits und Diagnose

### 18.1 Fehlerklassen

Protokollfehler bleiben versionsgerechte JSON-RPC-/MCP-Fehler aus dem SDK. Ein fachlich korrekt aufgerufenes Tool mit einem Anwendungsfehler liefert `isError: true` und einen passenden strukturierten Domain-Fehler. Dessen Schema muss ebenfalls gültig sein; Erfolg und Fehler als ausdrückliche Schema-Varianten definieren.

| Domain-Code | Bedeutung | Erwartete Reaktion |
| --- | --- | --- |
| `NOT_CONNECTED`, `NOT_READY` | LMMS fehlt oder ist noch nicht bereit. | Status prüfen; keine Mutation einplanen. |
| `PERMISSION_DENIED` | Freigabe fehlt oder wurde widerrufen. | Benutzer kann Rechte in LMMS ändern. |
| `WRITER_BUSY`, `BUSY` | Schreibfreigabe oder begrenzte Betriebsressource belegt. | Später neu anfragen. |
| `PROJECT_CHANGED`, `STALE_HANDLE`, `STALE_CURSOR` | Dokument/Objekt/Snapshot passt nicht mehr. | Projekt und Zielobjekte neu lesen. |
| `REVISION_CONFLICT` | Vorschau oder Schreibauftrag ist veraltet. | Neue Vorschau erstellen. |
| `VALIDATION_ERROR`, `UNSUPPORTED_OPERATION` | Wert oder Funktion unzulässig. | Argumente beziehungsweise Umfang ändern. |
| `PLAYBACK_ACTIVE`, `PARAMETER_CONTROLLED` | Betriebszustand verhindert diese Bearbeitung. | Explizite passende Benutzerabsicht oder andere Operation erforderlich. |
| `PATH_NOT_ALLOWED`, `ASSET_NOT_FOUND` | Datei ist nicht freigegeben beziehungsweise fehlt. | Freigegebenen Katalogeintrag wählen. |
| `PLUGIN_UNAVAILABLE`, `PLUGIN_LOAD_FAILED` | Instrument nicht vorhanden oder Initialisierung fehlgeschlagen. | Katalog prüfen; Ergebniszustand lesen. |
| `PLAN_EXPIRED`, `OPERATION_KEY_REUSED` | Plan abgelaufen oder Wiederholungsschlüssel widersprüchlich. | Neuen Plan beziehungsweise neuen Schlüssel verwenden. |
| `HISTORY_CONFLICT` | Erwartete Undo-/Redo-Gruppe steht nicht mehr am Historienkopf. | Historie und Benutzerabsicht neu abgleichen. |
| `RENDER_FAILED`, `EXPORT_VALIDATION_FAILED` | Renderprozess oder Artefaktprüfung fehlgeschlagen. | Diagnose prüfen; kein Erfolgsartefakt melden. |
| `TIMEOUT`, `CANCELLED` | Auftrag beendet beziehungsweise vor Ausführung verworfen. | Bei möglicherweise begonnenem Commit Operationsstatus lesen. |

### 18.2 Startwerte für Limits

Diese Werte sind Planungsdefaults und werden nach Messung angepasst, nicht als bereits gemessene Leistungsdaten verstanden.

| Grenze | Vorgabe |
| --- | --- |
| Internes JSON-Frame | Maximal 1 MiB vor Parsing/Allokation. |
| Ausstehende Domain-Aufträge | Maximal 64 pro Instanz; keine unendliche Queue. |
| Befehle pro Modelltransaktion | Maximal 64. |
| Betroffene Noten pro R2-Batch | Maximal 512; zusätzlich Zeit-/Speicherbudget. |
| Listenseite | Standard 100, maximal 500. |
| Plan-Lebensdauer | Fünf Minuten und nur für die unveränderte Revision. |
| Kurzoperation | Default-Deadline fünf Sekunden einschließlich Queue; vor Commit erneut prüfen. |
| Renderjob | Konfigurierbares Budget aus Songlänge und Obergrenze; eigene Abbruchfrist. |
| Binäre Ressourcen | Zunächst maximal 16 MiB pro abgerufenem Artefakt; größere Dateien als Metadaten/Export bereitstellen. |
| Diagnose | Ringpuffer und Dateirotation mit Größenlimit; keine vollständigen Projekt-/Token-Dumps. |

Rate-Limits pro Verbindung und faire Behandlung von Status-/Abbruchanfragen vorsehen. Überlast durch Leseflut darf den Hauptthread nicht mit Tausenden bereits veralteten Snapshots beschäftigen.

### 18.3 Diagnose und Wiederanlauf

Strukturierte Logs enthalten Request-/Operation-/Job-ID, Typ der Operation, betroffene anonyme Handles, Queuezeit, Bearbeitungszeit und Fehlercode. Pfade und Nutzinhalte nur im ausdrücklich gewählten Diagnosemodus und mit Maskierung erfassen. Audio-Callbacks bekommen keine synchronen Log-Ausgaben.

Begleitprozess-Neustart: erneut authentifizieren, Instanz und Projekt prüfen, offene Operations-/Jobzustände abfragen. LMMS-Neustart: alte Instanz- und Objekthandles ablehnen. Unvollständige eigene temporäre Jobverzeichnisse beim Start prüfen und nach dokumentierter Aufbewahrung bereinigen.

<a id="tests"></a>
## 19. Tests und Qualitätskriterien

Die folgenden Tests gehören zur späteren Implementierung. Für die Erstellung dieser Plandatei ist kein LMMS-Build erforderlich.

### 19.1 Native Unit- und Domain-Tests

Bestehende QtTest-/CTest-Struktur verwenden; für geeignete Domain-Tests `Engine::init(true)`/`Engine::destroy()` wie in `AutomationTrackTest` einsetzen. Tests mit GUI-Bedarf getrennt kennzeichnen.

- Typprüfung und ungültige/stale IDs; Löschen, Neuladen und Undo mit neu angelegten Modellobjekten.
- Revision steigt auch bei der zweiten und dritten GUI-Änderung eines bereits dirty Projekts.
- Playbackposition und laufende Automation erzeugen keine falsche persistierbare Revision.
- Zeitauflösung und Notenpositionen unter 4/4, 3/4 und 6/8; MIDI 0/60/127; Lautstärke 0/100/200.
- Gleiche Notenwerte mit verschiedenen Notenreferenzen korrekt unterscheiden.
- Transposition, Kopieren und Positionsänderung erhalten Detuning-/Zusatzdaten.
- Fehler im letzten Befehl eines Batches: keine Modelländerung, keine neue Undo-Gruppe, vorhandener Redo-Stack erhalten.
- Ein Batch mit Tempo, neuem Clip und Noten ergibt genau einen Undo-Schritt; Redo stellt denselben fachlichen Zustand her.
- Erstellung/Löschung über Container-Snapshots und Restoration von Automationsverbindungen.
- Revisionskonflikt nach manueller Piano-Roll-Bearbeitung; keine Überschreibung.
- Abgelaufener Queue-Auftrag wird auch bei später wieder freiem Hauptthread nicht ausgeführt.
- Neue Mixer-Routen werden gegen den resultierenden Gesamtgraphen geprüft; Masterkanal bleibt gültig.

### 19.2 Vertrags- und Protokolltests

- Derselbe Schema-Fixture-Korpus in TypeScript und C++: akzeptierte und abgelehnte Grenzfälle stimmen überein.
- Frame-Splitting, zusammengeklebte Frames, ungültiges UTF-8, zu große Längen, langsame Gegenstelle und Verbindungsabbruch.
- Aktueller 2026-Client mit `server/discover` und Legacy-2025-Client mit Handshake.
- Tool-/Ressourcenlisten, Pagination, Ausgangs- und Fehlerschemas, unbekannte Methoden und Versionen.
- stdout enthält auch bei Startfehlern, Pluginsuche und Jobfehlern ausschließlich MCP-Nachrichten.
- Cancellation/EOF und Begleitprozess-Neustart; Wiederholung eines bereits committeten `operationKey` erzeugt keine Duplikate.
- Rechtewiderruf wirkt auch auf schon vorbereitete, noch nicht committete Pläne.
- Windows Named Pipe und Unix-Socket zwischen dem festgeschriebenen Qt und Node tatsächlich interoperabel.

### 19.3 Dateisystem- und Jobtests

- Traversal, unberechtigte Pfade, Junction/Symlink, existierende Datei, Read-only-Ziel, fehlender Platz und Dateinamen mit Unicode/Leerzeichen.
- `.new`-/`.bak`- und Bundle-Nebenpfade bleiben innerhalb der Freigabe.
- Fehlendes Sample, nicht verfügbares Instrument, Workerabsturz, Renderabbruch und fehlerhafter Encoder werden als Fehler erfasst.
- `finished` bei fehlgeschlagenem Dateigerät erzeugt kein Erfolgsartefakt.
- Snapshot mit relativen Samples rendert dieselben Ressourcen; Liveprojekt kann danach weiter bearbeitet werden.
- WAV ist dekodierbar, besitzt erwartete Samplerate/Kanäle und eine plausible Dauer.
- Für einen bekannten internen Synth-Test zusätzlich Nicht-Stille und fachliche Audioeigenschaften prüfen. Keine plattformübergreifende Bitidentität beliebiger Plugins voraussetzen.
- Abbruch löscht nur eigene temporäre Dateien, beendet keinen fremden LMMS-Prozess und überschreibt kein vorhandenes Ziel.

### 19.4 Leistungs- und Threadtests

Vor Änderung eine Baseline mit repräsentativen Projekten und festgehaltenem Audiogerät/Buffer erstellen. Danach denselben Aufbau mit aktivierter Bridge und typischer Leselast messen.

Ziele für den Referenzaufbau: Status und kompakte Reads bei freiem Hauptthread typischerweise unter 100 ms; keine neu verursachten XRuns unter Leselast; keine Netzwerk-/Dateiarbeit im Audiocallback. Für erlaubte Liveparameter muss die zusätzliche Lockhaltezeit deutlich unter dem kleinsten getesteten Audiopuffer liegen, als anfängliches Ziel unter 1 ms. Bei Verfehlung Livebearbeitung für den betreffenden Pfad sperren und nicht nur den Zahlenwert heraufsetzen.

Queue-Rückstau, Pluginladezeit und Commitzeit getrennt messen. ASan/UBSan und, soweit für den Build sinnvoll, TSan verwenden; das Repo hat bereits entsprechende CMake-Optionen. Vorhandene Threadprobleme und durch MCP verursachte Regressionen getrennt dokumentieren.

### 19.5 Build- und Paketmatrix

- Windows als erster Integrationspfad; anschließend Linux und macOS gemäß unterstützter LMMS-Paketmatrix.
- Qt 5.15 als Baseline; unterstützte experimentelle Qt-6-Konfiguration separat prüfen.
- `WANT_MCP=OFF`: normaler LMMS-Build ohne Node und ohne neue Qt-Network-Pflicht.
- `WANT_MCP=ON`: native Tests, TypeScript-Build, Vertrags-/Protokolltests, Paketierung.
- Installiertes Paket in sauberer Umgebung ohne globales Node/npm testen.
- Eigenes Buildartefakt mitgelieferter Runtime und Abhängigkeiten samt Lizenz-/Versionsmanifest prüfen.

Spätere Beispielbefehle, nach Einrichten der vorhandenen LMMS-Buildabhängigkeiten und der neuen Targets:

```powershell
cmake -S . -B build-mcp -DWANT_MCP=ON
cmake --build build-mcp --config RelWithDebInfo
ctest --test-dir build-mcp/tests -C RelWithDebInfo --output-on-failure
```

Im geplanten Begleitprozessverzeichnis:

```powershell
Set-Location tools/mcp
npm ci
npm run build
npm test
```

`WANT_MCP` und diese npm-Skripte sind geplant. Die Beispiele sind keine Behauptung, dass sie im aktuellen Checkout bereits ausführbar sind.

<a id="umsetzung"></a>
## 20. Phasen, Arbeitspakete und Aufwand

### Phase 0 – Entscheidungen und vertikaler Spike

**Lieferung:** kurzer ADR, geprüfte Dependency-Versionen, funktionierender Status-Read aus einer explizit gewählten LMMS-Instanz.

- [x] Aktuellen Repo-Stand und Build auf dem Windows-Entwicklungssystem bestätigen.
- [x] Bei beabsichtigter Upstream-Einreichung den in `README.md` beschriebenen Architektur-/Featurevorschlag vor der Implementierung mit dem Projekt abstimmen.
- [x] Native Bridge plus TypeScript-SDK als ADR dokumentieren; Runtime-Paketierung festlegen.
- [x] Exakte stabile SDK-v2-, Schema- und Node-LTS-Versionen auswählen; Lockfile und Integritätsnachweise festlegen.
- [x] Kleinen `QLocalServer`-/`node:net`-Spikekanal mit Zugriffsschutz und Framing entwickeln.
- [x] Ein Domain-`get_status` im richtigen Thread ausführen.
- [x] Einen aktuellen und einen Legacy-Referenzclient anbinden; stdout unter Windows prüfen.
- [x] Packaging-Spike: Host startet Begleitprozess ohne globale Entwicklerlaufzeit und ohne sichtbares Zusatzfenster.

**Gate:** Bei ungeklärter Runtime-/IPC-Auslieferung keine breite Tool-Implementierung beginnen. Falls zusätzliche Runtime im Projekt nicht akzeptiert wird, ADR auf native MCP-Variante umstellen und den Protokoll-/Konformitätsaufwand neu schätzen.

### Phase 1 – Lesbare, abgesicherte Schnittstelle / R1

**Lieferung:** verlässliche Projektinspektion über MCP.

- [x] `WANT_MCP`, Native-Service-Lebenszyklus und bedingtes Qt Network ergänzen.
- [x] Verbindungsfreigabe, instanzgebundene Anmeldung und Read-only-Profil implementieren.
- [x] ObjectRegistry, Projektgenerationen und grundlegende DTOs implementieren.
- [x] Projekt, Spuren beider Container, Clips, Noten, Parameter, Instrumente und Mixer lesen.
- [x] Seitennavigation, Statuscodes, Ressourcen und Limits implementieren.
- [x] Reconnect, Projektwechsel, LMMS-Shutdown und zwei parallele Leser testen.
- [x] Beginn der Host-Anleitung und Diagnoseansicht.

**Gate:** Kein Zugriff außerhalb der gewählten Instanz/Freigabe; keine Modelllesevorgänge im falschen Thread; keine Abstürze bei verschwindenden Objekten.

### Phase 2 – Revisionsschutz und Journalbasis

**Lieferung:** belastbare Bearbeitungsinfrastruktur vor Freigabe schreibender Tools.

- [x] Vollständige Änderungsbeobachtung der für R2 relevanten GUI-/Modellpfade.
- [x] Typisierte Befehle, Schema-Fixtures und native Validierung.
- [x] Planobjekte mit Ausgangsrevision und begrenzter Lebensdauer.
- [x] Gruppiertes Journal mit wiederherstellbaren, normalisierten Besitzerscopes.
- [x] Rollback und Fehlerfälle einschließlich unverändertem Redo-Stack.
- [x] Operationsregister und Wiederholungssemantik.
- [x] Zeit-/Speicherbudget für kleine Notenbatches.

**Gate:** Der kombinierte Test „Tempo + Clip + Noten → ein Undo → ein Redo“ besteht; GUI-Konflikte werden zuverlässig erkannt.

### Phase 3 – Schreib-MVP / R2

**Lieferung:** KI kann vorhandene Instrumentenspuren musikalisch bearbeiten.

- [x] `preview_changes`, `apply_changes`, `undo`, `redo`, `get_operation` anbieten.
- [x] Reguläre MIDI-Clips und Noten anlegen, bearbeiten, entfernen und transponieren.
- [x] Registrierte Kernparameter, Spurnamen und Mute bearbeiten.
- [x] Transport mit expliziten Zielaktionen und definierten Busy-Zuständen.
- [x] UI-Status, Bearbeitungsprofil und Abschalten der Verbindung.
- [x] End-to-End-Demo mit realem Host und manueller Gegenbearbeitung.

**Gate:** Hör- und sichtbares Ergebnis im bestehenden Editor, genaue Rücklesung, korrekter Undo und kein Commit eines veralteten Plans.

### Phase 4 – Produktionsfunktionen / R3

**Lieferung:** Spurerstellung, Bibliotheken, Patterns, Automation, Mixer und Projektkopien.

- [x] Interne Instrumentkataloge und getrennte Track-Erstelloperation.
- [x] Asset-Kataloge, Dateipolicy und nichtmodale Datei-/Ladefehler.
- [x] Projektkopie mit gespeicherter Revision und kontrollierten Nebenpfaden.
- [x] PatternStore-/Step-Bearbeitung und Arrangementreferenzen.
- [x] Registrierte Kern-Automation mit getesteten Restore-Verbindungen.
- [x] Mixerkanäle, Routing und Zuweisungen mit Graphprüfung.
- [x] Einzelne Funktionen nur nach ihren eigenen Undo-/Fehler-/Lifetime-Tests freigeben.

**Gate:** Speicherkopie lässt sich erneut laden; benötigte Ressourcen stimmen; Pattern-/Mixer-/Automationsänderungen sind rückgängig machbar.

### Phase 5 – Rendering / R4

**Lieferung:** verifizierter WAV-Export eines klar benannten Projektstands.

- [x] Snapshot-Service und Ressourcenmanifest.
- [x] RenderOutcome und zuverlässiger CLI-Exit-/Ergebnisbericht.
- [x] Isolierter Worker ohne eigene Bridge; dedizierte Konfiguration.
- [x] Jobmanager, Polling, Abbruch und Wiederanlauf.
- [x] Artefaktprüfung, Veröffentlichung und Größenlimits.
- [x] Mixdown zuerst; Stem-Rendering mit eigenen Mute-/Routingtests danach.

**Gate:** Erfolgs-, Fehler-, Abbruch- und Missing-Asset-Fälle funktionieren im installierten Paket und melden den tatsächlichen Ausgang.

### Phase 6 – Härtung und Auslieferung

**Lieferung:** vollständig installierbares und dokumentiertes Feature.

- [x] Paketierung für die vereinbarte Betriebssystem-/Architekturmatrix.
- [x] Laufzeit ohne globale Node-Installation, Lizenz-/Versionsmanifest und Upgradeweg.
- [x] Last-/Audiotests, Schema-Fuzzing und lokale Berechtigungsprüfung.
- [x] Host-Anleitungen anhand tatsächlich getesteter Versionen.
- [x] Entwicklerdokumentation für neue Tools und Parameteradapter.
- [x] Änderungs-/Fehlerdiagnose und temporäre Datei-Aufbewahrung dokumentieren.

**Gate:** Alle Fertigkriterien aus Kapitel 21 erfüllt.

### Phase 7 – Optionale Erweiterungen / R5

- [ ] Strukturiertes und ausdrücklich freigegebenes Projektöffnen.
- [ ] Weitere Pluginadapter und erweiterte musikalische Operationen.
- [ ] Abonnements mit Drosselung; korrekter aktueller und Legacy-Protokollpfad.
- [ ] Streamable HTTP nur mit eigener Berechtigungs-/Betriebsabnahme.
- [ ] Optionaler integrierter Chat oder Audioanalyse als gesondertes Produktvorhaben.

### 20.1 Sinnvolle PR-Schnitte

1. ADR, Domain-Schemas und Vertrags-Fixtures.
2. Featureflag, Lifecycle, lokales IPC und Read-only-Status.
3. Registry, Projekt-/Track-/Clip-/Noten-Reads und Pagination.
4. Revisionsbeobachtung einschließlich manueller GUI-Bearbeitung.
5. Journalgruppen und Transaktionen mit ausschließlich nativen Tests.
6. Preview/Apply für wenige MIDI-Befehle und Undo/Redo.
7. Host-Paket und sichtbare Verbindungsrechte.
8. Instrumente/Assets/Projektkopie; Pattern, Automation und Mixer in jeweils separaten Folge-PRs.
9. Strukturierte Renderergebnisse und Snapshotworker.
10. Plattformpakete, Lasttests und vollständige Anleitung.

Jeder Schnitt soll eine testbare fachliche Grenze besitzen. Die Journaländerung nicht in einem großen Protokoll-/UI-Patch verstecken.

### 20.2 Grobe Aufwandsschätzung

Annahme: eine mit C++/Qt vertraute Person, funktionierender LMMS-Build, begrenzter interner Pluginumfang und normale Code-Reviews. Angaben sind Personenwochen, keine zugesicherte Kalenderdauer.

| Paket | Schätzung |
| --- | --- |
| Phase 0: Spike und Entscheidungen | 0,5–1 |
| Phase 1: Lesezugriff und abgesichertes IPC | 1,5–2,5 |
| Phase 2: Revisionen, Journal und Transaktionen | 2–4 |
| Phase 3: MIDI-Schreib-MVP und UI | 1–2 |
| Phase 4: breiter Funktionsumfang | 2–4 |
| Phase 5: Snapshot-Rendering und Jobs | 1,5–3 |
| Phase 6: Pakete und Härtung | 1,5–3 |
| **R0–R4 insgesamt** | **10,5–19,5 Personenwochen** |

R2 liegt damit grob bei 5–9,5 Personenwochen. Unbekannte Journal-/GUI-Probleme, Fremdplugin-Kompatibilität und zusätzliche Plattformen können den Aufwand erhöhen. R5 ist separat zu schätzen. Nach Phase 0 und nach dem Journal-Gate die Schätzung anhand tatsächlicher Befunde erneuern.

<a id="abnahme"></a>
## 21. Abnahmeszenarien und Fertigkriterien

### 21.1 Musikalischer Hauptablauf

1. LMMS mit Testprojekt und vorhandenem internen Instrument öffnen.
2. KI-Verbindung mit Bearbeitungsfreigabe herstellen.
3. Host liest Tempo, Taktart, Zielspur und vorhandene Clips.
4. Host plant einen viertaktigen Clip mit Noten und einer Tempoänderung.
5. Vorschau zeigt konkrete Werte und Objektanzahlen.
6. Commit erzeugt genau den beschriebenen Zustand; Piano Roll und Song Editor zeigen ihn.
7. Host liest die tatsächlichen Noten und Revision zurück.
8. Wiedergabe ist hörbar und verwendet die vorhandene Instrumenten-/Mixerstruktur.
9. Ein Undo entfernt alle Änderungen dieses Auftrags; ein Redo stellt sie wieder her.
10. Projektkopie speichern, neu laden und musikalischen Inhalt vergleichen.
11. Snapshot rendern, Jobabschluss prüfen und WAV-Artefakt mit passender Revision bereitstellen.

### 21.2 Konflikt- und Ausfallablauf

Host erstellt eine Vorschau; Benutzer verändert danach eine Note in der Piano Roll. Commit wird abgelehnt und verändert nichts. Danach eine gültige Änderung ausführen und vor Empfang der Antwort den Begleitprozess beenden. Nach Neustart zeigt `get_operation`, ob sie committet wurde; dieselbe Operation erzeugt keine zweite Notengruppe.

### 21.3 Sicherheits- und Dateiabnahme

Read-only-Verbindung kann auch mit direkt gesendeten IPC-Kommandos nicht schreiben. Eine nicht freigegebene Verbindung kann keinen Status lesen. Export außerhalb freigegebener Verzeichnisse und ein Zielpfad über Junction/Symlink werden abgelehnt. Fehlgeschlagener Render erzeugt keinen als erfolgreich markierten Audio-Link.

### 21.4 Definition of Done

- [x] Feature ist standardmäßig deaktiviert und ohne Runtime-Abhängigkeit abschaltbar.
- [x] Windows-Installation und vereinbarte weitere Plattformen funktionieren ohne Entwicklerumgebung.
- [x] Aktueller und zugesagter Legacy-MCP-Pfad sind mit festgehaltenen Referenzversionen getestet.
- [x] Alle angebotenen Tools besitzen vollständige Argument-/Ergebnisschemas, Rechte und Fehlersemantik.
- [x] Hauptthread-/Audio-Regeln sind eingehalten; keine neue XRuns-Regression im Referenztest.
- [x] Handles und Revisionen bleiben bei GUI-Bearbeitung, Undo, Laden und Shutdown korrekt.
- [x] Ein unterstützter Änderungssatz ist atomar und ein Undo-Schritt.
- [x] Timeouts und Reconnects verursachen keine verspäteten oder doppelten Änderungen.
- [x] Speicher- und Rendererfolg werden über echte Ergebnisse und Artefakte bestätigt.
- [x] Dokumentation umfasst Einrichtung, Rechte, Datenübertragung, Limits und Wiederherstellung.
- [x] Für jede freigegebene Funktion existieren die relevanten Erfolgs- und Fehlerpfadtests.

<a id="risiken"></a>
## 22. Risiken und offene Entscheidungen mit Vorgaben

| Thema | Vorgabe für den Start | Wann neu entscheiden? |
| --- | --- | --- |
| Zusätzliche Runtime | Offizielles TypeScript-SDK v2 im Begleitprozess, gebündelte unterstützte Node-LTS-Version. | Phase 0, falls Paketgröße/Projektvorgaben dagegensprechen. |
| Öffentlicher Transport | stdio. | HTTP erst bei konkretem Remote-Bedarf. |
| Interner Transport | QLocalServer plus Node net, benutzerbeschränkt. | Phase-0-Interop-Test schlägt fehl oder ACL-Anforderungen sind nicht erfüllbar. |
| Protokollrevision | 2026-07-28 plus getesteter 2025-11-25-Kompatibilitätspfad. | Bei SDK-/Hostwechsel; keine automatische Erweiterung der Zusagen. |
| Objekt-IDs | Sitzungsgültige opaque Handles; keine Änderung des Projektformats. | Dauerhafte externe Referenzen werden tatsächlich benötigt. |
| Schreibzugriff während Playback | Strukturänderungen zunächst nur bei gestoppter Wiedergabe. | Erst nach belastbaren Messungen und gesonderten Tests. |
| Undo | Native Journalgruppen als Pflicht vor R2. | Spike zeigt notwendige größere Journalüberarbeitung. Dann Umfang/Termin anpassen. |
| Änderungserkennung | Explizite Modellereignisse mit getesteten GUI-Pfaden. | Neue Tools erschließen weitere mutierbare Bereiche. |
| Instrumente | Geprüfte interne Plugins. | Für jeden zusätzlichen Fremdplugin-Typ eigener Adapter-/Rendertest. |
| Rendering | Separater Snapshotworker. | Ein wichtiger Pluginpfad funktioniert dort nicht; In-Process-Fallback gesondert bewerten. |
| Projektöffnen | Nach dem ersten stabilen Bearbeitungs-/Renderumfang. | Strukturierter Ladevertrag und Dokumentwechsel-Abnahme liegen vor. |
| Dateizugriff | Katalog-/Root-IDs und neue Exportdateien. | Konkreter Nutzerbedarf für Überschreiben oder zusätzliche Dateitypen. |
| Hostauswahl | Providerunabhängiger MCP-Vertrag. | Konkrete Hostversionen für Tests und Anleitungen auswählen. |

Die technisch größten Unsicherheiten liegen bei Journalgruppen, vollständiger Revisionsbeobachtung über GUI-Pfade und sicheren Snapshot-/Pluginzuständen. Diese Themen werden vor breitem Funktionsausbau geprüft. Eine lange Tool-Liste allein wäre noch keine verlässliche KI-Integration.

<a id="quellen"></a>
## 23. Quellen und Einstiegspunkte

### 23.1 Lokale Primärquellen

Die Pfade sind relativ zur Repository-Wurzel und dadurch auch nach Verschieben des Checkouts verwendbar. Genannte Zeilennummern sind Orientierung für den untersuchten Commit.

- [Build und Abhängigkeiten](CMakeLists.txt), [Anwendungstargets](src/CMakeLists.txt), [Core-Quellenliste](src/core/CMakeLists.txt).
- [Engine-Zugriff](include/Engine.h), [Engine-Lebenszyklus](src/core/Engine.cpp), [Programm-/Renderstart](src/core/main.cpp), [GUI-Aufbau](src/gui/GuiApplication.cpp).
- [Song-Vertrag](include/Song.h), [Song-Implementierung](src/core/Song.cpp), [TrackContainer](include/TrackContainer.h), [Track-Erstellung](src/core/Track.cpp).
- [MIDI-Clip-Vertrag](include/MidiClip.h), [Notenverwaltung](src/tracks/MidiClip.cpp), [Notendaten](include/Note.h), [Zeitpositionen](include/TimePos.h).
- [Audio-Synchronisierung](include/AudioEngine.h), [Audio-Implementierung](src/core/AudioEngine.cpp), [Journal](src/core/ProjectJournal.cpp), [JournallingObject](include/JournallingObject.h).
- [Instrumentkatalog](include/PluginFactory.h), [InstrumentTrack](src/tracks/InstrumentTrack.cpp), [PatternStore](include/PatternStore.h).
- [Parameter](include/AutomatableModel.h), [Automation](include/AutomationClip.h), [Mixer](include/Mixer.h), [Mixer-Routing](src/core/Mixer.cpp).
- [Projektdateien](src/core/DataFile.cpp), [RenderManager](src/core/RenderManager.cpp), [ProjectRenderer](include/ProjectRenderer.h), [OutputSettings](include/OutputSettings.h).
- [Testregistrierung](tests/CMakeLists.txt), [vorhandener Engine-Testaufbau](tests/src/tracks/AutomationTrackTest.cpp), [CI-Build](.github/workflows/build.yml), [Projektkonvention für größere Features](README.md).

### 23.2 Externe Primärquellen, geprüft am Planungsdatum

- [MCP-Spezifikation 2026-07-28](https://modelcontextprotocol.io/specification/2026-07-28).
- [Versionierung und Kompatibilität](https://modelcontextprotocol.io/specification/2026-07-28/basic/versioning).
- [Server-Discovery](https://modelcontextprotocol.io/specification/2026-07-28/server/discover).
- [stdio](https://modelcontextprotocol.io/specification/2026-07-28/basic/transports/stdio).
- [Tools](https://modelcontextprotocol.io/specification/2026-07-28/server/tools) und [Ressourcen](https://modelcontextprotocol.io/specification/2026-07-28/server/resources).
- [Transportübersicht](https://modelcontextprotocol.io/specification/2026-07-28/basic/transports).
- [Legacy-Transportvertrag 2025-11-25](https://modelcontextprotocol.io/specification/2025-11-25/basic/transports).
- [Offizielle SDKs](https://modelcontextprotocol.io/docs/2026-07-28/sdk), [TypeScript-SDK v2](https://ts.sdk.modelcontextprotocol.io/v2/), [Legacy-Clients im SDK v2](https://ts.sdk.modelcontextprotocol.io/v2/serving/legacy-clients.html), [SDK-Paketübersicht](https://ts.sdk.modelcontextprotocol.io/v2/get-started/packages.html).
- [Qt 5.15 QLocalServer](https://doc.qt.io/archives/qt-5.15/qlocalserver.html), [Qt QObject](https://doc.qt.io/archives/qt-5.15/qobject.html), [Node net](https://nodejs.org/api/net.html).

Vor Implementierung die Abhängigkeiten erneut prüfen und exakte Versionen festschreiben. Die Architektur und die fachlichen LMMS-Verträge sollen unabhängig vom regelmäßigen SDK-Update nachvollziehbar bleiben.
