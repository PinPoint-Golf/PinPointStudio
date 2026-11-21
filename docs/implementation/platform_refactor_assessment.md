# Platform refactor: assessment

**Date:** 2026-11-21.
**Status:** assessment for the platform refactor, after step 8 of the current refactor; reviewed
(`build/refactor/platform-review/REVIEW.md`, 21 Nov 2026) and revised.
**What it is:** input to the platform refactor's briefs. It is not a plan. The worklist of record is
`docs/implementation/swe-audit/05-deferred-worklist.md` (05); appendix A places every 05 row.
**Checked against:** HEAD `8cd42e44` (main, 21 Nov 2026); `docs/developer/information_model.md` (IM) v1.9.10, and v1.9.12 for DEC-193;
`docs/implementation/swe-audit/02-findings.md` (02), `04-remediation-plan.md` (04) and 05;
`docs/implementation/code_quality_review.md` (the June review); `src/Gui/tests/architecture_allowlist.txt` (the
allowlist); the open GitHub issues.

**How things are measured here.** Line counts are production code only: `.cpp`, `.h`, `.mm` and `.qml` files, with
every `tests/` directory left out, counted with `find <dir> \( -name '*.cpp' -o -name '*.h' -o -name '*.mm' -o -name
'*.qml' \) -not -path '*/tests/*' | xargs cat | wc -l`. Single files are `wc -l`. Allowlist entries are cited by path
and rule, not by line number, because line numbers go stale. Every count is at `8cd42e44` and is to be re-measured when
its stage is planned.

**Words used.** IMC: an information-model change, as IM §1.2 defines it (C1–C11). PPCP: the PinPoint Capture
Protocol, the phone link (`src/Ppcp`). Execution provider: the ONNX Runtime back-end a model runs on (CPU, CUDA,
DirectML, CoreML). A ring is a dependency ring of IM §4 (Platform, Vocabulary, Rules, Domain model, Application,
Adapters); this page uses the names, not numbers. "The lint" is `architecture_lint_test` and its rules A1–A8 (IM §1.4).
"The cabin PC" is GOLFSIMPC, the Windows studio machine.

---

## Approvals this page needs

Two hard rules apply to everything below (`CLAUDE.md`):

- **Any information-model change needs Mark's approval before it is built** (IM §1.2, C1–C11).
- **Any design-system change needs Mark's approval before it is built**: a new component, a new variant, a changed
  look or behaviour, a new or changed Theme token.

Each is marked "(proposed, for Mark)" where it appears, and each stage lists its own. Every stage brief carries both
rules and the IM §10 ownership section, as 04 §1 does for the current refactor. The ones this page proposes:

| Proposal | Kind | Stage |
|---|---|---|
| A new `src/Platform/` directory, or `src/Core/` made Platform-only, and the file moves out of `Core` | IMC C9 (the precedent is DEC-148: a new `src/Identity` was C9) | B |
| Correct IM's Studio/Rig entry: `StudioProfile` is not a rig object | IMC C1, C2, C3: approved and applied (Mark, 21 Nov 2026; DEC-193) | done (Premise 2) |
| The Rig's design: identity, store, writer, `RigChanged`, a rig reference in each Recording | IMC C1, C3, C4, C5, C7, C8 | C |
| SET-05 and SET-09's rig keys moving into the Rig | IMC C3 | C |
| A device key type | IMC C5 | C |
| The capture service's ring, and where its device half goes | IMC C9 | D |
| When in capture the shot is allocated (DEC-142 says `Session::addShot` allocates it) | IMC C8, if the timing changes | D |
| A persisted shot-source type in the Vocabulary ring | IMC C3/C9 | D |
| The composition root's ring (`AppComposition`) | IMC C9 (lint A1) | E |
| The settings split, where it moves keys that hold domain facts (SET-*) | IMC C3, C4 | E |
| Amending R-CON-5 to allow a helper for long-lived hardware and media threads | IMC C11 | E |
| Narrowing lint A6 back to P9's wording | IMC (a change to a lint rule's scope: DEC-120) | E, any time |
| PPCP's files: retiring FILE-LIB-05, a PPCP repository, FILE-SHOT-11's owner | IMC C1, C3, C4 | F |
| The pairing key (`ppcp/pairings`, SET-09): any change of store | IMC C3; a question for Mark only, citing E56/E57 | none proposed |
| #105: a "not ready" state on a sensor's device card (`PpDeviceCard`) | design system | A |
| Notification coalescing (#79) in `PpNotificationHost`, `PpNotificationCard`, `PpToast` | design system | F |
| #53's coverage rule in `recording_integrity` / `capture_integrity_check` (Rules ring) | a rule change; an IMC if the integrity verdict is a recorded product (IM §7.2): for Mark | A |
| #183: recording gain, compensation, black level and gamma per stream per swing | IMC C3 (a field family in the swing file) | A |

---

## 1. Summary

Steps 1–8 of 04 fix the domain core: the information model, the aggregates, typed identities and the Recording
contract. They leave the platform layer as it is, by design (05 "Purpose").

What this assessment adds to 05:

1. **Premise 2 was the main finding, and it is now corrected.** IM filed the studio profile (FILE-APP-06) under
   Studio/Rig and mapped `StudioProfile` to OBJ-RIG, though it holds report and coach details only
   (`src/Core/studio_profile.h:21–34`). Mark approved the correction on 21 Nov 2026, and IM v1.9.12 applies it as
   DEC-193: OBJ-STUDIO-PROFILE, SET-20, and FILE-APP-06 re-classed.
2. **Build structure is under-weighted in 05** (one row, B-14). Ring-aligned CMake libraries are worth more than that
   row says, but only after step 7 and after the eight directories that span rings are split; and they enforce lint
   A1 only (Premise 3).
3. **June's concurrency, secrets and execution-provider items were not re-checked in 02.** This page re-checks them at
   HEAD (§4). Most are not shown to be bugs; they are listed as "suspected; check first", and become issues only
   where a failure is observed.
4. **Every 05 row is placed** (appendix A), and the stale 05 rows are named for whoever next updates 05
   (appendix B).

---

## 2. Five premises, re-weighed

### Premise 1: agree with 05's grouping; run capture as its own plan

05 already splits the work into "platform refactor (capture)", "platform refactor", "(settings)" and "(build)"
(05 "Conventions", §1–§3), and its first step is "Pick one group … Don't mix groups in one plan" (05 "How to use it").
This page agrees, and proposes that capture (Stages C, D and F) runs as its own plan, with its hardware testing on the
cabin PC, apart from the build and settings work (Stages B and E) (proposed, for Mark).

The size of each group, measured as above:

| Group | Directories | Production lines | Rings (IM §4.1) |
|---|---|---:|---|
| `src/Core` | `src/Core` (41 files) | 8,894 | Platform, Vocabulary, Rules (`kf3_smoother`), Adapters (`device_enumerator`, `pp_settings`, `studio_profile`, `app_info`) |
| Build | `CMakeLists.txt` | 3,900 (the root file alone) | none |
| Capture and devices | `Buffer` 4,045, `Video` 8,946, `IMU` 10,302, `LaunchMonitor` 6,337, `Gui/cameras` 9,257, `Gui/imu` 7,596, `Gui/calibration` 3,978, all of `Gui/shot` 11,800 | 62,261 | all six; IMU fusion and the LM's inferred reads are Rules, not capture |
| Phone link (PPCP) | `src/Ppcp` (47 files) | 24,871 | Adapters |
| App host, settings, tools, media | `Gui/app` 2,868, `Gui/main.cpp` 1,662, `Gui/monitor` 5,133, `Gui/settings` 11,155, `Update` 2,678, `Secrets` 229, `Audio` 1,950, `LLM` 767, `STT` 1,567, `TTS` 1,380 | 29,389 | Application, Adapters |

`src/Ppcp` also has 16,608 lines of tests in 23 `.cpp`/`.h` files: about 40% of its code is tests. That is a sign of
health. The production code of Buffer, Video, IMU and LaunchMonitor together is 29,630 lines, so PPCP is not larger
than those four combined.

### Premise 2: `StudioProfile` is not the Rig (right; corrected, DEC-193)

`StudioProfile` (`src/Core/studio_profile.h:21–34`) holds the studio's and coach's details for reports and emails:
name, logo, accent colour, address, phone, email, website, booking link, practice steps, email subject and body,
coach name, title, email, phone and photo. It holds nothing about cameras, sensors or calibration.

Yet IM classed FILE-APP-06 as "source (rig)", owned by Studio/Rig (§8.1), mapped `StudioProfile` to OBJ-RIG
(§11.2), and listed FILE-APP-06 among "where rig facts live today" (§2.3, §11.1). Because §2.3 says "No new place may
be added before the design (C3) (DEC-140)", the misclassification also constrained work. DEC-193 corrected it.

A Recording's reference to the rig (IM §2.3, "each Recording references the rig state it was captured with") must
never go stale because a coach changed a phone number or a logo.

**What DEC-193 decided** (Mark, 21 Nov 2026; IM v1.9.12):

- The studio profile is a source (the coach's entries), owned at the application level by `StudioProfile`
  (OBJ-STUDIO-PROFILE), held per machine: the Mac and the cabin PC each hold their own. It lives in SET-20 (`Studio/*`,
  new) and FILE-APP-06 (the copied logo and coach's photo). The report reads it.
- `ppcp/studioName` is the machine's own name as phones see it, and stays in SET-09 with the rig facts.
- OBJ-RIG is unchanged: which cameras and sensors, where they are, and their calibration, versioned over time (IM
  §2.3). FILE-APP-06 leaves its list of where rig facts live today, and SET-05 joins it.
- The two allowlist lines were re-worded (Mark, 21 Nov 2026): `studio_profile.cpp`'s A5 line is now tagged
  `# platform refactor (settings)`, `studio_profile.h`'s A6 line stays `# unplanned`, and 05 lists both in §2. So
  they are Stage E's.

Only the profile's directory is still open (§9, Q11), with Stage B's `Core` split.

### Premise 3: build structure deserves more than B-14, after step 7

05 has one build row, B-14 ("Diagnostics is not a library target"). The build today:

- `CMakeLists.txt` is 3,900 lines.
- Internal libraries: two, `pinpoint_buffer` (`src/Buffer/CMakeLists.txt:10`) and `pinpoint_swingstore`
  (`cmake/swingstore/CMakeLists.txt:16`). Everything else compiles into the `PinPointStudio` executable.
- 102 `.cpp`/`.mm` files are listed in `qt_add_executable` (`CMakeLists.txt:711–850`), and 20
  `target_sources(PinPointStudio …)` calls add more: 260 `.cpp`/`.mm` names in all, counting each platform-conditional
  mention once (a parse of the calls' arguments).
- 50 include directories are given to every file of the app (`CMakeLists.txt:851–862`: 49 source directories and the
  generated one).
- Test targets hand-list the sources they need, and engineers work around the missing libraries:
  `src/Core/atomic_write.h:59` says it is header-only "on purpose: there is no Core library and every test compiles
  the sources it needs".

**What libraries can enforce, and what they cannot.**

- They can enforce lint A1 (inward-only includes) through `PRIVATE` include directories and `target_link_libraries`,
  but only for a library whose directories each sit in one ring. Eight directories span rings and must be split
  first (IM §4.1, "Directories that span rings": `Export`, `Analysis`, `Core`, `LaunchMonitor`, `Buffer`, `IMU`,
  `Pose`, `Gui`). Step 7 moves rules down and renames namespaces (04 step 7), which changes this graph.
- They cannot enforce A2 to A8: QML creating a domain type, folder-name parsing, file names spelled outside their
  owner, file access outside a repository, the QML boundary, function length, and Analysis reaching past the
  Recording. Those stay with the lint.
- **411 quoted includes in 135 production files resolve only through the global include directories** (counted as an
  `#include "…"` whose path does not exist relative to the including file). Every one of them breaks when the include
  directories become `PRIVATE` to a library. That is the size of the job.

**Recommendation.** Promote ring libraries above B-14, as high-leverage work, timed for after step 7 and once the
spanning directories are split. Its estimate is made in its brief (Stage B).

### Premise 4: resolve 05's open decision on the lint scope

P9 forbids "a new `QVariantMap`/`QVariantList` in a presenter's or domain type's public API" (IM §3), and A6 is
written the same way (IM §1.4). In step G the lead widened A6 to every header (and, since the follow-up, slots and
signals), and that call is waiting for Mark (05 "Decisions waiting": "The lint-scope calls … A6 on every header, not
only presenters").

**Recommendation.** Ask Mark to narrow A6 back to P9's wording (proposed, for Mark; a lint-scope change is an IMC,
DEC-120). That one decision settles most of 05 §2's A6 lines, instead of re-tagging them `# kept` one at a time. The
lines it would take out of A6's scope are settings, monitoring and tool objects, which are not presenters or domain
types: `app_settings.h` ×26, `markup_controller.h` ×11, `profiler_controller.h` ×8, `resource_monitor_controller.h`
×4, `swing_zip_exporter.h` ×2, `app_info.h` ×1, `llm_controller.h` ×1, `transcription_controller.h` ×1,
`LlmWorker.h` ×1, `archive_controller.h` ×1, and `studio_profile.h` ×4 (in 05 §2 since DEC-193).

Whatever the ruling, the shapes that carry rig and capture facts are worth typing, because they will hold domain facts
once the Rig exists: the camera and IMU device objects, the trunk ceremony recorder and the buffer controller (Stages
C and D), the PPCP host service (Stage F), and the rig keys inside `AppSettings` (Stage C) (proposed, for Mark).
Appendix A places every line.

### Premise 5: PPCP is in 05; June's concurrency, secrets and execution-provider items are not

PPCP is in 05 §1: its first row ("connectors"), its A5 row (seven files) and its A6 row (`ppcp_host_service.h` ×6).
IM gives its files ids (FILE-LIB-03…05, FILE-SHOT-11), all homed in "platform refactor (capture)", and the pairing
store's file lines are capture's by Mark's decision of 15 Nov 2026 (05 §1, citing
`build/refactor/step-2/JUDGEMENT-ANSWERS.md`). It is not missed.

The real gap is that 02's "The June review, re-checked" table leaves out June's concurrency, secrets and
execution-provider items. §4 re-checks them. They should be checked on hardware where needed, then filed as issues
only where a failure is observed.

---

## 3. What is sound: keep it

- **The Platform ring's new pieces**: the worker pool and its receiver (`src/Core/work_pool.*`, step 3.1), `atomicWrite`
  (`src/Core/atomic_write.h`), `FolderLock`, `post_shot_trace`, `cpu_topology`. They are domain-free and tested.
- **The ring buffer in `src/Buffer`.** Leave its internals alone. Two narrow changes only: the `fetch_add` in §4, and
  the frozen window's handle (Stage D).
- **The updater's back-end split is done.** `src/Update` has one back-end per platform behind `UpdateBackend`
  (`linux_appimage_backend`, `mac_sparkle_backend`, `win_sparkle_backend`, `inert_update_backend`), so the June
  review's Theme J is stale. What is left is its A1 lines: it includes `AppSettings` and `SessionController` (Stage E).
- **The secure credential store in `src/Gui/report/mail_credentials_{mac.mm,win.cpp,linux.cpp}`**: macOS Keychain,
  Windows Credential Manager, a Linux fallback and an in-memory test mode. It is a candidate home for the API keys
  (Stage E), not for the pairing key (§4).
- **Real-time capture loops keep their own threads**: the `EventBuffer` merger, the Spinnaker and Aravis frame
  callbacks, BLE I/O and the PPCP accept loop. They have latency and blocking needs a shared pool cannot meet. Do not
  force them onto the pool (Stage E).
- **N cameras from day one in the Rig** (#180 adds a third permanent perspective; #74 and #178 add per-bay
  calibration). This is the right constraint for the Rig (Stage C).
- **The measured map of the capture seam**: `ShotProcessor`'s 17 A1 findings (13 in the `.cpp`, 4 in the `.h`),
  `ShotController`'s 3, and `ShotProcessor`'s five jobs (Stage D). They are a good starting map for the capture
  brief.
- **#187's cross-platform test-isolation helper.** The issue confirms the macOS leak; it is cheap (Stage B).

---

## 4. The June items and the exit crash, at HEAD

The project's rule is that a bug needs an observed failure. So each item is either "shown" (with the evidence) or
"suspected; check first".

| Item | June's claim | State at HEAD | What to do | Stage |
|---|---|---|---|---|
| `source_published_` (`src/Buffer/event_buffer.cpp:232`) | a non-atomic read-modify-write from several producer threads can stall the merger | The read-modify-write is real. The worst case is a missed wake, bounded by `merger_cold_timeout_us = 500` (`event_buffer_config.h:38`, used at `event_buffer.cpp:578`): at most 0.5 ms of latency, not a stall. Suspected, not observed | Make it `fetch_add`: cheap and plainly right | A (after the freeze) |
| `BleAdapterPool::m_index` (`src/IMU/ble_adapter_pool.cpp:56`) | a data race across per-IMU I/O threads | Not shown. Both callers run on the GUI thread before they hand off to the I/O thread (`src/Gui/imu/imu_instance.cpp:496`, `hm_instance.cpp:867`), and both are Linux-only | Check first; make it atomic only if a second thread is found | A (after the freeze) |
| `DeviceEnumerator::m_devices` (`src/Core/device_enumerator.cpp`) | mutated and iterated with no mutex | Not shown. BLE results reach it through a queued connection to the GUI thread (`device_enumerator.cpp:370–371`). The threads of the video and PPCP callers are not established | Check first; assert the GUI thread at the entry points | D |
| `PersonSegmenter` (`src/Pose/person_segmenter.cpp:42`) | runs CPU-only, with no execution-provider cascade | Shown in the code: it sets no execution provider, while the three pose estimators each carry their own cascade | A refactor that changes analysis output: after the freeze, with a stage-version bump and a re-run (Stage A) | A (after the freeze) |
| MediaPipe debug image (`src/Pose/pose_estimator_mediapipe.cpp:724–726`) | written on the hot path | Written once per process (guarded by `sCropSaved`), to `~/mediapipe_crop_debug.jpg`, with no environment switch. IM's EXT-10 describes it as behind a switch, which it is not | Remove it; low priority. Note for IM §8.3: EXT-10's wording | A (after the freeze) |
| `ppDebug()` (`src/Core/pp_debug.h:59`) | a dangling-`else` bug | The hazard is real: `if (false) PpLogStream(…)` captures a following `else`. A scan for `if (…) ppDebug() …; else` found no such site. Latent, not observed | Harden the macro | A (after the freeze) |
| Gemini key in the URL (`src/LLM/GeminiLlmEngine.cpp:66–70`) | key in the query string; default model id broken | The key in the URL is confirmed. No evidence is given that the default model id (`GeminiLlmEngine.h:44`) is broken | Move the key to a request header; check the model id before changing it | E |
| API keys in plaintext settings (`src/Secrets/SecretsManager.cpp:37–50`, seeding at `:60–84`) | Azure, Gemini and AssemblyAI keys in the settings file | Shown: `secrets/*` through `ppSettings()`, with a compile-time AssemblyAI default and environment overrides | Move the API keys to a keychain-backed store (Stage E) | E |
| The PPCP pairing root key | June did not raise it; the first draft of this page did | It is stored in settings through `ppSettings()` on purpose, "NOT `src/Secrets/SecretsManager`", and the reasons are recorded at `src/Ppcp/ppcp_pairing_store.cpp:19–70` (libppcp errata E56 and E57, commit `1616016d`): in the keychain, Windows and Linux could persist no pairing at all, and macOS prompted on every relink. The cost, a key readable by anything running as the user, was "judged and accepted" | Leave it where it is. A change is only a question for Mark, citing E56/E57 (§9, Q7) | none |
| The PPCP statics in `main.cpp` | exit-crash bugs | The crash was observed on 23 Aug and **is fixed**: `aboutToQuit` stops the host and drops the borrowed ledger while everything is alive (`src/Gui/main.cpp:1568–1595`, "Stopping here fixes it at the root"). What remains is design debt: three function-local statics whose order of destruction matters | Own them in the composition root (Stage E) | E |
| The pose warm-up at exit (`main.cpp:136–157`) | — | A quit during the warm-up ends the process early, on purpose. The Windows path is "NOT TESTED" (`main.cpp:153–157`) | Test a quit during the warm-up on the cabin PC before Stage E changes teardown | E |

---

## 5. The stages

The stages are named A–F so they do not collide with the IM's principles P1–P11. Each gives what it closes, deletes,
tests, documents and re-runs, as 04's steps do, and its ownership answers (IM §10) in brief, or says that they are
answered when the stage is planned.

Two facts apply to every stage:
- **There is no CI that runs tests.** `.github/workflows/release.yml` is the only workflow; it builds releases, is
  "authored, not yet run", and runs no `ctest`. Tests are run by hand, through `ctest`.
- **Windows is the primary platform.** Studios run Windows with an NVIDIA GPU. A stage that changes build,
  teardown, threading or devices is tested on the cabin PC, not only on the Mac.

Estimates are made in each stage's brief. What limits the pace is what 04 §4 names: builds on one machine, the hours a
GOLFSIMPC re-run takes, review rounds and Mark's testing; and here, the cabin PC with real cameras, IMUs and phones.

### Stage A · Bug fixes

04 §1 says "Bug fixes go to Stage 0 or to the step that owns the area". So the observed bugs below can go now, inside
the current refactor, if Mark agrees (§9, Q4). The rest are refactors or unobserved, and wait for the freeze to end.

**Now (observed on hardware or in the code):**
- **#184**: the swing-detection sensitivity subtitle says the opposite of what the setting does (`GeneralPanel.qml:222`,
  `SettingsIndex.qml:71`). Wording only.
- **#105**: a Witmotion can connect without its vertical-install setting, and nothing notices (`src/IMU/`). Log the
  read-back with `ppInfo`/`ppWarn`, retry the write, and show the sensor as not ready. Showing "not ready" on the
  device card is a design-system change (`PpDeviceCard`) (proposed, for Mark).
- **#183**: the cameras pick their own brightness target. The issue's own first step comes before any change: compare
  the camera's settings after Swing Catalyst opens it and after PinPoint does, with the bay lit and a bag standing in.
  Then take control of exposure compensation on connect (`src/Video/spinnaker_settings.cpp:190`,
  `applyConnectSettings`). The issue says the 2 Nov darkness "has two causes mixed together" (one bay light was on),
  so the fix is not claimed to explain all of it. Recording gain, compensation, black level and gamma per stream per
  swing is a field family in the swing file: an IMC (C3) (proposed, for Mark), and a `swing_json_schema.md` change.
- **#53, the detection half**: a swing whose video starts after impact passes the capture check. Add a coverage check
  in `recording_integrity` / `capture_integrity_check` (Rules ring). The capture cause is unknown (the issue says
  "perhaps because …"); finding it is Stage D's.

**After the freeze:**
- The `fetch_add`, the `BleAdapterPool` check, the `ppDebug()` macro, and the MediaPipe image (§4).
- The pose back-ends' execution-provider cascade, shared by the three estimators and the segmenter (proposed, for
  Mark: §9, Q10, because it changes segmentation output).

| | |
|---|---|
| Closes | #184, #105, #183 (its settings half), #53 (its detection half); June's `source_published_`, `ppDebug()`, MediaPipe and segmenter rows |
| Deletes | the three copies of the execution-provider cascade; the debug image write |
| Test | #105 and #183 on the cabin PC's sensors and cameras; #53 against a swing known to start after impact; `ctest` for the touched targets |
| Re-run | the segmenter's GPU path: a stage-version bump, a library re-run on GOLFSIMPC with a dry-run first, and a control run, because pose runs are non-deterministic. #53's coverage rule: Mark decides whether existing swings are re-graded |
| Docs | `docs/design/flir_camera_settings.md` and `docs/reference/swing_json_schema.md` (#183) |

**Ownership.** #183's new per-stream fields: source facts of the Recording (shot level), written once by capture
through `RecordingRepository::create` (R-REC-2), Vocabulary types in `src/Recording`, read by a view through the shot.
#105's readiness: application state of the device object, not persisted. #53's rule: answered in its brief, with
whether the integrity verdict is a recorded product (IM §7.2). Ids: FILE-SHOT-01 (the swing file's capture block),
R-REC-2, RING-RULES.

### Stage B · Build libraries and the `Core` split

After step 7, and after the spanning directories it touches are split (Premise 3). Its measurements are redone then.

- **Ring libraries** (proposed, for Mark): one static library per ring, starting with Platform, then Vocabulary,
  Rules (B-14 is the Diagnostics part of it), Domain model and the stores. Each links only inward; its include
  directories are `PRIVATE`. The 411 quoted includes that rely on the global paths are rewritten as each library is
  cut.
- **`Core` holds only the Platform ring** (proposed, for Mark; IMC C9), in `src/Core/` or a new `src/Platform/`.
  `kf3_smoother.h` moves to a Rules directory (it is already Rules by IM's RING-RULES); `pp_tuned_constants.h` and the
  other Vocabulary files of `Core` move with the Vocabulary library. The studio profile's directory is §9 Q11; the device
  enumerator moves with Stage D, because it is capture work.
- **Tests link libraries** instead of hand-listing sources.
- **#187**: one test-isolation helper for every suite (`QStandardPaths::setTestModeEnabled` or a temporary
  directory), in place of `XDG_DATA_HOME`, which Qt ignores on macOS.
- **#90**: the stale `session_setup_ui` and `qml_ui` suites. UI tests are run only when Mark asks, so greening
  `qml_ui` needs his say-so.
- **Tools**: `swinglab_run --doc-blocks` exits non-zero when its document write fails; `parity_baseline.py
  --self-test` works with other arguments (05 §3). Both need a build or a tools change, which this stage has.
- **The per-release re-run of the audit tools** (05 §3, 03 §14b) gets an owner: the release runbooks (proposed, for
  Mark).

| | |
|---|---|
| Closes | B-14; 05 §3's tool rows; #187; #90's suites (with Mark's say-so) |
| Deletes | the hand-listed source lists in test targets; the header-only workarounds such as `atomic_write.h`'s |
| Test | a clean build and the full `ctest` on the Mac and on the cabin PC (Microsoft's compiler, MSVC, links static libraries differently) |
| Re-run | none to analysis expected; the parity run must show 0 differences |
| Docs | `docs/developer/testing_developer_guide.md` (the isolation helper); IM §4.1 RING-PLATFORM, which should also list `post_shot_trace` (the lint's Platform table has it, `architecture_lint_test.cpp:213`; IM's row does not) |

**Ownership.** No domain data. Each moved file keeps its ring, except where the move is the point (C9: the new
directory, and the RING-* rows that name `Core`). Ids: RING-PLATFORM, RING-VOCABULARY, RING-RULES, B-14.

### Stage C · The Rig and calibration

The first stage of the capture plan. The IM correction of Premise 2 is made (DEC-193); the Rig's design is an IMC
(C1, C3, C4, C5, C7, C8) for Mark before anything is built.

Where rig facts live today (IM §2.3): settings SET-09 (`camera/*`, `imu/*`, `ppcp/studioName`, `ppcp/pairings`);
SET-05 (`analysis/clubLenPrior`, learned rig state per machine, DEC-153); the trunk ceremony FILE-SESS-11; and
assumptions in code ("fixed cameras are square", the uncalibrated down-the-line yaw σ, `pp_tuned_constants.h`). 3,978
lines of calibration QML run state machines, quaternion maths and pass/fail thresholds.

What to design (all proposed, for Mark):
- **OBJ-RIG with an identity, a versioned snapshot and one writer**, persisted per machine, in the library, or as a
  copy embedded in each Recording (§9, Q2). For N cameras and sensors (#180, #74, #178).
- **A rig reference in each Recording.** Today only IM prose reserves it; the code says "There is no rig reference
  yet" (`src/Recording/recording.h:97–98`). Adding it is a swing-file format change, with libppswing's backward
  compatibility: a swing without it reads as "rig unknown".
- **`RigChanged` (EVT-15)**: a calibration change makes a new snapshot and never alters a past Recording.
- **What the Rig holds** is IM §2.3's: the cameras and sensors, where they are, and their calibration. Exposure and
  gain are per-stream capture facts in the Recording (Stage A, #183), not rig facts.
- **One device key type** in the Vocabulary ring (IMC C5), in place of the open-coded `description + "|" + id` (7 sites
  in 5 files by a grep for `"|"` beside a serial, description or id: `camera_instance.cpp`, `camera_manager.cpp`,
  `imu_role_map.h`, `resource_monitor_controller.cpp`, `shot_processor.cpp`).
- **One perspective type (A-13)**: `CameraInstance::Perspective` (`camera_instance.h:145`) and
  `kImpact = 4` (`app_settings.cpp:36`) give way to `recording::Perspective` (`recording.h:56`).
- **Calibration procedures in C++.** The procedure belongs to Acquisition and its result to the Rig (IM §2.3). Pull
  the thresholds, maths and step transitions out of `ImuCalibrationFlow.qml`, `TrunkRoutine.qml` and
  `HackMotionArmRoutine.qml` into tested C++ that produces a typed result and commits it to the Rig. The trunk
  ceremony recorder's file (FILE-SESS-11) and its pool-thread write move with it. The brief reads
  `docs/design/system/README.md` and the component pages before any QML is touched.
- **#74 is a programme in its own right.** `docs/design/camera_calibration_design.md` stages it, starting with "make
  the focus failure visible". The Rig gives its results a home; the stage does not absorb the programme.

| | |
|---|---|
| Closes | 05 §1's Studio/Rig, `RigChanged` and ceremony-file rows; A-13; 05 §4's device key; the A5 lines of `trunk_ceremony_recorder.cpp` ×7; the A6 lines of `trunk_ceremony_recorder.h` ×2 |
| Deletes | `CameraInstance::Perspective`, `kImpact`, the open-coded device keys; the calibration logic in QML |
| Test | an equivalence test of each calibration procedure, old QML logic against new C++, on recorded inputs; the procedures on the real sensors on the cabin PC; a reader test for swings without a rig reference |
| Re-run | none unless calibration results start feeding analysis; then a stage-version bump and a re-run |
| Docs | IM §2.3, §8, §11; `docs/design/camera_calibration_design.md`; `docs/reference/swing_json_schema.md` |

**Ownership.** Answered when the stage is planned, from the Rig's design. Ids it touches:
OBJ-RIG, EVT-15, FILE-SESS-11, SET-05, SET-09, A-13, the Recording's stream setup (R-REC-*).

### Stage D · The capture seam

After Stage C, so the capture service writes rig references from the start.

- **The frozen window stops being a view onto the ring.** `swing_window.h` and `swing_payload_source.h` (Vocabulary
  ring) include `source_ring.h` (Adapters) for `SourceRing::ReadHandle`. A value type for the handle, in the
  Vocabulary ring, closes their two A1 lines (proposed, for Mark).
- **Split `ShotProcessor`** (2,153 + 503 lines; 17 A1 findings). Its five jobs today: post-roll and late-source
  gathering over the event buffer and the device managers; the export and the frozen window; session and shot
  bookkeeping; starting the analysis; live replay. IM puts the capture service in the Application ring (IM §4:
  "services that run work: capture, …"; RING-APPLICATION lists `shot_processor`), and says the capture services'
  device half leaves in this refactor (IM §4.1). So (proposed, for Mark; IMC C9): the capture service stays in
  Application, and its device half moves to an Adapters-ring collaborator behind an interface the inner ring
  defines. That removes the device and ring includes behind the A1 lines.
- **The shot is allocated by `Session::addShot`** (DEC-142: allocate, then create through
  `RecordingRepository::create`, then capture writes), not by the capture service. The capture service binds the
  target session when the shot is triggered, so a shot in flight when End is pressed lands in that session (#188).
  Whether `addShot` is called at the freeze rather than after the export is a change of timing (C8) for Mark (§9,
  Q8). #188 itself has not been re-checked since step 3.4a-ii (the issue says so); check it first.
- **Late launch-monitor readings arrive through `Shot::completeCapture` only** (R-REC-2, EVT-22). #192 was found in
  step 3.4b-ii's probe; ask the lead whether step 3 or 4 closes it before planning it here.
- **#53's capture cause** is unknown; investigate it here, with the probe, on back-to-back swings and near End.
- **`ShotController`** (640 + 346 lines; 3 A1 findings) takes narrow interfaces instead of the event buffer and the
  session controller; one arbiter for all phones (#78: "today two phones make two shots in two sessions from one
  ball"); the shot source becomes an explicit persisted type instead of an enum whose order matters
  (`Source::Acoustic == 4`) (proposed, for Mark; IMC C3/C9).
- **`CameraInstance`** (2,186 lines, up from 1,721 in June) gives its pipeline construction and its ball-presence
  monitor to collaborators, as June proposed; `CameraManager` (1,052) and `ImuManager` (1,041) share their
  device-selection and producer mechanics.
- **The device enumerator** moves out of `Core` into an Adapters capture directory, and its `Device` struct, which
  couples Video, IMU and Bluetooth types, is split (proposed, for Mark; IMC C9).
- **The motion-capture probe** (`Pose/motion_capture_probe.*`): its A1 line (it includes `SessionController`) and its
  A6 line go with the probe's move out of `src/Pose` and an injected "is a session running" question. #86's items
  (it benchmarks a different pose format; opening Settings runs it) are fixed with it.
- **Video duplication**: the pixel-format name mapping appears in `video_input_factory.cpp`, `VideoInputAravis.cpp`
  and `VideoInputSpinnaker.cpp`; one function.

| | |
|---|---|
| Closes | 05 §1's transport, pipeline, `swing_payload_source.h`, `swing_window.h`, `shot_controller` and `ShotProcessor` rows; E-09's capture half; the A6 lines of the camera, IMU and buffer-controller headers; the probe's A1 and A6 lines; #188; #53's capture half; #192 if not closed earlier; #78's arbiter |
| Deletes | `ShotProcessor` as one class; the ring include in the window types; the copies of the pixel-format mapping |
| Test | a live capture probe on the cabin PC; #188: End pressed while a shot is in flight, the shot lands in that session; back-to-back swings and a swing just before End each save a full clip on every camera; `ctest` |
| Re-run | the parity run (re-analysis must not change); the live-equals-re-analysis check on real shots (04 step 1's) |
| Docs | IM §4.1, §11; `docs/developer/event_buffer_developer_guide.md` (the window's handle) |

**Ownership.** Answered when the stage is planned. Ids: RING-APPLICATION, RING-ADAPTERS, LC-SHOT, ID-SWING-NUMBER,
R-REC-2, EVT-01, EVT-22, DEC-142, A-13, E-09.

### Stage E · App host, settings, threading and secrets

The second half of the non-capture plan (05 §2 and §4's settings-fallback row; proposed, for Mark, as one plan,
because they are one change: inject the settings object from `app_settings.h`, and delete each local fallback). The
composition root and the updater's injection can start any time after Stage B. The settings split waits for Stages C
and D: 20 of the 24 `AppSettings fallback` declarations are in `camera_manager.cpp` (11) and `imu_manager.cpp` (9),
which Stage D edits, and the rig keys leave `AppSettings` in Stage C. The alternative is to delete each manager's
fallbacks in Stage D, as that file is edited (for Mark).

- **The composition root** (proposed, for Mark; IMC C9). One owning object for what `main.cpp` builds today (1,662
  lines, 43 `setContextProperty` calls), with members declared in the order they must be torn down, and no
  function-local static services (the three PPCP statics, §4). It cannot sit in `src/App`, which is the Application
  ring (RING-APPLICATION): a composition root includes adapters, so lint A1 would fail. It stays beside `main.cpp`,
  which is `# kept (composition root)` in the allowlist, unless Mark gives it a ring of its own. The probe harnesses
  (`PINPOINT_PROBE_*`) move to their own file.
- **The updater** takes an injected "is a session running" question and its one setting, which deletes its A1 lines
  (`Update/linux_appimage_backend.cpp` ×1, `mac_sparkle_update.mm`, `update_controller.cpp`, `win_sparkle_update.cpp`
  ×2 each).
- **`AppSettings`** (1,958-line header, 163-line `.cpp`): implementations out of the header; split into groups
  backed by one injected settings object (proposed, for Mark); no stack-local `AppSettings fallback;`. Today the
  fallback idiom has 24 declarations in production code (grep `AppSettings +\w*[fF]allback`, tests excluded, gives 27 hits;
  three are comments: `app_settings.cpp:58`, `imu_manager.cpp:616`, `stt_processor.h:49`). Its setters write `ppSettings()` directly (for example `app_settings.h:1024–1030`), so a
  write through a fallback persists; what is lost is the live object's cached value and its change signal. Where a
  group holds keys that are domain facts (IM §8.2), moving them is an IMC (C3, C4).
- **Threading: what is left of F-09 after step 5, re-measured at that point.** Step 5 closes F-09 for the Charts
  presenters ("one presenter base and one threading idiom", 04 step 5), and step 3.7 moves the diagnostics owners
  onto the pool (IM §11.1, R-CON-5's row). Today's raw counts (grep, tests excluded: `QThreadPool` 47,
  `QtConcurrent` 50, `WorkPool` 26, `std::thread` 23, `new QThread` 14) describe a tree those steps will change.
  What they will not reach:
  - **Long-lived `QThread` + `moveToThread` owners**, each with its own teardown: `CameraInstance` (four threads:
    capture, preprocess, pose, ball; `camera_instance.cpp:103–431`), `ImuInstance` (`ImuIoWorker` on the manager's
    I/O thread, `imu_instance.cpp:153–154`), `HmInstance` (`hm_instance.cpp:224`), `TtsController`,
    `TranscriptionController` (two threads), `LlmController`, `DeviceEnumerator` (the IMU scan thread,
    `device_enumerator.cpp:354–358`) and `SttProcessor` (`stt_processor.cpp:68–69`). About 12 threads in 8 owners.
  - **Finite jobs on other idioms**: `MotionCaptureProbe` on its own `std::thread` (`motion_capture_probe.cpp:143`)
    and `TrunkCeremonyRecorder` on the global `QThreadPool` (`trunk_ceremony_recorder.cpp:424`). These are pool work
    by R-CON-5 as it stands, so moving them is making code match the IM, not a change to it.
  - **The proposal: amend R-CON-5 to allow one helper for long-lived hardware and media threads** (proposed, for
    Mark; IMC C11). R-CON-5 says "One threading idiom … Never: a new pool, thread or idiom" (IM §9.1). The real-time
    capture loops (§3) are the case for the amendment: they cannot run on the pool, so today each owner hand-writes
    the same start, drain and `quit()`/`wait()` teardown. One tested helper would replace those copies. The
    alternative is to keep one idiom and name the capture threads as exceptions (§9, Q9).
- **Secrets**: move `SecretsManager`'s API keys (Azure TTS and STT, Gemini, AssemblyAI) to a keychain-backed store
  generalised from `mail_credentials_*`, into `src/Secrets/` (proposed, for Mark); drop the compile-time AssemblyAI
  default (`SecretsManager.cpp:64–70`); move the Gemini key to a header. The pairing key is not in this item (§4).
  #89's "no keychain prompt on a signed upgrade" check applies.
- **Media duplication**: the cloud-fallback pair (`applyCloudFallbackPref`, `refreshCloudAvailability`) appears in
  the TTS, transcription and LLM controllers; one shared piece. `STTBackendAssemblyAI` (296 lines) is built but
  referenced by nothing outside its own files: delete it, or wire it (for Mark).
- **#193** (the app's own trash always): new behaviour, held by the freeze, and it overlaps step 3's housekeeping
  (DEC-80, SET-14). Ask after step 3 (§9, Q6).

| | |
|---|---|
| Closes | 05 §2's updater A1 lines; 05 §2's studio-profile lines (`studio_profile.cpp` A5 ×9 with the profile's repository, `studio_profile.h` A6 ×4 unless the lint-scope ruling takes it out of scope; DEC-193); 05 §4's settings-fallback row; F-09's remainder (as re-measured); the A6 lines the lint-scope ruling leaves in scope; June's secrets and Gemini rows |
| Deletes | the PPCP statics; `AppSettings fallback;`; the hand-written thread teardowns (if the amendment is agreed); the compile-time key default |
| Test | quit at every point of start-up and during the pose warm-up, on the Mac and the cabin PC (the Windows path is "NOT TESTED", `main.cpp:153–157`); the helper's own tests under ThreadSanitizer (TSan, the data-race checker); settings read and write tests; a signed upgrade on macOS with keys in the keychain |
| Re-run | none to analysis |
| Docs | IM §8.2, §9.1 (if R-CON-5 is amended), §11; the settings and secrets developer notes |

**Ownership.** Answered when the stage is planned. Ids: R-CON-5, F-09, SET-* (those that move), RING-APPLICATION,
the composition root's allowlist entry (`Gui/main.cpp`, A5, `# kept`).

### Stage F · The phone link (PPCP)

The last stage of the capture plan, after Stage E's composition root, which takes PPCP's ownership.

- **PPCP's files** (all "design: the platform refactor (capture)" in IM §8.1): the import ledger FILE-LIB-03, the
  imported clips FILE-LIB-04, the legacy ledger FILE-LIB-05, and the annotations FILE-SHOT-11. Retire FILE-LIB-05 once
  it is shown unused, or migrate it once; one repository for the ledger; decide whether FILE-SHOT-11 joins the
  `Shot` (as FILE-SHOT-12 did, DEC-187) or is a peer cache (all proposed, for Mark; IMC C1, C3, C4).
- **`PpcpHostService`** (3,709 + 1,191 lines) splits into the headless engine it already builds on (`makeHostEngine`,
  `src/Ppcp/ppcp_host_peer.cpp:558`) and a typed presenter for QML (its A6 ×6).
- **#78 and #79's defects**: a disabled camera still gets a preview; the wired preview channel drops about every 50
  s; a cabled phone connects twice; MP4 timestamps not rebased; the misleading "still processing the previous shot"
  message (`shot_controller.cpp:577`). The shared arbiter is Stage D's.
- **Notification coalescing** (#79: "one unmounted share once produced 10 identical toasts"): a change to how
  `PpNotificationHost`, `PpNotificationCard` and `PpToast` behave, a design-system change (proposed, for Mark).

| | |
|---|---|
| Closes | 05 §1's PPCP A5 row (seven files) and `ppcp_host_service.h` A6 ×6; #78 and #79's listed defects |
| Deletes | FILE-LIB-05 (if retired); PPCP's own file access outside its repository |
| Test | the PPCP conformance suite (`docs/conformance/ppcp-conformance.md`); two phones on the cabin PC; a cabled phone |
| Re-run | none to analysis |
| Docs | IM §8.1, §11; the PPCP design docs |

**Ownership.** Answered when the stage is planned. Ids: FILE-LIB-03, FILE-LIB-04, FILE-LIB-05, FILE-SHOT-11, SET-09
(`ppcp/pairings`, unchanged).

---

## 6. What not to do

| Item | Recommendation | Why |
|---|---|---|
| Re-tagging single A6 lines `# kept` one at a time | Ask for the lint-scope ruling instead (Premise 4) | One decision settles them |
| Rewriting `src/Update` into a back-end split (June's Theme J) | Already done | §3 |
| Rewriting the ring buffer's internals | Leave them | §3 |
| Forcing real-time capture loops onto the pool | Do not | §3; Stage E |
| Rewriting the STT and TTS workers' parallelism | Leave it | Their payloads differ (the June review) |
| Moving the pairing key into a keychain | Do not, unless Mark reopens E56/E57 | §4 |
| Hardening the lint's A1 blind spots now | Wait for Stage B; then decide whether the A1 rows stay or go (§9, Q12) | Libraries cover A1 only for single-ring directories; A2, A4, A5 and A7's limits are not affected by libraries and stay as 05 §3's rows |
| A machine-readable IM (Q-20) | Reconsider, rather than defer indefinitely | The first draft said "unless doc drift becomes a recurring real failure". The review found drift in 05 (counts, line numbers), in IM (FILE-APP-06, since corrected by DEC-193; EXT-10; `post_shot_trace` missing from RING-PLATFORM) and in the first draft of this page. That is the failure the condition names (for Mark) |

---

## 7. Order

```
Stage A (now: observed bugs, under 04 §1, if Mark agrees)
   │
   ▼  the current refactor's steps 3–8; the freeze ends
Stage A (the rest)          Stage B · build libraries and the Core split (after step 7)
                               │
          ┌────────────────────┴──────────────────────┐
          ▼                                           ▼
   capture plan                               non-capture plan
   Stage C · the Rig  ──────────┐             Stage E, part 1 · composition root, updater
                                │                     │
          ▼                     │                     │
   Stage D · the capture seam ──┴──────────▶  Stage E, part 2 · settings split, threading, secrets
          │                                           │
          └───────────────▶ Stage F · PPCP ◀──────────┘  (needs the composition root)
```

- Stage C before the settings split: SET-05 and SET-09's rig keys leave `AppSettings` for the Rig, so splitting
  `AppSettings` first would be redone.
- Stage D before the settings split: `camera_manager.cpp` and `imu_manager.cpp` hold 20 of the 24 fallbacks.
- Stage F after Stage E's composition root, which owns PPCP.
- The capture plan and the non-capture plan are not independent: they meet at `AppSettings`, the device managers
  and the composition root. Run them in this order, or plan the shared files once.

---

## 8. Open issues this touches

| Issue | State at HEAD | Stage |
|---|---|---|
| #183 cameras pick their own brightness target (high) | open; "two causes mixed together" on 2 Nov; the issue's comparison comes first | A (settings), A (recording the values, an IMC) |
| #105 Witmotion connects without its vertical-install setting (high) | open | A |
| #53 a swing saved with video starting after impact passes the check | open; the capture cause is unknown ("perhaps because …") | A (detection), D (cause) |
| #184 the sensitivity subtitle is inverted | open, no labels; wording only | A |
| #188 a shot in flight at End goes to a stray folder | open; not re-checked since step 3.4a-ii (the issue); the dashboard says it "belongs with step 1" | D, after a re-check |
| #192 a live row's launch-monitor device flips until End | open; found in step 3.4b-ii's probe | ask the lead first; else D |
| #187 `model_browser_test` writes to the real macOS app data | open | B |
| #90 tests, corpus and library upkeep | open | B (the stale suites, with Mark's say-so) |
| #31 3-D views go blank after a while | open; "The root cause is not known … Hiding is a strong lead but not a proven cause" | its own track, until a cause is found; no stage owns it |
| #78, #79 phone capture | open programmes; their listed defects | D (the arbiter), F (the rest, and coalescing) |
| #85 devices and session setup | open programme; "Calibrate mag" and "Save to flash" have no engine function | C |
| #86 performance | open programme; the motion-capture benchmark items; camera and IMU teardown joins | D (the probe), E (teardown) |
| #89 release, updates and platforms | open programme; a "no keychain prompt on a signed upgrade" check; write-back refusing older recordings with no session type | E (the keychain check); the write-back item is not platform |
| #74, #178, #180 calibration, set-up aim, a third perspective | open programmes; they set the Rig's requirements | C (the model), not the programmes |
| #193 the app's own trash always | open; new behaviour, held by the freeze | ask after step 3 |

The other open bugs are analysis, tracker, diagnostics or UI issues outside the platform layer.

---

## 9. Open questions for Mark

Answered: the first draft's Q1, "correct IM's Studio/Rig entry now?", is answered by DEC-193 (Mark, 21 Nov 2026;
Premise 2). Its number is kept free so the other questions keep theirs.

Kept from the first draft:
2. **Where does the Rig live?** Per machine (`<AppData>`), in the library keyed by its id, or as a copy embedded in
   each Recording. Analysis runs on two machines (the Mac and the cabin PC), so a per-machine store means the Mac cannot
   read the cabin's rig unless each Recording carries a copy. The embedded copy may be the simplest.
3. **How active is phone capture over the next six months?** If it is a main path, Stage F matters; if it is parked,
   only the composition root's ownership of the PPCP statics is needed (Stage E).
4. **Stage A's observed bugs now?** #184 (wording), #105 and #183 (observed on hardware) and #53's detection half,
   under 04 §1's bug-fix rule, rather than after step 8.

Reframed:
5. **The lint-scope ruling** (05 "Decisions waiting"): narrow A6 back to P9's wording, a presenter's or domain type's
   public API? That takes 05 §2's settings, monitoring and tool lines out of scope (Premise 4).
6. **#193**: ask after step 3, which builds housekeeping as the one purging service (DEC-80, SET-14).

Added:
7. **The pairing key**: keep it in settings, as E56/E57 decided (`ppcp_pairing_store.cpp:19–70`), or move it to a
   keychain, accepting a prompt on macOS and no store on Linux? This page recommends keeping it.
8. **The capture service**: does it stay in the Application ring with its device half in Adapters (IM §4.1), and does
   `Session::addShot` (DEC-142) allocate the shot at the freeze or after the export?
9. **R-CON-5**: amend it to allow one helper for long-lived hardware and media threads, or keep one idiom with the
   capture threads named as exceptions?
10. **GPU segmentation**: is it wanted, given a stage-version bump and a library re-run?
11. **The studio profile's directory**: its owner is decided (`StudioProfile`, application level, DEC-193); which
    directory does it move to, with Stage B's `Core` split?
12. **After Stage B**: keep the lint's A1 limits as 05 rows or retire them; and the pre-commit hook (05 "Decisions
    waiting") and the per-release audit re-run (05 §3)?
13. **Windows testing**: which cabin-PC tests does each stage need? This page names them per stage; Mark sets the bar.
14. **Q-20**: given the drift the review found, plan the machine-readable IM after all (§6)?

---

## Appendix A. Every 05 row, placed

| 05 row | Placement |
|---|---|
| §1 Transports, devices, ring buffer, connectors, threading inside capture | Stages C, D, F |
| §1 The capture pipeline that produces a Recording | Stage D |
| §1 Studio/Rig | Stage C; the IM correction is made (DEC-193) |
| §1 `RigChanged` | Stage C |
| §1 Studio and acquisition files | Stage C (ceremony); Stage F (PPCP ledger); the studio profile is now 05 §2's (DEC-193) |
| §1 `swing_payload_source.h`, `swing_window.h` A1 | Stage D |
| §1 `shot_controller.cpp` A1 ×2, `.h` A1 ×1 | Stage D |
| §1 `ShotProcessor`'s capture half (A1 13 + 4 today) | Stage D |
| §1 E-09, the capture half | Stage D |
| §1 A-13 | Stage C |
| §1 `motion_capture_probe.h` A6 ×1 | Stage D |
| §2 `studio_profile.h` A6 ×4 (moved from §1, DEC-193) | Stage E; out of A6 scope if the lint-scope ruling narrows it |
| §1 `buffer_controller.h` ×1, `camera_instance.h` ×2, `camera_manager.h` ×2 A6 | Stage D |
| §1 `imu_instance.h` ×2, `imu_manager.h` ×3 A6 | Stage D; `trunk_ceremony_recorder.h` ×2: Stage C |
| §1 `ppcp_host_service.h` A6 ×6 | Stage F |
| §1 PPCP's A5 lines (seven files, FILE-LIB-03…05, FILE-SHOT-11, the pairing store) | Stage F; the pairing store's line with the repository design, its storage unchanged |
| §1 `trunk_ceremony_recorder.cpp` A5 ×7 | Stage C |
| §2 `studio_profile.cpp` A5 ×9 (moved from §1, `# platform refactor (settings)`, DEC-193) | Stage E, with the profile's repository |
| §1 `motion_capture_probe.cpp` A1 ×1 | Stage D |
| §2 `app_settings.h` A6 ×26 | the lint-scope ruling; the rig keys' shapes in Stage C; else Stage E |
| §2 `markup_controller.h` A6 ×11 | the lint-scope ruling (a tool); if A6 stays wide, Stage E |
| §2 `profiler_controller.h` ×8, `resource_monitor_controller.h` ×4 | the lint-scope ruling; proposed out of scope (single-consumer monitoring) |
| §2 `llm_controller.h` ×1, `transcription_controller.h` ×1, `LlmWorker.h` ×1 | the lint-scope ruling; proposed out of scope |
| §2 `app_info.h` ×1, `swing_zip_exporter.h` ×2 | the lint-scope ruling; proposed out of scope |
| §2 the updater's A1 lines | Stage E |
| §2 `archive_controller.h` A6 ×1 | re-check after step 3, as 05 says; then the lint-scope ruling |
| §3 lint A1 reads only quoted includes | Stage B, then §9 Q12 |
| §3 lint A2 misses module-qualified creation | deferred: no case today; keep the row |
| §3 lint A4 misses concatenated file names | deferred: no case known; keep the row |
| §3 lint A5 misses `std::filesystem` and `QStandardPaths` | deferred; worth revisiting in Stage C and F, which move file access into repositories |
| §3 lint A7 misses trailing-return functions | deferred; keep the row |
| §3 IMU-integrity parity untested | deferred: needs a Witmotion swing (05's "later decision"); not platform work |
| §3 report figures and key screens in the parity baseline | not this page's: inside the current refactor, before steps 4 and 5 |
| §3 `swinglab_run --doc-blocks` exit code | Stage B |
| §3 `parity_baseline.py --self-test` | Stage B |
| §3 the per-release re-run of the audit tools | Stage B (an owner), §9 Q12 |
| §3 the pre-commit hook | §9 Q12 (Mark's decision) |
| §3 B-14 | Stage B |
| §4 the settings-fallback idiom | Stage E |
| §4 the device key | Stage C |
| §4 markup truth files | stale: now the `Shot`'s (appendix B) |
| Decisions: the `# unplanned` lines (25 today) | placed line by line above |
| Decisions: the lint-scope calls | §9 Q5 |
| Decisions: the pre-commit hook | §9 Q12 |
| Decisions: G-05, D-20, E-16 | out of scope: design-system items 05 holds only so they are not lost |

## Appendix B. Stale 05 rows (a note for whoever next updates 05; 05 is not edited here)

- §1 `ShotProcessor`: "`shot_processor.cpp` A1 ×16". The allowlist now says 13 (the `.h` 4).
- The "L n" line numbers throughout are stale; for example, `app_settings.h`'s A6 entry is now allowlist line 165, not
  L107, and the window types are lines 31–32, not L28–29. Cite by path and rule instead.
- §1 "Studio and acquisition files": "The trunk ceremony recorder still writes a part file then renames". It now
  writes through `atomicWrite`, copying the earlier ceremony aside first (`trunk_ceremony_recorder.cpp:435–448`; IM
  §8.1 FILE-SESS-11).
- §4 "Markup truth files": FILE-SHOT-12 is now owned by the `Shot` (IM §8.1; DEC-187), and its A5 lines are tagged
  `# step 3`.
- §4 "The settings-fallback idiom: 48 uses". 24 declarations today (27 hits by `AppSettings +\w*[fF]allback`, tests excluded, three of them comments); 05
  should state the pattern it counts.
- "Decisions waiting": "The 23 platform-layer `# unplanned` allowlist lines (… 4 A1 and 19 A6 …)". There are 25: 6 A1
  and 19 A6.
