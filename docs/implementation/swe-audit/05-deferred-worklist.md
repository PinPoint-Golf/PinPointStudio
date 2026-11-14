# 05 · Deferred worklist: the technical and platform layer

**Status:** 14 Nov 2026, at HEAD `3b0ead97`. Updated the same day with Mark's decisions: the triage approved as
proposed (its platform-shaped items and the three it could not place are added here); the later review of capture is
called **the platform refactor (capture)**; a new step 8 in 04.

## Purpose

Mark, 14 Nov 2026: *"the refactoring of the technical/platform layer will come after this, so let's just keep track
of the deferred items so there is a worklist with some context when we get to them."*

This page lists only **technical and platform-layer** items that the refactor (steps 1–8 of
[04-remediation-plan.md](04-remediation-plan.md)) deliberately leaves out (Mark's rule, 14 Nov): devices, capture,
settings, tool APIs, build and test infrastructure, and Studio/Rig. They are the worklist of 04's "Later · the
platform refactor". Nothing in the audit's core scope belongs here: analysis and its producers, metrics, diagnostics,
coaching, their persistence, and the views' derivation of those facts. The core items that no step had scheduled
were placed in steps 1–8 by the triage (`build/refactor/TRIAGE-PROPOSALS.md`, approved by Mark 14 Nov 2026). The
one exception is the last table on this page: three design and design-system proposals the triage could not place,
held here only so they are not lost.

## How to use it

| Step | What to do |
|---|---|
| 1 | Pick one group (one section below). Don't mix groups in one plan. |
| 2 | Re-check it against the tree. Run `ctest -R '^architecture_lint_test$'` and `grep '# unplanned' src/Gui/tests/architecture_allowlist.txt`. |
| 3 | Read the row's evidence: the allowlist line, the design section, the notes file. |
| 4 | Propose a plan to Mark, with the ownership section (03 §14a). Any information-model change (a new owner, file, identity key, event, or anything created by a view) and any design-system change needs his approval **before** it is built. Both rules are in `CLAUDE.md`. |
| 5 | Until the freeze ends (04 §1), nothing here is built unless it is in the plan. |
| 6 | When a row is done, delete it here in the same commit, and lower or delete its allowlist line. |

**Conventions.**
- "L n" is a line of `src/Gui/tests/architecture_allowlist.txt` in the working tree. It is written that way, not as
  `path:line`, so the audit's citation check (pinned at `37289e33`) skips it.
- Suggested homes:
  - **platform refactor (capture)**: the capture part of 04's "Later · the platform refactor": transports, devices,
    the ring buffer, connectors, calibration and Studio/Rig;
  - **platform refactor**: the rest of the technical/platform-layer refactor that follows this one;
  - **later decision**: Mark has to decide what it should be first.

---

## 1. The platform refactor (capture)

This is a separate review of transports, devices, the ring buffer, connectors and calibration, held against the
Recording contract (04 "Later · the platform refactor"; 03 §0.4). Steps 1–8 fix the contract but leave capture's
internals as they are.

| Item | What and why deferred | Evidence | Blocks or risks | Suggested home |
|---|---|---|---|---|
| Transports, devices, ring buffer, connectors, threading inside capture | Out of the audit's scope by design: it covered Analysis and Domain + Application only | 03 §0, §0.4; IM §2 | Capture defects can only be patched locally until this is done | platform refactor (capture) |
| The capture pipeline that produces a Recording, and how devices deliver data into it | Only the contract (its contents, "analysis only on a Recording", its name) is decided now | 03 §0.1, §0.4 | Step 1's one builder sits on today's capture path | platform refactor (capture) |
| Studio/Rig: its full model, identity, persistence, writer and calibration procedures | Named as a domain entity (decision 17), designed later | 03 §0.2; IM §2 "Studio/Rig", §5, §11 | Rig assumptions are spread through the code ("fixed cameras are square", the uncalibrated DTL yaw σ). A Recording's "reference to rig state" has nothing to point at yet | platform refactor (capture) |
| The `RigChanged` event | Named; what it recomputes is designed with Studio/Rig | 03 §5; IM §7 | Nothing re-grades or re-analyses when a camera moves | platform refactor (capture) |
| Studio and acquisition files: studio profile, PPCP import ledger, calibration and ceremony recordings | Left out of the persistence table on purpose | IM §8; 03 §7 | No repository and no one-door rule for them. The trunk ceremony recorder still writes a part file then renames (step 2's helper should take it) | platform refactor (capture) |
| `Buffer/swing_payload_source.h` (A1 ×1), `Buffer/swing_window.h` (A1 ×1) | The Recording's frozen window is still a view onto the capture ring (`source_ring.h`) | L28–29 | The Recording is not yet a value independent of the ring | platform refactor (capture) |
| `Gui/shot/shot_controller.cpp` (A1 ×2), `shot_controller.h` (A1 ×1) | The shot trigger, an Application service, listens to the devices and the ring itself | L30–31 | Application keeps depending on Adapters | platform refactor (capture) |
| The capture half of `ShotProcessor` (`Gui/shot/shot_processor.cpp` A1 ×16, `.h` A1 ×4) | Tagged `# step 1`. Step 1 moves the analysis-job half out (the store, athlete and session includes) and the lines shrink; what remains is re-tagged (D8, Mark 14 Nov 2026): the device and ring includes (`camera_*`, `imu_*`, `hm_instance`, `device_enumerator`, ring payload sources) → `# unplanned`, the platform refactor (capture); the carousel → step 3; the session folder → step 2 | L20–21; G3-REVIEW-2 item B; 04 §1 "done"; step 1 brief §4.10, D8 | Application keeps device includes until the platform refactor (capture) | step 1 (re-tag), then platform refactor (capture) |
| E-09, the capture half (High) | Step 1 closes "the parity half of E-09" only | 04 step 1 Closes | `ShotProcessor`'s capture, carousel rows and replay player stay mixed | platform refactor (capture) |
| A-13 (Med): camera roles defined in the GUI, passed down as magic ints (the lead's call, 14 Nov 2026: the triage offered this home or step 1, and step 1's brief does not claim A-13; for Mark) | A device identity that analysis consumes. Step 1's Recording carries one perspective mapping, so analysis stops reading magic ints (step 1 brief §4.1); where the roles are defined, and device identity itself, is capture's | finding A-13; 03 §0.1 | Roles stay a Gui enum | platform refactor (capture) |
| `Pose/motion_capture_probe.h` (A6 ×1) (triage §4, Mark 14 Nov 2026) | The hardware probe behind the "Motion capture quality" setting: platform and execution-provider code that happens to live in `src/Pose`, not pose analysis | L136; the file's header | Its QVariant contract is documented only in comments (D-10) | platform refactor (capture) |
| `Core/studio_profile.h` (A6 ×4) | The QML API of a device or capture object; no step types it | L105 | QVariantMap contracts documented only in comments (D-10) | platform refactor (capture) |
| `Gui/cameras/buffer_controller.h` (A6 ×1), `camera_instance.h` (A6 ×2), `camera_manager.h` (A6 ×2) | As above | L108–110 | As above | platform refactor (capture) |
| `Gui/imu/imu_instance.h` (A6 ×2), `imu_manager.h` (A6 ×3), `trunk_ceremony_recorder.h` (A6 ×2) | As above | L112–114 | As above | platform refactor (capture) |
| `Ppcp/ppcp_host_service.h` (A6 ×6) | As above | L137 | As above | platform refactor (capture) |

## 2. QML APIs of settings, monitoring and tools (A6, 10 lines)

These objects are not presenters, so step 5's typed presenters don't reach them, and no step types their QML API
(P9: the QML boundary is typed).

| Item | What and why deferred | Evidence | Blocks or risks | Suggested home |
|---|---|---|---|---|
| `Gui/app/app_settings.h` (A6 ×26) | Settings stay a context global after step 5 ("devices, settings, logging and `app`"), so nothing types them | L107 | The largest settings contract in QVariant shapes | platform refactor |
| `Gui/markup/markup_controller.h` (A6 ×11) | A tool (the video markup) | L115 | Markup truth files are also outside the persistence table (section 4) | platform refactor |
| `Gui/monitor/profiler_controller.h` (A6 ×8), `resource_monitor_controller.h` (A6 ×4) | Monitoring objects | L118–119 | Low | platform refactor |
| `Gui/media/llm_controller.h` (A6 ×1), `transcription_controller.h` (A6 ×1) | Tool objects | L116–117 | Low | platform refactor |
| `LLM/LlmWorker.h` (A6 ×1) | A QVariantList slot, visible to the lint since the follow-up widened A6 to slots and signals | L135 | Low | platform refactor |
| `Core/app_info.h` (A6 ×1) | A settings or monitoring object | L104 | Low | platform refactor |
| `Export/swing_zip_exporter.h` (A6 ×2) | A tool. `src/Export` is renamed in step 7, but its API is not typed | L106 | Low | platform refactor |
| `Gui/settings/archive_controller.h` (A6 ×1) | The archive becomes a `Session` command (steps 2–3), but no step types the settings screen's controller API | L126 | Check after step 3: the line may go with the old archiver | platform refactor |

## 3. Build and test infrastructure

| Item | What and why deferred | Evidence | Blocks or risks | Suggested home |
|---|---|---|---|---|
| Lint A1 reads only `#include "…"` | A project header in angle brackets is not seen. If this is added, resolve by exact path only: a basename fallback would map an SDK header onto a project one with the same basename. An include with an ambiguous basename is skipped silently | lint test header, KNOWN LIMITS | No case today | platform refactor |
| Lint A2 misses module-qualified creation (`X.Type {`) | No QML imports PinPoint types `as` today | as above | — | platform refactor |
| Lint A4 misses a file name built by concatenation | — | as above | — | platform refactor |
| Lint A5 doesn't look for `std::filesystem` or `QStandardPaths` | — | as above | — | platform refactor |
| Lint A7 misses trailing-return functions (`auto f() -> T {`) | `size_table.py` has the same blind spot | as above | — | platform refactor |
| IMU-integrity parity is untested | The library has no Witmotion (Madgwick, host-fused) swing, so the baseline never exercises the `imuIntegrity` block or its branch of `swinglab_run --doc-blocks`. HackMotion fuses on the device and makes no claim | `build/refactor/step-G/G2-NOTES.md` §6 | Step 1 rewires the integrity verdicts, and the baseline cannot catch an `imuIntegrity` regression | later decision (needs a Witmotion swing) |
| The report figures and key screens are not in the parity baseline | Deferred from Step G; they must be captured before the first step that touches them | G2-NOTES §6; Step G brief §2 | Steps 4 (screens) and 5 (report) would have nothing to diff against | **before step 4 and step 5** (inside the refactor) |
| `swinglab_run --doc-blocks` exits 0 when its document write fails | It only warns. `parity_baseline.py` guards it (a missing `summary` fails the swing); the C++ fix needs a build | G2-NOTES §6–7 | A direct caller of `swinglab_run` misses the failure | the next tool rebuild |
| `parity_baseline.py --self-test` works only as the sole argument | With a label as well, it ignores the flag and runs a real Mac half (the scratch guard held) | G2-REVIEW-3 nit | A confusing run on the share's scratch | any later tools change |
| Per-release re-run of the audit tools | 03 §14b lists it as a gate: diff the include graph, the seam, identities, sizes and duplicates against the last release. No step and no owner | 03 §14b | Decay between releases goes unmeasured | platform refactor |
| The pre-commit hook | 03 §14b: run both lints before `git commit` | 03 §14, decision 3 | — | later decision |
| B-14 (Med): Diagnostics is not a library target (triage §4, Mark 14 Nov 2026) | Every target hand-lists its sources; the comments record link hazards. Build-system work, though the code is in an analysis directory | finding B-14 | A source added to Diagnostics must be added by hand to each target | platform refactor (build) |

## 4. Platform code hygiene

| Item | What and why deferred | Evidence | Blocks or risks | Suggested home |
|---|---|---|---|---|
| The settings-fallback idiom: 48 uses (it was 32 in June) | Mechanical duplication in settings reads | 02 "The June review, re-checked" | Copies drift | platform refactor |
| The device key, duplicated | As above | as above | — | platform refactor |
| Markup truth files | Not in the persistence table, whose scope is athlete, session and shot files plus Studio/Rig | G4-REVIEW-1 §5; IM §8 | Files with no repository | platform refactor |

## 5. Not in this list

| What | Where it is |
|---|---|
| Core-scope findings, allowlist lines, open model items and step-brief decisions that no step had scheduled | placed in steps 1–8 of 04 by the triage (`build/refactor/TRIAGE-PROPOSALS.md`, approved by Mark 14 Nov 2026), each marked "(triage, Mark 14 Nov 2026)" |
| Bugs found during the refactor | `build/refactor/TO-FILE.md`, filed as GitHub issues at session end |
| New ideas held by the freeze | GitHub issues labelled for after the refactor (for example "See more" on Your swing and Coaching, in #182) |

## Decisions waiting for Mark

| Decision | Options and context | Source | When |
|---|---|---|---|
| The 23 platform-layer `# unplanned` allowlist lines (sections 1 and 2: 4 A1 and 19 A6, the motion-capture probe among them; `model_browser.h`, the pack and corridor editor, moved to step 3 as core scope, the lead's call) | For each line: fold it into a step, plan it separately, or re-tag it `# kept` | G3-NOTES §5; G3-REVIEW-3 | before the platform refactor |
| The lint-scope calls the lead took in Step G | A6 on every header, not only presenters (and, since the follow-up, slots and signals too); A7 on the Vocabulary and Rules rings only. The `# unplanned` and `# kept` tags themselves | G3-NOTES §2, §4–5 | any time |
| The pre-commit hook | On or off | 03 §14, decision 3 | any time |
| G-05 (High): the per-shot "score" is the wrist resemblance, a red "0" on every non-wrist card | **Not platform**: a design proposal (decision 8 defers it), and its UI half is a design-system change, which needs Mark's approval before it is built. Nearest fit: step 4, which already changes what a card shows | triage §1 "Could not place" | when a design proposal is made |
| D-20 (Low): the cast window hand-draws its Pin button | **Not platform**: a design-system fix (CLAUDE.md: no hand-drawn controls). Nearest fit: step 4 (the cast), with Mark's design-system approval | triage §1 "Could not place" | any time, with approval |
| E-16 (Low): `PpModeStage` hand-draws its tab strip while `PpTabs` exists | **Not platform**: a design-system fix, as D-20. Nearest fit: step 4 (the mode) | triage §1 "Could not place" | any time, with approval |
