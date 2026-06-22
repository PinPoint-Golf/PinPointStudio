# Pinpoint — The Lesson Model

**Version:** 0.3 (draft)
**Status:** Design paradigm — not yet implemented. Supersedes the flat "session" framing in [`pinpoint-ux-design.md`](pinpoint-ux-design.md).
**Companion to:** [`personas.md`](personas.md) — every decision here traces to TP / EA / WN.

---

## What this document is

This is the load-bearing reframe of the product. It defines what Pinpoint *is* at the
level of identity, and gives the heuristics that every future screen must be checked
against. Where `personas.md` answers *who we build for*, this answers *what we are building*.

Read it before designing any session, capture, review, or coach surface. If a proposed
screen pulls the product back toward "a dashboard you read," it is wrong — check it here.

---

## 1. The thesis

> **Pinpoint does not record sessions. It gives lessons.**

The incumbents — Trackman Performance Studio, Swing Catalyst, GCQuad — are very
expensive **mirrors**. They reflect what a swing did at maximum fidelity and hand the
interpretation to an expert in the room. Their unit of work is the *capture*; their
surface is a *dashboard*; their stance is *retrospective*. Building the same thing makes
us a worse Trackman, competing on accuracy and brand we cannot win.

Our one asset they cannot copy is an always-present, language-fluent **coach** that sees
the full kinematic chain. So we change what the software *is*:

- A mirror shows you everything. A **coach** watches, decides what matters now,
  prescribes one change, and watches your next rep.
- **A session ends. A lesson concludes.** A session is a neutral container (start,
  capture, stop). A lesson is a narrative with intent and an outcome — it is *about*
  something, it goes somewhere, and it leaves a takeaway.

This is the line that separates us from the incumbents at the level of identity, not
features. Nobody else gives a lesson.

---

## 2. The object model

```
Athlete  →  Lesson  →  Block  →  Shot
```

Two of these are new or reframed; the bottom two are unchanged.

- **Athlete** — unchanged. The person. Their history is now a *coaching journey* (§8).
- **Lesson** — the reframed "session." A bounded coaching episode with an intent (maybe),
  a structure (blocks), and a **conclusion** (a takeaway). This is the unit the user
  begins and wraps up.
- **Block** — *new*. A group of shots sharing one intent. The unit of **assessment and
  coaching**. Has a *role* (§5). This is the layer that was missing: diagnosis comes from
  the group and its spread, not from a single swing.
- **Shot** — unchanged. The per-swing evidence (clip, traces, metrics). Stays exactly as
  built. It is now the *drill-down*, not the top of the hierarchy.

Underneath, nothing about capture changes — a lesson is still journalled as
blocks/shots/captures through the existing EventBuffer and analysis pipeline. "Lesson" is
the human-facing frame over infrastructure that already exists.

---

## 3. Declared or discovered

A lesson does not demand structure at the door. It forms one of two ways:

- **Declared** — the user brings a thesis: "Today, low point." (TP; a focused EA.)
- **Discovered** — the user just hits balls; the coach watches, finds the pattern, and
  **shapes it into a lesson with a takeaway at the end.** (WN; casual EA.)

This is the nuance that keeps "everything is a lesson" from feeling like homework. Let the
lesson **crystallize retrospectively** when no goal was brought. One choice serves the pro
who arrives with a plan and the beginner who only wants to hit balls — without imposing
pedagogy on either.

---

## 4. The lesson lifecycle

A session is binary (recording / not). A lesson has an arc:

```
Open ──────────► In progress ──────────► Wrap-up ──────────► Concluded
(intent,         (blocks accumulate:     (coach            (a document with a
 optional)        assess / drill /        synthesizes)       takeaway, threaded
                  re-test)                                   to the next lesson)
```

The new moment that does not exist today is **Wrap-up**. Ending a lesson *produces
something* — the takeaway — instead of merely stopping a recording. "End session" becomes
**"Wrap up."** It is the smallest surface that proves the whole reframe, and the first
thing to build (§13).

---

## 5. Blocks and their roles

The unit of coaching is the **group**, not the shot. One swing lies; a block tells the
truth. A block carries a **role**, and the role determines the view — because assessing,
improving, and confirming are different cognitive tasks and must not share one dashboard.

| Role | Intent | The view it gets |
|------|--------|------------------|
| **Assess** | "Hit five normal." | **Dispersion strip** — each shot's key metric with mean + spread; the pattern named in plain language. |
| **Drill** | "Belt to the target — hit seven." | **Target-progress trace** — the one worked metric, shot by shot, goal zone shaded, so you watch it start to stick. A **felt cue** is pinned at the top. |
| **Re-test** | "Hit four normal again." | **Held-up read** — did the change survive when attention moved off it. |
| **Free** | warm-up / play | no imposed structure; shots accrue, a lesson may be discovered from them. |

**Dispersion is first-class.** A single shot gives a *value*; a block gives a value **and
its spread**. Golf improvement is largely spread reduction, and no incumbent surfaces
body-kinematic dispersion well. The assess headline is not "low point 2 cm back" — it is
**"2 cm back, ±1 cm, *consistently*."** Consistency is the read.

**A read may name one thing — or one relationship.** Some faults only make sense as a
pair: swing path and face-to-path, or pelvis and thorax separation (X-factor). The block
read carries the relationship and still resolves to one plain-language sentence. This is
selectivity (§10), not a metric dump — and the surface renders it with one generic
component, not a bespoke screen per pair (§15).

Blocks are **emergent, not a wizard.** Default: the coach proposes the boundary as part of
the conversation ("let me see five normal" opens an Assess block). The system may also
*auto-segment* on intent shifts (club change, a pause) and offer a boundary to confirm. A
lightweight manual control ("Mark baseline / Start drill / Re-test") exists for the
self-coached EA. Never force a lesson plan up front.

---

## 6. Coach is the frame, not a mode

Today Coach is the fourth sibling beside Swing / Wrist / GRF — a place you navigate to.
Under the lesson model, **coaching is the narrator of every lesson, not a destination.**

Swing, Wrist, and Ground Forces demote from peer "modes" to **lenses** brought into a
lesson. You do not start a "Wrist session" — you give a lesson and pull up the wrist lens
when it is relevant. This collapses the top-level architecture (four parallel modes → one
lesson + selectable lenses) and is the cleanest expression of "coach, not mirror."

**The coach can be silent.** TP *is* the coach; the system documents her lesson and
produces the take-home. Whether the AI narrates or stays quiet is a setting — it never
forks the product, and it never overrides her authority.

Concretely, the coach becomes the right-hand **coach stream** (§7), and the Swing / Wrist /
GRF lenses live in the per-shot **deep dive** (§15).

---

## 7. The surface: a swing, a coach stream, a record

The surface does **not** dissolve into a single scroll, and it does **not** stay a fixed
metrics dashboard. It folds onto the layout the app already has — **cameras on the left, a
content chooser on the right** — and changes what the right-hand side is *for*:

- **Cameras stay on the left, unchanged** — live, dual-angle, with pose and club-path
  overlay. The swing is the evidence and keeps its real estate.
- **The right-hand chooser becomes the coach stream** — a vertical, chaptered feed of the
  coach *getting involved* (read · prescription · progress), plain-language, newest entry
  live at the foot. This is where the four-mode chooser used to be: coaching replaces
  "pick a panel."
- **The carousel stays along the bottom** — the stream of recorded blocks and shots.
  Structure crystallizes *here*, as a record of what happened.
- **The full instrument demotes to a per-shot deep dive** — the EA taps a shot and the old
  content fills the screen for that one swing, with Swing / Wrist / GRF as **lenses**
  (§6, §15). "Show me everything" is allowed to be a mirror precisely because it is a
  deliberate drill-down, not the default.

The coach stream, chaptered (a zoom-in on the right pane):

```
┌ ASSESS · Driver · 6 ──────────────────────┐
│ "Early low point, ±1 cm — consistent."    │
│ ▸ dispersion strip   ▸ tap a shot → clip  │
├ DRILL · "belt to target" · 7 ─────────────┤
│ 📌 FEEL: belt buckle at the target         │
│ ▸ target-progress  ●  ●   ●●● starting to │
├ RE-TEST · Driver · 4 ─────────────────────┤
│ "Held up — +0.6 cm forward, 3 of 4."      │
├ WRAP-UP ──────────────────────────────────┤
│ Worked: low point · ±1 cm back → +0.6 fwd │
│ Take home: [drill] + [one feel]           │
│ Next lesson: re-test, then face control   │
└───────────────────────────────────────────┘
```

And the whole screen — cameras, stream, carousel, and the deep dive it opens:

```
┌ Lesson · Sam · 7-iron      Coach · prescribing            [ Wrap up ] ┐
├─────────────────────────────────────────────┬────────────────────────┤
│  CAMERAS — left, unchanged                   │  COACH STREAM — right   │
│  ┌───────────────────────┐  ┌─────────┐      │  Assess   start line →  │
│  │ down-the-line+overlay │  │ face-on │      │  Read     path × face,  │
│  │ pose · club path      │  └─────────┘      │           starts right  │
│  └───────────────────────┘                   │  Prescribe ▸ feel·drill │
│  ▷ impact · shot 3         [ deep dive ]      │  ▸ talk to coach  on    │
├──────────────────────────────────────────────┴────────────────────────┤
│ CAROUSEL  [ Assess · 7i · ▣▣▣▣▣ ]   mark·drill·re-test    + hit to go   │
└────────────────────────────────────────────────────────────────────────┘
        tap a shot ──► DEEP DIVE   [ Swing | Wrist | GRF ]   full metrics
```

**The coach stream is the lesson arc — without a wizard.** The stages — *scope · assess ·
read · prescribe · practice · re-test · wrap-up* — are not steps you click through; they
are the shapes the **focus** at the head of the stream takes as the coach's attention
moves. There is no "step 3 of 7, Next." The only forward action is **hitting another ball
or talking to the coach**; structure appears *behind* you in the carousel as blocks get
recorded. **Progress lives in the rear-view, not the windshield** — which is exactly what
lets a tightly-run lesson still feel *discovered* (§3) rather than marched.

| Stage | What the focus shows | What the carousel does |
|-------|----------------------|------------------------|
| **Scope** | intent — or "just hit, I'll find it" | empty; *hit to begin* |
| **Assess** | dispersion forming; no verdict yet | the Assess block accrues |
| **Read** | the pattern named — one thing **or one relationship** (path × face; pelvis × thorax) | Assess block completes |
| **Prescribe** | one feel, one drill, one target | a Drill block opens |
| **Practice** | target-progress: in-zone · trend · spread | the Drill block accrues |
| **Re-test** | held-up read (optional, quick) | a Re-test block |
| **Wrap-up** | the takeaway → seeds Practice Mode (§14) | the lesson, collapsed to a record |

Tapping a block in the carousel rewinds the focus to that block's read: **the carousel is
the navigation, the focus is the detail.** Moving through a lesson is browsing your own
record, never advancing a flow.

**Keep / demote discipline:**

- **Keep** per-shot evidence (clip, trace, metrics) as the ground-truth **drill-down**.
  It has real value; it is simply no longer the top of the hierarchy.
- **Demote** the full metric dashboard to a "show me everything" pull *from a block read*,
  not the default surface.
- **Resist** showing all metrics. The assess view names *one* pattern; the drill tracks
  *one* target. Completeness is the incumbent's virtue; **selectivity** is ours.

---

## 8. The coaching journey (across lessons)

The takeaway is a **first-class object that threads lessons.** Today's takeaway becomes the
next lesson's opening hypothesis ("Last time: low point — re-test?"). The athlete's history
stops being a log of recordings and becomes a **curriculum**: running threads ("low point"
spanned three lessons, improving), open vs. resolved.

This is the EA's missing "what do I work on next," the WN's emotional fuel (visible
progress on a *named* thing), and the TP's take-home record for a student — all of which
fall out naturally once a lesson has a conclusion.

---

## 9. Vocabulary cascade

| Today | Under the lesson model |
|-------|------------------------|
| Start session | Begin a lesson |
| Session type (Swing / Wrist / GRF / Coach) | Lesson + selectable lenses; Coach is the frame |
| Capture / shot | unchanged (evidence) |
| — (no equivalent) | Block / set (Assess · Drill · Re-test) |
| End session | Wrap up (produces a takeaway) |
| Session history | The athlete's coaching journey |

User-facing name for a Block is an open decision (§11) — current lean is **"set"**
("hit a set"), labelled by role.

---

## 10. Design heuristics (the screen checklist)

Check every new surface against these, the way PRs are checked against `personas.md`:

- **Does it coach, or does it mirror?** If the screen's job is to render numbers for the
  user to interpret, it is a mirror. Find the one thing — or the one relationship — and say it.
- **Is the unit a group or a shot?** Assessment and progress live at the **block** level.
  A screen that can only show one swing cannot diagnose.
- **Does it conclude?** A lesson surface should move toward a takeaway, not just display
  state. [WN: progress is the fuel. EA: "what next." TP: the take-home.]
- **Is it selective?** One pattern, one target, one cue. Every metric shown by default is
  a step back toward Trackman.
- **Plain language first.** Diagnosis and cues read for the WN; the technical layer is a
  disclosure, never the headline. [WN, then all.]
- **The instrument survives as a drill-down.** Never delete the dashboard the TP's
  credibility leans on — demote it behind a block read. [TP.]
- **No engineering metrics in the lesson.** Reprojection error, ring fill, fps, BLE IDs
  belong in System, never in the feed. [all.]

---

## 11. Decisions

1. **Collapse Coach-as-mode into the lesson frame? — Resolved: yes.** The right-hand
   content chooser becomes the **coach stream** (§7); Swing / Wrist / GRF survive as
   per-shot **lenses** (§6, §15). It simplifies the rail, the start flow, and the IA, and
   it is the real differentiator.
2. **Coaching *agent* or *document* for the TP? — Resolved: both, through one seam.** The
   coach **engine drives** (it emits the structured stream) and the TP **edits**;
   `AppSettings.coachNarration` decides only whether it also narrates aloud (§15). The
   block layer is active, not a separate manual builder.
3. **User-facing name for a Block — still open.** Internal model is `Block`; user-facing
   lean remains "**set**", role as the label ("Baseline", "Drill: low point", "Re-test").

---

## 12. Where the product stands against this model

A June 2026 functionality audit — *what would a user expect to do here, and can't?* — maps
cleanly onto this reframe. The gaps sort into four kinds. Three bear directly on the lesson
model and are recorded here; the fourth is operational debt the model deliberately keeps off
the lesson surface (§10) but which still gates whether a lesson can be captured at all. Within
each, highest-leverage first.

### 12.1 Latent assets — built, waiting to be surfaced

The shortest path to "coach, not mirror" is wiring up capability that already exists.

- **The coaching read for Wrist is built but invisible.** `WristAssessmentEngine` and the
  fault rules are implemented and unit-tested (Cast, Flip, Over-rotation, Holding-off,
  Chicken-wing, Open-face-at-top); the `Fault{title, cause, drill, pointsLost, phase}` object
  is defined. But `WristAnalyzer::analyze()` never calls the engine, `detail->faults` is never
  populated, and no surface renders it. **This is the §1 thesis — the coach's "one thing" plus
  its drill — sitting one wire from the screen.** It is the cheapest proof of the reframe after
  Wrap-up, and should be built alongside slice §13.1. [WN: the plain-language read. EA: what to
  work on. TP: corroborates her eye.]
- **Thumbnails.** The exporter writes `thumb.jpg`, yet the shot card still draws a placeholder
  — likely a missing binding, not missing capability. *(Verify the wiring.)*

### 12.2 Mirror-debt — where the product still reflects instead of coaching

These are the §10 heuristic violations the model exists to correct.

- **A number wearing authority it hasn't earned.** The Swing / GRF / Coach analyzers emit a
  *deterministic placeholder score* (seeded from the impact timestamp) and an empty metric set.
  For a lens that does not yet produce a real read, a fake score is worse than silence — it is
  the mirror's sin (a value to interpret) with none of the mirror's honesty. **Until a lens
  produces a genuine read, it must show no score.** [heuristic: "find the one thing and say it."]
  *(These modes are gated behind "coming soon" today; confirm no capture path reaches them.)*
- **A lesson that cannot conclude.** "End session" still only stops a recording — there is no
  Wrap-up and no takeaway. This is exactly the missing moment §4 names; it is build slice §13.1
  for a reason.

### 12.3 Missing journey scaffolding — the across-lessons layer (§8)

The coaching journey has no data layer yet.

- **Every shot is analysed in isolation.** There is no aggregation, no spread across a block,
  no named thread tracked over lessons; the only comparison that exists is a pairwise
  "vs. previous" ghost on the wrist chart, and re-analysis is stubbed. The dispersion read (§5)
  and the curriculum (§8) both depend on this layer and neither has it. [EA: "what next." WN:
  visible progress on a named thing.]
- **No takeaway object to thread.** Nothing carries one lesson's conclusion into the next
  lesson's opening hypothesis (§8).
- **History is a log, not a curriculum.** Shot history filters on quality / rating / video but
  cannot search or sort by date or club, and offers no shot-to-shot comparison beyond the wrist
  ghost.

### 12.4 Operational debt — real, but off the lesson surface

The model keeps engineering and reliability concerns in **System**, never the feed (§10), so
these get no lesson surface. They are recorded here only so the reframe is not mistaken for
"the plumbing is fine": they gate whether a lesson can be captured at all, and belong in the
engineering backlog, not this document.

- Camera connection failures are silent — no error shown, no retry (IMUs retry; cameras don't).
- Mid-session device dropouts and low battery are not surfaced during capture.
- Calibration status is invisible on the capture screen — a whole lesson can be recorded on a
  stale or failed calibration.
- Settings that silently don't take effect (live framerate, camera trigger mode).
- No storage visibility or cleanup; no per-shot export (bulk ZIP only); no in-app trash recovery.
- Multi-IMU wrist rig: no placement guidance and no mismount detection — sensors can be swapped
  and record the wrong segment with no warning.

**Read against the build path (§13):** surfacing the Wrist coaching read (§12.1) alongside the
Wrap-up view (§13.1) converts the largest latent asset into the cheapest proof of the reframe;
the journey scaffolding (§12.3) is the data layer that slices §13.2–§13.4 depend on.

---

## 13. Build path (de-risked slices)

Do not rebuild the session shell first. Each slice ships standalone and earns the next.

1. **Wrap-up / takeaways view** — generate a real lesson narrative (diagnosis · evidence
   clip · drill · "next") from an *existing* session's shots. No new capture model. Put it
   in front of an EA and a WN: do they say "I know what to do now"? Cheapest proof of the
   whole reframe.
2. **Assess block + dispersion strip** — group existing shots; show spread + a named
   pattern. Validates that the group read feels truer than the shot read.
3. **Drill block + target-progress + felt cue** — the prospective loop.
4. **Cross-lesson thread** — last takeaway → this lesson's opening hypothesis.
5. **Coach as frame** — only after 1–4 land, restructure the rail/start flow (decision
   §11.1): swap the right-hand chooser for `PpCoachStream` (§15).

**Where the templates first live.** Slices 2–3 ship inside **Practice Mode** (§14) — a far
smaller shell than the full lesson screen — so the focus-zone templates (§15) accumulate
there before the right-hand side is ever swapped (slice 5). The shell is never rebuilt up
front.

---

## 14. Practice Mode — the between-lessons loop

A lesson is an occasional event; **practice is the habit.** The most-used surface is not
the lesson — it is the loop the golfer runs *between* lessons, and it falls out of the
take-home for free.

When a lesson concludes, its takeaway (target metric · goal zone · the one feel · the
drill) **seeds a standalone Practice Mode session.** Same vocabulary as the lesson,
stripped to the loop: the feel pinned, reps measured against the prescribed target, and —
critically — **honest reads only.** No synthetic score.

```
┌ Practice · from your last lesson                 session 2 of the week ┐
│ 📌 release the face — logo left of target                              │
│ goal: face-to-path square · ±2°                                        │
│ ● ● ●   ● ●   ●●●●●  ──[ square ]──                                     │
│ in zone 13/20   ·   vs lesson 5/8 → 13/20   ·   spread ±3° → ±1.2°      │
│ starting to stick → next lesson: re-test face control                  │
└────────────────────────────────────────────────────────────────────────┘
```

Three reads, never a 0–100: **rate in zone** (efficacy), **trend vs the lesson baseline**
(improvement), and **spread** (stickiness — dispersion is first-class, §5). All three are
real measurements against the *one* prescribed metric. This is the §10 / §12.2 discipline
applied to a *scoring* surface — the place most likely to regress into a mirror, where a
fake composite would be the mirror's sin.

The result threads forward (§8): "was 5/8, now 13/20" becomes the next lesson's opening
hypothesis. The loop closes:

```
Lesson  ──prescribes──►  Practice Mode  ──measures──►  next Lesson
(occasional, coached)    (daily, mostly silent)        (re-scopes)
```

Per persona: the WN's emotional fuel (visible progress on a *named* thing), the EA's "what
to work on next" made actionable, the TP's take-home turned into measured homework. And it
is **cheap** — Practice Mode reuses the lesson's own focus-zone templates (§15);
`PpReadProgress` is the Drill view, standalone. It is therefore the natural *first host*
for build slices §13.2–§13.3.

---

## 15. The QML surface architecture

The fold is mostly a **navigation refactor**, not a rebuild. The right pane stops being a
chooser and becomes a model-driven coach stream; the old modes stop being top-level
destinations and become per-shot lenses. The tree, with what is reused / refactored /
net-new:

```
PpLessonScreen
├─ PpLessonTopBar
├─ RowLayout
│  ├─ PpCameraStage ........... LEFT — current camera side, ~unchanged    [reuse]
│  │  ├─ PpCameraView + PpSwingOverlay   (RHI / VideoOutput + Shapes)
│  │  ├─ PpCameraPip (face-on)
│  │  └─ PpScrubBar
│  └─ PpCoachStream .......... RIGHT — replaces the content chooser       [new]
│     ├─ ListView(CoachStreamModel) → DelegateChooser → PpRead* templates
│     └─ PpCoachComposer (mic / coach on·off)
├─ PpShotCarousel ............ BOTTOM — existing component                [reuse]
└─ StackView → PpShotDeepDive ....... per-shot drill-down                 [refactor]
   ├─ PpLensBar (Swing | Wrist | GRF)
   └─ Loader → Pp{Swing,Wrist,Grf}Lens   (current modes, as lenses)
```

**The focus zone is a state machine, not seven screens.** Each lesson stage (§7) is one
delegate template — `PpReadIntent / Dispersion / Relationship / Prescription / Progress /
Held / Takeaway` — chosen by entry `kind`. The same templates render compact in the stream
and full-size in Practice Mode (§14): **one component, two hosts.** `PpReadRelationship` is
generic over its *levers*, so path × face and pelvis × thorax X-factor are the same
component with different data — the "related things" requirement (§5) lives in the data,
not in bespoke screens.

```qml
// PpCoachStream.qml — the coach getting involved, model-driven
ListView {
    model: coachStreamModel
    onCountChanged: positionViewAtEnd()          // keep the live entry in view
    delegate: DelegateChooser {                  // Qt.labs.qmlmodels (mobile-safe)
        role: "kind"
        DelegateChoice { roleValue: "read"
            PpReadRelationship {
                headline: model.headline
                levers:   model.levers           // [{label,value,token}] ← kinematic Theme tokens
                outcome:  model.outcome
                live:     model.state === "live"
                onActivated: carousel.scrollToBlock(model.blockId)   // tap entry → rewind focus
            }
        }
        // intent · dispersion · prescribe · progress · held · takeaway · chat …
    }
}
```

**The coach that "gets involved" is an abstract + factory seam — the §12.1 wire made real:**

```cpp
class ICoach : public QObject {                  // base; RuleCoach now, LlmCoach later
    virtual void observe(const Block&) = 0;       // -> emits CoachEntry
signals: void entryProduced(CoachEntry); void statusChanged(QString);
};
// CoachEngineFactory::create(AppSettings::coachKind()) -> ICoach*
// RuleCoach reuses WristAssessmentEngine / Fault{title,cause,drill,pointsLost,phase}
// CoachStreamModel : QAbstractListModel
//   roles: Kind, Headline, Levers, Outcome, BlockId, ShotId, State, Ts
//   fed by entryProduced — this IS the §12.1 latent asset, wired to the screen.
```

**Silent coach (§6, decision §11.2):** `AppSettings.coachNarration = false` forks nothing.
The engine still emits the structured entries — the stream still builds, the TP can still
edit — it merely suppresses voice / auto-narration. The coach *drives but is editable.*

**Lenses are a registry/factory:** `LensRegistry` maps an id to a `Component`, so a new
lens never edits `PpShotDeepDive`. `ModeSwingAnalysis / ModeWrist / ModeGrf` become
`Pp{Swing,Wrist,Grf}Lens`, re-parameterised to take a `shot` instead of owning capture; the
**AI-Coach mode is deleted** — it became the stream.

**Reused / refactored / net-new.** Reused as-is: `PpShotCarousel`, `Theme`, the camera
stage, EventBuffer / analysis, `WristAssessmentEngine` / `Fault`. Refactored, not rebuilt:
RHS chooser → `PpCoachStream`; modes → `Pp*Lens(shot)`; AI-Coach → removed. Net-new:
`PpCoachStream` + the `PpRead*` templates, `ICoach` / `CoachEngine` + `CoachStreamModel`,
the `Lesson` / `Block` model + repository, `LensRegistry`.

**Slice mapping (§13).** Slice 1 (Wrap-up) needs only `PpReadTakeaway` + a
`CoachStreamModel` run over an existing session's shots — no capture changes. Slices 2–3
are `PpReadDispersion` + `PpReadProgress`, first hosted in Practice Mode (§14). The RHS swap
to `PpCoachStream` is the *last* step, once the templates already exist. The templates ship
one at a time before the screen ever changes; the shell is never rebuilt up front.

---

*Maintained alongside `personas.md` and `pinpoint-ux-design.md`. This document describes the
intended paradigm; §12 is the one exception, auditing current implementation against it. Update
it as slices land; preserve prior versions with a date stamp rather than overwriting.*

*v0.2 (2026-06-22): added §12 — current-state audit mapping the functionality gap analysis onto
the model; build path renumbered §12 → §13.*

*v0.3 (2026-06-22): folded the resolved capture surface into §7 (cameras left · coach stream
right · carousel · per-shot deep-dive lenses; the lesson arc as an emergent, non-wizard focus
zone); added §14 (Practice Mode — the between-lessons loop) and §15 (QML surface architecture);
resolved decisions §11.1–§11.2; noted related-pair reads in §5 and §10.*
