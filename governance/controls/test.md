# Controls: 4. Test

Running the tests and checking that the change works. Each control is stated once, in the first stage where it applies; later stages list it by id. A listed id marked "(agents)" binds only agents. The control format and the terms are defined in [`README.md`](../README.md).

**BT-16.** A change to hardware behaviour (cameras, sensors) must be checked on the real devices by the agent or developer making it, with a standalone device probe while the app is closed, after asking the owner each time;
an in-app check also needs the owner's yes first (AG-15).
*Why:* the headless launcher reaches no device, and handing the owner debug builds to try cost repeated round trips.[^om-2]
*Owner:* Product Owner · *Stage:* Test · *Compliance:* none yet

**BT-21.** A second backing or reimplementation of an existing component must have a parity test proving it observationally identical to the original at every observable surface
(counts, formats, byte-exact payloads on the lossless path, the end-to-end consumer's output); a lossy path gets its own tolerance test.
*Why:* the original path is the independent oracle the new one is checked against.[^om-13]
*Owner:* Product Owner · *Stage:* Test · *Compliance:* none yet

**BT-22.** A test run that configured fewer tests than expected must not be called green:
compare the ctest total with the expected count, and read the configure log for SKIPPED.
*Why:* a suite shrinks silently when a dependency is missing (a missing OpenSSL drops the phone-link transport tests).[^om-13]
*Owner:* Product Owner · *Stage:* Test · *Compliance:* none yet

**BT-6.** A test, probe or agent run must not make a sound.
Before a harness runs app QML, check what it creates for audio (sound players, audio outputs) and compile the audio out of the test binary with a silent stand-in; don't just lower a volume. To stop a noisy run, stop the agent first, then its processes; never say a sound has stopped from a process listing.
*Why:* a calibration test rang a chime out of the machine's speakers for forty minutes, through a `pkill`, until a reboot.[^bt-6]
*Owner:* Product Owner (proposed) · *Stage:* Test · *Compliance:* none yet

**BT-3.** Tests must run through ctest (`ctest -R '<a>|<b>' --output-on-failure`), never as bare binaries.
ctest supplies the environment the tests need (for example the diagnostics norm packs). If a whole suite fails at once, or fails on content counts, suspect the harness before the code. Reproducing a failure on a clean tree proves only that your change didn't cause it, not that the test is broken. On Windows, a screen-test binary may be run directly only to read a crash, with ctest's own environment (from `ctest --show-only=json-v1`)[^om-3].
*Why:* two tests run bare failed wholesale and one segfaulted; they were reported as long-standing breakage and both passed first time under ctest.[^bt-3]
*Owner:* Product Owner · *Stage:* Test · *Compliance:* none yet

**BT-4.** The tests a change affects must run once, at the end; the full suite only when the change reaches shared QML or many panels, touches build lists (link closures), or the owner asks.
QML UI tests (`qml_ui`, one ctest test per `tst_*.qml`) run for the screens a change touches, by file (`ctest -R '^qml_ui\.tst_<file>$'`). A full run is two passes, `ctest -LE qml_ui` then `ctest -L qml_ui --no-tests=error`, never one mixed run. Every brief names the exact test files it may run.
*Why:* the full suite is the commit gate, not the working loop; agents re-ran the seven-minute UI binary in their fix loops, "wasting hours".[^bt-4][^d3]
*Owner:* Product Owner · *Stage:* Test · *Compliance:* none yet

**BT-5.** A UI change must be built, rendered or tried, and shown first, saying the tests have not run yet; the tests BT-4 names must then run once the change is accepted and before the commit.
In a round of small fixes, hold the suite and the commit until the list is done. In an unattended run, render, check it yourself, run the tests, and report once with both.
*Why:* a UI fix usually takes a round or two of visual tweaks, and a full suite before each look made every round cost about twenty minutes of waiting.[^bt-5]
*Owner:* Product Owner · *Stage:* Test · *Compliance:* none yet

**BT-7.** A test must find defects, not record today's behaviour: every assertion must be an independent oracle.
Every assertion is an independent oracle: a stated invariant, two producers of one fact agreeing, a write-then-read round trip, or an input the arguments cannot reach. A test written by calling the code and recording its answer is not allowed in. For an untested area, write the tests first (deliberately red), then fix; never weaken an assertion to make it pass.
*Why:* tests written first over the capture and session code found fourteen real defects, one of which discarded an extended session's swings.[^bt-7]
*Owner:* Product Owner · *Stage:* Test · *Compliance:* none yet

**BT-8.** A fixture copied from real data must be re-copied whenever the analysis pipeline changes materially, and what moves must be re-pinned.
A stale fixture understates the model silently while every pin still passes, and a test can pin a fault that exists only because a measurement was broken.
*Why:* the coverage fixture had drifted a month; re-copying it moved coverage from 54 to 63 and removed a pinned firing that came from a broken reading.[^bt-8]
*Owner:* Product Owner (proposed) · *Stage:* Test · *Compliance:* none yet (`live_measure_source_test`'s header states it)

**BT-14.** A change to QML that hosts a 3-D view must pass a render-persistence soak before commit: run windowed (scheduled with the owner, AG-16), grab the app's own views every 30 s for at least six minutes beside a blank control, and check the pixel spread stays high.
*Why:* two unrelated 3-D views went blank in one session and the cause was never confirmed.[^bt-14]
*Owner:* Product Owner · *Stage:* Test · *Compliance:* none yet

**AG-25.** A claim about how the UI behaves must be verified by running it, never by reading QML: an instrumented headless probe, then the same probe with the fix taken out (A/B).
Never tell the owner something is on screen from code or a build check: go to the screen, read the image, and say exactly where it is (which tab), with a review sheet. A phone screen is checked by a device screenshot of the deployed build, A/B. Never capture the whole screen of a machine someone is working on. When the owner reports a fault you can't reproduce, ask for their run environment before theorising.
*Why:* three answers from careful reading were wrong; a feature reported present was one tab away; a whole-screen capture once caught private email.[^ag-25]
*Owner:* Product Owner (proposed) · *Stage:* Test · *Compliance:* none yet

**AG-16.** Agent-driven renders of the app must run under a memory guard (kill above a footprint cap) and an instance cap.
When a change writes anything at app start, point the render's settings at a scratch library, and check that nothing new landed in the live library before saying it is untouched. Windowed renders and soaks that take over the screen are written ready to run and scheduled as one batch with the owner.
*Why:* a runaway mock QML file reached 97 GB and filled swap; a render wrote into the live library and was then reported as untouched; windowed renders kept popping up over the owner's own work.[^ag-16]
*Owner:* Product Owner · *Stage:* Test · *Compliance:* none yet

## The checks

These run under ctest ([BT-3](test.md): always through ctest). A rule with a check names it.

| Test | Checks | Rules written in |
|---|---|---|
| `architecture_lint_test` | A1–A8 (A9 lands with the platform refactor's Stage B) | `information_model.md` §1.4; exceptions in `src/Gui/tests/architecture_allowlist.txt` |
| `design_system_lint_test` | D1–D5 | `docs/design/system/README.md`; exceptions in `src/Gui/tests/design_system_allowlist.txt` |
| `design_system_tokens_check` | `tokens.json` is current. After changing `Theme.qml`, run `python3 tools/theme/make_design_system.py` and commit the regenerated `tokens.json` | `docs/design/system/README.md` |
| `session_setup_lint_test` | W1–W10 | the test's header (`src/Gui/tests/session_setup_lint_test.cpp`); `docs/design/session_wizard_refactor_design.md` §7.5 |
| `qml_reactivity_test` | No binding subscribes to a dependency it doesn't use (a bare revision counter on a line of its own) | the test's comment in `src/Gui/tests/CMakeLists.txt`; [L-1](../guidance/lessons.md) |
| `namespace_collision_test` | One name defined at namespace scope in two files (an ODR violation the linker accepts) | the test's comment; [CODE-2](build.md) |
| `store_isolation_lint_test` | Tests redirect the per-user settings stores before constructing anything that reads them | `docs/developer/testing_developer_guide.md`, "Per-user stores" |
| `core_pack_test`, `diagnostics_catalogue_integrity_test` | Among their checks: no brand names in the diagnostics content | `docs/design/diagnostics_content_extension.md`, "Provenance discipline" |
| `headless_probe_test`, `run_headless_selftest` | The app started with `PINPOINT_HEADLESS_PROBE=1` raises no permission prompt and reaches no device; the launcher refuses a real home, settings file or library | `src/Gui/tests/headless_probe_check.cmake`; `tools/probe/run_headless.sh` |

Rules agreed but not yet checked by a test say "none yet" in their compliance; the checks for G1, G2 and G3 are build items[^po-g].

Coverage: every control in this folder appears in at least one stage or kind (a retired control excepted), and every id links to the file that states it; as a repo doc check it joins #195's step 5 (proposed, for the Product Owner).

**Also at this stage (written elsewhere):** [AG-26](build.md) a watchdog on every long command (agents) · [AD-20](kinds-of-work.md) measure the pose noise floor · [AD-22](kinds-of-work.md) one swing for a hypothesis.

**Guidance for this stage:** [BT-23](../guidance/advice.md) the suite in under 3 minutes.

**Guidance, lessons that bite here:** [L-5](../guidance/lessons.md) offscreen isn't the window · [L-7](../guidance/lessons.md) static scenes stop rendering · [L-8](../guidance/lessons.md) Creator's environment · [L-21](../guidance/lessons.md) sample intermittent failures · [L-22](../guidance/lessons.md) a failed build reruns old binaries · [L-23](../guidance/lessons.md) scaled clocks in setup tests · [L-24](../guidance/lessons.md) pgrep -f matches itself · [L-25](../guidance/lessons.md) shell traps · [L-26](../guidance/lessons.md) macOS permissions and signing · [L-27](../guidance/lessons.md) Neural Engine recompiles · [L-28](../guidance/lessons.md) renders at 100 % from frozen settings · [L-46](../guidance/lessons.md) suites that shrink; Windows test traps · [L-47](../guidance/lessons.md) MSVC Debug is slow · [L-49](../guidance/lessons.md) error dialogs fool liveness checks.

[^om-2]: Product Owner, 25 Nov 2026 (#195, OM-2).
[^om-13]: Product Owner, 25 Nov 2026 (#195, OM-13).
[^bt-6]: Incident 5 Oct 2026. Owning role (proposed): Product Owner.
[^om-3]: Product Owner, 25 Nov 2026 (#195, OM-3).
[^bt-3]: Incident 21 Aug 2026. Owning role (proposed): Product Owner.
[^bt-4]: Product Owner, 28 Oct and 29 Oct 2026; the UI suite split into one ctest test per file, 24 Nov 2026 (platform refactor B0.2b).
[^d3]: Product Owner, 25 Nov 2026 (#195, decision D3): screen tests run for the screens a change touches.
[^bt-5]: Product Owner, 12, 16, 17 and 25 Oct 2026.
[^bt-7]: Product Owner, 7 Sep 2026 (the capture and session test suite).
[^bt-8]: Incident 7 Sep 2026. Owning role (proposed): Product Owner.
[^bt-14]: Product Owner, 18 Aug 2026; kept without its setup-wizard clause, 25 Nov 2026 (#195, decision D14).
[^ag-25]: Incidents Aug 2026, 2 Oct and 6 Nov 2026. Owning role (proposed): Product Owner.
[^ag-16]: Product Owner, 26 Oct 2026 (windowed renders); incidents 11 Oct and 6 Nov 2026.
[^po-g]: Product Owner, 25 Nov 2026 (#195).
