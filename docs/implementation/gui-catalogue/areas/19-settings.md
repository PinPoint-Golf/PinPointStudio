# Settings

*Directory:* `src/Gui/settings/`  
*12 components · 12,716 lines of QML*

The Settings screen, its search index singleton and the panels: General, Appearance, Displays, Cameras, IMUs, Phones, Microphones, Launch Monitor, Storage, Archiving.

**C++ in this area:** `ArchiveController`, `LibraryConverter`  (see [cpp-seam.md](../cpp-seam.md))

| Component | Lines | Root | Used by | Uses | Notes |
|---|---:|---|---:|---:|---|
| [AppearancePanel](#appearancepanel) | 1020 | `Item` | 1 | 6 |  |
| [ArchivingPanel](#archivingpanel) | 348 | `Item` | 1 | 5 | 3 local comp. |
| [CamerasPanel](#cameraspanel) | 3150 | `Item` | 1 | 8 | 4 local comp. |
| [DisplaysPanel](#displayspanel) | 847 | `Item` | 1 | 5 |  |
| [GeneralPanel](#generalpanel) | 1162 | `Item` | 1 | 8 | 2 local comp. |
| [ImusPanel](#imuspanel) | 1827 | `Item` | 1 | 8 | 2 local comp. |
| [LaunchMonitorPanel](#launchmonitorpanel) | 583 | `Item` | 1 | 5 | 1 local comp. |
| [MicrophonesPanel](#microphonespanel) | 787 | `Item` | 1 | 3 | 2 local comp. |
| [PhonesPanel](#phonespanel) | 862 | `Item` | 1 | 3 | 1 local comp. |
| [ScreenSettings](#screensettings) | 592 | `Item` | 1 | 13 |  |
| [SettingsIndex](#settingsindex) | 445 | `QtObject` | 1 | 1 | singleton |
| [StoragePanel](#storagepanel) | 1093 | `Item` | 1 | 5 | 1 local comp. |

### AppearancePanel
`src/Gui/settings/AppearancePanel.qml` · 1020 lines · root `Item` · 2 hard-coded colour(s)

Settings > Appearance: aesthetic and colour-mode gallery (each card previews its own theme from Theme.themeInfo), per-theme metric-colour editors with reset, and type scale / text size.  
*(description written for this catalogue — the file has no header comment)*

- **Properties:** `readonly var themeData`, `string lastHighlightId`
- **Functions:** `findChild(parent, name)`, `scrollToItem(itemId)`
- **Uses:** `PpButton`, `PpChipGroup`, `PpDisplayText`, `PpDivider`, `PpPressable`, `PpTextField`
- **Used by (1):** `ScreenSettings`

### ArchivingPanel
`src/Gui/settings/ArchivingPanel.qml` · 348 lines · root `Item`

> Settings → Archiving (panelIndex 9). swing_storage_impl.md, Phase 2 stage 6.
> Space as HOURS of capture left at the swing size this library actually records; moving whole
> sessions to an archive location and back (ArchiveController → SessionArchiver: a verified copy,
> the library keeps a stub that still lists, trends and grades); the library trash; and the
> startup housekeeping, every automatic part of which is OFF until set here.
> NO FILESYSTEM WORK ON COMPLETION — this panel is built at launch (a StackLayout child; see the
> warning in StoragePanel.qml). The controller scans on a worker when the panel is first shown.
> The archive location is typed, not picked: no native dialogs in this application.

- **Properties:** `readonly bool hasController`, `string lastHighlightId`, `readonly var ageOptions`, `readonly var floorOptions`, `readonly var trashOptions`
- **Functions:** `findChild(parent, name)`, `scrollToItem(itemId)`, `formatBytes(n)`, `indexOfValue(options, v)`
- **Local components:** `TogglePill`, `SectionHead`, `RowTitle`
- **Uses:** `PpButton`, `PpComboBox`, `PpDisplayText`, `PpDivider`, `PpTextField`
- **Used by (1):** `ScreenSettings`

### CamerasPanel
`src/Gui/settings/CamerasPanel.qml` · 3150 lines · root `Item` · 17 hard-coded colour(s)

Settings > Cameras (3.1k lines, largest file in the GUI): per-camera rows (alias, enable, view assignment face-on / down-the-line / impact, mirror, frame rate), live preview, tuning (exposure/gain), ROI and ball-detection set-up, impact-camera defaults.  
*(description written for this catalogue — the file has no header comment)*

- **Properties:** `int openRoiIndex`, `int openTuningIndex`, `int openBallIndex`, `bool ballAutoConnected`, `readonly double impactDefaultExposureUs`, `readonly double impactDefaultGainDb`, `readonly double impactDefaultViewGain`, `string lastHighlightId`
- **Functions:** `closeBallPanel()`, `applyImpactMode(cameraKey, camIndex, maxW, maxH, mode, liveInstance)`, `setImpactExposure(cameraKey, us, liveInstance)`, `impactTuning(cameraKey, member, fallback)`, `setImpactTuning(cameraKey, member, value, liveInstance)`, `wideTuningKey(cameraKey)`, `wideExposureUs(cameraKey)`, `wideGainDb(cameraKey)`, `applyWideTuning(cameraKey, exposureUs, gainDb, liveInstance)`, `setWideExposure(cameraKey, us, liveInstance)` (+3)
- **Local components:** `TuneChip`, `TuneHeading`, `TogglePill`, `CameraDeviceRow`
- **Uses:** `PpButton`, `PpCameraFrame`, `PpComboBox`, `PpDisplayText`, `PpDivider`, `PpPressable`, `PpTextField`, `PpToast`
- **Used by (1):** `ScreenSettings`

### DisplaysPanel
`src/Gui/settings/DisplaysPanel.qml` · 847 lines · root `Item`

Settings > Displays: connected screens (friendly vendor names from EDID codes) and which one hosts the main Pinpoint window vs the post-shot display, and which monitor to launch on.  
*(description written for this catalogue — the file has no header comment)*

- **Properties:** `readonly var screenModel`, `string lastHighlightId`
- **Functions:** `mainDisplayIndexFor(stored)`, `secondaryDisplayIndexFor(stored)`, `findChild(parent, name)`, `scrollToItem(itemId)`
- **Uses:** `Main`, `PpChipGroup`, `PpComboBox`, `PpDisplayText`, `PpDivider`
- **Used by (1):** `ScreenSettings`

### GeneralPanel
`src/Gui/settings/GeneralPanel.qml` · 1162 lines · root `Item` · 1 hard-coded colour(s)

Settings > General: language (7 locales), measurement units, session behaviour (auto-detect swing start and sensitivity, AI coaching on session end), cloud fallback keys; lazily triggers the motion-capture hardware probe when first shown.  
*(description written for this catalogue — the file has no header comment)*

- **Properties:** `readonly var languageModel`, `bool hostVisible`, `string lastHighlightId`
- **Functions:** `findChild(parent, name)`, `scrollToItem(itemId)`
- **Local components:** `CloudFallbackRow`, `CloudKeyField`
- **Uses:** `PpButton`, `PpChipGroup`, `PpComboBox`, `PpDisplayText`, `PpDivider`, `PpPressable`, `PpTextField`, `PpUnitToggle`
- **Used by (1):** `ScreenSettings`

### ImusPanel
`src/Gui/settings/ImusPanel.qml` · 1827 lines · root `Item` · 8 hard-coded colour(s)

Settings > IMUs (1.8k lines): device rows with alias/enable/exclude, per-device test panel with live 3-D view and sensor ranges/battery/transport, placement, and who holds a device (HackMotion vs Witmotion).  
*(description written for this catalogue — the file has no header comment)*

- **Properties:** `string openTestId`, `string lastHighlightId`
- **Functions:** `findChild(parent, name)`, `scrollToItem(itemId)`
- **Local components:** `TogglePill`, `ImuDeviceRow`
- **Uses:** `ImuMounts`, `ImuVizView`, `PpComboBox`, `PpConnectingFrame`, `PpDisplayText`, `PpDivider`, `PpPressable`, `PpTextField`
- **Used by (1):** `ScreenSettings`
- **Exercised by tests/tools:** `tst_setup_mounts`

### LaunchMonitorPanel
`src/Gui/settings/LaunchMonitorPanel.qml` · 583 lines · root `Item`

> Settings → Launch Monitor (panelIndex 6).
> Connect a launch monitor and say where it writes. The first connector reads
> Foresight's LastShot.CSV out of a folder FSX2020 is pointed at.
> NO FILESYSTEM WORK ON COMPLETION. This panel is a direct StackLayout child, so it
> is built at launch whether or not anybody opens Settings — see the warning in
> StoragePanel.qml, written after a library walk on this thread cost 5-10 s of black
> window on an SMB mount. Everything shown here is a property of the controller,
> which its own poller maintains off this code path.

- **Properties:** `string lastHighlightId`, `readonly var deviceOptions`, `readonly bool chosen`, `readonly bool switchedOn`, `readonly bool configured`, `readonly bool isGcQuad`, `readonly bool isGsPro`, `readonly color statusColor`
- **Functions:** `findChild(parent, name)`, `scrollToItem(itemId)`
- **Local components:** `TogglePill`
- **Uses:** `PpButton`, `PpComboBox`, `PpDisplayText`, `PpDivider`, `PpTextField`
- **Used by (1):** `ScreenSettings`

### MicrophonesPanel
`src/Gui/settings/MicrophonesPanel.qml` · 787 lines · root `Item` · 4 hard-coded colour(s)

Settings > Microphone: input devices (rescan, system default), use-for-shot-detection toggle, distance to hitting strip, and impact-sound calibration with the 'ting' test player.  
*(description written for this catalogue — the file has no header comment)*

- **Properties:** `bool hostVisible`, `readonly bool shown`, `bool calibrating`, `readonly bool calActive`, `var devices`, `string lastHighlightId`, `int detectCount`, `real lastConfidence`, `bool pendingDet`
- **Functions:** `refreshDevices()`, `deviceIsActive(d)`, `findChild(parent, name)`, `scrollToItem(itemId)`
- **Local components:** `TogglePill`, `DeviceRow`
- **Uses:** `PpDisplayText`, `PpDivider`, `PpPressable`
- **Used by (1):** `ScreenSettings`

### PhonesPanel
`src/Gui/settings/PhonesPanel.qml` · 862 lines · root `Item`

> Settings -> Phones.  Where a remembered PPCP pairing is seen and revoked.
> ⚠ THIS IS A CONFORMANCE SURFACE AND NOT A CONVENIENCE.  RV 7.4b: persistence
> is "visible to the user, and individually revocable", and both of those are
> this panel — its whole reason to exist.  A remembered pairing is a standing
> ability to complete a handshake with this host, so it is shown rather than
> kept out of the way.  It used to be a table at the foot of the home screen;
> deleting that table without providing this would have been a regression
> against 7.4b, which is why the two changes are one commit.
> ⚠ THERE IS NO "REMEMBER" BUTTON HERE, AND THAT IS NOT AN OMISSION.  7.4b's
> third clause — persistence is opt-in — was itself downgraded to a SHOULD by
> libppcp erratum E57 (25 August 2026): a user who has just paired a device
> has already given the consent a separate opt-in step used to ask for twice,
> so `PpcpRendezvous` now remembers a pairing the moment it completes.  What
> this panel offers instead is the opt-OUT — "Forget" — which is what 7.4b's
> surviving clauses actually require: visible, and individually revocable.
> ⚠ IT USED TO CARRY A WARNING FOR WINDOWS AND LINUX, AND NO LONGER NEEDS ONE.
> …

- **Properties:** `var controller`, `readonly bool havePpcp`, `readonly bool haveCameraManager`, `readonly var rows`, `var aliasDraft`, `string aliasFocusId`, `int aliasCursor`, `bool aliasFieldFocused`, `string lastHighlightId`
- **Functions:** `noteAliasDraft(pairingId, txt, cursor)`, `clearAliasDraft(pairingId)`, `knownPairing(pairingId)`, `finishAliasEdit(pairingId, force)`, `findChild(parent, name)`, `scrollToItem(itemId)`
- **Local components:** `PhoneDeviceRow`
- **Uses:** `PpButton`, `PpDisplayText`, `PpTextField`
- **Used by (1):** `ScreenSettings`
- **Exercised by tests/tools:** `tst_ppcp_pair_dialog`

### ScreenSettings
`src/Gui/settings/ScreenSettings.qml` · 592 lines · root `Item`

The Settings screen: left navigation (General, Appearance, Displays; Hardware: Cameras, IMUs, Phones, Microphone, Launch Monitor; Data: Storage, Archiving, Diagnostic Model), search (SettingsIndex) with scroll-to-highlight, panel stack, and a pinned System Info action.  
*(description written for this catalogue — the file has no header comment)*

- **Properties:** `int cameraCount`, `int imuCount`, `int phoneCount`, `int activeNavIndex`, `string searchQuery`, `readonly bool shown`, `readonly bool isSearching`, `readonly var searchResults`, `readonly bool navCollapsed`
- **Signals:** `resourceMonitorRequested()`
- **Functions:** `navigateToResult(entry)`, `showMetricDetail(key)`, `showCharacteristicDetail(conditionId)`, `showMeasureDetail(measureId)`, `scrollWithRetry(panel, itemId, retries)`
- **Uses:** `AppearancePanel`, `ArchivingPanel`, `CamerasPanel`, `DiagnosticModel`, `DisplaysPanel`, `GeneralPanel`, `ImusPanel`, `LaunchMonitorPanel`, `MicrophonesPanel`, `PhonesPanel`, `PpPressable`, `SettingsIndex`, `StoragePanel`
- **Used by (1):** `Main`

### SettingsIndex
`src/Gui/settings/SettingsIndex.qml` · 445 lines · root `QtObject` · **singleton** · no direct `Theme.*` refs

Singleton: full-text search index of every settings panel's entries (title, description, keywords, panelIndex and item id) that ScreenSettings searches and scrolls to.  
*(description written for this catalogue — the file has no header comment)*

- **Properties:** `readonly var entries`
- **Uses:** `Main`
- **Used by (1):** `ScreenSettings`

### StoragePanel
`src/Gui/settings/StoragePanel.qml` · 1093 lines · root `Item` · 3 hard-coded colour(s)

Settings > Storage: athlete-library location, folder-naming order (date/athlete/session type), video codec (H.264/H.265), resolution and quality presets, and disk/size estimates (cheap volume info plus a worker-measured session size).  
*(description written for this catalogue — the file has no header comment)*

- **Properties:** `var diskInfo`, `readonly var videoMbPerCamera`, `readonly real codecMultiplier`, `readonly real resolutionMultiplier`, `readonly int cameras`, `readonly real rawMbPerCamera`, `readonly real documentMb`, `readonly real clipMb`, `readonly real sessionMb`, `readonly real remainingHours`, `readonly bool hasConverter`, `readonly var namingOptions`, `readonly var resolutionOptions`, `readonly var codecOptions` … (+3 more)
- **Functions:** `refreshDiskInfo()`, `rescanLibrarySize()`, `formatBytes(n)`, `formatMb(n)`, `formatGb(n)`, `countJsonSwings()`, `findChild(parent, name)`, `scrollToItem(itemId)`
- **Local components:** `TogglePill`
- **Uses:** `PpButton`, `PpComboBox`, `PpDisplayText`, `PpDivider`, `PpPressable`
- **Used by (1):** `ScreenSettings`
