<p align="left">
    <img src="src/Resources/icons/pinpointstudio_1024.png" width="150" />
  </p>

[English](README.md) · [Français](README.fr.md) · **Deutsch** · [日本語](README.ja.md) · [한국어](README.ko.md) · [简体中文](README.zh-CN.md)

> Hinweis: Diese Datei ist eine Übersetzung von README.md; die verlinkten Dokumente liegen nur auf Englisch vor.

# PinPoint Studio

PinPoint Studio ist eine kostenlose, plattformübergreifende Open-Source-Desktop-Anwendung zur
Analyse des Golfschwungs. Sie führt Hochgeschwindigkeitskameras, Smartphones, am Körper getragene
Sensoren und Launch-Monitore zusammen, vermisst den Schwung mit Computer Vision direkt auf dem
eigenen Rechner und erklärt, was sie gefunden hat – ohne Ihre Daten irgendwohin zu senden, sofern
Sie das nicht selbst so einrichten.

Die Anwendung ist für Golfer, Trainer und Forschende gedacht und läuft unter Windows, macOS und
Linux.

> **Status:** Beta (v0.1). Aufnahme, Analyse, Auswertung und Diagnose funktionieren durchgängig;
> die Sitzungen zu Bodenreaktionskräften und zum KI-Coach sind noch in Entwicklung.

<img width="2560" height="1440" alt="beta3-screenshot" src="https://github.com/user-attachments/assets/c060ba3b-2927-4380-8c58-a34cc4c424a2" />

## Die wichtigsten Funktionen

- **Aufnahme mit mehreren Kameras** – USB-Webcams, GenICam-Industriekameras (Aravis) und
  Hochgeschwindigkeitskameras von Teledyne FLIR (Spinnaker), dazu eine eigene Nahaufnahmekamera für
  den Treffmoment.
- **Smartphone als Kamera** – ein iPhone mit PinPoint Capture nimmt per WLAN oder USB-Kabel über
  eine verschlüsselte Verbindung an einer Sitzung teil.
- **Markerlose Schwungerfassung** – Posenschätzung für den ganzen Körper (ViTPose), Verfolgung von
  Schlägerschaft und Schlägerkopf ohne Klebeband oder Marker sowie Ballerkennung.
- **Automatische Schlagerkennung** – Schläge werden über den IMU-Ausschlag im Treffmoment, das
  Treffgeräusch oder den Launch-Monitor erkannt; jeder Schlag wird sofort analysiert und
  gespeichert.
- **Schwungkennzahlen** – die zehn P-positions (von der Ansprechposition bis zum Finish), Tempo,
  Schlägerführung zum Ball (Club Delivery), Körperrotation, Körperhaltung, Handgelenkwinkel und die
  kinematische Sequenz, jeweils mit der Angabe, wie verlässlich der Wert ist.
- **Diagnose** – Schwungfehler und Stärken werden anhand von Normbereichen benannt, mit den
  wahrscheinlichen Ursachen und passenden Drills, für jeden einzelnen Schlag und über eine ganze
  Sitzung hinweg.
- **Handgelenkanalyse** – Witmotion-IMUs oder ein HackMotion wG3 erfassen Beugung (Flexion),
  Streckung (Extension) und Winkeln (Cocking) des führenden Handgelenks über den gesamten Schwung.
- **Launch-Monitore** – Foresight GCQuad (über FSX2020) sowie jedes Gerät, für das es eine Bridge
  zu GSPro Open Connect gibt (Garmin R10, Rapsodo MLM2PRO, SkyTrak+ und weitere). Die Messwerte
  werden dem Schwung zugeordnet, aus dem sie stammen.
- **Auswertung** – synchrone Wiedergabe aller Kameras mit Overlays, Diagrammen und einer
  Phasen-Zeitleiste, dazu eine Schwungbibliothek, die jede Sitzung lokal auf dem Datenträger
  aufbewahrt.
- **Ground-Truth-Annotation** – bildgenaues Markieren der P-positions direkt in der App, um die
  Analyse zu validieren und abzustimmen.
- **Sprachein- und -ausgabe** – lokale Spracherkennung (whisper.cpp) und Sprachausgabe (Kokoro),
  bei Bedarf mit Cloud-Diensten als Ausweichlösung.
- **Lokal zuerst und privat** – die Analyse läuft auf dem eigenen Rechner und nutzt
  GPU-Beschleunigung, wo sie verfügbar ist (CUDA, Vulkan, CoreML, Metal). Es gibt keine Telemetrie,
  und Cloud-Dienste bleiben ausgeschaltet, solange Sie sie nicht einrichten.
- **Updates in der App** – signierte Installationsprogramme für Windows und macOS, die sich an Ort
  und Stelle selbst aktualisieren. Ein selbstaktualisierendes AppImage für Linux ist in Arbeit; bis
  dahin wird unter Linux aus dem Quellcode gebaut.

## PinPoint Studio beziehen

Installationsprogramme werden auf der
[Releases-Seite](https://github.com/PinPoint-Golf/PinPointStudio/releases) veröffentlicht. Wie Sie
die Anwendung aus dem Quellcode bauen, steht in **[BUILDING.md](BUILDING.md)**.

## Aufbau des Repositorys

| Pfad | Inhalt |
|---|---|
| `src/` | Die Anwendung, ein Ordner je Subsystem (siehe unten) |
| `tests/` | Der übergreifende Test-Build. Jede Testsuite liegt neben ihrem Code in `src/<area>/tests/` |
| `tools/` | Offline- und Laborwerkzeuge: SwingLab (lässt die Analyse-Pipeline erneut über aufgezeichnete Schwünge laufen), Labs für Schaft, Ball und Treffmoment, simulierte Launch-Monitore, Paketierungsskripte |
| `packaging/` | Skripte für den Windows-Installer und den Appcast, Paketierungsressourcen für macOS |
| `cmake/` | Build-Module: Qt-Erkennung, das QML-Modul, Schriften, OpenSSL, Codesignierung |
| `third_party/` | Kleiner eingebundener Fremdcode (der ESKF-Orientierungsfilter) |
| `docs/` | Dokumentation, nach Zielgruppen geordnet (siehe unten) |

In `src/`:

| Ordner | Subsystem |
|---|---|
| `Gui/` | Die Oberfläche in Qt Quick / QML und ihre C++-Modelle |
| `Video/`, `Pose/` | Kamera-Backends; Modelle für Pose, Ball und Personensegmentierung |
| `Analysis/`, `Metrics/`, `Diagnostics/` | Pipeline der Schwunganalyse, Kennzahlenkatalog, Fehlerdiagnose |
| `Buffer/` | Der sperrfreie (lock-free) Ereignispuffer, der alle Sensoren zeitlich abgleicht |
| `IMU/`, `LaunchMonitor/`, `Ppcp/`, `Audio/` | Sensoren, Launch-Monitore, Smartphone-Aufnahme, Mikrofone und Schlaggeräusch |
| `Export/` | Schwungvideo und das Dokument `swing.json` |
| `STT/`, `TTS/`, `LLM/`, `Secrets/` | Sprachein- und -ausgabe, der KI-Coach, Speicherung der API-Schlüssel |
| `Update/`, `Core/`, `Resources/`, `Shaders/` | Updates in der App, gemeinsame Infrastruktur, mitgelieferte Ressourcen, GPU-Shader |

## Dokumentation

| Wo | Wofür |
|---|---|
| [BUILDING.md](BUILDING.md) | Abhängigkeiten, Bauen auf den einzelnen Plattformen, Paketierung, Ausführen der Tests |
| [`docs/user/`](docs/user) | Die App benutzen: [Diagnose-Leitfaden](docs/user/pinpoint-diagnostics-guide.md), [Handgelenkkalibrierung](docs/user/wristcalibration.md), [Smartphone-Aufnahme](docs/user/phone_capture.md), UX-Design |
| [`docs/developer/`](docs/developer) | Leitfäden für Entwickler: [Analyse-Pipeline](docs/developer/analysis_pipeline_developer_guide.md), [Testen](docs/developer/testing_developer_guide.md), [SwingLab](docs/developer/swinglab_developer_guide.md), [Feature-Schalter](docs/developer/feature_switches_developer_guide.md), [Übersetzungen](docs/developer/translations_developer_guide.md), Ereignispuffer, Diagnose, Kennzahlen |
| [`docs/design/`](docs/design) | Entwurfsdokumente zu jedem Subsystem und das [QML-Designsystem](docs/design/pinpoint_qml_design_system.md) |
| [`docs/reference/`](docs/reference) | Dateiformate ([`swing.json`](docs/reference/swing_json_schema.md), [Schwungordner](docs/reference/swing_folder_layout.md)), Vorzeichenkonventionen, Geräteprotokolle, Normdaten |
| [`docs/validation/`](docs/validation), [`docs/research/`](docs/research) | Validierungsprotokolle, Korpus-Ergebnisse und Forschungsnotizen |
| `docs/implementation/` | Arbeitspläne und Release-Runbooks |

## Verwandte Projekte

Diese Schwester-Repositorys werden als Abhängigkeiten eingebunden:

- [libppcp](https://github.com/PinPoint-Golf/libppcp) – das PinPoint Capture Protocol, das auch die Smartphone-App nutzt
- [PinPoint Capture](https://github.com/PinPoint-Golf/PinPointCapture) – die Aufnahme-App für das iPhone
- [libwrist](https://github.com/PinPoint-Golf/libwrist) – Treiber für den Sensor HackMotion wG3
- [libgspro](https://github.com/PinPoint-Golf/libgspro) – Server für GSPro Open Connect

## Lizenz

PinPoint Studio steht unter der GNU General Public License, Version 2 oder später. Die verteilten
Binärdateien werden wegen der Komponenten, gegen die sie gelinkt sind, unter GPL-3.0
weitergegeben. Siehe [LICENSE](LICENSE); alle Komponenten von Drittanbietern und ihre Bedingungen
sind in [LICENSEDEPS.md](LICENSEDEPS.md) aufgeführt.
