# PinPoint Studio: the four contexts (an amendment to IM §2)

**Status:** **ruled, 24 Nov 2026**: an amendment to `docs/developer/information_model.md` (IM) §2, approved by the Solution Architect and the Product Owner[^po24-contexts]. One wording call is left for the Design Lead (AM11). The text merges into IM §2, §4, §4.1 and the glossary, and this page is then retired. Two outside reviews (24 Nov) are folded in, and where this page departs from them, it says why.

## Why this matters

IM §2 already says PinPoint is a chain of **contexts**: what the product does, in the order data flows. It also says that "contexts and rings are different cuts" (DEC-48). A context describes how data flows; a ring describes which code may depend on which.

The domain refactor of 14–24 Nov made the second cut real. Each fact now has one owner, and the rings are enforced by the lint. That is what lets a new context sit at the end of the chain without reaching back into the others: Coaching can read the diagnosis without re-deriving it, because there is now one diagnosis, with one owner, to read.

## What IM §2 says today

Three contexts: **Acquisition → Analysis → Domain + Application**, joined by two contracts (the Recording, and the results).
- Acquisition and Analysis "own nothing persistent".
- Domain + Application "owns and stores everything" and hands Analysis its context: athlete facts, rig state, session usuals and norms.
- Analysis's cross-shot reductions are listed as "session and athlete diagnostics, diagnosis, coaching". There, "coaching" means today's rules that turn findings into outcomes, tendencies and themes (`src/Coaching`).

## The amendment

**AM1 · Four contexts.** Analysis is renamed **Diagnostics** (analysis and diagnosis). **Coaching** is added after it.

```
 ┌──────────────┐ Recording ┌───────────────────┐ results ┌───────────────────┐ plans,  ┌──────────────────────────┐
 │ ACQUISITION  │ ────────▶ │ DIAGNOSTICS       │ ──────▶ │ COACHING          │ ──────▶ │ DOMAIN + APPLICATION     │
 │ transports,  │           │ analysis graph,   │ (find-  │ what to work on,  │ evidence│ Athlete · Session · Shot │
 │ devices,     │           │ firing findings,  │  ings,  │ how, how much to  │         │ · Studio/Rig · the       │
 │ ring buffer, │           │ cross-shot        │  diag-  │ tell, whether it  │ ◀────── │ coaching objects ·       │
 │ connectors,  │           │ reductions and    │  nosis) │ stuck: the work-  │ context │ persistence · AppState · │
 │ calibration  │           │ the derived facts │         │ on lifecycle, the │         │ presentation             │
 │ procedures   │           │                   │         │ feedback policies │         │ owns and stores          │
 └──────────────┘           └───────────────────┘         └───────────────────┘         │ everything               │
                                                                                         └──────────────────────────┘
        Platform (IM §4, ring 0) is the base every context uses. It is not a context: it holds nothing domain-shaped.
```

**AM2 · Coaching owns nothing persistent,** like Acquisition and Diagnostics. Its stored objects are domain objects, owned at the athlete and session levels and keyed by the owners' ids: the golfer model, the work-on lifecycle's state, the playbook and the practice log. Each is an IM change of its own (C1, C3), made in the coaching track. Facts about one shot or one session stay on their one owner, as a part with its own writer, as the shot's `review` and `derived` blocks are today.

**AM3 · Direction** (a new rule, C11; proposed id R-CTX-1). **Data flows left to right; code dependencies point right to left.**
- A later context may depend on an earlier context's contracts and rules. An earlier context never depends on a later one.
- Coaching reads Diagnostics' results and never re-derives them. Diagnostics knows nothing of coaching. Acquisition knows nothing of either.
- Context flows back only as typed input from Domain + Application, as §2 already says for Analysis. A context that must learn of something later in the chain subscribes to the domain's events (§7.4); it never names a later context.
- **No acquisition or diagnostics code branches on a coaching modality** (lesson, practice, play, fire-fighting, review). Modalities change only feedback, Coaching's own reductions, and what is shown (`coaching_layer.md` T5, and "analysis is session-agnostic").

**AM4 · The results contract.** Diagnostics → Coaching is a contract, as the Recording is for Acquisition → Diagnostics. Coaching reads, through the owners:
- a shot's derived data and findings (OBJ-SHOT-DERIVED, OBJ-SHOT-RESULTS);
- the session's diagnosis and summary (OBJ-SESSION-DIAG, OBJ-SESSION-SUMMARY);
- the session's series of measurements (OBJ-SESSION-SERIES);
- the athlete's diagnosis (OBJ-ATHLETE-DIAG).

It never reads raw streams and never runs an analysis stage.

**AM5 · Today's `src/Coaching` moves to `src/Diagnostics/derived/`, and `src/Coaching` becomes the Coaching context's folder.** Today's `src/Coaching` holds rules that feed findings or derive from them: the shot's derived data (outcomes, factors, strike from flight; M-SHOT-LAYERS layer 3) and the cross-shot reductions (tendencies, consistency, spread, session results). They belong to Diagnostics: `fire_findings.h` includes `diagnostic_ledger.h`, `shot_factors.h` and `strike_from_flight.h`, so under AM3 they cannot sit in a later context. The subfolder's name, `derived`, is IM §7's own word ("Derived facts", at the shot, session and athlete levels). The move is a scripted move at Stage B, Part 1.

**Interpretation is not a Diagnostics folder name.** Interpreting a result against a golfer's stated intent and goals (the play debrief, decision quality against outcome, a work-on's verdict, the range-to-course gap) is Coaching's, and it will live in `src/Coaching`. A Diagnostics folder called "interpretation" would invite those rules into the wrong context. IM §4's Rules row reads "derived facts (outcomes, factors, flight check, Sky, tendencies, link grading)" in place of "coaching (…)" (C9). The drills (AM7) are the Coaching context's first rules.

**AM6 · Contexts are tagged, not foldered.** §4.1 gains a context column: each RING-* row says which context each directory serves. The Vocabulary areas keep the subject names ruled on 24 Nov (Q15: `Metrics`, `Recording`, `Identity`, `LaunchMonitor`, `Buffer`, `Club`, `Common`, and the existing `Analysis`); `Vocabulary/Coaching` is added when coaching types arrive. The lint gains a check that no context includes a later one (proposed lint A9, C9). It lands with Stage B's folder moves, with no allowlist line added. That holds because the capture code in `Gui` that includes Diagnostics headers today stays tagged Domain + Application until Stage D splits capture: `Gui/cameras` (`camera_instance` includes the pose estimator and the ball detector, for live triggering), `Gui/imu` and `Gui/shot` (`shot_processor` and `capture_context` include `Analysis`, `Club` and `Timeline`). Stage D decides how live pose and ball triggering sit across the boundary. Only the pure device directories are tagged Acquisition at first (`Video`, `Buffer`, `Ppcp`, and `LaunchMonitor`'s connectors once its inferred reads leave in Stage B), and they include no Diagnostics header today.

**AM7 · The packs.** One loader, `RuleData` (R-CON-4's immutable snapshots, in Domain + Application), loads the packs for every context.
- **Screens** (`src/Diagnostics/packs/screen_pack.*`) stay in Diagnostics. A screen answer is an input to diagnosis (R-INPUT-3): it says whether a fault is a capacity limit.
- **Drills** (`src/Diagnostics/packs/drill_pack.*`) are Coaching's intervention library. Today a diagnostics condition names its drill, and the health check (`src/Diagnostics/engine/diagnostics_health.*`) checks that the drill exists. Under AM3 that is a Diagnostics rule naming a Coaching fact. So the link from a condition to its drill moves to Coaching, and the cross-pack health check moves to where both packs are visible: `RuleData`'s level. Diagnostics then says what is wrong, and Coaching says what to do about it. This changes the packs' format, which is the Solution Architect's to approve, and the pack editor, which is the Design Lead's.

**AM8 · Starting points already in IM,** so the coaching track builds on them rather than beside them (the risk `coaching_layer.md` names):
- the session's declared focus, the declared work-on (OBJ-SESSION-INTENT, FILE-SESS-02; R-INPUT-3; GL-68), is Coaching's starting point;
- the declared miss and the screen answers in the same object stay Diagnostics' inputs.

`work_ons.json` (FILE-SESS-05) is **not** coaching state, despite its name: it holds the session summary (OBJ-SESSION-SUMMARY), a Diagnostics product.

**AM9 · Intent and cold tests at the boundary** (`coaching_layer.md` T2, T3).
- **Intent.** A golfer's stated intent is a golfer fact, owned by the domain (R-INPUT-1, R-INPUT-2).
  - Diagnostics may summarise it, as it already does (R-INPUT-4 and R-INPUT-5, the intent-consistency summaries), but it never excuses, suppresses or re-grades a finding because of it.
  - Judging a shot against its stated intent (the play debrief, decision review) is Coaching's.
- **Cold tests.** A cold retest is a labelled Coaching test over shots that still count in full in every Diagnostics reduction. There are no warm-up shots.

**AM11 · Priorities and work-ons: one word, two meanings, split.** Today the session's diagnosis produces "work-ons": its ranked, classed faults (`src/Diagnostics/engine/session_reduction.cpp` includes `work_ons_pack.h`; `condition_role.h`, `session_results.h`, `swing_areas.h`, `session_report.h`, `AthleteDiagnostics` and the session index summary include `work_ons.h` for `WorkOnClass`). `coaching_layer.md` uses "work-on" for a lifecycle: adopted, practised, retested, retained. So:
- **Diagnostics produces *priorities*:** the ranked, classed faults that `session_reduction` computes today. The types are renamed (for example `WorkOnClass` → `PriorityClass`) and move to `src/Diagnostics/derived/` with AM5.
- **Coaching owns *work-ons*:** a priority that the golfer or the coach adopts, with its lifecycle. The session's declared focus, "the declared work-on" (GL-68, OBJ-SESSION-INTENT; AM8), is already one.
- **The golfer keeps the word "work-on".** The file `work_ons.json` (FILE-SESS-05, the session summary) keeps its name: renaming a file on disk is a format change, and nothing needs it. The IM glossary gains "priority" and narrows "work-on".
- **For the Design Lead:** until the coaching track builds adoption, today's panel shows priorities under its "Work-ons" heading. Whether it says "work-on" before anything is adopted, or "priorities", is a wording call.

**AM10 · The word.** These are contexts, not layers. In IM, "layer" already means the shot's layers (§2.4, M-SHOT-LAYERS: acquired, analysed, derived, assessed, findings).

## Each context, and the IM rules that already govern it

| Context | Holds | Already ruled in IM |
|---|---|---|
| **Acquisition** | transports, devices, the ring buffer, connectors, calibration procedures, and the signal-level sensor maths (L2); produces the Recording | §2 (the context); §2.2 the Recording contract (OBJ-RECORDING, CLS-RECORDING); §2.3 Studio/Rig (CLS-RIG); R-REC-1…R-REC-8 (capture writes the swing file once, in stages); R-INS-1 and R-INS-2 (readings are written by capture); M-SHOT-LAYERS layer 1 |
| **Diagnostics** | the analysis graph, firing findings, the derived facts (outcomes, factors, tendencies, priorities), the session and athlete diagnosis | §2.1 (its three shapes); R-ANA-* (stale is known, re-analysis reproduces live); R-DER-* (the pipeline never fires findings); R-STAMP-* (stamps); R-INS-3 and R-INS-5 (rank reported metrics, estimate derived ones); R-INPUT-4 and R-INPUT-5 (intent summaries); §7 derived facts; OBJ-SHOT-DIAG, OBJ-SESSION-DIAG, OBJ-ATHLETE-DIAG and their classes; CLS-PACKS-NORMS; M-SHOT-LAYERS layers 2–5 |
| **Coaching** | what the golfer works on, how, how much they are told, and whether it stuck (`docs/design/coaching_layer.md`) | **no rules yet.** Its starting points are named in AM8. Its objects, files, events and rules are IM changes made in the coaching track, against this amendment |
| **Domain + Application** | the owners, persistence, `RuleData`, `AppState`, presentation | P2, P3, P4, P10 and P11; §5 objects and LC-* lifecycles; §6 `AppState` and R-APP-*; §8 persistence and R-PERS-*; §9 R-CON-* and R-QML-*; §10 the ownership questions |
| **Platform** (the base, not a context) | logging, profiling, machine queries, the worker pool, atomic writes, locks, paths | §4's Platform row; R-CON-5; R-PERS-2 |

## What it changes outside IM

- **The platform refactor** (`docs/implementation/platform_refactor_assessment.md`):
  - **Stage B's libraries stay coarse:** by ring, as revision 3 has them. A library splits by context only where a clean seam already exists, such as the IMU's signal maths against its golf fusion (L2). The lint (A9) enforces the context boundary; the libraries don't have to. Cutting libraries by context and ring now would multiply MSVC targets, Stage B's largest linking risk, for a Coaching context with almost no code.
  - The move of today's `src/Coaching` into `src/Diagnostics/derived/` (AM5), with the priorities' type renames (AM11), joins Stage B, Part 1's scripted move.
  - Stage D's capture seam is Acquisition's boundary with Domain + Application.
- **Three guards for Stages C and D,** so that they don't close doors the coaching track needs (constraints on the stages' designs, not new work in them):
  - **A third kind of Recording.** External video uploads (`coaching_layer.md` ASY-R1) will be a Recording with declared capabilities and no rig. So the shot kind (`shotKind`, today `captured` or `deviceOnly`; R-REC-8) and Stage C's rig reference must not assume two values.
  - **Not every capture is a shot.** Rehearsal swings with no ball (PRA-R8) are an Acquisition event stream, so Stage D's capture interfaces must not assume that every trigger is a ball impact.
  - **A play source is an Acquisition adapter.** The course context that a play source supplies (WMP-R2's `PlaySource`) is one, beside the launch-monitor connectors.
- **The coaching layer** (`coaching_layer.md`): "the coaching layer" becomes "the Coaching context" in its text. Its note that a new ring is not needed stands.
- **Governance:** this page is on #195's list. Once ruled, its text merges into IM §2, §4 and §4.1, and the page is retired.

## Rulings (24 Nov 2026)

All ruled as written[^po24-contexts]:
- **L1.** AM1–AM4: four contexts, Analysis renamed Diagnostics, the direction rule R-CTX-1 with its no-branching-on-modality clause, and the results contract.
- **L2.** The IMU's signal-level orientation and calibration maths (`imu_calibration.h`, `trunk_angles.h`, `trunk_stand_tall.h`) moves to Acquisition's rules; golf-specific fusion and kinematics stay in Diagnostics.
- **L3.** AM2 and AM8: Coaching owns nothing persistent; its objects are domain objects keyed by the owners' ids; the declared focus is its starting point.
- **L4.** AM5 and AM11: today's `src/Coaching` moves to `src/Diagnostics/derived/`; `src/Coaching` becomes the Coaching context's folder; Diagnostics' priorities are split from Coaching's work-ons.
- **L5.** AM6: the context column, the Vocabulary areas kept as ruled, lint A9, and the capture code in `Gui` tagged Domain + Application until Stage D.
- **L6.** Stage B's libraries kept coarse, split by context only at a clean seam.
- **L7.** AM7: drills to Coaching, screens staying in Diagnostics, the condition-to-drill link and the cross-pack health check moving up.
- **L8.** AM9: stated intent summarised but never used against a finding by Diagnostics; cold tests as labelled Coaching tests.

**Open:** the Design Lead's wording call in AM11.

[^po24-contexts]: The Product Owner, 24 Nov 2026: the product is Platform, Acquisition, Diagnostics and Coaching, with Coaching new and last. The Product Owner leans to keeping facts about one shot or session on their one owner, and to coaching's own objects being separate objects keyed by the owners' ids. On 24 Nov the Product Owner and the Solution Architect approved L1–L8 as written, L4 in its revised form (AM5 and AM11), worked out with an outside review.
