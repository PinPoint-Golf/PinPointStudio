# Shell

*Directory:* `src/Gui/shell/`  
*11 components · 2,663 lines of QML when catalogued; `ScreenPlaceholder` (the holding screens for the old Swing/GRF/Coach modes) has since been deleted*

The application window and chrome: Main.qml, rail, header, about dialog, update banner, detect cluster, analysing badge, and the dev-hatch PlayPage.

**C++ in this area:** `NavigationController`  (see [cpp-seam.md](../cpp-seam.md))

| Component | Lines | Root | Used by | Uses | Notes |
|---|---:|---|---:|---:|---|
| [MacAboutMenu](#macaboutmenu) | 53 | `Labs.MenuBar` | 1 | 0 |  |
| [Main](#main) | 910 | `ApplicationWindow` | 2 | 22 |  |
| [PlayPage](#playpage) | 75 | `Item` | 1 | 4 |  |
| [PpAboutDialog](#ppaboutdialog) | 270 | `Popup` | 1 | 3 |  |
| [PpAnalysingBadge](#ppanalysingbadge) | 163 | `Rectangle` | 1 | 0 |  |
| [PpDetectCluster](#ppdetectcluster) | 259 | `Item` | 1 | 1 | 1 local comp. |
| [PpHeader](#ppheader) | 296 | `Item` | 1 | 4 |  |
| [PpRail](#pprail) | 227 | `Item` | 1 | 3 |  |
| [PpRailButton](#pprailbutton) | 137 | `Item` | 1 | 1 |  |
| [PpUpdateBanner](#ppupdatebanner) | 184 | `Item` | 1 | 1 |  |

### MacAboutMenu
`src/Gui/shell/MacAboutMenu.qml` · 53 lines · root `Labs.MenuBar`

> macOS ONLY. Loaded by Main.qml exclusively on macOS via a Loader — this file's
> Qt.labs.platform import and the native menu bar it installs must never be
> instantiated on Linux/Windows, where the header version pill is the About trigger.
> Provides the standard macOS application-menu items. Items carry native roles, so
> Qt relocates them into the bold application (app-name) menu automatically:
> • AboutRole → "About PinPoint Studio" → emits aboutRequested()
> • QuitRole  → "Quit PinPoint Studio"  → emits quitRequested() (Main.qml routes
> it through window.close() so the session-active confirm still fires).

- **Signals:** `aboutRequested()`, `quitRequested()`
- **Used by (1):** `Main`

### Main
`src/Gui/shell/Main.qml` · 910 lines · root `ApplicationWindow`

The application window (ApplicationWindow, created hidden then positioned on the chosen display): rail + header + screen stack (Home, Swing, Wrist, Ground forces, Coach, Developer, athletes, System, Settings), session-in-progress quit confirmation, about dialog, update banner, notification host, cast window and debounced geometry persistence.  
*(description written for this catalogue — the file has no header comment)*

- **Properties:** `readonly bool sessionScreenActive`, `readonly int screenHome`, `readonly int screenSwing`, `readonly int screenWrist`, `readonly int screenGrf`, `readonly int screenCoach`, `readonly int screenPlay`, `readonly int screenNewAthlete`, `readonly int screenAthletes`, `readonly int screenSystem`, `readonly int screenSettings`, `readonly int screenWizard`, `readonly int screenSwingDiagnostics`, `readonly var screenNames`
- **Functions:** `toggleFullscreen()`
- **Uses:** `MacAboutMenu`, `MetricRoute`, `PlayPage`, `PpAboutDialog`, `PpHeader`, `PpNotificationHost`, `PpPressable`, `PpRail`, `PpSessionDiagnosticsWindow`, `PpUpdateBanner`, `ScreenAthleteForm`, `ScreenAthletePicker`, `ScreenHome`, `ScreenResourceMonitor`, `ScreenSessionSetup` (+6)
- **Used by (2):** `DisplaysPanel`, `SettingsIndex`
- **Exercised by tests/tools:** `setup_walk`

### PlayPage
`src/Gui/shell/PlayPage.qml` · 75 lines · root `Item`

Developer-hatch container (Main.qml: 'screenPlay — dev-hatch only') with a TabBar over CapturePage (IMU), AudioPage, CoachPage (AI) and VideoPage (Pose).  
*(description written for this catalogue — the file has no header comment)*

- **Uses:** `AudioPage`, `CapturePage`, `CoachPage`, `VideoPage`
- **Used by (1):** `Main`

### PpAboutDialog
`src/Gui/shell/PpAboutDialog.qml` · 270 lines · root `Popup`

> About PinPoint Studio — modal dialog showing the icon, "PinPoint Studio for
> <OS>", the version + build stats, and the bundled library versions. It surfaces
> the updater state and lets the user check, download and restart into an update —
> reusing the existing `updateController` (the same surface the Settings → General
> version row drives). Opening the dialog never checks on its own: a check only
> happens when the user presses "Check for updates". Unlike the Settings row the
> update block is shown on every platform — a devbuild or an unsupported build gets
> the badge saying so and a disabled button, rather than a blank gap that reads as a
> missing control. Opened from the header version pill (all platforms) and the macOS
> application menu.

- **Properties:** `readonly string uState`, `readonly var pal`
- **Uses:** `PpButton`, `PpDisplayText`, `PpDivider`
- **Used by (1):** `Main`

### PpAnalysingBadge
`src/Gui/shell/PpAnalysingBadge.qml` · 163 lines · root `Rectangle` · 1 hard-coded colour(s)

> Post-shot ANALYSING badge — label + elapsed time on the top line, a filling
> progress bar (sweeping sheen + leading-edge twinkles) beneath. Lifted out of
> PpSessionToolbar so the title bar (PpHeader) can host it in the centre slot,
> taking the DETECT cluster's place while a shot is processed (post-roll +
> analysis + export; it hides again once the on-screen replay runs). Pure
> presentation: the PLACEMENT
> SITE owns the visibility gate. All motion is gated on Theme.reduceMotion; the
> plain filling bar remains. Reads the global shotProcessor context property.

- **Properties:** `readonly bool sparkling`, `int elapsedS`, `readonly string elapsedLabel`
- **Used by (1):** `PpHeader`

### PpDetectCluster
`src/Gui/shell/PpDetectCluster.qml` · 259 lines · root `Item` · 3 hard-coded colour(s)

> Shot-detection indicator cluster — the "DETECT" label plus one dot per
> detection modality (IMU impact + acoustic onset today; Ball/vision reserved).
> Hosted centred in the title bar (PpHeader) during a live Capture session.
> This cluster is ALSO the manual shot trigger — it REPLACES the old toolbar
> SHOT button. A frame fades in on hover (armed only) and a click fires
> shotController.triggerShot() with the same brief accent border-flash the SHOT
> button used. `armed` tracks shotController.armed (buffer capturing + processor
> idle), so a disarmed click is a no-op and the hover frame / pointer cursor do
> not appear. The placement site still owns the OUTER visibility gate (session
> screen + Capture mode). Reads the global context props imuManager /
> cameraManager / shotController / controller / appSettings, plus the Theme and
> SessionMode singletons.
> Dot tiers: even-more-dim (auto-detect off, or the modality is unavailable —
> e.g. the Ball placeholder / no IMU connected), dim (armed pipeline idle), glow
> (armed & listening, with a breathing halo). A detector firing flashes its dot
> green and decays back to the base tier over 2 s.
> …

- **Properties:** `readonly bool armed`, `readonly bool hovered`, `bool lmReceived`
- **Local components:** `DetectDot`
- **Uses:** `PpPressable`
- **Used by (1):** `PpHeader`

### PpHeader
`src/Gui/shell/PpHeader.qml` · 296 lines · root `Item`

Top header bar: screen name after a separator, back/forward, version pill (About on Linux/Windows), analysing badge, and a centred device-detect cluster shown only on session screens; fullscreen toggle.  
*(description written for this catalogue — the file has no header comment)*

- **Properties:** `string screenName`, `bool showVersionPill`, `bool isFullscreen`, `bool sessionScreenActive`, `bool backEnabled`, `bool forwardEnabled`
- **Signals:** `fullscreenToggleRequested()`, `backRequested()`, `forwardRequested()`, `aboutRequested()`, `closeRequested()`
- **Uses:** `PpAnalysingBadge`, `PpDetectCluster`, `PpPressable`, `SessionMode`
- **Used by (1):** `Main`

### PpRail
`src/Gui/shell/PpRail.qml` · 227 lines · root `Item`

Left navigation rail (Home, Swing, Wrist, GRF, Coach, Settings) of PpRailButtons plus the athlete avatar; locks to System and Settings only while a session screen is active.  
*(description written for this catalogue — the file has no header comment)*

- **Properties:** `int currentPageIndex`, `bool locked`, `int sessionLockIndex`, `readonly bool anyLocked`
- **Signals:** `pageRequested(int index)`, `avatarClicked()`
- **Uses:** `PpDivider`, `PpPressable`, `PpRailButton`
- **Used by (1):** `Main`

### PpRailButton
`src/Gui/shell/PpRailButton.qml` · 137 lines · root `Item`

One rail button: icon and label, active/muted/attention states.  
*(description written for this catalogue — the file has no header comment)*

- **Properties:** `string iconText`, `string labelText`, `bool isActive`, `bool isMuted`, `bool attention`
- **Signals:** `clicked()`
- **Uses:** `PpPressable`
- **Used by (1):** `PpRail`

### PpUpdateBanner
`src/Gui/shell/PpUpdateBanner.qml` · 184 lines · root `Item` · 1 hard-coded colour(s)

> Non-modal, dismissible launch banner for the Linux in-app updater (design §5,
> surface A). Reads the global context properties (updateController, appSettings,
> sessionController) directly. Suppressed during a session and for skipped
> versions; the Settings → General Version row remains the always-available
> surface B. Place anchored at the top of the window in Main.qml.

- **Properties:** `bool dismissedThisRun`, `bool showNotes`, `bool allowed`, `readonly string uState`, `readonly bool offerFresh`, `readonly bool shouldShow`, `readonly bool isReady`, `readonly bool isWorking`
- **Uses:** `PpButton`
- **Used by (1):** `Main`

