# GUI layer — overview

Generated from the repository at the time of the run (see README for the commit). All numbers below are measured, not estimated.

## Size

| | Files | Lines |
|---|---:|---:|
| App QML (shipped module `PinPointStudio`) | 290 | 90,468 |
| App QML helper `.js` | 3 | 618 |
| GUI-layer C++ (`src/Gui/**`, excluding tests) | 157 | 73,234 |
| Test QML + probes | 80 | 22,714 |
| Developer-tool QML | 15 | 3,879 |

## How the layer is put together

- **One flat QML module.** Every app `.qml` file is listed in `cmake/PinPointQmlModule.cmake` (`PP_QML_FILES`) and compiled into a single module, `import PinPointStudio`. There are no per-area sub-modules, so a type name is global: two files cannot share a name, and any file can instantiate any other without an import. The directory layout is a convention, not an encapsulation boundary.
- **Singletons** (`pragma Singleton`, and also listed in `PP_QML_SINGLETONS`): `AnnotationTool`, `ClubFormat`, `DataWarningFormat`, `ImuMounts`, `MetricRoute`, `SessionMode`, `SettingsIndex`, `Theme`, `TrustFocus`, `ViewLayout`.
- **Two parallel vocabularies, not a stack.** `Theme` sits under everything. On top of it are (a) the *older* shared components in `components/` (buttons, chips, pills, badges, cards, toasts — the coaching-card look, design system §13) used by Settings, Home, Athlete, Setup, Monitor, Shell and the session toolbar, and (b) the *typeset* system (`typeset/` + the role objects `PpTypeset`/`PpDiagRoles`/`PpChRoles`, design system §15), used by the Charts, Session-diagnostics and Trust panels. Measured: the typeset-built areas use only 2 of the 37 `components/` files (`PpCardShell`, `PpMicro`) — they bring their own text, marks, tabs and plots. The design system calls typeset the target for every other stage panel and, later, whole screens (§15). Feature blocks and screens (`Screen*`, `Main.qml`) sit above both.
- **C++ ↔ QML seam.** Application state reaches QML two ways: 39 *context properties* set in `src/Gui/main.cpp` (globals such as `appSettings`, `shotReplay`, `cameraManager`, `controller`), and 27 `QML_ELEMENT` declarations registered through the module. See [cpp-seam.md](cpp-seam.md). QML files reach for the globals by bare name, not through properties, so a component's real inputs are not visible from its API.
- **Local (inline) components.** Files may declare `component Foo: …` inside themselves; there are 120 across 46 files. They are invisible to the module (not instantiable elsewhere) and are where most re-implementation of shared primitives hides — see [duplicates.md](duplicates.md).

## Areas

| # | Area | Components | QML lines | Doc |
|---:|---|---:|---:|---|
| 01 | Theme | 1 | 881 | [01-theme.md](areas/01-theme.md) |
| 02 | Shared components | 37 | 3,421 | [02-components.md](areas/02-components.md) |
| 03 | Typeset | 18 | 2,113 | [03-typeset.md](areas/03-typeset.md) |
| 04 | Shell | 11 | 2,663 | [04-shell.md](areas/04-shell.md) |
| 05 | Home | 11 | 3,756 | [05-home.md](areas/05-home.md) |
| 06 | Athlete | 5 | 1,914 | [06-athlete.md](areas/06-athlete.md) |
| 07 | Session setup | 21 | 4,308 | [07-setup.md](areas/07-setup.md) |
| 08 | Calibration | 6 | 2,772 | [08-calibration.md](areas/08-calibration.md) |
| 09 | Session screens | 14 | 7,906 | [09-session.md](areas/09-session.md) |
| 10 | Cameras | 8 | 4,402 | [10-cameras.md](areas/10-cameras.md) |
| 11 | IMU | 3 | 1,029 | [11-imu.md](areas/11-imu.md) |
| 12 | Shots | 9 | 2,809 | [12-shot.md](areas/12-shot.md) |
| 13 | Review (replay/analyse) | 17 | 7,096 | [13-review.md](areas/13-review.md) |
| 14 | Charts panel | 39 | 6,923 | [14-charts.md](areas/14-charts.md) |
| 15 | Session diagnostics | 32 | 7,373 | [15-diagnostics.md](areas/15-diagnostics.md) |
| 16 | Diagnostic Model editor | 19 | 9,488 | [16-diagnosticmodel.md](areas/16-diagnosticmodel.md) |
| 17 | Trust and gates | 7 | 702 | [17-trust.md](areas/17-trust.md) |
| 18 | Resource monitor | 10 | 4,250 | [18-monitor.md](areas/18-monitor.md) |
| 19 | Settings | 12 | 12,716 | [19-settings.md](areas/19-settings.md) |
| 20 | Media (dev hatch) | 3 | 1,495 | [20-media.md](areas/20-media.md) |
| 21 | 3-D body/IMU views | 4 | 1,600 | [21-viz.md](areas/21-viz.md) |
| 22 | Swing 3-D | 2 | 699 | [22-swing3d.md](areas/22-swing3d.md) |
| 23 | Print | 1 | 152 | [23-print.md](areas/23-print.md) |

## Naming conventions (as found)

| Prefix / pattern | Meaning | Count |
|---|---|---:|
| `Pp*` — shared/panel component (house prefix) | | 153 |
| other (unprefixed) | | 58 |
| `PpCh*` — Charts panel blocks | | 29 |
| `Model*` — Diagnostic Model editor | | 17 |
| `Screen*` — a full screen | | 10 |
| `Rm*` — Resource Monitor | | 9 |
| `Setup*` — session-setup parts/engine | | 9 |
| `Hm*` — Home area | | 5 |

## Most reused components (fan-in from app files)

| Component | Area | Used by (files) | Lines |
|---|---|---:|---:|
| `Theme` | theme | 180 | 881 |
| `PpPressable` | components | 62 | 97 |
| `PpMicro` | components | 37 | 31 |
| `PpButton` | components | 27 | 112 |
| `PpDiagRoles` | diagnostics | 24 | 229 |
| `SessionMode` | session | 24 | 157 |
| `PpDisplayText` | components | 22 | 102 |
| `PpCardNote` | components | 19 | 32 |
| `PpTypeset` | typeset | 18 | 300 |
| `PpDivider` | components | 17 | 32 |
| `PpTextField` | components | 17 | 56 |
| `TrustFocus` | trust | 16 | 47 |
| `PpComboBox` | components | 15 | 135 |
| `PpMark` | typeset | 13 | 86 |
| `PpBadge` | components | 12 | 130 |
| `PpTrustInlineFor` | trust | 12 | 63 |
| `PpChip` | components | 11 | 53 |
| `ViewLayout` | session | 11 | 487 |
| `PpChText` | charts | 10 | 41 |
| `WizardPage` | setup | 10 | 71 |
| `PpPopoverCard` | components | 9 | 29 |
| `ClubFormat` | components | 8 | 38 |
| `PpCardShell` | components | 7 | 84 |
| `PpCameraFrame` | cameras | 6 | 2133 |
| `PpChBoxCell` | charts | 6 | 124 |

## Largest components

| Component | Area | Lines | Local components |
|---|---|---:|---:|
| `CamerasPanel` | settings | 3,150 | 4 |
| `PpLmGraphicsBody` | session | 2,653 | 28 |
| `ScreenResourceMonitor` | monitor | 2,390 | 0 |
| `ModelGraph` | diagnosticmodel | 2,169 | 3 |
| `PpCameraFrame` | cameras | 2,133 | 1 |
| `DiagnosticModel` | diagnosticmodel | 1,994 | 0 |
| `ImusPanel` | settings | 1,827 | 2 |
| `PpMetricChart` | review | 1,550 | 2 |
| `GeneralPanel` | settings | 1,162 | 2 |
| `PpMarkupPanel` | session | 1,135 | 2 |
| `StoragePanel` | settings | 1,093 | 1 |
| `ModelInspector` | diagnosticmodel | 1,078 | 0 |
| `AppearancePanel` | settings | 1,020 | 0 |
| `PpChartPlot` | review | 1,008 | 0 |
| `Main` | shell | 910 | 0 |
| `PpSessionToolbar` | session | 882 | 1 |
| `Theme` | theme | 881 | 0 |
| `BodyVizView` | viz | 880 | 2 |
| `PhonesPanel` | settings | 862 | 1 |
| `DisplaysPanel` | settings | 847 | 0 |

## Files nothing in the app instantiates

8 app files are never instantiated by another app QML file. Each was searched for under `src/`, `tools/`, `tests/` and `cmake/` to separate *dead* from *staged/test-only*:

| File | Lines | Other mentions found |
|---|---:|---|
| `diagnostics/PpDiagPrint.qml` | 292 | reached only from tests/probes (cmake list: 1, test/probe: 1) |
| `home/PpcpGuidedPairDialog.qml` | 377 | reached only from tests/probes (cmake list: 1, test/probe: 1) |
| `media/FilmPage.qml` | 597 | only in the cmake file list (cmake list: 1) |
| `print/PpPdfPrinter.qml` | 152 | reached only from tests/probes (cmake list: 1, test/probe: 2) |
| `review/MetricLibrary.qml` | 335 | only in the cmake file list (cmake list: 1) |
| `review/PpReplayCharts.qml` | 74 | only in the cmake file list (cmake list: 1) |
| `session/ScreenSessionMode.qml` | 202 | only in the cmake file list (cmake list: 1) |
| `trust/PpGateNote.qml` | 75 | only in the cmake file list (cmake list: 1) |

## Theme bypass

91 of the larger components (>30 code lines) have no direct `Theme.*` reference — they draw through a `roles`/typeset object (Charts, Diagnostics, Trust) or are logic-only. Components with hard-coded colour literals (`#rrggbb`, `Qt.rgba`) instead of `Theme.*` tokens:

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
| `ImusPanel` | 8 | 513 |
| `CoachPage` | 7 | 122 |
| `NormativeBar` | 7 | 35 |
| `SwingViz3DView` | 6 | 55 |
| `PpLmGraphicsBody` | 5 | 25 |
| `PpTopoBackground` | 4 | 0 |
| `ScreenHome` | 4 | 160 |
