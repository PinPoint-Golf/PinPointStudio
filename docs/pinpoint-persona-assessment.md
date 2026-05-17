# Pinpoint — Persona UX Assessment

**Version:** 0.1 (draft)
**Relates to:** Pinpoint UX Design Document v0.1
**Purpose:** Assess the current UX design against three distinct user archetypes to identify gaps, risks of abandonment, and design priorities.

---

## Table of contents

1. [The three personas](#1-the-three-personas)
2. [Persona 1 — The teaching pro](#2-persona-1--the-teaching-pro)
3. [Persona 2 — The enthusiastic amateur](#3-persona-2--the-enthusiastic-amateur)
4. [Persona 3 — The wealthy newcomer](#4-persona-3--the-wealthy-newcomer)
5. [Cross-cutting gap analysis](#5-cross-cutting-gap-analysis)
6. [The five most important design gaps](#6-the-five-most-important-design-gaps)
7. [The structural tension](#7-the-structural-tension)

---

## 1. The three personas

### Persona 1 — Teaching pro ("The authority who needs convincing")

- **Golf knowledge:** Expert. Decades of lesson delivery, strong intuitive mental model of the swing.
- **Technology:** Comfortable with launch monitors (Trackman, GC Quad). Sceptical of new jargon and unfamiliar workflows.
- **Kinematics vocabulary:** Deep intuitive knowledge of swing mechanics, weaker on formal terminology (X-factor, pelvis sway, proximal-to-distal sequence).
- **Motivation:** Wants to give better lessons. Professional credibility is everything.
- **Key trait:** High patience for golf complexity, low patience for technology friction. Calibration workflows that require physics knowledge will cause abandonment.

### Persona 2 — Enthusiastic amateur ("The self-improver who over-researches")

- **Golf knowledge:** Solid player. Understands their own game, follows instruction content closely.
- **Technology:** Home simulator owner or regular user. Very comfortable with launch-monitor data. Has used or considered HackMotion or Dewiz.
- **Kinematics vocabulary:** Has watched TPI content, partial vocabulary. Knows what X-factor and P-positions mean but cannot always translate numbers into practice changes.
- **Motivation:** Self-improvement. Wants to understand *why*, not just *what*.
- **Key trait:** The closest match to the design's implicit primary user. High engagement potential if the data layer connects meaningfully to the practice tee.

### Persona 3 — Wealthy newcomer ("The convert who bought the whole shop")

- **Golf knowledge:** Beginner to early intermediate. Enthusiastic, fundamental swing still forming.
- **Technology:** Comfortable with consumer apps and smart home devices. No familiarity with SDK, calibration, or camera concepts.
- **Kinematics vocabulary:** Little to none. May have heard terms in lesson contexts but cannot interpret them as metrics.
- **Motivation:** Progress and enjoyment. Wants to feel the hardware investment is paying off.
- **Key trait:** Has money and time but no reference framework for any aspect of the system. Setup friction is a critical abandonment risk. The AI Coach is the primary value proposition for this persona.

---

## 2. Persona 1 — The teaching pro

### First impressions

The empty state serves this persona reasonably well. "An open-source workshop for analysing the golf swing" reads as serious and purposeful rather than consumer-facing fluff. The privacy statement ("Nothing connects to the cloud unless you set it up") resonates strongly with someone who keeps client data professionally. The three secondary entry points (connect a camera, pair IMUs, read the docs) suit a pro who will want to verify hardware before committing a student's time.

The athlete-picker design maps well to a coaching practice. Multiple athletes, searchable, with last-session age visible at a glance — this mirrors how a pro manages a lesson book. The "Import roster" option has significant potential value for coaches onboarding from existing systems.

**Early friction:** The new athlete form asks for height and weight as "recommended" fields, explained as helping to "normalise body kinematics." A teaching pro will not recognise this as a meaningful benefit statement. It reads as bureaucracy with unexplained consequences. The explanation needs to speak their language: something like "used to scale the body-segment traces to your student's proportions."

### Session start

The configure-session screen works well for this persona. Club selection, session label, and sensor selection all map to decisions a pro makes before a lesson anyway. The trigger selector (audio / motion / manual) is a concept they will understand immediately and want to control.

The pre-session readiness screen is where this persona is most at risk. The calibration progress bar ("14 / 22 frames"), the reprojection error metric, and the coverage-map heatmap are unfamiliar concepts. A teaching pro has never had to think about what a reprojection error is — with Trackman, you place the device and it works. The current design asks them to monitor a physics metric and judge whether it is converging toward an acceptable threshold. This is not a reasonable expectation.

The design currently puts too much interpretive burden on the user. This persona needs a single unambiguous signal: **ready or not ready**. The reprojection error is a tool for engineers and power users — it should be available as a disclosure, not as the primary reading.

The calibration presets system is a strong feature for this persona once discovered. A pro with a fixed studio setup calibrates once and loads the preset every session thereafter. The discoverability path is poor, however — the preset list sits in the left device rail of the readiness screen, a location a first-time user has no reason to look. It should be surfaced as the first option when a calibration is flagged as due: "Load your last preset, or recalibrate?"

### In-session comprehension

The session view works well on the video side — slow-motion replay is a familiar concept. The replay panel and carousel are intuitive.

The kinematic sequence chart on the secondary monitor is potentially the most valuable element of the entire application for this persona. It shows the proximal-to-distal pattern they already teach — "hips first, then shoulders, then arms, then club" — in a visual format that confirms or refutes what they are saying in lessons. They may not use the words "kinematic sequence" but they absolutely teach the concept. **This chart is the screen that should anchor marketing and onboarding for this persona.** The current design buries it on the secondary monitor without explaining its connection to concepts they already know.

Metric cards will be partially familiar and partially opaque. "Attack angle" is Trackman vocabulary — they know it. "X-factor" they will have heard. "Pelvis sway at P6" may be unfamiliar as a labelled metric even if they understand the physical concept. The design currently provides no contextualisation — metric values appear without explanation. A brief tooltip or "what is this?" disclosure per metric would help without cluttering the dashboard.

The P-position labels on the replay timeline (P5, P6) are a TPI convention. They are not universal in coaching. A label-convention preference, or plain-language equivalents, should be considered.

### AI Coach mode

This is simultaneously the most exciting and the most uncomfortable feature for this persona.

Exciting because it could produce diagnostic observations they currently generate manually, faster and with more data coverage. Uncomfortable because their professional identity is the expert — having an AI produce coaching observations that they then deliver creates an ambiguous question about who is actually coaching.

The design frames Claude as a diagnostic tool rather than a replacement coach. The session-summary "Claude observation" is a single paragraph, not a lesson plan. That framing is correct. But the experience depends heavily on whether this pro is using Pinpoint for their own clients (where the AI reasoning thread visible in the coach session view might undermine their authority if a student can see it) or as a personal research tool.

A "coach-only" display mode — where the kinematic dashboard is visible to the student but Claude's conversation thread is not — would address this directly. The coach receives the AI's diagnostic input privately; what the student sees is the pro's interpretation of it.

### Long-term value

High, if the persona survives calibration setup. A pro with a fixed studio and a working preset is gaining kinematic diagnostic capability that their launch monitor does not provide. The trend analysis across sessions is directly useful for demonstrating student progress over time.

### Risk of abandonment

**Medium-high, concentrated in the setup phase.** The calibration flow is the most likely exit point. If this pro reaches the checkerboard calibration without a clear "you're done" confirmation — and instead sees a metric they don't understand moving toward a threshold they can't interpret — they will put the checkerboard down and return to their Trackman.

---

## 3. Persona 2 — The enthusiastic amateur

### First impressions

This persona is the closest match to the design's implied primary user. The empty state's tone ("open-source workshop") feels appropriate — this person has probably already read about Pinpoint on a forum or subreddit. The three secondary entry points are in the right order of operations for them: connect camera, pair wrist sensors (already ordered), read the docs.

The athlete picker is slightly over-engineered for someone who is the only athlete in their installation. They will create one athlete record and never use the multi-athlete features. The "Recent" cards at the top of the picker will quickly collapse this to a one-click step, but the first time through it feels like unnecessary bureaucracy.

### Session start

This persona is the most likely to engage thoroughly with the configure-session screen. They will label sessions deliberately, select clubs intentionally, and think carefully about trigger mode. They will appreciate that the sensor toggle is visible — they have used HackMotion or Dewiz and understand that wrist sensors are a separate data stream that can be added or removed.

The pre-session readiness screen is where this persona excels. They have the technical patience to understand what calibration is doing, they will read the coverage heatmap, and they will find the reprojection error progress bar satisfying rather than alarming. This is the screen that was implicitly designed for them.

The calibration presets are a particularly well-matched feature. They will set up their simulator bay, save it as "Simulator bay", and load it every subsequent session with one click.

### In-session comprehension

This persona will engage deeply with the session view but will hit a specific comprehension wall: **the gap between metric names and what to do about them.** They know what X-factor is from TPI content. They can see their number is 38°. They have no reference point for whether 38° is good, bad, or irrelevant for a player of their type and ability. The baseline delta ("+3° vs avg") tells them they are improving, but not whether they are improving toward something worth aiming for.

The current design provides deltas against the athlete's own historical baseline. That is useful for trend-tracking but insufficient for goal-setting. This persona needs **normative reference ranges** — "for a player with your swing speed and handicap, a typical X-factor range is 35–48°." The design has no mechanism for this. It is a significant gap for self-directed amateur use.

The AI Coach is the most compelling feature for this persona and the one most likely to sustain long-term engagement. They are accustomed to consuming instruction content — YouTube, TPI, HackMotion app analysis. Pinpoint's AI Coach is a meaningful step change: commentary on their specific swing, not generic instruction. The STT/TTS interface maps well onto their content-consumption habits.

Compare mode is also a strong fit. Self-coached improvers constantly ask "what did I change?" The A/B overlay with auto-generated difference annotation is precisely what this person has been attempting manually by scrubbing two clips side by side.

The wrist analysis mode is where this persona has the most pre-existing context. They understand the angle conventions, they know what flexion and extension mean at the top and at impact. The live dial display and trace replay map well onto the HackMotion experience. One gap: HackMotion provides a reference overlay of professional player data within the replay view — a key feature for self-coaching. The Pinpoint design has a "reference overlay picker" in the control bar but the visual design of what that overlay looks like in the replay panel is not yet defined.

### Long-term value

**Very high.** This persona has the motivation, the appropriate skill set, and the time to invest. Pinpoint is close to their ideal tool. The long-term risk is that the self-coaching loop hits a ceiling — they can see their kinematics improving but stop knowing what to work on next. The AI Coach is the answer to that ceiling, but its value depends heavily on the quality and specificity of the analysis it produces.

### Risk of abandonment

**Low, with one exception.** The exception is the metric comprehension problem. If this persona faces a screen of numbers they cannot translate into practice changes, they will revert to simpler tools — their launch monitor, their HackMotion app — that give clearer action signals even if they are less comprehensive. The fix is normative reference ranges and better "what this means for your game" framing. More data is not the answer.

---

## 4. Persona 3 — The wealthy newcomer

### First impressions

This is where the current design has its most significant gap, and where the stakes are highest — this person has made a substantial hardware investment and will feel that investment is wasted if the software does not work for them.

The empty-state welcome card is written in clear English and the primary CTA is obvious. But "An open-source workshop for analysing the golf swing" is slightly alienating at the outset. "Open-source" and "workshop" both signal a technical, self-directed tool. This persona did not buy a workshop; they bought a system they expect to guide them.

The three secondary cards ("Connect a camera", "Pair wrist IMUs", "Read the docs") are the first moment where this persona may feel out of their depth. "Read the docs" as a secondary CTA implies that documentation is the expected route to understanding, which is the opposite of what this persona wants. They want the application to explain itself.

### Session start

The new athlete form is a significant early hurdle. This persona is asked for their handicap — they may only have played a dozen rounds and may not have an official handicap yet. They are told height and weight will "normalise body kinematics." They do not know what this means or whether skipping it will damage their analysis. They will almost certainly leave the recommended fields blank and feel anxious about whether that was the right decision.

The reassurance copy ("Pinpoint learns from your first sessions") does not fully land for this persona because they do not yet understand what a session is or what "learning" means in this context.

The configure-session screen presents a second wall. Club selection asks them to choose from Driver / 3-wood / 5-iron / 7-iron / Wedge. A beginner may not know which club is appropriate to analyse their swing with — they are still deciding which clubs to buy. The capture-trigger descriptions (audio / motion / manual) are technically framed in a way that assumes the user already knows what they want.

The pre-session readiness screen will cause this persona to seek external help. The calibration checklist, coverage heatmap, and reprojection error progress bar all require mental models this persona does not have. A beginning golfer asked to "move the checkerboard through these poses" while watching a heatmap fill up will either perform the action mechanically (producing poor calibration) or abandon the flow.

The current design assumes the user understands what calibration is and why it is necessary. This persona needs a plain-language explanation before they engage with the process: what they are about to do, why it matters, and what "done" looks like in terms they can understand.

### In-session comprehension

If this persona gets through setup, the session view presents a further barrier: the metrics dashboard.

"Attack angle: -2.4°" means nothing to someone who is still working on making consistent contact. "X-factor: 38°" is equally opaque. "Pelvis sway at P6: 0.4"" requires knowing what P6 means, what pelvis sway means, and why less than half an inch matters. The current design provides no interpretation layer — just numbers, baseline deltas, and status pills.

The kinematic sequence chart on the secondary monitor — the most powerful visualisation in the application for an expert — is actively counterproductive for this persona. They cannot read it, and encountering something uninterpretable alongside an activity they are still learning is demoralising rather than informative.

The AI Coach mode is the most important part of the application for this persona, and the feature most likely to produce delight once reached. They are not self-coaching — they want to be coached. Claude reviewing their swing, asking them to hit balls, and explaining in plain language what it observed is the experience they paid for.

The critical question is whether the AI Coach output is written in golf language or data language. "Your hips are sliding toward the target instead of rotating, which is causing your arms to get stuck behind you" is what this persona needs. "Pelvis sway at P6 is 3.2" above your baseline" is not. This is partly a prompting and system-design question — the coach mode's output register should adapt to the athlete's experience level, which is inferrable from their profile.

### Long-term value

**Potentially very high, contingent on surviving setup.** This persona has time and money — the two things that are partially substitutable for technical knowledge. If they engage a professional to set up and calibrate the hardware, they can use the analytical and coaching features as a consumer rather than an operator. The AI Coach is genuinely compelling for them in that mode.

The athlete history screen — trend charts, session list, baseline progression — is something this persona will engage with deeply as they improve. Progress visibility is the emotional fuel for a new convert, and Pinpoint has unusually capable tools for showing it.

### Risk of abandonment

**Very high, before the first capture is made.** The setup sequence is not designed for this persona. They will encounter the calibration screen without the mental model needed to navigate it. Whether they persist depends on two things: whether the eventual AI Coach experience is compelling enough to justify the effort, and whether they have access to someone (a coach, a tech-savvy friend, a professional installer) who can resolve the setup friction on their behalf. Given this persona has money and motivation, both are plausible — but only if the post-setup experience delivers clearly enough that they understand what they were setting up for.

---

## 5. Cross-cutting gap analysis

### Setup and onboarding

| Area | Teaching pro | Enthusiastic amateur | Wealthy newcomer |
|---|---|---|---|
| Empty state clarity | Good | Good | Partial — "workshop" / "open-source" signals wrong audience |
| Athlete form | Partial — "normalise body kinematics" is jargon | Good | Poor — may lack handicap; purpose of fields is unclear |
| Configure session | Good | Good | Poor — club choice and trigger mode require prior knowledge |
| Camera calibration | Poor — reprojection error is opaque | Good — has patience and mental model | Poor — no concept of what calibration is or why |
| Calibration presets | Good once found — low discoverability | Great fit | Irrelevant until the purpose of presets is understood |

### In-session experience

| Area | Teaching pro | Enthusiastic amateur | Wealthy newcomer |
|---|---|---|---|
| Live preview | Good | Good | Good — seeing oneself in slow motion is universally compelling |
| Replay + P-positions | Partial — "P5" not a universal convention | Partial — TPI users know; others don't | Poor — no frame of reference |
| Kinematic sequence chart | Great fit — shows what they already teach | Partial — readable with context | Poor — uninterpretable without explanation |
| Metric cards (values + deltas) | Partial — knows some metrics, not all | Partial — knows values, lacks normative context | Poor — no interpretation layer |
| Wrist analysis (dials + traces) | Partial — understands physically, unfamiliar with UI | Good — maps to HackMotion experience | Poor — no context for what wrist angles mean |
| AI Coach conversation | Partial — useful but threatens authority positioning if visible to student | Great fit | Great fit — if output is in golf language, not data language |
| Compare mode | Good | Great fit | Partial — useful for before/after but cannot read the diff metrics |

### Long-term engagement

| Area | Teaching pro | Enthusiastic amateur | Wealthy newcomer |
|---|---|---|---|
| Athlete history / trends | Great fit — demonstrates student progress | Good | Good — progress tracking is highly motivating |
| Session summary | Good | Good | Partial — metrics opaque, but Claude observation helps |
| Multi-athlete management | Great fit | Over-featured for single-user setup | Over-featured for single-user setup |
| Normative reference ranges | Missing | Missing | Missing |
| Metric explanations / tooltips | Missing | Partial need | Missing |

---

## 6. The five most important design gaps

### Gap 1 — Calibration needs a clear "you're done" signal readable by any persona

The current design exposes the reprojection error metric as the primary indicator of calibration completeness. This works for the enthusiastic amateur and fails for the other two. The fix is a traffic-light status model layered on top of the technical metric. The system makes the call on whether the calibration is good enough and states it plainly:

> "Calibration complete — quality: good"

The raw reprojection error, frame count, and coverage map remain visible for power users via a disclosure. This single change materially improves the experience for all three personas without removing capability for the one who needs it.

### Gap 2 — Metric cards need an interpretation layer

Numbers with baseline deltas are not actionable without a reference frame. "X-factor: 38°, +3° vs avg" tells the teaching pro and the enthusiastic amateur different things, and tells the newcomer nothing at all.

Each metric card needs a tooltip or disclosure — accessible via a small `?` icon — that answers three questions:

1. What is this measuring?
2. What range is typical for a player of this type?
3. What does a high or low value mean for the golf swing?

The AI Coach could generate these contextual explanations dynamically from the athlete's profile and session history, which would be a meaningful differentiator over static lookup tables. This feature would benefit all three personas, with the newcomer benefiting most.

### Gap 3 — P-position labels should be configurable or explained

"P5" is a TPI convention. It is not universal in coaching, and it is completely opaque to newcomers. The replay panel timeline should either:

- Label positions in plain language ("late downswing" rather than "P5") as the default, with P-positions available as a preference.
- Offer a label-convention setting in Settings → App (TPI P-positions / plain language).
- Provide a brief explanation on hover or tap.

The underlying data and positions are identical regardless of label choice. This is a display decision with no architectural cost.

### Gap 4 — AI Coach output needs to adapt to the athlete's experience level

The system prompt behind Claude's responses in coach mode should incorporate the athlete's profile — handicap, experience level, session history — so that output naturally calibrates to the appropriate register.

A beginner should never see "pelvis sway at P6 is 3.2" above your baseline" in Claude's coaching output. They should see: "Your hips are sliding toward the target instead of rotating — this affects your balance and is likely causing the club to arrive late." Both descriptions refer to the same kinematic observation. The register difference is the entire gap between useful and alienating for this persona.

For the teaching pro, the same system prompt modification should produce more technically detailed output — one that respects their expertise rather than simplifying it.

This is a system-prompt design and athlete-profile design question as much as a UX question, but it needs to be specified before the AI Coach is built.

### Gap 5 — The newcomer needs a guided-start mode for their first session

The configure-session screen, pre-session readiness screen, and calibration flow are all designed for self-directed exploration. The newcomer needs a different entry path for their first session with a new athlete profile.

A contextual "first session setup" mode, triggered when an athlete has no session history, would:

- Recommend a starting club ("We suggest starting with a 7-iron — it's the most neutral club for swing analysis").
- Recommend a sensor set ("Start with just the cameras — you can add wrist sensors later").
- Explain what is about to happen before each step ("We're going to make sure the camera knows where it is in the room. This takes about two minutes.").
- Provide a plain-language "done" confirmation after calibration.

After the first session, the app trusts the user to navigate the standard flow. The guided mode is a once-only experience, not a permanent simplification.

---

## 7. The structural tension

The deepest issue the persona assessment reveals is that Pinpoint is currently designed around **data collection and analysis**, but the reason all three of these personas would purchase the hardware is to **improve their golf**.

The enthusiastic amateur is the closest to the design's implicit user — comfortable with the data layer, motivated by self-improvement. But even they need a bridge between numbers and the practice tee.

The AI Coach is that bridge. It translates kinematic data into coaching language. The design document treats it as one of four coordinate analysis modes. The persona assessment suggests it should be understood as the **primary value proposition** — the feature that makes the data layer meaningful — with Swing, Wrist, and Ground Forces as data-collection instruments that feed it.

This does not require restructuring the mode hub or the session architecture. It does require that the AI Coach is positioned, onboarded, and communicated differently across the three personas:

- **Teaching pro:** A diagnostic assistant that generates objective kinematic observations, giving lessons an evidence layer.
- **Enthusiastic amateur:** A personal coach that responds to their specific swing, available at any time, that closes the loop between data and practice.
- **Wealthy newcomer:** The reason they bought the kit — an always-available instructor that meets them where they are and shows them how to improve.

The hardware and data infrastructure supports all three framings. The UX and the AI Coach's output design determine which framing each persona actually experiences.

---

*Assessment produced against Pinpoint UX Design Document v0.1. Should be reviewed and updated as the design evolves, particularly as the AI Coach output design and normative reference range features are specified.*
