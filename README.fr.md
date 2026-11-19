<p align="left">
    <img src="src/Resources/icons/pinpointstudio_1024.png" width="150" />
  </p>

[English](README.md) · **Français** · [Deutsch](README.de.md) · [日本語](README.ja.md) · [한국어](README.ko.md) · [简体中文](README.zh-CN.md)

> Note : ce fichier est une traduction de README.md ; les documents liés ne sont disponibles qu’en anglais.

# PinPoint Studio

PinPoint Studio est une application de bureau gratuite, open source et multiplateforme d’analyse
du swing de golf. Elle réunit caméras haute vitesse, téléphones, capteurs portés sur le corps et
launch monitors, mesure le swing grâce à la vision par ordinateur exécutée sur l’appareil, et
explique ce qu’elle a constaté, sans envoyer vos données où que ce soit, sauf si vous le décidez.

Elle s’adresse aux golfeurs, aux coachs et aux chercheurs, et fonctionne sous Windows, macOS et
Linux.

> **Statut :** bêta (v0.1). La capture, l’analyse, la revue et le diagnostic fonctionnent de bout
> en bout ; les séances « forces au sol » et « coach IA » sont encore en cours de développement.

<img width="2560" height="1440" alt="beta3-screenshot" src="https://github.com/user-attachments/assets/c060ba3b-2927-4380-8c58-a34cc4c424a2" />

## Fonctionnalités principales

- **Capture multicaméra** — webcams USB, caméras industrielles GenICam (Aravis) et caméras haute
  vitesse Teledyne FLIR (Spinnaker), avec une caméra dédiée au gros plan sur l’impact.
- **Le téléphone comme caméra** — un iPhone exécutant PinPoint Capture rejoint une séance en WiFi
  ou par câble USB, via une liaison chiffrée.
- **Suivi du swing sans marqueurs** — estimation de pose du corps entier (ViTPose), suivi du shaft
  et de la tête de club sans ruban adhésif ni marqueurs, et détection de la balle.
- **Détection automatique des coups** — les coups sont détectés à partir de l’impact mesuré par
  l’IMU, du son de l’impact ou du launch monitor, et chacun est analysé et enregistré au fur et à
  mesure.
- **Métriques du swing** — les dix P-positions (de l’adresse au finish), le tempo, la présentation
  du club à l’impact (club delivery), la rotation du corps, la posture, les angles des poignets et
  la séquence cinématique, chaque mesure étant accompagnée de son niveau de fiabilité.
- **Diagnostic** — les défauts et les points forts du swing sont identifiés par rapport à des
  plages normatives, avec leurs causes probables et des exercices (drills), pour chaque coup et
  sur l’ensemble d’une séance.
- **Analyse des poignets** — des IMU Witmotion, ou un capteur HackMotion wG3, pour mesurer la
  flexion, l’extension et l’armement du poignet directeur (lead wrist) tout au long du swing.
- **Launch monitors** — Foresight GCQuad (via FSX2020), ainsi que tout appareil disposant d’une
  passerelle GSPro Open Connect (Garmin R10, Rapsodo MLM2PRO, SkyTrak+, entre autres). Les mesures
  sont associées au swing qui les a produites.
- **Revue** — lecture synchronisée de toutes les caméras avec incrustations, graphiques et frise
  chronologique des phases, ainsi qu’une bibliothèque de swings qui conserve chaque séance sur
  disque.
- **Annotation de la vérité terrain** — étiquetage des P-positions à l’image près, directement
  dans l’application, pour valider et ajuster l’analyse.
- **Voix** — reconnaissance vocale (whisper.cpp) et synthèse vocale (Kokoro) en local, avec repli
  facultatif sur des services cloud.
- **Priorité au local et respect de la vie privée** — l’analyse s’exécute sur la machine, avec
  accélération GPU lorsqu’elle est disponible (CUDA, Vulkan, CoreML, Metal). Il n’y a aucune
  télémétrie, et les services cloud restent désactivés tant que vous ne les configurez pas.
- **Mises à jour intégrées** — installateurs signés pour Windows et macOS, qui se mettent à jour
  sur place. Une AppImage Linux capable de se mettre à jour elle-même est en préparation ; d’ici
  là, la version Linux se compile à partir des sources.

## Obtenir l’application

Les installateurs sont publiés sur la
[page Releases](https://github.com/PinPoint-Golf/PinPointStudio/releases). Pour compiler à partir
des sources, voir **[BUILDING.md](BUILDING.md)**.

## Organisation du dépôt

| Chemin | Contenu |
|---|---|
| `src/` | L’application, un dossier par sous-système (voir ci-dessous) |
| `tests/` | Le build qui regroupe tous les tests. Chaque suite se trouve à côté de son code, dans `src/<area>/tests/` |
| `tools/` | Outils hors ligne et de laboratoire : SwingLab (rejoue le pipeline d’analyse sur des swings enregistrés), labs dédiés au shaft, à la balle et à l’impact, launch monitors factices, scripts de packaging |
| `packaging/` | Scripts de l’installateur Windows et de l’appcast, ressources de packaging macOS |
| `cmake/` | Modules de build : détection de Qt, module QML, polices, OpenSSL, signature de code |
| `third_party/` | Petites sources tierces intégrées au dépôt (le filtre d’orientation ESKF) |
| `docs/` | Documentation, organisée par public (voir ci-dessous) |

Dans `src/` :

| Dossier | Sous-système |
|---|---|
| `Gui/` | L’interface Qt Quick / QML et ses modèles C++ |
| `Video/`, `Pose/` | Backends de caméra ; modèles de pose, de balle et de segmentation des personnes |
| `Analysis/`, `Metrics/`, `Diagnostics/` | Pipeline d’analyse du swing, catalogue de métriques, diagnostic des défauts |
| `Buffer/` | Le tampon d’événements sans verrou qui aligne temporellement tous les capteurs |
| `IMU/`, `LaunchMonitor/`, `Ppcp/`, `Audio/` | Capteurs, launch monitors, capture par téléphone, microphones et son des coups |
| `Export/` | La vidéo du swing et le document `swing.json` |
| `STT/`, `TTS/`, `LLM/`, `Secrets/` | Parole, coach IA, stockage des clés d’API |
| `Update/`, `Core/`, `Resources/`, `Shaders/` | Mises à jour intégrées, infrastructure commune, ressources embarquées, shaders GPU |

## Documentation

| Emplacement | Contenu |
|---|---|
| [BUILDING.md](BUILDING.md) | Dépendances, compilation sur chaque plateforme, packaging, exécution des tests |
| [`docs/user/`](docs/user) | Utilisation de l’application : [guide des diagnostics](docs/user/pinpoint-diagnostics-guide.md), [calibration des poignets](docs/user/wristcalibration.md), [capture par téléphone](docs/user/phone_capture.md), conception UX |
| [`docs/developer/`](docs/developer) | Guides du développeur : [pipeline d’analyse](docs/developer/analysis_pipeline_developer_guide.md), [tests](docs/developer/testing_developer_guide.md), [SwingLab](docs/developer/swinglab_developer_guide.md), [bascules de fonctionnalités](docs/developer/feature_switches_developer_guide.md), [traductions](docs/developer/translations_developer_guide.md), tampon d’événements, diagnostics, métriques |
| [`docs/design/`](docs/design) | Dossiers de conception de chaque sous-système, et le [système de design QML](docs/design/pinpoint_qml_design_system.md) |
| [`docs/reference/`](docs/reference) | Formats de fichiers ([`swing.json`](docs/reference/swing_json_schema.md), [dossiers de swing](docs/reference/swing_folder_layout.md)), conventions de signe, protocoles des appareils, données normatives |
| [`docs/validation/`](docs/validation), [`docs/research/`](docs/research) | Protocoles de validation, résultats sur corpus et notes de recherche |
| `docs/implementation/` | Plans de travail et procédures de publication des versions |

## Projets associés

Ces dépôts frères sont utilisés comme dépendances :

- [libppcp](https://github.com/PinPoint-Golf/libppcp) — le PinPoint Capture Protocol, partagé avec l’application pour téléphone
- [PinPoint Capture](https://github.com/PinPoint-Golf/PinPointCapture) — l’application de capture pour iPhone
- [libwrist](https://github.com/PinPoint-Golf/libwrist) — pilote du capteur HackMotion wG3
- [libgspro](https://github.com/PinPoint-Golf/libgspro) — serveur GSPro Open Connect

## Licence

PinPoint Studio est publié sous la GNU General Public License, version 2 ou ultérieure. Les
binaires distribués sont fournis sous GPL-3.0 en raison des composants auxquels ils sont liés.
Voir [LICENSE](LICENSE), ainsi que [LICENSEDEPS.md](LICENSEDEPS.md) pour chaque composant tiers
et ses conditions.
