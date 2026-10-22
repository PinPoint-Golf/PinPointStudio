# Posture chart: where your posture goes, P1 to P8, against good players and their patterns

*2026-10-22. Status: **design for Mark to review. Nothing built.** Mark asked for it on 22 Oct, during the
posture composites build (`posture_metrics_design.md`, measured in
`docs/research/data/posture/posture_composites_2026-10-22.md`). It replaces the same evening's
first-thinking draft. The draft's two views, its picture idea and its order of normative sources are
kept. Its pattern list, its pro "ghost body" and its setup match-up are replaced; §4.1 and §5.4 say why.*

Mark, 22 Oct 2026: the posture equivalent of the Wrist chart. It should provide **more insight**, show
**how this compares with pros**, and show **how the patterns match up to different swing styles**.

---

## 0. Summary

**The question it answers.** The posture composites built tonight answer *"did you lose your
posture?"*. This chart answers *"where did it go, by how much, compared with good players, and which
good players' pattern is yours?"*

**The decisions in this design:**

1. **It is a section of Charts, not a page of its own.** A **Posture** section appears under the
   curves on This shot and on Session whenever a posture reading is among the curves on show. This
   follows the Wrist section as Mark reworked it on 16 Oct (`wrist_motion_into_charts.md` §8.1).
   The Posture preset already draws the curves.
2. **Change from address is the default, and the only view compared with good players.** Changes
   travel between camera set-ups; absolute angles do not. An absolute reading is compared with a
   published band only on a calibrated or protocol-placed camera (§7.4).
3. **Six rows, each one honest about where it can be trusted:**
   - head height
   - hip depth (the hips toward or away from the ball)
   - trunk bend
   - lead knee
   - trail knee
   - pelvis height

   Head height is the test of "standing up". Trunk bend and pelvis height carry a ⚠ at P3–P5, where
   the turning body moves the joint points they are read from (§2.3).
4. **Two patterns that good players use, and one free style choice:**
   - **Level**
   - **Squat and push**
   - the trail knee **kept or released** going back, which is free in both patterns

   Early extension, standing up, and an early or deep dip are **departures, never patterns** (§4).
   The draft's "sink going back" pattern and its setup match-up are dropped (§4.1).
5. **A "what moved" line at the top and at impact.** It separates **hips in** (early extension: the
   hips move toward the ball under a level head) from **standing up** (the head rises). Tonight's
   data shows these are different movements: Nick loses 13.5° of bend with his head dead level (§2.2).
6. **The bands come from published measurement first.** Kim et al. 2021 (20 tour professionals,
   14 swing events) gives the trunk and both knees. Evans et al. 2008 gives head height. Sportsbox
   tour ranges give hip depth and pelvis height. Each row is labelled measurement or coaching claim
   and keeps its own provenance (§6, §7.1).
7. **The norm model needs no schema change for v1.** Per-position measures follow the wrist precedent
   (`m_leadWristFlexExt_p1…p8`). Pattern bands are rows under two new context nodes, following the
   precedent of `archetype_bowed` / `archetype_cupped`. One new measure flag, `rigSensitive`,
   carries the honesty rule (§7.3, §7.4).
8. **The picture uses no pro body.** It shows the golfer's own down-the-line frames at address, top
   and impact, with the coach lines frozen at address. The pattern band is drawn as offsets of the
   golfer's **own** address lines, for example "good players' hips sit within this strip at impact"
   (§5.4).

**The three golfers, as a worked example (§4.6).** The mapping agrees with Mark's coach labels on all
three:

| Golfer | Nearest pattern | Trail knee | Where they leave it | Mark's label |
|---|---|---|---|---|
| Mark | Level | released | from P5: hips 4.5 → 7.6 cm toward the ball by impact | agrees |
| Nick | Level | released | from P4: hips +4 cm going back, +6.5 cm at impact; head level | agrees |
| Jess | between Squat and push and Level (Squat and push nearer) | kept | P3–P4: head 9.5 cm down at the top, lead knee +36°; back inside the band by P6 | agrees |

What the data adds to the labels:

- **Mark and Nick's "standing up going back"** shows in the hips moving toward the ball while the
  trail leg straightens. Their heads stay level.
- **Jess's dip** is mostly her lead knee folding. She keeps her trunk bend better than the published
  tour average.

---

## 1. What we have, honestly

### 1.1 The readings, per position

Every posture quantity is already a series over the swing. The **phase grid** (`swing_phasegrid.json`,
written beside each analysed swing) already holds each one's value at every P-position. So the chart needs no
new producer to draw P1–P8. The committed CSVs from tonight carry only P1, P4 and P7 and the extremes.
The per-position numbers below were read from the phase grids of the re-analysed copies (Appendix A).

| Row | Camera | Series | Unit | Read P1–P8? | Caveat |
|---|---|---|---|---|---|
| Head height | face-on | `headLift` | cm (inter-ear ruler) | yes, incl. P8 | none known; the honest test of standing up |
| Hip depth | DTL | `pelvisThrust` | cm | P1–P7 (bridged after impact) | moves 1.9 cm between two pose runs (V5) |
| Trunk bend | DTL | `spineForwardBend` | ° | P1–P7 | joint chord, not the back line; ⚠ P3–P5 (§2.3) |
| Lead knee | DTL | `leadKneeFlexion` | ° | P1–P8 | seen behind the trail leg |
| Trail knee | DTL | `trailKneeFlexion` | ° | P1–P8 | none known |
| Pelvis height | face-on | `pelvisLift` | % stance | P1–P7 | hip joints migrate at the top: ⚠ P3–P5 until the belt-line producer |
| *(not a row)* face-on trunk rise | face-on | `trunkRise` | % | — | held: failed V1, and moves with the shoulder turn |
| *(not a row)* side tilt, sway | face-on | `secondaryAxisTilt`, `headSway`, `pelvisSway` | | | owned by their own conditions; side tilt growing is the swing working (§8) |
| *(address only)* arm hang, head pitch, balance | DTL | | | P1 | absolute and rig-sensitive (§7.4) |

### 1.2 What tonight taught, which this design must respect

1. **Absolute 2-D angles move with the camera.** Mark sees all three golfers as "marginally too bent"
   at address, yet they read 36.5°, 38.3° and 29.4°. Jess's side-tilt curve sits about 8° below Mark's
   at every position. The heel-to-toe proxy reads Jess 78 % toward her toes. **Changes from address
   travel between rigs far better.**
2. **Within one golfer the spread is small; between golfers it is large.** Bend lost at impact has an
   SD of 1.0–1.6° within a golfer. The golfers' medians range from 2° to 13.5°. A session's spread
   against a band is readable. A single swing against a band is mostly that golfer's pattern plus noise.
3. **The hip-joint to shoulder-joint line is not the spine once the shoulders turn.** Nick read 9° more
   upright at the top while his TPI back line (tailbone to the back of the neck) did not change.
4. **Trail-knee straightening going back is common in good swings** (GOLFTEC: "most players
   straighten their trail leg in the backswing to some degree – some a little, some a lot",
   [scramble.golftec.com](https://scramble.golftec.com/blog/2015/03/straight-knee-golf-swing/)).
5. **Head height is the honest test of standing up.**

---

## 2. What the three golfers' curves show

### 2.1 Median change from address, per position

Medians are per golfer. Mark's figures are all clubs; his 7-iron-only interquartile ranges are in
Appendix A and do not change the picture. n is per row: Mark 106–166, Nick 27–28, Jess 21–23 (hip
depth 7).

| Row | Golfer | P2 | P3 | P4 | P5 | P6 | P7 | P8 |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| Head height (cm) | Mark | 0.1 | 0.0 | 0.8 | −0.2 | 0.8 | 2.4 | 6.6 |
| | Nick | 0.2 | 0.5 | −0.1 | −0.4 | −0.1 | 0.1 | 3.8 |
| | Jess | −2.6 | −5.2 | **−9.5** | −8.6 | −7.3 | −6.0 | 2.1 |
| Hip depth (cm, + toward ball) | Mark | −0.3 | 1.0 | 3.0 | 4.5 | 6.5 | **7.6** | - |
| | Nick | 2.6 | 2.9 | 4.1 | 4.7 | 5.6 | **6.5** | - |
| | Jess (n 7) | −0.3 | −0.4 | −1.8 | 0.9 | 3.3 | 0.6 | - |
| Trunk bend (°) | Mark | −3.6 | −8.3⚠ | −7.2⚠ | −4.4⚠ | −5.0 | **−10.2** | - |
| | Nick | −3.2 | −3.9⚠ | −8.5⚠ | −7.5⚠ | −9.4 | **−13.5** | - |
| | Jess | 1.8 | 3.0⚠ | 2.1⚠ | 2.9⚠ | −0.3 | −2.2 | - |
| Lead knee (° flex) | Mark | 3.9 | 14.2 | 14.2 | 13.0 | 4.4 | −3.0 | −15.0 |
| | Nick | 1.6 | 9.3 | 16.6 | 3.0 | −0.8 | −4.8 | −11.4 |
| | Jess | 6.1 | 19.4 | **36.0** | 23.4 | 2.5 | −9.3 | −7.9 |
| Trail knee (° flex) | Mark | −9.7 | −17.9 | −18.3 | −6.3 | 7.5 | 14.8 | 17.7 |
| | Nick | −17.1 | −21.7 | −21.0 | −12.7 | −8.2 | −7.5 | 2.4 |
| | Jess | −0.9 | −1.1 | 1.0 | 9.8 | 11.4 | 10.5 | 0.8 |
| Pelvis height (% stance) | Mark | −9.3 | −16.9⚠ | −20.4⚠ | −13.8⚠ | −7.7 | −3.9 | - |
| | Nick | −4.9 | −6.0⚠ | −5.3⚠ | −2.5⚠ | −1.6 | 5.2 | - |
| | Jess | −8.8 | −14.8⚠ | −18.6⚠ | −16.6⚠ | −9.1 | −4.4 | - |

### 2.2 What the curves say that the composites could not

- **Two ways to lose trunk bend, and the head tells them apart.** Nick's bend falls 13.5° by impact
  while his head stays within 0.5 cm of address at every position from P2 to P7. His hips move
  6.5 cm toward the ball. The trunk is getting more upright because the **hips move in under a level
  head**, not because he stands up. Within Nick's swings, more hip depth goes with more bend lost
  (r −0.55, n 27); head height does not (r −0.23). Mark shows the same mostly, plus a small rise:
  head +2.4 cm at impact and +6.6 cm by P8. TPI makes the same split. Early extension is "any forward
  movement (thrust) of the lower body towards the golf ball during the downswing"
  ([mytpi.com](https://www.mytpi.com/improve-my-game/swing-characteristics/early-extension)). Loss of
  posture is "any significant alteration from your body's original set up angles"
  ([mytpi.com](https://www.mytpi.com/improve-my-game/swing-characteristics/loss-of-posture)). The
  chart's "what moved" line (§5.3) is built on this split.
- **"Standing up going back"** in Mark's and Nick's swings is the hips moving 3–4 cm toward the ball
  by the top while the trail knee straightens 18–21°. Their heads do not rise (+0.8, −0.1 cm). The
  trunk-bend change at the top is partly the turn (§2.3).
- **Jess's dip is her lead knee.** By the top her lead knee has flexed 36° more than at address. The
  tour average is about 13° (§3). Her head is 9.5 cm lower and her trunk 2° more bent. From P5 she
  rises steadily to 6 cm below address at impact, then above it by P8. Her trail knee holds going
  back and flexes about 10° in the downswing. That is a sit-down-and-push shape whose sit came
  **before** the top instead of after it.
- **Jess keeps her trunk bend better than the average tour player.** She loses 2.2° by impact; the
  published average is 5.9° (§3.1).

### 2.3 Rows that need a ⚠ at some positions

- **Trunk bend at P3–P5.** The joint chord reads the shoulder midpoint. That point sits a few
  centimetres in front of the spine at address and swings out of the down-the-line picture as the
  shoulders turn 80–90°. That alone reads as 5–6° "more upright" with the back unchanged. The
  turning shoulder points are also partly hidden at the top. Nick's 9° against an unchanged back
  line is the measured case. By impact the shoulders are 30–40° open and the effect is near 1°.
  - **The fix** (stage 4, §10): read the TPI back line (tailbone to the back of the neck) at every
    P-frame from the person mask, as the address marks already do once. It is also the line closest
    to the published C7-to-sacrum trunk angle (§3.1).
- **Pelvis height at P3–P5.** The hip-joint keypoints migrate at the top: −20.7 %, −17.5 % and −5.3 %
  of stance on nearly every swing. The belt-line producer `pelvisLiftBelt` (designed, no producer)
  removes this.

A reading under a ⚠ is still drawn and still has its number. It carries the warning and its short
gate words, as the Charts pattern requires. It is left out of pattern matching (§4.5).

---

## 3. What good players do: the published numbers

Each source below is marked **[measured]** or **[claim]**. A measured source has a stated sample and
system. A claim is a coaching statement or a vendor range with no stated sample. Most quotes were read
in full text. Where only an abstract was read, the entry says so.

### 3.1 Trunk and knees through the swing: Kim et al. 2021 [measured]

Kim SE, Lee J, Lee SY, Lee HD, Shim JK, Lee SC. "Small changes in ball position at address cause a
chain effect in golf swing." *Sci Rep* 2021; 11: 2694
([doi:10.1038/s41598-020-79091-7](https://doi.org/10.1038/s41598-020-79091-7), PMC7846748), supplement
Tables S11–S13 and S17. The sample was 20 male professionals hitting a 5-iron, filmed with Vicon at
250 Hz.

- **Trunk angle:** the C7 to mid-PSIS (sacrum) line against vertical in the sagittal plane. That is
  close to TPI's tailbone-to-neck line and **not** our joint chord.
- **Knee flexion:** 180° minus the included thigh–shank angle.
- **Position mapping:** the paper's events are club-shaft angles. Mapping them to P-positions is
  ours: B90 ≈ P2, TC (the club's change of direction) ≈ P4, D225 ≈ early P5, D90 ≈ P6.
- **SD:** the SDs are between players at each event. The paper does not report the SD of the
  change from address.

| Event | Trunk ° | Δ | Lead knee ° | Δ | Trail knee ° | Δ |
|---|---:|---:|---:|---:|---:|---:|
| Address (P1) | 40.6 ± 2.7 | 0 | 23.5 ± 7.8 | 0 | 27.2 ± 7.4 | 0 |
| B90 (≈P2) | 39.4 ± 3.0 | −1.2 | 28.6 ± 8.8 | +5.1 | 25.9 ± 8.0 | −1.3 |
| TC (≈P4) | 39.9 ± 3.6 | −0.7 | 36.7 ± 8.7 | +13.2 | 26.6 ± 8.0 | −0.6 |
| D225 (≈early P5) | 42.0 ± 3.4 | **+1.4** | 43.8 ± 7.8 | **+20.3** | 32.3 ± 10.1 | +5.1 |
| D90 (≈P6) | 36.8 ± 4.0 | −3.8 | 29.5 ± 7.4 | +6.0 | 30.5 ± 10.0 | +3.3 |
| Impact (P7) | 34.7 ± 4.3 | **−5.9** | 22.4 ± 7.2 | −1.1 | 27.8 ± 8.7 | +0.6 |

What the table shows:

- **Tour players hold their trunk bend to the top and lose about 6° by impact,** with a small
  sit-down (+1.4°, lead knee +20°) at the start of the downswing. That sit-down is a mild squat and
  push in the average professional.
- **With a 5-iron these players barely straighten the trail knee** (−0.6°). The between-player SD
  is 8°, and GOLFTEC's tour data (above) says many straighten it a lot. The trail knee is a style
  (§4.2).
- **The centre of mass** (S17) moves 3.3 cm toward the heels by the hips' change of direction and is
  back to 0.9 cm short of address at impact. Good players' weight does not move toward the ball.

The same lab's 2018 paper is the PMID already cited in `norms.json`: Kim SE et al., *J Sports Sci
Med* 2018; 17: 589, [PMID 30479527](https://pubmed.ncbi.nlm.nih.gov/30479527/), PMC6243633. It reports
11 KPGA professionals at address only: trunk 41.5 ± 2.9°, lead knee 21.5 ± 7.4°, trail knee
24.4 ± 5.5°. Moving the ball one ball-width closer or further moves the trunk about 1.3°.

### 3.2 Head height and the pelvis: Evans et al. 2008 [measured]

Evans K, Refshauge KM, Adams R, Barrett R. "Swing kinematics in skilled male golfers following
putting practice." *JOSPT* 2008; 38(7): 425–433
([doi:10.2519/jospt.2008.2617](https://doi.org/10.2519/jospt.2008.2617)). The sample was 29 skilled
men (handicap 1.6 ± 2.4) hitting a 5-iron, with electromagnetic sensors on the forehead, T3 and S2.

| From address | Top | Impact |
|---|---:|---:|
| Head, vertical (cm) | **+2.0 ± 3.9** | **−3.5 ± 3.1** (lower) |
| Head, toward the ball (cm) | +1.3 ± 2.6 | −2.2 ± 2.0 |
| Pelvis, toward the ball (cm) | +5.4 ± 5.2 | +1.9 ± 2.5 |
| Pelvis flexion (°, absolute) | 16.0 ± 6.8 (from 23.1 ± 7.1) | 4.9 ± 6.1 |

Caveats:

- The tables say only "backswing" and "downswing"; that these are the top and impact values was read
  from the paper's Figure 2.
- The pelvis sensor sits on the sacrum. As the pelvis turns, it moves for reasons other than the
  hips moving in, so the +5.4 cm at the top is not comparable with our hip midpoint.
- The head figures are the published basis for "good players' heads are slightly **lower** at impact
  than at address".

### 3.3 The pelvis tucks under through impact [measured]

The pelvis tilts backward ("tucks under") by about 18° from address to impact in skilled players.
Two independent studies agree:

- **Okuda, Gribble and Armstrong 2010.** *J Sports Sci Med* 9: 127–133, PMC3737954,
  [Table 2](https://www.jssm.org/tablejssm-09-127.xml/table002.xml). 13 skilled players (handicap
  0.8), driver. Pelvic tilt went from −11.2 ± 6.5° (forward) at address to +6.6 ± 5.2° at impact; the
  low-skilled group reached 0.0 ± 8.1°.
- **Evans 2008.** Pelvis flexion fell from 23.1° to 4.9°.

We have no producer for pelvic tilt. It explains why the lower back's line changes through impact
even in a good swing.

### 3.4 Vendor tour ranges: Sportsbox AI [claim]

Sportsbox says its tour ranges come from "over a hundred PGA and LPGA tour professionals", scaled for
height, sex and hip width. The per-metric sample and club are not stated, so these are claims, not
measurements.

- **Hip depth ("pelvis thrust") at impact, men, irons:** "マイナス2.3cm〜プラス4.3cm", i.e. −2.3 to
  +4.3 cm ([Golf Digest Japan, Apr 2026](https://www.golfdigest-minna.jp/_ct/17833886)). An amateur
  in the same article read +9.2 cm. A 2023 range was −1.8 to +2.8 cm
  ([golfdigest-minna.jp](https://www.golfdigest-minna.jp/_ct/17667199)).
- **Pelvis height, from its lowest point to impact (men, driver):** +4.8 to +9.9 cm
  ([golfdigest-minna.jp](https://www.golfdigest-minna.jp/_ct/17762141)). The same article says
  Justin Thomas's "head keeps dropping" ("頭が下がり続けている") while the amateur's rises.
- **Pelvis and chest height** (Golf Digest, Kerr-Dineen, Nov 2024,
  [australiangolfdigest.com.au](https://www.australiangolfdigest.com.au/useful-golf-swingfacts-sportsbox-ai-graph)):
  - the pelvis is "almost two inches lower than it started" early in the downswing and "one inch
    higher than where it started" at impact;
  - the chest "drops down just two inches" and "rises back up about an inch through the ball";
  - chest bend is "about 30 degrees" at setup and at impact.
- **Chest bend at address, men:** +34° to +45°
  ([golfdigest-minna.jp](https://www.golfdigest-minna.jp/_ct/17871011)).

### 3.5 Other measured studies

- **Chu, Sell and Lephart 2010.** *J Sports Sci* 28: 1251–1259
  ([doi:10.1080/02640414.2010.507249](https://doi.org/10.1080/02640414.2010.507249)). 308 golfers of
  mixed ability, driver. The pelvis rises 9 ± 6 cm by impact (the reference instant is not stated).
  Lead knee 41.6° at the top and 16.2° at impact. Not elite.
- **Zheng et al. 2008** ([PMID 18563677](https://pubmed.ncbi.nlm.nih.gov/18563677/), abstract only).
  Trunk forward tilt at the top: PGA 31 ± 4°, LPGA 25 ± 4°.
- **Meister et al. 2011,** *J Appl Biomech* 27: 242–251. 10 professionals. Shoulder-line tilt at
  impact 25 ± 3°, pelvis-line tilt 12 ± 3°. This is side tilt, kept out of the posture patterns (§8).
- **Not verified, and not used:**
  - Mears et al. 2016 on 2-D against 3-D trunk angles (top −6.5 ± 3.6°); seen in a search summary only.
  - Gulgin et al. 2014 on the TPI toe touch and early hip extension; seen only through a citing preprint.
  - Horan et al. 2010 (male and female skilled golfers); paywalled, so no numbers.

### 3.6 Where the sources disagree, and what follows

- **Trunk bend lost by impact.** Kim's 3-D data say about 6°. Sportsbox and GOLFTEC say chest bend
  returns to about its setup value: "return to the setup numbers (or close to them) again in the
  downswing" ([GOLFTEC](https://scramble.golftec.com/blog/2015/04/explaining-head-drop-golf-swing/)).
  - The datums differ. Kim measures a trunk line to the sacrum; the vendors measure a chest segment.
  - The band at impact is therefore seeded wide, at **−6° to 0°**.
  - **Our fault line (5° lost from address) sits at the published tour mean.** That is Question 2 (§9).
- **Head height at impact.** Evans measures heads 3.5 cm lower than address. Sportsbox describes a
  chest about 2 inches (5 cm) down in the downswing and 1 inch up by impact. No source has a good
  player's head **above** address at impact.
  - **Our head corridor (μ 0) treats "level" as ideal.** By the published data, Mark's +2.4 cm is
    about 6 cm above the tour mean (about 2 SD) and Nick's +0.1 cm about 3.6 cm above it.
  - The published band agrees with Mark's labels **better** than our μ 0 corridor does.
- **Trail knee going back.** Kim: held (−0.6 ± 8°, 5-iron). GOLFTEC: "most players straighten their
  trail leg … some a little, some a lot". In the 1990s keeping it bent was taught as an ideal.
  Bobby Jones and Sam Snead kept little flex
  ([Australian Golf Digest](https://www.australiangolfdigest.com.au/controversial-golf-swing-move-bent-trail-knee-golf-iq)).
  So the trail knee is a **style** (§4.2).

### 3.7 Coaching descriptions of the patterns [claim, unless marked]

- **Squat and push (using the ground).**
  - Sasho MacKenzie [measured: 40 players, handicap +2 to 20]: "the greater the vertical force under
    the lead foot when the shaft is perpendicular to the ground, the faster the club moves"
    ([golf.com](https://golf.com/news/press-here-pushing-off-your-front-foot-can-add-extra-yards-off-the-tee/)).
  - Han, Como, Kim … Kwon 2019, *Sports Biomech* 18(2): 115–134 [measured: 63 golfers, handicap
    ≤ 3, force plates; abstract only]: lead-foot force components correlated with clubhead speed.
  - Sean Foley (Golf Digest 2011, search snippet only): "Many long-ball hitters drop several inches as
    they start the downswing. Tiger Woods has been doing it throughout his career."
  - GOLFTEC on the head drop: "a good move when executed correctly". Overdone, "you can have large
    problems hitting the ground before the ball"
    ([scramble.golftec.com](https://scramble.golftec.com/blog/2015/04/explaining-head-drop-golf-swing/)).
  - Justin Thomas "posts up so hard on his lead leg at impact that his feet practically leave the
    ground" ([golf.com](https://golf.com/instruction/leverage-the-ground-for-more-power-off-the-tee/)).
- **Early extension is a fault in good players, not a style.** TPI: "an amazing 67% have early
  extension" of over 90,000 golfers screened, while "99 % of them don't have early extension" of 100+
  tour players ([mytpi.com](https://www.mytpi.com/articles/swing/early-extension-swing-characteristic)).
  This is TPI's own screening count, observational. Brian Mogg calls it a compensation: "pushing the
  hips toward the ball to help the club find a path to impact", after a steep downswing or weight
  stuck back
  ([golf.com](https://golf.com/instruction/two-reasons-golfers-early-extension-fix/)).
- **Physical causes** (TPI screens, all claims):
  - The overhead deep squat: heels lifting means "it is almost impossible for them to maintain their
    posture during the downswing … thrust their lower body towards the golf ball"
    ([mytpi.com](https://www.mytpi.com/articles/screening/the-overhead-deep-squat-test)).
  - Pelvic tilt: "often associated with … loss of posture … early extension, reverse spine angle,
    sway, and slide"
    ([mytpi.com](https://www.mytpi.com/articles/swing/understanding-and-assessing-pelvic-tilt-in-the-golf-swing)).
- **Found nowhere:** a source for "a head dip going back makes the club shallow", which is Mark's
  reading of Jess. It stays Mark's coaching claim and is labelled as his.

---

## 4. The patterns

### 4.1 What changed from the draft, and why

- **Setup patterns are dropped.** The draft's hinge-dominant and knee-dominant setups were meant as a
  match-up of equally valid styles. The sources do not support that. They describe one good setup
  (hinge from the hips, knees "flexed, not bent", arms hanging), whose numbers change with the
  **club**: about 29° of shoulder bend with the driver against about 40° with irons (GOLFTEC, Kim).
  "Too much knee flex combined with a more upright torso" is a TPI cause of S-posture, a fault
  ([mytpi.com](https://www.mytpi.com/improve-my-game/swing-characteristics/s-posture)). Setup also
  rests on absolute angles, which our cameras cannot compare between rigs. So setup gets a **club-keyed
  band shown only on a trusted camera** (§7.4), not patterns.
- **"Sink going back" is dropped as a pattern.** Good players' heads go slightly **up** by the top in
  Evans (+2.0 ± 3.9 cm). No source describes a dip going back as a style. Mark calls Jess's dip her one
  flaw. A drop of a few centimetres stays inside the Level band; beyond that it is a departure (D4).
- **Squat and push is kept, and moved to where the literature puts it.** The drop comes in
  **transition** (P4→P5/P6), not going back, and the push comes through impact.
- **The trail knee becomes a free style choice** rather than a pattern of its own (§3.6).

### 4.2 The list

Two patterns and one style choice. The signatures are changes from address in the rows of §1.1.

| | **Level** | **Squat and push** |
|---|---|---|
| In a coach's words | "stays in posture": the swing centre keeps its height and distance from the ball until the ball has gone | "uses the ground": sits down into the lead side as the downswing starts, then pushes up through the ball |
| Head height | about level to the top; a little lower by impact | level to the top; **3–6 cm lower at P5–P6**; still at or below address at P7; rises after the ball |
| Hip depth | back from the ball a little at the top; at or within a few cm of address at impact | the same |
| Trunk bend | held to the top; about −4° at P6, −6…0° at P7 | held to the top; **slightly more bent at P5**; −6…0° at P7 |
| Lead knee | flexes about +13° by the top; back to about address at impact; straightens after | flexes **more** in transition (+20° or beyond at P5); **straightens hard through impact** ("posting up") |
| Pelvis height | close to address | **dips 3–5 cm at P5–P6, then rises** above address by impact (the push) |
| Named in the sources | the average of Kim's 20 professionals is close to it; Evans' 29 skilled men | Tiger Woods (drop), Justin Thomas (post up and jump) |
| Valid while | the head and hips stay in their bands | the dip comes **after** the top, the head is not above address at impact, and the hips do not move toward the ball |

**The trail knee, either way:**

- **Kept:** loses only a few degrees going back. This is Kim's 5-iron average.
- **Released:** straightens 10–20° or more going back. GOLFTEC calls it common ("some a lot"); Hogan,
  Jones and Snead are the classic examples.

Both are valid in both patterns. Straightening is a fault **only together with** the hips moving
toward the ball or the head rising going back (D3).

### 4.3 Departures: faults that are never a pattern

A departure is named when the golfer leaves their pattern's band in the way the departure describes.
Each one maps onto a condition the pack already has, so the chart and Session diagnostics say the
same thing (§6.2). **None of these is ever offered as a pattern.**

| | Departure | Signature in our rows | Conditions that name it |
|---|---|---|---|
| D1 | **Hips in** (early extension) | hip depth above the band from P5 on, head level or rising | `early_extension`, `loss_of_posture` |
| D2 | **Standing up** | head above the band at P6–P7, trunk bend lost | `coming_out_of_it`, `loss_of_posture` |
| D3 | **Standing up going back** | hips toward the ball and/or head up by P3–P4, with the trail leg straightening | `pelvis_thrust_backswing`, `head_rise_backswing`, `loss_of_posture_backswing` |
| D4 | **Early or deep dip** | head (and pelvis) below the band at P2–P4 | `head_drop_backswing`, `posture_sinks_backswing` |
| D5 | **Dip then rise** | D4, then the head above address by P7 | D4's conditions plus D2's; the edge `posture_sinks_backswing → loss_of_posture` exists |
| D6 | **Staying down into the ground** | head and pelvis still below the band at P7, bend gained at P5–P6 | `posture_sinks_impact`, `diving` |
| D7 | **Lead knee folds going back** | lead knee above the band at P3–P4 | **no condition today** (Question 5) |

The **combinations** are what separate faults from styles, as Tier 2 does in the wrist design
(`wristmotion_assessment_design.md` §7.2):

- A transition dip is Squat and push. A dip **before** the top is D4. A dip that is then paid back
  by rising above address is D5.
- A straightening trail knee is a style. Trail knee **plus** hips in is D3.
- Losing trunk bend with the head level and the hips in is D1. Losing it with the head rising is D2.

### 4.4 Matching a golfer to a pattern

1. **What is compared.** The session median of each row's change from address at P2–P7, from the
   rows read on the capture. Excluded from matching:
   - P8, which is bridged on the DTL rows;
   - readings under a ⚠ (trunk bend and pelvis height at P3–P5);
   - the trail knee, which is a style, not a pattern feature.

   A face-on-only session matches on head height and pelvis height alone, and says so. A DTL-only
   session matches on hip depth, trunk bend and knees.
2. **The distance to each pattern:**

       d = √( mean over the rows and positions read of ((x − μ) / σ)² )

   μ and σ come from the pattern's band (§7.1).
3. **The words:**

   | Result | What the page says |
   |---|---|
   | the two patterns within 0.15 of each other | "between Level and Squat and push" |
   | d < 1 | "a close match" |
   | d 1–2 | "nearest" |
   | d > 2 | "no pattern fits closely; nearest is …" |
4. **Where you leave it.** In time order, the first position where a row's |z| against the nearest
   pattern exceeds 2, named by the departure (§4.3) that the row and the direction describe. Later
   exits are listed after it.
5. **Trail knee.** "Kept" if it loses less than 8° by P4, "released" otherwise. Said in the words;
   it never counts against a pattern.
6. **Session against shot.**
   - The pattern is a property of the golfer, so it is chosen from the session (at least 5 shots;
     with fewer, from the shot itself, and said so).
   - This shot is then judged against the session's pattern.
   - The reader can override it, as the wrist bands can be ("Compare with: Auto → Level · Level ·
     Squat and push").

### 4.5 Worked example: Mark, Nick and Jess

Computed from the §2.1 medians against the seed bands of §7.1 (Appendix B).

| Golfer | d Level | d Squat and push | Nearest | Trail knee | First exits (z > 2) |
|---|---:|---:|---|---|---|
| Mark | **1.32** | 1.47 | Level | released (−18°) | P5 hips +4.5 cm (z 2.4), P6 +6.5 (2.6), P7 +7.6 (2.6); bend at P3–P4 shown with ⚠, not counted |
| Nick | **1.53** | 1.71 | Level | released (−21°) | P4 hips +4.1 cm (2.0); P5 hips +4.7 (2.5), bend −7.5⚠; P6 hips +5.6 (2.2); P7 bend −13.5 (−2.5) |
| Jess | 1.50 | **1.39** | between the two; Squat and push nearer (gap 0.11, under the 0.15 close-call rule) | kept (+1°) | P3 head −5.2 cm (−2.1); P4 head −9.5 (−2.9), lead knee +36° (3.3); inside both bands from P6 |

**Against Mark's labels:**

- **Mark and Nick, "lose their posture into early extension coming down":** agrees. Both leave Level
  through the hips (D1). Mark adds a small rise from impact, +2.4 cm and then +6.6 cm by P8. That is
  above the published band at P7 (about 6 cm above the tour mean), so it is named as part of D2.
  Nick's head never rises: his loss of bend is all hips. The chart says that in plain words, and the
  composite cannot.
- **Mark and Nick, "stand up going back":** agrees. By the top their hips are 3–4 cm toward the ball,
  where good players' hips sit back a little (D3, with the trail leg released). The head says level.
  So the evidence for "standing up going back" is the hips and the trail leg, not height. Mark can
  check that against his eye.
- **Jess, "excellent posture-wise apart from a head dip going back":** agrees. Her nearest pattern is
  Squat and push. She leaves it only at P3–P4, by dipping before the top instead of after it (D4),
  and is back inside both bands from P6. She keeps her trunk bend better than the tour mean, and her
  hips do not move in (+0.6 cm).
- **Jess shows where the model has holes**, as Mark suspected:
  1. Her dip is mostly the **lead knee** folding (+36° against about +13°). Nothing grades that
     today (D7), and from down the line the lead knee is seen behind the trail leg.
  2. Her "shallow plane and busy wrists" cannot be seen by posture readings. The chart can point to
     them: the Plane coach lines, the shaft and shoulder-plane conditions, the Wrist section. It must
     not claim them.
  3. Her absolute address readings are where the camera hides the golfer: 29.4° bend, 78 % toward
     the toes, side tilt −6°. Under §7.4 the chart shows none of them against a band until her
     camera is trusted.
  4. The close call between the two patterns (1.39 against 1.50) is said on the page, not hidden.
- **"All three marginally too bent at address":** the chart cannot say. Absolute address bend is
  rig-sensitive (§7.4). The three readings do not even order the golfers as Mark sees them.

---

## 5. What the golfer sees

### 5.1 Where it lives

A **Posture section** in Charts on both This shot and Session, built the way the Wrist section is
(`PpChWristShotSection`, `PpChWristSession`):

- It appears when the shot (or session) carries a posture reading **and** a posture curve is on show.
  In practice that means the Posture preset.
- It is a fold after Summary, with a one-line state when closed, and it remembers whether it is open.
- The Posture preset's curves stay as they are, from address, above it.
- In Replay (the glance at the mat, and the cast) it shows the head, the profile and the picture
  strip, and no controls.

No new tab: the wrist rework of 16 Oct settled that domain reads live inside the standard views.

### 5.2 This shot (Analyse, wide)

```
┌──────────────────────────────────────────────────────────────────────────────────────────┐
│ Posture                                                                       ▾ fold      │
│ Your hips moved 7.6 cm toward the ball by impact; good players' stay within 4 cm.        │  head (largest departure)
│ Pattern  Level (nearest, of your session) · trail knee released     Compare with ▸ Auto  │  pattern line
│                                                                                            │
│           P1    P2    P3    P4    P5    P6    P7    P8                                     │
│ Head      ●─────●─────●─────●─────●─────●─────●     ●      cm   ┐                          │
│  height   ░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░    up ▲  │ the pattern band as a     │
│           0    +0.1   0   +0.8  −0.2  +0.8  +2.4  +6.6          │ wash in the metric hue;   │
│ Hips      ●─────●─────●─────●─────●─────◉─────◉      -     cm   │ the fault line as a        │
│  to ball  ░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░────── fault ─    in ▲  │ hairline; this shot's dot; │
│           0    −0.3  +1.0  +3.0  +4.5  +6.5  +7.6               │ ◉ = outside + a word       │
│ Trunk     ●─────●─────●⚠────●⚠────●⚠────●─────●      -     °    │                            │
│  bend     0    −3.6  −8.3  −7.2  −4.4  −5.0  −10.2       upright│                            │
│ Lead knee …                                                     │                            │
│ Trail knee (style: released)  0  −9.7  −17.9  −18.3 …           │                            │
│ Pelvis height …                                                 ┘                            │
│                                                                                            │
│ What moved          At the top                    At impact                                │  §5.3
│                     hips 3 cm in, head level,     hips 7.6 cm in, head 2.4 cm up,          │
│                     trail leg straightened 18°    bend −10°: hips in, then up               │
│                                                                                            │
│ [ DTL address │ DTL top │ DTL impact ]  [ face-on address │ top │ impact ]                  │  §5.4 picture strip
│                                                                                            │
│ Findings                                   │  Working well                                  │
│ ◉ Early extension · P5–P7 · Go to P7       │  ● Head height held going back                 │
│ ◉ Loss of posture · P7                     │  ● Trunk bend held through P6                  │
└──────────────────────────────────────────────────────────────────────────────────────────┘
```

- **The profile.** One row per reading, P1…P8 on a discrete axis.
  - It is the Wrist profile's form: the band as a wash in the metric's own colour, the fault line as
    a hairline, the numbers under each column, a pole word at the right ("up", "in", "upright").
  - A column click seeks to that P-position, and the playhead's P is the picked column.
  - In Session mode the dots become the session's box per position (§5.5).
- **Words, not colour.** A reading outside the band gets a ring plus a word ("in 7.6", "up 2.4"),
  never colour alone, so it reads in greyscale.
- **Gated readings.** Readings under a ⚠ keep their number. A reading with no value is "-".
- **The fault line** is the corridor edge where the matching condition fires. When a reading leaves
  the band but no condition fired, the head says so (the wrist W-D6 rule): "outside the good-player
  band at P4, not far enough to be a fault".
- **Compare with.** Auto → (pattern), Level, Squat and push. It is persisted per screen and mode,
  like the wrist choices. The Against choices of Charts (★ Reference, session median, previous)
  draw as a ghost line, as on every trace.

### 5.3 "What moved"

At the top and at impact, one line each, built from the rows read. The rule, in order:

1. **Hips in** if hip depth is outside the band toward the ball.
2. **Up** if the head is above the band. **Down** if it is below it.
3. **Bend lost** or **bend gained**, attributed to whichever of 1–2 holds. If neither holds, "the
   trunk alone"; at P3–P5 the bend clause carries the ⚠.
4. The trail leg's style, said once at the top.

Examples:

- Nick at impact: "hips 6.5 cm in under a level head; that is where the 13.5° of bend went".
- Jess at the top: "down 9.5 cm, most of it the lead knee folding 36°; trunk slightly more bent".

### 5.4 The picture

The coach lines (Posture, Plane, and Head & hips sets) are measured at address and frozen. That is
what a coach draws.

- **The strip.** It puts the golfer's own frames at **address, top and impact** side by side, with
  the address lines drawn on all three:
  - down the line: spine (tailbone to the back of the neck), trail leg and butt line;
  - face-on: head circle and hip edges.
- **The band on the picture is an offset of the golfer's own lines**, never a pro's body:
  - **Down the line, impact:** a faint strip in front of the butt line, where good players' hips sit
    at impact (−2…+4 cm). It is scaled from centimetres to pixels by the DTL ruler that `pelvisThrust`
    already uses. Mark's butt is 7.6 cm into the ball, out past the strip.
  - **Face-on, impact:** a faint band below the address head circle's top: 0–6 cm lower is where
    good players' heads are.
  - **Down the line, top:** with the back-line producer (stage 4), the golfer's back line at the top
    is drawn in ink against the frozen address spine line.
- **Why not a ghost body.** The draft's ghost of a pro, scaled to the golfer's trunk, would need
  absolute pro angles. Those depend on the camera (§1.2.1), and it would show a shape no source
  measured in 2-D.
- **Interaction.** A click on a frame seeks the replay there. The strip honours the reader's Coach
  lines toggles. If the tile has no address marks (a swing analysed before version 2), the strip
  shows the frames with whatever lines the payload carries and says why the rest are absent.

### 5.5 Session

```
┌──────────────────────────────────────────────────────────────────────────────────────────┐
│ Posture · 15 shots                                                                         │
│ Level on 13 of 15 shots; you leave it the same way every time: hips in from P5.            │
│                                                                                            │
│           P1    P2    P3    P4    P5    P6    P7                                           │
│ Hips      ┬     ┬     ┬     ┬     ┬     ┬     ┬     session box per position (PpBoxPlot)   │
│  to ball  ┼─────┼─────┼─────┼─────╪═════╪═════╪     over the pattern band; this shot ringed │
│ Head …                                                                                     │
│                                                                                            │
│ Pattern by shot  ▮▮▮▮▮▮▮▮▮▮▮▮▮▯▯   (▮ Level ▯ between) · click a mark → that swing          │
│ Departures       D1 hips in  15 of 15 · D2 up at impact  9 of 15                            │
│ Repeatable?      the tightest row: head (P7 middle half 2.1 cm) · the loosest: hips at P3   │
└──────────────────────────────────────────────────────────────────────────────────────────┘
```

- **One box per position per row,** with PpBoxPlot and `box_stats.h` only (the one-box-plot rule).
  The hover lays the values on the plot.
- **The pattern per shot** as a strip of marks. A shot whose nearest pattern differs from the
  session's is ringed.
- **Departures** as counts across the shots, each opening its condition in Session diagnostics.
- **Repeatability:** the row and position with the narrowest and the widest middle half. A golfer
  with a consistent fault is told so ("the same way every time").

### 5.6 Trust, said once

The trust fold under the plot carries, once:

- which camera each row came from;
- the ⚠ reasons (the turn at P3–P5; the hip-joint migration);
- the bands' sources ("bands from 20 tour professionals, Kim 2021; 29 skilled men, Evans 2008; a vendor
  range, Sportsbox");
- the datum note for trunk bend ("published on the line from the base of the neck to the sacrum; ours
  reads the hip and shoulder joints; expect a few degrees between them");
- for absolutes, the rig rule (§7.4).

Nothing in the body repeats it.

### 5.7 Print

The Posture section prints as part of Charts:

- the profile with bands;
- the "what moved" lines;
- the picture strip at address, top and impact;
- the Session boxes and the pattern strip.

Figures are numbered and carry no affordances, as in `charts_panel_traces_relations.md` §3.

---

## 6. Content

### 6.1 Words per pattern and style

| Id | Name | golfer | golferWell | golferWhy |
|---|---|---|---|---|
| `posture_pattern_level` | Level | your swing stays level: your head and hips keep their height and their distance from the ball until the ball has gone | — (a pattern has no fault side) | Staying level keeps the bottom of your swing where you set it at address, so the strike repeats. |
| `posture_pattern_squat_push` | Squat and push | you sit down into your lead side as you start down, then push up through the ball | — | Pushing up from a squat is how many long hitters add speed; it works when the squat comes after the top and the push comes through the ball. |
| style: trail knee kept | Trail knee kept | your trail knee keeps its flex going back | — | Both are used by good players; what matters is that your hips do not move toward the ball as you turn. |
| style: trail knee released | Trail knee released | your trail leg straightens as you turn back | — | (the same) |

The pattern line on the page reads: "Your pattern: **Level** — your swing stays level …". Its
"why" is shown on hover and in print.

### 6.2 Words per departure, and the links

Each departure **borrows** its words, goal, cue, check, picture and drills from the condition that
names it. The chart adds only the position and the amount. That keeps one vocabulary, as the wrist
alignment did (`wrist_motion_into_charts.md` §7.3).

| Departure | Leads with | Its golfer words today | Proposed change | Drills already in the pack |
|---|---|---|---|---|
| D1 hips in | `early_extension` | "you stand up through the ball" | **"your hips move toward the ball coming down"**. TPI defines early extension by the hips moving in, and Nick shows the hips can move in without standing up. "Stand up" belongs to `loss_of_posture` / `coming_out_of_it`. | `drill.hipsToWall`, `drill.postureRod`, `drill.standoffCheck` |
| D2 standing up | `coming_out_of_it` (head) or `loss_of_posture` (several parts) | "your head lifts and you lean back through impact" / "you stand up out of your posture through impact" | none | `drill.headOnWall`, `drill.postureRod` |
| D3 standing up going back | `pelvis_thrust_backswing` first, then `loss_of_posture_backswing` | "your hips move toward the ball going back" | none | `drill.hipsToWall` |
| D4 early or deep dip | `head_drop_backswing` | "your head dips going back" | none | `drill.headOnWall` |
| D5 dip then rise | the two, linked | — | the page joins them: "you dip going back, then come up to make room" | as D2 and D4 |
| D6 staying down | `posture_sinks_impact` or `diving` | "you drop lower through impact" | none | `drill.leadLegBrace` |
| D7 lead knee folds | none | — | Question 5 | — |

**Work-ons.** No change. The work-on list already ranks the conditions. The chart only links to them:
a departure line ends with "Go to P7" (seek) and the condition's name, which opens it in Session
diagnostics. When `early_extension` and `loss_of_posture` are both patterns of the session, "roots
before symptoms" lists early extension. The chart's head then matches the report's work-on 1.

**"Working well"** lines come from the conditions' `golferWell` for the rows that stayed in the
band: "Your head stays level going back", "You keep your trunk bend through P6".

**The trail-knee condition.** `trail_knee_straighten` fires on its own on 124 of 127 Mark swings and
27 of 28 Nick swings ("a straight trail leg lets your hips slide instead of turn"). The design treats
the straightening as a style, so on the chart it is said as the trail-knee style, not as a finding,
unless D3 holds (Question 3).

---

## 7. Normative data

### 7.1 What can be seeded now

The bands are changes from address. "Level" is the general band. Squat and push differs only where
§4.2 says so. All are per position; P1 is 0 by construction.

| Row | P2 | P3 | P4 | P5 | P6 | P7 | Source | Kind |
|---|---|---|---|---|---|---|---|---|
| Head height (cm) | 0 ± 2 | +1 ± 3 | +2 ± 4 | 0 ± 3 | −2 ± 3 | −3.5 ± 3 | Evans 2008 at P4/P7; between them authored | measured at P4, P7 |
| Head, Squat and push | as Level | | | −4 ± 3 | −5 ± 3 | −3.5 ± 3 | Sportsbox chest −2", Foley, GOLFTEC | claim |
| Hip depth (cm, + toward ball) | 0 ± 2 | −1 ± 2.5 | −2 ± 3 | −1.5 ± 2.5 | 0 ± 2.5 | +1 ± 2.5 | P7: Sportsbox −2.3…+4.3; P4: Kim centre of mass −3.3 at the hips' change of direction | claim at P7; measured direction at P4 |
| Trunk bend (°) | −1 ± 2.5 | −1 ± 3 | −1 ± 3 | +1 ± 3 | −4 ± 3 | −6…0 (μ −3, σ 3) | Kim 2021 at every position; Sportsbox and GOLFTEC widen P7 | measured, different datum (§3.1) |
| Trunk bend, Squat and push | | | | +2 ± 3 | −3 ± 3 | as Level | Kim D225 +1.4 | measured |
| Lead knee (°) | +5 ± 5 | +10 ± 6 | +13 ± 7 | +20 ± 7 | +6 ± 6 | −1 ± 6 | Kim 2021 | measured |
| Lead knee, Squat and push | | | | +25 ± 7 | +12 ± 6 | as Level | authored from the coaching descriptions | claim |
| Trail knee (°) | style: kept 0 ± 5, released −15 ± 8 | | | +5 ± 8 | +3 ± 8 | +1 ± 8 | Kim (kept), GOLFTEC (released) | measured / claim |
| Pelvis height (% stance) | 0 ± 5 | ⚠ | ⚠ | ⚠ | 0 ± 5 | +5 ± 6 | Sportsbox +1" at impact; Chu +9 cm (not elite); converted at about 45 cm of stance | claim; **no band at P3–P5 until the belt line** |

**Spread.** σ is a seed for the **between-golfer** spread of a good player's change. No source reports
that directly. Kim reports between-player SDs of the absolute angle, which bound it from above. So σ
is authored, cited and marked `heuristic` where no source gives it, and `literature` where one does.

**Do these seeds sort the three golfers as Mark does?** Yes (§4.5). That is a sanity check on three
golfers, not a validation.

### 7.2 What needs collecting: the plan

In order of how soon each can exist.

**A. Coach-labelled golfers (now, no cost).** Every corpus golfer Mark labels becomes a labelled
example:

- Each label gives the pattern, the trail-knee style and the departures, at the level of the golfer,
  as tonight.
- Labels live with the corpus as a small file per golfer: golfer, date, pattern, departures, Mark's
  words.
- **Gate:** the matching agrees with Mark's pattern on every labelled golfer, or the disagreement is
  written up. When it disagrees, the bands move, not the labels.
- **Target:** 10 golfers before the bands are called anything but seeds. That also gives
  between-golfer spreads, which tonight's three cannot.

**B. Pro footage through our own pipeline (needs Mark's decision on rights).** This is the strongest
source, because the numbers come from the same producers and gates as the golfer's.

- **What is needed:**
  - Down-the-line and face-on clips of tour players, ideally slow-motion range footage.
  - One camera is enough for most rows. Every row in §1.1 is a single-camera quantity, so a
    DTL-only clip gives hip depth, trunk bend and knees, and a face-on-only clip gives head and
    pelvis height.
  - An import path for clips with no IMU and no launch monitor, at a known frame rate.
  - Provenance on every swing: player, club, event, source and date.
- **Target:** 30 swings from at least 10 players per pattern, each labelled by Mark by pattern.
- **Use:** changes from address straight away. Absolutes only where the camera position is known.
- **Rows:** `source: measured`, with n, the players and the footage named in the citation.
- **Rights (Question 4):** broadcast and social-media clips are not ours. The options are:
  - use them only to compute corridor numbers, never displayed or redistributed (the numbers are
    facts about the swings, but a lawyer's view is needed);
  - license footage;
  - film better players ourselves (club professionals, county players) with written consent. This
    is the clean route and also gives two cameras.

**C. Calibrated cameras (the camera calibration thread, designed, not built).** These turn absolutes
into numbers that travel. Until then:

- The two-camera capture protocol (`docs/validation/two_camera_capture_protocol.md`: DTL camera on the
  hands line at hand height, levelled) is the one placement whose absolutes the chart will compare.
- Setup bands (address bend, knee flex, arm hang) are seeded per club from Kim (5-iron 40.6 ± 2.7°)
  and GOLFTEC (driver shoulder bend 29°), and shown only on such captures.

**D. Wearables (the trunk IMU programme).** A thorax sensor reads trunk bend with no camera
artefact. When it lands it becomes the trunk row's best rung, as `posture_metrics_design.md` §3.3
already plans.

### 7.3 Representation: the smallest model change

**Per-position measures, following the wrist precedent.** Wrist bands already live in `norms.json` as
`m_leadWristFlexExt_p1…p8`, one measure per position. Posture does the same: a change-from-address
measure per row per position. Where a measure already exists for that row and position, it is
reused, not duplicated:

| Row | P2 | P3 | P4 | P5 | P6 | P7 | P8 |
|---|---|---|---|---|---|---|---|
| Head | `m_headLift_p2` | `_p3` | `m_headLiftBack` | `_p5` | `_p6` | `m_headLiftImpact` | `_p8` |
| Hip depth | `m_pelvisThrust_p2` | `_p3` | `_p4` | `_p5` | `_p6` | `m_pelvisThrustImpact` | — |
| Trunk bend | `m_spineBendChange_p2` | `_p3` | `m_spineBendChangeTop` | `_p5` | `_p6` | `m_spineBendChangeImpact` | — |
| Lead knee | `m_leadKneeFlex_p2…p8` (new) | | | | | | |
| Trail knee | `m_trailKneeFlex_p2`, `_p3`, `m_trailKneeFlex` (P4), `_p5…p8` | | | | | | |
| Pelvis height | `m_pelvisLift_p2`, `_p6`, `m_pelvisLiftImpact` | | | | | | |

The reused measures are `delta` reducers anchored at P1 and read at the position (checked in `core.json`:
`m_headLiftBack`, `m_trailKneeFlex` P1→P4; `m_headLiftImpact`, `m_pelvisLiftImpact` P1→P7).
`m_pelvisThrustBack` is an extreme over P1–P4, so hip depth at P4 gets its own `_p4` measure.

That is about 30 new measures, all of the existing "series at phase minus series at address" kind.
They need no producer: the phase grid has the values. They watch no condition, so they carry
`unwatchedTail` reasons ("drawn on the Posture chart; the departures are graded by the conditions
of §4.3"), or whatever keeps the wrist per-position measures lint-clean.

**Pattern bands as context nodes, following the archetype precedent.** `contexts.json` already has
`archetype_bowed` and `archetype_cupped` under `full_swing`: "not shot types", nodes that carry a
style's corridor. Posture adds:

```json
{ "id": "posture_level",       "label": "Level posture pattern",       "parent": "full_swing" },
{ "id": "posture_squat_push",  "label": "Squat-and-push posture pattern", "parent": "full_swing" }
```

- The **Level band** is written at `posture_level`.
- **Squat and push** is written at `posture_squat_push` and only differs at P5–P7 (§7.1). Its other
  rows fall back up the tree to the general rows.
  - For that to work, the general (`any`) rows of the per-position measures must **be** the Level
    band. Two consequences follow:
    - `posture_level` needs only the rows where Level differs from the general case (none at first).
    - The general row of `m_spineBendChangeImpact` is the fault corridor today (μ 0). The pattern
      band is therefore written at `posture_level` rather than replacing the general row, until
      Question 2 is answered.
- **Resolution:** the chart's presenter resolves each row's band by naming the pattern's node
  explicitly. **No shot's grading context ever becomes a posture node in v1.** Session diagnostics
  grades exactly as today, so the faults keep one judge, and the band is description.
- **Provenance:** every row carries `source` (`literature` with the DOI, or `heuristic` with the
  authoring note), n and the citation of §3. `NormSource::Literature` already exists.

**What this gives up.** A context node is one point in a single-parent tree. "Squat and push with a
driver" cannot have its own row; it resolves to the pattern node, not to `driver`. For changes from
address that is acceptable in v1: the published changes are 5-iron and the club mainly moves
absolutes. The upgrade, if per-club pattern rows are ever needed, is a fourth optional key term
(`style`), probed like cohort. That is a schema change, deferred until data asks for it.

**Considered and not chosen:**

- **A "curve norm" row** holding eight (μ, σ) pairs, as the draft proposed. It is less content, but
  it is a new schema, conditions could not read it, and it would split the chart's bands from the
  corridors that grade. The wrist alignment of 15 Oct chose one table for exactly that reason.
- **Grading by pattern.** Re-centring the departure conditions on the matched pattern, as the
  wrist archetypes do. Not in v1: the departures are faults in **every** pattern (§4.3), so no
  condition needs re-centring. The one row where the patterns differ in a graded way is head height
  at P5–P6, and no condition reads it today.

### 7.4 The honesty rule for rig-dependent absolutes

1. **A measure is marked `rigSensitive`** when its absolute value depends on where the camera stands:
   - address bend, knee flex, arm hang, head pitch;
   - heel-to-toe balance;
   - absolute side tilt;
   - any absolute trunk or knee angle at a position.

   This is a new boolean on the measure in `core.json`. Changes from address are not marked.
2. **An absolute is compared with a published band only on a trusted capture.** Trusted means the
   camera is calibrated (when the calibration thread lands), or the session declares the capture
   protocol's placement and the address frame passes its checks (the hands-line stick vertical, the
   camera level).
3. **On an untrusted capture,** the absolute is still shown, with its number. It is compared only
   with the golfer's own sessions on the **same** set-up (same camera, same placement record), as a
   box. The trust fold says, in these words: *"How bent you are at address depends on where the
   camera stands, so it is compared only with your other sessions from this set-up."*
4. **Changes from address are always compared with the bands,** each labelled with its source.
5. **Not in this design's scope, but asked (Question 6):** whether the same rule should stop the
   setup conditions grading absolutes on untrusted rigs. Today `poor_setup_posture` fires on 21 of
   Jess's 23 swings through the balance reading the camera may be causing.

---

## 8. What it does not do

- **It shows no pro body or pro frame.** Bands are drawn as offsets of the golfer's own lines (§5.4).
- **It makes no absolute comparison with good players on an untrusted camera** (§7.4).
- **It shows no posture score and no composite number.** Session diagnostics principle 5 keeps
  composites inside the engine. The chart shows each row in its own unit.
- **It grades nothing new.** The departures are the existing conditions. The pattern is description.
  The one new judgement on screen, the pattern and where you leave it, is worded as a comparison,
  never as a fault.
- **Side tilt, side bend and sway are not posture rows here.** Side tilt grows into impact in every
  good swing (TPI: "roughly 35 deg of side bend at the top of the swing and 35 at impact" as the PGA Tour average in
  its database, system and sample not named,
  [mytpi.com](https://www.mytpi.com/articles/biomechanics/helping-rising-star-david-puig-limit-side-bend-to-alleviate-stress-on-lower-back)). They have their own
  conditions, and including them would read a working swing as a loss (`posture_metrics_design.md` §2).
- **No back curves (C- and S-posture) and no pelvic tilt.** There is no producer for either. The
  published 18° posterior tilt through impact (§3.3) is said in the trust fold's note on trunk bend,
  not drawn.
- **No intent.** The chart describes what the body did, not what the golfer meant to do.
- **It does not claim the plane or the wrists.** For Jess's shallow plane and busy hands it links to
  the Plane coach lines and the Wrist section.

---

## 9. Questions for Mark

1. **Two patterns and a trail-knee style** (§4.2): is that the list? Is a dip going back ever a
   style to you, or always Jess's flaw? This design says always a flaw past a few centimetres.
2. **Should the fault lines move to the published band?** Kim's professionals lose about 6° of trunk
   bend by impact, and Evans' skilled players' heads are 3.5 cm **lower** at impact. Our corridors
   say "held is ideal" (μ 0, firing at 5° and at 3 cm).
   - **Re-centred:** Mark's head would read further out (+2.4 cm against a −3.5 mean) and his bend
     less (−10° against −6°).
   - **Unchanged:** the chart shows the band and the fault line as two different marks.
3. **`trail_knee_straighten` as a standalone fault:** demote it to a style note that fires only with
   the hips moving in or the head rising going back (D3)? Today it fires on 124 of 127 of your swings.
4. **Pro footage rights:** may clips of named tour players be run through the pipeline to compute
   corridor numbers that are never shown? Or only footage we film or license?
5. **The lead knee folding going back** (Jess, +36° against about +13°): a new condition, or part of
   `head_drop_backswing`'s explanation?
6. **Absolutes on untrusted cameras:** should the setup conditions also stop grading them, as the
   chart stops comparing them?

---

## 10. Build plan

The stages run in order. Commits come only at the end, after Mark has judged the results. Every UI
stage is shown to Mark as annotated before/after review sheets, probed with `--probe-qml` before it is
called done, and closed with a windowed run. Two to three app builds per stage.

| Stage | What | Done when |
|---|---|---|
| **0. Per-position data, committed** | Extract the per-position change table (Appendix A) for the three golfers into `docs/research/data/posture/posture_positions_<date>.csv`, from the run root's phase grids, before those copies are deleted. Add the script to `tools/diagnostics/`. | The CSV is in the repo, §2.1 reproduces from it, and the run tree's deletion is no longer blocked by this design |
| **1. Measures and bands** | The per-position measures of §7.3. The two context nodes. The §7.1 rows, with sources. The `rigSensitive` flag (parsed, validated, written). | `core_pack_test` and `diagnostics_health` clean; a `measure_dump` of the three golfers shows every row at every position; no grade in Session diagnostics changes (regrade the three golfers: an identical firing table) |
| **2. The presenter (C++)** | `ChartsPosturePresenter` beside `ChartsWristPresenter`, with nothing computed in QML. It produces: per shot and per session the profile with bands; the pattern match (§4.4) with the close-call rule; the first exits named by departure; the "what moved" lines; the trust lines; the trail-knee style. The session-series model carries the phase-grid rows. | Unit tests on fixtures of the three golfers' medians give the §4.5 table exactly; a face-on-only and a DTL-only fixture match on their own rows and say so; a ⚠ reading is excluded from matching and still drawn |
| **3. The Posture section (QML)** | This shot, Session and the Replay glance (§5), using the Wrist section's profile and ledger forms; Print. | Review sheets of the three golfers in Analyse, Replay and Print, light and dark, approved by Mark; `qml_ui` probes for the section's objectNames |
| **4. Producers that remove the ⚠** | (a) The TPI back line at every P-frame: tailbone and back of the neck from the person mask, extending `AddressMarksStage` to P2–P7 on DTL. (b) The belt-line pelvis height. Stage-version bumps, then re-analysis of the library on the cabin PC (pose reused). | On the three golfers, trunk bend at P4 from the back line agrees with the address back line within the V5 bar on Nick (who read 9° off); the ⚠ comes off any row whose producer passes; the firing table is rewritten |
| **5. The picture strip** | Address, top and impact frames with the frozen coach lines and the band offsets (§5.4), in the section and in print. | Review sheet on all three golfers; the hip strip scales with the DTL ruler; a swing without address marks degrades as specified |
| **6. Normative collection** | §7.2 A at once; B after Question 4; C with the calibration thread. Each new set of rows goes in with its write-up under `docs/research/data/posture/`. | Ten labelled golfers matched, disagreements written up; the bands' `source` moves from `heuristic` to `measured` row by row, never silently |

Stages 0–3 are the chart. Stage 4 makes it trustworthy at the top. Stage 5 is the picture Mark asked
for. Stage 6 never ends.

---

## Appendix A: the per-position table (reproduce)

The per-position numbers in §2.1 were read from the phase grids of tonight's re-analysed copies on the
share (run root `/mnt/swingdata/scratch/posture-20261021/copies/`). These are the copies that
`posture_composites_2026-10-22.md` describes. Phases: P1 = 0 (Address), P2 = 12, P3 = 8, P4 = 2,
P5 = 13, P6 = 9, P7 = 5, P8 = 14 (`src/Analysis/swing_analysis.h`). Run it with
`/opt/homebrew/bin/python3`. Stage 0 commits the output as a CSV, because the run tree is due to be
deleted.

```python
import json, glob, collections, statistics as st
root = '/mnt/swingdata/scratch/posture-20261021/copies'
P = [('P1',0),('P2',12),('P3',8),('P4',2),('P5',13),('P6',9),('P7',5),('P8',14)]
keys = ['headLift','pelvisThrust','spineForwardBend','leadKneeFlexion','trailKneeFlexion','pelvisLift']
for g in ['Mark-Liversedge','Nick-Henn','Jess-Ratcliffe']:
    acc = collections.defaultdict(lambda: collections.defaultdict(list))
    for f in glob.glob(f'{root}/{g}/*/swing_*/swing_phasegrid.json'):
        m = {x['key']: x for x in json.load(open(f))['metrics']}
        for k in keys:
            if k not in m: continue
            v = {x['phase']: x['value'] for x in m[k]['values'] if x.get('value') is not None}
            if 0 not in v: continue
            for name, ph in P:
                if ph in v: acc[k][name].append(v[ph] - v[0])
    print(g)
    for k in keys:
        print(' ', k, [f"{st.median(acc[k][n]):.1f}({len(acc[k][n])})" if acc[k][n] else '-' for n, _ in P])
```

Mark's 7-iron-only interquartile ranges (P2 … P8) match the all-club medians to within a degree or a
centimetre. Examples: hip depth at P7 6.8 / 8.7 cm; head at P7 0.7 / 3.4 cm; bend at P7 −11.3 / −9.5°.
Within-golfer correlations at P7 (two-camera swings):

| | Mark (n 108) | Nick (n 27) | Jess (n 7) |
|---|---:|---:|---:|
| Hip depth against bend change | −0.25 | −0.55 | +0.23 |
| Head height against bend change | +0.09 | −0.23 | −0.18 |

## Appendix B: the worked match (reproduce)

The §2.1 medians against the §7.1 seeds. Trunk bend at P3–P5 is given half weight here as a stand-in
for its ⚠. The build excludes it, which moves no golfer's nearest pattern.

```python
import numpy as np
Ps = ['P2','P3','P4','P5','P6','P7']
G = {'Mark': {'head':[0.1,0.0,0.8,-0.2,0.8,2.4], 'hips':[-0.3,1.0,3.0,4.5,6.5,7.6],
              'bend':[-3.6,-8.3,-7.2,-4.4,-5.0,-10.2], 'lead':[3.9,14.2,14.2,13.0,4.4,-3.0]},
     'Nick': {'head':[0.2,0.5,-0.1,-0.4,-0.1,0.1], 'hips':[2.6,2.9,4.1,4.7,5.6,6.5],
              'bend':[-3.2,-3.9,-8.5,-7.5,-9.4,-13.5], 'lead':[1.6,9.3,16.6,3.0,-0.8,-4.8]},
     'Jess': {'head':[-2.6,-5.2,-9.5,-8.6,-7.3,-6.0], 'hips':[-0.3,-0.4,-1.8,0.9,3.3,0.6],
              'bend':[1.8,3.0,2.1,2.9,-0.3,-2.2], 'lead':[6.1,19.4,36.0,23.4,2.5,-9.3]}}
L = {'head':([0,1,2,0,-2,-3.5],[2,3,4,3,3,3]), 'hips':([0,-1,-2,-1.5,0,1],[2,2.5,3,2.5,2.5,2.5]),
     'bend':([-1,-1,-1,1,-4,-6],[2.5,3,3,3,3,3]), 'lead':([5,10,13,20,6,-1],[5,6,7,7,6,6])}
S = dict(L, head=([0,1,2,-4,-5,-3.5],[2,3,4,3,3,3]), bend=([-1,-1,-1,2,-3,-6],[2.5,3,3,3,3,3]),
         lead=([5,10,13,25,12,-1],[5,6,7,7,6,6]))
w = {'P2':1,'P3':.5,'P4':.5,'P5':.5,'P6':1,'P7':1}
for name, g in G.items():
    for pn, pat in (('Level', L), ('Squat and push', S)):
        num = den = 0
        for r in g:
            mu, sd = map(np.array, pat[r]); z = (np.array(g[r]) - mu) / sd
            for i, p in enumerate(Ps):
                ww = w[p] if r == 'bend' else 1; num += ww * z[i]**2; den += ww
        print(name, pn, round(np.sqrt(num / den), 2))
```

(The §7.1 seed for trunk bend at P7 is −6…0, μ −3. This appendix uses Kim's −6 as μ. With μ −3 the
distances are Mark 1.39 / 1.52, Nick 1.61 / 1.79, Jess 1.48 / 1.37 (Level / Squat and push): no
golfer's nearest pattern changes.)
