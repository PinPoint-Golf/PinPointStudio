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

// Firing a shot's findings: see fire_findings.h. Extracted from SessionDiagnosticsModel::detectShot
// (refactor step 3.3) line for line — every rule and every word below is the one that ran there, so the
// ledger the model writes is byte-identical (parity A).

#include "fire_findings.h"

#include "characteristic_engine.h"
#include "../norms/context_tree.h"                // saveContextTree (the stamp)
#include "../norms/norm_pack.h"                   // saveNormPack (the stamp)
#include "../packs/characteristic_pack.h"         // savePack, isSessionScopeCondition, packMeasureGrades
#include "../../Coaching/club_distances.h"        // classifySwingLength
#include "../../Coaching/firing_side.h"           // conditionFiringSide (#16)
#include "../../Coaching/sequence_checks.h"       // the sequence checks, their spread and their gap words
#include "../../Core/club_vocabulary.h"           // the baseline's keys are canonical club ids

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace pinpoint::analysis {

namespace {

// THE AUTHORED NUMBER A THRESHOLD SIGNAL FIRES AT, or NaN for any other test (#16): a threshold reading's
// distance is measured against its own line (ConditionRow::faultAt), not the band its measure carries.
double thresholdOf(const CharacteristicPack &pack, const QString &signalId)
{
    const Signal *s = pack.signal(signalId);
    return s && s->test == SignalTest::Threshold && s->threshold ? *s->threshold
                                                                 : std::numeric_limits<double>::quiet_NaN();
}

// The corridor's shape as the ledger row records it — the one translation between
// MeasureEvidence's two open-side flags and CorridorShape, so there is exactly one place
// that decides what a missing scale means.
CorridorShape corridorShapeOf(const MeasureEvidence &e)
{
    if (!e.hasCorridor) return CorridorShape::None;              // graded against a number
    if (!(e.corridorHi - e.corridorLo > 0.0)) return CorridorShape::None;   // degenerate band
    if (e.lowOpen && e.highOpen) return CorridorShape::None;     // nothing to normalise by
    if (e.lowOpen)  return CorridorShape::Ceiling;
    if (e.highOpen) return CorridorShape::Floor;
    return CorridorShape::TwoSided;
}

// The status of the first measure in `measureIds` the pack does not grade — the one behind a NotLive
// refusal (the engine names it among the signal's missing measures). Empty when none is non-live.
QString notLiveStatusOf(const CharacteristicPack &pack, const QStringList &measureIds)
{
    for (const QString &mid : measureIds)
        if (const Measure *m = pack.measure(mid); m && !measureStatusGrades(m->status))
            return measureStatusName(m->status);
    return QString();
}

// The reason a row or a sequence check reads when the engine refused a reading it HAD (Finding::refusal)
// — the sequence checks' existing sentences where they have one, else a mechanical one of the same kind.
// Empty for a refusal that is not one of the engine's rules (signalRefusalRuleId).
QString refusalReason(SignalRefusal k, const QString &measureId)
{
    switch (k) {
    case SignalRefusal::NoCorridor:             return QStringLiteral("no range to judge it against on this shot");
    case SignalRefusal::NonPositiveDenominator: return QStringLiteral("the ratio could not be formed on this swing");
    case SignalRefusal::MixedScale:             return sequenceGapReason(SequenceGap::MixedScale, measureId);
    case SignalRefusal::Implausible:            return sequenceGapReason(SequenceGap::Implausible, measureId);
    case SignalRefusal::PeakAfterImpact:        return sequenceGapReason(SequenceGap::PeakAfterImpact, measureId);
    case SignalRefusal::CompositeCoverage:      return missingReasonText(MissingKind::CompositeCoverage);
    default:                                    return QString();
    }
}

// A composite's parts and coverage, from the engine's evidence into the ledger's row (schema 9). The
// two structs are copies of one shape — the ledger header stands apart from the Diagnostics module —
// and this is the one place the copy is made. Empty for any measure that is not a composite.
CompositeRowEvidence compositeRowOf(const MeasureEvidence &e)
{
    CompositeRowEvidence c;
    c.weightRead  = e.partsWeightRead;
    c.weightTotal = e.partsWeightTotal;
    c.parts.reserve(e.parts.size());
    for (const CompositePart &p : e.parts)
        c.parts.push_back({ p.name, p.measureId, p.value, p.unit, p.z, p.weight, p.share, p.sign,
                            p.atStart, p.atEnd });
    for (const CompositeUnread &u : e.partsUnread)
        c.unread.push_back({ u.name, u.measureId, u.reason, u.status });
    return c;
}

// The reading a measure records, NaN when the capture had none (absent is never zero). A PLAIN read, for
// a number that is shown or stored: it does not ask the measure's status. A read a verdict rests on goes
// through gradedOrNaN() instead.
double measuredOrNaN(const LiveMeasureSource &src, const char *measureId)
{
    const auto v = src.value(QLatin1String(measureId));
    return (v && std::isfinite(v->value)) ? v->value : std::numeric_limits<double>::quiet_NaN();
}

// THE SAME READ FOR A VERDICT (#176): NaN — "not read" — when the measure is not live in the pack
// (packMeasureGrades, the engine's own rule), whatever the producer emitted.
double gradedOrNaN(const LiveMeasureSource &src, const CharacteristicPack &pack, const char *measureId)
{
    return packMeasureGrades(pack, QLatin1String(measureId)) ? measuredOrNaN(src, measureId)
                                                             : std::numeric_limits<double>::quiet_NaN();
}

QString normaliseHandedness(const QString &raw)
{
    if (raw.compare(QLatin1String("Left"), Qt::CaseInsensitive) == 0)  return QStringLiteral("Left");
    if (raw.compare(QLatin1String("Right"), Qt::CaseInsensitive) == 0) return QStringLiteral("Right");
    return QString();
}

// The baseline's keys are canonical bag ids; the detection asks with whatever spelling the swing carries.
class CanonicalClubBaseline final : public IClubBaselineProvider {
public:
    explicit CanonicalClubBaseline(std::shared_ptr<IClubBaselineProvider> inner) : m_inner(std::move(inner)) {}
    std::optional<ClubBaselineEntry> forClub(const QString &club) const override
    {
        return m_inner->forClub(canonicalClubId(club));
    }
private:
    std::shared_ptr<IClubBaselineProvider> m_inner;
};

// One condition's row on this shot: the engine's finding in the ledger's words, with the evidence the
// panel draws and the reason a not-assessable row reads.
ConditionRow conditionRowOf(const Finding &f, const LiveDetection &d, const LiveMeasureSource &src,
                            const CharacteristicPack &pack)
{
    ConditionRow r;
    r.conditionId      = f.conditionId;
    r.confidence       = f.confidence;
    r.material         = f.material;
    r.sharp            = f.sharp;
    r.pFire            = f.pFire;        // §A8.4 — −1 when not formed
    r.grossRisk        = f.grossRisk;
    r.quantified       = f.quantified;
    r.contextId        = d.contextId;
    r.firedSignalCount = int(f.firedSignals.size());
    // "CLUB ASSUMED" (rule.diag.inferredContext): the swing declared no club, so it was graded at
    // the default context and detect() already demoted its corridor readings. The row carries
    // the shot's fact so the ranking demotes it too (rankScores, work_ons.h).
    r.contextInferred  = d.contextInferred;

    switch (f.state) {
    case FindingState::Fired:
        r.state     = ShotState::Fired;
        r.direction = (f.direction == Direction::High) ? 1 : -1;
        break;
    case FindingState::NotFired:
        r.state     = ShotState::Clean;
        r.direction = 0;
        break;
    case FindingState::Unavailable:
        r.state = ShotState::NotAssessable;
        // Never blank (ConditionRow's contract): the review strip prints this where a
        // corridor would go. The source knows WHY for the measure that failed; where a
        // condition names several, the first is the one that stopped it.
        if (!f.missingMeasures.isEmpty())
            r.notAssessableReason = src.missingReason(f.missingMeasures.first());
        // A value the producer measured but marked not for judging (KB-18): its own reason.
        if (r.notAssessableReason.isEmpty() && !f.missingMeasures.isEmpty())
            if (const auto w = src.value(f.missingMeasures.first()); w && !w->withheld.isEmpty())
                r.notAssessableReason = w->withheld;
        if (r.notAssessableReason.isEmpty())
            r.notAssessableReason = missingReasonText(MissingKind::MetricNotProduced);
        // A REFUSAL THE ENGINE MADE ON A READING IT HAD (trust stage 6 session 2): no range to judge
        // against, a reading not believed, two scales, no ratio, a peak after impact. The measure
        // read, so the absence reasons above say nothing true ("metric not produced") — the row says
        // the refusal and carries the engine RULE behind it (rule.diag.*), with no subject: a rule
        // leaves no per-swing record, so there is no Trust-panel reading to open, only its words.
        // Re-derived on every read, so it holds on a swing analysed before records existed too.
        if (const char *rule = signalRefusalRuleId(f.refusal)) {
            r.notAssessableReason = refusalReason(f.refusal, f.missingMeasures.value(0));
            r.notJudgedGate       = QString::fromLatin1(rule);
            r.notJudgedSubject.clear();
            // A MEASURE THE PACK DOES NOT GRADE (#176): held, planned, no producer. Its value may be on
            // the swing, so "not produced" would be false; the row says it is held back and carries
            // the status, the token-plus-status a composite's unread part already carries.
            if (f.refusal == SignalRefusal::NotLive) {
                r.notLiveStatus       = notLiveStatusOf(pack, f.missingMeasures);
                r.notAssessableReason = notLiveWords(r.notLiveStatus);
            }
        } else {
            // THE GATE THAT STOPPED IT (trust_and_gates_design.md §4.4): a missing or withheld measure
            // standing on a sequence node a gate refused says which gate, in its words, and the row
            // carries the gate and the reading so the Trust panel can list what each gate blocked.
            for (const QString &mid : f.missingMeasures) {
                const LiveMeasureSource::GateCause gc = src.gateCause(mid);
                if (gc.gate.isEmpty()) continue;
                r.notJudgedGate       = gc.gate;
                r.notJudgedSubject    = gc.subject;
                r.notAssessableReason = gc.clause;      // "pelvis peak out of sight"
                break;
            }
        }
        break;
    }

    if (f.evidence.hasEvidence) {
        r.drivingMeasureId = f.evidence.drivingMeasureId;
        r.value            = f.evidence.value;
        r.corridorLo       = f.evidence.corridorLo;
        r.corridorHi       = f.evidence.corridorHi;
        r.z                = f.evidence.z;
        // WHICH SIDE THE BAND IS OPEN, recorded here because this is the only place it is
        // known: MeasureEvidence carries the flags, the ledger row carries the number they
        // scale, and the strength meter downstream cannot read one without the other.
        // Every branch that left z at 0 for want of a scale records None, so a meter is
        // never drawn over a reading that was graded against an authored number.
        r.corridorShape    = corridorShapeOf(f.evidence);
        // …and which of the band's ends THIS condition fires on (schema 10, #16), so a clean
        // reading out on the end it never fires on is not scored as a near miss.
        r.firingSide       = conditionFiringSide(pack, f.conditionId, r.drivingMeasureId);
        // …the threshold test's own line when one decided it, and whether every signal must fire —
        // what a clean row's distance is read against (rowStrength).
        r.faultAt          = thresholdOf(pack, f.evidence.drivingSignalId);
        if (const Condition *pc = pack.condition(f.conditionId)) r.allOf = pc->detection == DetectionMode::All;
        // Schema 9: a composite's parts and coverage, the evidence its detail page draws.
        r.composite        = compositeRowOf(f.evidence);
    }
    // Every signal's measure, when there is more than one to show (schema 4).
    if (f.readings.size() > 1) {
        for (const MeasureEvidence &e : f.readings) {
            MeasureRow m;
            m.measureId     = e.drivingMeasureId;
            m.signalId      = e.drivingSignalId;
            m.value         = e.value;
            m.corridorLo    = e.corridorLo;
            m.corridorHi    = e.corridorHi;
            m.z             = e.z;
            m.corridorShape = corridorShapeOf(e);
            m.firingSide    = conditionFiringSide(pack, f.conditionId, m.measureId);
            m.faultAt       = thresholdOf(pack, e.drivingSignalId);
            m.fired         = e.fired;
            m.composite     = compositeRowOf(e);
            r.readings.push_back(std::move(m));
        }
    }
    return r;
}

// One sequence check's verdict on this shot (schema 5), and why it could not be assessed — in golfer words,
// naming the missing piece where the engine's refusal says which it was.
SignalRow signalRowOf(const SignalOutcome &o, const LiveMeasureSource &src, const CharacteristicPack &pack)
{
    SignalRow g;
    g.signalId = o.signalId;
    if (o.available) {
        g.state      = o.fired ? ShotState::Fired : ShotState::Clean;
        g.pFire      = o.pFire;
        g.quantified = o.quantified;
        g.comparedWith = o.comparedWith;
    } else {
        g.state = ShotState::NotAssessable;
        const QString first = o.missing.value(0);
        SequenceGap gap = SequenceGap::Unknown;
        switch (o.refusal) {
        case SignalRefusal::MeasureMissing:
            // The KS keys are emitted only for placed nodes (§10.9 P3), so "not produced" on
            // one of them IS an unplaced node. Every other reason (no analysis, no impact)
            // is the pipeline's, and says so in its own words below.
            if (src.missingKind(first) == MissingKind::MetricNotProduced) gap = SequenceGap::NotPlaced;
            break;
        case SignalRefusal::MixedScale:  gap = SequenceGap::MixedScale;  break;
        case SignalRefusal::Implausible: gap = SequenceGap::Implausible; break;
        case SignalRefusal::PeakAfterImpact: gap = SequenceGap::PeakAfterImpact; break;
        case SignalRefusal::Withheld:    gap = SequenceGap::Withheld;    break;
        default: break;
        }
        g.reason = sequenceGapReason(gap, first);
        if (gap == SequenceGap::Unknown && o.refusal == SignalRefusal::MeasureMissing) {
            const MissingKind k = src.missingKind(first);
            if (k != MissingKind::None && k != MissingKind::MetricNotProduced)
                g.reason = missingReasonText(k);
        }
        // the gate behind an unplaced or withheld peak, as for the condition rows above
        if (o.refusal == SignalRefusal::MeasureMissing || o.refusal == SignalRefusal::Withheld)
            for (const QString &mid : o.missing) {
                const LiveMeasureSource::GateCause gc = src.gateCause(mid);
                if (gc.gate.isEmpty()) continue;
                g.notJudgedGate    = gc.gate;
                g.notJudgedSubject = gc.subject;
                g.reason           = gc.clause;
                break;
            }
        // ...and the engine rule behind a refusal on a reading it had, as for the rows above: the
        // sentence the gap already chose (or the rule's own where the gap had none), no subject
        if (const char *rule = signalRefusalRuleId(o.refusal)) {
            if (gap == SequenceGap::Unknown) {
                const QString why = refusalReason(o.refusal, first);
                if (!why.isEmpty()) g.reason = why;
            }
            g.notJudgedGate = QString::fromLatin1(rule);
            g.notJudgedSubject.clear();
            if (o.refusal == SignalRefusal::NotLive) {      // as the condition rows above (#176)
                g.notLiveStatus = notLiveStatusOf(pack, o.missing);
                g.reason        = notLiveWords(g.notLiveStatus);
            }
        }
    }
    return g;
}

// ── The stamp's canonical bytes (R-STAMP-1). Every number at full precision, every map in key order, so two
//    equal inputs hash the same on every run. ─────────────────────────────────────────────────────────────

void addNumber(QCryptographicHash &h, double v)
{
    h.addData(std::isnan(v) ? QByteArrayLiteral("nan") : QByteArray::number(v, 'g', 17));
    h.addData(QByteArrayLiteral(";"));
}

void addText(QCryptographicHash &h, const QString &s)
{
    const QByteArray b = s.toUtf8();
    h.addData(QByteArray::number(b.size()));   // length-prefixed: "ab"+"c" never hashes as "a"+"bc"
    h.addData(QByteArrayLiteral(":"));
    h.addData(b);
}

void addBand(QCryptographicHash &h, const FlightBand &b)
{
    addNumber(h, b.lo);
    addNumber(h, b.hi);
}

template <typename T, typename Add>
void addSorted(QCryptographicHash &h, const QHash<QString, T> &map, Add add)
{
    QStringList keys = map.keys();
    std::sort(keys.begin(), keys.end());
    h.addData(QByteArray::number(keys.size()));
    for (const QString &k : std::as_const(keys)) {
        addText(h, k);
        add(map.value(k));
    }
}

void addFlightNorms(QCryptographicHash &h, const FlightNorms &n)
{
    h.addData(QByteArrayLiteral("flight"));
    h.addData(QByteArray::number(n.schema));
    addSorted(h, n.categoryOf, [&h](const QString &v) { addText(h, v); });
    addSorted(h, n.categoryLabel, [&h](const QString &v) { addText(h, v); });
    addSorted(h, n.landing, [&h](const FlightBand &b) { addBand(h, b); });
    addSorted(h, n.apexCarry, [&h](const FlightBand &b) { addBand(h, b); });
    addBand(h, n.apexBand);
    addSorted(h, n.apexReference, [&h](const std::vector<FlightApexPoint> &pts) {
        h.addData(QByteArray::number(qsizetype(pts.size())));
        for (const FlightApexPoint &p : pts) { addNumber(h, p.ballSpeedMph); addNumber(h, p.apexYd); }
    });
    h.addData(QJsonDocument(n.sources).toJson(QJsonDocument::Compact));
    for (const QString &note : n.notes) addText(h, note);
}

void addClubBaseline(QCryptographicHash &h, const std::optional<QHash<QString, ClubBaselineEntry>> &b)
{
    h.addData(b ? QByteArrayLiteral("baseline") : QByteArrayLiteral("nobaseline"));
    if (!b) return;
    addSorted(h, *b, [&h](const ClubBaselineEntry &e) {
        addText(h, e.club);
        for (double v : { e.carryMean, e.carrySd, e.ballSpeedMean, e.ballSpeedSd, e.speedMean, e.smashMean,
                          e.shaftAngleP4Mean })
            addNumber(h, v);
        h.addData(QByteArray::number(e.n));
    });
}

void addUsuals(QCryptographicHash &h, const QHash<QString, ClubFlightUsual> &usuals)
{
    h.addData(QByteArrayLiteral("usuals"));
    addSorted(h, usuals, [&h](const ClubFlightUsual &u) {
        for (double v : { u.launch, u.smash, u.attack, u.carry }) addNumber(h, v);
    });
}

void addCohort(QCryptographicHash &h, const Cohort &c)
{
    h.addData(QByteArrayLiteral("cohort"));
    addText(h, c.sex ? sexName(*c.sex) : QString());
    addText(h, c.age ? ageBandName(*c.age) : QString());
}

} // namespace

// ── The inputs ──────────────────────────────────────────────────────────────────────────────────────────────

const ClubBaselineEntry *clubUsualOf(const FiringInputs &inputs, const QString &club)
{
    if (!inputs.clubBaseline) return nullptr;
    const auto it = inputs.clubBaseline->constFind(canonicalClubId(club));
    return it == inputs.clubBaseline->constEnd() ? nullptr : &it.value();
}

std::shared_ptr<IClubBaselineProvider> canonicalClubBaselineProvider(QHash<QString, ClubBaselineEntry> clubs)
{
    return std::make_shared<CanonicalClubBaseline>(makeClubBaselineProvider(std::move(clubs)));
}

QString firingStamp(const FiringInputs &inputs)
{
    QCryptographicHash h(QCryptographicHash::Sha1);
    // The rules' own versions: the row shape, its words, how a row is graded, and the numbers it is graded on.
    h.addData(QByteArrayLiteral("schema") + QByteArray::number(kDiagSchemaVersion));
    h.addData(QByteArrayLiteral("words") + QByteArray::number(kRowWordsVersion));
    h.addData(QByteArrayLiteral("grading") + QByteArray::number(kRowGradingVersion));
    h.addData(QByteArrayLiteral("grid") + QByteArray::number(kPhaseGridSchemaVersion));
    // The diagnostic graph and its corridors, as the content stamp hashes them today.
    h.addData(QByteArrayLiteral("pack"));
    if (inputs.pack) h.addData(QJsonDocument(savePack(inputs.pack->pack())).toJson(QJsonDocument::Compact));
    h.addData(QByteArrayLiteral("norms"));
    if (inputs.norms) {
        h.addData(QJsonDocument(saveNormPack(inputs.norms->norms())).toJson(QJsonDocument::Compact));
        h.addData(QJsonDocument(saveContextTree(inputs.norms->contexts())).toJson(QJsonDocument::Compact));
    }
    h.addData(QByteArrayLiteral("policy"));
    addText(h, inputs.gradePolicy);
    h.addData(QByteArrayLiteral("disabled"));
    QStringList disabled = inputs.disabledNormSets;
    std::sort(disabled.begin(), disabled.end());
    for (const QString &s : std::as_const(disabled)) addText(h, s);
    addFlightNorms(h, inputs.flightNorms);
    addClubBaseline(h, inputs.clubBaseline);
    addUsuals(h, inputs.usuals);
    addCohort(h, inputs.cohort);
    h.addData(inputs.withSigma ? QByteArrayLiteral("sigma") : QByteArrayLiteral("nosigma"));
    return QString::fromLatin1(h.result().toHex().left(16));
}

QString notLiveWords(const QString &status)
{
    return status == QLatin1String("held") ? QStringLiteral("held back until it is trusted to grade")
                                           : QStringLiteral("not graded yet");
}

// ── Layers 4–5 ──────────────────────────────────────────────────────────────────────────────────────────────

FiredShot fireFindings(FiringReadings readings, const FiringInputs &inputs)
{
    FiredShot out;
    if (!inputs.pack || !inputs.norms) return out;
    const CharacteristicPack &pack = inputs.pack->pack();

    LiveMeasureSource src(std::move(readings.grid), pack);
    src.setWithSigma(inputs.withSigma);
    // Null while unfrozen: the two history measures then read as not earned, and nothing else changes.
    // A part swing never sees them either: "short" is judged on full swings only (design §3.3), so
    // a pitch falls through to the duff guard rather than being measured against a full-swing carry.
    // THE SWING LENGTH IS LAYER 3 (derived data: the readings against the club's usual) and the history
    // measures are gated on it, so it is decided here, before the assessment, and again on the record below.
    if (inputs.clubBaseline
        && classifySwingLength(gradedOrNaN(src, pack, "m_shaftAngleP4"), gradedOrNaN(src, pack, "m_lmClubheadSpeed"),
                               clubUsualOf(inputs, src.club()), gradedOrNaN(src, pack, "m_lmBallSpeed")) != QLatin1String("partial"))
        src.setClubBaselineProvider(canonicalClubBaselineProvider(*inputs.clubBaseline));

    // The kinematic sequence checks' signals ride the SAME call, so each is evaluated on the graded
    // source detect() read (§10.6 E4: the tile and the diagnosis cannot disagree).
    const LiveDetection     d = detectForSwing(src, pack, inputs.norms, gradePolicyByName(inputs.gradePolicy),
                                               inputs.cohort, QString(), sequenceCheckSignalIds());

    ShotRecord rec;
    rec.club        = d.club;
    rec.contextId   = d.contextId;

    rec.rows.reserve(d.result.findings.size());
    for (const Finding &f : d.result.findings) {
        // A SESSION-scope condition has no per-swing verdict (§10.8 E2, NR-04/NR-11): its measures
        // are refused on every swing, and a row here would draw a tick, a pip and a "not assessable"
        // for a question no swing is asked. rebuild() reads it over the session instead.
        if (isSessionScopeCondition(pack, f.conditionId)) continue;
        rec.rows.push_back(conditionRowOf(f, d, src, pack));
    }

    // Each sequence check's own verdict (schema 5), and why one could not be assessed — in golfer
    // words, naming the missing piece where the engine's refusal says which it was.
    rec.signalRows.reserve(d.signalOutcomes.size());
    for (const SignalOutcome &o : d.signalOutcomes) rec.signalRows.push_back(signalRowOf(o, src, pack));
    // The placed peak instants the session's timing spread is reduced from (schema 6, §10.9 P8). Only
    // placed nodes emit, so an absent value IS an unplaced node; a time a signal refused as
    // implausible on this swing is a capture fault, not a reading, and stays out of the spread too.
    for (const SequenceSpreadSegmentDef &seg : sequenceSpreadSegments()) {
        const QString mid = QLatin1String(seg.measureId);
        // The spread is graded (m_ksTimingSpread), so a peak time the pack does not grade — held,
        // planned — stays out of it, exactly as the engine refuses it on the order checks (#176).
        if (!packMeasureGrades(pack, mid)) continue;
        bool implausible = false;
        for (const SignalOutcome &o : d.signalOutcomes)
            if (o.refusal == SignalRefusal::Implausible && o.missing.contains(mid)) implausible = true;
        if (implausible) continue;
        // Nor does a time its producer marked not for judging (the camera club peak, KB-18).
        if (const auto v = src.value(mid); v && std::isfinite(v->value) && v->withheld.isEmpty()) {
            PeakTimeRow t;
            t.measureId = mid;
            t.valueMs   = v->value;
            t.sigmaMs   = v->sigma;
            rec.peakTimes.push_back(std::move(t));
        }
    }

    // The ranking's last tie-break: the measured clubhead speed where there is one, else ours.
    for (const char *mid : { "m_lmClubheadSpeed", "m_clubheadSpeedImpact" }) {
        if (const auto v = src.value(QLatin1String(mid)); v && std::isfinite(v->value)) {
            rec.clubheadSpeed = v->value;
            break;
        }
    }

    // Schema 11: the monitor's readings the per-club distances are reduced from, the backswing, and
    // how far the swing went against this club's frozen usual numbers (Contract E). Read PLAINLY and kept
    // as recorded, from the one table (shotReadingFields: carry, total, offline, ball and club speed,
    // smash, the P4 shaft angle; schema 12/13 launch, start direction, attack, descent, apex in feet,
    // spin and its axis), so a reading is shown whatever its measure's status; one whose measure is not
    // live is named in ungradedReadings (schema 15), and every verdict reads them through GradedReadings.
    for (const ShotReadingField &f : shotReadingFields()) {
        rec.*(f.field) = measuredOrNaN(src, f.measureId);
        if (!packMeasureGrades(pack, QLatin1String(f.measureId))) rec.ungradedReadings << QLatin1String(f.measureId);
    }
    rec.handedness   = normaliseHandedness(readings.handedness);
    {
        // The swing length decides what may be judged (a part swing is never short), so it is a verdict's input.
        const GradedReadings graded(rec);
        const ShotRecord &g = graded.record();
        rec.swingLength = classifySwingLength(g.shaftAngleP4, g.lmClubSpeed, clubUsualOf(inputs, rec.club), g.ballSpeed);
    }

    // Data-integrity exclusion. A shot whose recording is known broken — frames lost
    // during capture (captureIntegrity), or an IMU record that does not reproduce
    // (imuIntegrity) — is still drawn on the strip, still carries the ⚠ the card shows,
    // but says nothing to the session: every row becomes NotAssessable, which rule 1
    // already keeps out of every denominator (diagnostic_ledger.h tierAtPrefix). Done
    // here rather than in the ledger arithmetic so the reason reads in the corridor
    // slot like any other withheld row, and the ledger needs no new rule.
    // A refused CLUB track (dataWarningDetail.clubRefused, 2026-10-01) also carries
    // the ⚠ but withholds nothing here: its club rows are already absent (the
    // track is invalid, so no club-derived producer ran) and the body/wrist rows
    // are untouched by it. Only the two recording-integrity facts exclude the shot.
    if (readings.dataWarning) rec.dataWarning = true;
    if (readings.dataWarning && (readings.captureBroken || readings.imuBroken)) {
        // THE GATE IS THE SHOT'S, NOT WHATEVER THE ROW NAMED BEFORE (trust stage 6 session 2): the
        // integrity record on `shot` (frames missing, sensor recording inconsistent) replaces a gate or
        // rule the detection left on the row, which no longer explains it. Empty on a swing analysed
        // before records existed — the stale names are cleared, never kept.
        const LiveMeasureSource::GateCause shotGate = src.gateForSubject(QStringLiteral("shot"));
        for (ConditionRow &r : rec.rows) {
            r.state               = ShotState::NotAssessable;
            r.direction           = 0;
            r.notAssessableReason = missingReasonText(MissingKind::CaptureDataIssue);
            r.notJudgedGate       = shotGate.gate;
            r.notJudgedSubject    = shotGate.subject;
            r.notLiveStatus.clear();
        }
        for (SignalRow &g : rec.signalRows) {
            g.state            = ShotState::NotAssessable;
            g.pFire            = -1.f;
            g.quantified       = false;
            g.reason           = missingReasonText(MissingKind::CaptureDataIssue);
            g.notJudgedGate    = shotGate.gate;
            g.notJudgedSubject = shotGate.subject;
            g.notLiveStatus.clear();
        }
        rec.peakTimes.clear();
    }

    out.record           = std::move(rec);
    out.hasLaunchMonitor = src.hasLaunchMonitor();
    out.ok               = true;
    return out;
}

// ── Layer 3 ─────────────────────────────────────────────────────────────────────────────────────────────────

ShotFactors deriveShotFactors(const ShotRecord &shot, const ShotOutcome &outcome, const ShotOutcomePack &outcomePack,
                              const FiringInputs &inputs)
{
    return shotFactors(shot, outcome, outcomePack, inputs.flightNorms, inputs.usuals);
}

ShotDerivedData deriveShotData(const ShotRecord &shot, const std::vector<ShotCorrection> &corrections,
                               const QString &intent, const ShotOutcomePack &outcomePack,
                               const ShotOutcomeSessionFacts &diagnosis, const FiringInputs &inputs)
{
    const ShotOutcomeSessionFacts facts{ diagnosis.links, diagnosis.tiers, inputs.usuals };
    ShotDerivedData out;
    out.outcome = composeShotOutcome(shot, corrections, intent, clubUsualOf(inputs, shot.club), outcomePack, facts);
    out.factors = deriveShotFactors(shot, out.outcome, outcomePack, inputs);
    return out;
}

} // namespace pinpoint::analysis
