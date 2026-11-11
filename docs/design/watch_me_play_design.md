# Watch Me Play — Design Document

**Status:** Draft for discussion · **Date:** 11 Nov 2026 (rev. 2) · **Area:** Capture, Sessions, Tendencies, Diagnostics

**Rev. 3 (11 Nov):** §12 expanded into the full UI change list. **Rev. 2:** reconciled with the code at `PinPoint-Golf/pinpointstudio@bdf6a6e` (11 Nov 2026) and with the connector design docs it ships (`foresight_premier_connector.md`, `gspro_v4_connector.md`, `gspro_gcquad_connector.md`, `reference/foresight_gcquad_data_sources.md`). Rev. 1 assumed live data came only from open Open Connect devices and that Foresight setups would need post-round import. That was wrong: PinPoint already reads FSX2020 and Premier live by watching the files they write, and a GSPro reader is under investigation. §2, §4, §5.6, §6, §9.6, §13 and §14 are rewritten; the rest is unchanged in substance.

---

## 1. Purpose

Everything PinPoint Studio currently knows about a golfer comes from swings hit on purpose to be measured: range work and structured assessment. That is the most controlled data the golfer will ever produce. It is also the least representative of how they actually play.

**Watch Me Play** lets PinPoint observe the golfer while they are *playing*: one club per shot, a different target every time, consequence attached to every outcome, no rehearsal of the same shot. It captures the same evidence as any session (video, pose, shaft, IMU, launch monitor), classifies each shot automatically (club, shot type, outcome), and keeps the data distinguishable from practice so the app can answer the questions practice data cannot:

- *What does my game look like when it counts, not when I'm grooving it?*
- *Which tendencies only appear in play?* ("you pull mid-irons on the course but not on the range")
- *Has my work-on transferred?* (fixed in practice, still present in play)
- *What changes in my swing in play?* (tempo, length, sequence, routine)
- *Where do I lose shots?* (off the tee, approach, around the green)

The general concept is broader than "play a sim round": it is **capturing golf in a performance context and comparing it with golf in a practice context**. Sim rounds are the first and easiest instance.

This is the goal `gspro_v4_connector.md` §1 already states — *"play golf in GSPro and do nothing else"* — generalised beyond one simulator and carried through into analysis.

---

## 2. Governing principles

1. **PinPoint never second-guesses the golfer's intent.** It classifies what happened (club, swing length, carry, outcome), not what the golfer meant to do. Context (distance to pin) is recorded as *situation*, never used to excuse or reclassify an outcome.
2. **The golfer never has to classify shots.** Club, shot type, hole boundaries and outcomes come from the simulator where it records them, and are inferred where it does not. Self-report is optional, never required, and never blocks capture.
3. **Don't interrupt play.** During a round PinPoint is an observer. No post-shot dashboards, no prompts, no focus theft, no sounds. Everything is surfaced afterwards.
4. **Never disturb the game.** The connectors' existing rule, restated because play makes it load-bearing: stat first, open only on change, never hold a handle, never take a lock a game write can collide with (`foresight_premier_connector.md` §2.6.1; `gspro_v4_connector.md` §4.3). A failed read is "try next tick", never an error the golfer sees.
5. **Read what the host application writes; never go further.** Never decrypt, never decompile, never touch the GSPconnect↔GSPro channel on TCP 9050 (`gspro_v4_connector.md` §1.1; reference doc *Excluded*). Undocumented files are personal-use until the vendor conversation has happened.
6. **Label context honestly.** "Play" is not "pressure". A sim round with nothing riding on it is a different context from a range session, but it is not evidence about pressure. Pressure is modelled as a separate, measured dimension (§7.3).
7. **Same pipeline, different context.** Play shots go through exactly the same analysis and diagnostics pipeline as practice shots. Context is a facet on the data, not a separate code path.

---

## 3. Session context taxonomy

Today a session carries `SessionController::Type` (`Swing`, `Wrist`, `Grf`, `Coach`), which describes the rig, not the golfer's situation — and the UX refactor away from the four modes is retiring even that. Watch Me Play introduces **session context** as a separate first-class attribute, set when the session starts (or from the connector's round signal, §5.1) and correctable in review.

| Context | What it is | Shot variety | Consequence | Typical source |
|---|---|---|---|---|
| **Assessment** | Structured protocol to establish/re-test faults | Low (prescribed) | None | Existing assessment flow |
| **Block practice** | Same club, same target, repeated | Very low | None | Ordinary sessions; Premier/FSX2020/GSPro range |
| **Variable practice** | Club/target changes every shot, no score | High | None | PinPoint "random club" drill, sim range targets |
| **Practice game** | Scored game on the range (e.g. points per shot, ladder) | High | Low–medium (score) | Sim training drills; PinPoint-hosted games (§4.5) |
| **Sim play** | Round or holes on a simulator | High | Medium (score) | GSPro / Premier / FSX2020 course play |
| **Course play** | Real round | High | Medium–high | iOS companion (§4.6) |
| **Competition** | Any of the above with a stake declared | High | High | User flag at session start |

Contexts form an ordered **transfer gradient** (Assessment → Block → Variable → Game → Sim play → Course → Competition). Analysis can compare any two, but the default comparison is *practice-family* (Assessment, Block, Variable) vs *play-family* (Game, Sim, Course, Competition).

The connector can usually tell the context it is in without asking: Premier's `history.json` labels range and drill shots `courseName: "Shot Analysis"` with a negative `holeNumber`; GSPro separates `DrivingRangeShot` from `LastShot` and writes `currentRound.dat` only on the course. Where it can tell, the context is set automatically and the start-session choice is only a default.

A session may also carry free tags (course name, "after work", "windy sim settings").

---

## 4. Play sources — what the code already reads

### 4.1 Connector inventory (as of `bdf6a6e`)

All four are `LaunchMonitorBase` subclasses built by `makeLaunchMonitor()` (`src/LaunchMonitor/launch_monitor_factory.h`); all file-watchers poll at ~250 ms, stat first and open only on change.

| Connector | `Kind` | Status | Live source | Club name | Club metrics | Flight | Situation data |
|---|---|---|---|---|---|---|---|
| **GC Quad — FSX2020** | `GcQuad` | ✅ Built | `LastShot.CSV`, rewritten in place each shot | Family only (`1w`/`Fw`/`Irn`/`Wdg`); disagreed with the club hit on most shots, **never followed** | Speed, AoA, path (rest sentinel `16777215`) | Carry, total, offline, peak, descent | `Distance to Pin` column |
| **GC Quad — Foresight Premier** | `Premier` | ✅ Built 2 Nov; cabin run passed | `SessionLiveSnapshot.<userId>.json` (+ queue to finalise, + `ShotReplay/history.json` for carry) | Specific (`9i`, `4h`, `Gw`…); **opt-in follow** (`launchmonitor/premierFollowClub`) | Full with a full sticker set; **one shot late** | Carry only (no offline) | `history.json`: `courseName`, `holeNumber`, ball/aim world positions |
| **Open Connect server** | `GsPro` | ✅ Built | PinPoint *is* the server on 921; bridges (R10 ×5, MLM2PRO ×2, SkyTrak+, GC2 serial, PiTrac, OpenFlight…) connect to it | Flows **out** (PinPoint sends the 201) | Whatever the bridge sends | Whatever the bridge sends | none |
| **GC Quad — GSPro v4** | `GcQuadGsPro` (proposed) | 🔍 Investigating (doc 9 Nov) | `GSPro.db` → `LastShot` (course) / `DrivingRangeShot` (range), read page-level with no lock | `club` (`GW`, `LW` seen) — **follow by default** | Seen filled on the range; **course unproven** | Carry, total, offline, peak, descent | `currentRound.dat`: `Hole`, `HoleShot`, `HolePar`, `DistanceToPin`, `StartingSurface`, `EndingSurface`, `ShotResult`, `ClubIndex`, `CourseKey` |

Common machinery that Watch Me Play inherits rather than builds: timestamp-checked `ShotPairing` (`shot_pairing.h`) against camera swings; the late-update path that rewrites a swing and re-analyses it when a reading (or its club) arrives after analysis started (Premier §4.8/§4.9); standalone shots for readings nothing else saw (`standalone_gate.h`); the 10 mph ghost gate; and the corrected swing wallclock anchor (`ShotProcessor::beginGather()`, fixed 2 Nov).

### 4.2 What this means per simulator, for play specifically

**Premier.** Live on the **range**. Two documented facts bound it for play:

- *Training drills write nothing* to the snapshot or the queue, stickers or not (Premier §2.6.7, Q8). `history.json` still gets ball data and carry, but labels drill shots with the last range club, so its club name cannot be trusted there.
- *Course play has not been investigated.* The reference doc describes the snapshot as "the range session in progress", and nothing in the repo records a Premier round being watched. Whether course play writes `SessionLiveSnapshot` at all is **the first question this feature depends on** (§14 Q1).

If the snapshot is range-only, the fallback for course play is `history.json`: ball data, carry, `courseName`, `holeNumber`, and world positions, written live ~0.8 s after the shot, last 100 shots. That is ball-only with a club name of unknown reliability — still enough for situation and hole segmentation, not for club metrics.

**FSX2020.** Live wherever `FSX.exe` writes `LastShot.CSV`. Whether FSX2020's course mode writes it the same way as its range is not recorded in the repo (§14 Q2). Its club field is family-only, so in play the club must come from inference (§6) — but it carries `Distance to Pin`, which gives start distance directly. FSX2020 is the legacy application; Premier is where Foresight's effort is going, so it is the secondary target.

**GSPro.** The richest play source once it lands, because `currentRound.dat` hands PinPoint the round structure that rev. 1 proposed to infer: hole, stroke in hole, par, distance to pin, **starting and ending surface (lie)**, retakes (`ShotResult = 1`, each a real swing), and probable putts/gimmes (`ClubIndex` 26 with no launch monitor shot behind it). `gspro_v4_connector.md` §4.2 parks `currentRound.dat` as "the natural later source for hole, par, lie and course" because the reading struct has nowhere to put them. **Watch Me Play is that place** (§8). The decisive open question is whether `LastShot` carries club data on the course (that doc's §7 Q1, Block 4); if not, its own go/no-go is no-go.

**Open Connect bridges.** PinPoint replaces GSPro as the server, so with an open device the golfer is not playing a sim at all. That path serves Tier C (§4.5). A **relay** — PinPoint in the middle of a bridge and real GSPro, tapping both directions — is still possible but is now low priority: GSPro Connect holds 921 on the one machine where someone would run both (`gspro_monitor.h`), so it needs the bridge pointed at another port, and GSPro's own files will give the GC Quad user the same data without it.

### 4.3 Source of truth per field in play

| Field | Premier | FSX2020 | GSPro (proposed) | Fallback |
|---|---|---|---|---|
| Shot exists | snapshot entry | CSV rewrite | `LastShot` row | Camera/mic detection (standalone) |
| Club | `ClubName` (if follow on) | — | `club` | Inference (§6) |
| Club metrics | `ClubData` (late) | speed/AoA/path | `ShotData` (course: TBC) | none |
| Carry | `history.json` | CSV | `ShotData` | none |
| Start distance | world positions (TBC) | `Distance to Pin` | `DistanceToPin` | none |
| Hole / stroke | `holeNumber` (TBC on course) | — | `Hole`, `HoleShot` | Inference (§5.6) |
| Lie | — | — | `StartingSurface` | Assume tee/fairway |
| Retake | — | — | `ShotResult = 1` | Inference |
| Putt / gimme | — | — | `ClubIndex` 26, no LM shot | Inference |

### 4.4 Post-round import (secondary)

Kept for sessions where a connector was not running and for third-party data: GSPro's Practice Range and On-Course Practice CSV export; Premier and FSX2020 history via their own files. Aligned by time and sequence. No longer the primary Foresight path.

### 4.5 Tier C — PinPoint-hosted play (no sim required)

PinPoint can host "play" itself with any launch monitor it reads (all four connectors, including the Open Connect server):

- **Course card mode:** the golfer enters (or imports) hole yardages and pars. PinPoint announces each shot by TTS ("Hole 4, par 4, 410 to the middle"), computes the new position from carry + offline, and announces the next. A mini-map shows the hole schematically. When the LM reports no offline (Premier), lateral comes from the flight model as the LM panel already does (`lm_flight_path.h`).
- **Practice games:** scored formats run by PinPoint — random club/random target, 9-shot challenge, up-and-down ladder, consequence games (miss the window → restart). These populate *Variable practice* and *Practice game*, and — unlike Premier's drills, which write nothing — they keep full LM data because the golfer stays in the sim's range mode.

Club and target are known by construction, and it uses the existing TTS stack. With a GC Quad, Tier C sits on top of Premier or FSX2020 range mode, so it works on the author's rig today.

### 4.6 Tier D — Course play (later)

Real-course capture via the iOS companion app: phone on a tripod or bag mount at 150 fps, mic shot detection (already built), GPS for hole/position, optional portable LM. Lower fidelity per shot, but it is the true target context. Out of scope for v1; the data model must accommodate it now.

---

## 5. Capture-time behaviour

### 5.1 Session lifecycle

```
Idle ─▶ Round setup ─▶ Playing ─┬─▶ Shot in flight ─▶ Playing
                                ├─▶ Between holes ─▶ Playing
                                └─▶ Round end ─▶ Reconcile ─▶ Closed
```

- **Round setup:** phase 1 — the golfer starts a session once in PinPoint and picks the context. Phase 2 — auto-start from the simulator's round signal (GSPro: Block 9 of the investigation; Premier: whatever course play writes). One screen, defaults remembered.
- **Playing:** PinPoint runs minimised or on a second screen. A small status strip only (connected / shots captured / last club). No dashboard, no coaching, no TTS unless Tier C. `gspro_v4_connector.md` §4.7 already lists the checks: no focus theft, no sounds, no replay over the game, game frame rate unaffected.
- **Round end:** the simulator's round-end signal where one exists, explicit "Finish round", or idle timeout. Leads to Reconcile (§5.6). Ending the session also runs the Premier carry sweep (`sweepCarry()`), as it does today.

### 5.2 Shot lifecycle in play

Each reading (or standalone camera/mic shot) produces a **play shot** record linking:

- LM record (ball + club data), with provenance — including Premier's two-stage arrival (ball now, club one shot later) and GSPro's single late reading
- Swing capture (video, pose, shaft, IMU) — same pipeline as practice
- Situation snapshot (§7.2), from the simulator where it records it
- Routine timing (§5.3), physiological data (§5.4)
- Linked rehearsal swings (§5.5)
- Classification results (club, shot type, outcome) with confidence

**Re-analysis is the normal path in play.** On the range the club rarely changes, so a club that arrives after analysis started is an edge case. On a course it changes nearly every shot, and both Premier (club follow, ~0.7 s) and GSPro (reading several seconds after contact) can deliver it after analysis has begun. `gspro_v4_connector.md` §4.6 identifies this; the cost of a full re-analysis per shot beside a running game is its §7 Q10 and must be measured before Watch Me Play commits to per-shot analysis during the round. The fallback is to **defer analysis to round end**: capture and pair live, analyse after. That also satisfies principle 3 more strictly.

### 5.3 Pre-shot routine timing

Cheap, objective and highly relevant to play:

- **Ball-down → strike** (camera ball detection; Open Connect `LaunchMonitorBallDetected` heartbeat where a bridge sends it)
- **Time over the ball:** address stillness onset → takeaway (from pose)
- **Waggle count / pre-shot movement** (optional, from shaft track before takeaway)

Routine variability is a known marker of disrupted performance; comparing its distribution between practice and play is one of the most direct "what changes when it counts" measures.

### 5.4 Physiological arousal (optional)

PinPoint already does BLE. The standard Bluetooth Heart Rate Service is open and universally supported by chest straps. Recording HR during play gives HR at address per shot, its delta from a session baseline, and an objective arousal signal for the pressure model (§7.3). Optional; everything else works without it.

### 5.5 Practice (rehearsal) swings

In play, golfers make practice swings that never touch a ball. The Premier investigation confirmed that **a practice swing sends nothing** from the Quad (§2.6.3 there), so a camera-detected full swing with no reading and no ball at impact is a clean rehearsal signal. Store it linked to the next real shot. Rehearsal vs real swing (tempo, length, sequence, plane) is a distinctive "watch me play" insight: the rehearsal is what the golfer *wants*; the real swing is what the ball does to them.

Care: the standalone-shot path must not turn rehearsals into shots, and a dropped real shot (Premier silently drops shots hit before it has reset the tee, §2.6.3) must not be mistaken for a rehearsal — the impact camera's ball check separates them.

### 5.6 Hole segmentation, retakes and reconciliation

Taken from the simulator where it records them, inferred only where it does not:

| Event | GSPro (`currentRound.dat`) | Premier / FSX2020 | Inference fallback |
|---|---|---|---|
| New hole | `Hole` changes | `holeNumber` (Premier, TBC on course); `Distance to Pin` jumps up (FSX2020) | Putter → driver; long idle gap |
| Stroke in hole | `HoleShot` | — | Count since hole start |
| Par | `HolePar` | — | User in reconcile |
| Putt | `ClubIndex` 26 / putting | putter club, low ball speed | Ball speed / launch rule |
| Gimme / auto-putt | `ClubIndex` 26 with no LM shot | hole changes with no putt | same |
| Retake / mulligan | `ShotResult = 1` (a real swing; shares `GlobalShotNumber`) | distance to pin unchanged after a full shot | same club, same start, short interval |

**Reconcile** (after the round, optional, < 1 minute): a scorecard-shaped grid of holes and strokes. For GSPro rounds it is pre-filled and usually needs nothing; for others the golfer can fix boundaries and enter par/score. Skipping it leaves the round usable; only score-dependent analysis is reduced.

Retakes are kept, marked `replayed`, excluded from score analysis and included in swing/tendency analysis.

---

## 6. Club identification

### 6.1 Sources, in precedence order

| # | Source | Confidence | Notes |
|---|---|---|---|
| 1 | User correction in review | Authoritative | Always wins; `review.club` naming neither club is already treated as the user's pick |
| 2 | Tier C (PinPoint chose the club) | High | Known by construction |
| 3 | Sim-reported specific club | High | GSPro `club` (follow by default); Premier `ClubName` (opt-in follow). Unknown codes leave the club alone, logged once (`premier_club.h`; `gsProClubToVocabulary()` proposed) |
| 4 | Sim-reported family | Low | FSX2020 `1w`/`Fw`/`Irn`/`Wdg` — evidence only, never followed |
| 5 | **Inferred** from readings + capture | Varies | Always computed, even when 2–4 exist |

The inferred club is always computed and compared with the reported one; disagreement flags the shot "club mismatch?" in review. In play this catches the golfer who changed club in their hand but not in the sim — which matters more once the sim's club drives re-analysis and corridor selection.

Every shot stores `club`, `clubSource`, `clubConfidence`, and the inferred alternative if different.

### 6.2 Inference model

**Starting point already in the code:** `lm_club_evidence.h` — a two-of-three vote (dynamic loft, smash, spin rate) between the wood and iron families, deliberately narrow (fairway woods and hybrids never checked), suggesting the session's most common recorded club of the other family and never relabelling to a club nobody hit. Watch Me Play extends it from *family* to *club*, keeping its rules: evidence is shown, a missing input yields no read, and the user's choice wins.

The extension:

- **Delivered loft carries club identity, not speed** — which matters because partial shots destroy the speed signature but largely preserve the loft signature.
- Features where available: dynamic loft, launch angle, spin rate, spin loft, smash, attack angle, club speed, ball speed; from PinPoint's own capture, shaft lean at impact and swing length (§6.3).
- **Per-golfer, per-club distributions learned from labelled practice.** Range sessions under Premier/FSX2020/GSPro have the club recorded, so practice becomes the training set — a direct dividend of keeping contexts separate.
- Posterior over the golfer's bag, with a prior from start distance where the sim gives it (a 140-yard shot is unlikely to be a driver — a prior, never an override).
- **Ambiguity is a valid answer.** Adjacent wedges at partial speed are often indistinguishable; the result can be a club group ("wedge 50–56°") with confidence.
- **Ball-only shots** (no stickers: Premier gives `ClubData: null`; GSPro gives zeros) lose most features. Rounds will be mixed (`gspro_v4_connector.md` §4.9), so inference must degrade to launch + spin + ball speed + swing length, with wider confidence.

Validation: hold-out accuracy on labelled practice sessions, per club pair; target ≥ 90 % top-1 for full stickered shots, group-level for partial wedges and ball-only shots.

### 6.3 Swing length from video

PinPoint measures what the LM cannot: how long the swing was. Lead-arm and shaft angle at the top classify swing length (full / three-quarter / half / quarter). The most direct evidence of a partial shot, independent of outcome and of stickers.

---

## 7. Shot-type and situation classification

### 7.1 Shot types

Refines the original rule (pitch = distance below full; chip = under 30 yd) so that a **mishit full swing is not reclassified as a pitch** — that would excuse a poor strike, the other side of "never second-guess intent".

| Type | Rule (defaults, all configurable) |
|---|---|
| **Putt** | Putter, sim putting signal, or zero launch / roll-only |
| **Chip** | Carry < 30 yd (carry is the sim's or device's, see §14 Q6) |
| **Pitch** | Carry ≥ 30 yd **and** carry < club full-carry mean − 2σ **and** (club speed < 90 % of the club's full-swing mean **or** swing length < full) |
| **Controlled full** | Swing length full or three-quarter, speed 90–97 % of mean, carry within range |
| **Full** | Speed ≥ 97 % of mean or swing length full |
| **Full — short** | Full by speed/length, carry below the full band → a full shot that came up short; strike/outcome analysis explains why |

Without club speed (ball-only shots), the speed test falls back to ball speed against the club's ball-speed band, and swing length carries more weight. Later types: *knockdown* (full-ish speed, launch well below the club's norm) and *bunker/rough* (from GSPro's `StartingSurface`). Club full-carry mean and σ come from **practice** data, so play does not contaminate its own reference. The Tendencies filters already include "shot type" (`tendencies_flow_design.md` §1), and its open question on per-shot-type "good flight" (§11 Q1 there) applies directly to pitches.

### 7.2 Situation

Recorded per shot, an analysis facet, never a judgement:

- **Shot category:** tee (par 4/5), approach, around the green (< 50 yd), putt — from start distance and stroke in hole
- **Start distance / leave distance** (`DistanceToPin` for GSPro; `Distance to Pin` for FSX2020; Premier TBC)
- **Lie** (GSPro `StartingSurface`; otherwise assumed tee/fairway, and said so)
- **Stroke in hole, hole index, time into round** (fatigue proxy)
- **Previous shot outcome** (recovery shot after a miss)
- **Score state** (if known): relative to par, closing holes

### 7.3 Pressure index

Pressure is a **per-shot scalar with stated provenance**, not a session label:

- *Situational:* closing holes, score relative to the golfer's target, shot after a bad hole, competition flag, Tier-C consequence games
- *Physiological:* HR delta from baseline at address (if a strap is worn)
- *Self-report:* optional post-round "which shots felt tense?" tap on the scorecard

Without HR or stakes, PinPoint describes findings as "in play", never "under pressure".

---

## 8. Data model

```
Session
  type           : (existing SessionController::Type — rig, being retired)
  context        : Assessment | Block | Variable | Game | SimPlay | CoursePlay | Competition
  contextSource  : connector | user
  lmKind         : GcQuad | Premier | GsPro | GcQuadGsPro | None
  stake          : none | declared
  tags[]
  └─ Round (0..1)               — present for play contexts
       course, simulator, simRoundId?, settings snapshot (where readable)
       └─ Hole (1..n)
            index, par?, score?, source: sim | inferred | user
            └─ Stroke (1..n)
                 shotRef → Shot              — null for gimme / auto-putt / penalty
                 strokeInHole, startDistance, leaveDistance, startSurface?, endSurface?,
                 category, replayed, simShotId?   — e.g. GSPro ShotID GUID
Shot  (existing entity, extended)
  lmRecord + provenance  (deviceShotId, late club update, carry update — existing)
  club, clubSource, clubConfidence, clubInferredAlt
  shotType, shotTypeConfidence, swingLength
  situation { … }, pressure { value, components }
  routine { ballDownToStrike, timeOverBall, waggles }
  hr { atAddress, deltaFromBaseline }
  rehearsals[] → RehearsalSwing   — swing capture without ball
  outcome (existing auto-classified strike/direction/flight)
```

Round/hole/stroke data lives **beside** `LaunchMonitorReading`, not in it: the reading stays a pure device record, and the GSPro connector feeds `currentRound.dat` into this structure through a separate signal. Context is denormalised onto each Shot for query speed.

---

## 9. Analysis and review

### 9.1 Round review

The primary post-round surface. Scorecard-led: holes across the top, each hole a strip of strokes. Selecting a stroke opens the existing shot view (video, overlays, the FSX-style LM panel) plus situation and the **"what happened there?"** explanation from the shot-outcome feature. This is where that feature earns most: the golfer remembers the bad shot on 14 and can see *why*.

Round-level views:

- **Round timeline:** pressure index, HR and routine time across the round, shots marked by outcome
- **Shot categories:** off-tee / approach / around-green / putting
- **Club usage:** clubs hit, reported vs inferred, mismatches flagged for one-click fix

### 9.2 Practice vs play comparison ("Transfer")

The core of the feature. For any club or club group, compare practice-family vs play-family (or any two contexts):

| Dimension | Measures |
|---|---|
| Distance | Carry mean and σ per club; full shots only by default |
| Dispersion | Offline σ, dominant miss side (not available from Premier, which has no offline) |
| Strike | Strike-outcome mix |
| Direction & flight | Outcome Sankey (strike → direction → flight) split by context |
| Club delivery | Path, face, AoA, dynamic loft — stickered shots only |
| Swing | Tempo ratio, backswing length, transition timing, kinematic sequence, plane, head movement |
| Routine | Ball-down→strike and time-over-ball distributions |
| Diagnostics | Conditions present / confirmed in each context |

Each comparison shows effect size and uncertainty with the variability mark family, and "not enough play shots yet" as an explicit state.

**Like-for-like matters.** Practice under Premier with stickers and play under GSPro without them are different instruments. A comparison is drawn only over fields both arms measured, and the swing measures (PinPoint's own) are the most robust basis because they do not depend on the launch monitor at all.

### 9.3 Tendencies, context-aware

The Tendencies page gains a **context facet** beside club and shot type. Tendencies that hold in play but not practice are the headline: *"With 7–9 iron in play you pull 38 % of shots; on the range 12 %."* Partial-shot tendencies ("you push wedges when it's not a full shot") come largely from play data, because golfers rarely practise partials in volume. Home marks play-only tendencies distinctly.

### 9.4 Work-on transfer

Work-ons gain a lifecycle stage beyond "improving":

```
Identified → Improving (practice) → Stable (practice) → Transferred (play) → Retired
                                          └──▶ Not transferring (play)
```

A work-on is *Transferred* only when its measures are within corridor in play. This extends `SessionEvidence` (`work_ons.h`: NotMeasured / Inconclusive / Quiet / Pattern) per context rather than replacing it: the same evidence ladder, read separately for practice and play sessions.

### 9.5 Diagnostics in play

Session-diagnostics chain confirmation runs per context. Two new readouts:

- **Context-specific conditions:** confirmed in play but not in practice, or the reverse.
- **Rehearsal gap:** conditions present in the real swing and absent in the linked rehearsal.

The diagnostics model is unchanged; context affects corridor selection and materiality, never valence.

### 9.6 Scoring analysis

GSPro's `currentRound.dat` gives start distance, end distance **and start/end surface** per stroke — the inputs a strokes-gained calculation needs, lie included. Premier and FSX2020 give distance only; there v1 assumes tee/fairway lies and says so. The method and an amateur baseline are to be resolved against CrossRef and licensed appropriately before inclusion — no recalled numbers.

This connects swing faults to score: *"approach play costs ~2.1 shots per round against your handicap baseline, and 60 % of that is fat strikes with mid-irons."*

### 9.7 Play-derived yardages

Per-club carry in play is often shorter and more variable than on the range. Athlete club distances show **both**, labelled — practice yardage and play yardage. The play figure is the one to take onto the course. Note the source of carry: Premier's and GSPro's are each simulator's flight model; FSX2020's is computed by FSX. Comparing yardages across simulators is out of scope.

### 9.8 Cross-round trends

Rounds feed the cross-session progress tracking with context as a series dimension: practice and play lines on the same chart, converging when practice is transferring.

---

## 10. Statistical honesty

Play generates few shots per club: a round is ~14 tee shots, a handful of each iron, many partials — and on a GC Quad, a mix of stickered and ball-only clubs.

- **Pooling:** comparisons accumulate across rounds in a rolling window (default last 5 play sessions or 90 days).
- **Shrinkage:** per-club play estimates shrink toward the golfer's own practice estimate (empirical Bayes), so one wild round doesn't produce a tendency.
- **Minimum evidence:** a play-vs-practice finding needs minimum n per arm and an effect-size threshold; below that the UI says so.
- **Dispersion comparisons** use robust variance tests (Brown–Forsythe), not F-tests.
- **Multiplicity:** clubs × measures × contexts is a large family. Either Benjamini–Hochberg control, or restrict confirmatory claims to measures on authored DAG chains (consistent with session diagnostics).
- **Confounds, stated where relevant:** club mix, target variety, warm-up, fatigue, time of day, sticker coverage, and the simulator itself (Premier, FSX2020 and GSPro each compute carry with their own flight model). A difference is reported as a difference *between contexts*, not attributed to pressure unless the pressure index supports it.

---

## 11. Data retention

Consistent with the retention direction (keep metrics; keep full data only for exemplar swings), play has one adjustment: the swings most worth keeping are the **consequential failures** — the shots behind the biggest strokes-gained losses, play-only tendencies and non-transferring work-ons — plus one or two best-in-round for contrast. Rehearsals are metrics only unless linked to a retained shot. Raw simulator files are never copied into the library; only the fields mapped into the shot and round records.

---

## 12. UI changes

Few new screens. Most of the change is the **context** facet added to screens that already exist. Component names are as of `bdf6a6e`.

### 12.1 New surfaces

| Surface | What it is | Where it lives |
|---|---|---|
| **Round review** | The only substantial new screen. Holes across the top as a scorecard; each hole a strip of strokes. Picking a stroke opens the existing shot view with its situation and the "what happened there?" explanation (§9.1). Round-level tabs: timeline, shot categories, club usage | Reached from Sessions as a play session's Review action; play sessions do not open in Live |
| **Reconcile** | Small post-round scorecard grid to fix hole boundaries, par and score (§5.6). Optional. Pre-filled from `currentRound.dat` for GSPro, so it mostly matters for Premier and FSX2020 | Step at session close for play contexts, and reopenable from Round review |
| **Transfer view** | Practice-vs-play per club or club group: distance, dispersion, strike mix, club delivery, swing measures, routine, with the variability marks (§9.2) | A tab in the planned Trend area rather than its own screen |

### 12.2 Changes to existing screens

| Surface | Component(s) | Change |
|---|---|---|
| Session setup wizard | `setup/ScreenSessionSetup`, `SetupSteps` | One context step, defaulted from the connector where it can tell; optional HR strap row in the capability list (`setup_capability_rows.js`) |
| Live during play | session view, `PpSessionToolbar` | **Quiet mode**: post-shot dashboard and replay suppressed; a thin status strip (connected, shots, last club); no focus changes or sounds. A behaviour switch on the existing view, not a new screen |
| Home Tendencies | `home/HmShotFilter`, `HmTendencies`, `HmTendencyFlow`, `HmTendencyRow` | A fourth filter beside club and strike/direction/flight: context (practice / play / all). Play-only tendencies badged. The Sankey only needs to honour the filter |
| Tendencies report | `report/PpTendenciesPage` | Same context filter, so printed reports can separate practice from play |
| Sessions table | Sessions screen (redesign in flight) | Context column and filter; score column for rounds; Review routes play sessions to Round review |
| Work-ons on Home | Home Coaching tab | Transfer status per work-on: practised / transferred / not transferring (§9.4) |
| Athlete settings, club list | athlete settings | Practice yardage and play yardage side by side, each with σ (§9.7) |
| Shot view | shot view, LM club note | "Club mismatch?" note with one-click accept when the sim's club and the inferred club disagree (§6.1). Same pattern as the existing club-evidence note with Undo |
| Settings → Launch Monitor | `settings/LaunchMonitorPanel` | No new controls. Premier "Use the club selected in Premier" recommended on for play; GSPro follow defaults on |

### 12.3 Deliberately not added

- **Nothing during a round.** No prompts, overlays or notifications (principle 3).
- **No "play mode" in the rail.** Play is a context on ordinary sessions, consistent with the refactor away from the four modes.
- **No shot-tagging UI.** The only self-report is the optional "which shots felt tense?" tap on the reconcile scorecard (§7.3).

### 12.4 Minimum first cut

1. Context step in the setup wizard.
2. Quiet live mode.
3. Context filter on Tendencies (Home and report) and Sessions.
4. Round review.

Transfer view, work-on transfer status, play yardages and the mismatch note follow once enough play data exists to make them meaningful (§10).

---

## 13. Phasing

| Phase | Scope | Depends on |
|---|---|---|
| **P0 — Investigate play** | Three "you play, I watch" sessions in the existing style: **Premier course play** (does the snapshot exist? what does `history.json` hold on a hole?); **FSX2020 course play** (is `LastShot.CSV` written, is `Distance to Pin` live?); **GSPro v4** (already planned — its Block 4 is the decisive one). Plus the re-analysis-beside-the-game cost (GSPro Block 11) | Connectors as built |
| **P1** | Session context attribute + migration (existing sessions → Block/Assessment); auto-context from the connector; context facet in Sessions and Tendencies | — |
| **P2** | Round/Hole/Stroke model; GSPro `currentRound.dat` feed; inferred segmentation for Premier/FSX2020; reconcile; round review | P0, `GcQuadGsPro` |
| **P3** | Club inference (extend `lm_club_evidence.h`) + mismatch flagging; shot-type classifier incl. swing length; per-club practice distances as reference | Per-club distance tracking |
| **P4** | Practice-vs-play comparison; context-aware Tendencies; work-on transfer; routine timing | Tendencies page, work-ons |
| **P5** | Tier C: course card mode and practice games over any connector (TTS) | P2–P3 |
| **P6** | HR strap; pressure index; rehearsal swings; strokes-gained | P2 |
| **P7** | Tier D course play via iOS companion | Companion app |

**For the author's own rig (GC Quad):** range practice is fully covered today through Premier or FSX2020, so the practice side of every comparison already exists. The play side depends on P0. If Premier course play writes the snapshot, Watch Me Play works on Premier with no new connector. If it doesn't, and GSPro's `LastShot` has course club data, GSPro becomes the play simulator and Premier the practice one. If neither holds, play sessions are ball-only (`history.json` or `LastShot`), PinPoint's own swing capture carries the comparison, and club comes from inference.

---

## 14. Open questions / to verify

1. **Does Premier course play write `SessionLiveSnapshot`?** Documented as range-only; drills write nothing. If not, what does `history.json` hold during a round (`courseName`, `holeNumber`, `totalDistanceMeters`, world positions), and is its club name reliable there (it is wrong in drills)?
2. **Does FSX2020 course play write `LastShot.CSV`,** with `Distance to Pin` populated? Not recorded in the reference doc.
3. **GSPro `LastShot` club data on the course** — `gspro_v4_connector.md` §7 Q1. No-go for that connector if absent.
4. **Can a round's structure from `currentRound.dat` be joined to `LastShot` rows reliably?** It has no per-shot wall-clock time; join by sequence and ball speed, with retakes (`ShotResult = 1`) and `ClubIndex` 26 records accounted for.
5. **Re-analysis cost beside a running game** (GSPro §7 Q10). Decides live vs end-of-round analysis (§5.2).
6. **Chip threshold — carry or total?** Proposed carry, configurable; carry is the simulator's flight model in Premier and GSPro.
7. **Partial-shot thresholds** (−2σ carry, 90 % speed) need calibrating on labelled partials.
8. **Rehearsal detection** reliability, and separating rehearsals from shots Premier dropped.
9. **Strokes-gained baseline** — which amateur baseline, its licence, whether it can ship in a GPL application.
10. **Vendor conversations.** GSPro before v1.0 (`gspro_v4_connector.md` §1.1). Premier's files are undocumented and may change with any update; Foresight's EULA position is recorded in the Premier doc §7 Q7.
11. **Naming.** "Watch Me Play" for the feature; "Play" for the context family; "Transfer" for the comparison view.
