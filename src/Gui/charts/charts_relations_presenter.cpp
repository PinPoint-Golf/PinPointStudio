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

#include "charts_relations_presenter.h"

#include "charts_words.h"
#include "consistency_env.h"
#include "session_consistency.h"
#include "session_relations.h"
#include "session_sequence.h"
#include "session_series_extract.h"   // extractShot, folderShotId (refactor 5.5a)
#include "../../Export/series_store.h"   // seriesSwingLabelOf: the ★ label
#include "session_traces.h"
#include "session_warp.h"

#include "../review/timeline_labels.h"
#include "../trust/trust_readings.h"     // the gate behind a reading a shot has no value for (gateFor)
#include "../../Core/pp_debug.h"
#include "../../Vocabulary/Metrics/gate_catalogue.h"   // a masked reading's gate words (cautionOfPicked)
#include "../../Vocabulary/Metrics/metric_catalogue.h"
#include "../../Diagnostics/norms/norm.h"   // the grade policy: where a fired reading is past its fault line
#include "../../Diagnostics/norms/norm_provider.h"   // the Readings rows' corridor: the norm the reading grades against
#include "../../Coaching/session_spread.h"           // …turned into bands exactly as Session diagnostics draws them
#include "../variability/variability_facade.h"       // R8 typicality: the session window and the badge's `values`
#include "../../Vocabulary/Metrics/variability/variability_outcome.h"   // isOutcomeMetric: ball flight has no typicality (R4)

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDir>
#include <QElapsedTimer>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <optional>

namespace pinpoint::charts {

namespace {

namespace pa = pinpoint::analysis;
using words::Sentence;

// ── vocabulary ──────────────────────────────────────────────────────────────────────────────

const pa::MetricCatalogue &catalogue()
{
    static const pa::MetricCatalogue c = pa::makeMetricCatalogue();
    return c;
}

std::vector<int> catalogueReadAt(const QString &key)
{
    const pa::MetricDescriptor *d = catalogue().descriptor(key);
    const pa::MetricCardSpec card = d ? d->card : pa::MetricCardSpec{};
    std::vector<int> out;
    for (const pa::MetricCardReading &r : card.readAt) out.push_back(int(r.phase));
    return out;
}

using words::fin;
QVariant num(double v) { return fin(v) ? QVariant(v) : QVariant::fromValue(nullptr); }
// A per-shot (or per-sample) list of numbers, typed: NaN stays NaN where the map had null, which every reader of these
// lists takes alike (charts_relations_types.h).
QList<double> dbls(const std::vector<double> &v) { return QList<double>(v.begin(), v.end()); }
QList<int> intList(const std::vector<int> &v) { return QList<int>(v.begin(), v.end()); }
QList<bool> boolList(const std::vector<uint8_t> &f)
{
    QList<bool> out;
    out.reserve(qsizetype(f.size()));
    for (uint8_t x : f) out.append(bool(x));
    return out;
}
RelationsSentence sentenceOf(const Sentence &s) { return relationsSentence(s); }
// r as an annual report sets it: ".62", "−.88"; "-" when undefined (figure_format.h figR).
using words::figR;
QString dText(double d)
{
    if (!fin(d)) return QStringLiteral("-");
    return words::fig(d, 1);
}

using words::shortUnit;
// A "ratio" reading has no unit word, and a whole number says nothing of it: a smash factor is 1.09, never 1.
bool isRatio(const QString &u) { return u == QLatin1String("ratio"); }
QString unitText(const QString &u) { return (u == QStringLiteral("°") || u == QLatin1String(":1") || isRatio(u)) ? QString() : u; }
QString figBare(double v, const QString &u) { return words::fig(v, isRatio(u) ? 2 : words::decimalsFor(u)); }
QString figU(double v, const QString &u) { return words::withUnit(figBare(v, u), u); }
bool closedUnit(const QString &u) { return u == QStringLiteral("°") || u == QLatin1String("%") || u == QLatin1String(":1"); }

// "Lead wrist — bow / cup" → "lead wrist bow/cup" (Words.qml lowerLabel).
QString lowerLabel(QString l)
{
    l.replace(QStringLiteral(" — "), QStringLiteral(" ")).replace(QStringLiteral(" / "), QStringLiteral("/"));
    if (l.size() >= 2 && l[0].isUpper() && l[1].isLower()) l[0] = l[0].toLower();
    return l;
}

// "an 8 mph", "an 11 mph", "a 6 mph".
QString aOrAn(const QString &fig)
{
    QString digits;
    for (QChar c : fig) { if (c.isDigit()) digits += c; else if (c == QLatin1Char('.')) break; else if (!digits.isEmpty()) break; }
    const bool an = digits.startsWith(QLatin1Char('8'))
                 || ((digits.size() == 2 || digits.size() == 5) && (digits.startsWith(QStringLiteral("11")) || digits.startsWith(QStringLiteral("18"))));
    return an ? QStringLiteral("an") : QStringLiteral("a");
}

QString placeWords(const ReadingPlace &p, int n)
{
    if (!p.measured) return QString();
    if (p.rankHi == 1) return QStringLiteral("highest of %1").arg(n);
    if (p.rankLo == 1) return QStringLiteral("lowest of %1").arg(n);
    if (p.rankHi <= 3) return words::ordinalWord(p.rankHi) + QStringLiteral(" highest");
    if (p.rankLo <= 3) return words::ordinalWord(p.rankLo) + QStringLiteral(" lowest");
    return QString();
}

struct Vocab {
    TimelineLabels tl;
    QString tag(int ph) const { const QString t = tl.phaseShortTag(ph); return t.isEmpty() ? tl.phaseFullName(ph) : t; }
    QString name(int ph) const { return tl.phaseFullName(ph); }
    bool isP(int ph) const { return tl.hasPositionTag(ph); }
};

// A box in box_map.h's shape (the one serialisation), typed: its numbers null when n = 0, outliers by ordinal, and with a
// formatter its `texts` in the reading's own rounding (left "" when n = 0, where the map had none).
RelationsBox boxOf(const BoxStats &b, const std::function<QString(double)> &fmt = {})
{
    RelationsBox out;
    out.n = b.n;
    out.min = num(b.min); out.max = num(b.max);
    out.q1 = num(b.q1); out.med = num(b.med); out.q3 = num(b.q3);
    out.lo = num(b.lo); out.hi = num(b.hi);
    for (int i : b.outliers) out.outliers.append(i + 1);       // values are per shot: index + 1 = ordinal
    if (fmt && b.n > 0) {
        out.texts.lo = fmt(b.lo); out.texts.q1 = fmt(b.q1); out.texts.med = fmt(b.med);
        out.texts.q3 = fmt(b.q3); out.texts.hi = fmt(b.hi);
    }
    return out;
}
// a box's value texts in a reading's own rounding:
std::function<QString(double)> figOf(const QString &unit) { return [unit](double v) { return figBare(v, unit); }; }
RelationsRange rangeOf(double lo, double hi)
{
    RelationsRange r;
    r.lo = num(lo);
    r.hi = num(hi);
    return r;
}
RelationsRange paddedRange(const BoxStats &b, double frac)
{
    if (!b.n) return rangeOf(0, 1);
    const double p = std::max(1e-6, (b.max - b.min) * frac);
    return rangeOf(b.min - p, b.max + p);
}

double valueAtPhaseOf(const ShotSeries &s, const QString &key, int ph)
{
    return readingValueAt(s, key, ph);
}
qint64 phaseUsOf(const ShotSeries &s, int ph)
{
    for (const PhaseMark &p : s.phases) if (p.phase == ph) return p.tUs;
    return -1;
}

QString cleanDir(const QString &d) { return d.trimmed().isEmpty() ? QString() : QDir::cleanPath(d.trimmed()); }

// The picked shot's own analysisDetail as a ShotSeries — the live shot not yet in the snapshot.
std::shared_ptr<ShotSeries> shotFromDetail(const QVariantMap &d, const QString &swingDir, int ordinal)
{
    const QVariantList series = d.value(QStringLiteral("series")).toList();
    if (series.isEmpty()) return nullptr;
    auto s = std::make_shared<ShotSeries>();
    s->ordinal = ordinal;
    s->swingDir = swingDir;
    s->shotId = folderShotId(swingDir);
    s->kinematicSequence = d.value(QStringLiteral("kinematicSequence")).toMap();
    s->gatesVersion = d.value(QStringLiteral("gatesVersion")).toInt();
    s->gates = d.value(QStringLiteral("gates")).toList();
    for (const QVariant &pv : d.value(QStringLiteral("phases")).toList()) {
        const QVariantMap p = pv.toMap();
        PhaseMark m;
        m.phase = p.value(QStringLiteral("phase")).toInt();
        m.tUs = p.value(QStringLiteral("t_us")).toLongLong();
        m.conf = p.contains(QStringLiteral("conf")) ? p.value(QStringLiteral("conf")).toDouble() : kNaN;
        s->phases.push_back(m);
        if (m.phase == phase::Impact) s->impactUs = m.tUs;
    }
    if (s->impactUs < 0 && s->kinematicSequence.contains(QStringLiteral("impactUs")))
        s->impactUs = s->kinematicSequence.value(QStringLiteral("impactUs")).toLongLong();
    for (const QVariant &mv : series) {
        const QVariantMap m = mv.toMap();
        MetricSeries ms;
        ms.key = m.value(QStringLiteral("key")).toString();
        if (ms.key.isEmpty() || s->metrics.contains(ms.key)) continue;
        ms.label = m.value(QStringLiteral("label")).toString();
        ms.unit = m.value(QStringLiteral("unit")).toString();
        for (const QVariant &t : m.value(QStringLiteral("t_us")).toList()) ms.tUs.push_back(t.toLongLong());
        for (const QVariant &v : m.value(QStringLiteral("value")).toList()) ms.value.push_back(v.isNull() ? kNaN : v.toDouble());
        for (const QVariant &v : m.value(QStringLiteral("valid")).toList()) ms.valid.push_back(uint8_t(v.toInt() != 0));
        if (m.contains(QStringLiteral("sigma"))) ms.sigma = m.value(QStringLiteral("sigma")).toDouble();
        for (const QVariant &pv : m.value(QStringLiteral("phaseSamples")).toList()) {
            const QVariantMap p = pv.toMap();
            PhaseSample ps;
            ps.phase = p.value(QStringLiteral("phase")).toInt();
            ps.tUs = p.value(QStringLiteral("t_us")).toLongLong();
            ps.value = p.value(QStringLiteral("value")).isNull() ? kNaN : p.value(QStringLiteral("value")).toDouble();
            ms.phaseSamples.push_back(ps);
        }
        s->metricOrder << ms.key;
        s->metrics.insert(ms.key, ms);
    }
    return s;
}

// THE TRUNK PEAKS A GATE REFUSED, AS READINGS (Mark, 17 Oct 2026: "present what we have, with a health
// warning"). The sequence keeps an estimate for a pelvis or chest node it could not place (its peak, its
// instant, their σ), but the producer writes readings only for placed nodes — so the KS chart drew the curve
// while Readings had no row. Each shot lacking the reading gains the node's estimate; one the gate refused is
// listed in `refused` ("swingDir|key" → σ) so its row carries a caution instead of passing as measured.
std::shared_ptr<const SessionSnapshot> withTrunkEstimates(const std::shared_ptr<const SessionSnapshot> &snap,
                                                          QHash<QString, double> *refused)
{
    struct Seg { const char *id, *timeKey, *speedKey; };
    static const Seg segs[] = { { "pelvis", "pelvisPeakTime", "pelvisPeakSpeed" }, { "thorax", "thoraxPeakTime", "thoraxPeakSpeed" } };
    std::shared_ptr<SessionSnapshot> copy;
    for (size_t i = 0; i < snap->shots.size(); ++i) {
        const ShotSeries &sh = *snap->shots[i];
        const QVariantMap &ks = sh.kinematicSequence;
        const qint64 ksImpact = ks.value(QStringLiteral("impactUs"), -1).toLongLong();
        if (ksImpact < 0 || sh.impactUs < 0) continue;
        std::shared_ptr<ShotSeries> mine;
        for (const QVariant &nv : ks.value(QStringLiteral("nodes")).toList()) {
            const QVariantMap n = nv.toMap();
            const Seg *seg = nullptr;
            for (const Seg &g : segs) if (n.value(QStringLiteral("segment")).toString() == QLatin1String(g.id)) seg = &g;
            if (!seg || !n.contains(QStringLiteral("tPeakUs")) || !n.contains(QStringLiteral("peakDps"))) continue;
            const bool placed = n.value(QStringLiteral("placed")).toBool();
            const double sTime = n.value(QStringLiteral("tSigmaMs"), kNaN).toDouble();
            const double sSpeed = n.value(QStringLiteral("peakSigmaDps"), kNaN).toDouble();
            const struct { QString key; double value, sigma; } add[] = {
                { QString::fromLatin1(seg->timeKey), double(n.value(QStringLiteral("tPeakUs")).toLongLong() - ksImpact) / 1000.0, sTime },
                { QString::fromLatin1(seg->speedKey), std::fabs(n.value(QStringLiteral("peakDps")).toDouble()), sSpeed } };
            for (const auto &a : add) {
                if (sh.metric(a.key) || !fin(a.value)) continue;
                const pa::MetricDescriptor *d = catalogue().descriptor(a.key);
                if (!d) continue;
                if (!mine) mine = std::make_shared<ShotSeries>(sh);
                MetricSeries ms;
                ms.key = a.key; ms.label = d->label; ms.unit = d->unit;
                PhaseSample ps;
                ps.phase = phase::Impact; ps.tUs = sh.impactUs; ps.value = a.value;
                ms.phaseSamples.push_back(ps);
                mine->metricOrder << a.key;
                mine->metrics.insert(a.key, ms);
                if (!placed && refused) refused->insert(cleanDir(sh.swingDir) + QLatin1Char('|') + a.key, a.sigma);
            }
        }
        if (!mine) continue;
        if (!copy) copy = std::make_shared<SessionSnapshot>(*snap);
        copy->shots[i] = mine;
    }
    return copy ? std::shared_ptr<const SessionSnapshot>(copy) : snap;
}

// How a fired condition read its reading, said against the reading's instant (the pack's reducer kind,
// ConditionInfo::reducer): At → "at P7"; Extremum → "peak before P7" (the peak is inside the window that
// ENDS at P7, not at P7); Delta → "change to P7"; Rate → "rate of change to P7"; none → "".
struct FiredLink {
    QString id;
    int phase = -1;
    int reducer = -1;
};
QString instantPhrase(const Vocab &v, int reducer, int ph)
{
    if (ph < 0) return QString();
    const QString t = v.tag(ph);
    switch (reducer) {
    case 0: return QStringLiteral("at ") + t;
    case 1: return QStringLiteral("change to ") + t;
    case 2: return QStringLiteral("rate of change to ") + t;
    case 3: return QStringLiteral("peak before ") + t;
    default: return QString();
    }
}
// The warn word under a fired value: the value AT the reading's instant is what an At condition graded;
// for a peak or a change it is not, so the word says what was out of its corridor.
QString firedWordFor(const Vocab &v, const FiredLink &l, bool fault = false)
{
    const QString where = fault ? QStringLiteral("past its fault line") : QStringLiteral("out of its corridor");
    if (l.reducer <= 0) return where;
    return QStringLiteral("the ") + instantPhrase(v, l.reducer, l.phase) + QLatin1Char(' ') + where;
}

struct ReadingMeta {
    QString shortLabel, group;
    bool drawsCurve = true;
    QStringList presets;
};

} // namespace

// ── the worker's cache and the published output ─────────────────────────────────────────────

struct ChartsRelationsPresenter::Cache {
    const SessionSnapshot *base = nullptr;
    quint64 revision = 0;
    QString augmentedDir;
    QString soloDir;          // n < 3: the picked shot whose own readings are held (it decides them)
    int readAtGen = -1;
    bool valid = false;
    bool solo = false;        // readings are the picked shot's alone (n < 3): no box, no place
    ChartsRelationsPresenter::ReadAt readAt;
    std::shared_ptr<Cache> clubCache;   // kinds within the largest club, when the kinds are the clubs

    std::shared_ptr<const SessionSnapshot> snap;   // the effective session (with a live picked shot added)
    std::vector<SessionReading> readings;
    std::vector<ReadingMeta> meta;
    QHash<QString, int> byId;
    QStringList groupOrder;
    QHash<qint64, QList<FiredLink>> firedBy;        // reading × N + shot → the fired links on it
    QHash<int, QList<RelationsFiredElsewhere>> firedElsewhere;   // shot index → links that land on no reading
    struct Family { QString name; bool preset = false; std::vector<int> readings; };
    std::vector<Family> families;
    SessionKinds kinds;
    SessionSequence seq;
    std::vector<CorrRow> drifts;
    bool aligned = false;
    SessionAlignment alignment;
    QHash<QString, std::vector<TraceCurve>> curves;   // aligned, per key
    QHash<int, std::vector<CorrRow>> corr;            // per Up reading
    QHash<QString, std::optional<RelationsKindCurve>> kindCurves;   // per key; nullopt: no curve to draw
    QHash<QString, RelationsShapes> shapes;           // per family
    std::shared_ptr<const SessionSnapshot> clubBase;  // the largest club's shots, for kinds within it
    QString clubName;

    // The stand-out rule (doc §5). Read from the session as recorded, not `snap`, whose trunk estimates the
    // Consistency tab does not have: the two must not disagree. Worked out once a snapshot, on the first pick.
    std::shared_ptr<const SessionSnapshot> recorded;   // the session, with a live picked shot added
    bool consistencyRun = false;
    ConsistencyInput consistencyIn;
    ConsistencyResult consistencyOut;
    std::vector<std::vector<ConsistencyLmRow>> lmRows; // per shot

    int indexOf(const QString &id) const { return byId.value(id, -1); }
    int n() const { return snap ? snap->count() : 0; }

    const SessionAlignment &align()
    {
        if (!aligned) { alignment = alignSession(*snap); aligned = true; }
        return alignment;
    }
    const std::vector<TraceCurve> &alignedOf(const QString &key)
    {
        auto it = curves.find(key);
        if (it == curves.end()) it = curves.insert(key, alignedCurves(*snap, align(), key));
        return it.value();
    }
    const std::vector<CorrRow> &corrOf(int y)
    {
        auto it = corr.find(y);
        if (it == corr.end()) it = corr.insert(y, correlate(readings, y));
        return it.value();
    }
    // Returns the time it took, 0 once it has run.
    double ensureConsistency(const ConsistencyEnv &env)
    {
        if (consistencyRun) return 0.0;
        QElapsedTimer t;
        t.start();
        consistencyRun = true;
        consistencyIn = consistencyInput(*recorded, align(), env);     // alignment reads the P-marks, which estimates leave alone
        consistencyOut = consistencyResult(consistencyIn);
        for (const auto &sh : recorded->shots) lmRows.push_back(consistencyLmRows(*recorded, sh->ordinal));
        return double(t.nsecsElapsed()) / 1e6;
    }
};

struct ChartsRelationsPresenter::Output {
    bool ready = false;
    RelationsStatus status;
    RelationsThisShot thisShot;
    RelationsOpenRow openRow;
    RelationsSequence sequence;
    RelationsRelations relations;
    RelationsKinds kinds;
    QList<RelationsFamily> families;
    struct ReplayEntry { qint64 tUs = std::numeric_limits<qint64>::max(); RelationsReplaySession entry; };
    std::vector<ReplayEntry> replay;
    qint64 pickedImpactUs = -1;
    std::vector<PhaseMark> pickedPhases;
    TraceCurve openCurve, leadCurve;
    QString openUnit, leadUnit;
    double jobMs = 0;
    double consistencyMs = 0;   // what this job spent on the stand-out rule: 0 unless the snapshot changed
};

namespace {

using Cache = ChartsRelationsPresenter::Cache;
using Output = ChartsRelationsPresenter::Output;

// Fired, from the condition link only: per shot the reading drivenBy names, else ConditionInfo's.
void linkFired(const SessionSnapshot &s, std::vector<SessionReading> &rs, QHash<qint64, QList<FiredLink>> *firedBy,
               QHash<int, QList<RelationsFiredElsewhere>> *elsewhere)
{
    const Vocab v;
    const int N = s.count();
    QHash<QString, std::vector<int>> byKey;
    for (size_t i = 0; i < rs.size(); ++i) {
        byKey[rs[i].key].push_back(int(i));
        rs[i].fired.assign(size_t(N), 0);
        rs[i].clean.assign(size_t(N), 0);
        rs[i].firedZ.assign(size_t(N), kNaN);
        rs[i].conditionIds.clear();
    }
    const auto target = [&](const QString &key, int ph) {
        const auto it = byKey.constFind(key);
        if (it == byKey.constEnd()) return -1;
        for (int i : it.value()) if (rs[size_t(i)].phase == ph) return i;
        if (ph < 0 && it.value().size() == 1) return it.value().front();
        return -1;
    };
    for (auto it = s.conditions.constBegin(); it != s.conditions.constEnd(); ++it) {
        if (it.value().metricKey.isEmpty()) continue;
        const int t = target(it.value().metricKey, it.value().phase);
        if (t >= 0 && !rs[size_t(t)].conditionIds.contains(it.key())) rs[size_t(t)].conditionIds << it.key();
    }
    for (int i = 0; i < N; ++i) {
        const ChartShotFindings &d = s.shots[size_t(i)]->diagnostics;
        for (const QString &c : d.fired) {
            QString key; int ph = -1, red = -1;
            const auto dv = d.drivenBy.constFind(c);
            if (dv != d.drivenBy.constEnd() && !dv.value().metricKey.isEmpty()) { key = dv.value().metricKey; ph = dv.value().phase; red = dv.value().reducer; }
            else if (s.conditions.contains(c)) { key = s.conditions.value(c).metricKey; ph = s.conditions.value(c).phase; red = s.conditions.value(c).reducer; }
            if (key.isEmpty()) continue;
            const int t = target(key, ph);
            if (t < 0) {
                if (elsewhere) {
                    RelationsFiredElsewhere e;
                    e.condition = c;
                    e.name = s.conditions.value(c).name.isEmpty() ? c : s.conditions.value(c).name;
                    e.metricKey = key;
                    e.phase = ph;
                    e.reducer = red;
                    e.instant = instantPhrase(v, red, ph);
                    (*elsewhere)[i].append(e);
                }
                continue;
            }
            SessionReading &r = rs[size_t(t)];
            r.fired[size_t(i)] = 1;
            const double z = std::abs(d.values.value(c).z);
            if (std::isfinite(z) && !(z <= r.firedZ[size_t(i)])) r.firedZ[size_t(i)] = z;
            if (!r.conditionIds.contains(c)) r.conditionIds << c;
            if (firedBy) (*firedBy)[qint64(t) * N + i] << FiredLink{ c, ph, red };
        }
        // graded inside its corridor: the clean row's own reading (gradedBy), the condition's link where it named none
        for (const QString &c : d.clean) {
            const auto gb = d.gradedBy.constFind(c);
            const QString key = gb != d.gradedBy.constEnd() && !gb->metricKey.isEmpty() ? gb->metricKey : s.conditions.value(c).metricKey;
            const int ph = gb != d.gradedBy.constEnd() && !gb->metricKey.isEmpty() ? gb->phase : s.conditions.value(c).phase;
            if (key.isEmpty()) continue;
            const int t = target(key, ph);
            if (t >= 0) rs[size_t(t)].clean[size_t(i)] = 1;
        }
    }
    for (SessionReading &r : rs) r.conditionIds.sort();
}

void ensureSession(Cache &c, const std::shared_ptr<const SessionSnapshot> &base, const QString &pickedDir,
                   const QVariantMap &detail, const ChartsRelationsPresenter::ReadAt &readAt, int readAtGen,
                   bool full = true)
{
    // Is the picked shot in the session? If not, and its own detail is here, it joins the session.
    bool inSession = pickedDir.isEmpty();
    if (!inSession)
        for (const auto &sh : base->shots) if (cleanDir(sh->swingDir) == pickedDir) { inSession = true; break; }
    const QString augmented = (!inSession && !detail.isEmpty()) ? pickedDir : QString();
    const QString soloDir = (base->count() + (augmented.isEmpty() ? 0 : 1) < 3) ? pickedDir : QString();
    if (c.valid && c.base == base.get() && c.revision == base->revision && c.augmentedDir == augmented && c.readAtGen == readAtGen
        && c.soloDir == soloDir)
        return;
    c = Cache{};
    c.base = base.get(); c.revision = base->revision; c.augmentedDir = augmented; c.readAtGen = readAtGen; c.valid = true;
    c.soloDir = soloDir; c.readAt = readAt;
    c.snap = base;
    if (!augmented.isEmpty())
        if (auto s = shotFromDetail(detail, augmented, base->count() + 1)) {
            auto copy = std::make_shared<SessionSnapshot>(*base);
            copy->shots.push_back(s);
            c.snap = copy;
        }
    c.recorded = c.snap;
    QHash<QString, double> refusedEstimates;
    c.snap = withTrunkEstimates(c.snap, &refusedEstimates);
    const SessionSnapshot &S = *c.snap;

    // Keys in manifest order, then the uncatalogued (sorted).
    QSet<QString> present;
    for (const auto &sh : S.shots) for (const QString &k : sh->metricOrder)
        if (!pa::isResearchOnlySeries(k)) present.insert(k);
    QStringList order;
    for (const pa::MetricDescriptor *d : catalogue().all()) if (present.remove(d->key)) order << d->key;
    QStringList rest(present.cbegin(), present.cend());
    rest.sort();
    order += rest;

    const ReadAtPhases ra = readAt ? ReadAtPhases(readAt) : ReadAtPhases(catalogueReadAt);
    c.readings = sessionReadings(S, ra, order);
    // TOO FEW SHOTS TO PLACE: the picked shot still has its own readings (key figures, a lead), read at the
    // same instants. They are taken from three copies of that one shot — the maths' "measured on three"
    // gate then passes on exactly the readings the shot carries — and NOTHING session-level is read from
    // them: every box, place and relation stays refused at n < 3 (c.solo says so).
    if (S.count() < 3 && !soloDir.isEmpty()) {
        std::shared_ptr<const ShotSeries> me;
        for (const auto &sh : S.shots) if (cleanDir(sh->swingDir) == soloDir) me = sh;
        if (me) {
            SessionSnapshot three;
            three.shots = { me, me, me };
            three.conditions = S.conditions;
            c.readings = sessionReadings(three, ra, order);
            c.solo = true;
        }
    }
    for (SessionReading &r : c.readings) {
        r.unit = shortUnit(r.unit);
        if (pa::isLabelFromCatalogue(r.key))
            if (const pa::MetricDescriptor *d = catalogue().descriptor(r.key)) r.label = d->label;
    }
    // the estimates a gate refused: per shot, by the shot the reading's values came from (solo: the one shot)
    if (!refusedEstimates.isEmpty())
        for (SessionReading &r : c.readings) {
            r.refused.assign(r.values.size(), 0);
            r.refusedSigma.assign(r.values.size(), kNaN);
            for (size_t i = 0; i < r.values.size(); ++i) {
                const auto &sh = c.solo ? S.shots.front() : S.shots[std::min(i, S.shots.size() - 1)];
                const QString k = (c.solo ? soloDir : cleanDir(sh->swingDir)) + QLatin1Char('|') + r.key;
                const auto it = refusedEstimates.constFind(k);
                if (it != refusedEstimates.constEnd()) { r.refused[i] = 1; r.refusedSigma[i] = it.value(); }
            }
        }
    if (!c.solo) linkFired(S, c.readings, &c.firedBy, &c.firedElsewhere);
    for (size_t i = 0; i < c.readings.size(); ++i) {
        const SessionReading &r = c.readings[i];
        c.byId.insert(r.id, int(i));
        ReadingMeta m;
        if (const pa::MetricDescriptor *d = catalogue().descriptor(r.key)) {
            m.shortLabel = d->shortLabel; m.group = d->group; m.drawsCurve = d->card.drawsCurve();
            for (const QString &p : d->presets) m.presets << p;
        }
        if (m.shortLabel.isEmpty()) m.shortLabel = r.label;
        if (m.group.isEmpty()) m.group = QStringLiteral("Other");
        if (!c.groupOrder.contains(m.group)) c.groupOrder << m.group;
        c.meta.push_back(m);
    }
    // Families: the groups, then presets with two or more keys read.
    for (const QString &g : c.groupOrder) {
        Cache::Family f; f.name = g;
        for (size_t i = 0; i < c.readings.size(); ++i) if (c.meta[i].group == g) f.readings.push_back(int(i));
        c.families.push_back(f);
    }
    QStringList presetOrder;
    for (const ReadingMeta &m : c.meta) for (const QString &p : m.presets) if (!presetOrder.contains(p)) presetOrder << p;
    for (const QString &p : presetOrder) {
        if (c.groupOrder.contains(p)) continue;
        Cache::Family f; f.name = p; f.preset = true;
        QSet<QString> keys;
        for (size_t i = 0; i < c.readings.size(); ++i)
            if (c.meta[i].presets.contains(p)) { f.readings.push_back(int(i)); keys.insert(c.readings[i].key); }
        if (keys.size() >= 2) c.families.push_back(f);
    }
    if (!full) return;            // movesWith(): the readings are all it reads
    c.seq = sessionSequence(S);
    c.drifts = drifts(c.readings);
    if (S.count() >= 6) c.kinds = findKinds(c.readings);
}

// ── the job ────────────────────────────────────────────────────────────────────────────────

struct Ctx {
    Cache &c;
    const SessionSnapshot &S;
    const Vocab &v;
    int pick = -1;          // shot index of the picked shot, −1 none
    int n = 0;
    int readAtGen = 0;
    double faultZ = 3.0;    // where Action begins, in the ledger's z unit (the Ideal half-width): watchMaxZ / idealMaxZ
    // #10: the norms and the grade policy the Readings rows' corridors are drawn from; null = no corridors
    const pinpoint::analysis::INormProvider *norms = nullptr;
    pinpoint::analysis::GradePolicy policy;
    const ConsistencyEnv *env = nullptr;    // the stand-out rule's, taken on the GUI thread
    // R8: per shot, true when no "usual" counts it (a guarded P-ladder); filled only when a typicality
    // window could hold N_MIN others (outOfTypicalityWindow)
    std::vector<uint8_t> typicalOut;

    // A shot's own ordinal from its 1-based position in S (identity in a session; not in a club's subset).
    int ord(int pos) const { return pos >= 1 && pos <= int(S.shots.size()) ? S.shots[size_t(pos - 1)]->ordinal : pos; }
    std::vector<int> ords(const std::vector<int> &pos) const { std::vector<int> o; for (int p : pos) o.push_back(ord(p)); return o; }
    // The swing number of the shot at 1-based position `pos` in S: what "shot N" prints (step 2.7b, words::shotNumber).
    int no(int pos) const
    {
        return pos >= 1 && pos <= int(S.shots.size()) ? words::shotNumber(S.shots[size_t(pos - 1)]->shotId, S.shots[size_t(pos - 1)]->ordinal) : pos;
    }

    const SessionReading &r(int i) const { return c.readings[size_t(i)]; }
    const ReadingMeta &m(int i) const { return c.meta[size_t(i)]; }
    QString readingText(int i) const { return lowerLabel(r(i).label) + QStringLiteral(" at ") + v.tag(r(i).phase); }
    // A SHOT MARK { ordinal, shotId, swingDir } as the gadget T that carries one (a run's point, a scatter's, a member).
    template <typename T> T shotMark(int idx) const
    {
        const ShotSeries &s = *S.shots[size_t(idx)];
        T m;
        m.ordinal = s.ordinal;
        m.shotId = s.shotId;
        m.swingDir = s.swingDir;
        return m;
    }
    bool pickedHasCurve(const QString &key) const
    {
        if (pick < 0) return false;
        const MetricSeries *ms = S.shots[size_t(pick)]->metric(key);
        return ms && ms->hasCurve() && S.shots[size_t(pick)]->impactUs >= 0;
    }
};

// THE GATE BEHIND A MISSING VALUE on the picked shot (trust stage 6 session 2, trust_readings.h gateFor):
// { subject, gate, effect, words, … } when a gate refused or bounded the reading, else {} — an old swing
// (gatesVersion 0), or a value missing for another reason, keeps today's "-". Never asked of a value the
// shot has: masks do not label values.
QVariantMap gateOfPicked(const Ctx &x, const QString &key)
{
    if (x.pick < 0 || size_t(x.pick) >= x.S.shots.size() || key.isEmpty()) return {};
    const ShotSeries &s = *x.S.shots[size_t(x.pick)];
    if (s.gatesVersion <= 0 || s.gates.isEmpty()) return {};
    const QVariantMap g = trust::gateFor(s.gates, key);
    const QString eff = g.value(QStringLiteral("effect")).toString();
    if (g.value(QStringLiteral("words")).toString().isEmpty()
        || (eff != QLatin1String("refused") && eff != QLatin1String("bounded"))) {
        // a reading another route supplied, with nothing here: the failed route's record is still the
        // reason (route_substitution.h; trust_readings.h substitutedGapFor)
        const QVariantMap sub = trust::substitutedGapFor(s.gates, key);
        return sub.value(QStringLiteral("words")).toString().isEmpty() ? QVariantMap{} : sub;
    }
    return g;
}

// THE HEALTH WARNING ON A READING THAT HAS A VALUE (Mark, 17 Oct 2026: present what we have, with a health
// warning, on every reading). A gate that refused, bounded or downgraded the picked shot's reading — or masked a
// stretch holding the reading's own instant — while a value still stands: the gate's short words, under the value.
QString cautionOfPicked(const Ctx &x, const SessionReading &r)
{
    if (x.pick < 0 || size_t(x.pick) >= x.S.shots.size() || r.key.isEmpty()) return {};
    const ShotSeries &s = *x.S.shots[size_t(x.pick)];
    if (s.gatesVersion <= 0 || s.gates.isEmpty()) return {};
    const QVariantMap g = trust::gateFor(s.gates, r.key);
    if (!g.value(QStringLiteral("words")).toString().isEmpty()) return g.value(QStringLiteral("words")).toString();
    const qint64 at = phaseUsOf(s, r.phase);
    if (at < 0 || s.impactUs < 0) return {};
    const double atMs = double(at - s.impactUs) / 1000.0;
    for (const QVariant &rv : s.gates) {
        const QVariantMap rec = rv.toMap();
        if (rec.value(QStringLiteral("effect")).toString() != QLatin1String("masked")) continue;
        if (rec.value(QStringLiteral("subject")).toString() != r.key && !rec.value(QStringLiteral("covers")).toStringList().contains(r.key)) continue;
        const QVariantMap ev = rec.value(QStringLiteral("evidence")).toMap();
        if (!ev.contains(QStringLiteral("fromMs")) || !ev.contains(QStringLiteral("toMs"))) continue;
        const double a = ev.value(QStringLiteral("fromMs")).toDouble(), b = ev.value(QStringLiteral("toMs")).toDouble();
        if (atMs < std::min(a, b) - 2.0 || atMs > std::max(a, b) + 2.0) continue;
        if (const gates::GateEntry *e = gates::gateEntry(rec.value(QStringLiteral("gate")).toString())) return gates::claimWords(*e);
    }
    return {};
}

// #10: the corridor a diagnostics condition grades this reading against — the norm for the measure that graded
// it, resolved for that shot's context and turned into bands by the same function Session diagnostics draws
// with (corridorForRow), so the two screens agree. The ledger row's own corridorLo/Hi are only its stored Ideal
// band and are not used. The picked shot's grading first, else the first shot that graded the reading;
// { known: false } when no condition graded it or no norm resolves.
// ONLY a measure in the reading's own quantity draws (Reading::sameQuantity): the value AT the P-position or its
// unanchored peak over the window ending there. A change, a rate or a peak deviation from address is another
// quantity — hip stall grades pelvisRotationSigned@P7 by its P6→P7 rate in °/s, and drawn on the reading in °
// its corridor would be nonsense.
RelationsCorridor corridorOf(const Ctx &x, const SessionReading &r)
{
    if (!x.norms) return {};
    const int N = x.n;
    for (int k = -1; k < N; ++k) {
        const int idx = k < 0 ? x.pick : k;
        if (idx < 0 || idx >= N || (k >= 0 && idx == x.pick)) continue;
        const ChartShotFindings &d = x.S.shots[size_t(idx)]->diagnostics;
        for (auto it = d.gradedBy.constBegin(); it != d.gradedBy.constEnd(); ++it) {
            const ChartShotFindings::Reading &g = it.value();
            if (g.measureId.isEmpty() || g.metricKey != r.key || g.phase != r.phase) continue;
            if (!g.sameQuantity) continue;
            const pinpoint::analysis::NormResolution res = x.norms->resolve(g.measureId, g.contextId);
            if (!res.found()) continue;
            const auto shape = g.shape >= 0 ? pinpoint::analysis::Shape(g.shape) : pinpoint::analysis::Shape::Target;
            const pinpoint::analysis::SpreadCorridor c = pinpoint::analysis::spreadCorridorFromNorm(*res.norm, shape, x.policy);
            if (!c.known) continue;
            RelationsCorridor out;
            out.known = true;
            out.lowOpen = c.lowOpen;
            out.highOpen = c.highOpen;
            out.signalLo = c.signalLo;
            out.signalHi = c.signalHi;
            out.faultLo = c.faultLo;
            out.faultHi = c.faultHi;
            return out;
        }
    }
    return {};
}

// The box plot's own numbers, in the row's rounding, for the values a hover lays on the plot (Mark, 18 Oct: "all the
// values you would see on a box plot"): the whiskers' ends, the quartiles, the median, and the corridor's edges and
// fault lines where one is drawn. QML only places them.
RelationsBoxTexts boxTextsOf(const SessionReading &r, const RelationsCorridor &c)
{
    const BoxStats &b = r.box;
    RelationsBoxTexts m;
    if (b.n < 3) return m;
    m.lo = figBare(b.lo, r.unit);   m.hi = figBare(b.hi, r.unit);
    m.q1 = figBare(b.q1, r.unit);   m.q3 = figBare(b.q3, r.unit);
    m.med = figBare(b.med, r.unit);
    if (c.known) {
        m.signalLo = figBare(c.signalLo, r.unit); m.signalHi = figBare(c.signalHi, r.unit);
        m.faultLo = figBare(c.faultLo, r.unit);   m.faultHi = figBare(c.faultHi, r.unit);
    }
    return m;
}

// `link` is the reading whose fired links and corridor belong to the row, or -1 for a row that reads something
// else: what a condition graded is an instant, and a point inside a span is not it.
RelationsReadingRow rowOf(const Ctx &x, const SessionReading &r, const ReadingMeta &m, int link)
{
    const ReadingPlace p = x.pick >= 0 ? placeOf(r, x.pick) : ReadingPlace{};
    const bool fired = link >= 0 && x.pick >= 0 && size_t(x.pick) < r.fired.size() && r.fired[size_t(x.pick)];
    // this shot's verdict on the reading (the Readings tab's filter): past the fault line (the grade's Action band),
    // out of its corridor (Watch), graded inside it, or "" when no condition graded it on this shot
    const bool fault = fired && size_t(x.pick) < r.firedZ.size() && r.firedZ[size_t(x.pick)] > x.faultZ;
    const bool inside = !fired && link >= 0 && x.pick >= 0 && size_t(x.pick) < r.clean.size() && r.clean[size_t(x.pick)];
    RelationsReadingRow row;
    row.verdict = fault ? QStringLiteral("fault") : fired ? QStringLiteral("outside") : inside ? QStringLiteral("inside") : QString();
    // the filters the row shows under (thisShot.verdictFilters): every row under All, and past the fault line counts as out
    // of its corridor too, so a fault shows under Faults and Outside
    row.verdicts << QStringLiteral("all");
    if (fault) row.verdicts << QStringLiteral("fault");
    if (fired) row.verdicts << QStringLiteral("outside");
    if (inside) row.verdicts << QStringLiteral("inside");
    QString firedWord;
    if (fired)
        for (const FiredLink &l : x.c.firedBy.value(qint64(link) * x.n + x.pick)) {
            const QString name = x.S.conditions.value(l.id).name.isEmpty() ? l.id : x.S.conditions.value(l.id).name;
            const QString inst = instantPhrase(x.v, l.reducer, l.phase);
            const QString text = inst.isEmpty() ? name : name + QStringLiteral(", ") + inst;
            row.conditions << text;
            RelationsFiredCondition fc;
            fc.id = l.id;
            fc.name = name;
            fc.reducer = l.reducer;
            fc.instant = inst;
            fc.text = text;
            row.firedConditions.append(fc);
            if (firedWord.isEmpty()) firedWord = firedWordFor(x.v, l, fault);
        }
    if (fired && firedWord.isEmpty()) firedWord = fault ? QStringLiteral("past its fault line") : QStringLiteral("out of its corridor");
    const bool curve = m.drawsCurve && x.pickedHasCurve(r.key);
    const RelationsCorridor corr = link >= 0 ? corridorOf(x, r) : RelationsCorridor{};
    // no value on this shot because a gate refused it: a plain "-" in the row; why is said once, in the panel's tail
    // (Mark, 17 Oct 2026). gateWords stays on the row for readers that name it (none on screen).
    const QVariantMap gate = x.pick >= 0 && !p.measured ? gateOfPicked(x, r.key) : QVariantMap{};
    // a value a gate refused, bounded, downgraded or masked: shown, with its health warning under it (Mark, 17 Oct
    // 2026); a trunk peak the sequence could not place says how rough, the rest say their gate's words
    const bool estimate = x.pick >= 0 && size_t(x.pick) < r.refused.size() && r.refused[size_t(x.pick)] && p.measured;
    const double cSigma = estimate ? r.refusedSigma[size_t(x.pick)] : kNaN;
    const QString gateCaution = p.measured && !estimate ? cautionOfPicked(x, r) : QString();
    const QString cautionText = estimate ? (fin(cSigma) ? QStringLiteral("⚠ a rough estimate, ±%1 %2").arg(qRound(cSigma)).arg(r.unit)
                                                        : QStringLiteral("⚠ a rough estimate"))
                              : !gateCaution.isEmpty() ? QStringLiteral("⚠ ") + gateCaution : QString();
    row.caution = !cautionText.isEmpty();
    row.cautionText = cautionText;
    row.gateWords = gate.value(QStringLiteral("words")).toString();
    row.readingId = r.id;
    row.key = r.key;
    row.phase = r.phase;
    row.tag = x.v.tag(r.phase);
    row.phaseName = x.v.name(r.phase);
    row.label = r.label;
    row.shortText = m.shortLabel;
    row.unit = r.unit;
    row.unitText = unitText(r.unit);
    row.group = m.group;
    row.multi = r.multi;
    row.measured = p.measured;
    row.value = num(p.measured ? p.value : kNaN);
    row.valueText = figBare(p.measured ? p.value : kNaN, r.unit);
    row.valueFigure = figU(p.measured ? p.value : kNaN, r.unit);
    row.medianText = figBare(r.box.med, r.unit);
    row.place = placeWords(p, r.box.n);
    row.rankHi = p.rankHi;
    row.rankLo = p.rankLo;
    row.rz = p.rz;
    row.side = p.side;
    row.apart = p.side != 0;
    row.box = boxOf(r.box, figOf(r.unit));
    row.values = dbls(r.values);
    row.firedShots = boolList(r.fired);
    row.fired = fired;
    row.firedWord = firedWord;
    row.corridor = corr;
    row.boxText = boxTextsOf(r, corr);
    row.hasCurve = curve;
    row.opens = curve ? QStringLiteral("trace") : QStringLiteral("run");
    return row;
}

RelationsReadingRow rowAt(const Ctx &x, int i) { return rowOf(x, x.r(i), x.m(i), i); }

// ── Typicality: this shot against the usual (Mark's ruling R8; governance/artefacts/design_system/variability.md) ─────────
// Charts is the technical layer: each surface shows a Typicality badge with the grader's basis ("this swing 82 mph ·
// usual 72 mph"). Never a grade, never praise or fault: whether the shot was good is the corridor's job, so the
// reading's norm corridor rides along where it has one.

namespace pv = pinpoint::variability;

// The shots no "usual" counts: a shot whose P-ladder cannot be trusted (consistencyGuarded, the Consistency matrix's own
// guard, on the session's P-alignment). Every other shot counts (R13). Filled once a job, only when a typicality window could hold N_MIN others; the picked shot is compared, never
// counted, whatever it is.
std::vector<uint8_t> outOfTypicalityWindow(Cache &c, const SessionSnapshot &S, int pick)
{
    if (pick < 0 || c.solo || S.count() <= pv::kNMin) return {};
    std::vector<uint8_t> out(S.shots.size(), 0);
    const SessionAlignment &al = c.align();
    for (size_t i = 0; i < S.shots.size(); ++i) {
        const ShotSeries &sh = *S.shots[i];
        out[i] = uint8_t(consistencyGuarded(sh, al.warpFor(sh.ordinal)));
    }
    return out;
}

// `values` (one per shot, strike order: a reading's, or a row's) of this shot against R8's window — the session's OTHER
// shots with this shot's club, guarded shots left out, at least N_MIN of them. An outcome (ball flight, smash factor)
// has none: ball flight has no badge at all (R4). `corridor` is a row's
// corridorOf(): its edges (signalLo/Hi) set the badge's corridor when both are closed. The result is a row's
// `variability` as PpChProfile reads it, a RelationsVariability { kind: "typicality", values, basisText, showCorridor };
// an invalid QVariant when there is no result (too few others, or no value on this shot), and the row is then drawn as
// it always was.
QVariant typicalityOf(const Ctx &x, const std::vector<double> &values, const QString &key, const QString &unit,
                      const RelationsCorridor &corridor)
{
    if (x.pick < 0 || x.typicalOut.empty() || size_t(x.pick) >= values.size() || !fin(values[size_t(x.pick)])) return {};
    if (pv::isOutcomeMetric(key)) return {};
    std::vector<pv::TypicalitySwing> swings;
    swings.reserve(size_t(x.n));
    for (int i = 0; i < x.n; ++i) {
        const ShotSeries &s = *x.S.shots[size_t(i)];
        const bool counted = i == x.pick || (size_t(i) < x.typicalOut.size() && !x.typicalOut[size_t(i)]);
        swings.push_back({ QString::number(s.ordinal), s.club, counted && size_t(i) < values.size() ? values[size_t(i)] : kNaN });
    }
    std::optional<double> lo, hi;
    if (corridor.known) {
        if (!corridor.lowOpen) lo = corridor.signalLo;
        if (!corridor.highOpen) hi = corridor.signalHi;
    }
    const pa::MetricDescriptor *d = key.isEmpty() ? nullptr : catalogue().descriptor(key);   // "" a time: Interval
    const std::optional<pv::ScalarTypicalityWindow> w =
        pv::scalarTypicalityWindow(swings, QString::number(x.S.shots[size_t(x.pick)]->ordinal),
                                   d ? d->scaleType : pv::ScaleType::Interval, lo, hi);
    if (!w) return {};
    const pv::TypicalityPayload p = pv::typicalityPayload(*w, isRatio(unit) ? QString() : unit,
                                                          isRatio(unit) ? 2 : words::decimalsFor(unit));
    return QVariant::fromValue(relationsTypicality(*w, p.basisText, w->corridor.has_value()));
}

// A READING ROW with its typicality badge when R8 gives one, from the row's own `values` (what the row shows: a
// launch-monitor row's club values, a stand-out's point on this shot's club's scale) and its corridor.
RelationsReadingRow withTypicality(const Ctx &x, RelationsReadingRow row)
{
    const std::vector<double> values(row.values.cbegin(), row.values.cend());
    const QVariant t = typicalityOf(x, values, row.key, row.unit, row.corridor);
    if (t.isValid()) row.variability = t;
    return row;
}

// ── "Apart from the session": the stand-out rule's rows (doc §5) ────────────────────────────

// One row of it. `reading` is the reading the row opens: the curve a span was sampled from, or the launch
// monitor's run. -1 when the session has no reading of the key: the key has a curve but no phase samples
// (swingPlaneCurve), or too few shots read it at a P-position. See curveOnlyReading.
struct ApartRow {
    RelationsReadingRow row;
    QString key;
    int reading = -1;
    int side = 0;           // the side of the others the shot fell on
    QString words;          // "secondary axis tilt at P4-P5"
};

// The reading that opens a key's row: read at the column's end, else its start, else wherever the key is read.
int readingOfKey(const Ctx &x, const QString &key, int column)
{
    int any = -1, start = -1;
    for (size_t i = 0; i < x.c.readings.size(); ++i) {
        const SessionReading &r = x.c.readings[i];
        if (r.key != key) continue;
        if (column < 0 || r.phase == kConsistencyPhases[column]) return int(i);
        if (column > 0 && r.phase == kConsistencyPhases[column - 1]) start = int(i);
        if (any < 0) any = int(i);
    }
    return start >= 0 ? start : any;
}

// A row's reading, with `values` in place of its own. The opening reading lends everything else.
SessionReading readingLike(const Ctx &x, int reading, const QString &key, const QString &label, const QString &unit,
                           int phase, std::vector<double> values)
{
    SessionReading r;
    if (reading >= 0) r = x.r(reading);
    else {
        r.key = key; r.phase = phase; r.id = key + QLatin1Char('@') + QString::number(phase);
        r.label = label; r.unit = shortUnit(unit);
    }
    r.values = std::move(values);
    r.fired.resize(r.values.size(), 0);
    return r;
}

ReadingMeta metaOf(const Ctx &x, int reading, const QString &key, const QString &label)
{
    if (reading >= 0) return x.m(reading);
    ReadingMeta m;
    const pa::MetricDescriptor *d = catalogue().descriptor(key);
    m.shortLabel = d ? d->shortLabel : label;
    if (d) m.group = d->group;
    return m;
}

// A row with no reading behind it opens the picked shot's curve of the key, by its id ("key@phase"), which
// openRow resolves here. This is the reading it opens: the curve's, with no session values, the key having no
// instant to read them at. Empty when the shot has no such curve to show.
SessionReading curveOnlyReading(const Ctx &x, const QString &id)
{
    const int at = id.lastIndexOf(QLatin1Char('@'));
    if (at <= 0 || !x.pickedHasCurve(id.left(at))) return {};
    const ShotSeries &shot = *x.S.shots[size_t(x.pick)];
    SessionReading r;
    r.key = id.left(at);
    if (shotCurve(shot, r.key).empty()) return {};
    const MetricSeries *m = shot.metric(r.key);
    r.id = id;
    r.phase = id.mid(at + 1).toInt();
    r.label = m->label;
    r.unit = shortUnit(m->unit);
    r.values.assign(size_t(x.n), kNaN);
    r.fired.assign(size_t(x.n), 0);
    return r;
}

// Such a row offers its curve, or, with none on this shot, nothing: no opens and no id to open.
void offerWhatOpens(const Ctx &x, ApartRow &a)
{
    if (a.reading >= 0) return;
    const bool curve = !curveOnlyReading(x, a.row.readingId).id.isEmpty();
    a.row.hasCurve = curve;
    a.row.opens = curve ? QStringLiteral("trace") : QString();
    if (!curve) a.row.readingId.clear();
}

// The launch monitor's reading against its own club: the club's values, and the club's box, the shot in it,
// as the fence that put it here was worked out.
ApartRow lmRowOf(const Ctx &x, const ConsistencyLmRow &lm)
{
    ApartRow a;
    a.key = lm.key;
    a.side = lm.side;
    a.reading = readingOfKey(x, lm.key, -1);
    a.words = lowerLabel(lm.label);
    SessionReading r = readingLike(x, a.reading, lm.key, lm.label, lm.unit, phase::Impact, lm.values);
    r.box = boxStats(r.values);
    a.row = rowOf(x, r, metaOf(x, a.reading, lm.key, lm.label), a.reading);
    offerWhatOpens(x, a);
    return a;
}

// A tested reading at one of its points, as a row's reading. The test compares residuals, so every shot is set on
// this shot's club's scale, the club's median at that point put back. In a session of one club those are the plain
// values, and whatever the clubs, this shot's value less the median is the difference that was judged. `leaveShotOut`
// takes the shot out of the box, as a stand-out's comparison does; it stays on the row's scale. A shot its club's
// count left out of the comparison keeps its own value beside the rest, outside the box.
SessionReading readingAtPoint(const Ctx &x, const ConsistencyReading &tested, int opens, int column, int sample, bool leaveShotOut)
{
    const ConsistencyInput &in = x.c.consistencyIn;
    QStringList clubs;
    for (const ConsistencyShot &sh : in.shots) clubs << sh.club;
    const std::vector<double> residuals = consistencyResiduals(tested.values, tested.samples, clubs);
    const auto residualOf = [&](int i) { return residuals[size_t(i) * size_t(tested.samples) + size_t(sample)]; };
    const int N = int(in.shots.size());
    const bool compared = x.pick >= 0 && fin(residualOf(x.pick));
    double scale = kNaN;
    if (compared) scale = tested.at(x.pick, sample) - residualOf(x.pick);
    else {
        std::vector<double> raw;
        for (int i = 0; i < N; ++i) if (fin(residualOf(i))) raw.push_back(tested.at(i, sample));
        scale = medianOf(std::move(raw));
    }
    std::vector<double> values(size_t(N), kNaN);
    for (int i = 0; i < N; ++i) values[size_t(i)] = residualOf(i) + scale;
    std::vector<double> inBox = values;
    if (x.pick >= 0 && (leaveShotOut || !compared)) inBox[size_t(x.pick)] = kNaN;
    if (x.pick >= 0 && !compared) values[size_t(x.pick)] = tested.at(x.pick, sample);

    SessionReading r = readingLike(x, opens, tested.key, tested.label, tested.unit, kConsistencyPhases[column], std::move(values));
    r.box = boxStats(inBox);
    if (x.pick >= 0 && !fin(inBox[size_t(x.pick)]) && fin(r.values[size_t(x.pick)]) && r.box.n > 0) {
        const double v = r.values[size_t(x.pick)];
        r.box.min = std::min(r.box.min, v);
        r.box.max = std::max(r.box.max, v);
        ++r.box.n;
    }
    return r;
}

// A row showing what was tested, so the number shown is the number judged.
ApartRow testedRowOf(const Ctx &x, const ConsistencyReading &tested, int column, int sample, bool leaveShotOut)
{
    ApartRow a;
    a.key = tested.key;
    a.reading = readingOfKey(x, tested.key, column);
    const bool atImpact = consistencyReadsAtImpact(tested.area);        // Delivery reads P7 itself, not the span into it
    const QString tag = atImpact ? x.v.tag(phase::Impact) : consistencyColumnName(column);
    a.words = lowerLabel(tested.label) + QStringLiteral(" at ") + tag;

    SessionReading r = readingAtPoint(x, tested, a.reading, column, sample, leaveShotOut);
    const bool sameInstant = tested.kind == ConsistencyReadingKind::Position && a.reading >= 0 && x.r(a.reading).phase == kConsistencyPhases[column];
    if (!sameInstant) std::fill(r.fired.begin(), r.fired.end(), uint8_t(0));
    a.row = rowOf(x, r, metaOf(x, a.reading, tested.key, tested.label), sameInstant ? a.reading : -1);
    offerWhatOpens(x, a);
    a.row.tag = tag;
    a.row.phaseName = atImpact ? x.v.name(phase::Impact) : tag;
    return a;
}

// A stand-out: the weakest point of the reading, this shot against the others' box there.
ApartRow standOutRowOf(const Ctx &x, const ConsistencyStandOut &s)
{
    ApartRow a = testedRowOf(x, x.c.consistencyIn.readings[size_t(s.reading)], s.column, s.sample, true);
    a.side = s.dv < 0 ? -1 : 1;
    return a;
}

// Ball, then club, then swing, each as the rule orders it.
std::vector<ApartRow> apartRows(const Ctx &x)
{
    std::vector<ApartRow> rows;
    for (const ConsistencyLmRow &lm : x.c.lmRows[size_t(x.pick)]) rows.push_back(lmRowOf(x, lm));
    const int ordinal = x.c.consistencyIn.shots[size_t(x.pick)].ordinal;
    for (const ConsistencyStandOut &s : x.c.consistencyOut.standouts)
        if (s.ordinal == ordinal) rows.push_back(standOutRowOf(x, s));
    return rows;
}

// The other shots on the same side of the same reading, by ordinal.
std::vector<int> othersApart(const Ctx &x, const ApartRow &a)
{
    std::vector<int> same;
    const int me = x.c.consistencyIn.shots[size_t(x.pick)].ordinal;
    for (size_t i = 0; i < x.c.lmRows.size(); ++i)
        for (const ConsistencyLmRow &lm : x.c.lmRows[i])
            if (lm.key == a.key && lm.side == a.side && int(i) != x.pick) same.push_back(x.c.consistencyIn.shots[i].ordinal);
    for (const ConsistencyStandOut &s : x.c.consistencyOut.standouts)
        if (s.key == a.key && (s.dv < 0 ? -1 : 1) == a.side && s.ordinal != me) same.push_back(s.ordinal);
    return same;
}

// Why a shot with no rows has none, when the reason is that no test could run on it. "" when one could. `swingOnly`
// asks of the swing's tests alone, for a sentence about the swing.
QString cannotCompare(const Ctx &x, bool swingOnly)
{
    const ConsistencyInput &in = x.c.consistencyIn;
    const ConsistencyComparable can = consistencyComparable(*x.c.recorded, in, x.c.consistencyOut, x.pick);
    if (can.swing || (!swingOnly && can.ball)) return QString();
    if (!can.measured && !swingOnly)
        return can.launchMonitor ? QStringLiteral("This shot has no camera readings, and the launch monitor needs eight shots with a club to compare it.")
                                 : QStringLiteral("This shot has no camera or launch-monitor readings to compare.");
    const QString &club = in.shots[size_t(x.pick)].club;
    const int sameClub = int(std::count_if(in.shots.begin(), in.shots.end(), [&](const ConsistencyShot &s) { return s.club == club; }));
    const int ordinal = words::shotNumber(in.shots[size_t(x.pick)].shotId, in.shots[size_t(x.pick)].ordinal);   // "shot N" = the swing number
    if (sameClub < kConsistencyMinClubShots) return QStringLiteral("There are too few shots with this club to set shot %1 against them.").arg(ordinal);
    return QStringLiteral("There are too few shots in the session to set shot %1 against them.").arg(ordinal);
}

RelationsDrift driftValue(const Correlation &d, int n)
{
    RelationsDrift out;
    out.defined = d.defined;
    out.r = num(d.r);
    out.lo = num(d.lo);
    out.hi = num(d.hi);
    out.clear = d.clear;
    out.rText = figR(d.r);
    out.ciText = d.defined ? QStringLiteral("(%1 to %2)").arg(figR(d.lo), figR(d.hi)) : QString();
    out.slope = num(d.slope);
    out.intercept = num(d.intercept);
    if (d.defined && d.clear) {
        RelationsLine l;
        l.x0 = 1; l.y0 = d.intercept + d.slope;
        l.x1 = n; l.y1 = d.intercept + d.slope * n;
        out.line = QVariant::fromValue(l);
    }
    return out;
}

RelationsReadingRun runOf(const Ctx &x, const SessionReading &r)
{
    RelationsReadingRun out;
    for (int i = 0; i < x.n; ++i) {
        RelationsRunPoint p = x.shotMark<RelationsRunPoint>(i);
        const double v = size_t(i) < r.values.size() ? r.values[size_t(i)] : kNaN;
        p.value = num(v);
        p.measured = fin(v);
        p.fired = size_t(i) < r.fired.size() && r.fired[size_t(i)];
        p.picked = i == x.pick;
        out.points.append(p);
    }
    const Correlation d = driftOf(r);
    QString cap = r.label + QStringLiteral(" at ") + x.v.tag(r.phase) + QStringLiteral(", shot by shot");
    if (d.defined)
        cap += QStringLiteral("  ·  drift r %1 (%2 to %3)").arg(figR(d.r), figR(d.lo), figR(d.hi)) + (d.clear ? QString() : QStringLiteral(", inside chance"));
    out.readingId = r.id;
    out.key = r.key;
    out.label = r.label;
    out.tag = x.v.tag(r.phase);
    out.unit = r.unit;
    out.unitText = unitText(r.unit);
    out.caption = cap;
    out.box = boxOf(r.box, figOf(r.unit));
    out.yRange = paddedRange(r.box, 0.1);
    out.drift = driftValue(d, x.n);
    return out;
}

// One reading's curve on the picked shot's own clock, with a box of every shot at each P-position the
// key is read at (Trace.qml). nullopt with no picked shot or no curve on it.
std::optional<RelationsTrace> traceOf(const Ctx &x, const SessionReading &r, TraceCurve *keep)
{
    if (x.pick < 0) return std::nullopt;
    const ShotSeries &s = *x.S.shots[size_t(x.pick)];
    const TraceCurve c = shotCurve(s, r.key);
    if (c.empty()) return std::nullopt;
    double lo = kNaN, hi = kNaN;
    for (size_t i = 0; i < c.t.size(); ++i) if (fin(c.v[i])) { if (!fin(lo)) lo = c.t[i]; hi = c.t[i]; }
    const qint64 a = phaseUsOf(s, phase::Address), f = phaseUsOf(s, phase::Finish);
    double t0 = fin(lo) ? lo : c.t.front(), t1 = fin(hi) ? hi : c.t.back();
    if (a >= 0) t0 = std::max(t0, double(a - s.impactUs) / 1000.0 - 150.0);
    if (f >= 0) t1 = std::min(t1, double(f - s.impactUs) / 1000.0 + 100.0);
    if (!(t1 > t0)) { t0 = c.t.front(); t1 = c.t.back(); }

    RelationsTrace out;
    double ylo = std::numeric_limits<double>::infinity(), yhi = -ylo;
    for (size_t i = 0; i < c.t.size(); ++i) {
        if (c.t[i] < t0 - 1e-9 || c.t[i] > t1 + 1e-9) continue;
        out.curve.t.append(c.t[i]);
        out.curve.v.append(c.ok[i] && fin(c.v[i]) ? c.v[i] : kNaN);     // NaN where bridged (the map's null)
        out.curve.raw.append(fin(c.raw[i]) ? c.raw[i] : kNaN);
        out.curve.ok.append(bool(c.ok[i]));
        if (c.ok[i] && fin(c.v[i])) { ylo = std::min(ylo, c.v[i]); yhi = std::max(yhi, c.v[i]); }
    }
    for (const PhaseMark &p : s.phases) {
        if (!x.v.isP(p.phase)) continue;
        const double ms = double(p.tUs - s.impactUs) / 1000.0;
        if (ms < t0 || ms > t1) continue;
        RelationsTracePhase ph;
        ph.phase = p.phase;
        ph.tag = x.v.tag(p.phase);
        ph.shotMs = ms;
        ph.impact = p.phase == phase::Impact;
        out.phases.append(ph);
    }
    // every P-position the key is sampled at on some shot
    std::vector<int> phs;
    for (const auto &sh : x.S.shots)
        if (const MetricSeries *ms = sh->metric(r.key))
            for (const PhaseSample &p : ms->phaseSamples)
                if (fin(p.value) && std::find(phs.begin(), phs.end(), p.phase) == phs.end()) phs.push_back(p.phase);
    for (int ph : phs) {
        const qint64 us = phaseUsOf(s, ph);
        if (us < 0) continue;
        std::vector<double> vals;
        for (const auto &sh : x.S.shots) vals.push_back(valueAtPhaseOf(*sh, r.key, ph));
        const BoxStats b = boxStats(vals);
        if (b.n < 3) continue;
        ylo = std::min(ylo, b.min); yhi = std::max(yhi, b.max);
        const double pv = vals[size_t(x.pick)];
        RelationsTraceInstant in;
        in.phase = ph;
        in.tag = x.v.tag(ph);
        in.shotMs = double(us - s.impactUs) / 1000.0;
        in.box = boxOf(b, figOf(r.unit));
        in.values = dbls(vals);
        if (ph == r.phase) in.firedShots = boolList(r.fired);
        in.pickedValue = num(pv);
        in.pickedText = figU(pv, r.unit);
        in.own = ph == r.phase;
        out.instants.append(in);
    }
    if (!fin(ylo)) { ylo = 0; yhi = 1; }
    const double pad = std::max(1e-6, (yhi - ylo) * 0.08);
    if (keep) *keep = c;
    out.readingId = r.id;
    out.key = r.key;
    out.label = r.label;
    out.tag = x.v.tag(r.phase);
    out.unit = r.unit;
    out.unitText = unitText(r.unit);
    out.caption = out.instants.isEmpty() ? QStringLiteral("%1, shot %2").arg(r.label).arg(words::shotNumber(s.shotId, s.ordinal))
                                         : QStringLiteral("%1, shot %2; at each P-position a box of all %3 shots").arg(r.label).arg(words::shotNumber(s.shotId, s.ordinal)).arg(x.n);
    out.window.t0 = t0;
    out.window.t1 = t1;
    out.yRange = rangeOf(ylo - pad, yhi + pad);
    return out;
}

QString axisName(const Ctx &x, const SessionReading &r)
{
    return r.label + QStringLiteral(" at ") + x.v.tag(r.phase)
         + ((!r.unit.isEmpty() && r.unit != QStringLiteral("°")) ? QStringLiteral(", ") + r.unit : QString());
}

RelationsScatter scatterOf(const Ctx &x, const SessionReading &rx, const SessionReading &ry, const std::vector<int> &subset,
                           const std::vector<uint8_t> &firedMask)
{
    std::vector<int> idx;
    if (subset.empty()) { for (int i = 0; i < x.n; ++i) idx.push_back(i); }
    else for (int o : subset) idx.push_back(o - 1);
    std::vector<double> xs, ys, xa(size_t(x.n), kNaN), ya(size_t(x.n), kNaN);
    RelationsScatter out;
    for (int i : idx) {
        const double a = rx.values[size_t(i)], b = ry.values[size_t(i)];
        xs.push_back(a); ys.push_back(b); xa[size_t(i)] = a; ya[size_t(i)] = b;
        if (!fin(a) || !fin(b)) continue;
        RelationsScatterPoint p = x.shotMark<RelationsScatterPoint>(i);
        p.x = a; p.y = b;
        p.picked = i == x.pick;
        p.fired = size_t(i) < firedMask.size() && firedMask[size_t(i)];
        out.points.append(p);
    }
    const Correlation f = pearson(xs, ys);
    if (f.defined) {
        double x0 = kNaN, x1 = kNaN;
        for (size_t i = 0; i < xs.size(); ++i) if (fin(xs[i]) && fin(ys[i])) { x0 = fin(x0) ? std::min(x0, xs[i]) : xs[i]; x1 = fin(x1) ? std::max(x1, xs[i]) : xs[i]; }
        RelationsLine l;
        l.x0 = x0; l.y0 = f.intercept + f.slope * x0;
        l.x1 = x1; l.y1 = f.intercept + f.slope * x1;
        out.line = QVariant::fromValue(l);
    }
    out.caption = f.defined ? QStringLiteral("r %1 (%2 to %3)  ·  n %4").arg(figR(f.r), figR(f.lo), figR(f.hi)).arg(f.n)
                                  + (f.clear ? QString() : QStringLiteral("  ·  inside chance"))
                            : QStringLiteral("Too few shots for a line.");
    out.xId = rx.id;
    out.yId = ry.id;
    out.xKey = rx.key;
    out.yKey = ry.key;
    out.xLabel = axisName(x, rx) + QStringLiteral("  →");
    out.yLabel = QStringLiteral("↑  ") + axisName(x, ry);
    out.xUnit = rx.unit;
    out.yUnit = ry.unit;
    out.xRange = paddedRange(rx.box, 0.08);
    out.yRange = paddedRange(ry.box, 0.08);
    out.xBox = boxOf(boxStats(xa), figOf(rx.unit));
    out.yBox = boxOf(boxStats(ya), figOf(ry.unit));
    out.defined = f.defined;
    out.r = num(f.r);
    out.lo = num(f.lo);
    out.hi = num(f.hi);
    out.n = f.n;
    out.clear = f.clear;
    out.rText = figR(f.r);
    out.ciText = f.defined ? QStringLiteral("(%1 to %2)").arg(figR(f.lo), figR(f.hi)) : QString();
    return out;
}

QString slopeText(const Correlation &c, const SessionReading &x, const SessionReading &y)
{
    if (!c.defined || !fin(c.slope)) return QStringLiteral("-");
    const double a = std::abs(c.slope);
    const int d = a >= 10 ? 0 : a >= 1 ? 1 : a >= 0.1 ? 2 : 3;
    const QString xu = x.unit.isEmpty() ? QStringLiteral("unit") : x.unit;
    return words::withUnit(words::figSigned(c.slope, d), y.unit) + QStringLiteral(" per ") + xu;
}

RelationsLedgerRow ledgerRowOf(const Ctx &x, const CorrRow &row, const SessionReading &y, int rank)
{
    const SessionReading &r = x.r(row.reading);
    const Correlation &c = row.c;
    RelationsLedgerRow out;
    out.rank = row.same ? 0 : rank;
    out.readingId = r.id;
    out.key = r.key;
    out.label = r.label;
    out.shortText = x.m(row.reading).shortLabel;
    out.tag = x.v.tag(r.phase);
    out.same = row.same;
    out.sameMark = row.same ? QStringLiteral("‡") : QString();
    out.finding = !row.same && c.clear;
    out.r = num(c.r);
    out.rText = figR(c.r);
    out.lo = num(c.lo);
    out.hi = num(c.hi);
    out.loText = figR(c.lo);
    out.hiText = figR(c.hi);
    out.ciText = QStringLiteral("(%1 to %2)").arg(figR(c.lo), figR(c.hi));
    out.n = c.n;
    out.clear = c.clear;
    out.slope = num(c.slope);
    out.slopeText = slopeText(c, r, y);
    out.pair.xs = dbls(r.values);
    out.pair.ys = dbls(y.values);
    out.pair.xRange = rangeOf(r.box.min, r.box.max);
    out.pair.yRange = rangeOf(y.box.min, y.box.max);
    return out;
}

RelationsBrief briefOf(const Ctx &x, int i)
{
    const SessionReading &r = x.r(i);
    RelationsBrief out;
    out.readingId = r.id;
    out.key = r.key;
    out.label = r.label;
    out.shortText = x.m(i).shortLabel;
    out.tag = x.v.tag(r.phase);
    out.unit = r.unit;
    out.group = x.m(i).group;
    out.text = x.m(i).shortLabel + QLatin1Char(' ') + x.v.tag(r.phase);
    return out;
}

Sentence refusalSentence(const QString &what, int need, int have)
{
    Sentence s;
    s.append(words::cap1(words::word(need)) + QStringLiteral(" shots are needed ") + what + QStringLiteral("; this session has "))
     .append(have == 0 ? QStringLiteral("none") : words::word(have)).append(QStringLiteral("."));
    return s;
}

// No session at all, or one with nothing analysed: said as that, never as "N shots are needed; has none".
Sentence emptySession(const Ctx &x)
{
    return Sentence().append(x.S.sessionDir.isEmpty() ? QStringLiteral("No session is loaded.")
                                                      : QStringLiteral("No shot in this session has been analysed yet."));
}
Sentence tooFew(const Ctx &x, const QString &what, int need)
{
    return x.n == 0 ? emptySession(x) : refusalSentence(what, need, x.n);
}

// ── This shot ──────────────────────────────────────────────────────────────────────────────

// THE FAMILY, BY DATA. The reader's choice when the session has it; else the register's default (Club
// delivery in Replay, Kinematic sequence in Analyse), then Club delivery, then every family in manifest order —
// the first of those that carries a reading THIS SHOT measured (an LM-only session has no pose families,
// a one-camera session no sequence), else the first that carries any reading at all.
int familyIndex(const Cache &c, const QString &wanted, const QString &reg, int pick)
{
    const auto find = [&](const QString &name) {
        for (size_t i = 0; i < c.families.size(); ++i) if (c.families[i].name == name) return int(i);
        return -1;
    };
    int f = wanted.isEmpty() ? -1 : find(wanted);
    if (f >= 0) return f;                           // the reader asked for it and the session has it
    std::vector<int> order{ find(reg == QLatin1String("replay") ? QStringLiteral("Club delivery") : QStringLiteral("Kinematic sequence")),
                            find(QStringLiteral("Club delivery")) };
    for (size_t i = 0; i < c.families.size(); ++i) order.push_back(int(i));
    const auto pickedHas = [&](int fi) {
        if (pick < 0) return true;
        for (int r : c.families[size_t(fi)].readings) {
            const auto &v = c.readings[size_t(r)].values;
            if (size_t(pick) < v.size() && fin(v[size_t(pick)])) return true;
        }
        return false;
    };
    for (int fi : order) if (fi >= 0 && pickedHas(fi)) return fi;
    for (int fi : order) if (fi >= 0) return fi;
    return -1;
}

const QString kSeqFamily = QStringLiteral("Kinematic sequence");
const QStringList kSegLabels { QStringLiteral("Pelvis"), QStringLiteral("Chest"), QStringLiteral("Lead arm"), QStringLiteral("Club") };

QString signedMsBare(double ms)
{
    const QString s = words::signedMs(ms);
    return (s.endsWith(QStringLiteral(" ms")) || s.endsWith(QString(QChar(0x00A0)) + QStringLiteral("ms"))) ? s.chopped(3) : s;
}

RelationsKeyFigures keyFiguresOf(const Ctx &x, const Cache::Family *fam)
{
    RelationsKeyFigures out;
    const bool seq = fam && fam->name == kSeqFamily;
    if (seq && x.pick >= 0) {
        const ShotSequence &q = x.c.seq.shots[size_t(x.pick)];
        for (int s = 0; s < kSequenceSegments; ++s) {
            const SegmentPeak &p = q.segments[size_t(s)];
            const SegmentSummary &sm = x.c.seq.segments[size_t(s)];
            // a placed node, or the chest's probable peak (kinematic_sequence_design.md §12.6), which carries its ⚠
            const double v = p.timed() ? p.timeMs() : kNaN;
            const int timedN = sm.placed + sm.probable;
            // why an unplaced node is unplaced, for the tile's spoken sentence only (the bound and the gate's words)
            UnplacedWhy why = p.present && !p.timed() ? unplacedWhy(p) : UnplacedWhy{ QStringLiteral("-"), QString(), QStringLiteral("not placed"), QString() };
            // a node with no curve at all may still have a gate behind it: a cascade covering it (a refused club
            // track takes the club node with it), named by its own words rather than a bare "not placed"
            if (!p.present) {
                const QVariantMap g = gateOfPicked(x, QStringLiteral("ks.") + sequenceSegmentIds()[size_t(s)]);
                if (!g.isEmpty()) {
                    why.shortText = g.value(QStringLiteral("words")).toString();
                    why.sentence = g.value(QStringLiteral("sentence")).toString();
                }
            }
            const QString median = timedN >= 3 ? QStringLiteral("median ") + words::signedMs(sm.medianMs) : QString();
            // SAID ONCE (Mark, 17 Oct 2026): the headline names an unplaced node and the panel's tail says why; the
            // tile is a plain "-" with the median, no bound, no gate words, no link
            const QString foot = p.timed() ? (median.isEmpty() ? QStringLiteral("median needs 3 shots") : median) : median;
            RelationsKeyFigure item;
            item.readingId = sequenceSeriesKey(s);
            item.key = sequenceSeriesKey(s);
            item.label = kSegLabels[s] + QStringLiteral(" peak") + (p.probable ? QStringLiteral(" ⚠") : QString());
            item.figure = p.timed() ? signedMsBare(v) : QStringLiteral("-");
            item.unit = p.timed() && fin(v) ? QStringLiteral("ms") : QString();
            item.foot = foot;
            item.median = timedN >= 3 ? words::signedMs(sm.medianMs) : QString();
            item.warn = p.late;
            item.warnWord = p.late ? QStringLiteral("late") : QString();
            // the reason lives in the panel's tail ("How far can I trust this?"), not on the tile: no trustSubject
            item.sentence = sentenceOf((p.timed() || why.sentence.isEmpty() ? Sentence().figure(words::signedMs(v)) : Sentence().append(why.sentence))
                                           .append(p.probable ? (p.probableHow == QLatin1String("seen") ? QStringLiteral(", indicated") : QStringLiteral(", probable")) : QString())
                                           .append(timedN >= 3 ? QStringLiteral(", median ") : QString())
                                           .figure(timedN >= 3 ? words::signedMs(sm.medianMs) : QString())
                                           .append(p.late ? QStringLiteral(", late") : QString()));
            // R8: this shot's peak time against the usual, from every timed node of the segment (a probable chest peak
            // counts, with its ⚠ on the label); none when the node is not timed on this shot
            std::vector<double> times(size_t(x.n), kNaN);
            for (int k = 0; k < x.n && size_t(k) < x.c.seq.shots.size(); ++k) {
                const SegmentPeak &o = x.c.seq.shots[size_t(k)].segments[size_t(s)];
                if (o.timed()) times[size_t(k)] = o.timeMs();
            }
            const QVariant typ = p.timed() ? typicalityOf(x, times, QString(), QStringLiteral("ms"), RelationsCorridor{}) : QVariant();
            if (typ.isValid()) item.typicality = typ;
            out.items.append(item);
        }
    } else if (fam && x.pick >= 0) {
        for (int i : fam->readings) {
            if (out.items.size() >= 4) break;
            const SessionReading &r = x.r(i);
            const ReadingPlace p = placeOf(r, x.pick);
            if (!p.measured) {
                // A REFUSED READING KEEPS ITS PLACE as a plain "-"; why is said once, in the panel's tail
                const QVariantMap g = gateOfPicked(x, r.key);
                if (g.isEmpty()) continue;
                const bool solo = x.c.solo || x.n < 3;
                const QString med = solo ? QString() : figBare(r.box.med, r.unit);
                RelationsKeyFigure item;
                item.readingId = r.id;
                item.key = r.key;
                item.label = x.m(i).shortLabel + QStringLiteral(" at ") + x.v.tag(r.phase);
                item.figure = QStringLiteral("-");
                item.foot = med.isEmpty() ? QString() : QStringLiteral("median ") + med;
                item.median = med;
                item.sentence = sentenceOf(Sentence().append(g.value(QStringLiteral("words")).toString()));
                out.items.append(item);
                continue;
            }
            const bool fired = r.fired[size_t(x.pick)];
            QString warnWord;
            if (fired) {
                const QList<FiredLink> ls = x.c.firedBy.value(qint64(i) * x.n + x.pick);
                warnWord = ls.isEmpty() ? QStringLiteral("out of its corridor") : firedWordFor(x.v, ls.first());
            }
            // Too few shots to place against (n < 3): the value alone, and the foot says why.
            const bool solo = x.c.solo || x.n < 3;
            const QString place = solo ? QString() : placeWords(p, r.box.n), med = solo ? QString() : figBare(r.box.med, r.unit);
            Sentence s;
            s.figure(figU(p.value, r.unit));
            if (!solo) s.append(QStringLiteral(", median ")).figure(med);
            if (!place.isEmpty()) s.append(QStringLiteral(", ") + place);
            QStringList foot;
            if (solo) foot << (x.n <= 1 ? QStringLiteral("the session's only shot") : QStringLiteral("%1 shots: too few to place it").arg(words::cap1(words::word(x.n))));
            else foot << QStringLiteral("median ") + med;
            if (!place.isEmpty()) foot << place;
            // R8: a technical Typicality badge under the figure, when the window holds 8 (the foot stays)
            const QVariant typ = solo ? QVariant() : typicalityOf(x, r.values, r.key, r.unit, corridorOf(x, r));
            RelationsKeyFigure item;
            item.readingId = r.id;
            item.key = r.key;
            item.label = x.m(i).shortLabel + QStringLiteral(" at ") + x.v.tag(r.phase);
            item.figure = closedUnit(r.unit) ? figU(p.value, r.unit) : figBare(p.value, r.unit);
            item.unit = closedUnit(r.unit) ? QString() : r.unit;
            item.foot = foot.join(QStringLiteral("  ·  "));
            item.median = med;
            item.place = place;
            item.warn = fired;
            item.warnWord = warnWord;
            if (typ.isValid()) item.typicality = typ;
            item.sentence = sentenceOf(s);
            out.items.append(item);
        }
    }
    out.family = fam ? fam->name : QString();
    out.sequence = seq;
    return out;
}

// The Readings tab's verdict filters, in the order the tab shows them: the word, whether a count under it wears the
// warn tone (a fault, or out of the corridor), and what the tab says when nothing is under it — `none` with no name
// filter, `noneNamed` with one (%1 the filter's text). All has no empty words of its own: with nothing under All the
// name filter let nothing through, and the tab says thisShot.noName. A row's `verdicts` says which ones it shows under.
QList<RelationsVerdictFilter> readingVerdictFilters()
{
    const auto filter = [](const char *v, const char *word, bool warm, const QString &tail) {
        RelationsVerdictFilter f;
        f.verdict = QString::fromLatin1(v);
        f.word = QString::fromLatin1(word);
        f.warm = warm;
        f.none = tail.isEmpty() ? QString() : QStringLiteral("No reading ") + tail;
        f.noneNamed = tail.isEmpty() ? QString() : QStringLiteral("No reading named “%1” ") + tail;
        return f;
    };
    return { filter("all", "All", false, QString()),
             filter("fault", "Faults", true, QStringLiteral("is past its fault line on this shot.")),
             filter("outside", "Outside", true, QStringLiteral("is out of its corridor on this shot.")),
             filter("inside", "Inside", false, QStringLiteral("was graded inside its corridor on this shot.")) };
}

RelationsAsk askOf(const QString &id, const QString &text, const QString &answer, const std::vector<int> &lit,
                   const QString &go = QString(), const QString &upId = QString())
{
    RelationsAsk a;
    a.id = id;
    a.text = text;
    a.answer = answer;
    a.lit = intList(lit);
    a.go = go;
    a.upId = upId;
    return a;
}

RelationsThisShot thisShotOf(const Ctx &x, const Cache::Family *fam, Output *out)
{
    RelationsThisShot m;
    m.total = int(x.c.readings.size());
    // the Readings tab's filter words, whatever the shot (the tab draws them only when there are readings)
    m.verdictFilters = readingVerdictFilters();
    m.noName = QStringLiteral("No reading's name holds “%1”.");
    if (x.pick < 0) {
        m.available = false;
        m.head = sentenceOf(x.n == 0 ? emptySession(x) : Sentence().append(QStringLiteral("No shot is picked")));
        return m;
    }
    const ShotSeries &ps = *x.S.shots[size_t(x.pick)];
    m.kicker = words::shotOfText(ps.shotId, ps.ordinal, x.n)
               + (ps.club.isEmpty() ? QString() : QStringLiteral("  ·  ") + ps.club.toLower());
    m.keyFigures = keyFiguresOf(x, fam);
    // the family's lead reading on this shot — this shot's own, so published at any n
    if (fam) {
        int li = -1;
        if (fam->name == kSeqFamily) li = x.c.indexOf(QStringLiteral("clubAngularSpeed@%1").arg(phase::Impact));
        if (li < 0) for (int i : fam->readings) if (x.m(i).drawsCurve && x.pickedHasCurve(x.r(i).key)) { li = i; break; }
        if (li < 0) for (int i : fam->readings) if (placeOf(x.r(i), x.pick).measured) { li = i; break; }
        if (li < 0 && !fam->readings.empty()) li = fam->readings.front();
        if (li >= 0) {
            const std::optional<RelationsTrace> tr = traceOf(x, x.r(li), &out->leadCurve);
            out->leadUnit = x.r(li).unit;
            m.lead.readingId = x.r(li).id;
            if (tr) m.lead.trace = QVariant::fromValue(*tr);
        }
    }
    if (x.n < 3) {
        m.available = false;
        m.head = sentenceOf(tooFew(x, QStringLiteral("to set a shot against its session"), 3));
        return m;
    }
    m.available = true;

    // apart: the launch monitor's rows, then the stand-outs, by the rule the Consistency tab shares
    out->consistencyMs = x.c.ensureConsistency(*x.env);
    const std::vector<ApartRow> ap = apartRows(x);
    for (const ApartRow &a : ap) m.apart.append(withTypicality(x, a.row));     // R8: the badge, when the window holds 8
    const int na = int(ap.size());
    const QString cannot = na ? QString() : cannotCompare(x, false);
    m.apartCount = na;
    m.apartLabel = na > 0 ? QStringLiteral("%1 %2").arg(na).arg(na == 1 ? QStringLiteral("reading") : QStringLiteral("readings")) : QString();
    m.head = sentenceOf(na ? Sentence().append(words::cap1(words::word(na)) + (na == 1 ? QStringLiteral(" reading") : QStringLiteral(" readings"))
                                               + QStringLiteral(" apart from the session"))
                           : !cannot.isEmpty() ? Sentence().append(QStringLiteral("Shot %1 cannot be compared").arg(words::shotNumber(ps.shotId, ps.ordinal)))
                                               : Sentence().append(QStringLiteral("Nothing stood out on shot %1").arg(words::shotNumber(ps.shotId, ps.ordinal))));
    Sentence st;
    if (!cannot.isEmpty()) st.append(cannot);
    else if (!na) st.append(QStringLiteral("Set against the other shots, club for club, nothing on shot %1 stood out.").arg(words::shotNumber(ps.shotId, ps.ordinal)));
    else {
        st.append(QStringLiteral("Set against the other shots, club for club, shot %1 stood out on ").arg(words::shotNumber(ps.shotId, ps.ordinal)));
        if (na == 1) st.append(QStringLiteral("one reading: ") + ap[0].words + QStringLiteral("."));
        else st.append(words::word(na) + QStringLiteral(" readings, starting with ") + ap[0].words + QStringLiteral("."));
    }
    if (x.c.consistencyIn.shots[size_t(x.pick)].guarded)
        st.append(QLatin1Char(' ') + ConsistencyReadingRows::guardNote());
    m.standfirst = sentenceOf(st);

    // every reading by family (D5)
    for (const QString &g : x.c.groupOrder) {
        RelationsRowGroup group;
        group.name = g;
        for (size_t i = 0; i < x.c.readings.size(); ++i)
            if (x.c.meta[i].group == g) group.rows.append(withTypicality(x, rowAt(x, int(i))));   // R8, as Apart's
        if (!group.rows.isEmpty()) m.groups.append(group);
    }
    m.notes = QStringLiteral("Apart: a long way from the other shots, each shot set against its own club, across the whole "
                             "of a span and not at one instant in it. A launch-monitor figure is apart when it falls well "
                             "outside its club's usual range, which takes eight shots with that club. A span's row shows the "
                             "point closest to the others, with their box at that point. Each row has its own scale, lowest "
                             "to highest. A fired mark is a reading a diagnostics condition graded and found outside its corridor.");
    m.firedElsewhere = x.c.firedElsewhere.value(x.pick);

    // the family: the lead reading's trace and the other rows (Replay)
    if (fam) {
        const bool seq = fam->name == kSeqFamily;
        if (!seq) {
            int figs = 0;
            for (int i : fam->readings) if (figs < 4 && placeOf(x.r(i), x.pick).measured) ++figs;
            QStringList seen;
            std::vector<int> order(fam->readings.begin() + std::min<size_t>(size_t(figs), fam->readings.size()), fam->readings.end());
            for (int k = 0; k < figs && k < int(fam->readings.size()); ++k) if (x.r(fam->readings[size_t(k)]).multi) order.push_back(fam->readings[size_t(k)]);
            for (int i : order) if (!seen.contains(x.r(i).id)) { seen << x.r(i).id; m.familyRows.append(rowAt(x, i)); }
        }
    }

    // questions (Analyse.qml shotAsks)
    for (const ApartRow &a : ap) {
        const std::vector<int> same = othersApart(x, a);
        if (same.empty()) continue;
        std::vector<int> lit = same; lit.push_back(ps.ordinal);
        m.asks.append(askOf(QStringLiteral("same"), a.row.label + QStringLiteral(": was any other shot out there?"),
                            words::cap1(words::shotsList(same)) + (same.size() == 1 ? QStringLiteral(" was") : QStringLiteral(" were"))
                                + QStringLiteral(" apart the same way; lit in every row."),
                            lit));
        break;
    }
    if (!ap.empty() && ap[0].reading >= 0)
        m.asks.append(askOf(QStringLiteral("moves"), x.r(ap[0].reading).label + QStringLiteral(": what moves with it?"),
                            QStringLiteral("Relations opens on it."), {}, QStringLiteral("relations"), x.r(ap[0].reading).id));
    if (x.c.kinds.method != KindsMethod::None)
        m.asks.append(askOf(QStringLiteral("kind"), QStringLiteral("Shot %1: which kind of swing was it?").arg(words::shotNumber(ps.shotId, ps.ordinal)),
                            QStringLiteral("Kinds opens with shot %1 marked.").arg(words::shotNumber(ps.shotId, ps.ordinal)),
                            { ps.ordinal }, QStringLiteral("kinds")));
    return m;
}

// ── the sequence (SeqTiming) ────────────────────────────────────────────────────────────────

RelationsSequence sequenceOf(const Ctx &x)
{
    const SessionSequence &q = x.c.seq;
    RelationsSequence m;
    if (x.n == 0) { m.available = false; return m; }
    m.available = true;
    const ShotSequence *ps = x.pick >= 0 ? &q.shots[size_t(x.pick)] : nullptr;
    Sentence cs;
    QStringList unplaced, late;
    if (ps) {
        std::vector<int> order;
        for (int s = 0; s < kSequenceSegments; ++s) {
            // the timed segments: placed, or the chest's probable peak (kinematic_sequence_design.md §12.6) with its ⚠
            if (ps->segments[size_t(s)].timed()) order.push_back(s);
            else unplaced << kSegLabels[s].toLower();
            if (ps->segments[size_t(s)].late) late << kSegLabels[s].toLower();
        }
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return ps->segments[size_t(a)].timeMs() < ps->segments[size_t(b)].timeMs(); });
        for (size_t k = 0; k < order.size(); ++k) {
            const SegmentPeak &p = ps->segments[size_t(order[k])];
            if (k) cs.append(QStringLiteral("  →  "));
            cs.append(kSegLabels[order[k]] + (p.probable ? QStringLiteral(" ⚠ ") : QStringLiteral(" "))).figure(words::signedMs(p.timeMs()));
            if (p.late) cs.append(QStringLiteral(", late"));
            RelationsChainLink link;
            link.segment = sequenceSegmentIds()[size_t(order[k])];
            link.label = kSegLabels[order[k]];
            link.probable = p.probable;
            link.shotMs = p.timeMs();
            link.msText = words::signedMs(p.timeMs());
            link.late = p.late;
            link.lateWord = p.late ? QStringLiteral("late") : QString();
            m.chain.append(link);
        }
    }
    if (cs.isEmpty()) cs.append(QStringLiteral("No segment placed on this shot."));
    m.chainSentence = sentenceOf(cs);
    const QString unplacedText = unplaced.isEmpty() ? QString() : words::list(unplaced) + QStringLiteral(" not placed");
    m.unplaced = unplacedText;
    int same = 0;
    const QString mine = ps ? ps->order.join(QLatin1Char(',')) : QString();
    for (const ShotSequence &s : q.shots) if (!mine.isEmpty() && s.order.join(QLatin1Char(',')) == mine) ++same;
    m.orderSame = same;
    m.orderCount = x.n;
    QStringList parts;
    if (ps && ps->timedCount >= 2)
        parts << (late.isEmpty() ? QStringLiteral("in sequence") : QStringLiteral("out of sequence: the ") + words::list(late) + QStringLiteral(" peaked late"));
    if (!unplacedText.isEmpty()) parts << unplacedText;
    if (!mine.isEmpty()) parts << QStringLiteral("this order on %1 of %2 shots").arg(same).arg(x.n);
    m.verdict = sentenceOf(Sentence().append(parts.isEmpty() ? QString() : words::cap1(parts.join(QStringLiteral("; "))) + QStringLiteral(".")));

    double lo = -150, hi = 40;
    for (const ShotSequence &s : q.shots) for (const SegmentPeak &p : s.segments) if (p.timed() && fin(p.timeMs())) { lo = std::min(lo, p.timeMs()); hi = std::max(hi, p.timeMs()); }
    lo = std::floor((lo - 10) / 25) * 25; hi = std::ceil((hi + 10) / 25) * 25;
    m.axis = rangeOf(lo, hi);
    const double step = (hi - lo) > 300 ? 100 : 50;
    for (double t = std::ceil(lo / step) * step; t <= hi + 1e-9; t += step) {
        RelationsSequenceTick tk;
        tk.ms = t;
        tk.text = t == 0 ? QStringLiteral("impact") : signedMsBare(t);
        m.ticks.append(tk);
    }
    for (int s = 0; s < kSequenceSegments; ++s) {
        std::vector<double> ms;
        RelationsSequenceRow row;
        for (const ShotSequence &sh : q.shots) {
            const SegmentPeak &p = sh.segments[size_t(s)];
            ms.push_back(p.timed() ? p.timeMs() : kNaN);
            row.lateShots.append(p.late);
        }
        const SegmentSummary &sm = q.segments[size_t(s)];
        const double v = ps && ps->segments[size_t(s)].timed() ? ps->segments[size_t(s)].timeMs() : kNaN;
        const int timedN = sm.placed + sm.probable;
        row.segment = sequenceSegmentIds()[size_t(s)];
        row.label = kSegLabels[s] + (sm.probable > 0 ? QStringLiteral(" ⚠") : QString());
        row.valueText = signedMsBare(v);
        row.shotMs = num(v);
        row.placed = timedN;
        row.placedText = sm.probable > 0 ? QStringLiteral("%1 of %2, probable").arg(timedN).arg(x.n)
                                         : QStringLiteral("%1 of %2").arg(sm.placed).arg(x.n);
        row.box = boxOf(boxStats(ms), [](double v) { return signedMsBare(v); });
        row.values = dbls(ms);
        row.hasBox = timedN >= 3;
        row.notPlacedText = timedN == 0 ? QStringLiteral("not placed on any shot")
                          : timedN < 3 ? QStringLiteral("placed on too few shots for a box") : QString();
        m.rows.append(row);
    }
    const SegmentSummary &pel = q.segments[0];
    Sentence note;
    if (pel.placed >= 3 && pel.afterImpact * 2 >= pel.placed)
        note.append(QStringLiteral("The pelvis peaks after impact on ")).figure(QString::number(pel.afterImpact))
            .append(QStringLiteral(" of ")).figure(QString::number(pel.placed)).append(QStringLiteral(" placed shots. ") + words::latePelvisNote());
    m.note = sentenceOf(note);
    return m;
}

// ── Relations ──────────────────────────────────────────────────────────────────────────────

RelationsRelations relationsOf(Ctx &x, const QString &upWanted, const QString &acrossWanted, const QString &facetWanted, QString *upOut)
{
    RelationsRelations m;
    Cache &c = x.c;
    // r needs five shots both readings were read on (session_relations.h pearson), so below five there is
    // nothing to relate: refused with the reason, never an empty ledger.
    if (x.n < 5 || c.readings.size() < 2) {
        m.available = false;
        m.refusal = sentenceOf(x.n < 5 ? tooFew(x, QStringLiteral("to relate readings across a session"), 5)
                                       : Sentence().append(QStringLiteral("This session has too few readings to relate.")));
        return m;
    }
    int y = c.indexOf(upWanted);
    if (y < 0) y = c.indexOf(QStringLiteral("clubheadSpeed@%1").arg(phase::Impact));
    if (y < 0) y = 0;
    const std::vector<CorrRow> &rows = c.corrOf(y);
    int xi = c.indexOf(acrossWanted);
    if (xi == y) xi = -1;
    if (xi < 0) for (const CorrRow &r : rows) if (!r.same) { xi = r.reading; break; }
    if (xi < 0) xi = y == 0 ? 1 : 0;
    const SessionReading &Y = x.r(y), &X = x.r(xi);
    if (upOut) *upOut = Y.id;
    m.available = true;
    m.upId = Y.id;
    m.acrossId = X.id;
    m.up = briefOf(x, y);
    m.across = briefOf(x, xi);
    m.upValues = dbls(Y.values);

    // the pickers' catalogue
    for (const QString &g : c.groupOrder) {
        RelationsCatalogueGroup group;
        group.name = g;
        for (size_t i = 0; i < c.readings.size(); ++i) if (c.meta[i].group == g) {
            const RelationsBrief b = briefOf(x, int(i));
            RelationsCatalogueReading cr;
            cr.readingId = b.readingId;
            cr.shortText = b.shortText;
            cr.tag = b.tag;
            cr.text = b.text;
            cr.label = b.label;
            group.readings.append(cr);
        }
        m.catalogue.append(group);
    }

    // the chance line, said out loud
    const int tests = int(rows.size());
    int clear = 0;
    for (const CorrRow &r : rows) if (r.c.clear) ++clear;
    const int ch = chanceCount(tests), nY = Y.box.n;
    const double rc = rCritical(nY);
    QString upName = lowerLabel(Y.label);
    m.head = sentenceOf(Sentence().append(QStringLiteral("What moves with ") + upName));
    m.shortText = sentenceOf(Sentence().append(words::cap1(words::word(clear)) + QStringLiteral(" of ")).figure(QString::number(tests))
                                 .append(QStringLiteral(" readings clear chance with ") + x.readingText(y) + QStringLiteral("; chance alone would clear about ")
                                         + words::word(ch) + QStringLiteral(".")));
    Sentence sf;
    sf.append(words::cap1(words::word(clear)) + QStringLiteral(" of ")).figure(QString::number(tests))
      .append(QStringLiteral(" readings moved with ") + x.readingText(y) + QStringLiteral(" closely enough for the 95\u00A0% interval to clear zero"))
      .append(clear <= ch + 1 ? QStringLiteral("; chance alone would clear about ") + words::word(ch) + QStringLiteral(", so read them as leads to test, not findings. ")
                              : QStringLiteral("; chance alone would clear about ") + words::word(ch) + QStringLiteral(". "))
      .append(QStringLiteral("With ") + words::word(nY) + QStringLiteral(" shots an r inside ±")).figure(figR(rc))
      .append(QStringLiteral(" is what chance gives. ") + words::coOccurrenceNote());
    m.standfirst = sentenceOf(sf);
    m.chance.n = nY;
    m.chance.tests = tests;
    m.chance.clearCount = clear;
    m.chance.chanceCount = ch;
    m.chance.rCritical = rc;
    m.chance.rCriticalText = figR(rc);
    m.chance.line = sentenceOf(Sentence().append(QStringLiteral("|r| under ")).figure(figR(rc)).append(QStringLiteral(" is chance at n = ")).figure(QString::number(nY)));
    m.chance.sentence = sentenceOf(Sentence().figure(QString::number(clear)).append(QStringLiteral(" of ")).figure(QString::number(tests))
                                       .append(QStringLiteral(" readings clear chance; chance alone would clear about ")).figure(QString::number(ch))
                                       .append(QStringLiteral(". ") + words::coOccurrenceNote()));

    m.scatter = scatterOf(x, X, Y, {}, {});
    m.upRun = runOf(x, Y);

    // the ledger, ranked by |r|; a same-quantity row is labelled and not ranked as a finding
    int rank = 0;
    for (const CorrRow &r : rows) m.ledger.append(ledgerRowOf(x, r, Y, r.same ? 0 : ++rank));
    int restClear = 0;
    for (size_t i = 10; i < rows.size(); ++i) if (rows[i].c.clear) ++restClear;
    m.restText = rows.size() > 10 ? QStringLiteral("The other %1 readings, %2").arg(rows.size() - 10)
                                        .arg(restClear ? QStringLiteral("%1 of them clearing chance").arg(restClear) : QStringLiteral("all inside chance"))
                                  : QString();
    m.ledgerNote = sentenceOf(Sentence().append(QStringLiteral("r: how closely the two moved together across the shots both were read on, from −1 to 1. "
                                                               "The interval is 95\u00A0% (Fisher); one that crosses the centre line could be no relation at all. With "))
                                  .figure(QString::number(nY)).append(QStringLiteral(" shots an r inside ±")).figure(figR(rc))
                                  .append(QStringLiteral(" is what chance gives. ‡ The same quantity measured another way, so the pair says little and is not ranked. ")
                                          + words::coOccurrenceNote()));

    // facets offered
    const auto facetChoice = [](const QString &id, const QString &kind, const QString &name, const QString &label, int n) {
        RelationsFacetChoice f;
        f.id = id;
        f.kind = kind;
        f.name = name;
        f.label = label;
        f.n = n;
        return f;
    };
    const std::vector<SessionFacet> cf = conditionFacets(x.S);
    for (size_t i = 0; i < cf.size() && i < 3; ++i) {
        const QString name = x.S.conditions.value(cf[i].id).name.isEmpty() ? cf[i].id : x.S.conditions.value(cf[i].id).name;
        m.facets.append(facetChoice(cf[i].id, QStringLiteral("condition"), name, lowerLabel(name) + QLatin1Char(' ') + QString::number(cf[i].firedCount),
                                    cf[i].firedCount));
    }
    m.facets.append(facetChoice(QStringLiteral("half"), QStringLiteral("half"), QStringLiteral("Session half"), QStringLiteral("session half"), 0));
    const std::optional<SessionFacet> club = clubFacet(x.S);
    if (club) m.facets.append(facetChoice(QStringLiteral("club"), QStringLiteral("club"), QStringLiteral("Club"), QStringLiteral("club"), 0));

    // the chosen facet: the same pair as small multiples, each panel its own r (null when none is chosen, or it has no panels)
    if (!facetWanted.isEmpty()) {
        std::vector<std::pair<QString, std::vector<int>>> panels;
        std::vector<uint8_t> mask(size_t(x.n), 0);
        QString kind, name;
        Sentence fs;
        if (facetWanted == QLatin1String("half")) {
            const SessionFacet h = halfFacet(x.S);
            kind = QStringLiteral("half"); name = QStringLiteral("Session half");
            for (const FacetGroup &g : h.groups)
                if (!g.ordinals.empty())
                    panels.push_back({ QStringLiteral("Shots %1 to %2").arg(x.no(g.ordinals.front())).arg(x.no(g.ordinals.back())), g.ordinals });
            fs.append(QStringLiteral("The first and second halves of the session. "));
        } else if (facetWanted == QLatin1String("club") && club) {
            kind = QStringLiteral("club"); name = QStringLiteral("Club");
            for (const FacetGroup &g : club->groups) panels.push_back({ (g.key.isEmpty() ? QStringLiteral("No club") : g.key) + QStringLiteral(", %1 shots").arg(g.ordinals.size()), g.ordinals });
            fs.append(QStringLiteral("One panel per club. "));
        } else {
            for (const SessionFacet &f : cf) if (f.id == facetWanted) {
                kind = QStringLiteral("condition");
                name = x.S.conditions.value(f.id).name.isEmpty() ? f.id : x.S.conditions.value(f.id).name;
                for (int o : f.groups[0].ordinals) mask[size_t(o - 1)] = 1;
                panels.push_back({ name + QStringLiteral(", fired on %1").arg(f.groups[0].ordinals.size()), f.groups[0].ordinals });
                panels.push_back({ QStringLiteral("Clean on %1").arg(f.groups[1].ordinals.size()), f.groups[1].ordinals });
                fs.append(name + QStringLiteral(": fired on ")).figure(QString::number(f.groups[0].ordinals.size()))
                  .append(QStringLiteral(" / clean on ")).figure(QString::number(f.groups[1].ordinals.size())).append(QStringLiteral(". "));
            }
        }
        if (!panels.empty()) {
            RelationsFacet facet;
            for (size_t k = 0; k < panels.size(); ++k) {
                RelationsFacetPanel pl;
                pl.title = panels[k].first;
                pl.n = int(panels[k].second.size());
                pl.ordinals = intList(panels[k].second);
                pl.fired = kind == QLatin1String("condition") && k == 0;
                pl.scatter = scatterOf(x, X, Y, panels[k].second, mask);
                facet.panels.append(pl);
            }
            fs.append(words::coOccurrenceNote());
            facet.id = facetWanted;
            facet.kind = kind;
            facet.name = name;
            facet.sentence = sentenceOf(fs);
            m.facet = QVariant::fromValue(facet);
        }
    }

    // the family matrix: Up's family, lower triangle
    std::vector<int> mids;
    for (size_t i = 0; i < c.readings.size() && mids.size() < 9; ++i) if (c.meta[i].group == c.meta[size_t(y)].group) mids.push_back(int(i));
    for (size_t a = 0; a < mids.size(); ++a) {
        m.matrix.readings.append(briefOf(x, mids[a]));
        QList<RelationsMatrixCell> row;
        for (size_t b = 0; b < a; ++b) {
            const Correlation p = pairOf(x.r(mids[a]), x.r(mids[b]));
            const QString ya = x.r(mids[a]).id, xb = x.r(mids[b]).id;
            RelationsMatrixCell cell;
            cell.defined = p.defined;
            cell.r = num(p.r);
            cell.rText = figR(p.r);
            cell.clear = p.clear;
            cell.xId = xb;
            cell.yId = ya;
            cell.on = (ya == Y.id && xb == X.id) || (ya == X.id && xb == Y.id);
            row.append(cell);
        }
        m.matrix.cells.append(row);
    }
    m.matrix.group = c.meta[size_t(y)].group;

    // drift: the drifts that clear chance, strongest first
    int dclear = 0;
    for (const CorrRow &d : c.drifts) {
        if (!d.c.clear) continue;
        ++dclear;
        const SessionReading &r = x.r(d.reading);
        const QString dir = d.c.r > 0 ? QStringLiteral("rising") : QStringLiteral("falling");
        Sentence s;
        s.append(words::cap1(x.readingText(d.reading)) + QStringLiteral(", r ")).figure(figR(d.c.r)).append(QStringLiteral(", ") + dir + QStringLiteral(" through the session"));
        RelationsDriftItem item;
        item.readingId = r.id;
        item.label = r.label;
        item.tag = x.v.tag(r.phase);
        item.r = d.c.r;
        item.rText = figR(d.c.r);
        item.lo = d.c.lo;
        item.hi = d.c.hi;
        item.ciText = QStringLiteral("(%1 to %2)").arg(figR(d.c.lo), figR(d.c.hi));
        item.clear = true;
        item.direction = dir;
        item.sentence = sentenceOf(s);
        item.run = runOf(x, r);
        m.drifts.items.append(item);
    }
    const int dtests = int(c.drifts.size()), dch = chanceCount(dtests);
    m.drifts.clearCount = dclear;
    m.drifts.tests = dtests;
    m.drifts.sentence = sentenceOf(Sentence().append(words::cap1(words::word(dclear)) + QStringLiteral(" of ")).figure(QString::number(dtests))
                                       .append(QStringLiteral(" readings ran with shot order closely enough to clear chance; chance alone would clear about ")
                                               + words::word(dch) + QStringLiteral(". ") + words::coOccurrenceNote()));

    // questions (Analyse.qml sessionAsks)
    const Correlation f = pairOf(X, Y);
    if (f.defined) {
        std::vector<std::pair<double, int>> res;
        for (int i = 0; i < x.n; ++i)
            if (fin(X.values[size_t(i)]) && fin(Y.values[size_t(i)]))
                res.push_back({ Y.values[size_t(i)] - (f.intercept + f.slope * X.values[size_t(i)]), i });
        std::stable_sort(res.begin(), res.end(), [](const auto &a, const auto &b) { return std::abs(a.first) > std::abs(b.first); });
        if (res.size() >= 2) {
            const std::vector<int> two{ res[0].second + 1, res[1].second + 1 };
            m.asks.append(askOf(QStringLiteral("line"), X.label + QStringLiteral(": which shots break the line?"),
                                words::cap1(words::shotsList(two)) + QStringLiteral(" sit furthest from it, ")
                                    + figU(std::abs(res[0].first), Y.unit) + QStringLiteral(" and ") + figU(std::abs(res[1].first), Y.unit) + QStringLiteral(" off the line."),
                                two));
        }
    }
    const Correlation d = driftOf(Y);
    if (d.defined) {
        const int third = std::max(1, x.n / 3);
        std::vector<int> lit;
        for (int i = 1; i <= third; ++i) lit.push_back(i);
        for (int i = x.n - third + 1; i <= x.n; ++i) lit.push_back(i);
        m.asks.append(askOf(QStringLiteral("drift"), Y.label + QStringLiteral(": is it drifting through the session?"),
                            (d.clear ? (d.r > 0 ? QStringLiteral("Rising: ") : QStringLiteral("Falling: "))
                                     : QStringLiteral("Not that %1 shots can show: ").arg(words::word(d.n)))
                                + QStringLiteral("r %1 (%2 to %3) with shot order. The first and last thirds are lit.").arg(figR(d.r), figR(d.lo), figR(d.hi)),
                            lit));
    }
    if (c.kinds.method != KindsMethod::None) {
        QStringList nm;
        if (c.kinds.method == KindsMethod::GapSplit) {
            const QString k = x.r(c.kinds.splitReading).key;
            nm = k.contains(QStringLiteral("peed")) ? QStringList{ QStringLiteral("Faster"), QStringLiteral("slower") } : QStringList{ QStringLiteral("Higher"), QStringLiteral("lower") };
        } else nm = QStringList{ QStringLiteral("Kind A"), QStringLiteral("kind B") };
        m.asks.append(askOf(QStringLiteral("kind"), nm[0] + QStringLiteral(" and ") + nm[1] + QStringLiteral(" swings: what separates them?"),
                            QStringLiteral("Kinds opens on them."), {}, QStringLiteral("kinds")));
    }
    return m;
}

// ── Kinds ──────────────────────────────────────────────────────────────────────────────────

QStringList kindNames(const Ctx &x)
{
    const SessionKinds &k = x.c.kinds;
    if (k.method == KindsMethod::GapSplit && k.splitReading >= 0) {
        const QString key = x.r(k.splitReading).key;
        if (key.contains(QStringLiteral("peed"))) return { QStringLiteral("Faster"), QStringLiteral("Slower") };
        return { QStringLiteral("Higher"), QStringLiteral("Lower") };
    }
    return { QStringLiteral("Kind A"), QStringLiteral("Kind B") };
}

double sparsity(const SessionSnapshot &S, const QString &key)
{
    size_t n = 0, bad = 0;
    for (const auto &sh : S.shots) {
        const MetricSeries *m = sh->metric(key);
        if (!m || m->tUs.empty()) continue;
        n += m->tUs.size();
        if (m->valid.size() >= m->tUs.size()) for (uint8_t v : m->valid) if (!v) ++bad;
    }
    return n ? double(bad) / double(n) : 0.0;
}

std::optional<RelationsKindCurve> kindCurveOf(Ctx &x, int ri, const QStringList &names)
{
    Cache &c = x.c;
    const SessionReading &r = x.r(ri);
    const SessionKinds &k = c.kinds;
    const SessionAlignment &al = c.align();
    const std::vector<TraceCurve> &cs = c.alignedOf(r.key);
    double lo = std::numeric_limits<double>::infinity(), hi = -lo;
    for (const TraceCurve &t : cs) if (!t.empty()) { lo = std::min(lo, t.t.front()); hi = std::max(hi, t.t.back()); }
    if (!fin(lo)) return std::nullopt;
    if (const CanonicalPhase *a = al.canonicalOf(phase::Address)) lo = std::max(lo, a->tMs - 150.0);
    if (const CanonicalPhase *f = al.canonicalOf(phase::Finish)) hi = std::min(hi, f->tMs + 150.0);
    if (!(hi > lo)) return std::nullopt;
    const std::vector<double> grid = sessionGrid(lo, hi);
    RelationsKindCurve out;
    double ylo = std::numeric_limits<double>::infinity(), yhi = -ylo;
    double dataLo = std::numeric_limits<double>::infinity(), dataHi = -dataLo;   // where any fit has a value
    const std::vector<int> *groups[2] = { &k.groupA, &k.groupB };
    for (int g = 0; g < 2; ++g) {
        // The group fitted is the kind's shots that HAVE the curve: a kind of six with the curve on two
        // is a two-shot group, and is refused as one (contract rule 3), not drawn blank.
        std::vector<int> contributing;
        for (int o : *groups[g]) if (!cs[size_t(o - 1)].empty()) contributing.push_back(o);
        const GroupFit f = pooledFit(cs, contributing, grid);
        const int n = int(groups[g]->size()), nc = int(contributing.size());
        RelationsKindFit fit;
        for (int o : *groups[g]) {
            const TraceCurve &t = cs[size_t(o - 1)];
            RelationsCover cover;
            for (const TimeSpan &s : coverageRuns(t)) cover.runs.append(QList<double>{ s.t0, s.t1 });
            int okN = 0;
            for (uint8_t v : t.ok) okN += v;
            cover.ordinal = x.ord(o);
            cover.picked = o - 1 == x.pick;
            cover.frac = t.t.empty() ? 0.0 : double(okN) / double(t.t.size());
            fit.cover.append(cover);
        }
        fit.nShots = intList(f.nShots);
        // the ±1 sd hairlines (D2: the pooled spread), and the scale that holds them
        std::vector<double> flo(f.v.size(), kNaN), fhi(f.v.size(), kNaN);
        int drawn = 0;
        for (size_t i = 0; i < f.v.size() && i < f.t.size(); ++i)
            if (fin(f.v[i])) { ++drawn; dataLo = std::min(dataLo, f.t[i]); dataHi = std::max(dataHi, f.t[i]); }
        for (size_t i = 0; i < f.v.size(); ++i)
            if (fin(f.v[i]) && fin(f.sd[i])) {
                flo[i] = f.v[i] - f.sd[i]; fhi[i] = f.v[i] + f.sd[i];
                ylo = std::min(ylo, flo[i]); yhi = std::max(yhi, fhi[i]);
            }
        const QString label = names[g] + QStringLiteral(", %1 shots").arg(n);
        QString refusal;
        if (f.refusal == FitRefusal::GroupTooSmall)
            refusal = nc == n ? QStringLiteral("Three shots are needed for a kind's curve; this kind has %1.").arg(words::word(n))
                              : QStringLiteral("Three shots are needed for a kind's curve; %1 of the %2 %3 shots have this curve.")
                                    .arg(nc == 0 ? QStringLiteral("none") : words::word(nc)).arg(words::word(n)).arg(names[g].toLower());
        else if (f.refusal == FitRefusal::NoSamples)
            refusal = QStringLiteral("No measured samples to fit.");
        fit.name = names[g];
        fit.n = n;
        fit.contributing = nc;
        fit.label = f.refused() ? label + QStringLiteral(": too few for a curve") : label;
        fit.refused = f.refused();
        fit.refusal = refusal;
        fit.t = dbls(f.t);
        fit.v = dbls(f.v);
        fit.sd = dbls(f.sd);
        fit.lo = dbls(flo);
        fit.hi = dbls(fhi);
        fit.drawn = drawn;
        out.fits.append(fit);
    }
    // THE WINDOW FOLLOWS THE FITS' FINITE VALUES, not the curves' extent: the plot spans where at least
    // one kind has a value, padded by 5 % of that span (at least 2 ms) each side; an end is stretched to a
    // canonical P-position lying within 20 ms beyond the finite span, so its tag is on the plot rather than
    // just off it. Clipped to the fitted grid. A curve with no finite value anywhere keeps its fits'
    // refusals and has no window (null) and no phases. Each fit's arrays are trimmed to the window.
    if (fin(dataLo) && fin(dataHi)) {
        const double pad = std::max(2.0, 0.05 * (dataHi - dataLo));
        double w0 = dataLo - pad, w1 = dataHi + pad;
        for (const CanonicalPhase &p : al.canonical) {
            if (p.tMs < w0 && p.tMs >= dataLo - 20.0) w0 = p.tMs;
            if (p.tMs > w1 && p.tMs <= dataHi + 20.0) w1 = p.tMs;
        }
        w0 = std::max(w0, lo); w1 = std::min(w1, hi);
        RelationsWindow window;
        window.t0 = w0;
        window.t1 = w1;
        out.window = QVariant::fromValue(window);
        for (const CanonicalPhase &p : al.canonical)
            if (p.tMs >= w0 - 1e-9 && p.tMs <= w1 + 1e-9) {
                RelationsKindPhase ph;
                ph.phase = p.phase;
                ph.tag = x.v.tag(p.phase);
                ph.tMs = p.tMs;
                ph.impact = p.phase == phase::Impact;     // the curve marks impact by its P-position, never its tag (PpChKindCurve)
                out.phases.append(ph);
            }
        for (RelationsKindFit &fit : out.fits) {
            qsizetype a = 0, b = fit.t.size();
            while (a < b && fit.t[a] < w0 - 1e-9) ++a;
            while (b > a && fit.t[b - 1] > w1 + 1e-9) --b;
            fit.t = fit.t.mid(a, b - a);
            fit.v = fit.v.mid(a, b - a);
            fit.sd = fit.sd.mid(a, b - a);
            fit.lo = fit.lo.mid(a, b - a);
            fit.hi = fit.hi.mid(a, b - a);
            fit.nShots = fit.nShots.mid(a, b - a);
        }
    }
    out.readingId = r.id;
    out.key = r.key;
    out.label = r.label;
    out.unit = r.unit;
    out.unitText = unitText(r.unit);
    out.yRange = fin(ylo) ? rangeOf(ylo - 0.05 * std::max(1e-6, yhi - ylo), yhi + 0.05 * std::max(1e-6, yhi - ylo)) : rangeOf(0, 1);
    return out;
}

Sentence alignNote(const Ctx &x, const SessionAlignment &al)
{
    std::vector<int> landmarks, tempo, shift, none;
    for (const ShotWarp &w : al.shots) {
        if (w.route == AlignRoute::Landmarks) landmarks.push_back(w.ordinal);
        else if (w.route == AlignRoute::TempoFitted) tempo.push_back(w.ordinal);
        else if (w.route == AlignRoute::ImpactShift) shift.push_back(w.ordinal);
        else if (w.route == AlignRoute::Unaligned) none.push_back(w.ordinal);
    }
    Q_UNUSED(x);
    Sentence s;
    s.append(QStringLiteral("Every shot's clock is warped so its P-positions land on the session's median ones; only time moves, never a value."));
    if (!landmarks.empty())
        s.append(QStringLiteral(" Only impact is known on ") + words::shotsList(landmarks)
                 + QStringLiteral(": they are lined up on landmarks found in their own curves."));
    if (!tempo.empty())
        s.append(QStringLiteral(" On ") + words::shotsList(tempo)
                 + QStringLiteral(" only impact is known: one tempo scale was fitted to the session's median curve, which can fold a timing difference into a shape difference."));
    if (!shift.empty())
        s.append(QStringLiteral(" On ") + words::shotsList(shift)
                 + QStringLiteral(" only impact is known: shots are lined up at impact, tempo differences stay in the curves."));
    if (!none.empty())
        s.append(QStringLiteral(" ") + words::cap1(words::shotsList(none)) + QStringLiteral(" had no impact and take no part."));
    return s;
}

RelationsKinds kindsCoreOf(Ctx &x, const Cache::Family *fam)
{
    RelationsKinds m;
    Cache &c = x.c;
    const SessionKinds &k = c.kinds;
    if (k.method == KindsMethod::None) {
        const Sentence why = x.n < 6 ? tooFew(x, QStringLiteral("to look for kinds of swing"), 6)
                                     : Sentence().append(QStringLiteral("No kinds and no clean split in this session."));
        m.available = false;
        m.method = QStringLiteral("none");
        m.head = sentenceOf(why);
        m.refusal = sentenceOf(why);
        return m;
    }
    const QStringList nm = kindNames(x);
    const bool km = k.method == KindsMethod::KMedoids;
    const int nA = int(k.groupA.size()), nB = int(k.groupB.size());
    // A shot the grouping could not place (the split reading is unmeasured on it) is said and listed,
    // never dropped silently.
    std::vector<int> missing;
    for (int o = 1; o <= x.n; ++o)
        if (std::find(k.groupA.begin(), k.groupA.end(), o) == k.groupA.end() && std::find(k.groupB.begin(), k.groupB.end(), o) == k.groupB.end())
            missing.push_back(o);
    const int nM = int(missing.size());
    const QString missingWord = km ? QStringLiteral("not grouped") : QStringLiteral("not measured");
    m.available = true;
    m.method = km ? QStringLiteral("kmedoids") : QStringLiteral("split");
    m.structure = km;
    m.silhouette = num(k.silhouette);
    m.silhouetteText = figR(k.silhouette);
    m.readingsUsed = k.readingsUsed;
    m.minSize = k.minSize;
    m.names = nm;

    // the head says how weak the structure is FIRST (D7)
    Sentence head, st, split;
    st.append(QStringLiteral("Taken on all ")).figure(QString::number(k.readingsUsed))
      .append(QStringLiteral(" readings at once, the ") + words::word(x.n) + QStringLiteral(" shots "));
    if (km) {
        head.append(QStringLiteral("Two kinds of swing: ") + words::word(nA) + QStringLiteral(" and ") + words::word(nB));
        st.append(QStringLiteral("fall into two kinds (silhouette ")).figure(figR(k.silhouette)).append(QStringLiteral("). "));
    } else {
        const SessionReading &r = x.r(k.splitReading);
        const QString gap = figU(k.splitHigh - k.splitLow, r.unit);
        head.append(QStringLiteral("No structure; one split: ") + words::word(nA) + QLatin1Char(' ') + nm[0].toLower()
                    + QStringLiteral(", ") + words::word(nB) + QLatin1Char(' ') + nm[1].toLower());
        st.append(QStringLiteral("do not fall into kinds (silhouette ")).figure(figR(k.silhouette))
          .append(QStringLiteral(", where ")).figure(QStringLiteral(".25")).append(QStringLiteral(" would be weak structure). "));
        split.append(words::cap1(aOrAn(gap)) + QLatin1Char(' ')).figure(gap)
             .append(QStringLiteral(" gap in ") + x.readingText(k.splitReading) + QStringLiteral(": ")).figure(QString::number(nA))
             .append(QLatin1Char(' ') + nm[0].toLower() + QStringLiteral(", ")).figure(QString::number(nB)).append(QLatin1Char(' ') + nm[1].toLower());
        st.append(QStringLiteral("The one clean break is a split, not kinds: ") + words::word(nA) + QStringLiteral(" shots at "))
          .figure(figU(k.splitHigh, r.unit)).append(QStringLiteral(" or more in ") + x.readingText(k.splitReading) + QStringLiteral(" and ") + words::word(nB) + QStringLiteral(" at "))
          .figure(figU(k.splitLow, r.unit)).append(QStringLiteral(" or less, ") + aOrAn(gap) + QLatin1Char(' ')).figure(gap).append(QStringLiteral(" gap no shot falls in. "));
        if (nM)
            st.append(words::cap1(words::word(nM)) + (nM == 1 ? QStringLiteral(" shot has no ") : QStringLiteral(" shots have no "))
                      + x.readingText(k.splitReading) + (nM == 1 ? QStringLiteral(" and sits in neither: ") : QStringLiteral(" and sit in neither: ")) + words::shotsList(x.ords(missing)) + QStringLiteral(". "));
    }
    if (nM) {
        head.append(QStringLiteral("; ") + words::word(nM) + QLatin1Char(' ') + missingWord);
        if (km) st.append(words::cap1(words::shotsList(x.ords(missing))) + QStringLiteral(" could not be grouped. "));
    }
    QString pickedKind;
    if (x.pick >= 0) {
        const int o = x.pick + 1;
        const bool inA = std::find(k.groupA.begin(), k.groupA.end(), o) != k.groupA.end();
        const bool inB = std::find(k.groupB.begin(), k.groupB.end(), o) != k.groupB.end();
        if (inA || inB) {
            pickedKind = inA ? nm[0] : nm[1];
            const QString ps = QStringLiteral("Shot %1 is one of the %2%3.").arg(x.no(o)).arg(words::word(inA ? nA : nB))
                               .arg(km ? QStringLiteral(" in ") + pickedKind : QLatin1Char(' ') + pickedKind.toLower());
            st.append(ps);
            m.pickedSentence = ps;
        } else if (std::find(missing.begin(), missing.end(), o) != missing.end()) {
            const QString ps = km ? QStringLiteral("Shot %1 could not be grouped.").arg(x.no(o))
                                  : QStringLiteral("Shot %1 has no %2, so it is in neither.").arg(x.no(o)).arg(x.readingText(k.splitReading));
            st.append(ps);
            m.pickedSentence = ps;
        }
    }
    m.pickedKind = pickedKind;
    m.head = sentenceOf(head);
    m.standfirst = sentenceOf(st);
    m.splitLine = sentenceOf(split);

    // members
    const auto membersOf = [&](const std::vector<int> &ordinals) {
        QList<RelationsMember> out;
        for (int o : ordinals) { RelationsMember mk = x.shotMark<RelationsMember>(o - 1); mk.picked = o - 1 == x.pick; out.append(mk); }
        return out;
    };
    const std::vector<int> *gs[2] = { &k.groupA, &k.groupB };
    const int meds[2] = { k.medoidA, k.medoidB };
    for (int g = 0; g < 2; ++g) {
        RelationsKindGroup group;
        group.name = nm[g];
        group.n = int(gs[g]->size());
        group.members = membersOf(*gs[g]);
        group.medoid = x.ord(meds[g]);
        group.medoidText = QStringLiteral("most typical: shot %1").arg(x.no(meds[g]));
        group.neutral = false;
        m.groups.append(group);
    }
    if (nM) {
        RelationsKindGroup group;
        group.name = words::cap1(missingWord);
        group.n = nM;
        group.members = membersOf(missing);
        group.medoid = 0;
        group.neutral = true;
        group.text = missingWord + QStringLiteral(": ") + words::shotsList(x.ords(missing));
        m.groups.append(group);
    }
    m.unplacedCount = nM;

    // the separation table
    const QString splitKey = k.splitReading >= 0 ? x.r(k.splitReading).key : QString();
    for (const KindSeparation &s : k.separations) {
        if (m.separation.rows.size() >= 8) break;
        if (!km && s.reading == k.splitReading) continue;
        const SessionReading &r = x.r(s.reading);
        std::vector<double> va(size_t(x.n), kNaN), vb(size_t(x.n), kNaN);
        for (int o : k.groupA) va[size_t(o - 1)] = r.values[size_t(o - 1)];
        for (int o : k.groupB) vb[size_t(o - 1)] = r.values[size_t(o - 1)];
        const bool same = !km && sameQuantityAs(r.key, splitKey);
        RelationsSeparationRow row;
        row.readingId = r.id;
        row.key = r.key;
        row.label = r.label;
        row.tag = x.v.tag(r.phase);
        row.same = same;
        row.sameMark = same ? QStringLiteral("‡") : QString();
        row.d = s.d;
        row.dText = dText(s.d);
        row.clear = std::abs(s.d) >= k.dCritical;
        row.medAText = figBare(s.medA, r.unit);
        row.medBText = figBare(s.medB, r.unit);
        row.unitText = unitText(r.unit);
        row.scale = paddedRange(r.box, 0.06);
        row.boxA = boxOf(boxStats(va), figOf(r.unit));
        row.boxB = boxOf(boxStats(vb), figOf(r.unit));
        row.valuesA = dbls(va);
        row.valuesB = dbls(vb);
        m.separation.rows.append(row);
    }
    m.separation.dCritical = k.dCritical;
    m.separation.dCriticalText = words::fig(k.dCritical, 1);
    m.separation.note = sentenceOf(Sentence().append(QStringLiteral("d: the gap between the kinds' means in their pooled standard deviation. With ")
                                                     + words::word(nA) + QStringLiteral(" and ") + words::word(nB) + QStringLiteral(" shots, a d under about "))
                                       .figure(words::fig(k.dCritical, 1))
                                       .append(QStringLiteral(" is within what chance gives, and a reading that clears it is still one of "))
                                       .figure(QString::number(k.separations.size()))
                                       .append(QStringLiteral(" tested. ‡ The same quantity measured another way. ") + words::coOccurrenceNote()));

    // each kind's curve: the split reading, the best-separating curves, and the sparsest of the next
    std::vector<int> cand;
    QStringList keys;
    const auto consider = [&](int ri) {
        if (ri < 0) return;
        const SessionReading &r = x.r(ri);
        if (keys.contains(r.key) || !x.m(ri).drawsCurve) return;
        bool any = false;
        for (const auto &sh : x.S.shots) if (const MetricSeries *ms = sh->metric(r.key)) if (ms->hasCurve()) { any = true; break; }
        if (!any) return;
        keys << r.key; cand.push_back(ri);
    };
    if (!km) consider(k.splitReading);
    for (const KindSeparation &s : k.separations) if (std::abs(s.d) >= 0.6) consider(s.reading);
    std::vector<int> chosen(cand.begin(), cand.begin() + std::min<size_t>(2, cand.size()));
    int sparse = -1; double sf = 0.1;
    for (size_t i = 2; i < cand.size() && i < 14; ++i) { const double f = sparsity(x.S, x.r(cand[i]).key); if (f > sf) { sf = f; sparse = cand[i]; } }
    if (sparse >= 0) chosen.push_back(sparse);
    for (int ri : chosen) {
        const QString key = x.r(ri).key;
        auto it = c.kindCurves.find(key);
        if (it == c.kindCurves.end()) it = c.kindCurves.insert(key, kindCurveOf(x, ri, nm));
        if (!it.value()) continue;
        RelationsKindCurve cm = *it.value();
        // the picked flag on coverage rows follows the picked shot, which the cache does not know
        for (RelationsKindFit &fit : cm.fits)
            for (RelationsCover &cover : fit.cover) cover.picked = x.pick >= 0 && cover.ordinal == x.ord(x.pick + 1);
        m.curves.append(cm);
    }
    m.alignNote = sentenceOf(alignNote(x, c.align()));

    // A's curve-shape groups, a second grouping
    if (fam) {
        const QString fk = fam->name;
        auto it = c.shapes.find(fk);
        if (it == c.shapes.end()) {
            QStringList skeys;
            for (int i : fam->readings) {
                const QString key = x.r(i).key;
                if (skeys.contains(key) || !x.m(i).drawsCurve) continue;
                bool any = false;
                for (const auto &sh : x.S.shots) if (const MetricSeries *ms = sh->metric(key)) if (ms->hasCurve()) { any = true; break; }
                if (any) skeys << key;
            }
            RelationsShapes sm;
            const SessionAlignment &al = c.align();
            const CanonicalPhase *t0 = al.canonicalOf(phase::Top), *t1 = al.canonicalOf(phase::P8);
            if (!skeys.isEmpty() && t0 && t1) {
                std::vector<std::vector<TraceCurve>> per;
                for (const QString &key : skeys) per.push_back(c.alignedOf(key));
                const ShapeSplit sp = shapeSplit(per, t0->tMs, t1->tMs);
                if (sp.found) {
                    const auto g = [&](int med, const std::vector<int> &ords) {
                        RelationsShapeGroup group;
                        group.name = QStringLiteral("shaped like shot %1").arg(x.no(med));
                        group.label = QStringLiteral("shaped like shot %1 · %2").arg(x.no(med)).arg(ords.size());
                        group.medoid = med;
                        group.n = int(ords.size());
                        group.ordinals = intList(ords);
                        return group;
                    };
                    sm.found = true;
                    sm.groups = { g(sp.medoidA, sp.groupA), g(sp.medoidB, sp.groupB) };
                    sm.sentence = sentenceOf(Sentence().append(QStringLiteral("By the shape of their ") + lowerLabel(fk)
                                                               + QStringLiteral(" curves from P4 to P8, a second grouping: shaped like shot %1 (%2) and like shot %3 (%4).")
                                                                     .arg(x.no(sp.medoidA)).arg(sp.groupA.size()).arg(x.no(sp.medoidB)).arg(sp.groupB.size())));
                }
            }
            it = c.shapes.insert(fk, sm);
        }
        m.shapes = it.value();
    }
    m.fitNote = QStringLiteral("Each kind's curve is fitted to the pooled valid samples of every shot in it (a local straight line, "
                               "weighted, over 20 ms either side), on a clock that lines the shots' P-positions up on the session's median "
                               "ones; the hairlines either side are the pooled samples' spread, one standard deviation. Under it, each "
                               "shot's own measured stretches: a gap is where that shot was bridged or not sampled, and its kind's other "
                               "shots carry the curve there. The curve is the kind's, not any shot's, and no shot's number is read from "
                               "it. It breaks where fewer than three shots contribute, and a kind of fewer than three has none.");
    m.methodNote = QStringLiteral("How the kinds were found: every reading all %1 shots have, set on a common scale (median and "
                                  "middle half, held to ±2.5), the shots grouped by k-medoids into two of at least %2. When that "
                                  "grouping is no better than chance would make it, the single widest gap in any one reading is used "
                                  "instead, and the sentence above says which. %3 shots can suggest kinds; they cannot confirm them.")
                       .arg(x.n).arg(k.minSize).arg(words::cap1(words::word(x.n)));
    return m;
}

// KINDS THAT ARE THE CLUBS. In a mixed session the strongest separation is usually the club: a driver and a
// 7 iron differ in nearly every reading. When the kinds' membership agrees with the club labels — each kind
// has a majority club, the two majorities differ, and at least 75 % of the grouped shots sit with their
// kind's majority club — the kinds are said to be the clubs, not presented as kinds of swing: "The two
// kinds are the two clubs: 7 iron and driver" from 90 % agreement, "… are mostly the two clubs …" from 75 %
// (corpus club labels are not always right — 4 Aug LM: 21 of 25, three shots labelled against their
// numbers — and a two-way split of two clubs agrees by chance at roughly the larger club's share, so 75 %
// on a near-even session is not chance). Kinds are then looked for within the largest club alone when it
// has the six shots kinds need (`withinClub`); otherwise the panel says kinds across clubs are not meaningful.
constexpr double kClubAgreement = 0.75, kClubAgreementClean = 0.90;

RelationsKinds kindsOf(Ctx &x, const Cache::Family *fam, bool nested = false);

RelationsKinds clubKindsOf(Ctx &x, const SessionFacet &clubs, const QString &cA, const QString &cB, int agree, int grouped)
{
    Cache &c = x.c;
    const SessionKinds &k = c.kinds;
    RelationsKinds m;
    m.available = true;
    m.method = QStringLiteral("clubs");
    m.structure = false;
    m.silhouette = num(k.silhouette);
    m.silhouetteText = figR(k.silhouette);
    m.readingsUsed = k.readingsUsed;
    m.minSize = k.minSize;
    m.names = QStringList{ cA.toLower(), cB.toLower() };
    m.clubAgreement.agree = agree;
    m.clubAgreement.grouped = grouped;
    m.head = sentenceOf(Sentence().append((agree >= kClubAgreementClean * grouped ? QStringLiteral("The two kinds are the two clubs: ")
                                                                                  : QStringLiteral("The two kinds are mostly the two clubs: "))
                                          + cA.toLower() + QStringLiteral(" and ") + cB.toLower()));

    // within the largest club, when it has the shots kinds need
    const FacetGroup *big = nullptr;
    for (const FacetGroup &g : clubs.groups) if (!big || g.ordinals.size() > big->ordinals.size()) big = &g;
    Sentence within;
    if (big && big->ordinals.size() >= 6) {
        if (!c.clubBase || c.clubName != big->key) {
            auto sub = std::make_shared<SessionSnapshot>();
            sub->sessionDir = x.S.sessionDir; sub->conditions = x.S.conditions; sub->revision = x.S.revision;
            for (int o : big->ordinals) sub->shots.push_back(x.S.shots[size_t(o - 1)]);
            c.clubBase = sub; c.clubName = big->key; c.clubCache = std::make_shared<Cache>();
        }
        const QString pd = x.pick >= 0 ? cleanDir(x.S.shots[size_t(x.pick)]->swingDir) : QString();
        ensureSession(*c.clubCache, c.clubBase, pd, {}, c.readAt, x.readAtGen);
        Ctx sx{ *c.clubCache, *c.clubCache->snap, x.v };
        sx.n = sx.S.count(); sx.readAtGen = x.readAtGen;
        for (int i = 0; i < sx.n; ++i) if (!pd.isEmpty() && cleanDir(sx.S.shots[size_t(i)]->swingDir) == pd) { sx.pick = i; break; }
        const RelationsKinds inner = kindsOf(sx, nullptr, true);
        QString innerHead = inner.head.text;
        if (!innerHead.isEmpty() && innerHead[0].isUpper() && !innerHead.startsWith(QStringLiteral("Six"))) innerHead[0] = innerHead[0].toLower();
        if (!innerHead.endsWith(QLatin1Char('.'))) innerHead += QLatin1Char('.');
        within.append(QStringLiteral("Within the ") + words::word(int(big->ordinals.size())) + QLatin1Char(' ') + big->key.toLower()
                      + QStringLiteral(" shots alone: ") + innerHead);
        RelationsWithinClub wc;
        wc.club = big->key.toLower();
        wc.n = int(big->ordinals.size());
        wc.sentence = sentenceOf(within);
        wc.kinds = inner;
        m.withinClub = QVariant::fromValue(wc);
    } else {
        within.append(QStringLiteral("Neither club has the six shots kinds need, so this session cannot be split into kinds of swing."));
    }

    Sentence st;
    st.append(QStringLiteral("Taken on all ")).figure(QString::number(k.readingsUsed))
      .append(QStringLiteral(" readings at once, the ") + words::word(x.n) + QStringLiteral(" shots fall into two groups"));
    if (k.method == KindsMethod::KMedoids) st.append(QStringLiteral(" (silhouette ")).figure(figR(k.silhouette)).append(QStringLiteral(")"));
    else if (k.splitReading >= 0) st.append(QStringLiteral(" split on ") + x.readingText(k.splitReading));
    st.append(QStringLiteral(", and ")).figure(QString::number(agree)).append(QStringLiteral(" of the ")).figure(QString::number(grouped))
      .append(QStringLiteral(" grouped shots sit with their own club: the groups say which club was hit, not what kinds of swing there were. "
                             "Kinds across clubs are not meaningful. "));
    for (const auto &r : within.runs) { if (r.fig) st.figure(r.text); else st.append(r.text); }
    m.standfirst = sentenceOf(st);

    // the groups ARE the clubs: shown as the clubs
    QString pickedClub;
    for (const FacetGroup &g : clubs.groups) {
        RelationsKindGroup group;
        for (int o : g.ordinals) {
            RelationsMember mk = x.shotMark<RelationsMember>(o - 1);
            mk.picked = o - 1 == x.pick;
            group.members.append(mk);
            if (o - 1 == x.pick) pickedClub = g.key.toLower();
        }
        group.name = g.key.isEmpty() ? QStringLiteral("No club") : g.key.toLower();
        group.n = int(g.ordinals.size());
        group.medoid = 0;
        group.neutral = false;
        m.groups.append(group);
    }
    m.unplacedCount = 0;
    m.pickedKind = pickedClub;
    // no separation, no curves, no shapes, no notes: each at its default (an empty list, `found` false, an empty Sentence)
    m.methodNote = QStringLiteral("The kinds are called the clubs when each kind has a majority club, the two majorities differ, "
                                  "and at least 75\u00A0% of the grouped shots sit with their kind's majority club "
                                  "(\u201Care the two clubs\u201D from 90\u00A0%, \u201Cmostly\u201D below that).");
    return m;
}

RelationsKinds kindsOf(Ctx &x, const Cache::Family *fam, bool nested)
{
    const std::optional<SessionFacet> clubs = clubFacet(x.S);
    const QString clubText = clubs ? QStringLiteral("mixed clubs") : (x.n ? x.S.shots.front()->club.toLower() : QString());
    const QString kicker = QStringLiteral("Kinds of swing  ·  ") + words::word(x.n) + (x.n == 1 ? QStringLiteral(" shot") : QStringLiteral(" shots"))
                           + (clubText.isEmpty() ? QString() : QStringLiteral("  ·  ") + clubText);
    std::optional<RelationsKinds> m;
    const SessionKinds &k = x.c.kinds;
    if (!nested && clubs && k.method != KindsMethod::None) {
        QHash<QString, int> ca, cb;
        for (int o : k.groupA) ca[x.S.shots[size_t(o - 1)]->club] += 1;
        for (int o : k.groupB) cb[x.S.shots[size_t(o - 1)]->club] += 1;
        const auto major = [](const QHash<QString, int> &h) {
            QString best; int n = -1;
            for (auto it = h.constBegin(); it != h.constEnd(); ++it) if (it.value() > n || (it.value() == n && it.key() < best)) { best = it.key(); n = it.value(); }
            return best;
        };
        const QString cA = major(ca), cB = major(cb);
        const int grouped = int(k.groupA.size() + k.groupB.size());
        const int agree = ca.value(cA) + cb.value(cB);
        // gate rule: PP_GATE("rule.charts.kindsAsClubs")
        if (!cA.isEmpty() && !cB.isEmpty() && cA != cB && grouped > 0 && agree >= kClubAgreement * grouped)
            m = clubKindsOf(x, *clubs, cA, cB, agree, grouped);
    }
    if (!m) {
        m = kindsCoreOf(x, fam);
        if (!nested && clubs && m->available) {
            const QString note = QStringLiteral("The clubs are mixed; these kinds are taken across them, and they are not the clubs.");
            m->clubsNote = note;
            RelationsWordRun run;
            run.text = QStringLiteral(" ") + note;
            m->standfirst.runs.append(run);
            m->standfirst.text += QStringLiteral(" ") + note;
            // the clause follows the sentence it now is (5.5b review m4: it was cut before the note was appended)
            m->standfirst.clause = words::firstClauseOf(m->standfirst.text);
        }
    }
    // `refusal` is an empty Sentence where the map had {} (available): PpChRoles.has() reads both as none
    m->kicker = kicker;
    return *m;
}

// ── Replay Session (D6): one entry per P-position the lead reading is read at ────────────────

std::vector<Output::ReplayEntry> replayEntries(Ctx &x, const Cache::Family *fam)
{
    std::vector<Output::ReplayEntry> out;
    if (!fam || x.n < 3 || x.pick < 0) return out;
    int li = -1;
    if (fam->name == kSeqFamily) li = x.c.indexOf(QStringLiteral("clubAngularSpeed@%1").arg(phase::Impact));
    if (li < 0) for (int i : fam->readings) if (x.m(i).drawsCurve && x.pickedHasCurve(x.r(i).key)) { li = i; break; }
    if (li < 0 && !fam->readings.empty()) li = fam->readings.front();
    if (li < 0) return out;
    const QString key = x.r(li).key;
    const ShotSeries &ps = *x.S.shots[size_t(x.pick)];

    // every phase the key is sampled at, in the order the session reaches them
    QHash<int, std::vector<double>> when;
    for (const auto &sh : x.S.shots)
        if (const MetricSeries *ms = sh->metric(key))
            for (const PhaseSample &p : ms->phaseSamples)
                if (fin(p.value) && sh->impactUs >= 0) { const qint64 us = phaseUsOf(*sh, p.phase); if (us >= 0) when[p.phase].push_back(double(us - sh->impactUs)); }
    std::vector<std::pair<double, int>> phs;
    for (auto it = when.constBegin(); it != when.constEnd(); ++it) phs.push_back({ medianOf(it.value()), it.key() });
    std::sort(phs.begin(), phs.end());

    std::vector<SessionReading> rs;
    for (const auto &p : phs) {
        const int ph = p.second;
        std::vector<SessionReading> one = sessionReadings(x.S, [ph](const QString &) { return std::vector<int>{ ph }; }, QStringList{ key });
        if (one.empty() || one.front().phase != ph) continue;
        one.front().unit = shortUnit(one.front().unit);
        rs.push_back(std::move(one.front()));
    }
    linkFired(x.S, rs, nullptr, nullptr);
    QList<RelationsReplayPhase> phaseLinks;
    for (const SessionReading &r : rs) {
        RelationsReplayPhase link;
        link.phase = r.phase;
        link.tag = x.v.tag(r.phase);
        link.seekUs = phaseUsOf(ps, r.phase);
        link.current = false;
        phaseLinks.append(link);
    }
    const double rc = rCritical(x.n);
    for (size_t e = 0; e < rs.size(); ++e) {
        const SessionReading &r = rs[e];
        RelationsReplaySession m;
        m.readingId = r.id;
        m.key = r.key;
        m.label = r.label;
        m.tag = x.v.tag(r.phase);
        m.phaseName = x.v.name(r.phase);
        m.head = sentenceOf(Sentence().append(r.label + QStringLiteral(" at ") + x.v.tag(r.phase) + QStringLiteral(", ")
                                              + x.v.name(r.phase).toLower() + QStringLiteral(", every shot")));
        m.follows = QStringLiteral("follows the video");
        m.phases = phaseLinks;
        for (RelationsReplayPhase &l : m.phases) l.current = l.phase == r.phase;
        m.run = runOf(x, r);
        const ReadingPlace p = placeOf(r, x.pick);
        Sentence now;
        if (p.measured) {
            // R11: the median is "median", never "typical" (that word is the typicality badge's)
            now.append(QStringLiteral("This shot ")).figure(figU(p.value, r.unit)).append(QStringLiteral("; median ")).figure(figU(r.box.med, r.unit));
            const QString pl = placeWords(p, r.box.n);
            if (!pl.isEmpty()) now.append(QStringLiteral("; ") + pl);
            now.append(QStringLiteral("."));
        }
        // no value on this shot: said plainly; why is said once, in the panel's tail (Mark, 17 Oct 2026)
        if (!p.measured) {
            now.append(QStringLiteral("Not measured on this shot; median "));
            now.figure(figU(r.box.med, r.unit)).append(QStringLiteral("."));
        }
        // R8: a technical Typicality badge beside "This shot …", when the window holds 8 (null otherwise)
        const QVariant typ = p.measured ? typicalityOf(x, r.values, r.key, r.unit, corridorOf(x, r)) : QVariant();
        m.now.valueText = figU(p.measured ? p.value : kNaN, r.unit);
        if (typ.isValid()) m.now.typicality = typ;
        m.now.medianText = figU(r.box.med, r.unit);
        m.now.place = placeWords(p, r.box.n);
        m.now.sentence = sentenceOf(now);
        // partners: every other key's reading against this one
        std::vector<CorrRow> partners;
        for (size_t i = 0; i < x.c.readings.size(); ++i) {
            if (x.c.readings[i].key == key) continue;
            const Correlation c = pearson(x.c.readings[i].values, r.values);
            if (c.defined) partners.push_back(CorrRow{ int(i), sameQuantityAs(x.c.readings[i].key, key), c });
        }
        std::stable_sort(partners.begin(), partners.end(), [](const CorrRow &a, const CorrRow &b) { return std::abs(a.c.r) > std::abs(b.c.r); });
        for (const CorrRow &pr : partners) if (!pr.same) {
            const SessionReading &pr_r = x.r(pr.reading);
            RelationsPartner partner;
            partner.readingId = pr_r.id;
            partner.label = pr_r.label;
            partner.tag = x.v.tag(pr_r.phase);
            partner.rText = figR(pr.c.r);
            partner.scatter = scatterOf(x, pr_r, r, {}, {});
            m.partner = QVariant::fromValue(partner);
            break;
        }
        for (size_t i = 0; i < partners.size() && i < 3; ++i) {
            const CorrRow &pr = partners[i];
            const SessionReading &q = x.r(pr.reading);
            Sentence s;
            s.append(q.label + QStringLiteral(" at ") + x.v.tag(q.phase) + (pr.same ? QStringLiteral(" ‡") : QString()) + QStringLiteral(": r "))
             .figure(figR(pr.c.r)).append(QStringLiteral(" (")).figure(figR(pr.c.lo)).append(QStringLiteral(" to ")).figure(figR(pr.c.hi))
             .append(QStringLiteral("), n ")).figure(QString::number(pr.c.n)).append(pr.c.clear ? QString() : QStringLiteral(", inside chance"));
            RelationsPartnerRow row;
            row.readingId = q.id;
            row.label = q.label;
            row.tag = x.v.tag(q.phase);
            row.same = pr.same;
            row.sameMark = pr.same ? QStringLiteral("‡") : QString();
            row.r = pr.c.r;
            row.rText = figR(pr.c.r);
            row.lo = pr.c.lo;
            row.hi = pr.c.hi;
            row.ciText = QStringLiteral("(%1 to %2)").arg(figR(pr.c.lo), figR(pr.c.hi));
            row.n = pr.c.n;
            row.clear = pr.c.clear;
            row.sentence = sentenceOf(s);
            m.movesWith.append(row);
        }
        m.note = sentenceOf(Sentence().append(QStringLiteral("With ")).figure(QString::number(x.n)).append(QStringLiteral(" shots an r inside ±"))
                                .figure(figR(rc)).append(QStringLiteral(" is what chance gives. ") + words::coOccurrenceNote()));
        Output::ReplayEntry re;
        const qint64 us = phaseUsOf(ps, r.phase);
        re.tUs = us >= 0 ? us : std::numeric_limits<qint64>::max();
        re.entry = m;
        out.push_back(std::move(re));
    }
    return out;
}

} // namespace

static std::shared_ptr<ChartsRelationsPresenter::Output> computeRelations(const std::shared_ptr<const SessionSnapshot> &base,
                                                                   const QString &pickedRaw, const QVariantMap &detail,
                                                                   const QString &reg, const QString &family, const QString &up,
                                                                   const QString &across, const QString &facet, const QString &open,
                                                                   const ChartsRelationsPresenter::ReadAt &readAt, int readAtGen,
                                                                   double faultZ, const pinpoint::analysis::GradePolicy &policy,
                                                                   const std::shared_ptr<const pinpoint::analysis::INormProvider> &norms,
                                                                   const ConsistencyEnv &env, ChartsRelationsPresenter::Cache &cache)
{
    QElapsedTimer timer;
    timer.start();
    auto out = std::make_shared<ChartsRelationsPresenter::Output>();
    const QString picked = cleanDir(pickedRaw);
    const auto empty = std::make_shared<SessionSnapshot>();
    ensureSession(cache, base ? base : empty, picked, detail, readAt, readAtGen);
    const SessionSnapshot &S = *cache.snap;
    Vocab vocab;
    Ctx x{ cache, S, vocab };
    x.n = S.count();
    x.readAtGen = readAtGen;
    x.faultZ = faultZ;
    x.norms = norms.get();
    x.policy = policy;
    x.env = &env;
    for (int i = 0; i < x.n; ++i) if (!picked.isEmpty() && cleanDir(S.shots[size_t(i)]->swingDir) == picked) { x.pick = i; break; }
    x.typicalOut = outOfTypicalityWindow(cache, S, x.pick);

    int pickIdx = -1;
    for (int i = 0; i < S.count(); ++i) if (!picked.isEmpty() && cleanDir(S.shots[size_t(i)]->swingDir) == picked) { pickIdx = i; break; }
    const int fi = familyIndex(cache, family, reg, cache.solo ? std::min(pickIdx, 0) : pickIdx);
    const ChartsRelationsPresenter::Cache::Family *fam = fi >= 0 ? &cache.families[size_t(fi)] : nullptr;

    // status
    RelationsStatus &st = out->status;
    const ShotSeries *ps = x.pick >= 0 ? S.shots[size_t(x.pick)].get() : nullptr;
    const std::optional<SessionFacet> club = clubFacet(S);
    QString clubText = ps ? ps->club.toLower() : (x.n ? S.shots.front()->club.toLower() : QString());
    st.ready = true;
    st.shotCount = x.n;
    st.pickedOrdinal = ps ? ps->ordinal : 0;
    st.pickedShotId = ps ? ps->shotId : -1;
    st.pickedSwingDir = ps ? ps->swingDir : QString();
    st.pickedInSession = ps && cache.augmentedDir.isEmpty();
    st.pickedFromDetail = ps && !cache.augmentedDir.isEmpty();
    st.pickedImpactUs = ps ? ps->impactUs : -1;
    st.club = club ? QStringLiteral("mixed clubs") : clubText;
    st.clubsMixed = bool(club);
    QString clubs;
    if (club) {
        QStringList parts;
        for (const FacetGroup &g : club->groups)
            parts << (g.key.isEmpty() ? QStringLiteral("no club") : g.key.toLower()) + QStringLiteral(" on %1").arg(words::word(int(g.ordinals.size())));
        clubs = QStringLiteral("The clubs are mixed: ") + words::list(parts) + QStringLiteral("; every shot is shown together unless split by club.");
    }
    st.clubsSentence = clubs;
    st.kicker = ps ? words::shotOfText(ps->shotId, ps->ordinal, x.n) + (clubText.isEmpty() || club ? QString() : QStringLiteral("  ·  ") + clubText)
                   : QStringLiteral("%1 shots").arg(x.n);
    st.family = fam ? fam->name : QString();
    st.readingCount = int(cache.readings.size());
    for (const auto &sh : S.shots) {
        RelationsShot shot;
        shot.ordinal = sh->ordinal;
        shot.shotId = sh->shotId;
        shot.swingDir = sh->swingDir;
        shot.club = sh->club;
        st.shots.append(shot);
    }
    // the profile's column heads (ProfileTable: "Shot 8" over the value, "15 shots, low to high" over the box)
    st.shotLabel = ps ? QStringLiteral("Shot %1").arg(words::shotNumber(ps->shotId, ps->ordinal)) : QString();
    st.countLabel = QStringLiteral("%1 shots, low to high").arg(x.n);
    st.countLabelShort = QStringLiteral("%1 shots").arg(x.n);
    st.refused = x.n < 3;
    if (x.n < 3) st.refusal = sentenceOf(tooFew(x, QStringLiteral("to draw the session"), 3));    // else an empty Sentence, the map's {}

    for (size_t i = 0; i < cache.families.size(); ++i) {
        const auto &f = cache.families[i];
        RelationsFamily fm;
        fm.name = f.name;
        fm.count = int(f.readings.size());
        fm.note = f.name == kSeqFamily ? QStringLiteral("peak times")
                                       : QStringLiteral("%1 %2").arg(f.readings.size()).arg(f.readings.size() == 1 ? QStringLiteral("reading") : QStringLiteral("readings"));
        fm.preset = f.preset;
        fm.current = int(i) == fi;
        fm.sequence = f.name == kSeqFamily;
        out->families.append(fm);
    }

    out->thisShot = thisShotOf(x, fam, out.get());
    // the open row: its curve on this shot's clock, or its run when it has none ("" readingId: none open)
    const int oi = cache.indexOf(open);
    if (oi >= 0 && x.n >= 3) {
        const RelationsReadingRow row = rowAt(x, oi);
        const bool trace = row.opens == QLatin1String("trace");
        std::optional<RelationsTrace> tr;
        if (trace) { tr = traceOf(x, x.r(oi), &out->openCurve); out->openUnit = x.r(oi).unit; }
        out->openRow.readingId = x.r(oi).id;
        out->openRow.opens = trace && tr ? QStringLiteral("trace") : QStringLiteral("run");
        if (tr) out->openRow.trace = QVariant::fromValue(*tr);
        out->openRow.run = runOf(x, x.r(oi));
    } else if (x.n >= 3) {
        const SessionReading r = curveOnlyReading(x, open);     // a row with no reading behind it: its curve, by key
        if (!r.id.isEmpty()) {
            const std::optional<RelationsTrace> tr = traceOf(x, r, &out->openCurve);
            out->openUnit = r.unit;
            out->openRow.readingId = r.id;
            out->openRow.opens = QStringLiteral("trace");
            // always a trace here: curveOnlyReading has found the picked shot's curve
            if (tr) out->openRow.trace = QVariant::fromValue(*tr);
            out->openRow.run = runOf(x, r);
        }
    }
    out->sequence = sequenceOf(x);
    out->relations = relationsOf(x, up, across, facet, nullptr);
    out->kinds = kindsOf(x, fam);
    out->replay = replayEntries(x, fam);
    if (ps) { out->pickedImpactUs = ps->impactUs; out->pickedPhases = ps->phases; }
    out->ready = true;
    out->jobMs = double(timer.nsecsElapsed()) / 1e6;
    return out;
}

// ── the rows the Consistency tab shares ───────────────────────────────────────────────────────

struct ConsistencyReadingRows::Impl {
    ChartsRelationsPresenter::Cache cache;
    std::shared_ptr<const SessionSnapshot> none = std::make_shared<SessionSnapshot>();
    std::shared_ptr<const pinpoint::analysis::INormProvider> norms;
    pa::GradePolicy policy;
    double faultZ = 3.0;
    int runs = 0;
    double ms = 0.0;

    // A job's context on one shot. Vocab is a QObject, so each call makes its own on the thread it runs on.
    template <typename F> auto with(int shot, F &&f)
    {
        const Vocab vocab;
        Ctx x{ cache, *cache.snap, vocab };
        x.n = cache.snap->count();
        x.pick = shot;
        x.faultZ = faultZ;
        x.norms = norms.get();
        x.policy = policy;
        return f(x);
    }
};

ConsistencyReadingRows::ConsistencyReadingRows() : m(std::make_unique<Impl>()) {}
ConsistencyReadingRows::~ConsistencyReadingRows() = default;

bool ConsistencyReadingRows::update(const std::shared_ptr<const SessionSnapshot> &snapshot, const ConsistencyEnv &env, double faultZ,
                                    const pinpoint::analysis::GradePolicy &policy,
                                    const std::shared_ptr<const pinpoint::analysis::INormProvider> &norms)
{
    ensureSession(m->cache, snapshot ? snapshot : m->none, QString(), QVariantMap{}, {}, 0, false);
    m->faultZ = faultZ;
    m->policy = policy;
    m->norms = norms;
    const bool fresh = !m->cache.consistencyRun;
    m->ms = m->cache.ensureConsistency(env);
    if (fresh) ++m->runs;
    return fresh;
}

int ConsistencyReadingRows::runs() const { return m->runs; }
double ConsistencyReadingRows::ms() const { return m->ms; }
const SessionSnapshot &ConsistencyReadingRows::session() const { return *m->cache.recorded; }
const ConsistencyInput &ConsistencyReadingRows::input() const { return m->cache.consistencyIn; }
const ConsistencyResult &ConsistencyReadingRows::result() const { return m->cache.consistencyOut; }
const std::vector<ConsistencyLmRow> &ConsistencyReadingRows::lmRows(int shot) const { return m->cache.lmRows[size_t(shot)]; }

RelationsReadingRow ConsistencyReadingRows::lmRow(int shot, const ConsistencyLmRow &row) const
{
    return m->with(shot, [&](const Ctx &x) { return lmRowOf(x, row).row; });
}

RelationsReadingRow ConsistencyReadingRows::standOutRow(int shot, const ConsistencyStandOut &standOut) const
{
    return m->with(shot, [&](const Ctx &x) { return standOutRowOf(x, standOut).row; });
}

RelationsReadingRow ConsistencyReadingRows::readingRow(int shot, const ConsistencyJudged &judged) const
{
    return m->with(shot, [&](const Ctx &x) {
        const ConsistencyReading &tested = x.c.consistencyIn.readings[size_t(judged.reading)];
        return testedRowOf(x, tested, judged.column, tested.samples / 2, false).row;
    });
}

QString ConsistencyReadingRows::cannotCompare(int shot, bool swingOnly) const
{
    return m->with(shot, [swingOnly](const Ctx &x) { return charts::cannotCompare(x, swingOnly); });
}

QString ConsistencyReadingRows::guardNote()
{
    return QStringLiteral("⚠ ") + words::cap1(consistencyGuardWords()) + QStringLiteral(", so comparisons across its spans are not counted.");
}

// ── the QObject ───────────────────────────────────────────────────────────────────────────

ChartsRelationsPresenter::ChartsRelationsPresenter(QObject *parent)
    : QObject(parent), m_cache(std::make_shared<Cache>())
{
    m_normsGeneration = consistencyEnvGeneration();   // read first, as the consistency presenter
    m_consistencyEnv = consistencyEnvFromPacks();     // on the GUI thread, for the worker to hold by value
}

ChartsRelationsPresenter::~ChartsRelationsPresenter()
{
    // No wait: a job still running finishes into nothing (charts_work.h: the lane is cancelled, #173).
}

#define PP_SET(member, value, sig, recompute) \
    do { if (member == value) return; member = value; emit sig(); if (recompute) schedule(); } while (0)

void ChartsRelationsPresenter::setModel(SeriesSource *m)
{
    if (m_model == m) return;
    if (m_model) disconnect(m_model, nullptr, this, nullptr);
    m_model = m;
    if (m)
        connect(m, &SeriesSource::snapshotChanged, this, &ChartsRelationsPresenter::onSnapshotChanged);
    emit modelChanged();
    schedule();
}
void ChartsRelationsPresenter::setPickedSwingDir(const QString &d) { PP_SET(m_picked, d, pickedSwingDirChanged, true); }
void ChartsRelationsPresenter::setRegisterMode(const QString &r) { PP_SET(m_register, r, registerChanged, true); }
void ChartsRelationsPresenter::setFamily(const QString &f) { PP_SET(m_family, f, familyChanged, true); }
void ChartsRelationsPresenter::setUpId(const QString &id) { PP_SET(m_upId, id, upIdChanged, true); }
void ChartsRelationsPresenter::setAcrossId(const QString &id) { PP_SET(m_acrossId, id, acrossIdChanged, true); }
void ChartsRelationsPresenter::setFacetId(const QString &id) { PP_SET(m_facetId, id, facetIdChanged, true); }
void ChartsRelationsPresenter::setOpenId(const QString &id) { PP_SET(m_openId, id, openIdChanged, true); }
void ChartsRelationsPresenter::setGradePolicy(const QString &p) { PP_SET(m_gradePolicy, p, gradePolicyChanged, true); }
void ChartsRelationsPresenter::setPickedDetail(const QVariantMap &d)
{
    if (m_pickedDetail == d) return;
    m_pickedDetail = d;
    emit pickedDetailChanged();
    schedule();
}
void ChartsRelationsPresenter::setPlayheadUs(qint64 us)
{
    if (m_playheadUs == us) return;
    m_playheadUs = us;
    emit playheadUsChanged();
    updatePlayhead(false);
}
#undef PP_SET

void ChartsRelationsPresenter::setSnapshot(std::shared_ptr<const SessionSnapshot> s)
{
    m_directSnapshot = std::move(s);
    schedule();
}

void ChartsRelationsPresenter::setReadAt(ReadAt readAt)
{
    m_readAt = std::move(readAt);
    ++m_generation;        // the readings' identity changes with it
    schedule();
}

void ChartsRelationsPresenter::onSnapshotChanged() { schedule(); }

std::shared_ptr<const SessionSnapshot> ChartsRelationsPresenter::currentSnapshot() const
{
    return chosenSnapshot(m_directSnapshot, m_model.data());   // P2: a direct snapshot wins
}

void ChartsRelationsPresenter::start()
{
    // THE ONE SCHEDULER (charts_presenter_core.h, BusyFromAsk): coalesced, busy from the ask (P11); synchronous at once.
    m_sched.begin();
    // A corridor saved in the Diagnostic Model since the last job: the env and the cache start again, as a new
    // presenter's would, so this job reads ONE norm set, the new (ChartsConsistencyPresenter::start, the same rule).
    if (m_normsGeneration != consistencyEnvGeneration()) {
        m_normsGeneration = consistencyEnvGeneration();
        m_consistencyEnv = consistencyEnvFromPacks();
        m_cache = std::make_shared<Cache>();
    }
    const auto snap = currentSnapshot();
    const QString picked = m_picked, reg = m_register, family = m_family, up = m_upId, across = m_acrossId, facet = m_facetId, open = m_openId;
    const QVariantMap detail = m_pickedDetail;
    const ReadAt readAt = m_readAt;
    const int gen = m_generation;
    const pa::GradePolicy gp = pa::gradePolicyByName(m_gradePolicy);
    const double faultZ = gp.idealMaxZ > 0.0 ? gp.watchMaxZ / gp.idealMaxZ : 3.0;
    const auto norms = pinpoint::analysis::sharedNormProvider();
    const ConsistencyEnv env = m_consistencyEnv;
    auto cache = m_cache;
    if (m_sched.synchronous()) {
        m_pool.waitForDone();
        deliver(computeRelations(snap, picked, detail, reg, family, up, across, facet, open, readAt, gen, faultZ, gp, norms, env, *cache));
        return;
    }
    emit busyChanged();
    askLane(m_pool, [snap, picked, detail, reg, family, up, across, facet, open, readAt, gen, faultZ, gp, norms, env, cache]() { return computeRelations(snap, picked, detail, reg, family, up, across, facet, open, readAt, gen, faultZ, gp, norms, env, *cache); },
            [this](auto out) {
            const bool wasRunning = m_sched.land();
            deliver(out);
            m_sched.afterLanding(wasRunning);
        });
}

void ChartsRelationsPresenter::deliver(std::shared_ptr<const Output> out)
{
    QElapsedTimer t;
    t.start();
    auto o = std::const_pointer_cast<Output>(out);
    m_out = o;
    m_replayIndex = -2;          // force the playhead outputs
    emit resultsChanged();
    updatePlayhead(true);
    o->status.timings.jobMs = o->jobMs;          // read on this thread only
    o->status.timings.consistencyMs = o->consistencyMs;
    o->status.timings.applyMs = double(t.nsecsElapsed()) / 1e6;
    if (o->jobMs > 250.0)
        ppInfo() << "[Charts] relations recompute" << qRound(o->jobMs) << "ms for" << o->status.shotCount << "shots";
}

bool ChartsRelationsPresenter::ready() const { return m_out && m_out->ready; }
// Before the first result each output is its gadget's default, where the map was {} (charts_relations_types.h).
RelationsStatus ChartsRelationsPresenter::status() const { return m_out ? m_out->status : RelationsStatus{}; }
QList<RelationsFamily> ChartsRelationsPresenter::families() const { return m_out ? m_out->families : QList<RelationsFamily>{}; }
RelationsThisShot ChartsRelationsPresenter::thisShot() const { return m_out ? m_out->thisShot : RelationsThisShot{}; }
RelationsOpenRow ChartsRelationsPresenter::openRow() const { return m_out ? m_out->openRow : RelationsOpenRow{}; }
RelationsSequence ChartsRelationsPresenter::sequence() const { return m_out ? m_out->sequence : RelationsSequence{}; }
RelationsRelations ChartsRelationsPresenter::relations() const { return m_out ? m_out->relations : RelationsRelations{}; }
RelationsKinds ChartsRelationsPresenter::kinds() const { return m_out ? m_out->kinds : RelationsKinds{}; }

RelationsReplaySession ChartsRelationsPresenter::replaySession() const
{
    if (!m_out || m_replayIndex < 0 || size_t(m_replayIndex) >= m_out->replay.size()) return {};
    return m_out->replay[size_t(m_replayIndex)].entry;
}

void ChartsRelationsPresenter::updatePlayhead(bool force)
{
    if (!m_out) return;
    // the latest P-position the playhead has passed (a frame's slack), else the first
    int idx = m_out->replay.empty() ? -1 : 0;
    for (size_t i = 0; i < m_out->replay.size(); ++i)
        if (m_out->replay[i].tUs != std::numeric_limits<qint64>::max() && m_playheadUs >= 0 && m_out->replay[i].tUs <= m_playheadUs + 1000)
            idx = int(i);
    const bool replayMoved = idx != m_replayIndex;
    m_replayIndex = idx;

    RelationsPlayhead ph;
    const double ms = (m_out->pickedImpactUs >= 0 && m_playheadUs >= 0) ? double(m_playheadUs - m_out->pickedImpactUs) / 1000.0 : kNaN;
    const auto read = [&](const TraceCurve &c, const QString &unit) {
        if (!fin(ms) || c.empty()) return QString();
        const double v = valueAt(c, ms);
        return fin(v) ? figU(v, unit) : QString();
    };
    ph.openValueText = read(m_out->openCurve, m_out->openUnit);
    ph.leadValueText = read(m_out->leadCurve, m_out->leadUnit);
    ph.replayIndex = idx;
    const bool changed = force || replayMoved || ph != m_playhead;
    m_playhead = ph;
    if (changed) emit playheadChanged();
}

qint64 ChartsRelationsPresenter::seekUsFor(int ph) const
{
    if (!m_out) return -1;
    for (const PhaseMark &p : m_out->pickedPhases) if (p.phase == ph) return p.tUs;
    return -1;
}

qint64 ChartsRelationsPresenter::seekUsAtShotMs(double shotMs) const
{
    if (!m_out || m_out->pickedImpactUs < 0 || !fin(shotMs)) return -1;
    return m_out->pickedImpactUs + qint64(std::llround(shotMs * 1000.0));
}

namespace {

// Nice axis ticks: the app's one tick rule (figure_format.h), printed at the unit's decimals or the step's, whichever is
// finer. ticks() and axisTicks() both read these, so the screen and the paper cannot disagree.
QList<RelationsTick> ticksOf(double lo, double hi, int maxTicks, const QString &unit)
{
    QList<RelationsTick> out;
    if (!fin(lo) || !fin(hi)) return out;
    const auto tick = [](double v, const QString &text) { RelationsTick t; t.v = v; t.text = text; return t; };
    if (!(hi > lo)) { out.append(tick(lo, figBare(lo, shortUnit(unit)))); return out; }
    const double st = words::tickStep(lo, hi, std::max(1, maxTicks));
    const int dec = std::max(words::decimalsFor(shortUnit(unit)), words::tickDecimals(st));
    for (double v : words::tickValues(lo, hi, std::max(1, maxTicks))) out.append(tick(v, words::fig(v, dec)));
    return out;
}

} // namespace

QList<RelationsTick> ChartsRelationsPresenter::ticks(double lo, double hi, int maxTicks, const QString &unit) const
{
    return ticksOf(lo, hi, maxTicks, unit);
}

QVariantList ChartsRelationsPresenter::axisTicks(double lo, double hi, int maxTicks, const QString &unit)
{
    QVariantList out;       // the payloads' maps { v, text }, as before
    for (const RelationsTick &t : ticksOf(lo, hi, maxTicks, unit))
        out.append(QVariantMap{ { QStringLiteral("v"), t.v }, { QStringLiteral("text"), t.text } });
    return out;
}

bool ChartsRelationsPresenter::waitForIdle(int msTimeout)
{
    return m_sched.waitForIdle(m_pool, msTimeout);
}

// ── What moves with one reading (the Wrist tab's "what moves with the lead wrist at impact") ──────

RelationsMovesWith ChartsRelationsPresenter::movesWith(const std::shared_ptr<const SessionSnapshot> &snapshot,
                                                       const QString &readingId, int k, int lmK, const ReadAt &readAt)
{
    RelationsMovesWith m;
    const auto empty = std::make_shared<SessionSnapshot>();
    Cache cache;
    ensureSession(cache, snapshot ? snapshot : empty, QString(), {}, readAt, 0, false);
    Vocab vocab;
    Ctx x{ cache, *cache.snap, vocab };
    x.n = cache.snap->count();
    // the Relations tab's own refusal: r needs five shots both readings were read on
    if (x.n < 5 || cache.readings.size() < 2) {
        m.available = false;
        m.refusal = sentenceOf(x.n < 5 ? tooFew(x, QStringLiteral("to relate readings across a session"), 5)
                                       : Sentence().append(QStringLiteral("This session has too few readings to relate.")));
        return m;
    }
    const int y = cache.indexOf(readingId);
    if (y < 0) {
        m.available = false;
        m.refusal = sentenceOf(Sentence().append(QStringLiteral("This session does not read ") + readingId
                                                 + QStringLiteral(" on three shots or more.")));
        return m;
    }
    const SessionReading &Y = x.r(y);
    const std::vector<CorrRow> &rows = cache.corrOf(y);
    // the top k readings that are not the same quantity, then the top lmK launch-monitor readings not
    // already among them — each the Relations ledger's own row
    std::vector<int> taken;
    int rank = 0;
    for (const CorrRow &r : rows) {
        if (r.same) continue;
        ++rank;
        if (int(taken.size()) >= std::max(0, k)) break;
        taken.push_back(r.reading);
        m.rows.append(ledgerRowOf(x, r, Y, rank));
    }
    int lm = 0;
    rank = 0;
    for (const CorrRow &r : rows) {
        if (r.same) continue;
        ++rank;
        if (lm >= std::max(0, lmK)) break;
        if (!x.r(r.reading).key.startsWith(QLatin1String("lm."))) continue;
        if (std::find(taken.begin(), taken.end(), r.reading) != taken.end()) continue;
        m.rows.append(ledgerRowOf(x, r, Y, rank));
        ++lm;
    }
    const int tests = int(rows.size()), nY = Y.box.n;
    int clear = 0;
    for (const CorrRow &r : rows) if (r.c.clear) ++clear;
    const double rc = rCritical(nY);
    m.available = true;
    m.upId = Y.id;
    m.up = briefOf(x, y);
    m.head = sentenceOf(Sentence().append(QStringLiteral("What moves with ") + x.readingText(y)));
    m.chance.n = nY;
    m.chance.tests = tests;
    m.chance.clearCount = clear;
    m.chance.chanceCount = chanceCount(tests);
    m.chance.rCritical = rc;
    m.chance.rCriticalText = figR(rc);
    m.chance.line = sentenceOf(Sentence().append(QStringLiteral("|r| under ")).figure(figR(rc)).append(QStringLiteral(" is chance at n = "))
                                   .figure(QString::number(nY)));
    m.note = sentenceOf(Sentence().append(nY < 10 ? QStringLiteral("A small session. ") : QString())
                            .append(words::coOccurrenceNote()));
    return m;
}

// ── the shared builders (charts_relations_presenter.h) ───────────────────────────────────────

RelationsSentence relationsSentence(const words::Sentence &s)
{
    RelationsSentence out;
    out.text = s.text();
    out.runs.reserve(qsizetype(s.runs.size()));
    for (const words::Sentence::Run &r : s.runs) {
        RelationsWordRun run;
        run.text = r.text;
        run.fig = r.fig;
        out.runs.append(run);
    }
    out.clause = words::firstClauseOf(out.text);
    return out;
}

RelationsVariability relationsTypicality(const pinpoint::variability::ScalarTypicalityWindow &window,
                                         const QString &basisText, bool showCorridor)
{
    RelationsVariability out;
    out.kind = QStringLiteral("typicality");
    out.values.z = window.typicality.z;
    for (const pinpoint::variability::TypicalityGhost &g : window.ghosts) {
        RelationsTypicalityGhost ghost;
        ghost.swingId = g.swingId;
        ghost.z = g.z;
        out.values.ghosts.append(ghost);
    }
    if (window.corridor) {
        RelationsTypicalityCorridor c;
        c.loZ = window.corridor->loZ;
        c.hiZ = window.corridor->hiZ;
        out.values.corridor = QVariant::fromValue(c);
    }
    out.values.band = pinpoint::variability::VariabilityResultView::typicalityBandToken(window.typicality.band);
    out.basisText = basisText;
    out.showCorridor = showCorridor;
    return out;
}

} // namespace pinpoint::charts

#include "moc_charts_relations_types.cpp"
