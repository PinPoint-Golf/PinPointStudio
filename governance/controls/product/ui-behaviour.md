# Product controls: screens

What screens must show and how they must behave, beside the design system (`governance/artefacts/design_system/README.md` and its component pages). How a UI change is approved and reviewed is in [`decide.md`](../decide.md) (UI-18, UI-20) and [`review.md`](../review.md) (UI-17). Each control is stated once, in the first stage where it applies; later stages list it by id. A listed id marked "(agents)" binds only agents. The control format and the terms are defined in [`README.md`](../../README.md).

## Already stated elsewhere

- A catalogue metric always draws in its own identity colour, `Theme.metricColor(key)`; colour is never the only channel (`governance/artefacts/design_system/README.md`, "Colour", "The non-negotiables").
- Every box plot in the app is the one box-plot component, and shows its values on the plot while it is examined (`governance/artefacts/design_system/components/PpBoxPlot.md`).
- The registers, and a condition's detail opening in place (`governance/artefacts/design_system/typeset.md`, "The registers"; `docs/design/stage_typeset_playbook.md`).

## What a reading shows

**UI-1.** Every reading we have must be shown, with its warning; none may be hidden.
A reading is shown with its number and its comparison (band, norm, history), with a warning sign and short honest words where it may be unreliable, and the full reason said once in the trust fold. Never withhold a number, a band or a comparison because it might be wrong, and never design a feature that produces nothing to stay safe. When tempted to gate a display on trust, gate the warning instead. Warnings are debts to pay down, not reasons to hide. Grading (whether a condition fires) is a separate judgement.
*Why:* "an app that produces nothing is worthless"; the user is an intelligent adult, and a hidden reading is a forgotten debt where a warning on screen can't be forgotten.[^ui-1]
*Owner:* Product Owner · *Stage:* Build; checked at Review · *Compliance:* a review

**UI-2.** A reading we don't have must show "-".
A 0 where a metric is missing is a bug in that one metric, fixed to show "-"; no codebase-wide sweep.
*Why:* the app already shows "-" for unavailable metrics, and that is the wanted marker.[^ui-2]
*Owner:* Product Owner · *Stage:* Build; checked at Review · *Compliance:* a review

**UI-3.** A displayed reading must never be rounded to its uncertainty: print whole units, and show σ only as the ± beside the number, on every surface (cards, tooltips, tiles, exports).
*Why:* coarse rounding made the estimates look too rough, and σ already says how far to trust them.[^ui-3]
*Owner:* Design Lead · *Stage:* Build; checked at Review · *Compliance:* a review

**UI-4.** A chart page must run: headline and key figures, the plot, "How far can I trust this?" directly under it, then Controls, Summary and the domain analysis, each a fold.
Trust is said once: the headline names the story; inside the chart a reading with no value is a plain "-", and a reading with a value and a gate keeps its value with the warning sign and the gate's short words. Every reading is on its own Readings tab.
*Why:* novices read the numbers before provenance and stop at the plot; each fact is said once.[^ui-4] The trust fold itself is `docs/design/trust_and_gates_design.md` §7.4.
*Owner:* Design Lead · *Stage:* Build; checked at Review · *Compliance:* a review

**UI-5.** A hover on a chart mark must label the marks' own values at their positions on the plot, never with an explanatory sentence; what a mark means belongs in the key or the trust fold.
The presenter formats the numbers; QML places them, dropping lower-priority labels on collision.
*Why:* the reader reads the plot, not prose about it.[^ui-5]
*Owner:* Design Lead · *Stage:* Build; checked at Review · *Compliance:* a review

**UI-6.** A tile must read at a glance: a number per cell, no sentences.
*Why:* a tile of sentences, timing windows and a cohort paragraph was "way too wordy and almost unreadable".[^ui-6]
*Owner:* Design Lead · *Stage:* Build; checked at Review · *Compliance:* a review

**UI-7.** An angle a user reads at a glance (a lie angle off a tilted club head) must never be magnified; magnification is for path and spin only.
A spread ellipse encloses every shot of its group, is drawn only for three shots or more, never without its dots, and as an outline only.
*Why:* a ×3 gain drew 2.6° of lie as 7.8°, which would alarm a golfer even with the ×3 explained.[^ui-7] Partly stated in `docs/design/lm_panel_fsx.md`.
*Owner:* Design Lead · *Stage:* Build; checked at Review · *Compliance:* a review

## Where things appear

**UI-8.** A new reading or summary on Session diagnostics must go in every register (Analyse, Replay, Print), and on Charts in both registers, unless a role explicitly excludes one.
If unsure, ask in planning; probe each register.
*Why:* a feature missing from the register the reviewer is in reads as not built.[^ui-8]
*Owner:* Design Lead · *Stage:* Build; checked at Review · *Compliance:* a review

**UI-9.** Every stage panel where it makes sense must have a "This shot" view and a "Session" view, as quiet tabs.
Replay opens on This shot, Analyse offers both, print carries both. Put it in the brief up front.
*Why:* "what did this swing do" and "how repeatable was I" are different questions.[^ui-9]
*Owner:* Design Lead · *Stage:* Build; checked at Review · *Compliance:* a review

**UI-10.** The app must have no menu bar items (beyond the platform's About and Quit) and no native OS dialogs: every action is an in-screen control on the screen where its result appears.
A phone's sessions are never imported from files: the connected device offers them in an in-app list.
*Why:* all options are surfaced in the app.[^ui-10]
*Owner:* Design Lead · *Stage:* Build; checked at Review · *Compliance:* a review

**UI-22.** A toggle for what a view shows (an overlay) must live in the View menu, stored per mode, not in the device panels,
which are for managing devices.
*Why:* display choices belong with the view they change.[^om-13]
*Owner:* Product Owner · *Stage:* Build; checked at Review · *Compliance:* a review

**UI-11.** Keyboard shortcuts must not be added to individual panels or mocks: keyboard use is an accessibility question for the whole app, to be designed once.
*Why:* it needs one consistent design, not bindings added panel by panel.[^ui-11][^d10] Keyboard shortcuts are a feature: issue #83.
*Owner:* Design Lead · *Stage:* Build; checked at Review · *Compliance:* a review

**UI-21.** "Delete" must be used only for permanent, irreversible removal; anything recoverable says "Move to trash"
(and its toast "moved to trash"): choose the word by first asking whether the action can be undone.
*Why:* the word must tell the golfer whether they can get it back.[^om-13]
*Owner:* Product Owner · *Stage:* Build; checked at Review · *Compliance:* a review

## Layout and simplicity

**UI-19.** The overlap of the review screen's left stage (video) and right stage (chart panel) must be treated as deliberate, not flagged as a defect; when judging a chart's legibility, faint strokes vanish first over the video.
*Why:* shrinking either stage costs more than the overlap does.[^ui-19]
*Owner:* Design Lead · *Stage:* Build; checked at Review · *Compliance:* a review

**UI-12.** Smaller windows must degrade gracefully, with every control visible and usable and nothing overlapping or off the window; the app is designed for full screen; wrapping or stacking is fine.
Don't add logic to make narrow layouts as tidy as full screen, and take the simpler of two acceptable narrow arrangements without asking. Under about 440 px is not a case.
*Why:* the effort goes on the full-screen layout; a narrow window is the user's choice.[^ui-12]
*Owner:* Design Lead · *Stage:* Build; checked at Review · *Compliance:* a review

**UI-13.** A simplification must quieten the screen: list the sources of noise (frames, repeated labels, chips, colours, vertical space) and remove or hover-gate each by name.
Never ship a silently disabled control: keep it pressable and say why it is empty.
*Why:* a first pass only moved captions and disabled a tab, which read as broken.[^ui-13]
*Owner:* Design Lead · *Stage:* Build; checked at Review · *Compliance:* a review

## Routines the golfer follows

**UI-14.** A sensor calibration routine must run hands-free: it starts, and retries, only when the sensors are on and still (stillness from the device gyro), never on page entry, and says which sensor it is waiting for.
A step where the golfer faces away from the screen is taught before it, facing the screen, started and ended by the sensors, never a timed window of on-screen text, and its verdict shows when the golfer faces the screen again. A routine isn't done until it has had a real-sensor run; check a sensor-axis claim against a live reading before building a gate on it.
*Why:* the golfer holds or wears the sensors and can't click; the first real run "needed three hands"; headless tests never caught it.[^ui-14]
*Owner:* Product Owner · *Stage:* Build; checked at Review · *Compliance:* a review

**UI-15.** Every calibration and check avatar must be face-on, in every routine; direction is carried by golf-swing wording (DC-9), not by changing the view.
*Why:* consistency across the routines matters more than solving mirroring visually.[^ui-15]
*Owner:* Design Lead · *Stage:* Build; checked at Review · *Compliance:* a review

**UI-16.** Spoken output must be only an instruction the golfer must act on and can't read at that moment; never verdicts, statuses, readings or chatter.
Reuse the existing speech controller; a new spoken prompt sits behind an experimental setting, off by default, until promoted. A step done facing away gets a spoken start cue and a spoken return cue, fired when the movement is complete, not at its onset.
*Why:* anything else spoken is noise competing with the coach; a cue at the turn's onset was useless.[^ui-16]
*Owner:* Coaching Lead · *Stage:* Build; checked at Review · *Compliance:* a review

[^ui-1]: Product Owner, 22 Oct 2026 ("a big philosophical principle for the entire product"); it replaces the earlier "nothing is presented until it is earned".
[^ui-2]: Product Owner, 28 Sep 2026.
[^ui-3]: Design Lead, 3 Oct 2026.
[^ui-4]: Design Lead, 17 Oct 2026.
[^ui-5]: Design Lead, 18 Oct 2026.
[^ui-6]: Design Lead, 2 Oct 2026 (the kinematic-sequence tile).
[^ui-7]: Design Lead, 12–17 Oct 2026 (the launch monitor panel).
[^ui-8]: Design Lead, 16 Oct 2026.
[^ui-9]: Design Lead, 11 Oct 2026.
[^ui-10]: Design Lead, 22 Aug 2026.
[^om-13]: Product Owner, 25 Nov 2026 (#195, OM-13).
[^ui-11]: Design Lead, 12 Oct 2026.
[^d10]: Design Lead, 25 Nov 2026 (#195, decision D10): shortcuts go with the accessibility work on #83; `PpRowAction.md`'s tooltip-shortcut line is corrected at the design-system move.
[^ui-19]: Design Lead, 21 Sep 2026; confirmed 25 Nov 2026 (#195, decision D14).
[^ui-12]: Design Lead, 11 Nov 2026.
[^ui-13]: Design Lead, 2 Oct 2026.
[^ui-14]: Product Owner, 26 Oct 2026 (the first real-sensor runs of the trunk routine).
[^ui-15]: Design Lead, 27 Oct 2026.
[^ui-16]: Coaching Lead, 27 Oct 2026.
