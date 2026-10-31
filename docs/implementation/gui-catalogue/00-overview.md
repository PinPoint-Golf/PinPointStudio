# GUI layer — overview

Generated from the repository at the time of the run (see README for the commit). All numbers below are measured, not estimated.

## Size

| | Files | Lines |
|---|---:|---:|
| App QML (shipped module `PinPointStudio`) | 340 | 104,984 |
| App QML helper `.js` | 5 | 968 |
| GUI-layer C++ (`src/Gui/**`, excluding tests) | 205 | 98,141 |
| Test QML + probes | 112 | 31,448 |
| Developer-tool QML | 20 | 5,030 |

## How the layer is put together

- **One flat QML module.** Every app `.qml` file is listed in `cmake/PinPointQmlModule.cmake` (`PP_QML_FILES`) and compiled into a single module, `import PinPointStudio`. There are no per-area sub-modules, so a type name is global: two files cannot share a name, and any file can instantiate any other without an import. The directory layout is a convention, not an encapsulation boundary.
- **Singletons** (`pragma Singleton`, and also listed in `PP_QML_SINGLETONS`): `AnnotationTool`, `ClubFormat`, `DataWarningFormat`, `ImuMounts`, `MetricRoute`, `SessionMode`, `SettingsIndex`, `Theme`, `TrustFocus`, `ViewLayout`.
- **Two parallel vocabularies, not a stack.** `Theme` sits under everything. On top of it are (a) the *older* shared components in `components/` (buttons, chips, pills, badges, cards, toasts — the coaching-card look, design system §13) used by Settings, Home, Athlete, Setup, Monitor, Shell and the session toolbar, and (b) the *typeset* system (`typeset/` + the role objects `PpTypeset`/`PpDiagRoles`/`PpChRoles`, design system §15), used by the Charts, Session-diagnostics and Trust panels. Measured: the typeset-built areas use only 2 of the 46 `components/` files (`PpCardShell`, `PpMicro`) — they bring their own text, marks, tabs and plots. The session report (`report/`) draws on both: 10 `components/` files, and typeset roles on some pages. The design system calls typeset the target for every other stage panel and, later, whole screens (§15). Feature blocks and screens (`Screen*`, `Main.qml`) sit above both.
- **C++ ↔ QML seam.** Application state reaches QML two ways: 43 *context properties* set in `src/Gui/main.cpp` (globals such as `appSettings`, `shotReplay`, `cameraManager`, `controller`), and 31 `QML_ELEMENT` declarations registered through the module. See [cpp-seam.md](cpp-seam.md). QML files reach for the globals by bare name, not through properties, so a component's real inputs are not visible from its API.
- **Local (inline) components.** Files may declare `component Foo: …` inside themselves; there are 142 across 55 files. They are invisible to the module (not instantiable elsewhere) and are where most re-implementation of shared primitives hides — see [duplicates.md](duplicates.md).

## Areas

| # | Area | Components | QML lines | Doc |
|---:|---|---:|---:|---|
| 01 | Theme | 1 | 939 | [01-theme.md](areas/01-theme.md) |
| 02 | Shared components | 46 | 4,890 | [02-components.md](areas/02-components.md) |
| 03 | Typeset | 18 | 2,253 | [03-typeset.md](areas/03-typeset.md) |
| 04 | Shell | 10 | 2,604 | [04-shell.md](areas/04-shell.md) |
| 05 | Home | 13 | 3,831 | [05-home.md](areas/05-home.md) |
| 06 | Athlete | 6 | 2,472 | [06-athlete.md](areas/06-athlete.md) |
| 07 | Session setup | 23 | 5,051 | [07-setup.md](areas/07-setup.md) |
| 08 | Calibration | 7 | 3,732 | [08-calibration.md](areas/08-calibration.md) |
| 09 | Session screens | 13 | 7,879 | [09-session.md](areas/09-session.md) |
| 10 | Cameras | 8 | 4,454 | [10-cameras.md](areas/10-cameras.md) |
| 11 | IMU | 3 | 1,028 | [11-imu.md](areas/11-imu.md) |
| 12 | Shots | 9 | 3,026 | [12-shot.md](areas/12-shot.md) |
| 13 | Review (replay/analyse) | 15 | 6,706 | [13-review.md](areas/13-review.md) |
| 14 | Charts panel | 55 | 9,641 | [14-charts.md](areas/14-charts.md) |
| 15 | Session diagnostics | 33 | 7,792 | [15-diagnostics.md](areas/15-diagnostics.md) |
| 16 | Diagnostic Model editor | 18 | 9,421 | [16-diagnosticmodel.md](areas/16-diagnosticmodel.md) |
| 17 | Trust and gates | 7 | 707 | [17-trust.md](areas/17-trust.md) |
| 18 | Resource monitor | 10 | 4,250 | [18-monitor.md](areas/18-monitor.md) |
| 19 | Settings | 13 | 13,661 | [19-settings.md](areas/19-settings.md) |
| 20 | Media (dev hatch) | 3 | 1,495 | [20-media.md](areas/20-media.md) |
| 21 | 3-D body/IMU views | 4 | 1,721 | [21-viz.md](areas/21-viz.md) |
| 22 | Swing 3-D | 2 | 701 | [22-swing3d.md](areas/22-swing3d.md) |
| 23 | Print | 1 | 188 | [23-print.md](areas/23-print.md) |
| 24 | Session report | 22 | 6,542 | [24-report.md](areas/24-report.md) |

## Naming conventions (as found)

| Prefix / pattern | Meaning | Count |
|---|---|---:|
| `Pp*` — shared/panel component (house prefix) | | 183 |
| other (unprefixed) | | 62 |
| `PpCh*` — Charts panel blocks | | 45 |
| `Model*` — Diagnostic Model editor | | 16 |
| `Rm*` — Resource Monitor | | 9 |
| `Setup*` — session-setup parts/engine | | 9 |
| `Screen*` — a full screen | | 8 |
| `Hm*` — Home area | | 8 |

## Most reused components (fan-in from app files)

| Component | Area | Used by (files) | Lines |
|---|---|---:|---:|
| `Theme` | theme | 201 | 939 |
| `PpPressable` | components | 66 | 97 |
| `PpMicro` | components | 39 | 31 |
| `PpButton` | components | 34 | 112 |
| `PpDisplayText` | components | 24 | 102 |
| `PpDiagRoles` | diagnostics | 24 | 249 |
| `SessionMode` | session | 24 | 174 |
| `PpTypeset` | typeset | 20 | 319 |
| `PpTextField` | components | 19 | 56 |
| `PpDivider` | components | 18 | 32 |
| `PpCardNote` | components | 17 | 32 |
| `PpComboBox` | components | 16 | 135 |
| `TrustFocus` | trust | 16 | 47 |
| `PpChText` | charts | 14 | 41 |
| `PpMark` | typeset | 13 | 86 |
| `PpChBoxCell` | charts | 12 | 124 |
| `PpChip` | components | 12 | 63 |
| `PpPopoverCard` | components | 12 | 29 |
| `WizardPage` | setup | 12 | 71 |
| `PpTrustInlineFor` | trust | 12 | 63 |
| `ViewLayout` | session | 11 | 529 |
| `PpBadge` | components | 10 | 166 |
| `PpLink` | components | 9 | 51 |
| `ClubFormat` | components | 8 | 38 |
| `PpSegmentedControl` | components | 8 | 83 |

## Largest components

| Component | Area | Lines | Local components |
|---|---|---:|---:|
| `CamerasPanel` | settings | 3,150 | 4 |
| `PpLmGraphicsBody` | session | 2,704 | 28 |
| `ScreenResourceMonitor` | monitor | 2,390 | 0 |
| `PpCameraFrame` | cameras | 2,185 | 1 |
| `ModelGraph` | diagnosticmodel | 2,169 | 3 |
| `DiagnosticModel` | diagnosticmodel | 2,031 | 0 |
| `ImusPanel` | settings | 1,700 | 2 |
| `PpMetricChart` | review | 1,522 | 2 |
| `GeneralPanel` | settings | 1,222 | 2 |
| `ModelInspector` | diagnosticmodel | 1,153 | 0 |
| `PpMarkupPanel` | session | 1,134 | 2 |
| `StoragePanel` | settings | 1,093 | 1 |
| `ScreenAthleteForm` | athlete | 1,084 | 0 |
| `AppearancePanel` | settings | 1,020 | 0 |
| `PpChartPlot` | review | 1,008 | 0 |
| `BodyVizView` | viz | 1,001 | 2 |
| `Theme` | theme | 939 | 0 |
| `StudioPanel` | settings | 931 | 7 |
| `Main` | shell | 908 | 0 |
| `TrunkRoutine` | calibration | 888 | 0 |

## Files nothing in the app instantiates

4 app files are never instantiated by another app QML file. Each was searched for under `src/`, `tools/`, `tests/` and `cmake/` to separate *dead* from *staged/test-only*:

| File | Lines | Other mentions found |
|---|---:|---|
| `home/PpcpGuidedPairDialog.qml` | 377 | reached only from tests/probes (cmake list: 1, test/probe: 1) |
| `media/FilmPage.qml` | 597 | only in the cmake file list (cmake list: 1) |
| `review/MetricLibrary.qml` | 335 | only in the cmake file list (cmake list: 1) |
| `trust/PpGateNote.qml` | 75 | only in the cmake file list (cmake list: 1) |

## Theme bypass

119 of the larger components (>30 code lines) have no direct `Theme.*` reference — they draw through a `roles`/typeset object (Charts, Diagnostics, Trust) or are logic-only. Components with hard-coded colour literals (`#rrggbb`, `Qt.rgba`) instead of `Theme.*` tokens:

| Component | Hard-coded colours | Theme refs |
|---|---:|---:|
| `ScreenResourceMonitor` | 22 | 933 |
| `PpCameraFrame` | 17 | 112 |
| `CamerasPanel` | 17 | 743 |
| `FilmPage` | 12 | 200 |
| `PpAthleteCard` | 9 | 27 |
| `PpAthleteRow` | 9 | 45 |
| `RmAnalysisRunRow` | 9 | 106 |
| `AudioPage` | 8 | 152 |
| `ImusPanel` | 8 | 515 |
| `CoachPage` | 7 | 122 |
| `NormativeBar` | 7 | 35 |
| `PpShotCard` | 7 | 70 |
| `SwingViz3DView` | 6 | 55 |
| `PpLmGraphicsBody` | 5 | 22 |
| `PpTopoBackground` | 4 | 0 |
