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

// THE GATE CATALOGUE (docs/design/trust_and_gates_design.md §5) — every gate a producer can record,
// written once: its title, the paragraph that explains it, the sentence its records fill, the tuned
// constants it reads (with their CURRENT values, straight from pp_tuned_constants.h), and the routes
// that do not have it. Every surface reads its words from here, so the chart, the diagnostics and the
// docs cannot drift apart. A record (Analysis/gate_record.h) carries only evidence.
//
// ONLY GATES THAT ARE EMITTED. An entry joins when its producer starts firing it (gate_catalogue_test
// ties the two both ways: every id fired in a producer source is here, every entry here is fired by
// the producer it names). Stage 2 (16 Oct 2026): the kinematic sequence. The rest of the §8 census
// joins area by area (stage 6).
//
// IDS ARE GATE NAMES, not tuning keys: one gate can read several constants (`tuning`), and a switch
// is not a gate. Header-only on purpose — the producers, the tools and the GUI all link it with no
// source-list edits.
//
// TEMPLATES. `{key}` is replaced from the record's evidence; `{key:fmt}` formats it — `t` an instant
// from impact ("−129 ms"), `ms` a duration ("47 ms"), `deg`, `mph`, `px`, `route` (a route id as
// words), `code` (the entry's `codes` phrase for that code, itself a template). `[ … ]` is optional:
// dropped whole when any key inside it is missing. Evidence never carries prose, so a record read
// with revised words still says the right thing.

#include "../Core/pp_tuned_constants.h"
#include "../Analysis/gate_record.h"

#include <QChar>
#include <QHash>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <cmath>
#include <vector>

namespace pinpoint::gates {

using analysis::GateEffect;

struct GateTuning {
    QString key;        // the tuning-override key ("sequence.sightedTurnDeg")
    double  value;      // its compiled value
    QString unit;       // "°", "ms", "mph", "" (a ratio), "switch" (1 on / 0 off)
};

struct GateEntry {
    QString                 id;            // "sequence.sightedBand"
    QString                 area;          // "Kinematic sequence"
    QString                 title;         // "Out of the camera's sight" — the short form on a figure
    QString                 shortText;     // "out of sight" — lower-case, for a figure slot
    QString                 explain;       // one paragraph for the reader
    QString                 evidenceText;  // filled from a record's evidence (see TEMPLATES above)
    std::vector<GateEffect> effects;       // what it can produce
    std::vector<GateTuning> tuning;
    // Routes that do not have this gate, as data; "subject:route" limits one to a subject
    // ("ks.pelvis:pelvisImu"). liftText() makes the words.
    QStringList             lift;
    QString                 design;        // the doc section that justifies it
    int                     since = 1;     // the analysis.gatesVersion that introduced it
    QString                 producer;      // repo-relative source that fires it (the both-ways test)
    QHash<QString, QString> codes;         // for evidence carrying a "code": code → phrase template
    // SETUP-BOUND: the subjects this gate binds because of where the capture stands, not because of the
    // swing — it fires on every swing whose route for the subject is not one of `lift`. Only such a gate
    // may be forecast before a swing (the Setup page's line, trust_and_gates_design.md §7.4); the rest
    // depend on the swing and are said only once they fire. Empty for every gate but the sighted band.
    QStringList             setupBound;
    // A RULE, not a gate: decided when a screen is read (a session minimum, a statistical test, a
    // chart's sample-size floor), so it leaves no per-swing record. Catalogued so its explanation and
    // threshold live in one place and the Gates page lists it; `PP_GATE` still marks its code site,
    // so the both-ways test ties it to the code. Its `evidenceText` may be empty.
    bool                    rule = false;
};

namespace detail {

// The shorthand the per-area catalogue files write their entries with (stage 6): the fields every
// entry has, positionally; tuning, lifts, codes and the rest are set on the returned entry at once.
inline GateEntry &add(std::vector<GateEntry> &c, const char *id, const QString &area, const char *title,
                      const char *shortText, const QString &explain, const char *evidenceText,
                      std::vector<GateEffect> effects, const char *producer, const char *design)
{
    GateEntry e;
    e.id = QString::fromLatin1(id);
    e.area = area;
    e.title = QString::fromUtf8(title);
    e.shortText = QString::fromUtf8(shortText);
    e.explain = explain;
    e.evidenceText = QString::fromUtf8(evidenceText);
    e.effects = std::move(effects);
    e.producer = QString::fromLatin1(producer);
    e.design = QString::fromLatin1(design);
    e.since = 2;
    c.push_back(std::move(e));
    return c.back();
}
// A rule (see GateEntry::rule): no effects recorded on a swing, no evidence sentence required.
inline GateEntry &rule(std::vector<GateEntry> &c, const char *id, const QString &area, const char *title,
                       const char *shortText, const QString &explain, const char *producer, const char *design)
{
    GateEntry &e = add(c, id, area, title, shortText, explain, "", { GateEffect::Withheld }, producer, design);
    e.rule = true;
    return e;
}
inline GateTuning tune(const char *key, double value, const char *unit)
{
    return { QString::fromLatin1(key), value, QString::fromUtf8(unit) };
}

} // namespace detail

namespace detail {

inline GateTuning sw(const char *key, bool on) { return { QString::fromLatin1(key), on ? 1.0 : 0.0, QStringLiteral("switch") }; }

} // namespace detail
} // namespace pinpoint::gates

#include "gate_catalogue_body.h"
#include "gate_catalogue_club.h"
#include "gate_catalogue_shot.h"
#include "gate_catalogue_lm.h"
#include "gate_catalogue_rules.h"

namespace pinpoint::gates {
namespace detail {

inline std::vector<GateEntry> buildCatalogue()
{
    namespace S = tuned::sequence;
    const QString seq  = QStringLiteral("Kinematic sequence");
    const QString prod = QStringLiteral("src/Kinematics/segment_rates.cpp");
    const QString ksDesign = QStringLiteral("docs/design/kinematic_sequence_design.md");
    std::vector<GateEntry> c;

    c.push_back({ QStringLiteral("sequence.sightedBand"), seq,
                  QStringLiteral("Out of the camera's sight"), QStringLiteral("out of sight"),
                  QStringLiteral("A single face-on camera sees the hips and shoulders turn only while they are turned "
                                 "away from it. Within %1° of square it cannot tell how fast they are turning, so a "
                                 "peak there cannot be timed. The curve is still drawn through that stretch, but its "
                                 "values there are the ones the camera cannot vouch for.").arg(S::kSightedTurnDeg),
                  QStringLiteral("Out of sight from {sightLostMs:t}[ to {sightBackMs:t}][; the rate was still {rateAtEdge} as sight was lost]."),
                  { GateEffect::Masked, GateEffect::Bounded, GateEffect::Refused },
                  { { QStringLiteral("sequence.sightedTurnDeg"), S::kSightedTurnDeg, QStringLiteral("°") } },
                  { QStringLiteral("faceOn+dtl"), QStringLiteral("ks.pelvis:pelvisImu"), QStringLiteral("ks.thorax:thoraxImu") },
                  ksDesign + QStringLiteral(" §12.4"), 1, prod, {},
                  { QStringLiteral("ks.pelvis"), QStringLiteral("ks.thorax") } });

    c.push_back({ QStringLiteral("sequence.validityHole"), seq,
                  QStringLiteral("A gap in the track"), QStringLiteral("in a gap"),
                  QStringLiteral("The two-camera and 3-D routes throw away frames they cannot trust: hips or shoulders "
                                 "whose left and right labels swapped, frames too far apart to bridge. A peak at the "
                                 "edge of such a gap may lie inside it, so it is bounded rather than timed."),
                  QStringLiteral("No usable frames from {sightLostMs:t}[ to {sightBackMs:t}]."),
                  { GateEffect::Masked, GateEffect::Bounded, GateEffect::Refused },
                  { { QStringLiteral("sequence.pairMaxGapFrames"), S::kPairMaxGapFrames, QStringLiteral("frames") } },
                  { QStringLiteral("ks.pelvis:pelvisImu"), QStringLiteral("ks.thorax:thoraxImu") },
                  ksDesign + QStringLiteral(" §5.2"), 1, prod, {} });

    c.push_back({ QStringLiteral("sequence.reversalSpike"), seq,
                  QStringLiteral("A spike, not a peak"), QStringLiteral("a spike"),
                  QStringLiteral("A body segment that has just changed direction cannot be at its peak rate a moment "
                                 "later: the chest typically peaks about 200 ms after it starts to unwind. A 'peak' this "
                                 "soon after the rate crosses zero is the keypoints jumping as the arms cross the chest, "
                                 "and the view saw nothing there to time."),
                  QStringLiteral("The highest rate came {afterReversalMs:ms} after the turn reversed at {reversalMs:t}; a peak needs at least {minAfterReversalMs:ms}."),
                  { GateEffect::Refused },
                  { { QStringLiteral("sequence.minAfterReversalMs"), S::kMinAfterReversalMs, QStringLiteral("ms") } },
                  { QStringLiteral("ks.pelvis:pelvisImu"), QStringLiteral("ks.thorax:thoraxImu") },
                  ksDesign + QStringLiteral(" §12.4"), 1, prod, {} });

    c.push_back({ QStringLiteral("sequence.risingAtEnd"), seq,
                  QStringLiteral("Still speeding up"), QStringLiteral("still rising"),
                  QStringLiteral("The rate was still climbing at the end of the stretch the sequence is measured over, "
                                 "so the segment peaked after it. That is a reading, not a fault: it did not peak in "
                                 "the downswing."),
                  QStringLiteral("Still rising at {domainEndMs:t}."),
                  { GateEffect::Bounded },
                  { sw("sequence.throughP8", S::kThroughP8) },
                  {},
                  ksDesign + QStringLiteral(" §12.4"), 1, prod, {} });

    c.push_back({ QStringLiteral("sequence.routePlacement"), seq,
                  QStringLiteral("This route may not time it"), QStringLiteral("not timed by this route"),
                  QStringLiteral("Some routes may draw a segment's curve but not time its peak, because on graded swings "
                                 "their peaks did not agree with a better instrument. The curve and any bound it found "
                                 "are kept; the peak is not placed. This is a tuning switch, not a reading of this swing."),
                  QStringLiteral("Timing switched off for the {route:route} route[ ({switch})]."),
                  { GateEffect::Refused },
                  { sw("sequence.faceOnTrunkPlacement", S::kFaceOnTrunkPlacement),
                    sw("sequence.pairTrunk.placement", S::kPairTrunkPlacement),
                    sw("sequence.pairTrunk.thoraxPlacement", S::kPairTrunkThoraxPlacement),
                    sw("sequence.skel3d.placement", S::kSkel3dPlacement),
                    sw("sequence.skel3d.thoraxPlacement", S::kSkel3dThoraxPlacement) },
                  { QStringLiteral("ks.pelvis:pelvisImu"), QStringLiteral("ks.thorax:thoraxImu") },
                  ksDesign + QStringLiteral(" §9, §12"), 1, prod, {} });

    c.push_back({ QStringLiteral("sequence.maxPlaceSigma"), seq,
                  QStringLiteral("Timing too uncertain"), QStringLiteral("timing uncertain"),
                  QStringLiteral("A peak is placed only when its instant is known to within %1 ms (1σ), twice a "
                                 "professional pelvis-to-chest gap. Wider than that, the order of the segments cannot "
                                 "be read from it.").arg(S::kMaxPlaceSigmaMs),
                  QStringLiteral("Timing σ {tSigmaMs:ms}, over the {maxPlaceSigmaMs:ms} ceiling."),
                  { GateEffect::Refused },
                  { { QStringLiteral("sequence.maxPlaceSigmaMs"), S::kMaxPlaceSigmaMs, QStringLiteral("ms") } },
                  { QStringLiteral("ks.pelvis:pelvisImu"), QStringLiteral("ks.thorax:thoraxImu"), QStringLiteral("ks.club:clubSensorFused") },
                  ksDesign + QStringLiteral(" §3.2"), 1, prod, {} });

    c.push_back({ QStringLiteral("sequence.credibleClub"), seq,
                  QStringLiteral("Club track not credible"), QStringLiteral("track not credible"),
                  QStringLiteral("The club's peak is timed only on a track whose own clubhead speed is believable. Below "
                                 "%1 mph the track has broken, and its shaft angle is a straight line between "
                                 "anchors.").arg(S::kMinCredibleClubMph),
                  QStringLiteral("The track's peak clubhead speed was {clubheadPeakMph:mph}, under {minCredibleClubMph:mph}."),
                  { GateEffect::Refused },
                  { { QStringLiteral("sequence.minCredibleClubMph"), S::kMinCredibleClubMph, QStringLiteral("mph") } },
                  { QStringLiteral("ks.club:clubSensorFused") },
                  ksDesign + QStringLiteral(" §6"), 1, prod, {} });

    c.push_back({ QStringLiteral("sequence.clubWithheld"), seq,
                  QStringLiteral("Drawn, not judged"), QStringLiteral("not judged"),
                  QStringLiteral("Through impact the camera's shaft-angle rate collapses and wobbles, while the "
                                 "clubhead's own speed peaks about 2 ms before the ball. The club's peak is drawn with "
                                 "a σ that spans to impact, and no finding is drawn from it."),
                  QStringLiteral("The camera's club peak at {peakMs:t}; its σ spans to impact."),
                  { GateEffect::Withheld },
                  {},
                  { QStringLiteral("ks.club:clubSensorFused") },
                  QStringLiteral("known bug KB-18 (15 Oct 2026)"), 1, prod, {} });

    c.push_back({ QStringLiteral("sequence.pairRoute"), seq,
                  QStringLiteral("Two-camera route stood aside"), QStringLiteral("two-camera route stood aside"),
                  QStringLiteral("The two-camera route times the hips and shoulders from both views together, with no "
                                 "blind stretch at square. It stands aside when its inputs cannot be trusted on this "
                                 "swing, and the segment falls back to the route below."),
                  QStringLiteral("{code:code}."),
                  { GateEffect::Downgraded },
                  { { QStringLiteral("sequence.addrMinFrames"), double(S::kAddrMinFrames), QStringLiteral("frames") },
                    { QStringLiteral("sequence.pairMinExtentPx"), S::kPairMinExtentPx, QStringLiteral("px") } },
                  { QStringLiteral("ks.pelvis:pelvisImu"), QStringLiteral("ks.thorax:thoraxImu") },
                  ksDesign + QStringLiteral(" §5.2"), 1, prod,
                  { { QStringLiteral("noAddress"),        QStringLiteral("No address position on this swing") },
                    { QStringLiteral("addressFrames"),    QStringLiteral("Too few usable address frames ({framesFo} face-on, {framesDtl} down-the-line; {minFrames} needed)") },
                    { QStringLiteral("extent"),           QStringLiteral("The body is too small in the frame ({extentFoPx:px} face-on, {extentDtlPx:px} down-the-line; {minExtentPx:px} needed)") },
                    { QStringLiteral("admittedFrames"),   QStringLiteral("Too few usable frames ({framesFo} face-on, {framesDtl} down-the-line)") },
                    { QStringLiteral("labelsContradict"), QStringLiteral("The face-on left and right labels contradict the impact frame") },
                    { QStringLiteral("separations"),      QStringLiteral("Too few usable frames left after the guards ({separationsFo} face-on, {separationsDtl} down-the-line)") },
                    { QStringLiteral("orientation"),      QStringLiteral("The two views could not be oriented to each other") },
                    { QStringLiteral("dtlInterval"),      QStringLiteral("The down-the-line video has no usable frame interval") },
                    { QStringLiteral("dtlSeparations"),   QStringLiteral("Too few usable down-the-line frames ({separationsDtl})") },
                    { QStringLiteral("pairedSamples"),    QStringLiteral("Too few frames pair across the two views ({paired})") },
                    { QStringLiteral("pairedInDomain"),   QStringLiteral("Too few frames pair across the two views in the downswing ({paired}; {minPaired} needed)") } } });

    c.push_back({ QStringLiteral("sequence.pairMinCorr"), seq,
                  QStringLiteral("The two views disagree"), QStringLiteral("views disagree"),
                  QStringLiteral("The down-the-line view must follow what the face-on view predicts for a rigid turn. "
                                 "When the two correlate below %1, the pairing is not trusted and the segment falls "
                                 "back to the route below.").arg(S::kPairMinCorr),
                  QStringLiteral("Correlation {corr} under the {minCorr} floor."),
                  { GateEffect::Downgraded },
                  { { QStringLiteral("sequence.pairMinCorr"), S::kPairMinCorr, QString() } },
                  { QStringLiteral("ks.pelvis:pelvisImu"), QStringLiteral("ks.thorax:thoraxImu") },
                  ksDesign + QStringLiteral(" §5.2"), 1, prod, {} });

    c.push_back({ QStringLiteral("sequence.noPeak"), seq,
                  QStringLiteral("Nothing to time"), QStringLiteral("nothing to time"),
                  QStringLiteral("A segment's peak is searched for between the transition and P8. When the route's curve "
                                 "has no usable samples there — the view lost the segment for the whole downswing — "
                                 "there is no peak to place and no bound to give."),
                  QStringLiteral("{samples} usable samples from {fromMs:t} to {toMs:t}."),
                  { GateEffect::Refused },
                  {},
                  { QStringLiteral("faceOn+dtl"), QStringLiteral("ks.pelvis:pelvisImu"), QStringLiteral("ks.thorax:thoraxImu") },
                  ksDesign + QStringLiteral(" §3"), 2, prod, {} });

    c.push_back({ QStringLiteral("sequence.routeWithdrawn"), seq,
                  QStringLiteral("Route withdrew"), QStringLiteral("route withdrew"),
                  QStringLiteral("A two-camera route that could neither time the peak nor bound it would tell the reader "
                                 "less than the face-on camera does, so it withdraws for this segment and the route "
                                 "below runs instead."),
                  QStringLiteral("The {route:route} route could neither time nor bound the peak[ (its timing is switched off: {switch})]."),
                  { GateEffect::Downgraded },
                  {},
                  { QStringLiteral("ks.pelvis:pelvisImu"), QStringLiteral("ks.thorax:thoraxImu") },
                  ksDesign + QStringLiteral(" §5.2, §11"), 1, prod, {} });

    // THE CHEST'S PROBABLE PEAK (gap_bridge.h; Mark, 25 Oct 2026): downgraded, not refused — the reading is kept
    // and judged, at the "estimated" level, with the ⚠ that says how it was read.
    c.push_back({ QStringLiteral("sequence.gapBridged"), seq,
                  QStringLiteral("Bridged across a gap"), QStringLiteral("probable"),
                  QStringLiteral("The face-on camera loses the chest for a few frames at impact, where the shoulders square "
                                 "to it, so the chest's peak cannot be timed there. When the curve is measured either side of "
                                 "a short gap, a smooth fit (a Gaussian process) bridges it and reads the peak the gap hides, "
                                 "with a ± from the fit. The peak is PROBABLE, not measured: the fit assumes %1 °/s of noise "
                                 "in each sample and that the chest cannot spike inside the gap, and no trunk sensor has "
                                 "checked it yet. A taller hump earlier in the downswing, where the shoulder keypoints jump "
                                 "as the line squares to the camera, is read as the measurement, not the chest's peak.")
                      .arg(S::kGapBridgeNoiseDps),
                  QStringLiteral("Bridged across {gapMs:ms} ({gapFromMs:t} to {gapToMs:t}); the probable peak at {peakMs:t} ± {peakSigmaMs:ms}[, below a taller measured hump at {earlierMs:t}]."),
                  { GateEffect::Downgraded },
                  { { QStringLiteral("sequence.gapBridge.ellMs"), S::kGapBridgeEllMs, QStringLiteral("ms") },
                    { QStringLiteral("sequence.gapBridge.sfDps"), S::kGapBridgeSfDps, QStringLiteral("°/s") },
                    { QStringLiteral("sequence.gapBridge.noiseDps"), S::kGapBridgeNoiseDps, QStringLiteral("°/s") } },
                  { QStringLiteral("ks.thorax:thoraxImu") },
                  ksDesign + QStringLiteral(" §12.6"), 2, prod, {} });

    // ── stage 6: every other per-swing gate, and the on-read rules, area by area ──
    appendBodyGates(c);
    appendClubGates(c);
    appendShotGates(c);
    appendLaunchMonitorGates(c);
    appendRules(c);
    return c;
}

} // namespace detail

inline const std::vector<GateEntry> &gateCatalogue()
{
    static const std::vector<GateEntry> c = detail::buildCatalogue();
    return c;
}

inline const GateEntry *gateEntry(const QString &id)
{
    for (const GateEntry &e : gateCatalogue())
        if (e.id == id) return &e;
    return nullptr;
}

// A route id as the reader says it.
inline QString routeWords(const QString &route)
{
    static const QHash<QString, QString> w{
        { QStringLiteral("faceOn"),          QStringLiteral("face-on camera") },
        { QStringLiteral("faceOnClub"),      QStringLiteral("face-on club track") },
        { QStringLiteral("faceOn+dtl"),      QStringLiteral("two-camera") },
        { QStringLiteral("faceOn+dtl3d"),    QStringLiteral("3-D skeleton") },
        { QStringLiteral("pelvisImu"),       QStringLiteral("pelvis sensor") },
        { QStringLiteral("thoraxImu"),       QStringLiteral("chest sensor") },
        { QStringLiteral("leadArmImus"),     QStringLiteral("arm sensors") },
        { QStringLiteral("clubSensorFused"), QStringLiteral("club sensor") } };
    return w.value(route, route);
}

// ── Subjects ────────────────────────────────────────────────────────────────────────────────
// A record's subject as the reader names the reading ("ks.pelvis" → "Pelvis peak"). One table, read by
// the Trust panel, the Charts marks and the diagnostics' "not judged" reasons alike.
//
// A METRIC KEY ("pelvisSway") is named by the metric catalogue's label. This header cannot link the
// catalogue (the producers and tools include it alone), so the catalogue installs a labeller at
// start-up (metric_catalogue_manifest.cpp); without one a metric subject reads as its key.
using SubjectLabeller = QString (*)(const QString &metricKey);
inline SubjectLabeller &subjectLabeller() { static SubjectLabeller f = nullptr; return f; }

inline QString subjectLabel(const QString &subject)
{
    static const QHash<QString, QString> w{
        { QStringLiteral("ks.pelvis"),        QStringLiteral("Pelvis peak") },
        { QStringLiteral("ks.thorax"),        QStringLiteral("Chest peak") },
        { QStringLiteral("ks.leadArm"),       QStringLiteral("Lead arm peak") },
        { QStringLiteral("ks.club"),          QStringLiteral("Club peak") },
        { QStringLiteral("track.club"),       QStringLiteral("Club track") },
        { QStringLiteral("track.clubDtl"),    QStringLiteral("Down-the-line club track") },
        { QStringLiteral("track.club3d"),     QStringLiteral("3-D club") },
        { QStringLiteral("track.impact"),     QStringLiteral("Impact camera track") },
        { QStringLiteral("track.skeleton3d"), QStringLiteral("3-D skeleton") },
        { QStringLiteral("shot"),             QStringLiteral("The whole swing") } };
    if (w.contains(subject)) return w.value(subject);
    if (subject.startsWith(QLatin1String("route.")))
        return QStringLiteral("The ") + routeWords(subject.mid(6)) + QStringLiteral(" route");
    if (SubjectLabeller f = subjectLabeller()) {
        const QString l = f(subject);
        if (!l.isEmpty()) return l;
    }
    return subject;
}

// The subject a PRODUCED METRIC stands on: the kinematic sequence emits its node readings
// (segment_rates.cpp sequencePeakTimeSeries / sequenceNodeSeries) only for nodes it placed, so a
// missing `pelvisPeakTime` IS the pelvis node a gate refused. "" for a metric no record speaks for.
inline QString subjectForMetricKey(const QString &metricKey)
{
    static const struct { const char *prefix, *subject; } segs[] = {
        { "pelvis", "ks.pelvis" }, { "thorax", "ks.thorax" }, { "leadArm", "ks.leadArm" }, { "club", "ks.club" } };
    static const char *const kinds[] = { "PeakTime", "PeakSpeed", "ReversalTime" };
    for (const auto &sg : segs)
        for (const char *k : kinds)
            if (metricKey == QLatin1String(sg.prefix) + QLatin1String(k)) return QString::fromLatin1(sg.subject);
    return {};
}

// The record a claim about `subject` shows (trust_and_gates_design.md §3.3): of the records that
// refused, bounded or withheld it, the one that says the most — a bound first (out of sight, in a
// gap, still rising), then the gates that leave nothing, the σ ceiling last; a withheld record only
// when nothing refused. nullptr when no record acted on the subject (a masked band refuses nothing).
inline const analysis::GateRecord *primaryRecord(const std::vector<analysis::GateRecord> &recs, const QString &subject)
{
    static const QStringList rank{ QStringLiteral("sequence.sightedBand"), QStringLiteral("sequence.validityHole"),
                                   QStringLiteral("sequence.risingAtEnd"), QStringLiteral("sequence.reversalSpike"),
                                   QStringLiteral("sequence.routePlacement"), QStringLiteral("sequence.credibleClub"),
                                   QStringLiteral("sequence.maxPlaceSigma") };
    const auto score = [&](const analysis::GateRecord &r) {
        const int k = int(rank.indexOf(r.gate));
        switch (r.effect) {
        case GateEffect::Bounded:  return 0 + (k < 0 ? 99 : k);
        case GateEffect::Refused:  return 100 + (k < 0 ? 99 : k);
        case GateEffect::Withheld: return 300;
        default:                   return 1000;
        }
    };
    const analysis::GateRecord *best = nullptr;
    for (const analysis::GateRecord &r : recs)
        if ((r.subject == subject || r.covers.contains(subject)) && score(r) < 1000 && (!best || score(r) < score(*best)))
            best = &r;   // a cascade (stage 6) speaks for every reading it covers
    return best;
}

// A reading a gate kept out of a finding, in the words a "not judged" cell shows: "pelvis peak out of
// sight", "club peak not judged". Lower-case, no full stop: it is a clause.
inline QString notJudgedClause(const QString &subject, const QString &gateId);


namespace detail {

inline QString minusSigned(double v, int dec, bool plus)
{
    const double p = std::pow(10.0, dec);
    double x = std::round(v * p) / p;
    if (std::fabs(x) < 0.5 / p) x = 0.0;
    const QString body = QString::number(std::fabs(x), 'f', dec);
    return (x < 0 ? QString(QChar(0x2212)) : (plus && x > 0 ? QStringLiteral("+") : QString())) + body;
}

inline QString formatValue(const QVariant &v, const QString &fmt, const GateEntry &e, const QVariantMap &ev, int depth);

// Fill `tpl` from `ev`; false when a key it names is missing (so an optional block drops).
inline bool fill(const QString &tpl, const GateEntry &e, const QVariantMap &ev, int depth, QString &out)
{
    static const QRegularExpression re(QStringLiteral("\\{([A-Za-z0-9_]+)(?::([a-z]+))?\\}"));
    out.clear();
    qsizetype last = 0;
    bool ok = true;
    auto it = re.globalMatch(tpl);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        out += tpl.mid(last, m.capturedStart() - last);
        last = m.capturedEnd();
        const QString key = m.captured(1);
        if (!ev.contains(key)) { ok = false; continue; }
        out += formatValue(ev.value(key), m.captured(2), e, ev, depth);
    }
    out += tpl.mid(last);
    return ok;
}

inline QString formatValue(const QVariant &v, const QString &fmt, const GateEntry &e, const QVariantMap &ev, int depth)
{
    const QChar nb(0x00A0);
    if (fmt == QLatin1String("code")) {
        const QString phrase = e.codes.value(v.toString());
        if (phrase.isEmpty() || depth > 2) return v.toString();
        QString s;
        fill(phrase, e, ev, depth + 1, s);
        return s;
    }
    if (fmt == QLatin1String("route")) return routeWords(v.toString());
    if (v.typeId() == QMetaType::QString) return v.toString();
    if (v.typeId() == QMetaType::Bool) return v.toBool() ? QStringLiteral("on") : QStringLiteral("off");
    const double d = v.toDouble();
    if (fmt == QLatin1String("t"))   return minusSigned(d, 0, true) + nb + QStringLiteral("ms");
    if (fmt == QLatin1String("ms"))  return minusSigned(d, 0, false) + nb + QStringLiteral("ms");
    if (fmt == QLatin1String("deg")) return minusSigned(d, 0, false) + QStringLiteral("°");
    if (fmt == QLatin1String("mph")) return minusSigned(d, 0, false) + nb + QStringLiteral("mph");
    if (fmt == QLatin1String("px"))  return minusSigned(d, 0, false) + nb + QStringLiteral("px");
    if (fmt == QLatin1String("pct")) return minusSigned(d * 100.0, 0, false) + QStringLiteral("%");   // a fraction 0..1
    if (fmt == QLatin1String("cm"))  return minusSigned(d, 0, false) + nb + QStringLiteral("cm");
    if (fmt == QLatin1String("in"))  return minusSigned(d, 1, false) + nb + QStringLiteral("in");
    const bool whole = std::fabs(d - std::round(d)) < 1e-9;
    return minusSigned(d, whole ? 0 : 2, false);
}

} // namespace detail

// The record's evidence sentence: the entry's template, optional `[ … ]` blocks dropped when a key
// inside is missing.
inline QString evidenceText(const GateEntry &e, const QVariantMap &evidence)
{
    QString out;
    const QString &t = e.evidenceText;
    qsizetype i = 0;
    while (i < t.size()) {
        const qsizetype open = t.indexOf(QLatin1Char('['), i);
        const qsizetype stop = open < 0 ? t.size() : open;
        QString part;
        detail::fill(t.mid(i, stop - i), e, evidence, 0, part);
        out += part;
        if (open < 0) break;
        const qsizetype close = t.indexOf(QLatin1Char(']'), open);
        if (close < 0) break;
        QString opt;
        if (detail::fill(t.mid(open + 1, close - open - 1), e, evidence, 0, opt)) out += opt;
        i = close + 1;
    }
    return out;
}

inline QString notJudgedClause(const QString &subject, const QString &gateId)
{
    const GateEntry *e = gateEntry(gateId);
    const QString who = subjectLabel(subject).toLower();
    if (!e) return who;
    // a gate that only withholds keeps the reading on screen: "club peak drawn, not judged"
    const bool onlyWithholds = e->effects.size() == 1 && e->effects.front() == GateEffect::Withheld;
    return who + QLatin1Char(' ') + (onlyWithholds ? e->title.left(1).toLower() + e->title.mid(1) : e->shortText);
}

// One gate behind a reading, as every surface lists it (the Trust panel, the in-place block, the
// diagnostics' reason): { gate, effect, effectWord, title, evidence, explain, tuning, lift, primary }.
// `tried` = routes the swing already ran for the subject, never offered as a lift.
inline QVariantMap gateItem(const GateEntry &e, const QString &effect, const QVariantMap &evidence,
                            const QString &subject, const QStringList &tried, bool primary);

// "Timed on swings with the down-the-line camera or a pelvis sensor." — the routes without this gate
// that apply to `subject` (all of them when the subject is empty), less the routes this swing already
// ran (`tried`: a two-camera swing whose two-camera route withdrew is not told to add the camera it
// has). Empty when none would lift it.
inline QString liftText(const GateEntry &e, const QString &subject = QString(), const QStringList &tried = {})
{
    QStringList words;
    for (const QString &l : e.lift) {
        const qsizetype c = l.lastIndexOf(QLatin1Char(':'));
        if (c > 0 && !subject.isEmpty() && l.left(c) != subject) continue;
        const QString route = c > 0 ? l.mid(c + 1) : l;
        if (tried.contains(route)) continue;
        const QString w = route == QLatin1String("faceOn+dtl") ? QStringLiteral("the down-the-line camera")
                                                                : QStringLiteral("a ") + routeWords(route);
        if (!words.contains(w)) words << w;
    }
    if (words.isEmpty()) return {};
    QString list = words.size() == 1 ? words.first()
                 : QStringList(words.mid(0, words.size() - 1)).join(QStringLiteral(", ")) + QStringLiteral(" or ") + words.last();
    // the sequence times a peak; every other area measures a reading
    return (e.area == QLatin1String("Kinematic sequence") ? QStringLiteral("Timed") : QStringLiteral("Measured"))
           + QStringLiteral(" on swings with ") + list + QStringLiteral(".");
}

// Several subjects at once ("pelvis peak and chest peak … timed on swings with the down-the-line camera, a
// pelvis sensor or a chest sensor."): the union of each subject's lifts, in catalogue order.
inline QString liftTextAll(const GateEntry &e, const QStringList &subjects, const QStringList &tried)
{
    QStringList words;
    for (const QString &l : e.lift) {
        const qsizetype c = l.lastIndexOf(QLatin1Char(':'));
        if (c > 0 && !subjects.contains(l.left(c))) continue;
        const QString route = c > 0 ? l.mid(c + 1) : l;
        if (tried.contains(route)) continue;
        const QString w = route == QLatin1String("faceOn+dtl") ? QStringLiteral("the down-the-line camera")
                                                                : QStringLiteral("a ") + routeWords(route);
        if (!words.contains(w)) words << w;
    }
    if (words.isEmpty()) return {};
    QString list = words.size() == 1 ? words.first()
                 : QStringList(words.mid(0, words.size() - 1)).join(QStringLiteral(", ")) + QStringLiteral(" or ") + words.last();
    // the sequence times a peak; every other area measures a reading
    return (e.area == QLatin1String("Kinematic sequence") ? QStringLiteral("Timed") : QStringLiteral("Measured"))
           + QStringLiteral(" on swings with ") + list + QStringLiteral(".");
}

// Does `route` lift this gate for `subject` (a lift with no subject lifts it for every subject)?
inline bool liftedBy(const GateEntry &e, const QString &subject, const QString &route)
{
    for (const QString &l : e.lift) {
        const qsizetype c = l.lastIndexOf(QLatin1Char(':'));
        if (c > 0 ? (l.left(c) == subject && l.mid(c + 1) == route) : l == route) return true;
    }
    return false;
}

// The gate's words for a reading it acted on, as a claim says them: a gate that only withholds keeps the
// reading on screen ("drawn, not judged"); the rest give their short words ("out of sight").
inline QString claimWords(const GateEntry &e)
{
    const bool onlyWithholds = e.effects.size() == 1 && e.effects.front() == GateEffect::Withheld;
    return onlyWithholds ? e.title.left(1).toLower() + e.title.mid(1) : e.shortText;
}

inline QVariantMap gateItem(const GateEntry &e, const QString &effect, const QVariantMap &evidence,
                            const QString &subject, const QStringList &tried, bool primary)
{
    const auto effectWord = [&e](const QString &x) {
        if (x == QLatin1String("withheld"))   return QStringLiteral("not judged");
        // the chest's probable peak did not fall back to another route: a fit bridged the route's own gap
        if (x == QLatin1String("downgraded") && e.id == QLatin1String("sequence.gapBridged")) return QStringLiteral("bridged");
        if (x == QLatin1String("downgraded")) return QStringLiteral("fell back");
        if (x == QLatin1String("masked"))     return QStringLiteral("marked on the chart");
        return x;
    };
    QStringList tun;
    for (const GateTuning &t : e.tuning) {
        if (t.unit == QLatin1String("switch")) continue;
        const QString v = detail::minusSigned(t.value, std::fabs(t.value - std::round(t.value)) < 1e-9 ? 0 : 2, false);
        tun << (t.unit == QStringLiteral("°") ? v + t.unit : t.unit.isEmpty() ? v : v + QChar(0x00A0) + t.unit);
    }
    return { { QStringLiteral("gate"), e.id },
             { QStringLiteral("effect"), effect },
             { QStringLiteral("effectWord"), effectWord(effect) },
             { QStringLiteral("title"), e.title },
             { QStringLiteral("evidence"), evidenceText(e, evidence) },
             { QStringLiteral("explain"), e.explain },
             { QStringLiteral("tuning"), tun.isEmpty() ? QString() : QStringLiteral("Threshold ") + tun.join(QStringLiteral(", ")) },
             { QStringLiteral("lift"), liftText(e, subject, tried) },
             { QStringLiteral("primary"), primary } };
}

} // namespace pinpoint::gates
