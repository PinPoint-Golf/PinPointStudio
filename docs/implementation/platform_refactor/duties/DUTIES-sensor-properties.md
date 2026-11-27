**Updated 26 Nov: the Zero button is removed, so `ImuInstance::zeroOrientation` and `ImuManager::zeroAll` no longer exist. CL3 is gone: C-6b.2 has 7 clear paths, not 8, and CL1 has no third caller. The listing lines in §8 that name them describe `cc756cfa`.**

**Updated 27 Nov (C-6b.1, the sensors' cut-over): every §1 row now has the home built for it; the duties list, with where each went, is in C-6b.1's notes and its commit. One hit the ledger missed, added here: DK2, `src/Gui/tests/device_key_test.cpp` (the three sensor setters and getters in C-1a's key-spelling fixture, re-pointed to raw `ppSettings()` writes and reads, as Step 8 did for the cameras' DK1).**

**Updated 27 Nov (C-6b.2, every way a sensor is calibrated or cleared updates the Rig): the "Goes" of §2 and §3 is settled by the Rig design addendum's decision 4 (ruled 25 Nov 2026: the record follows the live sensor's alignment; the device object is the only caller of the sink), which answers F34, F35 and F36. Built: SE2, SE3, SE4a, SE4b and SE5's valid exit commit a `sensorAlignment` record (method `solved`) from `ImuInstance`; SE6 commits a `deviceHeld` record on each of the HackMotion's two lanes from `HmInstance`'s one before-and-after compare; CL2 and CL4 (through CL2) clear it; CL5 and CL6 (HM5, HM7, HM8 and the invalidation) clear both lanes through the same compare; CL7, the object's teardown, clears it. SE1 and CL1 touch only the older flag, so they commit and clear nothing (decision 4.2). CL8, a role released or moved, ends nothing (decision 4.6): the record stays. NC1 to NC5 are unchanged. Each path is tested in `imu_instance_rig_test`; where each went, with file and line, is in C-6b.2's notes and its commit.**

Taken at `cc756cfa` (main, after `git pull --ff-only`). C-6i; a document, read and not run: no code, no build, no IM edit.

# Duties of the six sensor properties, and of every path that sets or clears a sensor's calibration

This page is the authority for two lists C-6b.2 is tested against: **the set list (§2)** and **the clear list (§3)**. §1 covers the six `AppSettings` properties, §4 the role rules, §5 the destinations, §6 the tests and §7 the checks. §8 is a ledger of every grep hit, each tied to a row.

**How to read a row.** `| ID | Kind | Site | What | Goes |`.
- **Kinds:**
  - R reads a value; D reads only for a dependency (a QML binding that must re-evaluate); W writes; L listens; S sets a calibration; C clears one;
  - T a test, F a fake, N not a duty (a comment, a homonym, or an unused declaration).
- **Goes** names the destination the design or a C-6 brief gives: a Rig command, the sink (`commit` / `clear`), `app.rig`, typed input to a manager or `ImuInstance`, `src/IMU`, Adapters, unchanged, removed, re-pointed, or follows another row.
- **NO HOME** means the design does not place the duty. It always carries a finding number (§9). This page places nothing itself.
- Line numbers are at `cc756cfa`. The ledger keys on text, not numbers.

The approved design is `docs/design/rig_design.md` (§4.1–§4.3, §4.6, §4.8). The briefs that build from this page are C-6a.1, C-6a.2, C-6b.1 and C-6b.2.

---

## 1. The six properties

Each property, its key, its spelling (§4.6) and where §4.3 moves it, then every reader and writer outside `app_settings.*`, plus AppSettings' own load and store lines.

Proven by grep (§7):
- The two unused properties have no use outside `app_settings.h`, by property name or by raw key, in `src` or `tools`.
- No code reads `AppSettings` by reflection. `metaObject()->property`, `propertyCount()` and `QMetaProperty` over `*.cpp *.h`, outside tests, find only `charts/charts_gadget.h`, which maps a chart gadget.

### 1.1 `imuRoles`

- **Key:** `imu/roles`. A `QVariantMap` from a **placement key** to a role name.
- **Spelling:**
  - a Witmotion's bare `deviceId`;
  - a HackMotion's `<deviceId>#lowerArm` and `<deviceId>#palm` (spelled by `src/IMU/hm_unit_id.h`).
- **Values:** `pelvis`, `thorax`, `leadUpperArm`, `leadForearm`, `leadHand` (`segment_role.h`'s `placementRoleName`).
- **Marker:** `imu/rolesMigrated`.
- **Absence:**
  - With the marker set, an empty or missing map means no mounts, and it is returned as is.
  - With no marker, the roles are migrated once from `imu/placement` (§4).
- **Design:** the rig state, per `PlacementKey`, through `claimRole` / `releaseRole` (§4.1, §4.3). C-6a.2 makes the copy; C-6b.1 cuts over.

| ID | Kind | Site | What | Goes |
|---|---|---|---|---|
| RO1 | W | `app_settings.cpp:56–75` `AppSettings::loadImuRoles` (from the constructor, `app_settings.h:530`) | Reads `imu/rolesMigrated`, `imu/roles` and, only when not migrated, `imu/placement`; calls `imu_roles::loadRoles`. On a migration it writes `imu/roles` and the marker, never `imu/placement`, and `ppInfo`s each legacy entry. It runs on every `AppSettings` construction (they are built all over the tree), and after the first run it is a plain read | the Rig's copy reads the same keys through `loadRoles` (C-6a.2 What 5); AppSettings' call goes only if nothing else needs it, and the marker is untouched (C-6b.1 What 4) |
| RO2 | W | `app_settings.cpp:77–91` `AppSettings::setImuRoles` | Equality guard. Writes `imu/roles` and sets the marker as well, re-derives the placement view, and emits `imuRolesChanged`, then always `imuPlacementChanged` | removed (C-6b.1 What 4); its one production caller is RO4 |
| RO3 | R | `imu_manager.cpp:622–632` `ImuManager::currentRoles` (comment `imu_manager.h:165`) | The one C++ reader. Every resolver funnels through it, at about 30 Hz from the live wrist readout. It builds a fresh `AppSettings` only when the manager has none (tests) | typed input: `ImuManager` takes the rig state and follows `RigChanged` (C-6b.1 What 2) |
| RO4 | W | `imu_manager.cpp:634–644` `ImuManager::writeRoles` | The one C++ writer, one write per whole map. Callers: `setRoleForDevice` (RR1), `migrateHackMotionPlacement` (RR8), `claimArmForHackMotion` (RR9) | Rig command `claimRole` / `releaseRole` for RR1 (C-6b.1 What 2); for RR8 and RR9 NO HOME **F37** |
| RO5 | L | `imu_manager.cpp:115–118` | `AppSettings::imuRolesChanged` → `refreshRolesInSession` (notifies only on a real change) | follows `RigChanged` (C-6b.1 What 2) |
| RO6 | D | `ImusPanel.qml:66–69` `mountLabel` | Reads the map only so the binding re-evaluates; the value comes from `imuManager.roleForDevice` (not reactive) | app.rig (C-6b.1 What 3); a dependency-only read fails silently to "" once the property goes (**F40**) |
| RO7 | R | `ImusPanel.qml:1009–1012` `assignedRoles` | `Object.values(appSettings.imuRoles)`: the summary line's mounts by name | app.rig (C-6b.1 What 3) |
| RO8 | D | `PpImuPanel.qml:180` `placement` | Dependency for `roleForDevice`, the toolbar panel's mount chip | app.rig (C-6b.1 What 3; **F40**) |
| RO9 | D | `ArmVizView.qml:122` `_roleDeps` | Dependency for the three `instanceForRole` bindings | app.rig (C-6b.1 What 3; **F40**) |
| RO10 | D | `ImuCalibrationFlow.qml:205` `_roleDeps` | Dependency for the routine choice (C-10c IF rows) | app.rig (C-6b.1 What 3; **F40**) |
| RO11 | D | `SetupContext.qml:309` `devices`, `:354` `roles`, through its `settings` alias (`:53`, `property var settings: appSettings`) | Dependencies for `roleForDevice` and the owner ladder | app.rig (C-6b.1 What 3; **F40**: the `settings.` spelling escapes C-6b.1's accept grep) |
| RO12 | D | `SetupContext.qml:319` `devices.named` | Dependency on both maps, for `displayNameForDevice` | app.rig (C-6b.1 What 3; **F40**) |
| RO13 | R | `SetupContext.qml:505–519` `mountHolder` (comment 507) | Reads the map's keys, splits the `#` key shape itself, and returns the present, session-enabled holder: **a QML copy of `claimRole`'s refusal rule** | app.rig; the copy of the rule NO HOME **F37** |
| RO14 | R | `tests/probes/setup_mounts_settings.qml:110` | Logs the map | re-pointed (C-6b.1 What 5) |
| RO15 | L | `setup/pages/ImusPage.qml:111–115` (`Connections` on `page.ctx.settings`, `onImuRolesChanged`) | Any roles change clears the page's refusal notes | follows `RigChanged`, through app.rig (C-6b.1 What 3); **F40** (it targets the `settings` alias) |

### 1.2 `imuAlias`

- **Key:** `imu/alias`. A `QVariantMap` keyed `description|deviceId`: `imu_roles::aliasKey`, `imu_role_map.h:80–83`. The design's `persistedWithDescription()` spelling (§4.6).
- **Value:** the name.
- **Absence:** no entry means no alias. The seeded default `"<description> <deviceId>"` reads as no alias in `sensorDisplayName`.
- **Design:** the Rig, not versioned; `setAlias`; no `RigChanged` (§4.1, §4.2).

| ID | Kind | Site | What | Goes |
|---|---|---|---|---|
| AL1 | W | `app_settings.h:540` (load), `:1429–1435` `setImuAlias(QVariantMap)` | Load and store; the store has an equality guard | removed from AppSettings (C-6b.1 What 4); the Rig, not versioned: the copy and `setAlias` (C-6a.2 What 1, 5) |
| AL2 | W | `imu_manager.cpp:176–185` (the `deviceAdded` handler) | Seeds `"<description> <deviceId>"` for a newly seen sensor whose key is absent | Rig command `setAlias` (C-6b.1 What 2: writers call the Rig's commands) |
| AL3 | R | `imu_manager.cpp:264–292` `imuList` | Each entry's `alias` | NO HOME **F38** (the design hands devices the rig state, and the alias is not in it) |
| AL4 | R | `imu_manager.cpp:294–348` `imuDeviceList` | Each entry's `alias` and `imuKey` (QML writes the alias back under `imuKey`) | NO HOME **F38** |
| AL5 | R | `imu_manager.cpp:950–958` `displayNameForDevice` (comment `imu_manager.h:195`) | `sensorDisplayName(id, description, alias map, roles)`. Callers: `SetupContext.qml:329, 479, 524` | app.rig: `sensorDisplayName` stays behind `app.rig` (§4.1); the alias it reads NO HOME **F38** |
| AL6 | W | `imu_manager.cpp:1032–1050` `ImuManager::setImuAlias(key, alias)`, decl `imu_manager.h:153`; caller `ImusPanel.qml:125` | Trims; an empty name removes the key, so the default is seeded again at the next `deviceAdded`. Writes the whole map, emits `imuListChanged` and `imuDeviceListChanged` | Rig command `setAlias` through app.rig (C-6b.1 What 3); the two list signals NO HOME **F38** |
| AL7 | R | `shot_processor.cpp:997` (key open-coded at `:1005`) | At export, the alias of every enumerated sensor goes into `job.imuAliasBySerial`, keyed by the stream serial: the swing's lane alias, a recorded input | NO HOME **F39** |
| AL8 | R | `resource_monitor_controller.cpp:92` (a fresh `AppSettings` each refresh); key open-coded at `:342`; the local `imuAlias` at `:343, 405, 409` | The resource monitor's device and source names | NO HOME **F39** |
| AL9 | D | `SetupContext.qml:460` `absentMounts.named` | Dependency for `displayNameForDevice` | app.rig (C-6b.1 What 3; **F40**) |

### 1.3 `imuExcluded`

- **Key:** `imu/excluded`. A `QStringList` of bare device ids: `persistedId()` (§4.6).
- **Absence:** an id not listed is enabled.
- **Meaning:** a machine-wide switch that seeds each session's own list (`ImuManager::m_sessionExcluded`); the session's choice is never written back.
- **Design:** the Rig, not versioned; `setEnabledOnThisMachine`; no `RigChanged`.

| ID | Kind | Site | What | Goes |
|---|---|---|---|---|
| EX1 | W | `app_settings.h:529` (load), `:1346–1352` `setImuExcluded` | Load and store, with an equality guard | removed (C-6b.1 What 4); the Rig, not versioned: the copy and `setEnabledOnThisMachine` (C-6a.2 What 1, 5) |
| EX2 | R | `imu_manager.cpp:80–86` (comments `imu_manager.h:74, 374`) | Seeds `m_sessionExcluded` and `m_lastGlobalExcluded` at construction | NO HOME **F38** (the enabled flag is not in the rig state handed to devices) |
| EX3 | L | `imu_manager.cpp:88–113` (queued) | Diffs the new global list against the last. Each newly excluded id → `setSessionImuEnabled(id, false)`, which disconnects it and so tears its instance down (CL7); each re-included id → `(id, true)` | NO HOME **F38** (no `RigChanged` for an enabled flag, §4.2) |
| EX4 | R | `ImusPanel.qml:54` `isExcluded` | The Enable switch's state | app.rig (C-6b.1 What 3) |
| EX5 | W | `ImusPanel.qml:150–159` (the Enable switch) | Copies the list, edits it, `appSettings.imuExcluded = list`; closes the test panel | Rig command `setEnabledOnThisMachine` through app.rig (C-6b.1 What 3) |
| EX6 | R | `ImusPanel.qml:996–1000` `enabledCount` | Counts enabled devices | app.rig (C-6b.1 What 3) |
| EX7 | R | `home/HmRigCapability.qml:57` → `setup/capability_facts.js:61` (`_roleCounts`, input doc `:45`) | Home's capability: a role counts only when its holder is not globally excluded | app.rig (C-6b.1 What 3; **F40**: read through `input.` in a `.js` file) |
| EX8 | R | `SetupContext.qml:612–620` `seedSessionEnablement` | Seeds each sensor's session enablement from the global list, once per visit (`SetupDraft.reset`) | app.rig (C-6b.1 What 3; **F40**) |

### 1.4 `imuPlacement`

- **Not stored.** It is derived (`placementFromRoles`): the arm roles as slot letters (A forearm, B hand, C upper arm) under the same placement keys; trunk roles are omitted.
- **Key:** `imu/placement` is read once, by RO1's migration, and never written.
- **Design:** not moved. `app.rig` serves the same read-only view for its one reader (§4.3; C-6a.2 What 4).

| ID | Kind | Site | What | Goes |
|---|---|---|---|---|
| PL1 | W | `app_settings.cpp:74, 85` (in RO1 and RO2) | `m_imuPlacement = placementFromRoles(m_imuRoles)` | app.rig: the derived placement view from `placementFromRoles`, not stored (C-6a.2 What 4) |
| PL2 | R | `review/PpDataViewer.qml:96` → `SwingDataSource::imuPlacementPairs` (`swing_data_source.h:99, 102, 200`; `.cpp:234–245`) | The one reader. It labels an old swing's sensor stream, which carries no role, from the **current** placement, looked up by the stream's serial | app.rig placement view (§4.3). See §9's note on R-RIG-2 |
| PL3 | T | `imu_role_map_test.cpp` V1 (295–302), A1–A5 (510–543) | The derived view equals the legacy map; a trunk-only change still notifies; the property has no WRITE; it is empty when the roles are | re-pointed (C-6a.2 What 7, C-6b.1 What 5); the AppSettings subject goes (**F42**) |
| PL4 | T | `tests/probes/setup_mounts_settings.qml:111` | Logs the view | re-pointed (C-6b.1 What 5) |
| PL5 | T | `session_setup_lint_test.cpp:54, 89, 1098` (W9) | No QML outside `tests/` names the slot-letter API or `imuPlacement`, except the site `review/PpDataViewer.qml:96` (`kW9Allowed`, by path and line) | re-pointed (C-6b.1 What 5); the site key and the regex both have to follow the renamed read (**F42**) |

### 1.5 `imuCalibration`

- **Key:** `imu/calibration`. A `QVariantMap` that is loaded and never read or written by anything else.
- **Design:** not moved, as it is unused. The property is deleted and the stored key left for Stage E (§4.3; C-6b.1 What 4). C-6a.2 does not copy it.

| ID | Kind | Site | What | Goes |
|---|---|---|---|---|
| CA1 | N | `app_settings.h:245` (property), `:541` (load), `:756` (getter), `:1437–1443` (setter), `:1781` (signal), `:1904` (member) | Every hit of the property and of its key; 0 outside `app_settings.h`, in `src` or `tools` (§7) | removed: the property deleted, the key left for Stage E (C-6b.1 What 4) |

### 1.6 `imuMountOrientation`

- **Key:** `imu/mountOrientation`. A `QVariantMap` that is loaded and never read or written by anything else.
- **Design:** the same as 1.5. The mount checks the alignment record holds come from `ImuInstance`, not from this key (§4.3).

| ID | Kind | Site | What | Goes |
|---|---|---|---|---|
| MO1 | N | `app_settings.h:262` (property), `:533` (load), `:748` (getter), `:1374–1379` (setter), `:1773` (signal), `:1902` (member) | Every hit of the property and of its key; 0 outside `app_settings.h`, in `src` or `tools` (§7) | removed: the property deleted, the key left for Stage E (C-6b.1 What 4) |

---

## 2. The set list: every path that sets a calibration on a live sensor

An `ImuInstance` (a Witmotion) holds **two** calibrations, which today's code keeps apart:
- **The legacy half:** `m_calibrated`, `m_calibArmDown`, `m_calibArmTPose`, `m_calibTransform` and `m_calibrationAngleValid`. These are the `calibrated`, `calibArmDown`, `calibArmTPose`, `calibTransform` and `calibrationAngleValid` properties.
- **The anatomical half:**
  - A, M, `m_anatCalibrated` and `m_calibratedAtUtc`;
  - the worker's copy (`m_worker->setAnatomical`), which drives `anatQuat`;
  - the two mount checks (`m_mountDeviationDeg`, `m_mountGravityErrorDeg`);
  - the trunk provenance (`m_trunkProv`, `m_trunkRecord`).

`fullyCalibrated()` (`imu_instance.h:207–214`) is what a saved shot records as `calibrated`. It reads **only the anatomical half**:
- an anatomical calibration with a trunk provenance is true;
- otherwise it is true when anatomical, `mountDeviationDeg` ≤ 15 and `mountGravityErrorDeg` ≤ 25.

The capture snapshot reads the anatomical half: `capture_context.cpp:115–129` and `shot_processor.cpp:1043–1054`.

**What each path leaves on the sensor.** "—" means untouched.

| Path | Legacy half | A, M | anat flag, time | Mount checks | Trunk provenance | Worker copy |
|---|---|---|---|---|---|---|
| SE1 `setCalibration` | set: armDown, TPose, transform = conj(armDown), angle valid when 60–120°, `calibrated` true | — | — | — | — | — |
| SE2 `setFunctionalCalibration` | — | from `solveSegment` | true, now | deviation = angle(M, nominal); gravity — | cleared | pushed |
| SE3 `setNominalCalibration` | — | M = nominal arm or hand mount; A = conj(ref·M) | true, now | deviation 0; gravity from the live accel (unchanged if the accel reads ~0) | cleared | pushed |
| SE4a `refineMountAboutLongAxis`, \|φ\| ≤ 25 | — | M = nominal·Ry(φ); A re-anchored on ref | true, now | deviation \|φ\|; gravity — | cleared | pushed |
| SE4b `refineMountAboutLongAxis`, \|φ\| > 25 | — | M = nominal; A re-anchored on ref | true, now | deviation \|φ\| (> 15, so fails the gate); gravity — | cleared | pushed |
| SE5 `calibrateTrunk`, valid solve | set, with identity quaternions | the solve's A and M | true, now | both 0 | set (`m_trunkProv`, `m_trunkRecord`) | pushed |
| SE5 `calibrateTrunk`, invalid solve or unknown role | — | — | — | — | — | — (the earlier calibration stays) |

| ID | Kind | Site | What | Goes |
|---|---|---|---|---|
| SE1 | S | `imu_instance.cpp:720–753`, decl `imu_instance.h:257` (comment 472). Production callers: **0** (F17). Fake: `FakeImuInstance.qml:142–143` | The legacy half only, as in the table. No signal for the anatomical half, and `fullyCalibrated` is unchanged | sink commit (C-6b.2 What 2); what record it commits NO HOME **F35** |
| SE2 | S | `imu_instance.cpp:765–799`, decl `:263`. Production callers: **0** | As in the table; logs the axis angle and the mount deviation | sink commit (C-6b.2 What 2); unused **F41** |
| SE3 | S | `imu_instance.cpp:801–838`, decl `:274` (comment 107). Callers `WitmotionArmRoutine.qml:499, 501, 503` (each connected segment at arm-down; `handMount` false on every call; C-10b W10) | As in the table | sink commit (C-6b.2 What 2) |
| SE4a | S | `imu_instance.cpp:840–889` (the method; this branch at 868–888), decl `:281`. Caller `WitmotionArmRoutine.qml:357` (each connected segment at the T-pose; C-10b W16, F18) | \|φ\| ≤ 25: M = nominal·Ry(φ), A re-anchored, the time stamped | sink commit (C-6b.2 What 2) |
| SE4b | S | `imu_instance.cpp:853–866` (the branch \|φ\| > 25, "precise refine REJECTED"); same caller | M = nominal, A re-anchored, the time stamped; `mountDeviationDeg` = \|φ\| fails the 15° gate | sink commit (C-6b.2 What 2) |
| SE5 | S | `imu_instance.cpp:909–991`, decl `:313` (comments `imu_instance.h:43, 114, 294, 451, 485`; ring `imu_instance.cpp:64, 214`). Callers `TrunkRoutine.qml:417, 418` (C-10a T9; comments 26, 143, 179, 212) | Three exits: an unknown role (920) and an invalid solve change nothing; a valid solve sets both halves. One sensor can pass while the other fails (F7) | sink commit on the valid exit only (C-6b.2 What 2); the partial pass F7 |
| SE6 | S | A HackMotion reaching `WR_CAL_CALIBRATED`: `hm_instance.cpp:285–373` (HM5, HM6; HM7 and HM8 may write any state); `fullyCalibrated()` at `hm_instance.cpp:918–921`; the units' frame on the display tick, `hm_instance.cpp:676–705` (HM9) | Host side, the state, the presence figures and the anchor. On each unit, `anatCalibrated` = device calibrated **and** `hm_frame::isSelected()` (constant `kCandidate` 1, so always selected); M = `hm_frame::mountM()`, A identity. The transform itself lives in the device | sink commit, method `deviceHeld` (C-6b.2 What 3); which state change commits, one record or two, and the unit's M NO HOME **F36** |

**The two counts C-6b.2 tests against.** There are **7 set paths**: SE1, SE2, SE3, SE4a, SE4b, SE5 (its valid exit only) and SE6. There are **7 clear paths** (8 at `cc756cfa`; CL3, the Zero, was removed 26 Nov): CL1, CL2, CL4 (the routines' resets, through CL1 and CL2), CL5, CL6, CL7 and CL8. SE1 and SE2 have no production caller (F17, F41). CL6, CL7 and CL8 have no home (F34, F36). NC1–NC5 are not paths.

The arm routines call no other set path, and the HackMotion routine sets nothing host-side (`docs/implementation/platform_refactor/duties/DUTIES-HackMotionArmRoutine.qml.md` §5). Who requests each set, and what one run commits, are F7 and F15. They are cited, not re-filed.

---

## 3. The clear list: every path that clears a calibration, or ends one without a clear

| ID | Kind | Site | What | Goes |
|---|---|---|---|---|
| CL1 | C | `imu_instance.cpp:755–763`, decl `:258`. Callers: `WitmotionArmRoutine.qml:276` (`_reset`), `TrunkRoutine.qml:322` (`_clearSensors`); `ImuInstance::zeroOrientation` was a third, removed 26 Nov | **The legacy half only:** `calibrated` false, angle valid true, the three quaternions identity, `calibratedChanged`. A, M, the anat flag, the worker copy and `fullyCalibrated` are untouched | sink clear (C-6b.2 What 2); which half it clears **F35** |
| CL2 | C | `imu_instance.cpp:891–900`, decl `:268`. Callers: `WitmotionArmRoutine.qml:277`, `TrunkRoutine.qml:321` | The anat flag false, the time cleared, the trunk provenance and record cleared, A and M identity, the worker copy cleared. **Leaves** both mount checks at their last values (`fullyCalibrated` is false through the flag) | sink clear (C-6b.2 What 2) |
| CL3 | C | **REMOVED 26 Nov (the Zero button's removal): not a clear path any more.** At `cc756cfa`: `imu_instance.cpp:570–585`, decl `:254`. Callers: `ImusPanel.qml:857` ("Zero orientation", Witmotion rows only), `CapturePage.qml:308` ("Zero", every live instance), `ImuManager::zeroAll` 829–836 (decl `imu_manager.h:271`; **0** callers) | Does nothing when disconnected. Queues `reinitialize()` (NC3's re-seed of the host filter) and `zeroToCurrentPose()` (CALSW roll, pitch and yaw zero on the device, `wt9011dcl_ble.cpp:138–155`). Its own comment says a calibration "is now invalid — the user must recalibrate". It then calls CL1 **only if** the legacy flag is set. **A and M stay applied and `fullyCalibrated` stays as it was** | sink clear (§4.2, §4.8; C-6b.2 What 2); the record and the sensor disagree afterwards **F35** |
| CL4 | C | The routines' resets. Witmotion `_reset` (`WitmotionArmRoutine.qml:254–280`; comments 23–24, 256–264), on every begin() and reset() (C-10b W1). Trunk `_clearSensors` (`TrunkRoutine.qml:317–324`), on begin(), reset() and every stand-tall attempt (C-10a T1, 375). HackMotion: no host-side clear (`HackMotionArmRoutine.qml:222–225`) | Witmotion: CL1 then CL2 on A, B and C (each present segment). Trunk: CL2 then CL1 on both. stop() and `_fail` clear nothing (NC4) | follows CL1, CL2; who requests the clear F5 |
| CL5 | C | HackMotion invalidation: `HmInstance::invalidateCalibration`, `hm_instance.cpp:1024–1080` (decl `hm_instance.h:611–621`; signal `calibrationInvalidated` `:582–586`). Reached only from `calibrationLost` (`hm_instance.cpp:411–414`), which `hm_session_worker.cpp:865` (stream restarted → UNKNOWN) and `:895` (link down → UNCALIBRATED) emit (`hm_session_worker.h:273`). The transport's disconnect only routes there (`hm_instance.cpp:1200–1235`) | Phase idle; the state as the library reports it; the abort reason none; the presence surface cleared. `calibrationStateChanged`, then `calibrationInvalidated`, **unconditionally**. The log says "lost" only when there was something to lose. Consumer: `HackMotionArmRoutine.qml` `_deviceLink.onCalibrationInvalidated` (C-10b H28) | sink clear (C-6b.2 What 3) |
| CL6 | C | A HackMotion leaving `WR_CAL_CALIBRATED` **without** `calibrationInvalidated`: `calibrationStateRefreshed` (HM8, ABSENT after the presence check, `hm_session_worker.cpp:962–967`); a phase event (HM5) or `calibrationPresenceUnmeasured` (HM7), each writing whatever state the library reports | `fullyCalibrated` goes false, and only `calibrationStateChanged` says so. Whether `wr_calibration_begin` itself drops CALIBRATED is the library's; by reading, our code copies whatever it reports | NO HOME **F36** |
| CL7 | C | **Instance teardown**, `ImuManager::setSelected(false)` `imu_manager.cpp:441–511`: `stop()`, deregister, `deleteLater`. Reached from: Disconnect (`ImusPanel.qml:413`); the Capture page chip (`CapturePage.qml:105`); `setSessionImuEnabled(id, false)` (355–379, from `PpImuPanel.qml:369`, `SetupContext.qml:493, 619` and EX3's diff); `disconnectAll` (513–527, from `PpImuPanel.qml:145`, `PpSessionToolbar.qml:431`, `SetupContext.qml:637`). Also `~ImuManager` (203–245) | The calibration dies with the object, with no clear call: a Witmotion's halves, and the HackMotion's whole state. `HmInstance::stop()` closes the library session first (`hm_instance.cpp:880–896`), so no `calibrationLost` follows. The next Connect builds a new, uncalibrated instance | NO HOME **F34** |
| CL8 | C | **A role released or moved.** `setRoleForDevice(id, "")` (`imu_manager.cpp:759–787`, `imu_role_map.cpp:121–127`; Sensors page "Not worn"); a move to another role (claimRole drops the device's own keys first); RR1's displacement of an absent holder; RR8's `DroppedBare` | The live A and M stay on the instance, so a forearm calibration stays on a sensor now worn on the thorax. A record keyed by a Witmotion's `PlacementKey` (its bare id) is the same key before and after | NO HOME **F34** |
| NC1 | N | Witmotion link loss: `ImuInstance::onConnectionLost`, `imu_instance.cpp:1074–1116` | **Not a clear.** A, M, the legacy half and the worker's copy (`ImuIoWorker`, which lives as long as the instance) all stay. The same instance reconnects only through its retry timer (`imu_instance.cpp:276–279`): after a connect failure, or after the #105 configuration drop (1097–1106). After a genuine mid-stream drop no retry starts (1111–1116) and nothing reads `imuAutoReconnect` outside the settings panel. The way back is a deselect and a select: CL7 | unchanged (today's behaviour) |
| NC2 | N | `HmInstance::resetStreamingState`, `hm_instance.cpp:1103–1122` (comments 1108, 1121) | **Not a clear.** It deliberately leaves the calibration surface to the library's link-down event (CL5) | unchanged |
| NC3 | N | `ImuInstance::setOutputRateHz`, `imu_instance.cpp:994–1031` (called on every Ready, 1205) | **Not a clear.** When connected it calls `reinitialize()`, which is `initializeDevice()` (`wt9011dcl_ble.h:67`): it re-seeds the host orientation filter "from the current pose" (`resetOrientationFilter`, `wt9011dcl_ble.cpp:106–122`) and re-sends the configuration. It does not zero, and it clears nothing (F35) | unchanged |
| NC5 | N | `ImuInstance::beginZeroing`, `imu_instance.cpp:556–568`, decl `:284`. Callers: **0** | A device zero (CALSW ×3 and a fence read) with a 500 ms settle timer and a 30 s deadline (`:236–270`). **Not a set or a clear:** it touches no calibration field | NO HOME **F41** (unused) |
| NC4 | N | Routine stop() and `_fail`, and the HackMotion abort (`abortCalibration`; at VERIFYING the transform stays applied and the state UNKNOWN) | **Not a clear.** A stand tall or an arm-down already applied stays (C-10a §5, C-10b §5); a HackMotion that never read CALIBRATED leaves no record to clear | unchanged |

### The HackMotion's state, line by line (check 3)

| ID | Kind | Site | What | Goes |
|---|---|---|---|---|
| HM1 | N | `hm_instance.h:75–227` `HmUnit` (hits 102–200) | A unit's `calibrated()` is always false: the legacy fallback. Its `anatCalibrated` is the conjunction (SE6) | unchanged; the unit's M in a record **F36** |
| HM2 | N | `hm_instance.h:237–273` (class comment) | The library's fixed order (begin → horizontal → raise → apply → reference pose); "reconnect is not resume" | n/a |
| HM3 | N | `hm_instance.h:294–410` (properties), `520–526` (getters), `557` (`calibrationStateChanged`), `703–716` (members); `hm_instance.cpp:113–126` (constructor), `918–931` (`fullyCalibrated`, `calibrationActive`), `1013–1022` (`clearPresenceSurface`) | The calibration surface: phase, state, abort reason, presence angle, spread, samples, not-measured, active, relative angle. `fullyCalibrated` = state CALIBRATED | unchanged (the device object keeps it, §4.8 step 1) |
| HM4 | N | `hm_instance.h:531–551` (the five `Q_INVOKABLE`s, 547–551), `579` (`calibrationCallRefused`); `hm_instance.cpp:416–448` (refusal handler), `933–950` | The routine's calls, queued to the I/O thread; a refusal comes back only as `calibrationCallRefused` | unchanged; driven by the routine (C-13) |
| HM5 | S | `hm_instance.cpp:285–335` (`calibrationPhaseEvent`) | Writes the phase, **the state as read**, and the abort reason; a new routine (AWAIT_HORIZONTAL) clears the presence figures | NO HOME **F36** (it can set or clear) |
| HM6 | S | `hm_instance.cpp:337–373` (`calibrationPresenceMeasured`) | The anchor, the presence angle, the spread and **the state as read**: the usual entry to CALIBRATED | sink commit when the state reads CALIBRATED (C-6b.2 What 3); **F36** |
| HM7 | C | `hm_instance.cpp:375–398` (`calibrationPresenceUnmeasured`) | Not measured: NaNs, no anchor, the state as read (UNKNOWN) | NO HOME **F36** |
| HM8 | C | `hm_instance.cpp:400–409` (`calibrationStateRefreshed`) | ABSENT or INDETERMINATE: the state as read. ABSENT drops it to UNCALIBRATED | NO HOME **F36** |
| HM9 | S | `hm_instance.cpp:676–705` (the display tick; hits 680, 685, 737) | Each unit's `anatCalibrated`, `anatQuat`, M and A, recomputed on the 60 Hz tick from the state | NO HOME **F36** |
| HM10 | N | `hm_instance.cpp:527–547` (relative-angle log), `906` (source ids "invalidated") | Logging; a homonym | n/a |

---

## 4. The role rules (`imu_role_map.*`)

| ID | Kind | Site | What | Goes |
|---|---|---|---|---|
| RR1 | W | `imu_role_map.cpp:109–216` `claimRole`, decl `imu_role_map.h:263`; caller `ImuManager::setRoleForDevice` `imu_manager.cpp:759–787` ← `SetupContext.setMount` (504) ← `ImusPage.qml:94`; comment `ImuMounts.qml:50` | See "claimRole, case by case" below | Rig command `claimRole(placement, role, facts)` (C-6a.2 What 1–2); the function to `src/IMU`, text unchanged (C-6a.1) |
| RR2 | W | `imu_role_map.h:158–199` `RolesLoad`, `loadRoles` ← RO1 | The marker, not the map, decides. Not migrated: each legacy entry with a slot letter A, B or C becomes its role, carried verbatim (a bare wG3 pin included); D, `other` and "" are dropped and logged. Migrated: the stored map is returned as is, empty included | the Rig's copy (C-6a.2 What 5); `src/IMU` (C-6a.1) |
| RR3 | R | `imu_role_map.h:144–155` `placementFromRoles` ← PL1 | The arm roles as letters; trunk roles omitted | app.rig placement view (C-6a.2 What 4); `src/IMU` (C-6a.1) |
| RR4 | R | `imu_role_map.h:126–140` `roleForLegacySlot`, `legacySlotForRole` (the legacy-slot map) | A ↔ leadForearm, B ↔ leadHand, C ↔ leadUpperArm; any other letter "" | src/IMU (C-6a.1) |
| RR5 | R | `imu_role_map.h:50–66`: the role names, `placementRoleOrder`, `isPlacementRole` | The five names, in the order every list reports them | src/IMU (C-6a.1) |
| RR6 | R | `imu_role_map.h:96–124` `stripDeviceIds`, `sensorDisplayName` ← AL5 | Alias (not the seeded default), else the description, else "HackMotion wG3" for a unit key, else "sensor"; UUID-shaped tokens stripped | Adapters: stays in `src/Gui/imu`, behind app.rig (§4.1; C-6a.1 What 2) |
| RR7 | R | `imu_role_map.h:77–83` `aliasKey` (comment 78) ← `imu_manager.cpp:178, 276, 304`; open-coded copies `shot_processor.cpp:1005`, `resource_monitor_controller.cpp:342` | The `description|deviceId` spelling | typed key: C-1's `DeviceKey::persistedWithDescription()` (§4.6) |
| RR8 | W | `imu_role_map.cpp:218–271` `migrateBareHackMotion` ← `ImuManager::migrateHackMotionPlacement` `imu_manager.cpp:960–999` ← the constructor (146–149, every device already enumerated) and `deviceAdded` (193) | A wG3's bare-id pin → its two unit keys (Migrated), or the bare key dropped (DroppedBare), or left with a `ppWarn` (Unmappable, Blocked). It writes through RO4. **An automatic role write with no command** | NO HOME **F37** |
| RR9 | W | `imu_role_map.cpp:273–300` `claimArmForUnassignedHackMotion` ← `ImuManager::claimArmForHackMotion` `imu_manager.cpp:1001–1021` ← `deviceAdded` (194), `setSessionImuEnabled(id, true)` (375), a wG3's `imuConnectedChanged` (878) | A present, enabled wG3 with no key of its own takes the lead forearm and hand, unless a present, enabled holder has either (Held, `ppInfo`). It writes through RO4. **An automatic role write with no command** | NO HOME **F37** |
| RR10 | R | The owner ladder: `resolveRole`, `roleHolder`, `rolesInSession`, `roleForDevice`, `ownerOfKey`, `parseUnitKey` (`imu_role_map.cpp:35–107`; decls `.h:203–244`), through `ImuManager` 646–795, to `capture_context.cpp:114, 158` (the freeze's bindings, an analysis input), `shot_processor.cpp:1029`, `trunk_ceremony_recorder.cpp:133, 151`, `live_wrist_angles.cpp:120`, `ImuInstance::startRawRecording` (`imu_instance.cpp:641–648`) | Present and enabled beats present but disabled beats any claimant; key order within a rung; a double claim is warned once | NO HOME **F37** (the design names neither their ring nor whether they move) |
| RR11 | N | One arm instrument per wearing: `claimRole`'s refusal (`imu_role_map.cpp:168–175`), RR9's Held, and the ladder's first match | The rule, **not a defect**: never HackMotion and Witmotions on the arm at once; a wrist HackMotion with trunk Witmotions is allowed (roles are disjoint) | unchanged (STANDING 7; C-6a.2 "Not here") |

**`claimRole`, case by case** (RR1), as C-6a.2's table test must reproduce it:

| Input | Result |
|---|---|
| empty device id | error "no device id"; map unchanged |
| role "" | ok; removes the device's bare key, `#lowerArm` and `#palm` |
| not a placement role | error; map unchanged |
| a HackMotion asked for anything but `leadForearm` | error; never coerced |
| a target role held by **another present, session-enabled** device | refused: `refusedRole`, `holderId` (for a wG3 possibly `leadHand`). The caller `ppWarn`s and emits `roleRefused`, relayed by `SetupContext` (549–567) |
| a target role held by an **absent** device | that claim is removed (`displaced`; both unit keys of an absent wG3); the caller `ppWarn`s the keys |
| a target role held by a **present but disabled** device | both claims stand (a parked set-up; the ladder arbitrates) |
| otherwise | the device's own earlier keys removed, then the targets written: a Witmotion one key, a HackMotion `#lowerArm` → `leadForearm` and `#palm` → `leadHand` |

**The hardware facts it asks a device for today** (`ImuManager::deviceFacts`, `imu_manager.cpp:601–615`). These become the typed value C-6a.2 hands in:
- `present`: listed by `DeviceEnumerator`, which never forgets a device registered in this process. This is **not** `isImuPresent`'s "seen in the last completed scan".
- `sessionEnabled`: not in `m_sessionExcluded`. That is **the session's** list, not the machine-wide `imu/excluded`.
- `hackMotion`: the enumerated vendor.

**The legacy-slot map and `imu/rolesMigrated`.** RR4 is the map. The marker is written by RO1 on a migration and by RO2 on every store, and is never cleared. "The marker, not the map, decides", so a coach who cleared every mount is not migrated again.

---

## 5. By destination

- **Rig commands:**
  - `claimRole` / `releaseRole`: RO4 (for RR1), RR1;
  - `setAlias`: AL2, AL6;
  - `setEnabledOnThisMachine`: EX5.
- **The sink:**
  - `commit`: SE1, SE2, SE3, SE4a, SE4b, SE5 on `ImuInstance`; SE6 as `deviceHeld` on `HmInstance`;
  - `clear`: CL1, CL2 on `ImuInstance`; CL5 on `HmInstance`.
- **app.rig:** RO6–RO13, AL5, AL9, EX4, EX6–EX8, PL1, PL2, RR3; the labels RR6.
- **Typed input or following `RigChanged`:** RO3, RO5, RO15.
- **The Rig's copy:** RO1, RR2, and the copy halves of AL1 and EX1.
- **`src/IMU`:** RR1–RR5. **Typed key (C-1):** RR7.
- **Removed:** RO2; AL1 and EX1 (the AppSettings half); CA1, MO1.
- **Re-pointed:** RO14, PL3–PL5 and every test in §6.
- **Unchanged:** HM1–HM4, NC1–NC4, RR11.
- **NO HOME:**
  - CL7, CL8 (F34);
  - SE1, CL1 and CL3's agreement with the record (F35);
  - SE6, CL6, HM5–HM9 (F36);
  - RO4's automatic callers, RO13's copy, RR8–RR10 (F37);
  - AL3, AL4, AL5's alias, AL6's signals, EX2, EX3 (F38);
  - AL7, AL8 (F39);
  - SE2, NC5 and `zeroAll` (F41).

---

## 6. The tests that cover these duties

| ID | Kind | Test | Covers | Goes |
|---|---|---|---|---|
| TS1 | T | `imu_role_map_test` (C++, `src/Gui/tests/CMakeLists.txt:250–257`). It links `imu_role_map.cpp` and the real `AppSettings` against a scratch `QSettings` | §1 names, legacy letters (N2–N14); §2 the migration (M1–M10); §2b the wG3 arm claim (H1–H10); §3 the derived view (V1, V2); §3b display names (DN0–DN6); §4 the ladder (L1–L7); §5 one claim per role (C1–C15); §6 `rolesInSession`; §7 AppSettings: the marker, the stored keys, the derived view, the signals (A1–A5) | re-pointed: the rule cases at `src/IMU` (C-6a.1 What 4) and the Rig (C-6a.2 What 7); A1–A5 and V1 lose their subject (**F42**) |
| TS2 | F | `tests/setup/fakes/FakeImuManager.qml` | Holds placement in `appSettings.imuRoles`; restates `claimRole` (168–218), the ladder, `roleForDevice` and `sensorDisplayName` rule for rule; `_place` stages any map with no rule (401–413) | re-pointed (C-6b.1 What 5) |
| TS3 | F | `tests/setup/fakes/FakeImuInstance.qml` | Records every set and clear call (88–143); a scripted `calibrateTrunk` and `mountResult`. It models neither SE4 branch (F18) | re-pointed (C-6b.2 What 7 needs the real instance; **F42**) |
| TS4 | T | `qml_ui.tst_setup_trunk` | The clear before the solve (237–241), a fresh solve per attempt (275–280, 340–341), solve counts (312–578, 1333, 1360) | re-pointed (C-11b.2) |
| TS5 | T | `qml_ui.tst_setup_calib_witmotion` | `setNominalCalibration` and `refineMountAboutLongAxis` per segment (313–367, 534, 725–726, 804); both clears on each segment (390–393, 466, 831); no set without a capture (476, 493); roles reset (97) | re-pointed (C-12b) |
| TS6 | T | `qml_ui.tst_setup_sensors` | MQ1–MQ8 (a mount is a setting; refused picks; a wG3's pair; a disabled sensor), AN1–AN6 (names from aliases, never an id), NA1, D3N; **OC1: a HackMotion link drop voids the outcome**; OC2 | re-pointed (C-6b.1 What 5) |
| TS7 | T | `qml_ui.tst_setup_mounts` | S1–S4 (the Settings line read-only; follows the map; by name; no letters), P1–P4 ("Not worn" takes the mount away; a held mount not offered is still shown) | re-pointed (C-6b.1 What 5) |
| TS8 | T | `qml_ui.tst_setup_rolesviz` | V1–V5 (the arm view re-resolves when roles or instances change; wG3 units) | re-pointed (C-6b.1 What 5) |
| TS9 | T | `qml_ui.tst_setup_flow` | Resets the maps (56, 58); a trunk plan from roles (596–605) | re-pointed (C-6b.1 What 5) |
| TS10 | T | `qml_ui.tst_setup_smoke` | The Witmotion chain: `setNominalCalibration` on A and B, refine on each (277–290); roles reset (180) | re-pointed (C-12b) |
| TS11 | T | `qml_ui.tst_setup_calib_hackmotion` | H28–H30 link loss; roles reset and a staged wG3 pair (92, 714) | re-pointed (C-13b) |
| TS12 | T | `qml_ui.tst_setup_nav` N15 | A Recalibrate re-runs the clear on slot A (456–461) | re-pointed |
| TS13 | T | `qml_ui.tst_setup_guide_render` (75), `tests/setup/support/CalibDriver.qml` (460), `tests/setup/support/SetupDriver.qml` (132–135) | Reset the three maps before each case | re-pointed (C-6b.1 What 5) |
| TS14 | T | `session_setup_lint_test` W5 (the `Q_INVOKABLE` lists of `imu_instance.h`, `hm_instance.h`, 729–730) and W9 (PL5) | QML calls only real invokables; no slot-letter API | re-pointed |

Not covered by any test:
- No C++ test links `ImuInstance` or `HmInstance`: both are only in the app's list, `CMakeLists.txt:728, 733`. So no C++ test calls a set or clear method, `invalidateCalibration` or `fullyCalibrated`.
- `imu_calibration_test`, `trunk_stand_tall_test`, `trunk_ceremony_file_test` and `trunk_turn_path_test` test the maths those methods call.
- No test covers CL3 (zero), CL7 (teardown) or RR8 (the bare-pin migration through the manager). RR9 is covered only at the function (`imu_role_map_test` H1–H10).
- `camera_roi_test` also links `imu_role_map.cpp` (`src/Gui/tests/CMakeLists.txt:265`), so C-6a.1's move must re-point it.

### Homonyms and passing mentions (no duty)

| ID | Kind | Site | What | Goes |
|---|---|---|---|---|
| NP1 | N | `src/Metrics/*` (`MetricRequirements::imuRoles`, `SetupFacts::imuRoles`), `src/Analysis/analysis_stage.h`, `hackmotion_gate_test.cpp`, `metric_catalogue_test.cpp`, `setup_capability_test.cpp`, `review/metric_catalog.{h,cpp}`, `setup_capability_rows.js`, `capability_facts.js` (21, 80, 97, 104), `SetupContext.qml:576`, `tst_setup_flow.qml` (698–913), `tst_setup_capability.qml`, `tests/probes/ready_forecast.qml` | **Not the property:** the list of segment roles a route needs or a set-up has | n/a |
| NP2 | N | `ppcp_host_service.{h,cpp}`, `settings/PhonesPanel.qml:56` | Comments placing the phone alias key "beside `imuAlias`" | n/a |
| NP3 | N | The set-up step key `"calibrateTrunk"`: `SetupSteps.qml:245, 263`, `pages/CheckTrunkPage.qml:181`, `tst_setup_trunk.qml` (the `walkTo`, `current`, `state`, `plan` lines), `tst_setup_mounts.qml:226`, `tst_setup_flow.qml:605`, `SetupDriver.qml:21, 86, 220, 221` | **Not the method:** the step's name | n/a |
| NP4 | N | `settings/MicrophonesPanel.qml:42, 43`, `media/transcription_controller.cpp:294` | **Not a sensor:** `setCalibrationActive` (C-6b.2's check-1 grep has no word boundary) | n/a |
| NP5 | N | Comments that name a property or a method in passing: `hm_unit_id.h:30`, `hm_unit_id_test.cpp:15`, `hm_instance.cpp:54, 149`, `hm_instance.h:116`, `ImuMounts.qml:24, 26, 58`, `live_wrist_angles.h:86`, `segment_role.h:27`, `imu_device.h:61`, `PpImuPanel.qml:54, 348`, `ArmVizView.qml:45`, `SetupContext.qml:30`, `session_setup_ui_test.cpp:475, 479`, `tst_setup_rolesviz.qml:7`, `trunk_ceremony_file.h`, `trunk_stand_tall.h`, `trunk_ceremony_recorder.h`, `wt9011dcl_ble.cpp:120` (stale: `zeroToCurrentPose` is not called from `setCalibration`), the test comments in `trunk_turn_path_test.cpp`, `trunk_ceremony_file_test.cpp` and `trunk_stand_tall_test.cpp` | n/a |

---

## 7. The checks, as run

- **Check 1, the unused two.** `grep -rn 'imuCalibration\|imuMountOrientation\|"imu/calibration"\|"imu/mountOrientation"' src tools` returns 18 lines, all in `app_settings.h` (CA1, MO1).
- **Check 1, per-property uses outside `app_settings.*`.** The regex is `\b(set)?[iI]mu<Name>\b` over `src` `*.cpp *.h *.qml *.js`; 237 lines, about 100 of them homonyms (NP1, NP2, AL8's local).
- **Production uses** (not under `tests/`, homonyms out):

  | Property | QML or JS | C++ |
  |---|---|---|
  | roles | 9 lines: 5 `appSettings.`, 4 `settings.` (RO6–RO13) | 2: 1 read, 1 write (RO3, RO4) |
  | alias | 2 `settings.` (RO12, AL9) | 9 (AL2–AL8) |
  | excluded | 7: 5 `appSettings.`, 1 `settings.`, 1 `input.` in a `.js` (EX4–EX8) | 2 (EX2, EX3) |
  | placement | 1 | 0 |
  | calibration | 0 | 0 |
  | mountOrientation | 0 | 0 |

  The design's notes had roles 7/2, alias 0/11 and excluded 6/2. The difference is `SetupContext`'s `settings.` alias and the count rule; this table is the one to use.
- **Check 2** finds **9** `Q_INVOKABLE`s, not 8, at `imu_instance.h:254–313`: `beginZeroing` (284) is the ninth.
- **Check 3** finds 144 lines in `hm_instance.{h,cpp}` (HM1–HM10, CL5, NC2, NP5).
- **Check 4** finds 52 lines. `sensorKey` has **no hit**: the key function is `aliasKey` (RR7), which the brief's grep does not name.
- **C-6b.2's own check 1** finds 143 lines. Its grep has no word boundary, so it also catches the microphone (NP4) and the step key (NP3). The same names plus `beginZeroing|zeroAll` over `*.h *.js` add 25.
- **The raw sensor keys:** `"imu/(roles|placement|rolesMigrated|alias|excluded)"` finds 15 lines.
- **The listeners**, which check 1's trailing `\b` cannot match: `\b(on)?[iI]mu<Name>Changed\b` outside `app_settings.*` finds 6 lines (RO4, RO5, RO15, EX3, PL3).

## 8. The ledger

One line per distinct `file:line` of every grep above, as `file :: ID :: text`. The script in `NOTES-C-6i.md` re-runs the greps and checks each against this ledger, by file and text.

<!-- LEDGER-BEGIN -->
```
src/Analysis/analysis_stage.h :: NP1 :: // that a wG3 was worn, which `imuRoles` alone cannot express.
src/Analysis/tests/hackmotion_gate_test.cpp :: NP1 :: // ⚠ AND THE AXIS MUST STAY SEPARATE FROM imuRoles. That list says which SEGMENTS
src/Analysis/tests/hackmotion_gate_test.cpp :: NP1 :: // like from imuRoles alone, which is the whole reason the axis is separate.
src/Analysis/tests/hackmotion_gate_test.cpp :: NP1 :: c.imuRoles = { SegmentRole::LeadForearm, SegmentRole::LeadHand };
src/Analysis/tests/hackmotion_gate_test.cpp :: NP1 :: // simplification folded hasHackMotion into imuRoles, this is what fails —
src/Analysis/tests/hackmotion_gate_test.cpp :: NP1 :: everyRole.imuRoles = { SegmentRole::LeadForearm, SegmentRole::LeadHand,
src/Analysis/tests/hackmotion_gate_test.cpp :: NP1 :: foreOnly.imuRoles = { SegmentRole::LeadForearm };
src/Analysis/tests/trunk_turn_path_test.cpp :: NP5 :: // → trunk_stand_tall::solveLatest (calibrateTrunk's solve) for A and M
src/Analysis/tests/trunk_turn_path_test.cpp :: NP5 :: // ── Stand tall: 3 s upright and still; calibrateTrunk's 2 s window ends at the step's end ──
src/Analysis/tests/trunk_turn_path_test.cpp :: NP5 :: // ── Stand tall (ImuInstance::calibrateTrunk) ──
src/Analysis/tests/trunk_turn_path_test.cpp :: NP5 :: // fuseRawImu, then the arm calibration solve (solveSegment, as ImuInstance::setFunctionalCalibration)
src/Gui/app/app_settings.cpp :: RO1 :: const bool migrated = ppSettings().value(QStringLiteral("imu/rolesMigrated"), false).toBool();
src/Gui/app/app_settings.cpp :: RO1 :: const pinpoint::imu_roles::RolesLoad load = pinpoint::imu_roles::loadRoles(
src/Gui/app/app_settings.cpp :: RO1 :: ppSettings().value(QStringLiteral("imu/roles"), QVariantMap{}).toMap(),
src/Gui/app/app_settings.cpp :: RO1 :: : ppSettings().value(QStringLiteral("imu/placement"), QVariantMap{}).toMap());
src/Gui/app/app_settings.cpp :: RO1 :: ppSettings().setValue(QStringLiteral("imu/roles"), m_imuRoles);
src/Gui/app/app_settings.cpp :: RO1 :: ppSettings().setValue(QStringLiteral("imu/rolesMigrated"), true);
src/Gui/app/app_settings.cpp :: PL1 :: m_imuPlacement = pinpoint::imu_roles::placementFromRoles(m_imuRoles);
src/Gui/app/app_settings.cpp :: RO2 :: ppSettings().setValue(QStringLiteral("imu/roles"), v);
src/Gui/app/app_settings.cpp :: RO2 :: ppSettings().setValue(QStringLiteral("imu/rolesMigrated"), true);
src/Gui/app/app_settings.cpp :: PL1 :: m_imuPlacement = pinpoint::imu_roles::placementFromRoles(m_imuRoles);
src/Gui/app/app_settings.h :: CA1 :: Q_PROPERTY(QVariantMap imuCalibration READ imuCalibration WRITE setImuCalibration NOTIFY imuCalibrationChanged)
src/Gui/app/app_settings.h :: MO1 :: Q_PROPERTY(QVariantMap imuMountOrientation READ imuMountOrientation WRITE setImuMountOrientation NOTIFY imuMountOrientationChanged)
src/Gui/app/app_settings.h :: EX1 :: m_imuExcluded = ppSettings().value(QStringLiteral("imu/excluded"), QStringList{}).toStringList();
src/Gui/app/app_settings.h :: MO1 :: m_imuMountOrientation = ppSettings().value(QStringLiteral("imu/mountOrientation"), QVariantMap{}).toMap();
src/Gui/app/app_settings.h :: AL1 :: m_imuAlias = ppSettings().value(QStringLiteral("imu/alias"), QVariantMap{}).toMap();
src/Gui/app/app_settings.h :: CA1 :: m_imuCalibration = ppSettings().value(QStringLiteral("imu/calibration"), QVariantMap{}).toMap();
src/Gui/app/app_settings.h :: MO1 :: QVariantMap imuMountOrientation() const { return m_imuMountOrientation; }
src/Gui/app/app_settings.h :: CA1 :: QVariantMap imuCalibration() const { return m_imuCalibration; }
src/Gui/app/app_settings.h :: EX1 :: ppSettings().setValue(QStringLiteral("imu/excluded"), v);
src/Gui/app/app_settings.h :: MO1 :: if (m_imuMountOrientation == v) return;
src/Gui/app/app_settings.h :: MO1 :: m_imuMountOrientation = v;
src/Gui/app/app_settings.h :: MO1 :: ppSettings().setValue(QStringLiteral("imu/mountOrientation"), v);
src/Gui/app/app_settings.h :: MO1 :: emit imuMountOrientationChanged();
src/Gui/app/app_settings.h :: AL1 :: ppSettings().setValue(QStringLiteral("imu/alias"), v);
src/Gui/app/app_settings.h :: CA1 :: if (m_imuCalibration == v) return;
src/Gui/app/app_settings.h :: CA1 :: m_imuCalibration = v;
src/Gui/app/app_settings.h :: CA1 :: ppSettings().setValue(QStringLiteral("imu/calibration"), v);
src/Gui/app/app_settings.h :: CA1 :: emit imuCalibrationChanged();
src/Gui/app/app_settings.h :: MO1 :: void imuMountOrientationChanged();
src/Gui/app/app_settings.h :: CA1 :: void imuCalibrationChanged();
src/Gui/app/app_settings.h :: MO1 :: QVariantMap m_imuMountOrientation;
src/Gui/app/app_settings.h :: CA1 :: QVariantMap m_imuCalibration;
src/Gui/calibration/HackMotionArmRoutine.qml :: CL4 :: // clear host-side — the device holds the calibration and clearCalibration() /
src/Gui/calibration/HackMotionArmRoutine.qml :: CL4 :: // setNominalCalibration() do not exist on it. An in-flight routine is aborted so a
src/Gui/calibration/ImuCalibrationFlow.qml :: RO10 :: readonly property var _roleDeps: [imuManager.instances, appSettings.imuRoles, imuManager.imuDeviceList]
src/Gui/calibration/TrunkRoutine.qml :: SE5 :: // hold bar: both sensors still and upright for `hold`, then calibrateTrunk() on both. A
src/Gui/calibration/TrunkRoutine.qml :: SE5 :: hold: 2200, // the hold bar: calibrateTrunk reads the 2 s BEFORE the call, plus a tick's margin
src/Gui/calibration/TrunkRoutine.qml :: SE5 :: // The last records: { pelvis, thorax } from calibrateTrunk; { away, through } from checkTurn.
src/Gui/calibration/TrunkRoutine.qml :: SE5 :: // the last 250 ms — imu_instance.h). The stillness that counts is calibrateTrunk's own gate (the
src/Gui/calibration/TrunkRoutine.qml :: CL2 :: segs[i].clearFunctionalCalibration()
src/Gui/calibration/TrunkRoutine.qml :: CL1 :: segs[i].clearCalibration()
src/Gui/calibration/TrunkRoutine.qml :: SE5 :: var p = pelvisImu.calibrateTrunk("pelvis")
src/Gui/calibration/TrunkRoutine.qml :: SE5 :: var t = thoraxImu.calibrateTrunk("thorax")
src/Gui/calibration/WitmotionArmRoutine.qml :: SE3 :: // mount-validation gate. It calls ImuInstance-only methods (clearCalibration,
src/Gui/calibration/WitmotionArmRoutine.qml :: SE3 :: // setNominalCalibration, refineMountAboutLongAxis, calibArmDown, mountDeviationDeg,
src/Gui/calibration/WitmotionArmRoutine.qml :: CL4 :: // only clearCalibration() — which resets the legacy transform and leaves the
src/Gui/calibration/WitmotionArmRoutine.qml :: CL4 :: // run left it. clearFunctionalCalibration() is the one that resets those.
src/Gui/calibration/WitmotionArmRoutine.qml :: CL4 :: // refineMountAboutLongAxis()'s |φ| > 25° "precise refine REJECTED" guard or fails
src/Gui/calibration/WitmotionArmRoutine.qml :: CL1 :: if (seg.clearCalibration) seg.clearCalibration()
src/Gui/calibration/WitmotionArmRoutine.qml :: CL2 :: if (seg.clearFunctionalCalibration) seg.clearFunctionalCalibration()
src/Gui/calibration/WitmotionArmRoutine.qml :: SE4a :: s2.refineMountAboutLongAxis(ref, r._phiFromAbduction(s2), false)
src/Gui/calibration/WitmotionArmRoutine.qml :: SE3 :: r.leadImu.setNominalCalibration(r._refA, false)
src/Gui/calibration/WitmotionArmRoutine.qml :: SE3 :: r.slotB.setNominalCalibration(r._refB, false)
src/Gui/calibration/WitmotionArmRoutine.qml :: SE3 :: r.slotC.setNominalCalibration(r._refC, false)
src/Gui/home/HmRigCapability.qml :: EX7 :: cameraExcluded: appSettings.cameraExcluded, imuExcluded: appSettings.imuExcluded
src/Gui/imu/CapturePage.qml :: CL3 :: onClicked: if (ctrl) ctrl.zeroOrientation()
src/Gui/imu/ImuMounts.qml :: NP5 :: // Roles are the SETTINGS spelling (AppSettings::imuRoles, segment_role.h placementRoleName):
src/Gui/imu/ImuMounts.qml :: NP5 :: // survive only as the legacy imuPlacement view, which this file never reads.
src/Gui/imu/ImuMounts.qml :: RR1 :: // palm → leadHand; imu_role_map.cpp claimRole). Its one non-empty choice is "leadForearm".
src/Gui/imu/ImuMounts.qml :: NP5 :: // §16.2: SetupSteps' calibrateTrunk / checkTrunk, the ceremony of §6.3), registered in the same
src/Gui/imu/PpImuPanel.qml :: NP5 :: // as cameras. Seeded from appSettings.imuExcluded at startup and re-seeded by
src/Gui/imu/PpImuPanel.qml :: RO8 :: var roles = appSettings.imuRoles
src/Gui/imu/PpImuPanel.qml :: NP5 :: // TODO: per-session IMU location override (defaults to appSettings.imuRoles)
src/Gui/imu/hm_instance.cpp :: NP5 :: // SourceDescriptor::identifier in Phase B and the AppSettings::imuPlacement key
src/Gui/imu/hm_instance.cpp :: HM3 :: // measured, in fact, from the calibration that carried no axis information
src/Gui/imu/hm_instance.cpp :: HM3 :: // at all — so a default of 0 would read as "calibrated, superbly" before
src/Gui/imu/hm_instance.cpp :: HM3 :: // is what an applied calibration approaches).
src/Gui/imu/hm_instance.cpp :: HM3 :: m_calibrationPhase = WR_CALP_IDLE;
src/Gui/imu/hm_instance.cpp :: HM3 :: m_calibrationState = WR_CAL_UNKNOWN;
src/Gui/imu/hm_instance.cpp :: HM3 :: m_calibrationAbortReason = WR_CAL_ABORT_NONE;
src/Gui/imu/hm_instance.cpp :: NP5 :: // AppSettings::imuPlacement key in Phase C.
src/Gui/imu/hm_instance.cpp :: HM5 :: // ── The calibration surface ──────────────────────────────────────────────
src/Gui/imu/hm_instance.cpp :: HM5 :: connect(m_worker, &HmSessionWorker::calibrationPhaseEvent, this,
src/Gui/imu/hm_instance.cpp :: HM5 :: const QString from = QString::fromLatin1(wr_calibration_phase_name(
src/Gui/imu/hm_instance.cpp :: HM5 :: static_cast<wr_calibration_phase>(previousPhase)));
src/Gui/imu/hm_instance.cpp :: HM5 :: const QString to = QString::fromLatin1(wr_calibration_phase_name(
src/Gui/imu/hm_instance.cpp :: HM5 :: static_cast<wr_calibration_phase>(phase)));
src/Gui/imu/hm_instance.cpp :: HM5 :: // shows would prefer the WORST calibration available — the no-raise attempt
src/Gui/imu/hm_instance.cpp :: HM5 :: m_calibrationPhase = phase;
src/Gui/imu/hm_instance.cpp :: HM5 :: m_calibrationState = libraryState;
src/Gui/imu/hm_instance.cpp :: HM5 :: m_calibrationAbortReason = abortReason;
src/Gui/imu/hm_instance.cpp :: HM5 :: emit calibrationStateChanged();
src/Gui/imu/hm_instance.cpp :: HM5 :: // did it stop at" is the first question about a calibration that did not
src/Gui/imu/hm_instance.cpp :: HM5 :: ppInfo() << "[HmInstance]" << m_deviceId << "— calibration" << from << "→" << to
src/Gui/imu/hm_instance.cpp :: HM6 :: connect(m_worker, &HmSessionWorker::calibrationPresenceMeasured, this,
src/Gui/imu/hm_instance.cpp :: HM6 :: m_calibrationState = libraryState;
src/Gui/imu/hm_instance.cpp :: HM6 :: emit calibrationStateChanged();
src/Gui/imu/hm_instance.cpp :: HM6 :: // because its one sound use is catching "calibration never happened or was
src/Gui/imu/hm_instance.cpp :: HM6 :: " it inverts — §8.2 measured a no-raise calibration scoring BEST on"
src/Gui/imu/hm_instance.cpp :: HM6 :: "calibration is PRESENT, not that it is good.")
src/Gui/imu/hm_instance.cpp :: HM7 :: connect(m_worker, &HmSessionWorker::calibrationPresenceUnmeasured, this,
src/Gui/imu/hm_instance.cpp :: HM7 :: m_calibrationState = libraryState;
src/Gui/imu/hm_instance.cpp :: HM7 :: emit calibrationStateChanged();
src/Gui/imu/hm_instance.cpp :: HM7 :: "samples arrived. The calibration is UNVERIFIED, "
src/Gui/imu/hm_instance.cpp :: HM8 :: connect(m_worker, &HmSessionWorker::calibrationStateRefreshed, this,
src/Gui/imu/hm_instance.cpp :: HM8 :: if (m_calibrationState == libraryState) return;
src/Gui/imu/hm_instance.cpp :: HM8 :: m_calibrationState = libraryState;
src/Gui/imu/hm_instance.cpp :: HM8 :: emit calibrationStateChanged();
src/Gui/imu/hm_instance.cpp :: CL5 :: connect(m_worker, &HmSessionWorker::calibrationLost, this,
src/Gui/imu/hm_instance.cpp :: CL5 :: invalidateCalibration(libraryState, why);
src/Gui/imu/hm_instance.cpp :: HM4 :: connect(m_worker, &HmSessionWorker::calibrationCallRefused, this,
src/Gui/imu/hm_instance.cpp :: HM4 :: // spelled out here rather than folded into one "calibration error".
src/Gui/imu/hm_instance.cpp :: HM4 :: advice = QStringLiteral("the sensor is not streaming, and calibration "
src/Gui/imu/hm_instance.cpp :: HM4 :: advice = QStringLiteral("there is no calibration routine at that step.");
src/Gui/imu/hm_instance.cpp :: HM4 :: emit calibrationCallRefused(status, call);
src/Gui/imu/hm_instance.cpp :: HM10 :: // mean anything AT REST WITH A STRAIGHT WRIST. Uncalibrated and in motion
src/Gui/imu/hm_instance.cpp :: HM10 :: " uncalibrated reads ~15° and an applied calibration reads 0.4-0.8°."
src/Gui/imu/hm_instance.cpp :: HM10 :: " In any other pose an uncalibrated pair reaches 180° routinely —"
src/Gui/imu/hm_instance.cpp :: HM10 :: " score either: it is blind to the calibration axis.";
src/Gui/imu/hm_instance.cpp :: HM9 :: // its own calibration (before that the streamed quaternion carries
src/Gui/imu/hm_instance.cpp :: HM9 :: const bool deviceCalibrated = (m_calibrationState == WR_CAL_CALIBRATED);
src/Gui/imu/hm_instance.cpp :: HM9 :: // AT REST WITH A STRAIGHT WRIST: ~15° uncalibrated, 0.4-0.8° once the
src/Gui/imu/hm_instance.cpp :: HM10 :: // Idempotent: the ids are invalidated as they go, and deregisterSource() is
src/Gui/imu/hm_instance.cpp :: HM3 :: return m_calibrationState == WR_CAL_CALIBRATED;
src/Gui/imu/hm_instance.cpp :: HM3 :: bool HmInstance::calibrationActive() const
src/Gui/imu/hm_instance.cpp :: HM3 :: // calibrationState is for.
src/Gui/imu/hm_instance.cpp :: HM3 :: return m_calibrationPhase != WR_CALP_IDLE
src/Gui/imu/hm_instance.cpp :: HM3 :: && m_calibrationPhase != WR_CALP_COMPLETE
src/Gui/imu/hm_instance.cpp :: HM3 :: && m_calibrationPhase != WR_CALP_ABORTED;
src/Gui/imu/hm_instance.cpp :: HM4 :: // every wr_calibration_* call on the thread that owns the session, and blocking
src/Gui/imu/hm_instance.cpp :: HM4 :: // calibration. wr_calibration_confirm_reference_pose() returns before its
src/Gui/imu/hm_instance.cpp :: HM4 :: // calibrationCallRefused(status, call), which is the entire refusal channel. A UI
src/Gui/imu/hm_instance.cpp :: HM4 :: // ⚠ QueuedConnection for the reserve, exactly like the calibration entry points
src/Gui/imu/hm_instance.cpp :: HM3 :: // The caller emits calibrationStateChanged() — this is only ever called as part
src/Gui/imu/hm_instance.cpp :: CL5 :: void HmInstance::invalidateCalibration(int libraryState, const QString &why)
src/Gui/imu/hm_instance.cpp :: CL5 :: // reset and the signal are unconditional: a UI that misses one invalidation
src/Gui/imu/hm_instance.cpp :: CL5 :: // goes on claiming a calibration that is gone, which is the failure this whole
src/Gui/imu/hm_instance.cpp :: CL5 :: || m_calibrationState == WR_CAL_CALIBRATED
src/Gui/imu/hm_instance.cpp :: CL5 :: || calibrationActive()
src/Gui/imu/hm_instance.cpp :: CL5 :: m_calibrationPhase = WR_CALP_IDLE;
src/Gui/imu/hm_instance.cpp :: CL5 :: m_calibrationState = libraryState;
src/Gui/imu/hm_instance.cpp :: CL5 :: m_calibrationAbortReason = WR_CAL_ABORT_NONE;
src/Gui/imu/hm_instance.cpp :: CL5 :: emit calibrationStateChanged();
src/Gui/imu/hm_instance.cpp :: CL5 :: emit calibrationInvalidated();
src/Gui/imu/hm_instance.cpp :: CL5 :: ppWarn() << "[HmInstance]" << m_deviceId << "— calibration invalidated:" << why
src/Gui/imu/hm_instance.cpp :: CL5 :: // Nothing had been calibrated, so saying "lost" would invent a loss. The
src/Gui/imu/hm_instance.cpp :: CL5 :: // surface is still reset, because "nothing was calibrated" is exactly what
src/Gui/imu/hm_instance.cpp :: CL5 :: << "— calibration surface reset (" << why
src/Gui/imu/hm_instance.cpp :: CL5 :: << "); nothing had been calibrated. Library state" << libraryState;
src/Gui/imu/hm_instance.cpp :: NC2 :: // ⚠ The UI must not offer "calibrate" against a link that has gone: the call
src/Gui/imu/hm_instance.cpp :: NC2 :: // WHICH state to move to — see invalidateCalibration().
src/Gui/imu/hm_instance.cpp :: CL5 :: // and disconnected() in EITHER order; whichever lands first consumes
src/Gui/imu/hm_instance.cpp :: CL5 :: .arg(fromError ? QStringLiteral("error") : QStringLiteral("disconnected"))
src/Gui/imu/hm_instance.cpp :: CL5 :: // ⚠ Whatever the display said, the device's calibration did not survive
src/Gui/imu/hm_instance.cpp :: CL5 :: // calibrationLost() and invalidateCalibration() empties the whole surface.
src/Gui/imu/hm_instance.cpp :: CL5 :: // calibration state that leaves behind — UNCALIBRATED for a link-down,
src/Gui/imu/hm_instance.h :: HM1 :: // the legacy single-factor fallback (calibrated + calibTransform).
src/Gui/imu/hm_instance.h :: HM1 :: Q_PROPERTY(bool calibrated READ calibrated NOTIFY calibratedChanged)
src/Gui/imu/hm_instance.h :: HM1 :: Q_PROPERTY(QQuaternion calibTransform READ calibTransform NOTIFY calibratedChanged)
src/Gui/imu/hm_instance.h :: NP5 :: // AppSettings::imuPlacement key (Phase C). Both are PERSISTED, so the
src/Gui/imu/hm_instance.h :: HM1 :: // calibrates into ITS anatomical convention (§8.1), which the specification
src/Gui/imu/hm_instance.h :: HM1 :: // its calibration AND a candidate having been selected. While either is
src/Gui/imu/hm_instance.h :: HM1 :: bool calibrated() const { return false; }
src/Gui/imu/hm_instance.h :: HM1 :: void calibratedChanged();
src/Gui/imu/hm_instance.h :: HM2 :: // decode and fusion; the device owns calibration; and one peripheral produces
src/Gui/imu/hm_instance.h :: HM2 :: // PHASE C SCOPE. The device's own calibration routine, driven from QML through
src/Gui/imu/hm_instance.h :: HM2 :: // the five Q_INVOKABLEs below and observed through the calibration properties.
src/Gui/imu/hm_instance.h :: HM2 :: // wr_session_calibration_state() read WR_CAL_CALIBRATED. Skipping it leaves
src/Gui/imu/hm_instance.h :: HM2 :: // nothing (see invalidateCalibration()), and there is deliberately NO
src/Gui/imu/hm_instance.h :: HM3 :: // on it, or lets a coach "recalibrate until the number goes down", would
src/Gui/imu/hm_instance.h :: HM3 :: // systematically prefer the worst calibration available. Its one sound use
src/Gui/imu/hm_instance.h :: HM3 :: // is catching "calibration never happened or was lost", where the gap is an
src/Gui/imu/hm_instance.h :: HM3 :: // invalidation — a stale angle from the previous attempt sitting on screen is
src/Gui/imu/hm_instance.h :: HM3 :: Q_PROPERTY(double presenceAngleDeg READ presenceAngleDeg NOTIFY calibrationStateChanged)
src/Gui/imu/hm_instance.h :: HM3 :: Q_PROPERTY(int calibrationPhase READ calibrationPhase NOTIFY calibrationStateChanged)
src/Gui/imu/hm_instance.h :: HM3 :: // DERIVABLE FROM THE OTHER. `calibrationPhase` (wr_calibration_phase) is
src/Gui/imu/hm_instance.h :: HM3 :: // WHERE THE ROUTINE IS; `calibrationState` (wr_calibration_state) is WHAT IS
src/Gui/imu/hm_instance.h :: HM3 :: // back from wr_session_calibration_state() on every phase event rather than
src/Gui/imu/hm_instance.h :: HM3 :: Q_PROPERTY(int calibrationState READ calibrationState NOTIFY calibrationStateChanged)
src/Gui/imu/hm_instance.h :: HM3 :: // ImuDeviceBase::fullyCalibrated() for a wG3: calibrationState reads
src/Gui/imu/hm_instance.h :: HM3 :: // the device owns its calibration — so there is nothing else to gate on.
src/Gui/imu/hm_instance.h :: HM3 :: // Link-down drives the state back (invalidateCalibration()), so this cannot
src/Gui/imu/hm_instance.h :: HM3 :: Q_PROPERTY(bool fullyCalibrated READ fullyCalibrated NOTIFY calibrationStateChanged)
src/Gui/imu/hm_instance.h :: HM3 :: // wr_calibration_abort_reason. ⚠ NOT A SYNONYM FOR "ABORTED":
src/Gui/imu/hm_instance.h :: HM3 :: Q_PROPERTY(int calibrationAbortReason READ calibrationAbortReason NOTIFY calibrationStateChanged)
src/Gui/imu/hm_instance.h :: HM3 :: // Evidence about the HOLD, not about the calibration: the largest angular
src/Gui/imu/hm_instance.h :: HM3 :: Q_PROPERTY(double poseSpreadMaxDeg READ poseSpreadMaxDeg NOTIFY calibrationStateChanged)
src/Gui/imu/hm_instance.h :: HM3 :: Q_PROPERTY(int presenceSamplesUsed READ presenceSamplesUsed NOTIFY calibrationStateChanged)
src/Gui/imu/hm_instance.h :: HM3 :: // false and puts WR_CAL_ABORT_CALLER in calibrationAbortReason instead.)
src/Gui/imu/hm_instance.h :: HM3 :: Q_PROPERTY(bool presenceNotMeasured READ presenceNotMeasured NOTIFY calibrationStateChanged)
src/Gui/imu/hm_instance.h :: HM3 :: // reach; it is NOT a claim that anything has been calibrated.
src/Gui/imu/hm_instance.h :: HM3 :: Q_PROPERTY(bool calibrationActive READ calibrationActive NOTIFY calibrationStateChanged)
src/Gui/imu/hm_instance.h :: HM3 :: // ⚠ THE UI MUST NOT OFFER "CALIBRATE" WITHOUT THIS. wr_calibration_begin()
src/Gui/imu/hm_instance.h :: HM3 :: // calibration is not a standalone transaction. Under our one-stream cycle the
src/Gui/imu/hm_instance.h :: HM3 :: // calibration took, because it is a rotation magnitude and therefore
src/Gui/imu/hm_instance.h :: HM3 :: // uncalibrated and 0.4-0.8° means the transform is applied and holding. In any
src/Gui/imu/hm_instance.h :: HM3 :: // every invalidation.
src/Gui/imu/hm_instance.h :: HM3 :: // carry this flag the mistake is permanent and invisible". Pre-calibration
src/Gui/imu/hm_instance.h :: HM3 :: int calibrationPhase() const { return m_calibrationPhase; }
src/Gui/imu/hm_instance.h :: HM3 :: int calibrationState() const { return m_calibrationState; }
src/Gui/imu/hm_instance.h :: HM3 :: int calibrationAbortReason() const { return m_calibrationAbortReason; }
src/Gui/imu/hm_instance.h :: HM3 :: bool calibrationActive() const;
src/Gui/imu/hm_instance.h :: HM4 :: // library's threading contract puts every wr_calibration_* call on the one
src/Gui/imu/hm_instance.h :: HM4 :: // a BlockingQueuedConnection deliberately: a calibration call must never block
src/Gui/imu/hm_instance.h :: HM4 :: // device watches continuously. wr_calibration_confirm_reference_pose() is
src/Gui/imu/hm_instance.h :: HM4 :: // calibrationCallRefused(status, call) — that signal is the entire refusal
src/Gui/imu/hm_instance.h :: HM3 :: void calibrationStateChanged();
src/Gui/imu/hm_instance.h :: HM4 :: void calibrationCallRefused(int status, const QString &call);
src/Gui/imu/hm_instance.h :: CL5 :: // link-down (which ALWAYS invalidates — §8.3 measured 0.70° → 18.80° at the
src/Gui/imu/hm_instance.h :: CL5 :: void calibrationInvalidated();
src/Gui/imu/hm_instance.h :: CL5 :: // then calibrationInvalidated().
src/Gui/imu/hm_instance.h :: CL5 :: // last session", and none may ever be added. §8.3: a calibration is lost by a
src/Gui/imu/hm_instance.h :: CL5 :: // deliberately ships no wr_calibration_save()/_load() because such a
src/Gui/imu/hm_instance.h :: CL5 :: void invalidateCalibration(int libraryState, const QString &why);
src/Gui/imu/hm_instance.h :: HM3 :: // ── The calibration surface. Written only from the worker's queued
src/Gui/imu/hm_instance.h :: HM3 :: // calibration signals and from invalidateCalibration(); the enums live in
src/Gui/imu/hm_instance.h :: HM3 :: int m_calibrationPhase = 0; // WR_CALP_IDLE
src/Gui/imu/hm_instance.h :: HM3 :: int m_calibrationState = 0; // WR_CAL_UNKNOWN
src/Gui/imu/hm_instance.h :: HM3 :: int m_calibrationAbortReason = 0; // WR_CAL_ABORT_NONE
src/Gui/imu/hm_instance.h :: HM3 :: // ⚠ NaN, not 0. Zero degrees is the reading a perfectly applied calibration
src/Gui/imu/imu_device.h :: NP5 :: // AppSettings::imuExcluded and the manager's own map.
src/Gui/imu/imu_instance.cpp :: SE5 :: // lambda; consumer: calibrateTrunk() on the GUI thread, which copies it out oldest-first.
src/Gui/imu/imu_instance.cpp :: SE5 :: ring->push(in); // calibrateTrunk()'s window (a short mutex, no allocation)
src/Gui/imu/imu_instance.cpp :: CL3 :: void ImuInstance::zeroOrientation()
src/Gui/imu/imu_instance.cpp :: CL3 :: clearCalibration();
src/Gui/imu/imu_instance.cpp :: SE1 :: void ImuInstance::setCalibration(const QQuaternion &armDown, const QQuaternion &tPose)
src/Gui/imu/imu_instance.cpp :: SE1 :: qWarning() << "ImuInstance::setCalibration: arm-down→T-pose angle is"
src/Gui/imu/imu_instance.cpp :: CL1 :: void ImuInstance::clearCalibration()
src/Gui/imu/imu_instance.cpp :: SE2 :: void ImuInstance::setFunctionalCalibration(const QQuaternion &refRaw,
src/Gui/imu/imu_instance.cpp :: SE3 :: void ImuInstance::setNominalCalibration(const QQuaternion &refRaw, bool handMount)
src/Gui/imu/imu_instance.cpp :: SE4a :: void ImuInstance::refineMountAboutLongAxis(const QQuaternion &refRaw, double phiDeg, bool handMount)
src/Gui/imu/imu_instance.cpp :: CL2 :: void ImuInstance::clearFunctionalCalibration()
src/Gui/imu/imu_instance.cpp :: SE5 :: QVariantMap ImuInstance::calibrateTrunk(const QString &role)
src/Gui/imu/imu_instance.cpp :: SE5 :: ppWarn() << "[ImuInstance]" << m_deviceDescription << "— calibrateTrunk: unknown role"
src/Gui/imu/imu_instance.h :: SE5 :: struct ImuStandTallRing; // imu_instance.cpp — the last few seconds, for calibrateTrunk()
src/Gui/imu/imu_instance.h :: SE3 :: // setNominalCalibration. 0 until calibrated.
src/Gui/imu/imu_instance.h :: SE5 :: // The trunk stand-tall solve that set A/M (calibrateTrunk below), in calibrateTrunk's own
src/Gui/imu/imu_instance.h :: CL3 :: Q_INVOKABLE void zeroOrientation();
src/Gui/imu/imu_instance.h :: SE1 :: Q_INVOKABLE void setCalibration(const QQuaternion &armDown, const QQuaternion &tPose);
src/Gui/imu/imu_instance.h :: CL1 :: Q_INVOKABLE void clearCalibration();
src/Gui/imu/imu_instance.h :: SE2 :: Q_INVOKABLE void setFunctionalCalibration(const QQuaternion &refRaw,
src/Gui/imu/imu_instance.h :: CL2 :: Q_INVOKABLE void clearFunctionalCalibration();
src/Gui/imu/imu_instance.h :: SE3 :: Q_INVOKABLE void setNominalCalibration(const QQuaternion &refRaw, bool handMount = false);
src/Gui/imu/imu_instance.h :: SE4a :: Q_INVOKABLE void refineMountAboutLongAxis(const QQuaternion &refRaw,
src/Gui/imu/imu_instance.h :: NC5 :: Q_INVOKABLE void beginZeroing();
src/Gui/imu/imu_instance.h :: SE5 :: // (clearFunctionalCalibration + clearCalibration, as WitmotionArmRoutine._reset does).
src/Gui/imu/imu_instance.h :: SE5 :: Q_INVOKABLE QVariantMap calibrateTrunk(const QString &role);
src/Gui/imu/imu_instance.h :: SE5 :: // The last few seconds of samples, for calibrateTrunk(). Filled by the I/O-thread data lambda
src/Gui/imu/imu_instance.h :: SE1 :: // Derived from setCalibration() — maps raw sensor quaternion to anatomical world frame.
src/Gui/imu/imu_instance.h :: SE5 :: QVariantMap m_trunkRecord; // calibrateTrunk's map for that solve; else empty
src/Gui/imu/imu_manager.cpp :: EX2 :: m_sessionExcluded = s->imuExcluded();
src/Gui/imu/imu_manager.cpp :: EX3 :: // `appSettings.imuExcluded = list` (the Enable pill's handler), and a
src/Gui/imu/imu_manager.cpp :: EX3 :: const QStringList now = m_appSettings->imuExcluded();
src/Gui/imu/imu_manager.cpp :: AL2 :: QVariantMap aliasMap = s->imuAlias();
src/Gui/imu/imu_manager.cpp :: AL2 :: s->setImuAlias(aliasMap);
src/Gui/imu/imu_manager.cpp :: AL3 :: const QVariantMap aliasMap = s->imuAlias();
src/Gui/imu/imu_manager.cpp :: AL4 :: const QVariantMap aliasMap = s->imuAlias();
src/Gui/imu/imu_manager.cpp :: RO3 :: return m_appSettings ? m_appSettings->imuRoles() : AppSettings().imuRoles();
src/Gui/imu/imu_manager.cpp :: RO4 :: // ONE write, after the whole map is built. AppSettings::setImuRoles guards on
src/Gui/imu/imu_manager.cpp :: RO4 :: s->setImuRoles(roles);
src/Gui/imu/imu_manager.cpp :: RR1 :: pinpoint::imu_roles::claimRole(currentRoles(), deviceId, role, factsFn());
src/Gui/imu/imu_manager.cpp :: CL3 :: wt->zeroOrientation();
src/Gui/imu/imu_manager.cpp :: AL5 :: return pinpoint::imu_roles::sensorDisplayName(deviceId, description, s->imuAlias(), currentRoles());
src/Gui/imu/imu_manager.cpp :: AL6 :: void ImuManager::setImuAlias(const QString &key, const QString &alias)
src/Gui/imu/imu_manager.cpp :: AL6 :: QVariantMap map = s->imuAlias();
src/Gui/imu/imu_manager.cpp :: AL6 :: s->setImuAlias(map);
src/Gui/imu/imu_manager.h :: EX2 :: // Seeded from AppSettings::imuExcluded at startup (and re-seeded by the
src/Gui/imu/imu_manager.h :: AL6 :: Q_INVOKABLE void setImuAlias(const QString &key, const QString &alias);
src/Gui/imu/imu_manager.h :: RO3 :: // Unit-keyed placement. AppSettings::imuRoles maps a PLACEMENT KEY to a role name:
src/Gui/imu/imu_manager.h :: AL5 :: // remembered under the alias key). Rules: imu_roles::sensorDisplayName. Stage 5d: setup's
src/Gui/imu/imu_manager.h :: CL3 :: Q_INVOKABLE void zeroAll();
src/Gui/imu/imu_manager.h :: EX2 :: // Per-session exclusion (device ids). Seeded from AppSettings::imuExcluded;
src/Gui/imu/imu_role_map.cpp :: RR1 :: ClaimResult claimRole(const QVariantMap &roles, const QString &deviceId, const QString &role,
src/Gui/imu/imu_role_map.h :: RR7 :: // The alias map (AppSettings::imuAlias) is keyed "<description>|<deviceId>" — this is that
src/Gui/imu/imu_role_map.h :: RR6 :: inline QString stripDeviceIds(QString s)
src/Gui/imu/imu_role_map.h :: RR6 :: inline QString sensorDisplayName(const QString &deviceId, const QString &presentDescription,
src/Gui/imu/imu_role_map.h :: RR6 :: alias = stripDeviceIds(alias);
src/Gui/imu/imu_role_map.h :: RR6 :: desc = stripDeviceIds(desc);
src/Gui/imu/imu_role_map.h :: RR3 :: // The derived, read-only `imuPlacement` view: the arm roles as letters under the same
src/Gui/imu/imu_role_map.h :: RR3 :: inline QVariantMap placementFromRoles(const QVariantMap &roles)
src/Gui/imu/imu_role_map.h :: RR2 :: inline RolesLoad loadRoles(bool alreadyMigrated, const QVariantMap &storedRoles,
src/Gui/imu/imu_role_map.h :: RR1 :: ClaimResult claimRole(const QVariantMap &roles, const QString &deviceId, const QString &role,
src/Gui/imu/trunk_ceremony_recorder.h :: NP5 :: // step of that name: a map carrying "role" (calibrateTrunk's) goes under result[role], so one
src/Gui/imu/trunk_ceremony_recorder.h :: NP5 :: // stepStart("standTall") … hold bar fills … pelvis.calibrateTrunk("pelvis"), thorax.calibrateTrunk("thorax")
src/Gui/imu/trunk_ceremony_recorder.h :: NP5 :: // A/M (the stand-tall solve's, once calibrateTrunk applied one), runs checkTrunkTurn, attaches
src/Gui/media/transcription_controller.cpp :: NP4 :: void TranscriptionController::setCalibrationActive(bool active)
src/Gui/monitor/resource_monitor_controller.cpp :: AL8 :: const QVariantMap imuAliasMap = appSettings.imuAlias();
src/Gui/monitor/resource_monitor_controller.cpp :: AL8 :: const QString imuAlias = imuAliasMap.value(imuKey,
src/Gui/monitor/resource_monitor_controller.cpp :: AL8 :: sourceAliases[sid] = imuAlias + rowSuffix;
src/Gui/monitor/resource_monitor_controller.cpp :: AL8 :: dev[QStringLiteral("name")] = imuAlias + rowSuffix;
src/Gui/review/PpDataViewer.qml :: PL2 :: imuPlacementPairs: { var m = appSettings.imuPlacement; return Object.keys(m).reduce(function(a, k) { a.push(k, String(m[k])); return a }, []) } // settings fallback when device.role absent: serial, slot pairs
src/Gui/review/metric_catalog.cpp :: NP1 :: QList<pinpoint::catalogui::Capability> MetricCatalog::setupSummary(bool faceOn, bool dtl, const QStringList &imuRoles,
src/Gui/review/metric_catalog.cpp :: NP1 :: for (const QString &name : imuRoles) {
src/Gui/review/metric_catalog.cpp :: NP1 :: f.imuRoles.push_back(r);
src/Gui/review/metric_catalog.h :: NP1 :: // Metrics/setup_capability.h). The setup is its five facts (the setup facts' faceOn, dtl, imuRoles, hackMotion
src/Gui/review/metric_catalog.h :: NP1 :: Q_INVOKABLE QList<pinpoint::catalogui::Capability> setupSummary(bool faceOn, bool dtl, const QStringList &imuRoles,
src/Gui/review/swing_data_source.h :: PL2 :: // from QML (root.sessionType, appSettings.imuPlacement as serial, slot pairs). NOTIFY
src/Gui/review/swing_data_source.h :: PL2 :: // Coalesce refollow() and reload(): swingDir/session/sessionType/imuPlacement are all set in the same
src/Gui/settings/ImusPanel.qml :: EX4 :: readonly property bool isExcluded: appSettings.imuExcluded.indexOf(imuData.id) >= 0
src/Gui/settings/ImusPanel.qml :: RO6 :: var roles = appSettings.imuRoles
src/Gui/settings/ImusPanel.qml :: AL6 :: onEditingFinished: imuManager.setImuAlias(imuData.imuKey, text)
src/Gui/settings/ImusPanel.qml :: EX5 :: var list = appSettings.imuExcluded
src/Gui/settings/ImusPanel.qml :: EX5 :: appSettings.imuExcluded = list
src/Gui/settings/ImusPanel.qml :: CL3 :: onClicked: if (imuRow.inst) imuRow.inst.zeroOrientation()
src/Gui/settings/ImusPanel.qml :: EX6 :: var ex = appSettings.imuExcluded
src/Gui/settings/ImusPanel.qml :: RO7 :: var held = Object.values(appSettings.imuRoles)
src/Gui/settings/MicrophonesPanel.qml :: NP4 :: onCalActiveChanged: controller.setCalibrationActive(calActive)
src/Gui/settings/MicrophonesPanel.qml :: NP4 :: Component.onDestruction: if (calActive) controller.setCalibrationActive(false)
src/Gui/settings/PhonesPanel.qml :: NP2 :: // — and sits beside `cameraAlias`/`imuAlias` in the settings file rather than
src/Gui/setup/SetupContext.qml :: NP5 :: // imuConnected change, imu_manager.cpp createInstance) and appSettings.imuRoles (placement).
src/Gui/setup/SetupContext.qml :: RO11 :: var roles = settings.imuRoles // roleForDevice() is Q_INVOKABLE, not reactive
src/Gui/setup/SetupContext.qml :: RO12 :: var named = settings.imuAlias !== undefined && settings.imuRoles !== undefined
src/Gui/setup/SetupContext.qml :: RO11 :: var _map = settings.imuRoles
src/Gui/setup/SetupContext.qml :: AL9 :: var named = settings.imuAlias !== undefined // used below: displayNameForDevice reads it
src/Gui/setup/SetupContext.qml :: RO13 :: // make the manager refuse a new one (imu_role_map.cpp claimRole; ImusPanel.holderOf) — by
src/Gui/setup/SetupContext.qml :: RO13 :: var roles = settings.imuRoles, list = imuMgr.imuDeviceList
src/Gui/setup/SetupContext.qml :: NP1 :: // The facts MetricCatalog.setupSummary() takes (handed over by CapRows.summaryOf). imuRoles are the roles in the session whose
src/Gui/setup/SetupContext.qml :: EX8 :: imuMgr.setSessionImuEnabled(devList[j].id, settings.imuExcluded.indexOf(devList[j].id) < 0)
src/Gui/setup/SetupSteps.qml :: NP3 :: key: "calibrateTrunk"; title: qsTr("Calibrate trunk"); eyebrow: Theme.caps(qsTr("Calibrate trunk")); group: "sensors"
src/Gui/setup/SetupSteps.qml :: NP3 :: : draft.state("calibrateTrunk") === "skipped" ? qsTr("Skipped — trunk sensors not calibrated")
src/Gui/setup/capability_facts.js :: NP1 :: // ({ faceOn, dtl, imuRoles, hackMotion, launchMonitor }) and the context setup_capability_rows.js
src/Gui/setup/capability_facts.js :: EX7 :: // cameraExcluded, imuExcluded home: the global exclusion lists (appSettings), by key / id
src/Gui/setup/capability_facts.js :: EX7 :: && !_excluded(input.imuExcluded, e.deviceId)
src/Gui/setup/capability_facts.js :: NP1 :: imuRoles: out, hackMotion: hm, launchMonitor: input.launchMonitor === true }
src/Gui/setup/capability_facts.js :: NP1 :: imuRoles: out, hackMotion: hm, launchMonitor: input.launchMonitor === true }
src/Gui/setup/capability_facts.js :: NP1 :: return { faceOn: f.faceOn, dtl: f.dtl, imuRoles: f.imuRoles, hackMotion: f.hackMotion,
src/Gui/setup/pages/CheckTrunkPage.qml :: NP3 :: onActivated: page.redoCalibration("trunk", "calibrateTrunk")
src/Gui/setup/setup_capability_rows.js :: NP1 :: facts.imuRoles ? Array.prototype.slice.call(facts.imuRoles) : [],
src/Gui/shot/shot_processor.cpp :: AL7 :: const QVariantMap imuAliases = s->imuAlias();
src/Gui/tests/imu_role_map_test.cpp :: TS1 :: const RolesLoad r = loadRoles(false, {}, { { "W1", "A" }, { "W2", "B" } });
src/Gui/tests/imu_role_map_test.cpp :: TS1 :: const RolesLoad r = loadRoles(false, {}, { { "W1", "A" }, { "W2", "B" }, { "W3", "C" } });
src/Gui/tests/imu_role_map_test.cpp :: TS1 :: const RolesLoad r = loadRoles(false, {}, { { kHmL, "A" }, { kHmP, "B" } });
src/Gui/tests/imu_role_map_test.cpp :: TS1 :: const RolesLoad r = loadRoles(false, {}, { { kHm, "A" } });
src/Gui/tests/imu_role_map_test.cpp :: TS1 :: const RolesLoad r = loadRoles(false, {}, { { "W1", "A" }, { "W4", "D" }, { "W5", "other" } });
src/Gui/tests/imu_role_map_test.cpp :: TS1 :: const RolesLoad first = loadRoles(false, {}, legacy);
src/Gui/tests/imu_role_map_test.cpp :: TS1 :: const RolesLoad second = loadRoles(true, first.roles, legacy);
src/Gui/tests/imu_role_map_test.cpp :: TS1 :: loadRoles(false, {}, legacy).roles, first.roles);
src/Gui/tests/imu_role_map_test.cpp :: TS1 :: const RolesLoad r = loadRoles(true, {}, { { "W1", "A" }, { "W2", "B" } });
src/Gui/tests/imu_role_map_test.cpp :: PL3 :: checkMap(QStringLiteral("V1 derived imuPlacement = the legacy map (arm-only)"),
src/Gui/tests/imu_role_map_test.cpp :: TS1 :: placementFromRoles(loadRoles(false, {}, legacy).roles), legacy);
src/Gui/tests/imu_role_map_test.cpp :: TS1 :: placementFromRoles({ { "W1", "pelvis" }, { "W2", "thorax" }, { "W3", "leadUpperArm" } }),
src/Gui/tests/imu_role_map_test.cpp :: PL3 :: // went at Stage 5c — imuPlacement is read-only.)
src/Gui/tests/imu_role_map_test.cpp :: TS1 :: sensorDisplayName(id, QString(), { { hmKey, QStringLiteral("Lead arm wG3") } }, {}),
src/Gui/tests/imu_role_map_test.cpp :: TS1 :: sensorDisplayName(id, QString(), { { hmKey, QStringLiteral("HackMotion wG3 ") + id } }, {}),
src/Gui/tests/imu_role_map_test.cpp :: TS1 :: sensorDisplayName(id, QString(), {}, { { id + QStringLiteral("#lowerArm"), QStringLiteral("leadForearm") },
src/Gui/tests/imu_role_map_test.cpp :: TS1 :: sensorDisplayName(id, QString(), {}, { { id, QStringLiteral("leadForearm") } }), QStringLiteral("sensor"));
src/Gui/tests/imu_role_map_test.cpp :: TS1 :: sensorDisplayName(id, QStringLiteral("WT901BLE68"),
src/Gui/tests/imu_role_map_test.cpp :: TS1 :: sensorDisplayName(id, QStringLiteral("WT901BLE68"),
src/Gui/tests/imu_role_map_test.cpp :: TS1 :: const ClaimResult c = claimRole(roles, "W2", "leadForearm",
src/Gui/tests/imu_role_map_test.cpp :: TS1 :: const ClaimResult c = claimRole({ { "GONE", "leadForearm" }, { "W3", "leadHand" } }, "W2",
src/Gui/tests/imu_role_map_test.cpp :: TS1 :: const ClaimResult c = claimRole({ { "OLD#lowerArm", "leadForearm" }, { "OLD#palm", "leadHand" } },
src/Gui/tests/imu_role_map_test.cpp :: TS1 :: const ClaimResult c = claimRole({ { kHmL, "leadForearm" }, { kHmP, "leadHand" } }, "W1",
src/Gui/tests/imu_role_map_test.cpp :: TS1 :: const ClaimResult c = claimRole({ { kHm, "leadForearm" } }, kHm, "leadForearm",
src/Gui/tests/imu_role_map_test.cpp :: TS1 :: const ClaimResult bad = claimRole({}, kHm, "leadHand", Table().hackmotion(kHm).fn());
src/Gui/tests/imu_role_map_test.cpp :: TS1 :: const ClaimResult trunk = claimRole({}, kHm, "pelvis", Table().hackmotion(kHm).fn());
src/Gui/tests/imu_role_map_test.cpp :: TS1 :: const ClaimResult c = claimRole(roles, "W1", "leadHand",
src/Gui/tests/imu_role_map_test.cpp :: TS1 :: const ClaimResult fore = claimRole({ { "W1", "leadForearm" } }, kHm, "leadForearm",
src/Gui/tests/imu_role_map_test.cpp :: TS1 :: const ClaimResult hand = claimRole({ { "W2", "leadHand" } }, kHm, "leadForearm",
src/Gui/tests/imu_role_map_test.cpp :: TS1 :: ClaimResult c = claimRole(roles, kHm, "leadForearm", f);
src/Gui/tests/imu_role_map_test.cpp :: TS1 :: c = claimRole(roles, "P", "pelvis", f);
src/Gui/tests/imu_role_map_test.cpp :: TS1 :: c = claimRole(roles, "T", "thorax", f);
src/Gui/tests/imu_role_map_test.cpp :: TS1 :: claimRole({ { "W1", "leadHand" } }, "W1", "leadForearm", f).roles,
src/Gui/tests/imu_role_map_test.cpp :: TS1 :: claimRole({ { "W1", "leadForearm" } }, "W1", "leadForearm", f).ok);
src/Gui/tests/imu_role_map_test.cpp :: TS1 :: claimRole({ { kHmL, "leadForearm" }, { kHmP, "leadHand" }, { kHm, "leadForearm" },
src/Gui/tests/imu_role_map_test.cpp :: TS1 :: !claimRole({}, "W1", "D", f).ok);
src/Gui/tests/imu_role_map_test.cpp :: TS1 :: ppSettings().setValue(QStringLiteral("imu/placement"), legacy);
src/Gui/tests/imu_role_map_test.cpp :: TS1 :: checkMap(QStringLiteral("A1 first load migrates imu/placement"), s.imuRoles(),
src/Gui/tests/imu_role_map_test.cpp :: PL3 :: checkMap(QStringLiteral("A1 derived imuPlacement"), s.imuPlacement(),
src/Gui/tests/imu_role_map_test.cpp :: TS1 :: ppSettings().value(QStringLiteral("imu/rolesMigrated")).toBool());
src/Gui/tests/imu_role_map_test.cpp :: TS1 :: ppSettings().value(QStringLiteral("imu/placement")).toMap(), legacy);
src/Gui/tests/imu_role_map_test.cpp :: TS1 :: s.setImuRoles({ { "W1", "leadForearm" }, { "W2", "leadHand" }, { "P", "pelvis" } });
src/Gui/tests/imu_role_map_test.cpp :: PL3 :: check(QStringLiteral("A2 a trunk-only change still notifies imuPlacement"),
src/Gui/tests/imu_role_map_test.cpp :: TS1 :: s.setImuRoles(s.imuRoles());
src/Gui/tests/imu_role_map_test.cpp :: PL3 :: // A4: imuPlacement is READ-ONLY (Stage 5c): its property has no WRITE, and a roles
src/Gui/tests/imu_role_map_test.cpp :: PL3 :: const int pi = s.metaObject()->indexOfProperty("imuPlacement");
src/Gui/tests/imu_role_map_test.cpp :: PL3 :: check(QStringLiteral("A4 imuPlacement is a read-only property"),
src/Gui/tests/imu_role_map_test.cpp :: TS1 :: s.setImuRoles({ { "P", "pelvis" }, { "W2", "leadForearm" } });
src/Gui/tests/imu_role_map_test.cpp :: PL3 :: checkMap(QStringLiteral("A4 the derived view follows the roles"), s.imuPlacement(),
src/Gui/tests/imu_role_map_test.cpp :: TS1 :: ppSettings().value(QStringLiteral("imu/placement")).toMap(), legacy);
src/Gui/tests/imu_role_map_test.cpp :: TS1 :: s.setImuRoles({}); // the coach unassigns everything
src/Gui/tests/imu_role_map_test.cpp :: TS1 :: again.imuRoles(), {});
src/Gui/tests/imu_role_map_test.cpp :: PL3 :: checkMap(QStringLiteral("A5 and the derived view is empty"), again.imuPlacement(), {});
src/Gui/tests/probes/ready_forecast.qml :: NP1 :: { title: "Face-on camera + arm sensors", facts: { faceOn: true, dtl: false, imuRoles: ["leadForearm", "leadHand"], hackMotion: false, launchMonitor: false },
src/Gui/tests/probes/ready_forecast.qml :: NP1 :: { title: "Two cameras, no sensors", facts: { faceOn: true, dtl: true, imuRoles: [], hackMotion: false, launchMonitor: false },
src/Gui/tests/probes/setup_mounts_settings.qml :: RO14 :: log("appSettings.imuRoles " + json(appSettings.imuRoles))
src/Gui/tests/probes/setup_mounts_settings.qml :: PL4 :: log("appSettings.imuPlacement " + json(appSettings.imuPlacement) + " (legacy view, derived)")
src/Gui/tests/session_setup_lint_test.cpp :: PL5 :: // setPlacementForDevice, appSettings.imuPlacement), except the site in kW9Allowed
src/Gui/tests/session_setup_lint_test.cpp :: PL5 :: // labelling, which reads the derived, read-only imuPlacement view on purpose to label swings that
src/Gui/tests/session_setup_lint_test.cpp :: PL5 :: R"((?<![\w$])(instanceForSlot|deviceForSlot|deviceIdForSlot|unitLabelForSlot|placementKeyForSlot|setPlacementForDevice|imuPlacement)(?![\w$]))"));
src/Gui/tests/setup/fakes/FakeImuInstance.qml :: TS3 :: // ── Trunk stand-tall solve (imu_instance.h calibrateTrunk, W2.1a) ────
src/Gui/tests/setup/fakes/FakeImuInstance.qml :: TS3 :: // (cleared by clearFunctionalCalibration, as the real instance's is).
src/Gui/tests/setup/fakes/FakeImuInstance.qml :: TS3 :: // Scripted results of calibrateTrunk(role), oldest first: each call takes the next entry
src/Gui/tests/setup/fakes/FakeImuInstance.qml :: TS3 :: // Scripted outcome of refineMountAboutLongAxis(). The real gate is dev ≤ 15°, grav ≤ 25°.
src/Gui/tests/setup/fakes/FakeImuInstance.qml :: TS3 :: function clearCalibration() {
src/Gui/tests/setup/fakes/FakeImuInstance.qml :: TS3 :: _rec("clearCalibration", [])
src/Gui/tests/setup/fakes/FakeImuInstance.qml :: TS3 :: function clearFunctionalCalibration() {
src/Gui/tests/setup/fakes/FakeImuInstance.qml :: TS3 :: _rec("clearFunctionalCalibration", [])
src/Gui/tests/setup/fakes/FakeImuInstance.qml :: TS3 :: function calibrateTrunk(role) {
src/Gui/tests/setup/fakes/FakeImuInstance.qml :: TS3 :: _rec("calibrateTrunk", [role])
src/Gui/tests/setup/fakes/FakeImuInstance.qml :: TS3 :: function setNominalCalibration(q, handMount) {
src/Gui/tests/setup/fakes/FakeImuInstance.qml :: TS3 :: _rec("setNominalCalibration", [q, handMount])
src/Gui/tests/setup/fakes/FakeImuInstance.qml :: TS3 :: function refineMountAboutLongAxis(ref, phiDeg, handMount) {
src/Gui/tests/setup/fakes/FakeImuInstance.qml :: TS3 :: _rec("refineMountAboutLongAxis", [ref, phiDeg, handMount])
src/Gui/tests/setup/fakes/FakeImuInstance.qml :: TS3 :: function setCalibration(a, b) {
src/Gui/tests/setup/fakes/FakeImuInstance.qml :: TS3 :: _rec("setCalibration", [a, b])
src/Gui/tests/setup/fakes/FakeImuManager.qml :: TS2 :: // Placement is held where the real one holds it — appSettings.imuRoles (the REAL
src/Gui/tests/setup/fakes/FakeImuManager.qml :: TS2 :: // B leadHand, C leadUpperArm, P pelvis, T thorax) written straight onto appSettings.imuRoles; the real class's
src/Gui/tests/setup/fakes/FakeImuManager.qml :: TS2 :: // derived imuPlacement view is read-only (Stage 5c), and the real ImuManager has no …ForSlot
src/Gui/tests/setup/fakes/FakeImuManager.qml :: TS2 :: var roles = appSettings.imuRoles
src/Gui/tests/setup/fakes/FakeImuManager.qml :: TS2 :: var roles = appSettings.imuRoles
src/Gui/tests/setup/fakes/FakeImuManager.qml :: TS2 :: // imu_role_map.cpp claimRole + ImuManager::setRoleForDevice, rule for rule:
src/Gui/tests/setup/fakes/FakeImuManager.qml :: TS2 :: var cur = appSettings.imuRoles, roles = {}
src/Gui/tests/setup/fakes/FakeImuManager.qml :: TS2 :: appSettings.imuRoles = roles
src/Gui/tests/setup/fakes/FakeImuManager.qml :: TS2 :: appSettings.imuRoles = roles
src/Gui/tests/setup/fakes/FakeImuManager.qml :: TS2 :: var roles = appSettings.imuRoles
src/Gui/tests/setup/fakes/FakeImuManager.qml :: TS2 :: // imu_roles::sensorDisplayName, rule for rule (imu_role_map.h): the coach's alias (not the
src/Gui/tests/setup/fakes/FakeImuManager.qml :: TS2 :: // remembered in its appSettings.imuAlias key ("<description>|<id>") — else "HackMotion wG3"
src/Gui/tests/setup/fakes/FakeImuManager.qml :: TS2 :: var aliases = appSettings.imuAlias || ({})
src/Gui/tests/setup/fakes/FakeImuManager.qml :: TS2 :: var roles = appSettings.imuRoles
src/Gui/tests/setup/fakes/FakeImuManager.qml :: TS2 :: var cur = appSettings.imuRoles
src/Gui/tests/setup/fakes/FakeImuManager.qml :: TS2 :: appSettings.imuRoles = map
src/Gui/tests/setup/session_setup_ui_test.cpp :: NP5 :: // appSettings.imuRoles, so this matters more here than it does there.
src/Gui/tests/setup/session_setup_ui_test.cpp :: NP5 :: // all handed whatever a file wrote — a theme, imuRoles, cameraFixedInPlace, imuExcluded —
src/Gui/tests/setup/support/CalibDriver.qml :: TS13 :: appSettings.imuRoles = ({})
src/Gui/tests/setup/support/SetupDriver.qml :: NP3 :: // "calibrateTrunk","checkTrunk","ready".
src/Gui/tests/setup/support/SetupDriver.qml :: NP3 :: "calibrateArm", "checkArm", "calibrateTrunk", "checkTrunk", "ready"]
src/Gui/tests/setup/support/SetupDriver.qml :: TS13 :: appSettings.imuRoles = ({})
src/Gui/tests/setup/support/SetupDriver.qml :: TS13 :: appSettings.imuAlias = ({})
src/Gui/tests/setup/support/SetupDriver.qml :: TS13 :: appSettings.imuExcluded = []
src/Gui/tests/setup/support/SetupDriver.qml :: NP3 :: if (p !== null && p.stepKey === "calibrateTrunk" && trunkPace !== null) p.routine.pace = trunkPace
src/Gui/tests/setup/support/SetupDriver.qml :: NP3 :: trunkGolfer.routine = p !== null && p.stepKey === "calibrateTrunk" ? p.routine : null
src/Gui/tests/setup/tst_setup_calib_hackmotion.qml :: TS11 :: appSettings.imuRoles = ({})
src/Gui/tests/setup/tst_setup_calib_hackmotion.qml :: TS11 :: appSettings.imuRoles = ({ "HM-1#lowerArm": "leadForearm", "HM-1#palm": "leadHand" })
src/Gui/tests/setup/tst_setup_calib_witmotion.qml :: TS5 :: appSettings.imuRoles = ({})
src/Gui/tests/setup/tst_setup_calib_witmotion.qml :: TS5 :: var capA = drv.firstCall("WT-A", "setNominalCalibration")
src/Gui/tests/setup/tst_setup_calib_witmotion.qml :: TS5 :: var refA = drv.firstCall("WT-A", "refineMountAboutLongAxis")
src/Gui/tests/setup/tst_setup_calib_witmotion.qml :: TS5 :: // setNominalCalibration on A, B, C with each sensor's averaged (constant) quat.
src/Gui/tests/setup/tst_setup_calib_witmotion.qml :: TS5 :: compare(inst.countCalls("setNominalCalibration"), 1, s + " setNominalCalibration")
src/Gui/tests/setup/tst_setup_calib_witmotion.qml :: TS5 :: compare(inst.countCalls("refineMountAboutLongAxis"), 1, s + " refineMountAboutLongAxis")
src/Gui/tests/setup/tst_setup_calib_witmotion.qml :: TS5 :: if (inst.calls[i].name === "setNominalCalibration") q = inst.calls[i].args[0]
src/Gui/tests/setup/tst_setup_calib_witmotion.qml :: TS5 :: motionResetsHold("phase1", "phase1AccumMs", "setNominalCalibration")
src/Gui/tests/setup/tst_setup_calib_witmotion.qml :: TS5 :: motionResetsHold("phase2", "stableAccumMs", "refineMountAboutLongAxis")
src/Gui/tests/setup/tst_setup_calib_witmotion.qml :: TS5 :: compare(drv.callsSince(m, "clearCalibration").filter(function(c) { return c.device === segs[i] }).length,
src/Gui/tests/setup/tst_setup_calib_witmotion.qml :: TS5 :: 1, segs[i] + " clearCalibration")
src/Gui/tests/setup/tst_setup_calib_witmotion.qml :: TS5 :: compare(drv.callsSince(m, "clearFunctionalCalibration").filter(function(c) { return c.device === segs[i] }).length,
src/Gui/tests/setup/tst_setup_calib_witmotion.qml :: TS5 :: 1, segs[i] + " clearFunctionalCalibration")
src/Gui/tests/setup/tst_setup_calib_witmotion.qml :: TS5 :: return c.name === "clearCalibration" || c.name === "clearFunctionalCalibration" })
src/Gui/tests/setup/tst_setup_calib_witmotion.qml :: TS5 :: compare(drv.callsSince(m, "setNominalCalibration").length, 0)
src/Gui/tests/setup/tst_setup_calib_witmotion.qml :: TS5 :: compare(drv.callsSince(m, "setNominalCalibration").length, 0)
src/Gui/tests/setup/tst_setup_calib_witmotion.qml :: TS5 :: return c.name === "setNominalCalibration" || c.name === "refineMountAboutLongAxis" })
src/Gui/tests/setup/tst_setup_calib_witmotion.qml :: TS5 :: compare(drv.imus.C.countCalls("setNominalCalibration"), 1)
src/Gui/tests/setup/tst_setup_calib_witmotion.qml :: TS5 :: compare(drv.imus.C.countCalls("refineMountAboutLongAxis"), 1)
src/Gui/tests/setup/tst_setup_calib_witmotion.qml :: TS5 :: compare(drv.callsSince(m, "setNominalCalibration").length, 0, drv.fmtCalls(drv.callsSince(m)))
src/Gui/tests/setup/tst_setup_calib_witmotion.qml :: TS5 :: compare(drv.callsSince(m2, "clearCalibration").length, 2)
src/Gui/tests/setup/tst_setup_capability.qml :: NP1 :: facts: { faceOn: true, dtl: false, imuRoles: [], hackMotion: false, launchMonitor: false },
src/Gui/tests/setup/tst_setup_capability.qml :: NP1 :: facts: { faceOn: true, dtl: false, imuRoles: ["leadForearm", "leadHand"], hackMotion: true, launchMonitor: false },
src/Gui/tests/setup/tst_setup_capability.qml :: NP1 :: facts: { faceOn: true, dtl: false, imuRoles: ["leadForearm", "leadHand"], hackMotion: false, launchMonitor: false },
src/Gui/tests/setup/tst_setup_capability.qml :: NP1 :: facts: { faceOn: true, dtl: true, imuRoles: ["pelvis", "thorax"], hackMotion: false, launchMonitor: false },
src/Gui/tests/setup/tst_setup_capability.qml :: NP1 :: facts: { faceOn: true, dtl: true, imuRoles: ["pelvis", "thorax", "leadForearm", "leadHand"], hackMotion: true,
src/Gui/tests/setup/tst_setup_capability.qml :: NP1 :: facts: { faceOn: false, dtl: false, imuRoles: [], hackMotion: false, launchMonitor: false },
src/Gui/tests/setup/tst_setup_capability.qml :: NP1 :: facts: { faceOn: true, dtl: true, imuRoles: [], hackMotion: false, launchMonitor: false },
src/Gui/tests/setup/tst_setup_capability.qml :: NP1 :: facts: { faceOn: false, dtl: false, imuRoles: [], hackMotion: false, launchMonitor: false },
src/Gui/tests/setup/tst_setup_capability.qml :: NP1 :: var s = { facts: { faceOn: true, dtl: true, imuRoles: ["pelvis", "thorax", "leadForearm", "leadHand"],
src/Gui/tests/setup/tst_setup_flow.qml :: TS9 :: appSettings.imuRoles = ({})
src/Gui/tests/setup/tst_setup_flow.qml :: TS9 :: appSettings.imuExcluded = []
src/Gui/tests/setup/tst_setup_flow.qml :: NP3 :: compare(flow().plan, ["intent", "cameras", "ball", "imus", "calibrateTrunk", "checkTrunk", "ready"])
src/Gui/tests/setup/tst_setup_flow.qml :: NP1 :: compare(ctx().setupFacts.imuRoles.slice().sort(), ["leadForearm", "leadHand"])
src/Gui/tests/setup/tst_setup_flow.qml :: NP1 :: compare(ctx().setupFacts.imuRoles, [])
src/Gui/tests/setup/tst_setup_flow.qml :: NP1 :: compare(ctx().setupFacts.imuRoles.slice().sort(), ["leadForearm", "leadHand"])
src/Gui/tests/setup/tst_setup_flow.qml :: NP1 :: compare(ctx().setupFacts.imuRoles, [])
src/Gui/tests/setup/tst_setup_flow.qml :: NP1 :: var groups = catalog.setupSummary(!!f.faceOn, !!f.dtl, Array.prototype.slice.call(f.imuRoles || []),
src/Gui/tests/setup/tst_setup_flow.qml :: NP1 :: var after = catalog.setupSummary(!!af.faceOn, !!af.dtl, Array.prototype.slice.call(af.imuRoles || []),
src/Gui/tests/setup/tst_setup_guide_render.qml :: TS13 :: appSettings.imuRoles = ({})
src/Gui/tests/setup/tst_setup_mounts.qml :: TS7 :: appSettings.imuRoles = ({})
src/Gui/tests/setup/tst_setup_mounts.qml :: TS7 :: appSettings.imuRoles = ({ "WT-1": "leadForearm", "WT-2": "leadHand", "X": "leadUpperArm",
src/Gui/tests/setup/tst_setup_mounts.qml :: NP3 :: compare(d.plan(), ["intent", "cameras", "framing", "ball", "imus", "calibrateTrunk", "checkTrunk", "ready"])
src/Gui/tests/setup/tst_setup_mounts.qml :: TS7 :: compare(appSettings.imuRoles["WT-A"], undefined)
src/Gui/tests/setup/tst_setup_mounts.qml :: TS7 :: appSettings.imuRoles = ({ "WT-1": "pelvis" }) // as a hand-edited settings file would
src/Gui/tests/setup/tst_setup_nav.qml :: TS12 :: var clr0 = a.countCalls("clearFunctionalCalibration")
src/Gui/tests/setup/tst_setup_nav.qml :: TS12 :: compare(a.countCalls("clearFunctionalCalibration"), clr0 + 1, "no fresh run (no reset of slot A)")
src/Gui/tests/setup/tst_setup_rolesviz.qml :: NP5 :: // dependency, appSettings.imuRoles + imuManager.instances / imuDeviceList /
src/Gui/tests/setup/tst_setup_rolesviz.qml :: TS8 :: appSettings.imuRoles = ({})
src/Gui/tests/setup/tst_setup_rolesviz.qml :: TS8 :: appSettings.imuRoles = ({})
src/Gui/tests/setup/tst_setup_rolesviz.qml :: TS8 :: compare(appSettings.imuRoles["HM-1#lowerArm"], "leadForearm")
src/Gui/tests/setup/tst_setup_sensors.qml :: TS6 :: appSettings.imuRoles = roles
src/Gui/tests/setup/tst_setup_sensors.qml :: TS6 :: appSettings.imuAlias = al
src/Gui/tests/setup/tst_setup_sensors.qml :: TS6 :: compare(appSettings.imuRoles["WT-N"], "leadForearm", "the mount is a setting: it sticks")
src/Gui/tests/setup/tst_setup_sensors.qml :: TS6 :: compare(appSettings.imuRoles["WT-A"], "leadForearm", "the holder lost its mount")
src/Gui/tests/setup/tst_setup_sensors.qml :: TS6 :: compare(appSettings.imuRoles["HM-1#palm"], "leadHand", "the pair was not written as a pair")
src/Gui/tests/setup/tst_setup_sensors.qml :: TS6 :: appSettings.imuRoles = roles
src/Gui/tests/setup/tst_setup_sensors.qml :: TS6 :: appSettings.imuAlias = ({})
src/Gui/tests/setup/tst_setup_sensors.qml :: TS6 :: appSettings.imuAlias = al
src/Gui/tests/setup/tst_setup_smoke.qml :: TS10 :: appSettings.imuRoles = ({})
src/Gui/tests/setup/tst_setup_smoke.qml :: TS10 :: tryVerify(function() { return a.countCalls("setNominalCalibration") === 1 }, 15000,
src/Gui/tests/setup/tst_setup_smoke.qml :: TS10 :: "arm-down capture (setNominalCalibration) never recorded")
src/Gui/tests/setup/tst_setup_smoke.qml :: TS10 :: compare(b.countCalls("setNominalCalibration"), 1, "slot B not captured with slot A")
src/Gui/tests/setup/tst_setup_smoke.qml :: TS10 :: compare(a.countCalls("refineMountAboutLongAxis"), 1)
src/Gui/tests/setup/tst_setup_smoke.qml :: TS10 :: compare(b.countCalls("refineMountAboutLongAxis"), 1)
src/Gui/tests/setup/tst_setup_trunk.qml :: TS4 :: var solve = names.indexOf("calibrateTrunk")
src/Gui/tests/setup/tst_setup_trunk.qml :: TS4 :: verify(names.slice(0, solve).indexOf("clearFunctionalCalibration") >= 0, JSON.stringify(names))
src/Gui/tests/setup/tst_setup_trunk.qml :: TS4 :: verify(names.slice(0, solve).indexOf("clearCalibration") >= 0, JSON.stringify(names))
src/Gui/tests/setup/tst_setup_trunk.qml :: TS4 :: compare(p[0].countCalls("calibrateTrunk"), 1)
src/Gui/tests/setup/tst_setup_trunk.qml :: TS4 :: // The attached records are calibrateTrunk's own.
src/Gui/tests/setup/tst_setup_trunk.qml :: TS4 :: compare(pel.countCalls("calibrateTrunk"), 2)
src/Gui/tests/setup/tst_setup_trunk.qml :: TS4 :: compare(tho.countCalls("calibrateTrunk"), 2)
src/Gui/tests/setup/tst_setup_trunk.qml :: TS4 :: var second = names.lastIndexOf("calibrateTrunk"), first = names.indexOf("calibrateTrunk")
src/Gui/tests/setup/tst_setup_trunk.qml :: TS4 :: verify(names.slice(first + 1, second).indexOf("clearFunctionalCalibration") >= 0, JSON.stringify(names))
src/Gui/tests/setup/tst_setup_trunk.qml :: TS4 :: compare(pel.countCalls("calibrateTrunk"), 1, "a turn failure repeated stand tall")
src/Gui/tests/setup/tst_setup_trunk.qml :: TS4 :: verify(names.slice(names.indexOf("calibrateTrunk") + 1, names.lastIndexOf("calibrateTrunk"))
src/Gui/tests/setup/tst_setup_trunk.qml :: TS4 :: .indexOf("clearFunctionalCalibration") >= 0, JSON.stringify(names))
src/Gui/tests/setup/tst_setup_trunk.qml :: TS4 :: compare(pel.countCalls("calibrateTrunk"), 1)
src/Gui/tests/setup/tst_setup_trunk.qml :: TS4 :: compare(pel.countCalls("calibrateTrunk"), 0)
src/Gui/tests/setup/tst_setup_trunk.qml :: TS4 :: compare(pel.countCalls("calibrateTrunk"), 0)
src/Gui/tests/setup/tst_setup_trunk.qml :: TS4 :: compare(pel.countCalls("calibrateTrunk"), 1)
src/Gui/tests/setup/tst_setup_trunk.qml :: TS4 :: compare(pel.countCalls("calibrateTrunk"), 1, "stand tall re-ran on sensors lying flat")
src/Gui/tests/setup/tst_setup_trunk.qml :: TS4 :: compare(pel.countCalls("calibrateTrunk"), 2)
src/Gui/tests/setup/tst_setup_trunk.qml :: NP3 :: return p !== null && d.current() === "calibrateTrunk" ? p : null
src/Gui/tests/setup/tst_setup_trunk.qml :: NP3 :: compare(d.plan(), ["intent", "cameras", "framing", "ball", "imus", "calibrateTrunk", "checkTrunk", "ready"])
src/Gui/tests/setup/tst_setup_trunk.qml :: NP3 :: d.walkTo("calibrateTrunk")
src/Gui/tests/setup/tst_setup_trunk.qml :: NP3 :: d.walkTo("calibrateTrunk")
src/Gui/tests/setup/tst_setup_trunk.qml :: NP3 :: d.walkTo("calibrateTrunk")
src/Gui/tests/setup/tst_setup_trunk.qml :: NP3 :: d.walkTo("calibrateTrunk")
src/Gui/tests/setup/tst_setup_trunk.qml :: NP3 :: compare(d.current(), "calibrateTrunk")
src/Gui/tests/setup/tst_setup_trunk.qml :: NP3 :: compare(d.current(), "calibrateTrunk")
src/Gui/tests/setup/tst_setup_trunk.qml :: NP3 :: compare(d.state("calibrateTrunk"), "pending")
src/Gui/tests/setup/tst_setup_trunk.qml :: NP3 :: d.walkTo("calibrateTrunk")
src/Gui/tests/setup/tst_setup_trunk.qml :: NP3 :: compare(d.current(), "calibrateTrunk")
src/Gui/tests/setup/tst_setup_trunk.qml :: NP3 :: "calibrateTrunk", "checkTrunk", "ready"])
src/Gui/tests/setup/tst_setup_trunk.qml :: NP3 :: d.walkTo("calibrateTrunk")
src/Gui/tests/setup/tst_setup_trunk.qml :: NP3 :: d.walkTo("calibrateTrunk")
src/Gui/tests/setup/tst_setup_trunk.qml :: NP3 :: d.walkTo("calibrateTrunk")
src/Gui/tests/setup/tst_setup_trunk.qml :: NP3 :: d.walkTo("calibrateTrunk")
src/Gui/tests/setup/tst_setup_trunk.qml :: NP3 :: d.walkTo("calibrateTrunk")
src/Gui/tests/setup/tst_setup_trunk.qml :: NP3 :: compare(d.current(), "calibrateTrunk")
src/Gui/tests/setup/tst_setup_trunk.qml :: TS4 :: compare(P.countCalls("calibrateTrunk"), 2)
src/Gui/tests/setup/tst_setup_trunk.qml :: TS4 :: compare(P.countCalls("calibrateTrunk"), 3)
src/Gui/tests/setup/tst_setup_trunk.qml :: NP3 :: d.walkTo("calibrateTrunk")
src/Gui/tests/setup/tst_setup_trunk.qml :: NP3 :: compare(d.current(), "calibrateTrunk")
src/Gui/tests/setup/tst_setup_trunk.qml :: NP3 :: d.walkTo("calibrateTrunk")
src/Gui/viz/ArmVizView.qml :: NP5 :: // IMU mounts (placement roles, AppSettings::imuRoles — design §4.13):
src/Gui/viz/ArmVizView.qml :: RO9 :: readonly property var _roleDeps: [appSettings.imuRoles, imuManager.instances,
src/Gui/viz/live_wrist_angles.h :: NP5 :: // imuPlacement here again would be a seventh copy of that rule, and the copies
src/IMU/hm_unit_id.h :: NP5 :: // SourceDescriptor::identifier (Phase B), as the AppSettings::imuPlacement key
src/IMU/tests/hm_unit_id_test.cpp :: NP5 :: // the AppSettings::imuPlacement key (Phase C)
src/IMU/tests/trunk_ceremony_file_test.cpp :: NP5 :: // call ran, as ImuInstance::calibrateTrunk and TrunkCeremonyRecorder::checkTurn see them — then
src/IMU/tests/trunk_ceremony_file_test.cpp :: NP5 :: // ImuInstance::calibrateTrunk's map, as the live call builds it.
src/IMU/tests/trunk_ceremony_file_test.cpp :: NP5 :: // Live, calibrateTrunk would apply nothing — the first solve stays in force — so must the replay.
src/IMU/tests/trunk_stand_tall_test.cpp :: NP5 :: // ImuInstance::calibrateTrunk() is a thin wrapper: it copies its sample ring out and calls
src/IMU/trunk_ceremony_file.h :: NP5 :: // stand-tall replay is trunk_stand_tall::solveLatest — the function ImuInstance::calibrateTrunk
src/IMU/trunk_ceremony_file.h :: NP5 :: // standTall { pelvis: <calibrateTrunk's map>, thorax: <…> } — keyed by the map's "role";
src/IMU/trunk_ceremony_file.h :: NP5 :: // The solve that left A/M on this sensor, replayed. Live, calibrateTrunk applies only a valid
src/IMU/trunk_stand_tall.h :: NP5 :: // segments"). ImuInstance::calibrateTrunk() takes the LAST 2 s of its own samples; swinglab
src/IMU/trunk_stand_tall.h :: NP5 :: // The record as ImuInstance::calibrateTrunk returns it to QML. The key names are the contract
src/IMU/wt9011dcl_ble.cpp :: NP5 :: // zeroToCurrentPose(), which is called from ImuInstance::setCalibration().
src/Metrics/metric_catalogue_manifest.cpp :: NP1 :: via("wristImus", RM::Inertial, Direct, { .imuRoles = { R::LeadForearm, R::LeadHand } },
src/Metrics/metric_catalogue_manifest.cpp :: NP1 :: via("wristImus", RM::Inertial, Direct, { .imuRoles = { R::LeadForearm, R::LeadHand } },
src/Metrics/metric_catalogue_manifest.cpp :: NP1 :: via("wristImus", RM::Inertial, Direct, { .imuRoles = { R::LeadForearm, R::LeadHand } },
src/Metrics/metric_catalogue_manifest.cpp :: NP1 :: via("wristImus", RM::Inertial, Direct, { .imuRoles = { R::LeadForearm, R::LeadHand } },
src/Metrics/metric_catalogue_manifest.cpp :: NP1 :: { .imuRoles = { R::LeadForearm, R::LeadHand, R::LeadUpperArm } },
src/Metrics/metric_catalogue_manifest.cpp :: NP1 :: { .imuRoles = { R::LeadForearm, R::LeadHand, R::LeadUpperArm } },
src/Metrics/metric_catalogue_manifest.cpp :: NP1 :: via("wristImus", RM::Inertial, Direct, { .imuRoles = { R::LeadForearm } },
src/Metrics/metric_catalogue_manifest.cpp :: NP1 :: via("pelvisImu", RM::Inertial, Direct, { .imuRoles = { R::Pelvis } },
src/Metrics/metric_catalogue_manifest.cpp :: NP1 :: via("pelvisImu", RM::Inertial, Direct, { .imuRoles = { R::Pelvis } },
src/Metrics/metric_catalogue_manifest.cpp :: NP1 :: via("thoraxImu", RM::Inertial, Direct, { .imuRoles = { R::Thorax } },
src/Metrics/metric_catalogue_manifest.cpp :: NP1 :: via("thoraxImu", RM::Inertial, Direct, { .imuRoles = { R::Thorax } },
src/Metrics/metric_catalogue_manifest.cpp :: NP1 :: via("trunkImus", RM::Inertial, Direct, { .imuRoles = { R::Pelvis, R::Thorax } },
src/Metrics/metric_catalogue_manifest.cpp :: NP1 :: via("trunkImus", RM::Inertial, Direct, { .imuRoles = { R::Pelvis, R::Thorax } },
src/Metrics/metric_catalogue_manifest.cpp :: NP1 :: { .faceOnCamera = true, .dtlCamera = true, .imuRoles = { R::Pelvis } },
src/Metrics/metric_catalogue_manifest.cpp :: NP1 :: via("pelvisImu", RM::Inertial, Estimated, { .imuRoles = { R::Pelvis } },
src/Metrics/metric_catalogue_manifest.cpp :: NP1 :: { .faceOnCamera = true, .dtlCamera = true, .imuRoles = { R::Pelvis } },
src/Metrics/metric_catalogue_manifest.cpp :: NP1 :: via("pelvisImu", RM::Inertial, Estimated, { .imuRoles = { R::Pelvis } },
src/Metrics/metric_catalogue_manifest.cpp :: NP1 :: via("trunkImus", RM::Inertial, Direct, { .imuRoles = { R::Thorax } },
src/Metrics/metric_catalogue_manifest.cpp :: NP1 :: via("trunkImus", RM::Inertial, Direct, { .imuRoles = { R::Pelvis, R::Thorax } },
src/Metrics/metric_catalogue_manifest.cpp :: NP1 :: via("thoraxImu", RM::Inertial, Direct, { .imuRoles = { R::Thorax } },
src/Metrics/metric_catalogue_manifest.cpp :: NP1 :: via("pelvisImu", RM::Inertial, Direct, { .imuRoles = { R::Pelvis } },
src/Metrics/metric_catalogue_manifest.cpp :: NP1 :: { .faceOnCamera = true, .imuRoles = { R::Club }, .clubTrack = true },
src/Metrics/metric_catalogue_manifest.cpp :: NP1 :: { .imuRoles = { R::Pelvis, R::Thorax, R::LeadForearm }, .clubTrack = true },
src/Metrics/metric_catalogue_manifest.cpp :: NP1 :: via("pelvisImu", RM::Inertial, Direct, { .imuRoles = { R::Pelvis } },
src/Metrics/metric_catalogue_manifest.cpp :: NP1 :: via("thoraxImu", RM::Inertial, Direct, { .imuRoles = { R::Thorax } },
src/Metrics/metric_catalogue_manifest.cpp :: NP1 :: via("leadArmImus", RM::Inertial, Direct, { .imuRoles = { R::LeadForearm } },
src/Metrics/metric_catalogue_manifest.cpp :: NP1 :: via("clubSensorFused", RM::Fused, Direct, { .imuRoles = { R::Club }, .clubTrack = true },
src/Metrics/metric_catalogue_manifest.cpp :: NP1 :: via("pelvisImu", RM::Inertial, Direct, { .imuRoles = { R::Pelvis } },
src/Metrics/metric_catalogue_manifest.cpp :: NP1 :: via("thoraxImu", RM::Inertial, Direct, { .imuRoles = { R::Thorax } },
src/Metrics/metric_catalogue_manifest.cpp :: NP1 :: via("pelvisImu", RM::Inertial, Direct, { .imuRoles = { R::Pelvis } },
src/Metrics/metric_catalogue_manifest.cpp :: NP1 :: via("thoraxImu", RM::Inertial, Direct, { .imuRoles = { R::Thorax } },
src/Metrics/metric_catalogue_manifest.cpp :: NP1 :: via("leadArmImus", RM::Inertial, Direct, { .imuRoles = { R::LeadForearm } },
src/Metrics/metric_catalogue_manifest.cpp :: NP1 :: via("clubSensorFused", RM::Fused, Direct, { .imuRoles = { R::Club }, .clubTrack = true },
src/Metrics/metric_catalogue_manifest.cpp :: NP1 :: via("sessionLeadArmImu", RM::Derived, Estimated, { .imuRoles = { R::LeadForearm } },
src/Metrics/metric_catalogue_manifest.cpp :: NP1 :: via("sessionPelvisImu", RM::Derived, Estimated, { .imuRoles = { R::Pelvis } },
src/Metrics/metric_catalogue_manifest.cpp :: NP1 :: via("sessionThoraxImu", RM::Derived, Estimated, { .imuRoles = { R::Thorax } },
src/Metrics/metric_catalogue_manifest.cpp :: NP1 :: via("thoraxImu", RM::Inertial, Direct, { .imuRoles = { R::Thorax } },
src/Metrics/metric_catalogue_manifest.cpp :: NP1 :: via("trunkImus", RM::Inertial, Direct, { .imuRoles = { R::Thorax } },
src/Metrics/metric_catalogue_manifest.cpp :: NP1 :: via("thoraxImu+dtl", RM::Fused, Direct, { .dtlCamera = true, .imuRoles = { R::Thorax } },
src/Metrics/metric_catalogue_manifest.cpp :: NP1 :: { .faceOnCamera = true, .dtlCamera = true, .imuRoles = { R::Thorax } },
src/Metrics/metric_descriptor.h :: NP1 :: std::vector<SegmentRole> imuRoles; // required anatomical IMU roles
src/Metrics/metric_descriptor.h :: NP1 :: // `imuRoles` says which anatomical segments a route needs and is blind to WHICH INSTRUMENT
src/Metrics/metric_descriptor.h :: NP1 :: // fields. The difference shows on `imuRoles`, which is an unordered set the manifest authors in
src/Metrics/metric_descriptor.h :: NP1 :: for (SegmentRole have : r.imuRoles)
src/Metrics/metric_descriptor.h :: NP1 :: for (SegmentRole rb : b.imuRoles)
src/Metrics/metric_descriptor.h :: NP1 :: if (std::find(a.imuRoles.begin(), a.imuRoles.end(), rb) == a.imuRoles.end()) return false;
src/Metrics/metric_provider.h :: NP1 :: std::vector<SegmentRole> imuRoles; // roles actually bound & calibrated this shot
src/Metrics/metric_provider.h :: NP1 :: // A HackMotion wG3 measured the lead wrist on this shot. Separate from `imuRoles` because that
src/Metrics/metric_provider.h :: NP1 :: for (SegmentRole x : imuRoles)
src/Metrics/metric_provider.h :: NP1 :: for (SegmentRole r : req.imuRoles)
src/Metrics/setup_capability.cpp :: NP1 :: return !r.faceOnCamera && !r.dtlCamera && r.imuRoles.empty() && !r.clubTrack && !r.ballTrack
src/Metrics/setup_capability.cpp :: NP1 :: for (SegmentRole r : req.imuRoles)
src/Metrics/setup_capability.cpp :: NP1 :: m.imuRoles.push_back(r);
src/Metrics/setup_capability.cpp :: NP1 :: for (SegmentRole r : facts.imuRoles)
src/Metrics/setup_capability.cpp :: NP1 :: addRole(c.imuRoles, r);
src/Metrics/setup_capability.cpp :: NP1 :: addRole(c.imuRoles, SegmentRole::LeadForearm);
src/Metrics/setup_capability.cpp :: NP1 :: addRole(c.imuRoles, SegmentRole::LeadHand);
src/Metrics/setup_capability.cpp :: NP1 :: c.tier = c.imuRoles.empty() ? ReconstructionTier::Angles2D : ReconstructionTier::Mono3DPlusImu;
src/Metrics/setup_capability.h :: NP1 :: std::vector<SegmentRole> imuRoles;
src/Metrics/tests/metric_catalogue_test.cpp :: NP1 :: c.imuRoles = std::move(roles);
src/Metrics/tests/metric_catalogue_test.cpp :: NP1 :: check(pr->baselineRequirement().imuRoles.empty(),
src/Metrics/tests/metric_catalogue_test.cpp :: NP1 :: && fb->routes[0].requirement.imuRoles == std::vector<SegmentRole>{ SegmentRole::Thorax }
src/Metrics/tests/metric_catalogue_test.cpp :: NP1 :: && sb->routes[0].requirement.imuRoles == std::vector<SegmentRole>({ SegmentRole::Pelvis, SegmentRole::Thorax }),
src/Metrics/tests/metric_catalogue_test.cpp :: NP1 :: && d->routes[1].requirement.imuRoles == std::vector<SegmentRole>{ SegmentRole::Pelvis },
src/Metrics/tests/metric_catalogue_test.cpp :: NP1 :: for (SegmentRole role : r.requirement.imuRoles)
src/Metrics/tests/metric_catalogue_test.cpp :: NP1 :: && d->routes[0].requirement.imuRoles == std::vector<SegmentRole>{ row.role }
src/Metrics/tests/metric_catalogue_test.cpp :: NP1 :: const bool thx = r.requirement.imuRoles == std::vector<SegmentRole>{ SegmentRole::Thorax };
src/Metrics/tests/metric_catalogue_test.cpp :: NP1 :: && r.requirement.dtlCamera == rungs[i].dtl && (rungs[i].thorax ? thx : r.requirement.imuRoles.empty());
src/Metrics/tests/setup_capability_test.cpp :: NP1 :: f3.imuRoles = { SegmentRole::LeadForearm, SegmentRole::LeadHand };
src/Metrics/tests/setup_capability_test.cpp :: NP1 :: f4.imuRoles = { SegmentRole::Pelvis, SegmentRole::Thorax };
src/Metrics/tests/setup_capability_test.cpp :: NP1 :: for (SegmentRole role : r.imuRoles)
src/Metrics/tests/setup_capability_test.cpp :: NP1 :: for (SegmentRole r : f.imuRoles)
src/Metrics/tests/setup_capability_test.cpp :: NP1 :: pel.imuRoles = { SegmentRole::Pelvis };
src/Ppcp/ppcp_host_service.cpp :: NP2 :: // the ordinary settings file beside `cameraAlias` and `imuAlias`, which is what
src/Ppcp/ppcp_host_service.cpp :: NP2 :: // `imuAlias` rather than nested like `ppcp/phoneNames` above: those two are
src/Ppcp/ppcp_host_service.cpp :: NP2 :: // priority `cameraAlias`/`imuAlias` have over a device's own reported
src/Ppcp/ppcp_host_service.h :: NP2 :: // and `imuAlias` have to a device's own reported name. Empty clears it,
src/Vocabulary/Analysis/segment_role.h :: NP5 :: // persists `imu/roles` and derives the legacy `imuPlacement` view from it — and
src/Gui/setup/pages/ImusPage.qml :: RO15 :: function onImuRolesChanged() { page._refusals = ({}) }
src/Gui/tests/imu_role_map_test.cpp :: PL3 :: QSignalSpy roles(&s, &AppSettings::imuRolesChanged);
src/Gui/tests/imu_role_map_test.cpp :: PL3 :: QSignalSpy placement(&s, &AppSettings::imuPlacementChanged);
src/Gui/imu/imu_manager.cpp :: EX3 :: connect(m_appSettings, &AppSettings::imuExcludedChanged, this, [this]() {
src/Gui/imu/imu_manager.cpp :: RO5 :: connect(m_appSettings, &AppSettings::imuRolesChanged,
src/Gui/imu/imu_manager.cpp :: RO4 :: // Without m_appSettings there is no imuRolesChanged connection to do this.
```
<!-- LEDGER-END -->

## 9. Findings: see `NOTES-C-6i.md` §3

F34 to F42 are filed there, with the loss flags L1–L4. The rows above cite them.
