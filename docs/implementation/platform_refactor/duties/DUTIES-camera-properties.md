# C-5i · Duties inventory of the six camera rig properties: taken at `4a10080c` (main, after `git pull --ff-only`)

This page is the work list for C-5a.1 (the Rig's camera commands) and C-5b (the cut-over). It is a document: no code, no IM edit, no build, and the app was not run. Every line number is at `4a10080c`. The count script in `NOTES-C-5i.md` matches each grep hit by its file and its text, not its number.

**Kinds.**
- R: reads.
- W: writes.
- C: clears.
- L: listens, through a binding or a handler.
- S: a side effect inside a writer.
- T: a test or a fixture.
- ND: not a duty (a comment).

**Destinations**, from `docs/design/rig_design.md` §4.1, §4.3 and §4.9, and BRIEF-C-5b What 2–6:
- **Rig command** `x`: the writer calls the Rig's command, through `app.rig` from QML, or as a request from a manager.
- **app.rig model**: a QML read or binding becomes `app.rig.*`.
- **typed input**: `CameraManager` or `CameraInstance` is handed the rig state as a typed value.
- **unchanged**: the line reads the live instance's own value, or a value handed to it.
- **removed**: C-5b What 6 deletes the line.
- **NO HOME Fn**: the design does not place the duty; the finding is in NOTES.

**The count today** (the brief's check 2, QML / C++, tests and comments included), beside the design's notes:

| Property | Today | Design | Why it differs |
|---|---|---|---|
| `cameraPerspective` | 2 / 14 | 2/14 | — |
| `cameraIsMirrored` | 3 / 3 | 3/3 | — |
| `cameraFixedInPlace` | 11 / 5 | 6/3 | the tests (5 QML, 1 C++) and 3 comments |
| `cameraBallRoi` | 3 / 5 | 0/5 | `SetupContext.qml:291` reads through `settings.`; 2 test writes |
| `cameraAlias` | 2 / 13 | 0/9 | 5 comments (ppcp, `PhonesPanel`); `CamerasPanel.qml:356` calls the manager's method of the same name |
| `cameraExcluded` | 5 / 5 | 2/4 | 2 test resets, 2 comments |

The brief's grep leaves out `*.js`, which holds 4 more hits: `camera_health.js:24` and `:34`, and `capability_facts.js:45` and `:78` (F33). Check 3 has 36 hits (§7). Total: 111 hits, every one in the ledger (§9).

---

## 1. Per property

All six maps use one key spelling, the camera's `DeviceKey` (§4.6): `description + "|" + (serial, or the device id when there is none)`. It is built by `CameraManager::cameraKey` (`camera_manager.h:211–217`) and `CameraInstance::cameraKey` (`camera_instance.cpp:921–926`), and open-coded in `createController` (`camera_manager.cpp:969–972`; a C-1 site). Each property is loaded once per `AppSettings` instance, and its setter writes the whole value (`app_settings.h:1290–1344`). Its signal has no C++ listener: `grep -rni 'camera…Changed'` finds one QML handler, PE2. Bindings listen implicitly; those are the L rows.

### 1.1 `cameraPerspective`

- **Key:** `camera/perspective`, a map from camera key to code (1 down-the-line, 2 face-on, 3 other, 4 Impact).
- **Absent means** None: None is never stored, because `assignPerspective` removes the entry (`camera_manager.cpp:756–759`).
- **Moves to:** the rig state; command `assignPerspective`.
- **Also read through the perspective:** which crop entry and which tuning key a camera uses (§4).

| ID | Kind | Where | What it does | Goes to |
|---|---|---|---|---|
| PE1 | R | `CamerasPanel.qml:516` | the VIEW combo's `Component.onCompleted` picks the stored code (`\|\| 0`) | app.rig model |
| PE2 | L | `CamerasPanel.qml:524–525` | `Connections { onCameraPerspectiveChanged }` re-selects the combo; the only explicit listener of the six signals | app.rig model (its change notification) |
| PE3 | W | `CamerasPanel.qml:561` | `onActivated` calls `cameraManager.assignPerspective(key, p)`, after seeding the non-Rig keys for Impact (`setImpactExposure`, `applyImpactMode`: exposure, rate, `camera/roi`'s `#impact` strip, 536–560) | Rig command `assignPerspective`; the pre-seeding order is §2.1 |
| PE4 | R | `resource_monitor_controller.cpp:287` | a fresh `AppSettings` each refresh; the code picks the `camera/roi` entry shown for a camera with no live crop | NO HOME F28 |
| PE5 | R | `camera_instance.cpp:135` | constructor: the role's crop, before the ring registration | typed input (`CameraInstance`) |
| PE6 | R | `camera_instance.cpp:147` | constructor, buffer-backed and Impact: rate, locked exposure, gain, strobe | typed input (`CameraInstance`) |
| PE7 | R | `camera_instance.cpp:161` | constructor, other roles: the `#wide` tuning key | typed input (`CameraInstance`) |
| PE8 | R | `camera_role_probe.cpp:145` | the probe snapshots the whole map | NO HOME F29 |
| PE9 | R | `camera_role_probe.cpp:359` | checks the original role is restored | NO HOME F29 |
| PE10 | R | `camera_role_probe.cpp:367` | compares the map with the snapshot | NO HOME F29 |
| PE11 | W | `camera_role_probe.cpp:368` | restores the whole map with one `setCameraPerspective`, bypassing `assignPerspective` (no Impact strip, no live instance set) | NO HOME F29 |
| PE12 | W | `camera_role_probe.cpp:214, 217, 219, 228` | four `assignPerspective` calls on the real cameras | NO HOME F29 |
| PE13 | R | `camera_manager.cpp:153` | `cameraList()`: the `perspective` field (216), the role's crop for `initialWidth`/`Height` (200), the Impact ring rate (282) | typed input (`CameraManager`) |
| PE14 | R | `camera_manager.cpp:738` | `assignPerspective` reads the map and keeps `before` | Rig command `assignPerspective` |
| PE15 | W | `camera_manager.cpp:760` | `assignPerspective` saves the map | Rig command `assignPerspective` |
| PE16 | S | `camera_manager.cpp:741–753` | Impact is exclusive: the other entry is erased from the map, and the other live controller is set to None | Rig command `assignPerspective` (the map half, invariant 1); the live half: NO HOME F26 |
| PE17 | S | `camera_manager.cpp:762–795` | sets the live controller; reconnects every selected camera whose Impact-ness changed; `cameraListChanged` | NO HOME F26 |
| PE18 | R | `camera_manager.cpp:913` | `createPreviewInstance`: the role's crop | typed input (`CameraManager`) |
| PE19 | R | `camera_manager.cpp:979` | `createController`: the role's crop | typed input (`CameraManager`) |
| PE20 | R | `camera_manager.cpp:985` | `createController` applies the code to the instance (`> 0` only); Impact turns pose off | typed input (`CameraInstance`) |
| PE21 | W | `camera_manager.cpp:720–729` | `CameraManager::setPerspective(QObject*, int)`: live only, never persisted, no caller | NO HOME F30 |
| PE22 | R | `capture_context.cpp:224`, `:343`; `shot_processor.cpp:862`; `camera_manager.cpp:647` | read the live instance's `perspective()` | unchanged |
| PE23 | R | `cameraList`'s consumers: 27 QML lines outside tests match `camData\.(perspective\|alias\|enabled)\|\bc\.(perspective\|alias)\b` (e.g. `CamerasPanel.qml:264, 507, 787, 1499`; `PpCameraTiles.qml:450, 463`; `SetupContext.qml:254, 259`), and `SetupContext.qml` reads `list[i].perspective` and `cams[i].perspective` at 134, 140, 160–161, 174 | the list's fields | unchanged (fed by PE13, AL7, EX13) |
| PE24 | W | `app_settings.h:220, 700, 1290–1296, 1758, 1886` | the property, getter, setter, signal, member | removed |

### 1.2 `cameraIsMirrored`

- **Key:** `camera/isMirrored`, a map from camera key to `true`.
- **Absent means** not mirrored: both writers remove the entry for `false` (`camera_manager.cpp:822–825`; `CamerasPanel.qml:584–585`).
- **Moves to:** the rig state; command `setMirrored`.

| ID | Kind | Where | What it does | Goes to |
|---|---|---|---|---|
| MI1 | L | `CamerasPanel.qml:577` | `mirrorChip.isMirrored`, bound to the map | app.rig model |
| MI2 | R | `CamerasPanel.qml:583` | the chip copies the map (a deep copy) | app.rig model |
| MI3 | W | `CamerasPanel.qml:588` | the chip writes the map itself: the only write when no camera is live | Rig command `setMirrored` |
| MI4 | W | `CamerasPanel.qml:594` | with a live instance, also `cameraManager.setIsMirrored(inst, mirrored)`, the clicked value read once before the map write (586): two writers per click, now agreeing (F31c, seen and fixed: NOTES-F31-probe) | Rig command `setMirrored`; the live apply: NO HOME F26 |
| MI5 | R | `camera_manager.cpp:819` | `setIsMirrored` reads the map | Rig command `setMirrored` |
| MI6 | W | `camera_manager.cpp:829` | `setIsMirrored` saves; the instance was set first (815) | Rig command `setMirrored` |
| MI7 | R | `camera_manager.cpp:997` | `createController` restores it onto the instance | typed input (`CameraInstance`) |
| MI8 | R | `capture_context.cpp:225`; `shot_processor.cpp:863` | the live instance's `isMirrored()` | unchanged |
| MI9 | W | `app_settings.h:221, 701, 1298–1304, 1759, 1887` | the property's own lines | removed |

### 1.3 `cameraFixedInPlace`

- **Key:** `camera/fixedInPlace`, a map from camera key to bool. The switch stores an explicit `false` (`CamerasPanel.qml:702`).
- **Absent means** not fixed. Absent and `false` both read as not fixed.
- `CameraInstance` has no such property, so every reader goes to the settings.
- **Moves to:** the rig state; command `setFixedInPlace`.

| ID | Kind | Where | What it does | Goes to |
|---|---|---|---|---|
| FX1 | L | `CamerasPanel.qml:696` | the switch's `fixedVal` binding | app.rig model |
| FX2 | R | `CamerasPanel.qml:701` | the switch copies the map | app.rig model |
| FX3 | W | `CamerasPanel.qml:703` | the switch writes the map. No live effect: the box is applied or withheld only at `createController` (FX10) | Rig command `setFixedInPlace` |
| FX4 | L | `CamerasPanel.qml:709` | the "Yes"/"No" text | app.rig model |
| FX5 | L | `CamerasPanel.qml:2261` | `ballPanel.fixedInPlace`: the "will not be restored next session" warning (2341) | app.rig model |
| FX6 | L | `SetupContext.qml:213` | `anyFixedCamera` (through `settings.`): makes triangulation optional (`SetupSteps.qml:118, 128, 130`; `TriangulatePage.qml:41–82`) | app.rig model |
| FX7 | L | `camera_health.js:34` | `anyNeedsCalibration(mgr, settings)`: a connected camera that is not fixed lights the toolbar, called with `appSettings` by `PpCameraPanel.qml:51` and `PpSessionToolbar.qml:66` | app.rig model (the call sites pass it) |
| FX8 | ND | `camera_health.js:24` | comment | n/a |
| FX9 | ND | `PpCameraPanel.qml:27` | comment | n/a |
| FX10 | R | `camera_manager.cpp:1004` | `createController`: the box is restored only for a fixed camera | typed input (`CameraManager`) |
| FX11 | R | `shot_processor.cpp:864` | the export's fallback when no snapshot exists | NO HOME F28 |
| FX12 | R | `capture_context.cpp:220` | `snapshotAtFreeze`: each camera's fixed flag at the freeze, into the Recording's `streams[].setup.fixedInPlace` (226, 250) | NO HOME F28 |
| FX13 | R | `capture_context.cpp:345` | the same map gates SET-05: the club-length prior is read only for a fixed face-on camera (an analysis input, RP-9) | NO HOME F28 |
| FX14 | ND | `shot_processor.h:443` | comment | n/a |
| FX15 | W | `app_settings.h:225, 705, 1330–1336, 1762, 1890` | the property's own lines | removed |

### 1.4 `cameraBallRoi` (the hitting-area box)

- **Key:** `camera/ballRoi`, a map from camera key to `{x, y, w, h}`, normalised.
- **Absent means** no saved box: `clearBallRoi` removes the entry.
- An entry whose box is empty is never applied at connect (`camera_manager.cpp:1012`), and is ignored by the Ball step (`r.w > 0 && r.h > 0`).
- **Moves to:** the rig state; commands `setHittingArea` and `clearHittingArea`.

| ID | Kind | Where | What it does | Goes to |
|---|---|---|---|---|
| BR1 | R | `SetupContext.qml:291` | `ensureBallRoi`: the Ball step uses the saved box, **fixed or not**, when the live one is empty (called from `BallPage.qml:49, 90`) | app.rig model |
| BR2 | W | `SetupContext.qml:293`, `:295` | `cameraMgr.setBallRoi(inst, saved)`, or the default `0.40, 0.55, 0.20, 0.30`, which also saves it | Rig command `setHittingArea` |
| BR3 | W | `CamerasPanel.qml:2321–2330` | the ball panel's Loader `onLoaded`, when the live box is empty: a saved box is set on the live instance only (2327), as the Ball step does (BR1), so a click or move re-saves the golfer's own box; with none saved, the default is seeded and saved (2329) (F31a and review B1, seen and fixed: NOTES-F31-probe) | Rig command `setHittingArea` |
| BR3a | R | `CamerasPanel.qml:2325` | the ball panel's seed reads `appSettings.cameraBallRoi[key]`, the saved box (F31-probe) | app.rig model |
| BR4 | W | `PpCameraFrame.qml:2166–2167` | a new draw, on release; saved only when at least 0.02 each way (the handles' minimum), so a click, still or jittered by a pixel or two, keeps the box and never reaches `clearBallRoi` (F31b and review B2, seen and fixed: NOTES-F31-probe) | Rig command `setHittingArea` (or `clearHittingArea`) |
| BR5 | W | `PpCameraFrame.qml:2165` | a move or resize, saved once on release | Rig command `setHittingArea` |
| BR6 | S | `PpCameraFrame.qml:2114–2138` | during the drag, `instance.setRoi` only, never saved: already "one command at the end of the edit" | unchanged |
| BR7 | R | `camera_manager.cpp:857` | `setBallRoi` reads the map, after setting the live box (853) | Rig command `setHittingArea` |
| BR8 | W | `camera_manager.cpp:869` | `setBallRoi` saves | Rig command `setHittingArea` |
| BR9 | C | `camera_manager.cpp:845–847` | `setBallRoi` with an empty box calls `clearBallRoi` | Rig command `clearHittingArea` |
| BR10 | R | `camera_manager.cpp:882` | `clearBallRoi` reads the map, after clearing the live box (878) | Rig command `clearHittingArea` |
| BR11 | C | `camera_manager.cpp:889` | `clearBallRoi` saves the map without the entry | Rig command `clearHittingArea` |
| BR12 | R | `camera_manager.cpp:1005` | `createController`: restores the box onto a fixed camera's instance | typed input (`CameraInstance`) |
| BR13 | R | `capture_context.cpp:227`; `shot_processor.cpp:865` | the live instance's `roi()` (the search box) | unchanged |
| BR14 | W | `app_settings.h:222, 702, 1306–1312, 1763, 1891` | the property's own lines | removed |

### 1.5 `cameraAlias`

- **Key:** `camera/alias`, a map from camera key to label.
- **Absent means** no label. Absent is short-lived: at the next launch the constructor seeds the key with `|` replaced by a space (§2.3).
- **Moves to:** the Rig, not versioned; command `setAlias`.

| ID | Kind | Where | What it does | Goes to |
|---|---|---|---|---|
| AL1 | W | `CamerasPanel.qml:356` | the alias field's `onEditingFinished` calls `cameraManager.setCameraAlias(key, text)` | Rig command `setAlias` |
| AL2 | R | `resource_monitor_controller.cpp:91` | a fresh `AppSettings` each refresh, for the monitor's device names | NO HOME F28 |
| AL3 | W | `camera_manager.h:126` | the `Q_INVOKABLE` that QML calls | Rig command `setAlias` (forwarded) |
| AL4 | R | `camera_instance.cpp:125` | constructor: the instance's `deviceAlias` | NO HOME F27 (the unversioned part is outside the rig state a device is handed, §4.1) |
| AL5 | R | `camera_manager.cpp:75` | constructor: reads the map to seed it | Rig command `setAlias` |
| AL6 | W | `camera_manager.cpp:91` | constructor: saves the seeded map, once per launch with a new camera | Rig command `setAlias` |
| AL7 | R | `camera_manager.cpp:154` | `cameraList()`'s `alias` field (165) | NO HOME F27 (the unversioned part is outside the rig state a device is handed, §4.1); its update too |
| AL8 | W | `camera_manager.cpp:1034` | `setCameraAlias`: trims; an empty text removes the entry; no-op when unchanged | Rig command `setAlias` |
| AL9 | R | `camera_manager.cpp:1039` | `setCameraAlias` reads the map | Rig command `setAlias` |
| AL10 | W | `camera_manager.cpp:1051` | `setCameraAlias` saves | Rig command `setAlias` |
| AL11 | S | `camera_manager.cpp:1053–1059` | pushes the alias to the live controller (`setDeviceAlias`), then `cameraListChanged` | NO HOME F27 |
| AL12 | R | `shot_processor.cpp:841` | each clip's file name and the swing's `alias` come from the live instance's `deviceAlias()` (854–855) | unchanged (depends on F27) |
| AL13 | ND | `ppcp_host_service.cpp:2323` | comment | n/a |
| AL14 | ND | `ppcp_host_service.cpp:2352` | comment | n/a |
| AL15 | ND | `ppcp_host_service.cpp:2680` | comment | n/a |
| AL16 | ND | `ppcp_host_service.h:337` | comment | n/a |
| AL17 | ND | `PhonesPanel.qml:56` | comment | n/a |
| AL18 | W | `app_settings.h:226, 709, 1338–1344, 1764, 1892` | the property's own lines | removed |

### 1.6 `cameraExcluded` (enabled on this machine)

- **Key:** `camera/excluded`, a list of camera keys.
- **Absent means** enabled.
- A machine-wide switch: each session is seeded from it, and the session's own choice is never written back (`camera_manager.h:51–56`).
- **Moves to:** the Rig, not versioned; command `setEnabledOnThisMachine`.

| ID | Kind | Where | What it does | Goes to |
|---|---|---|---|---|
| EX1 | L | `HmRigCapability.qml:57` | Home's capability input binds the global list | app.rig model |
| EX2 | R | `capability_facts.js:78` | `facts(input, "home")`: a camera counts only if not globally excluded | unchanged (pure; handed the list by EX1) |
| EX3 | ND | `capability_facts.js:45` | comment | n/a |
| EX4 | R | `SetupContext.qml:616` | `seedSessionEnablement` (through `settings.`; from `SetupDraft.qml:140`, once per visit): the session list from the global list | app.rig model |
| EX5 | ND | `PpCameraPanel.qml:135` | comment | n/a |
| EX6 | ND | `camera_manager.h:52` | comment | n/a |
| EX7 | R | `camera_manager.cpp:66` | constructor: each entry's `excluded`; `m_sessionExcluded` seeded as a copy (72) | NO HOME F27 (the unversioned part is outside the rig state a device is handed, §4.1) |
| EX8 | R | `camera_manager.cpp:545` | `enumerate()`: a newly found camera's `excluded` | NO HOME F27 (the unversioned part is outside the rig state a device is handed, §4.1) |
| EX9 | W | `CamerasPanel.qml:397` | the Enable switch calls `cameraManager.setExcluded(index, !v)` | Rig command `setEnabledOnThisMachine` |
| EX10 | R | `camera_manager.cpp:590` | `setExcluded` reads the list | Rig command `setEnabledOnThisMachine` |
| EX11 | W | `camera_manager.cpp:595` | `setExcluded` saves (append, or `removeAll`) | Rig command `setEnabledOnThisMachine` |
| EX12 | S | `camera_manager.cpp:563–582, 598` | the in-memory flag; the change is mirrored into the session list; the camera is deselected (excluded) or selected (enabled); `cameraListChanged` | unchanged (`CameraManager`'s session state); how it is triggered: NO HOME F27 |
| EX13 | R | `camera_manager.cpp:214` | `cameraList()`'s `enabled` field, from the in-memory flag, read by `CamerasPanel.qml` 282–987 | NO HOME F27 (the unversioned part is outside the rig state a device is handed, §4.1) |
| EX14 | W | `app_settings.h:216, 696, 1234–1240, 1754, 1882` | the property's own lines | removed |

---

## 2. The invariants and side effects in today's writers

1. **Impact is exclusive** (`CameraManager::assignPerspective`, `camera_manager.cpp:732–795`). Assigning Impact to camera K:
   - erases every other entry whose code is Impact, so the camera that loses Impact becomes **absent, which is None**, not another role;
   - sets every other live controller holding Impact to `None`.

   Then, for any code:
   - None removes K's entry, and any other code stores it, unknown codes included (`map[key] = perspective`);
   - the map is saved **before** anything is rebuilt;
   - K's live controller is set;
   - every selected camera whose Impact-ness changed, K or the loser, is reconnected (`setSelected(i, false)`, then `true`), so the new instance re-reads its role's crop, rate, exposure and gain;
   - `cameraListChanged` is emitted.

   The VIEW combo writes the non-Rig keys first (exposure, rate, the `#impact` crop: `CamerasPanel.qml:536–560`), so the reconnect reads them. The map half is the Rig's invariant 1 (§4.1). The live None, the reconnect and that ordering are not placed: F26.
2. **The hitting-area box is applied only while its camera is fixed, and kept while it is not** (§4.1; table A2). In the code:
   - **Applied:** at `createController` only (`camera_manager.cpp:1001–1019`). Toggling fixed later changes nothing live (FX3).
   - **Kept:** no writer removes an entry because the camera is not fixed.
   - **Two exceptions, by reading.** The Ball step applies the saved box to a camera that is not fixed, on purpose (`SetupContext.qml:284–286`, BR1). The Settings ball panel used to replace a saved box with the default when the live box was empty, which for a camera not fixed in place is every connect (BR3, F31a); since F31-probe it shows the saved box (set on the live instance only, as the Ball step does) and keeps it; the default is seeded and saved only when none is saved.
   - **The flag is read twice.** The flag at the freeze is read from settings (FX12), while the live box reflects the flag at connect time.
3. **The alias seeded for a newly found camera** (`camera_manager.cpp:73–91`):
   - **When:** only in the constructor, for each enumerated camera with no entry; `enumerate()` (Refresh, a camera found later) seeds none.
   - **The value:** the key with `|` replaced by a space (a shape-aware site, C-1).
   - **One save:** the whole map, saved once if any entry was added.
   - **Clearing is not durable:** an empty alias removes the entry (AL8), so the next launch seeds the default again, and clearing an alias means "reset to the default at the next launch".

   C-5b What 3 makes the seed a `setAlias` call. That keeps it at construction, which needs the copy (C-5a.2) to have run first.
4. **The mirror chip writes twice** (MI3, MI4) when a camera is live, and once when none is.
5. **The fixed switch stores an explicit `false`** (FX3), while the mirror writers and the alias remove the entry. A copy or a fingerprint (C-5a.2) must read absent and `false` the same way.
6. **The excluded list:**
   - a change also edits the session list and the selection (EX12);
   - `setExcluded` appends without a `contains` check, guarded only by the in-memory flag (EDGE);
   - the session list is seeded twice: at construction (EX7) and per set-up visit (EX4).
7. **One `AppSettings` per caller.** Each `AppSettings` caches its own maps. `CameraManager` uses the app's instance, or a local `fallback` when it has none (tests, tools). `ResourceMonitorController` builds a fresh one each refresh (PE4, AL2), so it sees the store, not the app's instance.

## 3. `AppSettings::migrateImpactRoi` and `camera/impactRoiMigrated`

- **When it runs:** in every `AppSettings` constructor, right after the camera keys load (`app_settings.h:527`). `AppSettings` is constructed in about 28 production places, so it runs many times per process.
- **The marker:** it reads `camera/impactRoiMigrated` (`app_settings.cpp:34`) and returns at once when the marker is set.
- **What it does otherwise:**
  - it walks `m_cameraPerspective`, the property's own member, with `kImpact = 4` (36);
  - for each camera whose code is Impact, that has a plain `camera/roi` entry and no `#impact` entry, it moves the plain entry to `<key>#impact` (43) and logs `ppInfo`;
  - it saves `camera/roi` if anything moved (48), and always writes the marker (49).
- **So it runs at most once per settings store,** and on a store with no keys it writes only the marker.
- **Its readers:** of the six properties, only `cameraPerspective`. The design keeps it working with a raw read of `camera/perspective` and the one enum (§4.7), and retires it with the marker in Stage E.
- **The raw key is never written after C-5b,** so the raw read sees the map as the last build before C-5b left it. That is what an old-format crop needs.
- **Covered by** `camera_roi_test` `testMigration`, M1–M6 (§6), which seeds the raw keys and so is unaffected.

## 4. `camera/roi`, which shares the perspective map but is not the Rig's

- **What it is:** `camera/roi` (`cameraRoi`; Stage E, SET-09) holds a camera's crop per **role**. The non-Impact crop is under the camera key, and the impact strip under `<key>#impact` (`camera_roi.h`).
- **The perspective decides two keys:**
  - which entry is the crop, through `pp_camroi::key` and `cropFor`;
  - which exposure and gain key is read, through `tuningKey`: the camera key for Impact, `<key>#wide` for any other role.
- **The C++ readers that combine the two:**
  - `CameraInstance`'s constructor (PE5–PE7);
  - `cameraList()` (PE13);
  - `createPreviewInstance` (PE18) and `createController` (PE19);
  - the resource monitor (PE4);
  - the role probe (`expectedSize`, `seedImpactLikeTheViewCombo`).
- **QML spells the key inline** from `camData.perspective`: `CamerasPanel.qml:1499` and `PpCameraTiles.qml:450`, so that a binding follows `appSettings.cameraRoi`.
- **After C-5b:** the perspective comes from the rig state (typed input, or `cameraList`'s field), and the crop map stays in `AppSettings`. Every reader combines one of each. `migrateImpactRoi` is the one writer of `camera/roi` that reads the perspective (§3).

## 5. Where each writer goes (summary)

| Destination | Writers and clears |
|---|---|
| `assignPerspective` | PE3, PE15 (PE14 reads for it); the map half of PE16 |
| `setMirrored` | MI3, MI4, MI6 |
| `setFixedInPlace` | FX3 |
| `setHittingArea` / `clearHittingArea` | BR2, BR3, BR4, BR5, BR8 / BR9, BR11 |
| `setAlias` | AL1, AL3, AL6, AL8, AL10 |
| `setEnabledOnThisMachine` | EX9, EX11 |
| NO HOME | writers: PE11, PE12 (F29), PE21 (F30). Other duties: the live halves PE16, PE17, MI4 (F26); AL4, AL7, AL11, EX7, EX8, EX12's trigger, EX13 (F27) |
| removed | PE24, MI9, FX15, BR14, AL18, EX14 |

Every W and C row has an entry in its last column, and the script checks it. Three writers have **NO HOME** as that entry: PE11, PE12 and PE21. MI4 has a command, and only its live apply is NO HOME.

## 6. The tests that cover a property, and what each is re-pointed at

| ID | Kind | Test (ctest name) | Where | What it covers | Re-pointed at |
|---|---|---|---|---|---|
| TS1 | T | `tst_setup_flow` (`session_setup_ui`, `qml_ui`) | `tst_setup_flow.qml:57` | `init()` resets `cameraExcluded` on the real `AppSettings` (scratch store) | re-pointed: `app.rig`'s enabled list on a scratch rig (F32) |
| TS2 | T | `tst_setup_flow` | `tst_setup_flow.qml:59` | `init()` resets `cameraFixedInPlace` | re-pointed: `app.rig` |
| TS3 | T | `tst_setup_flow` | `tst_setup_flow.qml:60` | `init()` resets `cameraBallRoi` | re-pointed: `app.rig` |
| TS4 | T | `tst_setup_flow` `test_CTXF_cameras`, tag "FO" | `tst_setup_flow.qml:789` | the Ball step uses a saved box on a camera that is not fixed; the default when none is saved; a set box is left alone (BR1, BR2) | re-pointed: `app.rig`'s box |
| TS5 | T | `tst_setup_flow` `test_CTXF_cameras`, tag "FO+DTL" | `tst_setup_flow.qml:809` | `anyFixedCamera` follows the map (FX6) | re-pointed: `app.rig`'s fixed flag |
| TS6 | T | `tst_setup_ready` `test_R01_readinessIssuesGolden`, `test_R03_summaryRowsGolden` (combo "fo+dtl-fixed") | `tst_setup_ready.qml:374` | "Optional — cameras fixed in place" (FX6) | re-pointed: `app.rig` |
| TS7 | T | `SetupDriver.setUp()`, for `tst_setup_check`, `_devices`, `_framing`, `_indicator`, `_mounts`, `_nav`, `_ready`, `_lifecycle`, `_sensors`, `_trunk` | `SetupDriver.qml:134` | resets `cameraExcluded` | re-pointed: `app.rig` |
| TS8 | T | the same | `SetupDriver.qml:136` | resets `cameraFixedInPlace` | re-pointed: `app.rig` |
| TS9 | ND | `session_setup_ui` | `session_setup_ui_test.cpp:479` | the comment on one scratch store per file: the guard that keeps the tests off the real settings | n/a; C-4's rig override joins this guard |
| TS10 | T | `FakeCameraManager.qml:57` (used by `tst_setup_flow`, `_smoke`, `_rolesviz`, `CalibDriver`, and `SetupDriver.qml:89`, so by TS7's ten files) | | a fake `setBallRoi` that sets the instance's box and saves nothing | re-pointed with the manager (BRIEF-C-5b What 7) |
| TS11 | T | `session_setup_lint_test` (the context-object table, `session_setup_lint_test.cpp:701, 717`) | | every `cameraManager.`/`cameraMgr.` member QML calls (`setBallRoi`, `setSessionCameraEnabled`, …) is a public `Q_INVOKABLE` | follows any command that moves to `app.rig` (a new row) |
| TS12 | T | `camera_roi_test` `testMigration`, M1–M6 | `camera_roi_test.cpp:142–175` | `migrateImpactRoi` (§3), from raw keys | unchanged |
| TS13 | T | `recording_contract_test` Test 9, `testExportTakesFrozenInputs`; `swing_window_parity` Test 8 | `recording_contract_test.cpp:396–429`; `swing_window_parity_test.cpp:888–911` | the frozen perspective, mirrored and fixed reach the export. They start from `CaptureCameraFacts`, after FX12's read | unchanged; FX12's read itself has no test |
| TS14 | T | `qml_ui.tst_ppcp_pair_dialog`, TestCase `SettingsCameras` (always on, the studio PC too) | `tst_ppcp_pair_dialog.qml:521–836` | the REAL Settings ▸ Cameras panel over an inline fake `cameraManager` whose writers copy camera_manager.cpp's, with the real AppSettings: the ball panel shows and keeps the saved box, and a click inside it re-saves the same box (BR3, BR3a); a click outside with no drag or a 2 px jitter keeps the box, and a drag draws one (BR4); the Mirrored chip hands the manager the clicked value (MI3, MI4); each case sets and resets `cameraBallRoi`, `cameraIsMirrored`, `cameraFixedInPlace` (F31-probe) | re-pointed: `app.rig` |

**Added at C-5b's re-count (`fd2b8e25`):** DK1, `device_key_test` (`src/Gui/tests/device_key_test.cpp`, C-1a's key-spelling fixture: the six setters at 342–347 and their getters at 372–377, 392–399, 418, 615, 624), missed by the ledger; re-pointed to raw `ppSettings()` writes and reads, as the last build before the cut-over wrote them.

**No test covers** (F32):
- `assignPerspective`: the strip, the live None, the reconnect;
- `setIsMirrored`;
- the persisting halves of `setBallRoi` and `clearBallRoi`;
- `setExcluded`;
- `setCameraAlias`;
- the alias seed;
- `createController`'s restore and its fixed gate;
- `cameraList()`'s `perspective`, `alias` and `enabled` fields;
- `seedSessionEnablement`'s read of the global list (every test resets it to `[]`);
- `camera_health.js`;
- `capability_facts.js`' home branch;
- the resource monitor;
- `snapshotAtFreeze`'s read of the fixed flag (FX12, FX13).

The role probe (`PINPOINT_PROBE_CAMERA_ROLES`, real cameras) is the only check of `assignPerspective`, and it is not a ctest.

## 7. Raw reads of the `camera/` keys (the brief's check 3)

| ID | Where | What | Goes to |
|---|---|---|---|
| RK1 | `app_settings.h:513, 517, 518, 519, 522, 523` | the six properties' loads | removed (C-5b What 6); the keys stay in the store |
| RK2 | `app_settings.h:1238, 1294, 1302, 1310, 1334, 1342` | the six setters' writes | removed |
| RK3 | `app_settings.h:514, 515, 516, 520, 521, 524, 525, 526` | loads of `targetFps`, `triggerMode`, `roi`, `preroll`, `syncEnabled`, `exposureUs`, `tuning`, `impactPip`: not the Rig's (§4.3) | unchanged (Stage E) |
| RK4 | `app_settings.h:1246, 1254, 1262, 1270, 1278, 1286, 1318, 1326` | the same keys' setters | unchanged (Stage E) |
| RK5 | `app_settings.cpp:34, 48, 49` | `migrateImpactRoi` (§3) | unchanged; its perspective input becomes a raw read (§4.7) |
| RK6 | `camera_roi_test.cpp:142, 144, 161, 163` | the migration test's raw seeds and checks (TS12) | unchanged |
| RK7 | `frame_crop.h:29` | comment | n/a |

No code outside `AppSettings` and the test reads one of the six keys raw.

## 8. NO HOME, by finding (the findings are in `NOTES-C-5i.md`)

- **F26:** PE16's live half, PE17, MI4's live apply.
- **F27:** AL4, AL7, AL11, EX7, EX8, EX12's trigger, EX13.
- **F28:** PE4, AL2, FX11, FX12, FX13.
- **F29:** PE8–PE12.
- **F30:** PE21.

F31 is three paths that might lose a saved setting. F32 covers the test gaps and the fixtures' keys, and F33 the accept grep's blind spots.

## 9. The ledger

Each grep hit, by file and its text with the whitespace collapsed, and the row that holds it. The script counts multiplicity: `CamerasPanel.qml` 516/525 and 696/709, and `camera_manager.cpp` 857/882 and 869/889, are the same text twice.

<!-- LEDGER-BEGIN -->
```
src/Gui/settings/CamerasPanel.qml :: PE1 :: var p = appSettings.cameraPerspective[camRow.camData.cameraKey] || 0
src/Gui/settings/CamerasPanel.qml :: PE2 :: var p = appSettings.cameraPerspective[camRow.camData.cameraKey] || 0
src/Gui/monitor/resource_monitor_controller.cpp :: PE4 :: const QString roiKey = pp_camroi::key(camKey, appSettings.cameraPerspective().value(camKey).toInt());
src/Gui/cameras/camera_instance.cpp :: PE5 :: const int role = appSettings->cameraPerspective().value(key).toInt();
src/Gui/cameras/camera_instance.cpp :: PE6 :: if (buffer && appSettings->cameraPerspective().value(key).toInt() == Impact) {
src/Gui/cameras/camera_instance.cpp :: PE7 :: const QString tk = pp_camroi::tuningKey(key, appSettings->cameraPerspective().value(key).toInt());
src/Gui/cameras/camera_role_probe.cpp :: PE8 :: m_perspective = m_settings->cameraPerspective();
src/Gui/cameras/camera_role_probe.cpp :: PE9 :: check(m_settings->cameraPerspective().value(t.key).toInt() == t.originalRole,
src/Gui/cameras/camera_role_probe.cpp :: PE10 :: if (m_settings && !m_perspective.isEmpty() && m_settings->cameraPerspective() != m_perspective)
src/Gui/cameras/camera_role_probe.cpp :: PE11 :: m_settings->setCameraPerspective(m_perspective);
src/Gui/cameras/camera_manager.cpp :: PE13 :: const QVariantMap perspMap = s->cameraPerspective();
src/Gui/cameras/camera_manager.cpp :: PE14 :: QVariantMap map = s->cameraPerspective();
src/Gui/cameras/camera_manager.cpp :: PE15 :: s->setCameraPerspective(map);
src/Gui/cameras/camera_manager.cpp :: PE18 :: const QRectF roi = pp_camroi::cropFor(rs->cameraRoi(), key, rs->cameraPerspective().value(key).toInt(),
src/Gui/cameras/camera_manager.cpp :: PE19 :: const QRectF roi = pp_camroi::cropFor(cs->cameraRoi(), key, cs->cameraPerspective().value(key).toInt(),
src/Gui/cameras/camera_manager.cpp :: PE20 :: const QVariantMap perspMap = cs->cameraPerspective();
src/Gui/settings/CamerasPanel.qml :: MI1 :: readonly property bool isMirrored: (appSettings.cameraIsMirrored[camRow.camData.cameraKey] === true)
src/Gui/settings/CamerasPanel.qml :: MI2 :: var map = appSettings.cameraIsMirrored
src/Gui/settings/CamerasPanel.qml :: MI3 :: appSettings.cameraIsMirrored = map
src/Gui/cameras/camera_manager.cpp :: MI5 :: QVariantMap map = ps->cameraIsMirrored();
src/Gui/cameras/camera_manager.cpp :: MI6 :: ps->setCameraIsMirrored(map);
src/Gui/cameras/camera_manager.cpp :: MI7 :: const QVariantMap mirrorMap = cs->cameraIsMirrored();
src/Gui/settings/CamerasPanel.qml :: FX1 :: var v = appSettings.cameraFixedInPlace[camRow.camData.cameraKey]
src/Gui/settings/CamerasPanel.qml :: FX2 :: var map = appSettings.cameraFixedInPlace
src/Gui/settings/CamerasPanel.qml :: FX3 :: appSettings.cameraFixedInPlace = map
src/Gui/settings/CamerasPanel.qml :: FX4 :: var v = appSettings.cameraFixedInPlace[camRow.camData.cameraKey]
src/Gui/settings/CamerasPanel.qml :: FX5 :: appSettings.cameraFixedInPlace[camRow.camData.cameraKey] === true
src/Gui/setup/SetupContext.qml :: FX6 :: var fixed = settings.cameraFixedInPlace
src/Gui/tests/setup/session_setup_ui_test.cpp :: TS9 :: // all handed whatever a file wrote — a theme, imuRoles, cameraFixedInPlace, imuExcluded —
src/Gui/tests/setup/tst_setup_ready.qml :: TS6 :: if (c.cams === "fo+dtl-fixed") appSettings.cameraFixedInPlace = ({ "FO1": true, "DTL1": true })
src/Gui/tests/setup/tst_setup_flow.qml :: TS2 :: appSettings.cameraFixedInPlace = ({})
src/Gui/tests/setup/tst_setup_flow.qml :: TS5 :: appSettings.cameraFixedInPlace = ({ DTL: true })
src/Gui/tests/setup/support/SetupDriver.qml :: TS8 :: appSettings.cameraFixedInPlace = ({})
src/Gui/cameras/PpCameraPanel.qml :: FX9 :: // not "fixed in place" (appSettings.cameraFixedInPlace) — i.e.
src/Gui/cameras/camera_manager.cpp :: FX10 :: if (cs->cameraFixedInPlace().value(key).toBool()) {
src/Gui/shot/shot_processor.cpp :: FX11 :: cam.fixedInPlace = s->cameraFixedInPlace().value(track.ctrl->cameraKey()).toBool();
src/Gui/shot/capture_context.cpp :: FX12 :: const QVariantMap fixedInPlace = settings.cameraFixedInPlace();
src/Gui/shot/shot_processor.h :: FX14 :: // face-on camera is cameraFixedInPlace (else left empty/cold — nothing to
src/Gui/setup/SetupContext.qml :: BR1 :: var r = settings.cameraBallRoi[inst.cameraKey]
src/Gui/settings/CamerasPanel.qml :: BR3a :: var r = appSettings.cameraBallRoi[camRow.camData.cameraKey]
src/Gui/tests/setup/tst_setup_flow.qml :: TS3 :: appSettings.cameraBallRoi = ({})
src/Gui/tests/setup/tst_setup_flow.qml :: TS4 :: appSettings.cameraBallRoi = ({ FO: { x: 0.31, y: 0.62, w: 0.12, h: 0.18 } })
src/Gui/cameras/camera_manager.cpp :: BR7 :: QVariantMap map = ps->cameraBallRoi();
src/Gui/cameras/camera_manager.cpp :: BR8 :: ps->setCameraBallRoi(map);
src/Gui/cameras/camera_manager.cpp :: BR10 :: QVariantMap map = ps->cameraBallRoi();
src/Gui/cameras/camera_manager.cpp :: BR11 :: ps->setCameraBallRoi(map);
src/Gui/cameras/camera_manager.cpp :: BR12 :: const QVariantMap ballRoiMap = cs->cameraBallRoi();
src/Ppcp/ppcp_host_service.cpp :: AL13 :: // the ordinary settings file beside `cameraAlias` and `imuAlias`, which is what
src/Ppcp/ppcp_host_service.cpp :: AL14 :: // A flat pairingId -> alias map, deliberately shaped like `cameraAlias` and
src/Ppcp/ppcp_host_service.cpp :: AL15 :: // priority `cameraAlias`/`imuAlias` have over a device's own reported
src/Ppcp/ppcp_host_service.h :: AL16 :: // 3.3's `declare` (untrusted, 4.4d) — the same relationship `cameraAlias`
src/Gui/settings/CamerasPanel.qml :: AL1 :: onEditingFinished: cameraManager.setCameraAlias(camRow.camData.cameraKey, text)
src/Gui/settings/PhonesPanel.qml :: AL17 :: // — and sits beside `cameraAlias`/`imuAlias` in the settings file rather than
src/Gui/monitor/resource_monitor_controller.cpp :: AL2 :: const QVariantMap camAliasMap = appSettings.cameraAlias();
src/Gui/cameras/camera_manager.h :: AL3 :: Q_INVOKABLE void setCameraAlias(const QString &key, const QString &alias);
src/Gui/cameras/camera_instance.cpp :: AL4 :: m_deviceAlias = appSettings->cameraAlias().value(key).toString();
src/Gui/cameras/camera_manager.cpp :: AL5 :: QVariantMap aliasMap = s->cameraAlias();
src/Gui/cameras/camera_manager.cpp :: AL6 :: s->setCameraAlias(aliasMap);
src/Gui/cameras/camera_manager.cpp :: AL7 :: const QVariantMap aliasMap = s->cameraAlias();
src/Gui/cameras/camera_manager.cpp :: AL8 :: void CameraManager::setCameraAlias(const QString &key, const QString &alias)
src/Gui/cameras/camera_manager.cpp :: AL9 :: QVariantMap map = s->cameraAlias();
src/Gui/cameras/camera_manager.cpp :: AL10 :: s->setCameraAlias(map);
src/Gui/home/HmRigCapability.qml :: EX1 :: cameraExcluded: appSettings.cameraExcluded, imuExcluded: appSettings.imuExcluded
src/Gui/setup/SetupContext.qml :: EX4 :: settings.cameraExcluded.indexOf(camList[i].cameraKey) < 0)
src/Gui/tests/setup/tst_setup_flow.qml :: TS1 :: appSettings.cameraExcluded = []
src/Gui/tests/setup/support/SetupDriver.qml :: TS7 :: appSettings.cameraExcluded = []
src/Gui/cameras/PpCameraPanel.qml :: EX5 :: // Seeded from appSettings.cameraExcluded at startup, re-seeded by the
src/Gui/cameras/camera_manager.h :: EX6 :: // AppSettings::cameraExcluded at startup but NEVER written back — global
src/Gui/cameras/camera_manager.cpp :: EX7 :: const QStringList excluded = s->cameraExcluded();
src/Gui/cameras/camera_manager.cpp :: EX8 :: if (es->cameraExcluded().contains(key)) entry.excluded = true;
src/Gui/cameras/camera_manager.cpp :: EX10 :: QStringList excList = xs->cameraExcluded();
src/Gui/cameras/camera_manager.cpp :: EX11 :: xs->setCameraExcluded(excList);
src/Gui/cameras/camera_health.js :: FX8 :: // records the reads (the camera list and appSettings.cameraFixedInPlace) and
src/Gui/cameras/camera_health.js :: FX7 :: var fixed = settings.cameraFixedInPlace
src/Gui/setup/capability_facts.js :: EX3 :: // cameraExcluded, imuExcluded home: the global exclusion lists (appSettings), by key / id
src/Gui/setup/capability_facts.js :: EX2 :: var camOn = function(c) { return !_excluded(input.cameraExcluded, c.cameraKey) }
src/Video/frame_crop.h :: RK7 :: // AppSettings ("camera/roi"). Backends with hardware ROI support (Aravis,
src/Gui/app/app_settings.h :: RK1 :: m_cameraExcluded = ppSettings().value(QStringLiteral("camera/excluded"), QStringList{}).toStringList();
src/Gui/app/app_settings.h :: RK3 :: m_cameraTargetFps = ppSettings().value(QStringLiteral("camera/targetFps"), QVariantMap{}).toMap();
src/Gui/app/app_settings.h :: RK3 :: m_cameraTriggerMode = ppSettings().value(QStringLiteral("camera/triggerMode"), QVariantMap{}).toMap();
src/Gui/app/app_settings.h :: RK3 :: m_cameraRoi = ppSettings().value(QStringLiteral("camera/roi"), QVariantMap{}).toMap();
src/Gui/app/app_settings.h :: RK1 :: m_cameraPerspective = ppSettings().value(QStringLiteral("camera/perspective"), QVariantMap{}).toMap();
src/Gui/app/app_settings.h :: RK1 :: m_cameraIsMirrored = ppSettings().value(QStringLiteral("camera/isMirrored"), QVariantMap{}).toMap();
src/Gui/app/app_settings.h :: RK1 :: m_cameraBallRoi = ppSettings().value(QStringLiteral("camera/ballRoi"), QVariantMap{}).toMap();
src/Gui/app/app_settings.h :: RK3 :: m_cameraPreroll = ppSettings().value(QStringLiteral("camera/preroll"), 1.0).toDouble();
src/Gui/app/app_settings.h :: RK3 :: m_cameraSyncEnabled = ppSettings().value(QStringLiteral("camera/syncEnabled"), true).toBool();
src/Gui/app/app_settings.h :: RK1 :: m_cameraFixedInPlace = ppSettings().value(QStringLiteral("camera/fixedInPlace"), QVariantMap{}).toMap();
src/Gui/app/app_settings.h :: RK1 :: m_cameraAlias = ppSettings().value(QStringLiteral("camera/alias"), QVariantMap{}).toMap();
src/Gui/app/app_settings.h :: RK3 :: m_cameraExposureUs = ppSettings().value(QStringLiteral("camera/exposureUs"), QVariantMap{}).toMap();
src/Gui/app/app_settings.h :: RK3 :: m_cameraTuning = ppSettings().value(QStringLiteral("camera/tuning"), QVariantMap{}).toMap();
src/Gui/app/app_settings.h :: RK3 :: m_impactPipRect = ppSettings().value(QStringLiteral("camera/impactPip"), QVariantMap{}).toMap();
src/Gui/app/app_settings.h :: RK2 :: ppSettings().setValue(QStringLiteral("camera/excluded"), v);
src/Gui/app/app_settings.h :: RK4 :: ppSettings().setValue(QStringLiteral("camera/targetFps"), v);
src/Gui/app/app_settings.h :: RK4 :: ppSettings().setValue(QStringLiteral("camera/triggerMode"), v);
src/Gui/app/app_settings.h :: RK4 :: ppSettings().setValue(QStringLiteral("camera/roi"), v);
src/Gui/app/app_settings.h :: RK4 :: ppSettings().setValue(QStringLiteral("camera/exposureUs"), v);
src/Gui/app/app_settings.h :: RK4 :: ppSettings().setValue(QStringLiteral("camera/tuning"), v);
src/Gui/app/app_settings.h :: RK4 :: ppSettings().setValue(QStringLiteral("camera/impactPip"), v);
src/Gui/app/app_settings.h :: RK2 :: ppSettings().setValue(QStringLiteral("camera/perspective"), v);
src/Gui/app/app_settings.h :: RK2 :: ppSettings().setValue(QStringLiteral("camera/isMirrored"), v);
src/Gui/app/app_settings.h :: RK2 :: ppSettings().setValue(QStringLiteral("camera/ballRoi"), v);
src/Gui/app/app_settings.h :: RK4 :: ppSettings().setValue(QStringLiteral("camera/preroll"), v);
src/Gui/app/app_settings.h :: RK4 :: ppSettings().setValue(QStringLiteral("camera/syncEnabled"), v);
src/Gui/app/app_settings.h :: RK2 :: ppSettings().setValue(QStringLiteral("camera/fixedInPlace"), v);
src/Gui/app/app_settings.h :: RK2 :: ppSettings().setValue(QStringLiteral("camera/alias"), v);
src/Gui/app/app_settings.cpp :: RK5 :: if (s.value(QStringLiteral("camera/impactRoiMigrated"), false).toBool())
src/Gui/app/app_settings.cpp :: RK5 :: s.setValue(QStringLiteral("camera/roi"), m_cameraRoi);
src/Gui/app/app_settings.cpp :: RK5 :: s.setValue(QStringLiteral("camera/impactRoiMigrated"), true);
src/Gui/tests/camera_roi_test.cpp :: RK6 :: ppSettings().setValue(QStringLiteral("camera/perspective"),
src/Gui/tests/camera_roi_test.cpp :: RK6 :: ppSettings().setValue(QStringLiteral("camera/roi"),
src/Gui/tests/camera_roi_test.cpp :: RK6 :: ppSettings().value(QStringLiteral("camera/roi")).toMap() == r);
src/Gui/tests/camera_roi_test.cpp :: RK6 :: ppSettings().value(QStringLiteral("camera/impactRoiMigrated")).toBool());
```
<!-- LEDGER-END -->
