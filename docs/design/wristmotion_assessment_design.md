# Wrist Motion — Position Assessment Engine & UI

**PinPointStudio · design specification (pre-mockup, pre-implementation)**
Status: draft for review · Owner: Mark · Target screen: **Wrist Motion → Review mode**

This document specifies two coupled deliverables:

1. **The assessment mechanism** — every measured value, how each maps to swing faults, the
   reference-band model, the rule engine, and the RAG roll-up.
2. **The UI** that presents the values, trajectories, and findings with RAG rating and visualisation.

It is written to feed the normal pipeline: validate the design here → produce the authoritative
themed HTML mockup → write the Claude Code build prompt(s) → phased implementation with plan-mode
gates. Numbers in band tables are **starting heuristics, explicitly tunable** (see §11). This is a
*screening and feedback* tool, not biomechanical ground truth or a clinical diagnosis.

---

## 1. Purpose & scope

For a captured swing we have per-segment orientation through time (IMU + camera pose fused by the
in-memory OpenSim IK pipeline). Swing segmentation gives us timestamps for positions **P1–P8**.

The Wrist Motion assessment answers, at each position:

- What are the wrist / forearm / upper-arm **angles and rotations**, expressed **relative to address (P1)**?
- Is each value **within a sensible range** for that position (Tier 1, per-cell RAG)?
- Do the values across positions and across joints reveal a **named swing fault** (Tier 2, rule engine)?
- What is the **likely ball-flight consequence**, and a coaching feel to correct it?

In scope: lead/trail wrist (flex-ext, radial-ulnar), lead/trail forearm rotation (pro-sup),
lead/trail shoulder (elevation, horizontal abd-add, humeral int-ext rotation), lead/trail elbow
flexion. Cross-referenced but **owned elsewhere**: X-factor, X-factor stretch, O-factor, kinematic
sequence (the wrist release is the terminal link of that sequence — see §7.5).

---

## 2. Conceptual model

### 2.1 Segments and joints

Rigid bodies tracked (per side where instrumented), plus the thorax reference:

```
thorax ── shoulder ── upper arm ── elbow ── forearm ── wrist ── hand
 (ref)   (gleno-      (humerus)            (radius/    (radio-   (club-
          humeral)                          ulna)       carpal)   holding)
```

Joint angles are the **relative orientation between adjacent segments**, decomposed into
anatomically-meaningful degrees of freedom. We never report a raw segment quaternion to the user.

### 2.2 Degrees of freedom (the "values")

| Joint | DOF | + direction (canonical) | Golf name / feel |
|---|---|---|---|
| Wrist | Flexion / Extension | **+ flexion** (palmar) | bow (+) / cup (−) |
| Wrist | Radial / Ulnar deviation | **+ radial** | set·cock·hinge (+) / release·un-cock (−) |
| Forearm | Pronation / Supination | **+ pronation** | roll·rotate-closed (+) / hold-off·open (−) |
| Shoulder | Elevation | **+ up** | arm height / lift |
| Shoulder | Horizontal abd / add | **+ abduction** (away from midline / behind) | connection (− = across chest) |
| Shoulder | Internal / External rotation | **+ internal** | humeral roll |
| Elbow | Flexion | **+ flexed** | fold (lead: chicken-wing risk; trail: 90° at top) |

### 2.3 Sign convention and handedness

- All conventions are defined **lead/trail**, with the **right-handed golfer as canonical baseline**.
  For a left-handed golfer, mirroring is applied **at the acquisition layer**, so the assessment
  engine is **handedness-agnostic** and all rules/bands are written once.
- Decomposition uses a **fixed anatomically-aligned Euler sequence per joint**. Consistent with the
  existing **ZYX** handling: only the **middle (pitch) term** requires singularity checking. The
  primary clinical DOF of each joint is mapped to a non-middle axis where possible; where a position
  lands near gimbal lock, that single DOF is marked **indeterminate (Grey)** for that position
  rather than emitting a wild value (timestamp/position gaps self-document absence — no sentinels).
- The **forearm rotation sign** (pronation vs supination) is resolved during calibration against a
  known pose; the *functional description* ("rotating toward pronation through impact closes the
  face") is authoritative and the measured sign is mapped to it at calibration time.

---

## 3. Swing positions (P-system) and sampling

| Pos | Definition | Why it matters for wrist/arm |
|---|---|---|
| **P1** | Address / setup | **Reference.** All deltas computed from here. |
| **P2** | Club shaft parallel to ground, takeaway | Early set + face-to-plane match begins |
| **P3** | Lead arm parallel to ground, backswing | Set building; trail wrist extending |
| **P4** | Top of backswing / transition | **Face checkpoint** (lead wrist flat/bowed/cupped); flying-elbow check |
| **P5** | Lead arm parallel to ground, downswing | **Lag retention** begins; over-the-top / throw check |
| **P6** | Club shaft parallel to ground, downswing (pre-impact) | **Lag-retention checkpoint** (casting shows here) |
| **P7** | Impact | **Strike checkpoint**: shaft lean, flip/scoop, face square |
| **P8** | Club shaft parallel to ground, follow-through | Release completion; chicken-wing / over-rotation check |

**Sampling at a position.** IMU streams are noisy and P-timestamps have finite confidence, so each
cell value is a **short windowed median** about the Pn timestamp (window ≈ ±1 frame-cluster, tunable),
not a single instantaneous sample. Each sampled cell carries `(value, available, confidence)`.

---

## 4. Data sources & confidence

The engine consumes **fused joint angles** from the OpenSim IK output and does not care which sensor
produced them. But per-DOF **confidence is source-aware**, which feeds rule weighting and UI styling.

| DOF group | Primary source | Confidence | Notes |
|---|---|---|---|
| Wrist flex-ext, radial-ulnar | **IMU** (lead hand + lead forearm sensors) | High | High rate, low latency; best signal we have |
| Forearm pronation-supination | IMU (absolute roll) + pose cross-check | Med–High | Sign resolved at calibration |
| Shoulder elevation / horizontal abd-add | **Camera pose** (ViTPose wholebody / MoveNet) → IK | Medium | Good in-plane, weaker out-of-plane |
| **Humeral internal/external rotation** | Pose → IK | **Low** | Rotation about the limb long axis is poorly observed by keypoints — flag in UI; prefer IMU if a trail/upper-arm sensor is later added |
| Elbow flexion | Pose → IK | Medium | Adequate for chicken-wing/flying-elbow gross detection |

Confidence is propagated end-to-end: `cell.confidence = f(source, IK residual, segmentation
confidence at Pn, gimbal proximity)`. A cell whose value cannot be trusted is **Grey / no
assessment**, never fabricated.

---

## 5. Values catalogue — expected behaviour and fault mapping

For each value: the canonical sign, the **expected shape of its trajectory P1→P8** (as **Δ from
address**), and the faults a deviation implies. Ranges are **STARTING HEURISTICS — calibrate (§11)**.
Where a DOF is **style-dependent**, the band is deliberately wide / archetype-selected (§6) so valid
styles are not red-flagged; Red is reserved for dysfunction or internal inconsistency.

### 5.1 Lead wrist — radial / ulnar deviation (the "set / lag" curve)

Signature shape: **rise to a plateau (≈P3–P5), then a late, rapid drop into impact.** The *lateness*
of the drop is the marker of good lag/release timing.

| Pos | Expected Δ (heuristic) | Reading |
|---|---|---|
| P1 | 0 (ref) | — |
| P2 | +5…+15° radial | set begins |
| P3 | +20…+35° | set building |
| P4 | +30…+45° | fully set (some players set fully by P3) |
| P5 | +30…+45° (retained) | lag held — should **not** have collapsed |
| **P6** | **+20…+40° (retained)** | **lag-retention checkpoint** |
| P7 | 0…+15° | un-cocking into impact, near neutral |
| P8 | −5…+5° | release through |

Faults: **early release / casting** (drop too early, `radUln@P6` low), **insufficient set**
(`radUln@P4` low), **over-set / steep** (excessive, with timing dependence).

### 5.2 Lead wrist — flexion / extension (the "face" curve)

Most face control lives here: the back of the lead hand ≈ the clubface. Cupped (−) ⇒ open face;
bowed (+) ⇒ closed face. **Style-dependent** (bowed vs cupped tour archetypes both valid).

| Pos | Expected Δ (neutral archetype) | Reading |
|---|---|---|
| P1 | 0 (ref, slightly extended absolute) | — |
| P2 | −5…+5° | stay flat-ish |
| P3 | −10 (cup) … +10 (bow) | archetype diverges |
| **P4** | **−5…+20° = flat→bowed (good)** | **face checkpoint**; mild cup −5…−15 = Amber; cup < −15 = Red (open) |
| P5 | flat → bowed trend | downswing flattening |
| P6 | 0…+15° (flex, shaft lean building) | — |
| **P7** | **0…+15° flex = forward shaft lean (good)** | **flip if extension increasing into impact** |
| P8 | begins to extend as release completes | — |

Signature fault: **extension increasing P6→P7** (the flip/scoop). Tracked as `Δ = flexExt@P7 −
flexExt@P6`; with + = flexion, flip ⇒ this delta is **negative**.

### 5.3 Lead forearm — pronation / supination (the "roll / rotation" curve)

Functional model (authoritative; measured sign resolved at calibration): backswing carries some
**supination** (face matches plane); downswing-to-impact **rotates toward pronation** (squaring);
through P7→P8 continued pronation drives the face **closed** (the "roll/release").

| Pos | Expected behaviour | Faults |
|---|---|---|
| P2–P4 | moderate supination, plane-matched | over-supinated = shut/laid-off tendencies |
| P5–P6 | beginning to rotate back toward neutral | — |
| **P6→P8** | **smooth rotation toward pronation** | **over-rotation** (too fast/too much ⇒ hook) · **holding off** (stays supinated, face held open ⇒ push/block/weak fade) |

### 5.4 Trail wrist — flexion / extension (the "waiter's tray")

The trail-side mirror of the lead-wrist face/flip signals — used as **corroboration** to raise
finding confidence.

| Pos | Expected | Reading |
|---|---|---|
| P2–P4 | extension increasing | "tray" supports club; at top, trail-wrist extension ≈ lead-wrist flatness |
| P5–P6 | extension **retained** | trail-side lag |
| **P7** | **still slightly extended** | trail wrist *flattening early* = throw/flip (same event as lead-wrist flip) |
| P8 | extension releasing | — |

### 5.5 Trail wrist — radial/ulnar & trail forearm rotation

Trail radial/ulnar mirrors the set; trail forearm rotation mirrors the roll. Primarily corroborative.

### 5.6 Lead shoulder + lead elbow (connection / chicken wing)

| DOF | Expected | Faults |
|---|---|---|
| Lead shoulder horiz add | stays connected (across chest) in backswing | lifting (elevation↑ + abduction↑) = **disconnection** |
| Lead elbow flexion | near-straight throughout (small flexion) | **chicken wing** at P7–P8: flexion↑ + abduction↑ + under-rotated forearm |
| Lead humeral rotation | rotates through release | (low confidence from pose) |

### 5.7 Trail shoulder + trail elbow (flying elbow / over-the-top)

| DOF | Expected | Faults |
|---|---|---|
| Trail elbow flexion | 0 (P1) → ≈90° (P4) → extending (P6–P7) → near straight (P8) | **early extension P4→P5** = throw from the top |
| Trail shoulder elevation/abduction @P4 | moderate | **flying elbow** = high abduction at top (workable but across-the-line / OTT risk) |
| Trail forearm rotation timing | late, with the body | early/independent rotation = casting-rotation |

---

## 6. Reference bands & archetypes (where "good" comes from)

Bands are supplied by an **abstract provider** so the source is swappable (provider pattern, factory):

- **`ConfigReferenceBandProvider`** *(ship first)* — declarative bands from a versioned config
  (per DOF × per position × per archetype × per club/shape context). The §5 tables seed this.
- **`PlayerBaselineBandProvider`** *(later)* — bands derived from the player's own reference / best
  shots (intra-player consistency, the most coaching-relevant comparison).
- **`ArchetypeBandProvider`** *(later)* — match to a named model (e.g. *bowed-tour*, *cupped-tour*,
  *neutral*) so a valid style is scored against its **own** model, not a single dogmatic ideal.

**Stylistic-variant handling is a first-class requirement.** The engine must distinguish *"different
from a model"* from *"internally inconsistent / dysfunctional."* A bowed lead wrist at the top is
fine **if** the rest of the swing matches it; it is the **combinations** that reveal faults. This is
precisely why Tier 2 (relational rules) carries more weight than Tier 1 (per-cell bands).

**Context parameterisation** (seam, default = mid-iron / neutral shape): club type changes ideal
shaft lean and face; intended draw vs fade biases expected forearm-rotation timing. Bands are keyed
by `(archetype, club, intendedShape)`; v1 hard-defaults these and exposes the seam.

---

## 7. Assessment mechanism

### 7.1 Tier 1 — per-cell banding → RAG

For each `(DOF, position)` cell with a trusted value:

```
deltaFromAddress = value@Pn − value@P1
band             = bandProvider.band(DOF, Pn, context)   // {green:[lo,hi], amber:[lo,hi]}
rag = GREEN if deltaFromAddress ∈ band.green
      AMBER if deltaFromAddress ∈ band.amber
      RED   otherwise
      GREY  if !cell.available  (no assessment)
```

Tier 1 is the **dense instrument view** (the grid, §8) and the simple-first deliverable. It is
intentionally shallow — it flags *where* to look, not *what is wrong*.

### 7.2 Tier 2 — rule engine

A **declarative rule registry** (`IAssessmentRule` + `AssessmentRuleRegistry`, factory/registry
pattern). Each rule reads named cells across positions/DOFs and emits a **Finding**. Rules are data:
authorable, tunable, version-controlled, unit-testable in isolation.

**Rule schema:**

```yaml
id:            string            # stable identifier
name:          string            # display name
category:      face | strike | power | sequence | structure
inputs:        [ {dof, position} ... ]
derived:       expressions over inputs (deltas, rates, retained values)
condition:     amber: <predicate>   red: <predicate>   # over derived values
corroboration: [ signals that, if present, RAISE confidence ]   # e.g. trail-wrist mirror, sequence
suppression:   [ predicates that DOWNGRADE severity ]           # valid compensations
ballFlight:    [ slice | hook | push | pull | fat | thin | weak-high | low-strong | two-way ]
coaching:      short plain-language feel/drill
weight:        0..1               # clinical importance, feeds composite score
```

**Finding object (engine output):**

```
{ faultId, name, category, severity: RAG, magnitude: °,
  confidence: 0..1, contributingPositions: [Pn...], contributingDofs: [...],
  ballFlight: [...], explanation, coachingFeel, corroboratedBy: [...] }
```

**Severity vs confidence are orthogonal.** `severity` = how bad the deviation is (RAG, from
magnitude vs band). `confidence` = how much we trust it:

```
confidence = ruleBase
           × dataQuality(inputs)              # source-aware, §4
           × segmentationConfidence(positions)
           × (1 + corroborationBoost)         # capped
```

Findings below a confidence floor are **demoted** (shown under a "low-confidence" toggle), never
silently dropped. The UI encodes severity as colour and confidence as a secondary indicator (§8.4).

### 7.3 Fault catalogue (master mapping)

| # | Fault | Detecting DOFs @ positions | Rule logic (plain) | Ball-flight tendency | Coaching feel |
|---|---|---|---|---|---|
| F1 | **Open face at top** | lead wrist flex-ext @P4 | excessive cup (Δ < −15°), archetype-aware | slice; pull if OTT-compensated | flatten/bow lead wrist at top |
| F2 | **Closed face at top** | lead wrist flex-ext @P4 | excessive bow vs archetype | hook/block (if held off) | reduce bow / match release |
| F3 | **Flip / scoop at impact** | lead wrist flex-ext @P6→P7 (+ trail wrist @P6→P7) | extension **increasing** into impact | thin, fat, weak-high, distance loss | covered/flexed lead wrist, handle leads |
| F4 | **Casting / early release** | lead wrist radial-ulnar @P5,P6 (+ sequence, trail wrist) | lag dumped early (`radUln@P6` low / drop-rate high) | distance loss, weak, fat-thin variance | retain trail-wrist bend, pull the handle |
| F5 | **Insufficient set** | lead wrist radial-ulnar @P4 | under-cocked at top | narrow/short, timing-dependent | set the club earlier/fuller |
| F6 | **Over-rotation / over-release** | lead forearm rotation @P6→P8 | rotation toward pronation too fast/much | hook, two-way miss | quieter hands, body-led rotation |
| F7 | **Holding off / blocked release** | lead forearm rotation @P6→P8 | stays supinated, face held open | push, block, weak fade | let the lead forearm rotate/release |
| F8 | **Chicken wing** | lead elbow flexion @P7,P8 + lead forearm under-rotated + lead shoulder abd @P8 | arm folds/abducts instead of rotating+extending | weak-high, pull-slice, distance loss | extend lead arm, rotate through |
| F9 | **Flying trail elbow** | trail shoulder abd/elevation @P4 (+ trail elbow) | high abduction at top | across-the-line / OTT risk | tuck trail elbow at top |
| F10 | **Trail-arm throw / OTT tendency** | trail elbow flexion @P4→P5 (+ trail forearm rotation timing) | early extension/rotation from the top | pull, pull-slice, steep | retain trail-elbow bend into P5 |
| F11 | **Disconnection** | lead shoulder elevation + horiz abd, backswing | lead upper arm lifts off chest | inconsistency, fat/thin | keep lead arm connected to chest |
| F12 | **Reverse / negative shaft lean** | lead wrist flex-ext @P7 vs P6 + impact face proxy | hands behind clubhead at impact | weak-high, added loft | forward shaft lean, hands ahead |

(Note F3 and F4 are different events: F3 = wrist extension event at impact (loft), F4 = early loss
of the radial angle (power). They frequently co-occur and corroborate, but are reported distinctly.)

### 7.4 Relationship rules (cross-DOF / cross-position) — detail

These are the rules that make the system more than a heatmap:

- **Face-at-top consistency:** if `open at P4` **and** no compensating forearm rotation/bowing by
  P7 **and** impact face-proxy poor → escalate F1; if a clean compensation is detected, **suppress**
  to Amber (this is the bowed/cupped-style tolerance in action).
- **Flip corroboration:** F3 fires on lead-wrist extension into impact; if trail-wrist flattening at
  P6→P7 also present → confidence boost (two independent sensors agree).
- **Cast ↔ flip linkage:** F4 (early radial-ulnar collapse) commonly precedes F3; when both fire,
  surface them as a linked pair in the UI rather than two unrelated cards.
- **Over-rotation vs holding-off are mutually exclusive** on the forearm-rotation axis at P6→P8;
  the rule set must not emit both.

### 7.5 Kinematic-sequence tie-in

The wrist release is the **terminal link** of the proximal-to-distal chain (pelvis → thorax → arm →
club). The Wrist Motion engine **cross-references** the kinematic-sequence module (owned elsewhere):
if wrist angular-velocity peak occurs **before** the arm/torso peaks, that is out-of-sequence and
**corroborates** casting/early-release (F4) — boosting its confidence. This is a read-only
dependency; the sequence module remains authoritative for sequence findings.

### 7.6 Composite Wrist Score & RAG roll-up

A single headline number, consistent with the existing 0–100 quartile-graded quality pill
(**component reuse, distinct semantics**):

```
score = 100 − Σ_over_active_findings ( penalty_f )
penalty_f = severityWeight(RAG) × confidence_f × weight_f × scale
score clamped to [0,100] → quartile → RAG/colour token (reuse pill grading)
```

The score is a **relative coaching score, not absolute truth**, and is **fully explainable**: tapping
it reveals the contributing findings and their penalties. Low-confidence findings contribute
proportionally less, so the headline never over-reacts to weak signal.

---

## 8. UI design

### 8.1 Where it lives & information hierarchy

The assessment is a **Review-mode** surface on the Wrist Motion screen (Capture/Replay/Review already
exist). Hierarchy, most-glanceable first:

1. **Composite Wrist Score pill** (headline RAG/0–100) + shot identity.
2. **Findings list** — the prioritised "what's wrong and why" (severity × confidence ordered).
3. **DOF trajectory strips** — the *shape* of each motion through P1–P8 with the expected corridor.
4. **Position × DOF grid** — the dense instrument table (power-user, collapsible).
5. **Body schematic / 3D skeleton** at the selected position with RAG-coloured joints.

### 8.2 Components

**A · Position scrubber** — a P1–P8 segmented control (reuse the existing P-markers). Selecting a
position re-snapshots the grid, the schematic, and a position-cursor on the trajectory strips. Plus a
**"compare to"** control: *Address* (default) / *Reference shot* / *Previous shot* / *Archetype*.

**B · DOF trajectory strips (the key visualisation)** — small-multiples, one compact plot per DOF,
grouped (Lead wrist · Lead forearm · Trail wrist · Arms). Each strip:
- x = positions P1…P8, y = Δ-from-address (°), DM-mono axis labels.
- The **expected band** drawn as a shaded corridor (green core, amber margins).
- The **player's line** plotted through it; points **outside band** rendered in the RAG colour with a
  marker shape (accessibility, §8.4).
- A vertical cursor at the selected position.
This is where casting (line dipping out of the lag corridor before P6) and flip (extension spike into
P7) become *visible* rather than tabular. This is the most informative single element — lead with it.

**C · Position × DOF grid (heatmap table)** — rows = DOFs (grouped), columns = P1…P8. Each cell:
Δ value (DM Mono) on a RAG background; P1 column shows "0 · ref". Grey = no data. Cell tap → a
pop-over with the value, the band, source, and confidence. Collapsible drawer ("Data") for users who
want the dense view; hidden by default to keep the instrument-light feel.

**D · Findings list** — prioritised cards. Each card:
- Fault name + **RAG pill** + **confidence bar**.
- One-line plain-language explanation.
- **Position chips** (which P's contribute) and **ball-flight chips**.
- Expand → biomechanical "why", coaching feel/drill, corroborating signals, and a "show on body" link.
Linked faults (e.g. cast+flip) grouped. Low-confidence findings behind a toggle.

**E · Body schematic / 3D skeleton** — reuse the **Qt Quick 3D Y-Bot skeleton** (OrbitCameraController
already in place) posed at the selected position, with the **relevant joints tinted RAG**. Tapping a
finding highlights its joints and frames the camera; tapping a joint filters the findings to that
joint. A 2D fallback schematic for low-power/preview contexts. Ties the abstract numbers to the body.

**F · Composite Wrist Score pill** — reused 0–100 quartile pill, distinct data; tap → score breakdown.

### 8.3 Interaction model

- **Cross-highlight everywhere:** selecting a position, a finding, a grid cell, or a body joint
  highlights the same entity across all panels.
- **Compare-to overlays:** the trajectory strips can overplot a reference/previous/archetype line as
  a ghost trace for direct visual comparison.
- **Honesty in the UI:** Grey cells and "low confidence" badges are explicit; the tool never presents
  a fabricated value as fact.

### 8.4 RAG visual language & accessibility

RAG is **never colour-only**. Each state pairs colour with **shape + label**:

| State | Meaning | Colour token (semantic) | Shape | Label |
|---|---|---|---|---|
| Green | within range | `colorRagGood` | ● | OK |
| Amber | borderline / watch | `colorRagWatch` (≈ existing `colorAttention`) | ▲ | WATCH |
| Red | outside range / likely fault | `colorRagFault` | ■ | FAULT |
| Grey | no data / not assessed | `colorRagNone` | ◇ | — |

All colours, spacings and type sizes come from the **theme token system** (`Theme.sp()`,
`Theme.fontSymbol` for the glyph shapes, DM Mono for data, Georgian serif for body) across all eight
themes. RAG tokens are added to the semantic palette alongside the existing `colorAttention` family.
**Confidence** is a *separate* visual channel (a small bar / opacity on the secondary indicator), so
severity and confidence are never conflated.

### 8.5 Layout (Review mode)

```
┌───────────────────────────────────────────────────────────────────────────┐
│  Wrist Motion · Review     [Driver · Shot 14]            ⟨ Score  72 ▲ ⟩    │  header + score pill
├───────────────────────────────────────────────────────────────────────────┤
│  P1   P2   P3   P4   P5  [P6]  P7   P8        compare to: ▸ Address ▾        │  position scrubber
├───────────────────────────────────┬───────────────────────────────────────┤
│  TRAJECTORY STRIPS                 │  FINDINGS                              │
│  Lead wrist · radial-ulnar (lag)   │  ■ Early release (cast)        ▣▣▣▢    │
│    ┌─band──────────────╮           │     lag dumped before P6 · P5 P6       │
│    │      ·····●╲      │           │     [distance loss] [fat/thin]         │
│    └────────────╲──────╯           │  ──────────────────────────────────── │
│  Lead wrist · flex-ext (face)      │  ■ Flip / scoop at impact      ▣▣▣▢    │
│    ┌─band──────────────╮           │     extension into impact · P6 P7      │
│    │        ●╱ flip    │           │     [thin] [weak-high]                 │
│    └──────────────────╯            │  ──────────────────────────────────── │
│  Lead forearm · rotation           │  ▲ Holding off (mild)          ▣▣▢▢    │
│  Trail wrist · extension           │     [push] [weak fade]                 │
│                                    │     ⌄ low-confidence findings (1)      │
├───────────────────────────────────┴───────────────────────────────────────┤
│  ▸ Data  (P×DOF grid)            │   BODY @ P6  (3D skeleton, joints RAG)   │
│   lead wrist flx  0  -3  -8 …    │            lead wrist ■   trail ▲        │
└───────────────────────────────────────────────────────────────────────────┘
```

---

## 9. Implementation architecture (conventions)

C++ business logic; **pure-binding QML** (no JS in QML); abstract/factory at every device/model seam;
theme tokens for all visual values; `PpMessageLog` for diagnostics; **GPL v2 headers on all source**;
phased with plan-mode approval.

### 9.1 C++ (engine)

| Type | Kind | Responsibility |
|---|---|---|
| `PpJointDof` | enum | the DOF taxonomy (§2.2) |
| `PpSwingPosition` | enum | P1…P8 |
| `PpSwingPositionTimeline` | value | Pn timestamps + per-position confidence (from segmentation) |
| `PpWristAngleSet` | value | sampled `(value, available, confidence)` per (DOF, Pn); windowed-median sampling |
| `IReferenceBandProvider` | **abstract** | `band(dof, pos, context)` → green/amber bands |
| `ConfigReferenceBandProvider` | concrete | declarative bands (ships first) |
| `PlayerBaselineBandProvider` / `ArchetypeBandProvider` | concrete | future providers (factory-selected) |
| `IAssessmentRule` | **abstract** | `evaluate(angleSet, bands, context)` → optional `PpWristFinding` |
| `AssessmentRuleRegistry` | registry/factory | builds + owns the rule set; declarative rule loading |
| `PpWristFinding` | value | finding object (§7.2) |
| `PpWristAssessmentResult` | value | per-cell RAG matrix + findings + composite score + provenance |
| `WristAssessmentEngine` | orchestrator | sample → Tier 1 → Tier 2 → roll-up; emits result model |

The engine exposes `PpWristAssessmentResult` as a **QML-consumable model** (list models for grid
cells and findings, scalar props for the score). QML binds; it computes nothing.

### 9.2 QML (view, pure bindings)

```
WristAssessmentPanel.qml
├─ PositionScrubber.qml        (reuse P-markers)        + CompareToControl.qml
├─ WristScorePill.qml          (reuse 0–100 quartile pill, distinct data)
├─ DofTrajectoryStrip.qml      (band corridor + player line + cursor)  ×N  [small-multiples]
├─ PositionAngleGrid.qml       (RAG heatmap table, collapsible "Data" drawer)
├─ FindingsList.qml
│   └─ FindingCard.qml         (RAG pill, confidence bar, chips, expand)
└─ BodyPoseSchematic.qml       (Qt Quick 3D Y-Bot, RAG joint tints, OrbitCameraController) + 2D fallback
```

### 9.3 Tokens

Add to the semantic palette across all eight themes: `colorRagGood`, `colorRagWatch`
(can alias the existing `colorAttention`/`colorAttentionLight`), `colorRagFault`, `colorRagNone`, plus
band-corridor fills (`colorBandGreen`, `colorBandAmber` at low alpha). RAG glyph shapes via
`Theme.fontSymbol`. No hardcoded colours, sizes, or spacings anywhere.

---

## 10. Phased delivery (plan-mode gates)

- **Phase 0 — Contracts & conventions.** `PpJointDof`, `PpSwingPosition`, sign conventions,
  `PpSwingPositionTimeline`, `PpWristAngleSet` with windowed-median sampling + confidence + gimbal
  handling. No UI. *(Plan-mode approval before code.)*
- **Phase 1 — Tier 1 + core UI.** `ConfigReferenceBandProvider` (seed bands from §5), per-cell RAG,
  `PositionAngleGrid`, `DofTrajectoryStrip` with bands, composite score (simple), RAG tokens.
- **Phase 2 — Rule engine.** `IAssessmentRule` + `AssessmentRuleRegistry`, core fault rules
  (F1–F8 + trail-wrist corroboration), `FindingsList`/`FindingCard`, body schematic highlight,
  severity/confidence split.
- **Phase 3 — Context & comparison.** `PlayerBaselineBandProvider`, `ArchetypeBandProvider`,
  club/shape context, kinematic-sequence corroboration, compare-to overlays.
- **Phase 4 — Deferred.** Population/ML-tuned bands, drill library, trail-side full IMU
  instrumentation, club-specific ideal models.

Each phase documents seams as hook comments; simple-first implementations throughout.

---

## 11. Calibration, validation & honesty

- **Every number in §5 is a starting heuristic.** They belong in `ConfigReferenceBandProvider`'s
  versioned config, not in code, and are expected to move. Treat the §5 tables as v0 seeds.
- **Validation path:** (1) sanity-check against a handful of known-good and known-faulty swings;
  (2) tune bands against the player's own reference shots (`PlayerBaselineBandProvider`); (3) only
  then consider population/literature anchoring. If literature-anchored ranges are wanted, pull
  specific studies and cite them in the config — say the word and that becomes a task.
- **Screening, not diagnosis.** IMU drift, sensor placement, soft-tissue artefact, and pose
  uncertainty (especially humeral rotation) all bound accuracy. The UI surfaces confidence and Grey
  states honestly; the composite score is explicitly relative.
- **Do not over-fit to one model swing.** The archetype/baseline providers and the
  suppression rules exist so valid styles are scored against their own model, and Red is reserved
  for dysfunction or internal inconsistency — not mere difference.

---

## 12. Open questions / decisions for review

1. **Trail-side instrumentation.** Current IMUs are lead-side (hand + forearm); trail-wrist
   corroboration (§7.4) and F9/F10 lean on trail-side and pose-derived signals. Confirm: design for
   trail-side IMUs as a future add (graceful degradation when absent), or rely on pose for trail/arm?
2. **Composite score — keep or drop?** A headline number is glanceable but reductive. Keep it (with
   tap-to-explain), or lead purely with the findings list and make the score optional?
3. **Archetype selection.** Auto-detect archetype (bowed/cupped/neutral) from P4, or have the
   user/coach select it? Auto is slicker but can be wrong; manual is honest but adds a step.
4. **Grid default visibility.** Hide the P×DOF grid behind a "Data" drawer (instrument-light), or
   show it inline? I've defaulted to hidden.
5. **Band config format.** JSON sidecar vs a dedicated QML/C++ config resource for
   `ConfigReferenceBandProvider`. (Leaning JSON for tunability and version control.)
