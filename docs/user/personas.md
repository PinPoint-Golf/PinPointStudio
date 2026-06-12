# Pinpoint — User Personas

**Version:** 1.0  
**Last updated:** May 2026  
**Maintained by:** Project lead  

This document defines the three primary user personas for Pinpoint and articulates
their needs, failure modes, and design implications. It is written for two audiences:
product decision-making (what to build and why) and developer onboarding (who you
are building for). Every significant design decision in Pinpoint should be traceable
to one or more of these personas.

The document also carries a current implementation assessment for each persona —
an honest account of where the software is serving them well and where the gaps
remain. This section should be updated as the product evolves.

---

## Contents

1. [How to use this document](#1-how-to-use-this-document)
2. [Persona 1 — The Teaching Pro](#2-persona-1--the-teaching-pro)
3. [Persona 2 — The Enthusiastic Amateur](#3-persona-2--the-enthusiastic-amateur)
4. [Persona 3 — The Wealthy Newcomer](#4-persona-3--the-wealthy-newcomer)
5. [Cross-cutting design principles](#5-cross-cutting-design-principles)
6. [Structural tension — the core challenge](#6-structural-tension--the-core-challenge)
7. [Implementation assessment — current state](#7-implementation-assessment--current-state)
8. [Updating this document](#8-updating-this-document)

---

## 1. How to use this document

When making a product decision — whether it is a new feature, a UI layout choice,
a default setting, or a piece of copy — ask yourself: which persona does this serve,
and does it help or harm the other two? The three personas have meaningfully different
needs, and design that serves all three simultaneously is the highest-quality outcome.
Design that serves one at the expense of another is an acceptable trade-off when
made consciously. Design that ignores all three is a defect.

When reviewing a pull request, ask whether the change is consistent with the persona
it most affects. When writing a Claude Code prompt, include the relevant persona
context so the AI understands who the work is for.

**Naming convention.** The three personas are referred to throughout the codebase
and design documents as:

- **TP** — Teaching Pro
- **EA** — Enthusiastic Amateur  
- **WN** — Wealthy Newcomer

---

## 2. Persona 1 — The Teaching Pro

### Profile

**Name for reference:** The Teaching Pro (TP)  
**Primary motivation:** Give better lessons. Credibility is everything.

A PGA or independent teaching professional with years of experience delivering
lessons. They have a deep, largely intuitive understanding of the golf swing —
they know what good sequencing looks like, they can feel it in a student's movement,
and they have vocabulary for it that their students understand. They are not,
however, fluent in the formal biomechanical terminology that research-oriented
coaching tools use (X-factor, pelvis sway at P6, proximal-to-distal sequencing).
They know these concepts — they just use different words.

They have used launch monitors professionally for years. Trackman, GC Quad, Flightscope
— these are the tools they already trust. When a Trackman goes down, they know what
to do. When a new tool gives them numbers they don't recognise, they will not use it.

They teach multiple students per day. Their time is valuable and structured. A tool
that takes 15 minutes to set up per session will not survive contact with their
working day. Setup must be fast, reliable, and repeatable. Calibration must be
something they do once (for a fixed studio), not every session.

### What they need from Pinpoint

**The kinematic sequence chart is their killer feature.** They already teach
proximal-to-distal sequencing — "hips first, then shoulders, then arms, then club."
Seeing this as a chart that confirms or refutes what they're saying in a lesson is
genuinely novel capability that their Trackman cannot provide. This is the screen
that should be in the marketing material for this persona.

**Metric cards that speak their language.** "Attack angle" is Trackman vocabulary —
they know it. "X-factor" they've heard. "Pelvis sway at P6" is likely unfamiliar as
a labelled metric even if they understand the physical concept. Every metric needs a
tooltip or brief explanation that answers: what is this measuring, and what does a
high or low value mean in plain coaching terms?

**Calibration presets, not calibration every day.** A teaching pro with a fixed
studio setup should calibrate once and load a named preset thereafter. The calibration
workflow should be transparent and fast; it should never interrupt a lesson in
progress.

**Multi-athlete management.** They see five to ten students per day. The athlete
picker must be fast — the three most-recently active athletes should be one click
away. Import from a roster (CSV) matters for onboarding a coaching practice.

**Professional authority — not a co-teacher.** The AI Coach is useful for diagnostic
observations, but this persona's professional identity is the expert. Having Claude's
observations visible to the student during a lesson could undermine their authority.
A "coach-only" display mode — where the kinematic dashboard is visible to the student
but Claude's reasoning is not — is a critical future requirement for this persona.

### Failure modes

**The calibration screen.** If the teaching pro hits the checkerboard calibration
workflow without a clear "you're done" signal — and instead sees a physics metric
they don't understand (reprojection error) declining toward a threshold they can't
interpret — they will put the checkerboard down and go back to their Trackman. This
is the highest-probability exit point for this persona.

**Unfamiliar metric vocabulary.** A screen full of metric names they don't recognise
communicates "this tool was not made for me." P-position labels (P5, P6) are TPI
convention — not universal. Every metric must be either immediately recognisable or
immediately explainable.

**Setup time.** If connecting a second student takes more than two minutes from the
previous session, they will not use the app in a professional context.

### Design heuristics for this persona

- **The default should always be the safest option for a professional.** If a setting
  has a default, it should be the one that a teaching pro would choose without being
  told what it does.
- **Never expose engineering metrics as primary UI.** Reprojection error, ring fill
  fraction, max inter-arrival time — these belong in the System Resources screen, not
  in the session start flow.
- **Calibration presets are a first-class feature**, not a power-user shortcut.
- **The kinematic sequence chart is the headline.** It should be the first thing a
  new teaching pro sees when they complete their first session.

---

## 3. Persona 2 — The Enthusiastic Amateur

### Profile

**Name for reference:** The Enthusiastic Amateur (EA)  
**Primary motivation:** Self-improvement. Wants to understand *why*, not just *what*.

A committed golfer who takes their game seriously. They play regularly, follow
instruction content closely (YouTube, TPI, specific coaches), and invest in their
improvement. They likely own or regularly use a home golf simulator. They are very
comfortable with launch monitor data — they know their attack angle, spin rates,
and carry distances. They may have experimented with a HackMotion or Dewiz wrist
sensor. They have watched TPI content and can discuss X-factor and kinematic
sequence in casual conversation, though they cannot always translate data into
practice changes without help.

This persona is the closest match to the design's implicit primary user. They are
technically patient — they will read documentation, configure settings, and work
through a calibration flow if they understand why it matters. They will set up
calibration presets for their simulator bay and remember to use them. They will label
sessions and use the comparison tools.

They are self-coaching. No teaching pro is looking over their shoulder. This means
they need Pinpoint to close the loop between seeing a number and knowing what to
do about it — and that loop is harder to close than it appears from the data alone.

### What they need from Pinpoint

**Normative reference ranges.** "X-factor: 38°" tells them nothing without context.
"For a player with your swing speed, typical X-factor is 35–48°" tells them whether
to work on it. The current design provides deltas against the athlete's own baseline —
useful for tracking trends, insufficient for goal-setting. This is a significant
gap for self-directed improvement.

**The compare mode is a great fit.** Self-coached improvers constantly ask "what
did I change?" Overlaying two captures, seeing the metric diff, and having the
auto-generated annotation ("pelvis 3.2" forward at P6") is precisely what this
person has been trying to do manually by scrubbing two separate clips side by side.

**The wrist analysis mode maps to their existing mental model.** If they've used
HackMotion or Dewiz, they understand the angle conventions. The live dial display
and trace replay should map to that experience. The professional reference overlay
(a touring pro's wrist angle pattern) is a key feature for self-coaching that the
current design has as a placeholder.

**The AI Coach is the long-term retention feature.** They are accustomed to consuming
instruction content. Pinpoint's AI Coach is a step change — commentary on their
specific swing, not generic instruction. This is what keeps them engaged beyond the
first few sessions.

**The risk of metric overload.** If this persona looks at a screen of numbers they
cannot translate into a practice change, they will revert to simpler tools — their
launch monitor, their HackMotion app — that give clearer action signals even if less
comprehensive. More data is not always better.

### Failure modes

**The metric comprehension gap.** They can see their X-factor is 38°. They don't
know if that's good. The current implementation shows deltas against their own
baseline — which is useful after ten sessions but useless in the first five, when
the baseline is still forming.

**Self-coaching ceiling.** They improve, their numbers get better, and then they
plateau. Without guidance on what to work on next, they run out of self-directed
questions to ask. The AI Coach is the designed answer to this ceiling, but its value
depends entirely on the quality and specificity of its output.

**The professional reference overlay.** HackMotion shows you how your wrist angles
compare to tour players. Pinpoint currently has this as a placeholder chip in the
wrist analysis control bar. Until it's implemented, this persona's wrist analysis
experience is less capable than the tool they already have.

### Design heuristics for this persona

- **Every metric needs a "what does this mean?" path.** A `?` icon opening a brief
  explanation in coaching language is the minimum. AI-generated contextual
  explanations from the athlete's profile is the target.
- **Comparison is a core workflow, not a power-user feature.** The compare mode
  carousel and diff panel should be prominent, not buried.
- **The AI Coach output must be in golf language, not data language.** "Your hips
  are sliding toward the target" is useful. "Pelvis sway at P6 is 3.2″ above
  baseline" is not.
- **Self-directed workflows must have a clear next step.** Every session summary
  should answer: "what should I work on next?" Even a placeholder answer is better
  than none.

---

## 4. Persona 3 — The Wealthy Newcomer

### Profile

**Name for reference:** The Wealthy Newcomer (WN)  
**Primary motivation:** Progress and enjoyment. Wants to feel the investment is paying off.

An enthusiastic recent convert to golf who has the means and time to invest seriously
in the sport. They have purchased or are interested in purchasing a complete home
simulator setup, including Pinpoint. They may have had a few lessons but their
fundamental swing is still forming. They do not know their handicap (they may not
have an official one yet), they cannot distinguish between clubs by loft angle, and
they have no frame of reference for whether 38° of X-factor is something to improve
or something to celebrate.

They are comfortable with consumer technology — smartphones, smart home devices,
subscription apps. They are not comfortable with anything that resembles software
installation or device configuration. "Connect a camera over USB3" is a meaningful
sentence to them; "run a checkerboard calibration to establish extrinsic parameters"
is not.

They will likely pay for professional setup — someone to install the cameras, run
the calibration, and hand them a working system. Their primary interface to Pinpoint
is then as a consumer, not as an operator.

### What they need from Pinpoint

**The AI Coach is the reason they bought the kit.** This is the feature that
justifies the investment to this persona. They want to be coached — not to read
numbers and figure out what to do. Claude reviewing their swing, asking them to hit
balls, and explaining in plain language what it saw and what to work on is the
experience they paid for. Everything else is infrastructure that the AI Coach runs on.

**Progress visibility.** The athlete history screen — trend charts, session list,
baseline progression — is something this persona will engage with deeply as they
improve. Seeing that their club speed has improved 6 mph over three months is
emotionally rewarding in a way that session-level metrics are not. This is the hook
for long-term retention.

**Plain language throughout.** Every label, tooltip, screen title, and AI Coach
output must be written for someone who does not know what P5 means. "Late downswing"
is better than "P5." "Your hips are moving forward instead of rotating" is better
than "pelvis sway at P6 is 3.2″ above baseline."

**The setup path must be survivable.** If this persona encounters the checkerboard
calibration screen without prior context — without knowing what calibration is,
what a checkerboard is for, or what "14 of 22 frames" means — they will either do
it mechanically (producing a poor calibration) or call for help. The guided first-run
mode (flagged in the UX assessment as a pending design item) is critical for this
persona.

### Failure modes

**Everything before the first capture.** The configure-session screen, the
pre-session readiness screen, and the calibration flow are not designed for this
persona. They will hit the checkerboard calibration before they understand why it
exists. This is the highest-probability exit point — not from the session, but from
the product entirely.

**Metric screens without interpretation.** A screen of numbers with green and red
delta indicators communicates "this tool was made for engineers" to this persona.
Without an interpretation layer, every metrics screen is a source of anxiety rather
than insight.

**Complexity before value.** If they see the System Resources screen (ring fill
fractions, max inter-arrival times, event buffer states) before they have hit a
single ball successfully, they will question the investment. The complexity of the
underlying system must be entirely invisible to this persona during normal use.

### Design heuristics for this persona

- **The AI Coach output register must adapt to the athlete's experience level.**
  The system prompt driving Claude's responses should incorporate the athlete's
  profile (handicap, session count, session history). A beginner should receive
  plain-language coaching; an advanced player should receive technical detail.
  Both access the same underlying data.
- **A guided first-session mode is non-negotiable.** The first time an athlete with
  no session history starts a session, the setup flow should explain each step in
  plain language, recommend defaults, and confirm readiness in terms the athlete
  can verify themselves.
- **Complexity must be opt-in.** The System Resources screen, the calibration
  coverage heatmap, the reprojection error metric — all of these are correct and
  useful, but they should require active navigation to reach. They must not appear
  in the path of a first-time user.
- **Progress is the emotional fuel.** Every session should end with a clear,
  human-readable statement of what happened and whether it was good. The session
  summary Claude observation is the designed mechanism for this.

---

## 5. Cross-cutting design principles

These principles emerged from the persona analysis and apply to every feature in
Pinpoint regardless of which persona it primarily serves.

### The calibration signal must be binary

All three personas need a single unambiguous answer to "is calibration good enough?"
The current calibration flow exposes the reprojection error metric as the primary
indicator. This works for EA and fails for TP and WN. The fix is a traffic-light
model: the system makes the judgment and communicates it plainly
("Calibration complete — quality: good"). The engineering detail remains accessible
as a disclosure.

### Metric cards need an interpretation layer

Numbers with deltas are not actionable without a reference frame. Every metric in
the UI needs a path to understanding: what is this measuring, what is a typical range
for a player of this type, and what does a high or low value mean for the golf swing.
This applies to all three personas. The teaching pro needs it because some metrics
are unfamiliar. The enthusiastic amateur needs it because they lack normative context.
The wealthy newcomer needs it because all of it is new.

### The AI Coach output must adapt to the athlete

The system prompt behind Claude's responses in coach mode should incorporate the
athlete's profile. A beginner should receive "your hips are sliding toward the target
instead of rotating." An advanced player should receive "pelvis sway at P6 is 3.2″
above your baseline, consistent with early extension." Both descriptions point at
the same measurement. The output register is what changes.

### P-position labels should be explainable or configurable

"P5" is a TPI convention. It is not universal. The replay timeline labels should
either default to plain language ("late downswing") or provide an explanation on
hover. A label convention preference in Settings is acceptable for power users.

### The left rail is a mode switcher, not a history trail

The rail switches context. Pressing the Home button resets navigation history
entirely. Pressing a mode button clears forward history. This reflects the UX
principle that Home means "start over" and mode buttons mean "I am now thinking
about something different." Browser-style back/forward navigation operates within
this model, not against it.

### No engineering metrics in the primary flow

Reprojection error, ring fill fraction, max inter-arrival time, bounds violations —
all correct, all useful, none appropriate in the path of a first-time user. These
belong in the System Resources screen, reachable from Home but never encountered
uninvited.

---

## 6. Structural tension — the core challenge

The deepest tension the persona analysis reveals is this: **Pinpoint is currently
designed around data collection and analysis, but the reason all three personas would
buy the hardware is to improve their golf.**

The EA is the closest match to the design's implicit primary user — comfortable with
the data layer, motivated by self-improvement. But even they need a bridge between
the numbers and the practice tee.

The AI Coach is that bridge. It translates kinematic data into coaching language.
The architecture treats it as one of four analysis modes. The persona analysis
suggests it should be understood as the primary value proposition — the feature that
makes the data layer meaningful — with Swing, Wrist, and Ground Forces as
data-collection instruments that feed it.

This does not require restructuring the mode hub or the session architecture. It
requires that the AI Coach be positioned, onboarded, and communicated differently
for each persona:

- **Teaching Pro:** A diagnostic assistant that generates objective kinematic
  observations, giving lessons an evidence layer. The coach remains the authority;
  Claude provides the data.
- **Enthusiastic Amateur:** A personal coach available at any time, closing the loop
  between data and practice. The thing that keeps them coming back.
- **Wealthy Newcomer:** The reason they bought the kit — an always-available
  instructor that meets them where they are, speaks plain language, and shows them
  how to improve.

The hardware and data infrastructure support all three framings. The UX and the AI
Coach's output design determine which framing each persona actually experiences.

---

## 7. Implementation assessment — current state

This section assesses the current implementation (May 2026) against each persona.
It is written to be honest about gaps. Update it when significant features land.

### What is built

As of this writing, Pinpoint has the following implemented:

**Shell and navigation:**  
Three-aesthetic theme system (Instrument, Editorial, Studio) with light/dark modes,
bundled variable fonts, scale-aware `sp()` sizing. Left rail navigation with
Home, Swing, Wrist, GRF, Coach, Play (dev mode), System, and Settings buttons.
Persistent header with screen name, back/forward navigation (`NavigationController`
in C++), fullscreen toggle, and version display. History-aware navigation with
Home-resets-all semantics.

**Athlete management:**  
Full CRUD athlete management (`AthleteController`, `QSettings`-backed). Athlete
picker with recent cards and searchable list. New athlete form with Required /
Recommended / Optional field grouping. Athlete avatar in rail with initials.

**Home screen:**  
Single scrollable column with three sections: athlete identity, session launcher
(with type cards and club selector), and device readiness. Welcome state when no
athlete exists.

**Session setup:**  
Five-step session wizard (`ScreenSessionWizard`): session type, cameras, IMU setup,
verify, ready. Session type cards with camera/IMU requirement indicators.

**Devices and sensors:**  
Camera manager with multi-camera enumeration, pylon/Aravis/GenTL SDK support,
perspective label assignment. IMU manager with BLE scanning, multi-IMU support,
per-IMU zero/calibration. `ResourceMonitorController` aggregating camera, IMU,
and event buffer state. System Resources screen with device cards, source table,
and timeline chart.

**Data infrastructure:**  
Lockless ring-buffer event system (`EventBuffer`) with per-source statistics,
timeline index, and diagnostic snapshots. Full test suite (ASAN, TSAN, fuzz,
latency benchmark).

**Settings:**  
Full settings screen with panels for General, Cameras, IMUs, Audio, Appearance,
Displays, and Storage.

**Other:**  
Audio pipeline, STT (Whisper/AssemblyAI/Azure), TTS (Kokoro/Azure), video capture
with pose estimation (MediaPipe/MoveNet), ball detection.

---

### Persona 1 — Teaching Pro (TP)

#### What works well for them now

The **Home screen** serves this persona well on arrival. The three-aesthetic visual
design communicates professional seriousness without technical aggression. The
privacy statement ("Nothing connects to the cloud unless you configure it") earns
trust immediately.

The **athlete picker** maps directly to how a coaching practice works. The three
recent-athlete cards, session count, and last-seen timestamp are exactly the
information a coach scanning their lesson book wants to see.

The **settings system** is comprehensive and well-organised. Cameras, IMUs, audio,
and display configuration are all in one place with clear panel separation.

The **back/forward navigation** with Home-reset semantics is correct for this
persona. Clicking Home between students is a clean reset without navigating through
previous student data.

#### Where the gaps are

**Calibration UX is still engineering-facing.** The session wizard step 2 (cameras)
shows calibration age with "recalibration recommended" warnings, which is good. But
the calibration workflow itself (when triggered) exposes reprojection error as the
primary indicator. This persona needs a single green/red "calibration quality" signal.
The calibration preset system (`src/Gui/CamerasPanel.qml`) exists in the settings
panel but its discoverability from the session wizard is low — a first-time user has
no reason to look there.

**Metric interpretation layer does not exist.** No tooltips, no `?` disclosures, no
plain-language explanations appear on any metric card in the current implementation.
Every metric is a number with a delta. This is the most significant gap for the
teaching pro.

**The kinematic sequence chart is not yet implemented.** The session analysis screens
(`ScreenPlaceholder.qml` for Swing, Wrist, GRF, Coach) are stubs. The feature that
would most convince a teaching pro to adopt Pinpoint does not yet exist.

**P-position labels are hardcoded as "P5", "P6" etc.** in the session wizard type
card descriptions. No plain-language alternative or explanation exists.

**No "coach-only" display mode.** The AI Coach screen, when implemented, will show
Claude's reasoning thread to anyone looking at the screen. For a teaching pro using
this with students, this is a concern that must be addressed before the feature is
production-ready.

**Risk of abandonment:** Medium-high. The teaching pro is most likely to be lost at
the calibration step. Once past that, the home screen and athlete management will
hold them. They will become advocates if the kinematic sequence chart delivers what
it promises.

---

### Persona 2 — Enthusiastic Amateur (EA)

#### What works well for them now

This persona is the closest match to the current design's implicit user and is
served best overall.

The **Home screen session launcher** with type cards showing camera/IMU requirements
is exactly right for this persona. They understand the sensor requirements, they
will read the requirement indicators, and they will feel informed making the choice.

The **session wizard** five-step flow (type → cameras → IMU setup → verify → ready)
is correctly scoped. This persona has the patience and technical literacy to work
through it, and the step-by-step progression with clear headings gives them the
context they need.

The **IMU manager** with BLE scanning, device list, role assignment (lead/trail),
and zero calibration is well-matched to someone who has used HackMotion or Dewiz.
The `ImuVizView` live angle visualisation is a direct analogue of HackMotion's
live readout.

The **System Resources screen** will resonate with this persona. They will find the
ring fill fraction, source table, and timeline chart interesting rather than
overwhelming. The "System resources →" link from Home is appropriately placed —
available but not in the way.

The **theme system** (three aesthetics, light/dark, variable font loading) is
something this persona will engage with positively. They will cycle through themes,
prefer Editorial or Studio, and appreciate that the typography is clearly considered.

#### Where the gaps are

**Normative reference ranges are absent.** The athlete baseline (30-day rolling
average) will eventually provide within-self comparison, but there is no mechanism
for "how does this compare to players at my level?" This is the most significant
analytical gap for self-directed improvement.

**Session analysis screens are stubs.** `ScreenPlaceholder.qml` is shown for Swing,
Wrist, GRF, and Coach modes. This persona will reach these screens quickly and
find nothing. Until these are implemented, Pinpoint cannot serve the EA's core
use case.

**Capture carousel and compare mode do not yet exist.** The session analysis flow
(live capture → replay → compare) is designed but not implemented. This is the
workflow this persona will use most.

**The professional reference overlay** in wrist analysis is a chip in the control
bar design but not implemented. Until it exists, the wrist analysis experience is
less capable than HackMotion for self-coaching.

**The AI Coach system prompt is not tuned for register adaptation.** When the AI
Coach is implemented, the system prompt must incorporate the athlete's skill profile
to produce coaching language appropriate to their level. This is a prompt-engineering
requirement as much as a UX one.

**Risk of abandonment:** Low once session analysis is implemented. This persona will
tolerate the stub screens during early development if the setup flow and home
experience are good. They are likely early adopters and contributors.

---

### Persona 3 — Wealthy Newcomer (WN)

#### What works well for them now

The **welcome state on the Home screen** is the strongest part of the current
implementation for this persona. "An open-source workshop for understanding the golf
swing" is clear. The privacy statement earns trust. "Add your first athlete" as the
primary CTA is obvious and correctly sequenced. The three secondary cards (camera,
IMU, docs) are gentle entry points for those who want to prepare first.

The **athlete form** — with Required / Recommended / Optional grouping and the
reassurance copy ("Pinpoint learns from your first sessions") — is well-designed for
this persona. The grouping communicates that the required fields are genuinely the
only ones they must fill. The reassurance removes anxiety about leaving fields blank.

The **visual design** communicates quality. The typographic choices (Playfair Display
in the Editorial aesthetic, generous whitespace, restrained use of colour) position
Pinpoint as a premium product, which matches this persona's expectations for what
they've invested in.

The **back/forward navigation** helps. If this persona finds themselves somewhere
confusing, they can press `‹` to go back. The navigation history means they don't
lose their place.

#### Where the gaps are

**The calibration flow is the most significant risk of total product abandonment.**
The session wizard step 2 (cameras) will show this persona a calibration age and
possibly "recalibration recommended." Clicking "Recalibrate" will take them to a
flow that uses terms like "reprojection error" and "frame coverage." Without a
professional installation, this persona will be lost here. The guided first-session
mode (explaining what calibration is, why it matters, and providing a step-by-step
visual guide) is not yet implemented.

**The session type cards use terminology this persona cannot interpret yet.** "Ground
forces" and "kinematic sequence" are concepts they will learn over time, but seeing
them on day one without explanation is alienating. Each type card needs two to three
lines of plain English description of what the session will show them.

**Note:** The current `HmTypeCard.qml` *does* have a `description` property and the
Home screen *does* populate it with descriptions ("Capture golf shots with IMUs on
your spine and review your sequencing..."). This is a correct implementation. The gap
is that the session wizard's type selection step does not currently carry these
descriptions through — when the wizard confirms the selected type, the plain-language
description should remain visible.

**There is no mechanism for adapting AI Coach output to skill level.** When the AI
Coach is implemented, this persona must receive plain-language, encouraging coaching.
"Your hips are sliding toward the target instead of rotating" not "pelvis sway at P6
is 3.2 inches above your baseline." The system prompt architecture must account for
this.

**The settings screen is too visible.** The Settings rail button is prominent and
always visible. For this persona, encountering `ImusPanel.qml` with BLE device IDs,
firmware versions, and RSSI values in normal navigation would be destabilising. The
Settings screen should be reachable but not discoverable by accident. The current
implementation is fine in this respect — Settings is below the spacer and requires
deliberate navigation — but this must be maintained as the rail evolves.

**Risk of abandonment:** Very high before the first successful capture, especially
without professional installation. The calibration and device setup flows are the
critical path. Once past setup, the Home screen, athlete management, and eventual
AI Coach experience will serve them well — but getting there is not yet smooth
enough for an unsupported first-time user.

---

## 8. Updating this document

This document should be updated whenever:

- A significant feature lands that changes the experience for any persona
- A gap identified in the assessment is addressed
- New gaps are discovered through user testing or feedback
- The persona definitions themselves need refinement based on actual users

The implementation assessment in section 7 should be updated with each major release.
Previous assessments should be preserved with a date stamp rather than overwritten,
so the project history is readable.

**A note on the three personas as a design tool.** They are representations of
real user types, not rigid boxes. The teaching pro who is also technically curious
exists. The enthusiastic amateur who has no patience for setup exists. The wealthy
newcomer who turns out to be a quick learner exists. Use the personas to stress-test
decisions, not to justify them. If a feature genuinely serves all three, that is
strong evidence it is the right feature. If it serves only one and actively harms
another, that is a signal worth examining.
