/*
 * Copyright (c) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#pragma once

#include "measure_facets.h"

#include <QString>
#include <QStringList>

#include <optional>
#include <vector>

// The MEASUREMENT vocabulary: what a Measure is, which way its corridor is open, and how a Signal
// tests it. Split out of characteristic.h, which otherwise carried this alongside the condition
// graph (Condition, Edge, CharacteristicPack) — a reader wanting only "what is a Measure" had to
// pull in the causal model too. Header-only, Qt-only, no Qt-GUI, same as the file it came from.

namespace pinpoint::analysis {

// ── Measure ─────────────────────────────────────────────────────────────────
enum class MeasureKind {
    Composed,   // built from facets: a Series sampled by a Reducer
    Provided,   // a first-class metric we ship with a producer (tempo ratio, X-factor, sequence)
    Composite,  // several GRADED measures read as one z (posture_metrics_design.md §6.1) — no series,
                // no metric key, no reducer: it names its parts, and NormMeasureSource combines them
};

// ── Composite: several graded measures read together ────────────────────────
//
// THE ONE MEASURE THAT JUDGES A SHAPE RATHER THAN A NUMBER. Every other signal tests one measure
// against one corridor, and "you lost your posture" is not a statement about one reading: a golfer
// who loses 4° of bend, moves 4 cm toward the ball and rises 3 cm has lost it with no single reading
// past its line. A composite reads each part through its OWN norm (so each part keeps its context,
// its cohort, and every refusal that reading would have met on its own), turns each into a z, and
// combines the z's into one number that is calibrated to trip a clean swing exactly as often as one
// reading trips its own fault line. After that it is an ordinary measure: μ 0, σ 1, a corridor, two
// tails, the ledger. See docs/design/posture_metrics_design.md §4.3, §5.2 and §6.
//
// The number itself is never shown to a golfer (session diagnostics principle 5). What a surface
// draws is the parts, in their own units — which is why the reading carries them (MeasureReading::parts).
enum class CompositeCombine {
    // Σ wᵢsᵢzᵢ / √(ΣᵢΣⱼ wᵢwⱼρᵢⱼ): the parts moving together IN ONE DIRECTION (loss of posture).
    // One part alone does not carry it, and a compensation cancels — both by design (§4.3).
    Coherent,
    // Σ wᵢzᵢ², turned into an equivalent z through Satterthwaite's gχ²ₕ: the parts away from their
    // ideal IN ANY DIRECTION (poor setup posture). One badly-off part is enough (§5.2).
    Distance,
};

// One part of a composite: a quantity, read by the FIRST of its alternatives that reads with a
// corridor on this swing.
//
// ALTERNATIVES ARE A LADDER WITHIN ONE PART, best instrument first — DetectionMode::First at the
// level of a part. Needed because a measure's own `preferKeys` ladder must keep one unit, and two
// instruments for one quantity often do not share one: DTL forward bend is in degrees, face-on trunk
// rise in % of the address chord. Both together would count the quantity twice, so the first that
// reads stands for the part and the rest are ignored.
struct CompositePartSpec {
    QString             name;        // stable within the composite: "trunk", "pelvisDepth"
    QStringList         measures;    // the alternatives, best first
    // COHERENT ONLY: one per alternative, ±1 — the direction of THIS instrument's reading that counts
    // toward the composite's high tail. Per alternative because two instruments for one quantity can
    // run opposite ways (bend falls as trunk rise grows). Doubles, not ints, so a 2 or a 0.5 an
    // author typed survives to the validator (`compositeSign`) instead of being rounded into a
    // legal-looking ±1. Empty on a distance part — it squares, so it has no direction to state.
    std::vector<double> signs;
    // Strictly inside 0 and 1 (`compositeWeight`, [[weights-normalised-0-to-1]]). The coherent
    // statistic does not change when every weight is scaled together, so the range costs nothing,
    // and it lets the weights compose with the rest of the model.
    double              weight   = 0.0;
    // Without this part there is nothing to judge (the trunk hinge of the setup composite, §5.3), so
    // the composite is unavailable however much of the rest was read. Honoured in either mode; the
    // design only needs it on a distance composite.
    bool                required = false;
};

struct CompositeSpec {
    CompositeCombine               combine   = CompositeCombine::Coherent;
    // The coverage rule (§4.6): the parts read must carry at least this share of the authored weight,
    // or the composite is unavailable (MissingKind / SignalRefusal::CompositeCoverage). Missing parts
    // are never counted as z = 0 — that would turn "not seen" into "fine".
    double                         minWeight = 0.5;
    std::vector<CompositePartSpec> parts;
};

QString compositeCombineName(CompositeCombine c);
bool    compositeCombineFromName(const QString &s, CompositeCombine &out);

enum class MeasureStatus {
    Live,           // a producer exists today
    Planned,        // a producer is planned
    NoProducer,     // nothing produces it yet — ROADMAP work, someone could pick this up
    NotCapturable,  // no sensor this product has can ever resolve it — a CAPTURE GAP, not roadmap
    ExternalDevice, // a producer is intended, but it reads from hardware the user may not own
    Held,           // a producer exists and runs, but the measure is deliberately NOT graded yet
};

// Held exists because Planned was being used for two different facts. A DTL posture series that
// ships every shot, but whose conditions have no authored cause (or whose measure has no norm), is
// not pipeline work — the number is on screen — yet grading it would fire a fault nobody can
// explain. It is treated exactly as Planned by everything that grades or counts live measures, and
// differs only in what it SAYS: "held", with `gapReason` naming what content is owed. Appended, not
// inserted: the enum's int is used as a sort key.

// The distinction between NoProducer and NotCapturable is the whole reason the roadmap can be
// trusted as a work queue. NoProducer means "we could build this"; NotCapturable means "a different
// sensor or view is required, and listing it as missing pipeline work would mislead every reader".
//
// ExternalDevice is the third answer, and it is not a shade of either. A launch monitor resolves
// face angle, spin and strike location; integrating one is work we intend to do, so calling it
// NotCapturable would be false. But the work is an INTEGRATION, not a producer written from our own
// pixels, and a roadmap row reading "build a spin-axis producer" would send somebody the wrong way.
// So it is roadmap-eligible and sectioned apart. The per-shot half lives on the requirement
// (MetricRequirement::launchMonitor): a user with no launch monitor gets "needs a launch monitor"
// through the same path a missing face-on camera already takes, which is what makes the absence
// graceful rather than a hole. `gapReason` is required here exactly as it is for NotCapturable.
//
// WHY THE VOCABULARY HOLDS SOME QUANTITIES TWICE — once as our own estimate (m_launchAngle,
// m_ballSpeed, m_attackAngle, m_clubPathAtImpact…) and once as a launch-monitor reading
// (m_lmLaunchAngle, m_lmBallSpeed, m_lmAttackAngle, m_lmClubPath…). The pairs look like drift and
// are deliberate, for two reasons that pull in the same direction. First, not every user owns a
// launch monitor, so every ball-flight quantity a diagnosis can rest on must be producible from
// our own pixels; the m_* rows are that commitment, and the `planned` ones among them are roadmap,
// not leftovers. Second, for a user who owns both, the two are INDEPENDENT measurements of one
// event — and the monitor's reading is the standard our estimate is validated against. One merged
// measure could do neither job: it would leave monitor-less users without the quantity, and a
// measure cannot validate itself. Resolution is an exact metricKey match with no fallback
// (measure_sample.cpp), so which of the pair a signal binds to is a per-quantity authoring
// decision made where the signal is written — not something a loader infers from what happens to
// be plugged in.

// WHAT A MEASURE IS READ OVER: one swing's phases, or the session's swings
// (trunk_imu_sequence_grf_design.md §10.8 E2, §10.9 P7/P8). A Session measure has no per-swing value
// at all — LiveMeasureSource refuses it (MissingKind::SessionScope) and the session model reduces it
// from the session's shot records — so a condition read by one is never given a per-swing verdict
// (NR-04, NR-11). Absent in JSON => Swing, which is every measure authored before scopes existed.
enum class MeasureScope { Swing, Session };

inline QString measureScopeName(MeasureScope s)
{
    return s == MeasureScope::Session ? QStringLiteral("session") : QStringLiteral("swing");
}
inline bool measureScopeFromName(const QString &s, MeasureScope &out)
{
    if (s == QLatin1String("swing"))   { out = MeasureScope::Swing;   return true; }
    if (s == QLatin1String("session")) { out = MeasureScope::Session; return true; }
    return false;
}

// Which way a measure's corridor is open.
//
// ON THE MEASURE, NOT THE NORM, and that is the load-bearing decision. One-sidedness is semantics
// of the QUANTITY and invariant across contexts: smash factor has no upper fault with a driver and
// none with a wedge either, only a different implausibility. If shape sat on the norm, a driver row
// and an iron row could disagree about the shape of one measure and the corridor editor could flip
// shape per context — so a norm row carries only NUMBERS, and validateNormsAgainst checks those
// numbers against the measure's shape exactly the way it already checks `unit`.
//
// This changes which values reach which band. It does NOT add a band: Ideal/Good/Watch/Action/
// NotMeasured and ragOf() are untouched, and a one-sided norm consults the same z ladder on its one
// graded tail.
//
// The open tail is open UP TO PLAUSIBILITY, not to infinity — see Norm::plausibleLo/plausibleHi. A
// driver smash of 1.62 is not a swing finding, it is a mis-tracked ball, and grading it either way
// would launder a capture fault into a confident diagnosis.
//
// `Floor` is for self-normalising ratios and mechanically-universal thresholds ONLY. Cohort-relative
// bigger-is-better outcomes — ball speed, club speed, carry — get no norm at all: a population floor
// on those grades a golfer Action for their age. They are benchmarks, a different feature.
enum class Shape {
    Target,    // the default and ~90% of the catalogue: a corridor, both tails grade
    Floor,     // higher is better. mu is the aspiration; only the LOW tail grades
    Ceiling,   // lower is better. Mirror of Floor; only the HIGH tail grades
};

// Which tail of the corridor a signal watches, or a measure deliberately does not. Required for the
// corridor and threshold tests: a condition is one tail of one measure, so a signal without a
// direction cannot identify a condition. Declared here rather than beside Signal because Measure
// needs it too — see `unwatchedTail`.
enum class Direction { High, Low };

struct Measure {
    QString       id;                                   // stable, never reused
    MeasureKind   kind   = MeasureKind::Composed;
    Series        series;                               // Composed only — the facet-built series
    // The reducer applies to BOTH kinds. A Provided measure names its series with a metricKey
    // instead of facets, but a catalogue TimeSeries is still a curve: metric_type.h describes it as
    // "reduced to peak / @impact / Δ / rate by the chart layer". Three characteristics can sit on
    // one live series at different phases (sway, slide and hanging back are all pelvis lateral
    // displacement), and modelling that as three separate metrics would be the parallel registry
    // this design exists to avoid. For a PointInTime or Summary metric the reducer is At().
    Reducer       reducer;
    QString       metricKey;                            // link into MetricCatalogue (Provided)

    // BETTER INSTRUMENTS FOR THE SAME QUANTITY, tried in order BEFORE metricKey. Empty for almost
    // every measure; `metricKey` alone is still the ordinary case.
    //
    // This is the answer to a question the paragraph above ("why the vocabulary holds some
    // quantities twice") left open, and it does not reopen it. The pairs stay: m_attackAngle is
    // still our estimate and m_lmAttackAngle is still the device's reading, separately addressable,
    // so the validation comparison a golfer who owns both is entitled to is untouched — a measure
    // still cannot validate itself. What changes is what a DIAGNOSIS binds to. Before this, a
    // signal had to name one instrument for ever: `attack_too_steep` named our projected estimate,
    // so a golfer with a launch monitor plugged in was diagnosed from the camera while the device's
    // own attack angle sat in the document read by nothing at all.
    //
    // AUTHORED, NOT INFERRED, which is the distinction that matters. The loader does not decide
    // anything from what happens to be plugged in; it walks a list somebody wrote in the measure
    // row, best first, and takes the first key this swing actually carries. That is the same
    // best-first ladder MetricRoute already uses one layer down, for the same reason: which
    // instrument is better is a per-quantity judgement, and it belongs where it can be read.
    //
    // EVERY KEY IN THE LADDER MUST CARRY THE MEASURE'S UNIT. A ladder is one quantity measured
    // twice, so a corridor authored in degrees has to mean degrees whichever rung answered;
    // `measureKeyUnit` in the pack validator refuses the row otherwise. Mixing units here would
    // grade a golfer against a number stated in something else, silently, and only on the shots
    // where the preferred instrument happened to be absent.
    QStringList   preferKeys;

    QString       label;                                // canonical for Composed, authored for Provided
    QStringList   aliases;                              // coach phrasing that resolved here; grows with use
    QString       unit;
    ViewNeeded    viewNeeded = ViewNeeded::Any;
    MeasureStatus status     = MeasureStatus::NoProducer;
    QString       gapReason;                            // NotCapturable: why, in one line, for the UI

    // Which way this quantity's corridor is open. See Shape. Absent in JSON => Target, so every
    // measure authored before shapes existed keeps grading exactly as it did.
    Shape         shape      = Shape::Target;

    // Read over one swing (the default) or over the session's swings. See MeasureScope.
    MeasureScope  scope      = MeasureScope::Swing;

    // What a HIGH value of this measure means, in the measure's own words — "further back, toward
    // the trail foot". Rendered wherever a signal's direction is chosen, INSTEAD of High/Low.
    //
    // This is a correctness mechanism, not decoration. Three signals shipped inverted because an
    // author picked High or Low against a sign convention that was either unstated or the opposite
    // of what they assumed; an inverted signal then fires happily on the wrong swings with
    // correct-sounding consequence text attached. An author who reads "further back, toward the
    // trail foot" cannot make that mistake in the same way. See
    // docs/design/pinpoint_sign_conventions.md.
    QString       highMeans;

    // How a GOLFER, not a coach, would say that a reading was higher — and lower — than usual. Used
    // by the home screen's plain-language summary (docs/design/home_themes_design.md) in "On swings
    // where {A}, {B}", across ONE golfer's swings compared with each other, so both phrases are
    // COMPARATIVE to that golfer's usual and never absolute: "your hips slide further toward the
    // target", "your hips turn less going back".
    //
    // Derived from `highMeans`, and an inverted pair is the same bug an inverted signal is: a
    // summary that tells a golfer the opposite of what their swings did, in words they will trust
    // because they are plain. Each phrase stands alone — the other half of the sentence may be any
    // measure at all, so "them" or "it" has nothing to refer to. Two measures of one quantity at
    // different moments say the moment ("going back" / "coming down"), or a theme naming both reads
    // as the same thing twice. `golferPhraseMissing` / `golferPhraseWording` lint both.
    QString       golferHigh;
    QString       golferLow;

    // The NUMBER in a golfer's words, for the session report (docs/design/session_report_design.md
    // §5): "trail-knee bend lost by the top". Lower case. `golferSign` is +1 or −1: the value the
    // golfer is shown is raw × golferSign, −1 where the words already carry the sign ("lost"), so the
    // value and its chart read as a magnitude in the direction the words name.
    QString       golferName;
    int           golferSign = 1;

    // A tail that GRADES and is deliberately not watched, with the reason it is not.
    //
    // Same contract as `gapReason` for NotCapturable: the field says something is intentional, the
    // reason says what, and the UI quotes the reason. It exists because there are three correct
    // answers to a tail with no condition behind it and `shape` only expresses two of them. Author
    // the condition when the tail carries a fault; set `shape` when the quantity is one-sided and
    // the tail should stop grading altogether; set this when the tail genuinely grades, the reading
    // out there is real, and there is still no fault to name — the corridor is mis-centred, or the
    // deviation is a known artefact of how the number is measured. Silences `ungradedTail` for that
    // tail ONLY; the other tail is unaffected.
    //
    // NOT a quiet way to dismiss a tail. On a one-sided measure it is a contradiction (that tail
    // does not grade, so there is nothing to be unwatched) and on a tail a signal already watches it
    // is the opposite of true — both are load ERRORS, not judgements. And an unwatchedTail with no
    // reason is indistinguishable from a tail nobody got to, which is what `unwatchedTailNoReason`
    // is for.
    std::optional<Direction> unwatchedTail;
    QString                  unwatchedReason;

    // The ABSOLUTE value depends on where the camera stands (posture_chart_design.md §7.4): address
    // bend, knee flex, arm hang, head pitch, heel-to-toe balance, absolute side tilt. A view may
    // compare such a reading with a published band only on a trusted capture (calibrated, or the
    // capture protocol's placement declared and checked); otherwise only with the golfer's own
    // sessions from the same set-up. Content, not presentation, so it is in the pack and in its
    // content stamp. It does NOT gate grading (yet, §7.4.5). A change from address cancels the
    // placement, so the flag on a delta-from-address measure is a contradiction the validator warns
    // about (`rigSensitiveDelta`).
    bool                     rigSensitive = false;

    // MeasureKind::Composite only: its parts and how they combine. Empty on every other kind, and the
    // loader refuses a `composite` block on one (`compositeKind`).
    CompositeSpec            composite;
};

// Every measure id a composite can read, alternatives included, in authored order. Empty for any
// other kind. The ONE walk the validator, the shared-evidence stamp and the tail lint all use, so
// none of them can disagree about which measures a composite stands on.
inline QStringList compositePartMeasures(const Measure &m)
{
    QStringList out;
    if (m.kind != MeasureKind::Composite) return out;
    for (const CompositePartSpec &p : m.composite.parts)
        for (const QString &id : p.measures)
            if (!out.contains(id)) out << id;
    return out;
}

// Every catalogue key this measure will accept a value from, best first: the preferred instruments
// and then its own. THE ONE PLACE THE ORDER IS EXPRESSED — reading a value, reporting why there
// wasn't one, and the reverse metric-to-measure join all walk this, so none of them can disagree
// about which rungs exist or which comes first.
inline QStringList measureKeyLadder(const Measure &m)
{
    QStringList keys = m.preferKeys;
    if (!m.metricKey.isEmpty())
        keys << m.metricKey;
    return keys;
}

// ── Signal ──────────────────────────────────────────────────────────────────
// Signals are purely COMPARATIVE. Change and rate are not tests — they are reducers, and live on
// the measure. One place expresses time.
enum class SignalTest {
    OutsideCorridor,   // preferred: authors no numbers, inherits the measure's norm
    Threshold,         // an authored number; needs a citation to be more than an opinion
    Order,             // two measures, temporal ordering (kinematic sequence)
    Ratio,             // two measures IN THE SAME UNIT, their quotient against an authored number
                       // (tempo). A quotient is not a measure, so no norm can key on it — see the
                       // Ratio branch of characteristic_engine.cpp for why a stand-in measure with
                       // a dimensionless corridor cannot be made to work, and why the number is
                       // therefore authored on the signal exactly as Threshold's is.
};

struct Signal {
    QString                  id;
    SignalTest               test = SignalTest::OutsideCorridor;
    QStringList              measures;     // 1 for corridor/threshold, 2 for order/ratio
    std::optional<Direction> direction;
    // The authored number, for the two tests that grade against one no norm supplies: Threshold,
    // and Ratio — whose quotient cannot key a norm at all. Required on both and refused on the
    // other two, which inherit their numbers from the catalogue (signalThreshold).
    std::optional<double>    threshold;
    // ORDER ONLY: the links further down the chain, in order, tried when measures[1] produced no
    // value. The kinematic sequence is a chain — hips, chest, lead arm, club — and a link this
    // capture never placed must not end the check: the hips are compared with the next link that
    // WAS placed. An order signal with no fallback is the plain two-event test. The literal
    // kImpactLink ("impact") may end the list: every time here is read from impact, so impact is the
    // chain's last link and is always placed, at 0 ms with no σ of its own.
    QStringList              fallback;
    // A time-from-impact measure (ms, negative = before impact) that must be BEFORE impact for this
    // signal to mean anything; at or after impact the signal is not assessable (PeakAfterImpact).
    // Hip braking is the case: speed at impact over peak speed reads "slowed down" when the peak
    // has not happened yet and the hips are still speeding up through the ball.
    QString                  requiresBeforeImpact;
};

// The chain's last link in Signal::fallback: impact itself, t = 0 on every time-from-impact measure.
inline constexpr const char *kImpactLink = "impact";

// ── Enum <-> string (the JSON spelling) ─────────────────────────────────────
QString measureKindName(MeasureKind k);
bool    measureKindFromName(const QString &s, MeasureKind &out);
QString measureStatusName(MeasureStatus s);
QString measureStatusLabel(MeasureStatus s);   // "Live", "No producer", … — the UI's own words
bool    measureStatusFromName(const QString &s, MeasureStatus &out);
QString shapeName(Shape s);
QString shapeLabel(Shape s);                   // "Higher is better", … — never the bare enum word
bool    shapeFromName(const QString &s, Shape &out);

// True when only ONE tail of this shape grades, so a signal watching the other can never fire and a
// corridor drawn with two hard edges would state a bound the norm does not hold.
inline bool shapeIsOneSided(Shape s) { return s != Shape::Target; }
QString signalTestName(SignalTest t);
// "past a threshold", not "threshold". Every other enum here already had one and this did not, so
// the editor printed the bare token — which reads as a category to somebody who already knows the
// vocabulary and as nothing at all to somebody choosing between them.
QString signalTestLabel(SignalTest t);
bool    signalTestFromName(const QString &s, SignalTest &out);

// The tests, in the order an author meets them. One definition, for the same reason
// allConditionGroups() exists: a picker and a filter row that each spell the list lose step.
const std::vector<SignalTest> &allSignalTests();
QString directionName(Direction d);
bool    directionFromName(const QString &s, Direction &out);

// ── Which tail fires, in the measure's own words ────────────────────────────
//
// ONE rule, here, because every surface that shows or sets a direction has to say the same thing:
// the picker where it is chosen, the signal row where it is changed, and the read-only detail page
// where a reader decides whether it is right. Three copies of this phrasing would eventually
// disagree, and the disagreement would be about which side of a corridor fires.
//
// With no `highMeans` authored it degrades to Too much / Too little — authorable, but saying
// nothing about the swing, which is the state that let three signals ship inverted.
//
// The LOW tail quotes the SAME authored sentence as "the other end of that range". No opposite is
// generated: a generated opposite reads like content and would be nobody's words.
struct DirectionPhrase {
    QString label;      // "Too much" / "Too little" — the chip
    QString means;      // this tail in the measure's own words; empty when none is authored
    QString sentence;   // the whole statement, for a row or a detail page
};

DirectionPhrase directionPhrase(Direction d, const QString &highMeans);

} // namespace pinpoint::analysis
