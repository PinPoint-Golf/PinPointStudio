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

#include "box_map.h"
#include "charts_words.h"
#include "session_relations.h"
#include "session_sequence.h"
#include "session_series_model.h"
#include "session_traces.h"
#include "session_warp.h"

#include "../review/timeline_labels.h"
#include "../trust/trust_readings.h"     // the gate behind a reading a shot has no value for (gateFor)
#include "../../Core/pp_debug.h"
#include "../../Metrics/gate_catalogue.h"   // a masked reading's gate words (cautionOfPicked)
#include "../../Metrics/metric_catalogue.h"
#include "../../Diagnostics/norms/norm.h"   // the grade policy: where a fired reading is past its fault line
#include "../../Diagnostics/norms/norm_provider.h"   // the Readings rows' corridor: the norm the reading grades against
#include "../diagnostics/session_spread.h"           // …turned into bands exactly as Session diagnostics draws them

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDir>
#include <QElapsedTimer>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

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

bool fin(double v) { return std::isfinite(v); }
QVariant num(double v) { return fin(v) ? QVariant(v) : QVariant::fromValue(nullptr); }
QVariantList nums(const std::vector<double> &v)
{
    QVariantList out;
    out.reserve(qsizetype(v.size()));
    for (double x : v) out.append(num(x));
    return out;
}
QVariantList ints(const std::vector<int> &v)
{
    QVariantList out;
    for (int x : v) out.append(x);
    return out;
}
const QString kMinus = QString(QChar(0x2212));

// r as an annual report sets it: ".62", "−.88"; "-" when undefined.
QString r2(double r)
{
    if (!fin(r)) return QStringLiteral("-");
    QString s = QString::number(std::abs(r), 'f', 2);
    if (s.startsWith(QLatin1Char('0'))) s.remove(0, 1);
    return (r < 0 && s != QStringLiteral(".00") ? kMinus : QString()) + s;
}
QString dText(double d)
{
    if (!fin(d)) return QStringLiteral("-");
    return words::fig(d, 1);
}

QString shortUnit(const QString &u) { return u.startsWith(QLatin1Char('%')) ? QStringLiteral("%") : u; }
QString unitText(const QString &u) { return (u == QStringLiteral("°") || u == QLatin1String(":1")) ? QString() : u; }
QString figBare(double v, const QString &u) { return words::fig(v, words::decimalsFor(u)); }
QString figU(double v, const QString &u) { return words::figUnit(v, u); }
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

// boxMap is box_map.h's (the one serialisation); a box's value texts in a reading's own rounding:
std::function<QString(double)> figOf(const QString &unit) { return [unit](double v) { return figBare(v, unit); }; }
QVariantMap rangeMap(double lo, double hi) { return { { QStringLiteral("lo"), num(lo) }, { QStringLiteral("hi"), num(hi) } }; }
QVariantMap paddedRange(const BoxStats &b, double frac)
{
    if (!b.n) return rangeMap(0, 1);
    const double p = std::max(1e-6, (b.max - b.min) * frac);
    return rangeMap(b.min - p, b.max + p);
}

double valueAtPhaseOf(const ShotSeries &s, const QString &key, int ph)
{
    if (const MetricSeries *m = s.metric(key))
        for (const PhaseSample &p : m->phaseSamples) if (p.phase == ph && fin(p.value)) return p.value;
    return kNaN;
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
    s->shotId = SessionSeriesModel::folderShotId(swingDir);
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
    QHash<int, QVariantList> firedElsewhere;        // shot index → links that land on no reading
    struct Family { QString name; bool preset = false; std::vector<int> readings; };
    std::vector<Family> families;
    SessionKinds kinds;
    SessionSequence seq;
    std::vector<CorrRow> drifts;
    bool aligned = false;
    SessionAlignment alignment;
    QHash<QString, std::vector<TraceCurve>> curves;   // aligned, per key
    QHash<int, std::vector<CorrRow>> corr;            // per Up reading
    QHash<QString, QVariantMap> kindCurves;           // per key
    QHash<QString, QVariantMap> shapes;               // per family
    std::shared_ptr<const SessionSnapshot> clubBase;  // the largest club's shots, for kinds within it
    QString clubName;

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
};

struct ChartsRelationsPresenter::Output {
    bool ready = false;
    QVariantMap status, thisShot, openRow, sequence, relations, kinds;
    QVariantList families;
    struct ReplayEntry { qint64 tUs = std::numeric_limits<qint64>::max(); QVariantMap map; };
    std::vector<ReplayEntry> replay;
    qint64 pickedImpactUs = -1;
    std::vector<PhaseMark> pickedPhases;
    TraceCurve openCurve, leadCurve;
    QString openUnit, leadUnit;
    double jobMs = 0;
};

namespace {

using Cache = ChartsRelationsPresenter::Cache;
using Output = ChartsRelationsPresenter::Output;

// Fired, from the condition link only: per shot the reading drivenBy names, else ConditionInfo's.
void linkFired(const SessionSnapshot &s, std::vector<SessionReading> &rs, QHash<qint64, QList<FiredLink>> *firedBy,
               QHash<int, QVariantList> *elsewhere)
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
        const ShotDiagnostics &d = s.shots[size_t(i)]->diagnostics;
        for (const QString &c : d.fired) {
            QString key; int ph = -1, red = -1;
            const auto dv = d.drivenBy.constFind(c);
            if (dv != d.drivenBy.constEnd() && !dv.value().metricKey.isEmpty()) { key = dv.value().metricKey; ph = dv.value().phase; red = dv.value().reducer; }
            else if (s.conditions.contains(c)) { key = s.conditions.value(c).metricKey; ph = s.conditions.value(c).phase; red = s.conditions.value(c).reducer; }
            if (key.isEmpty()) continue;
            const int t = target(key, ph);
            if (t < 0) {
                if (elsewhere)
                    (*elsewhere)[i].append(QVariantMap{ { QStringLiteral("condition"), c },
                                                        { QStringLiteral("name"), s.conditions.value(c).name.isEmpty() ? c : s.conditions.value(c).name },
                                                        { QStringLiteral("metricKey"), key }, { QStringLiteral("phase"), ph },
                                                        { QStringLiteral("reducer"), red }, { QStringLiteral("instant"), instantPhrase(v, red, ph) } });
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
    QHash<QString, double> refusedEstimates;
    c.snap = withTrunkEstimates(c.snap, &refusedEstimates);
    const SessionSnapshot &S = *c.snap;

    // Keys in manifest order, then the uncatalogued (sorted).
    QSet<QString> present;
    for (const auto &sh : S.shots) for (const QString &k : sh->metricOrder) present.insert(k);
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
    for (SessionReading &r : c.readings) r.unit = shortUnit(r.unit);
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

    // A shot's own ordinal from its 1-based position in S (identity in a session; not in a club's subset).
    int ord(int pos) const { return pos >= 1 && pos <= int(S.shots.size()) ? S.shots[size_t(pos - 1)]->ordinal : pos; }
    std::vector<int> ords(const std::vector<int> &pos) const { std::vector<int> o; for (int p : pos) o.push_back(ord(p)); return o; }

    const SessionReading &r(int i) const { return c.readings[size_t(i)]; }
    const ReadingMeta &m(int i) const { return c.meta[size_t(i)]; }
    QString readingText(int i) const { return lowerLabel(r(i).label) + QStringLiteral(" at ") + v.tag(r(i).phase); }
    QVariantMap shotMark(int idx) const
    {
        const ShotSeries &s = *S.shots[size_t(idx)];
        return { { QStringLiteral("ordinal"), s.ordinal }, { QStringLiteral("shotId"), s.shotId }, { QStringLiteral("swingDir"), s.swingDir } };
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
        || (eff != QLatin1String("refused") && eff != QLatin1String("bounded"))) return {};
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

QVariantList boolsOf(const std::vector<uint8_t> &f)
{
    QVariantList out;
    for (uint8_t x : f) out.append(bool(x));
    return out;
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
QVariantMap corridorOf(const Ctx &x, const SessionReading &r)
{
    if (!x.norms) return { { QStringLiteral("known"), false } };
    const int N = x.n;
    for (int k = -1; k < N; ++k) {
        const int idx = k < 0 ? x.pick : k;
        if (idx < 0 || idx >= N || (k >= 0 && idx == x.pick)) continue;
        const ShotDiagnostics &d = x.S.shots[size_t(idx)]->diagnostics;
        for (auto it = d.gradedBy.constBegin(); it != d.gradedBy.constEnd(); ++it) {
            const ShotDiagnostics::Reading &g = it.value();
            if (g.measureId.isEmpty() || g.metricKey != r.key || g.phase != r.phase) continue;
            if (!g.sameQuantity) continue;
            const pinpoint::analysis::NormResolution res = x.norms->resolve(g.measureId, g.contextId);
            if (!res.found()) continue;
            const auto shape = g.shape >= 0 ? pinpoint::analysis::Shape(g.shape) : pinpoint::analysis::Shape::Target;
            const pinpoint::analysis::SpreadCorridor c = pinpoint::analysis::spreadCorridorFromNorm(*res.norm, shape, x.policy);
            if (!c.known) continue;
            return { { QStringLiteral("known"), true }, { QStringLiteral("lowOpen"), c.lowOpen }, { QStringLiteral("highOpen"), c.highOpen },
                     { QStringLiteral("signalLo"), c.signalLo }, { QStringLiteral("signalHi"), c.signalHi },
                     { QStringLiteral("faultLo"), c.faultLo }, { QStringLiteral("faultHi"), c.faultHi } };
        }
    }
    return { { QStringLiteral("known"), false } };
}

// The box plot's own numbers, in the row's rounding, for the values a hover lays on the plot (Mark, 18 Oct: "all the
// values you would see on a box plot"): the whiskers' ends, the quartiles, the median, and the corridor's edges and
// fault lines where one is drawn. QML only places them.
QVariantMap boxTexts(const SessionReading &r, const QVariantMap &c)
{
    const BoxStats &b = r.box;
    QVariantMap m;
    if (b.n < 3) return m;
    m.insert(QStringLiteral("lo"), figBare(b.lo, r.unit));   m.insert(QStringLiteral("hi"), figBare(b.hi, r.unit));
    m.insert(QStringLiteral("q1"), figBare(b.q1, r.unit));   m.insert(QStringLiteral("q3"), figBare(b.q3, r.unit));
    m.insert(QStringLiteral("med"), figBare(b.med, r.unit));
    if (c.value(QStringLiteral("known")).toBool())
        for (const char *k : { "signalLo", "signalHi", "faultLo", "faultHi" })
            m.insert(QLatin1String(k), figBare(c.value(QLatin1String(k)).toDouble(), r.unit));
    return m;
}

QVariantMap rowMap(const Ctx &x, int i)
{
    const SessionReading &r = x.r(i);
    const ReadingMeta &m = x.m(i);
    const ReadingPlace p = x.pick >= 0 ? placeOf(r, x.pick) : ReadingPlace{};
    const bool fired = x.pick >= 0 && size_t(x.pick) < r.fired.size() && r.fired[size_t(x.pick)];
    // this shot's verdict on the reading (the Readings tab's filter): past the fault line (the grade's Action band),
    // out of its corridor (Watch), graded inside it, or "" when no condition graded it on this shot
    const bool fault = fired && size_t(x.pick) < r.firedZ.size() && r.firedZ[size_t(x.pick)] > x.faultZ;
    const bool inside = !fired && x.pick >= 0 && size_t(x.pick) < r.clean.size() && r.clean[size_t(x.pick)];
    const QString verdict = fault ? QStringLiteral("fault") : fired ? QStringLiteral("outside") : inside ? QStringLiteral("inside") : QString();
    QVariantList conds, links;
    QString firedWord;
    if (fired)
        for (const FiredLink &l : x.c.firedBy.value(qint64(i) * x.n + x.pick)) {
            const QString name = x.S.conditions.value(l.id).name.isEmpty() ? l.id : x.S.conditions.value(l.id).name;
            const QString inst = instantPhrase(x.v, l.reducer, l.phase);
            const QString text = inst.isEmpty() ? name : name + QStringLiteral(", ") + inst;
            conds << text;
            links << QVariantMap{ { QStringLiteral("id"), l.id }, { QStringLiteral("name"), name }, { QStringLiteral("reducer"), l.reducer },
                                  { QStringLiteral("instant"), inst }, { QStringLiteral("text"), text } };
            if (firedWord.isEmpty()) firedWord = firedWordFor(x.v, l, fault);
        }
    if (fired && firedWord.isEmpty()) firedWord = fault ? QStringLiteral("past its fault line") : QStringLiteral("out of its corridor");
    const bool curve = m.drawsCurve && x.pickedHasCurve(r.key);
    const QVariantMap corr = corridorOf(x, r);
    // no value on this shot because a gate refused it: a plain "-" in the row; why is said once, in the panel's tail
    // (Mark, 17 Oct 2026). gateWords stays on the map for readers that name it (none on screen).
    const QVariantMap gate = x.pick >= 0 && !p.measured ? gateOfPicked(x, r.key) : QVariantMap{};
    const QString gateWords = gate.value(QStringLiteral("words")).toString();
    // a value a gate refused, bounded, downgraded or masked: shown, with its health warning under it (Mark, 17 Oct
    // 2026); a trunk peak the sequence could not place says how rough, the rest say their gate's words
    const bool estimate = x.pick >= 0 && size_t(x.pick) < r.refused.size() && r.refused[size_t(x.pick)] && p.measured;
    const double cSigma = estimate ? r.refusedSigma[size_t(x.pick)] : kNaN;
    const QString gateCaution = p.measured && !estimate ? cautionOfPicked(x, r) : QString();
    const QString cautionText = estimate ? (fin(cSigma) ? QStringLiteral("⚠ a rough estimate, ±%1 %2").arg(qRound(cSigma)).arg(r.unit)
                                                        : QStringLiteral("⚠ a rough estimate"))
                              : !gateCaution.isEmpty() ? QStringLiteral("⚠ ") + gateCaution : QString();
    const bool caution = !cautionText.isEmpty();
    return {
        { QStringLiteral("caution"), caution }, { QStringLiteral("cautionText"), cautionText },
        { QStringLiteral("trustSubject"), QString() },
        { QStringLiteral("gateWords"), gateWords },
        { QStringLiteral("readingId"), r.id }, { QStringLiteral("key"), r.key }, { QStringLiteral("phase"), r.phase },
        { QStringLiteral("tag"), x.v.tag(r.phase) }, { QStringLiteral("phaseName"), x.v.name(r.phase) },
        { QStringLiteral("label"), r.label }, { QStringLiteral("short"), m.shortLabel }, { QStringLiteral("unit"), r.unit },
        { QStringLiteral("unitText"), unitText(r.unit) }, { QStringLiteral("group"), m.group }, { QStringLiteral("multi"), r.multi },
        { QStringLiteral("measured"), p.measured }, { QStringLiteral("value"), num(p.measured ? p.value : kNaN) },
        { QStringLiteral("valueText"), figBare(p.measured ? p.value : kNaN, r.unit) },
        { QStringLiteral("valueFigure"), figU(p.measured ? p.value : kNaN, r.unit) },
        { QStringLiteral("medianText"), figBare(r.box.med, r.unit) },
        { QStringLiteral("place"), placeWords(p, r.box.n) }, { QStringLiteral("rankHi"), p.rankHi }, { QStringLiteral("rankLo"), p.rankLo },
        { QStringLiteral("rz"), p.rz }, { QStringLiteral("side"), p.side }, { QStringLiteral("apart"), p.side != 0 },
        { QStringLiteral("box"), boxMap(r.box, figOf(r.unit)) }, { QStringLiteral("values"), nums(r.values) },
        { QStringLiteral("firedShots"), boolsOf(r.fired) }, { QStringLiteral("fired"), fired },
        { QStringLiteral("firedWord"), firedWord }, { QStringLiteral("verdict"), verdict },
        { QStringLiteral("conditions"), conds }, { QStringLiteral("firedConditions"), links },
        { QStringLiteral("corridor"), corr }, { QStringLiteral("boxText"), boxTexts(r, corr) },
        { QStringLiteral("hasCurve"), curve }, { QStringLiteral("opens"), curve ? QStringLiteral("trace") : QStringLiteral("run") } };
}

QVariantMap driftMap(const Correlation &d, int n)
{
    QVariantMap out{ { QStringLiteral("defined"), d.defined }, { QStringLiteral("r"), num(d.r) }, { QStringLiteral("lo"), num(d.lo) },
                     { QStringLiteral("hi"), num(d.hi) }, { QStringLiteral("clear"), d.clear }, { QStringLiteral("rText"), r2(d.r) },
                     { QStringLiteral("ciText"), d.defined ? QStringLiteral("(%1 to %2)").arg(r2(d.lo), r2(d.hi)) : QString() },
                     { QStringLiteral("slope"), num(d.slope) }, { QStringLiteral("intercept"), num(d.intercept) } };
    out.insert(QStringLiteral("line"), d.defined && d.clear
        ? QVariant(QVariantMap{ { QStringLiteral("x0"), 1 }, { QStringLiteral("y0"), d.intercept + d.slope },
                                { QStringLiteral("x1"), n }, { QStringLiteral("y1"), d.intercept + d.slope * n } })
        : QVariant::fromValue(nullptr));
    return out;
}

QVariantMap runMap(const Ctx &x, const SessionReading &r)
{
    QVariantList pts;
    for (int i = 0; i < x.n; ++i) {
        QVariantMap p = x.shotMark(i);
        const double v = size_t(i) < r.values.size() ? r.values[size_t(i)] : kNaN;
        p.insert(QStringLiteral("value"), num(v));
        p.insert(QStringLiteral("measured"), fin(v));
        p.insert(QStringLiteral("fired"), size_t(i) < r.fired.size() && r.fired[size_t(i)]);
        p.insert(QStringLiteral("picked"), i == x.pick);
        pts.append(p);
    }
    const Correlation d = driftOf(r);
    QString cap = r.label + QStringLiteral(" at ") + x.v.tag(r.phase) + QStringLiteral(", shot by shot");
    if (d.defined)
        cap += QStringLiteral("  ·  drift r %1 (%2 to %3)").arg(r2(d.r), r2(d.lo), r2(d.hi)) + (d.clear ? QString() : QStringLiteral(", inside chance"));
    return { { QStringLiteral("readingId"), r.id }, { QStringLiteral("key"), r.key }, { QStringLiteral("label"), r.label },
             { QStringLiteral("tag"), x.v.tag(r.phase) }, { QStringLiteral("unit"), r.unit }, { QStringLiteral("unitText"), unitText(r.unit) },
             { QStringLiteral("caption"), cap }, { QStringLiteral("points"), pts }, { QStringLiteral("box"), boxMap(r.box, figOf(r.unit)) },
             { QStringLiteral("yRange"), paddedRange(r.box, 0.1) }, { QStringLiteral("drift"), driftMap(d, x.n) } };
}

// One reading's curve on the picked shot's own clock, with a box of every shot at each P-position the
// key is read at (Trace.qml).
QVariantMap traceMap(const Ctx &x, const SessionReading &r, TraceCurve *keep)
{
    if (x.pick < 0) return {};
    const ShotSeries &s = *x.S.shots[size_t(x.pick)];
    const TraceCurve c = shotCurve(s, r.key);
    if (c.empty()) return {};
    double lo = kNaN, hi = kNaN;
    for (size_t i = 0; i < c.t.size(); ++i) if (fin(c.v[i])) { if (!fin(lo)) lo = c.t[i]; hi = c.t[i]; }
    const qint64 a = phaseUsOf(s, phase::Address), f = phaseUsOf(s, phase::Finish);
    double t0 = fin(lo) ? lo : c.t.front(), t1 = fin(hi) ? hi : c.t.back();
    if (a >= 0) t0 = std::max(t0, double(a - s.impactUs) / 1000.0 - 150.0);
    if (f >= 0) t1 = std::min(t1, double(f - s.impactUs) / 1000.0 + 100.0);
    if (!(t1 > t0)) { t0 = c.t.front(); t1 = c.t.back(); }

    QVariantList tt, vv, rr, ok;
    double ylo = std::numeric_limits<double>::infinity(), yhi = -ylo;
    for (size_t i = 0; i < c.t.size(); ++i) {
        if (c.t[i] < t0 - 1e-9 || c.t[i] > t1 + 1e-9) continue;
        tt.append(c.t[i]);
        vv.append(c.ok[i] ? num(c.v[i]) : QVariant::fromValue(nullptr));
        rr.append(num(c.raw[i]));
        ok.append(bool(c.ok[i]));
        if (c.ok[i] && fin(c.v[i])) { ylo = std::min(ylo, c.v[i]); yhi = std::max(yhi, c.v[i]); }
    }
    QVariantList phases;
    for (const PhaseMark &p : s.phases) {
        if (!x.v.isP(p.phase)) continue;
        const double ms = double(p.tUs - s.impactUs) / 1000.0;
        if (ms < t0 || ms > t1) continue;
        phases.append(QVariantMap{ { QStringLiteral("phase"), p.phase }, { QStringLiteral("tag"), x.v.tag(p.phase) },
                                   { QStringLiteral("shotMs"), ms }, { QStringLiteral("impact"), p.phase == phase::Impact } });
    }
    // every P-position the key is sampled at on some shot
    std::vector<int> phs;
    for (const auto &sh : x.S.shots)
        if (const MetricSeries *ms = sh->metric(r.key))
            for (const PhaseSample &p : ms->phaseSamples)
                if (fin(p.value) && std::find(phs.begin(), phs.end(), p.phase) == phs.end()) phs.push_back(p.phase);
    QVariantList instants;
    for (int ph : phs) {
        const qint64 us = phaseUsOf(s, ph);
        if (us < 0) continue;
        std::vector<double> vals;
        for (const auto &sh : x.S.shots) vals.push_back(valueAtPhaseOf(*sh, r.key, ph));
        const BoxStats b = boxStats(vals);
        if (b.n < 3) continue;
        ylo = std::min(ylo, b.min); yhi = std::max(yhi, b.max);
        const double pv = vals[size_t(x.pick)];
        instants.append(QVariantMap{ { QStringLiteral("phase"), ph }, { QStringLiteral("tag"), x.v.tag(ph) },
                                     { QStringLiteral("shotMs"), double(us - s.impactUs) / 1000.0 }, { QStringLiteral("box"), boxMap(b, figOf(r.unit)) },
                                     { QStringLiteral("values"), nums(vals) },
                                     { QStringLiteral("firedShots"), ph == r.phase ? boolsOf(r.fired) : QVariantList{} },
                                     { QStringLiteral("pickedValue"), num(pv) }, { QStringLiteral("pickedText"), figU(pv, r.unit) },
                                     { QStringLiteral("own"), ph == r.phase } });
    }
    if (!fin(ylo)) { ylo = 0; yhi = 1; }
    const double pad = std::max(1e-6, (yhi - ylo) * 0.08);
    if (keep) *keep = c;
    return { { QStringLiteral("readingId"), r.id }, { QStringLiteral("key"), r.key }, { QStringLiteral("label"), r.label },
             { QStringLiteral("tag"), x.v.tag(r.phase) }, { QStringLiteral("unit"), r.unit }, { QStringLiteral("unitText"), unitText(r.unit) },
             { QStringLiteral("caption"), QStringLiteral("%1, shot %2; at each P-position a box of all %3 shots").arg(r.label).arg(s.ordinal).arg(x.n) },
             { QStringLiteral("window"), QVariantMap{ { QStringLiteral("t0"), t0 }, { QStringLiteral("t1"), t1 } } },
             { QStringLiteral("yRange"), rangeMap(ylo - pad, yhi + pad) },
             { QStringLiteral("curve"), QVariantMap{ { QStringLiteral("t"), tt }, { QStringLiteral("v"), vv }, { QStringLiteral("raw"), rr }, { QStringLiteral("ok"), ok } } },
             { QStringLiteral("phases"), phases }, { QStringLiteral("instants"), instants } };
}

QString axisName(const Ctx &x, const SessionReading &r)
{
    return r.label + QStringLiteral(" at ") + x.v.tag(r.phase)
         + ((!r.unit.isEmpty() && r.unit != QStringLiteral("°")) ? QStringLiteral(", ") + r.unit : QString());
}

QVariantMap scatterMap(const Ctx &x, const SessionReading &rx, const SessionReading &ry, const std::vector<int> &subset,
                       const std::vector<uint8_t> &firedMask)
{
    std::vector<int> idx;
    if (subset.empty()) { for (int i = 0; i < x.n; ++i) idx.push_back(i); }
    else for (int o : subset) idx.push_back(o - 1);
    std::vector<double> xs, ys, xa(size_t(x.n), kNaN), ya(size_t(x.n), kNaN);
    QVariantList pts;
    for (int i : idx) {
        const double a = rx.values[size_t(i)], b = ry.values[size_t(i)];
        xs.push_back(a); ys.push_back(b); xa[size_t(i)] = a; ya[size_t(i)] = b;
        if (!fin(a) || !fin(b)) continue;
        QVariantMap p = x.shotMark(i);
        p.insert(QStringLiteral("x"), a); p.insert(QStringLiteral("y"), b);
        p.insert(QStringLiteral("picked"), i == x.pick);
        p.insert(QStringLiteral("fired"), size_t(i) < firedMask.size() && firedMask[size_t(i)]);
        pts.append(p);
    }
    const Correlation f = pearson(xs, ys);
    QVariant line = QVariant::fromValue(nullptr);
    if (f.defined) {
        double x0 = kNaN, x1 = kNaN;
        for (size_t i = 0; i < xs.size(); ++i) if (fin(xs[i]) && fin(ys[i])) { x0 = fin(x0) ? std::min(x0, xs[i]) : xs[i]; x1 = fin(x1) ? std::max(x1, xs[i]) : xs[i]; }
        line = QVariantMap{ { QStringLiteral("x0"), x0 }, { QStringLiteral("y0"), f.intercept + f.slope * x0 },
                            { QStringLiteral("x1"), x1 }, { QStringLiteral("y1"), f.intercept + f.slope * x1 } };
    }
    const QString cap = f.defined ? QStringLiteral("r %1 (%2 to %3)  ·  n %4").arg(r2(f.r), r2(f.lo), r2(f.hi)).arg(f.n)
                                        + (f.clear ? QString() : QStringLiteral("  ·  inside chance"))
                                  : QStringLiteral("Too few shots for a line.");
    return { { QStringLiteral("xId"), rx.id }, { QStringLiteral("yId"), ry.id }, { QStringLiteral("xKey"), rx.key }, { QStringLiteral("yKey"), ry.key },
             { QStringLiteral("xLabel"), axisName(x, rx) + QStringLiteral("  →") }, { QStringLiteral("yLabel"), QStringLiteral("↑  ") + axisName(x, ry) },
             { QStringLiteral("xUnit"), rx.unit }, { QStringLiteral("yUnit"), ry.unit },
             { QStringLiteral("xRange"), paddedRange(rx.box, 0.08) }, { QStringLiteral("yRange"), paddedRange(ry.box, 0.08) },
             { QStringLiteral("xBox"), boxMap(boxStats(xa), figOf(rx.unit)) }, { QStringLiteral("yBox"), boxMap(boxStats(ya), figOf(ry.unit)) },
             { QStringLiteral("points"), pts }, { QStringLiteral("defined"), f.defined }, { QStringLiteral("r"), num(f.r) },
             { QStringLiteral("lo"), num(f.lo) }, { QStringLiteral("hi"), num(f.hi) }, { QStringLiteral("n"), f.n },
             { QStringLiteral("clear"), f.clear }, { QStringLiteral("rText"), r2(f.r) },
             { QStringLiteral("ciText"), f.defined ? QStringLiteral("(%1 to %2)").arg(r2(f.lo), r2(f.hi)) : QString() },
             { QStringLiteral("line"), line }, { QStringLiteral("caption"), cap } };
}

QString slopeText(const Correlation &c, const SessionReading &x, const SessionReading &y)
{
    if (!c.defined || !fin(c.slope)) return QStringLiteral("-");
    const double a = std::abs(c.slope);
    const int d = a >= 10 ? 0 : a >= 1 ? 1 : a >= 0.1 ? 2 : 3;
    const QString yu = y.unit.isEmpty() ? QString() : (closedUnit(y.unit) ? y.unit : QString(QChar(0x00A0)) + y.unit);
    const QString xu = x.unit.isEmpty() ? QStringLiteral("unit") : x.unit;
    return (c.slope < 0 ? kMinus : QStringLiteral("+")) + QString::number(a, 'f', d) + yu + QStringLiteral(" per ") + xu;
}

QVariantMap ledgerRow(const Ctx &x, const CorrRow &row, const SessionReading &y, int rank)
{
    const SessionReading &r = x.r(row.reading);
    const Correlation &c = row.c;
    return { { QStringLiteral("rank"), row.same ? 0 : rank }, { QStringLiteral("readingId"), r.id }, { QStringLiteral("key"), r.key },
             { QStringLiteral("label"), r.label }, { QStringLiteral("short"), x.m(row.reading).shortLabel }, { QStringLiteral("tag"), x.v.tag(r.phase) },
             { QStringLiteral("same"), row.same }, { QStringLiteral("sameMark"), row.same ? QStringLiteral("‡") : QString() },
             { QStringLiteral("finding"), !row.same && c.clear },
             { QStringLiteral("r"), num(c.r) }, { QStringLiteral("rText"), r2(c.r) }, { QStringLiteral("lo"), num(c.lo) }, { QStringLiteral("hi"), num(c.hi) },
             { QStringLiteral("loText"), r2(c.lo) }, { QStringLiteral("hiText"), r2(c.hi) },
             { QStringLiteral("ciText"), QStringLiteral("(%1 to %2)").arg(r2(c.lo), r2(c.hi)) }, { QStringLiteral("n"), c.n },
             { QStringLiteral("clear"), c.clear }, { QStringLiteral("slope"), num(c.slope) }, { QStringLiteral("slopeText"), slopeText(c, r, y) },
             { QStringLiteral("pair"), QVariantMap{ { QStringLiteral("xs"), nums(r.values) }, { QStringLiteral("ys"), nums(y.values) },
                                                    { QStringLiteral("xRange"), rangeMap(r.box.min, r.box.max) },
                                                    { QStringLiteral("yRange"), rangeMap(y.box.min, y.box.max) } } } };
}

QVariantMap briefMap(const Ctx &x, int i)
{
    const SessionReading &r = x.r(i);
    return { { QStringLiteral("readingId"), r.id }, { QStringLiteral("key"), r.key }, { QStringLiteral("label"), r.label },
             { QStringLiteral("short"), x.m(i).shortLabel }, { QStringLiteral("tag"), x.v.tag(r.phase) }, { QStringLiteral("unit"), r.unit },
             { QStringLiteral("group"), x.m(i).group }, { QStringLiteral("text"), x.m(i).shortLabel + QLatin1Char(' ') + x.v.tag(r.phase) } };
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

QVariantMap keyFiguresMap(const Ctx &x, const Cache::Family *fam)
{
    QVariantList items;
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
            items.append(QVariantMap{ { QStringLiteral("readingId"), sequenceSeriesKey(s) }, { QStringLiteral("key"), sequenceSeriesKey(s) }, { QStringLiteral("label"), kSegLabels[s] + QStringLiteral(" peak") + (p.probable ? QStringLiteral(" ⚠") : QString()) },
                                      { QStringLiteral("figure"), p.timed() ? signedMsBare(v) : QStringLiteral("-") }, { QStringLiteral("unit"), p.timed() && fin(v) ? QStringLiteral("ms") : QString() },
                                      { QStringLiteral("foot"), foot }, { QStringLiteral("median"), timedN >= 3 ? words::signedMs(sm.medianMs) : QString() },
                                      { QStringLiteral("place"), QString() }, { QStringLiteral("warn"), p.late },
                                      { QStringLiteral("warnWord"), p.late ? QStringLiteral("late") : QString() },
                                      // the reason lives in the panel's tail ("How far can I trust this?"), not on the tile
                                      { QStringLiteral("trustSubject"), QString() },
                                      { QStringLiteral("sentence"), (p.timed() || why.sentence.isEmpty() ? Sentence().figure(words::signedMs(v)) : Sentence().append(why.sentence))
                                                                      .append(p.probable ? (p.probableHow == QLatin1String("seen") ? QStringLiteral(", indicated") : QStringLiteral(", probable")) : QString())
                                                                      .append(timedN >= 3 ? QStringLiteral(", median ") : QString())
                                                                      .figure(timedN >= 3 ? words::signedMs(sm.medianMs) : QString())
                                                                      .append(p.late ? QStringLiteral(", late") : QString()).toVariant() } });
        }
    } else if (fam && x.pick >= 0) {
        for (int i : fam->readings) {
            if (items.size() >= 4) break;
            const SessionReading &r = x.r(i);
            const ReadingPlace p = placeOf(r, x.pick);
            if (!p.measured) {
                // A REFUSED READING KEEPS ITS PLACE as a plain "-"; why is said once, in the panel's tail
                const QVariantMap g = gateOfPicked(x, r.key);
                if (g.isEmpty()) continue;
                const bool solo = x.c.solo || x.n < 3;
                const QString med = solo ? QString() : figBare(r.box.med, r.unit);
                items.append(QVariantMap{ { QStringLiteral("readingId"), r.id }, { QStringLiteral("key"), r.key },
                                          { QStringLiteral("label"), x.m(i).shortLabel + QStringLiteral(" at ") + x.v.tag(r.phase) },
                                          { QStringLiteral("figure"), QStringLiteral("-") },
                                          { QStringLiteral("unit"), QString() },
                                          { QStringLiteral("foot"), med.isEmpty() ? QString() : QStringLiteral("median ") + med },
                                          { QStringLiteral("median"), med }, { QStringLiteral("place"), QString() },
                                          { QStringLiteral("warn"), false }, { QStringLiteral("warnWord"), QString() },
                                          { QStringLiteral("trustSubject"), QString() },
                                          { QStringLiteral("sentence"), Sentence().append(g.value(QStringLiteral("words")).toString()).toVariant() } });
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
            items.append(QVariantMap{ { QStringLiteral("readingId"), r.id }, { QStringLiteral("key"), r.key }, { QStringLiteral("label"), x.m(i).shortLabel + QStringLiteral(" at ") + x.v.tag(r.phase) },
                                      { QStringLiteral("figure"), closedUnit(r.unit) ? figU(p.value, r.unit) : figBare(p.value, r.unit) },
                                      { QStringLiteral("unit"), closedUnit(r.unit) ? QString() : r.unit },
                                      { QStringLiteral("foot"), foot.join(QStringLiteral("  ·  ")) }, { QStringLiteral("median"), med },
                                      { QStringLiteral("place"), place }, { QStringLiteral("warn"), fired },
                                      { QStringLiteral("warnWord"), warnWord },
                                      { QStringLiteral("sentence"), s.toVariant() } });
        }
    }
    return { { QStringLiteral("family"), fam ? fam->name : QString() }, { QStringLiteral("sequence"), seq }, { QStringLiteral("items"), items } };
}

QVariantMap thisShotMap(const Ctx &x, const Cache::Family *fam, Output *out)
{
    QVariantMap m;
    const int total = int(x.c.readings.size());
    m.insert(QStringLiteral("total"), total);
    if (x.pick < 0) {
        m.insert(QStringLiteral("available"), false);
        m.insert(QStringLiteral("head"), (x.n == 0 ? emptySession(x) : Sentence().append(QStringLiteral("No shot is picked"))).toVariant());
        return m;
    }
    const ShotSeries &ps = *x.S.shots[size_t(x.pick)];
    m.insert(QStringLiteral("kicker"), QStringLiteral("Shot %1 of %2").arg(ps.ordinal).arg(x.n)
                                       + (ps.club.isEmpty() ? QString() : QStringLiteral("  ·  ") + ps.club.toLower()));
    m.insert(QStringLiteral("keyFigures"), keyFiguresMap(x, fam));
    // the family's lead reading on this shot — this shot's own, so published at any n
    QVariantMap lead;
    if (fam) {
        int li = -1;
        if (fam->name == kSeqFamily) li = x.c.indexOf(QStringLiteral("clubAngularSpeed@%1").arg(phase::Impact));
        if (li < 0) for (int i : fam->readings) if (x.m(i).drawsCurve && x.pickedHasCurve(x.r(i).key)) { li = i; break; }
        if (li < 0) for (int i : fam->readings) if (placeOf(x.r(i), x.pick).measured) { li = i; break; }
        if (li < 0 && !fam->readings.empty()) li = fam->readings.front();
        if (li >= 0) {
            const QVariantMap tr = traceMap(x, x.r(li), &out->leadCurve);
            out->leadUnit = x.r(li).unit;
            lead = { { QStringLiteral("readingId"), x.r(li).id }, { QStringLiteral("trace"), tr.isEmpty() ? QVariant::fromValue(nullptr) : QVariant(tr) } };
        }
    }
    m.insert(QStringLiteral("lead"), lead);
    if (x.n < 3) {
        m.insert(QStringLiteral("available"), false);
        m.insert(QStringLiteral("head"), tooFew(x, QStringLiteral("to set a shot against its session"), 3).toVariant());
        return m;
    }
    m.insert(QStringLiteral("available"), true);

    // apart, most extreme first
    const std::vector<int> ap = apartFrom(x.c.readings, x.pick);
    QVariantList apart;
    for (int i : ap) apart.append(rowMap(x, i));
    const int na = int(ap.size());
    m.insert(QStringLiteral("apartCount"), na);
    m.insert(QStringLiteral("apart"), apart);
    m.insert(QStringLiteral("head"), (na == 0 ? Sentence().append(QStringLiteral("Shot %1 sits inside the session").arg(ps.ordinal))
                                               : Sentence().append(words::cap1(words::word(na)) + (na == 1 ? QStringLiteral(" reading") : QStringLiteral(" readings"))
                                                                   + QStringLiteral(" apart from the session"))).toVariant());
    Sentence st;
    st.append(QStringLiteral("Of ")).figure(QString::number(total))
      .append(QStringLiteral(" readings taken on shot %1 at the instants their cards read them, ").arg(ps.ordinal));
    if (!na) st.append(QStringLiteral("none sits past the session's whiskers; this swing is the session's own."));
    else {
        st.append(words::word(na) + (na == 1 ? QStringLiteral(" sits") : QStringLiteral(" sit")) + QStringLiteral(" past the session's whiskers"));
        std::vector<int> hi, lo;
        for (int i : ap) { const ReadingPlace p = placeOf(x.r(i), x.pick); if (p.rankHi == 1) hi.push_back(i); if (p.rankLo == 1) lo.push_back(i); }
        QStringList parts;
        if (!hi.empty()) parts << QStringLiteral("the most ") + x.readingText(hi[0])
                                  + (hi.size() > 1 ? QStringLiteral(" and ") + words::word(int(hi.size()) - 1) + QStringLiteral(" other highest") : QString());
        if (!lo.empty()) parts << QStringLiteral("the least ") + x.readingText(lo[0]);
        st.append(parts.isEmpty() ? QStringLiteral(".")
                                  : QStringLiteral(": it had ") + parts.join(QStringLiteral(", and ")) + QStringLiteral(" of all ") + words::word(x.n) + QStringLiteral("."));
    }
    m.insert(QStringLiteral("standfirst"), st.toVariant());

    // every reading by family (D5)
    QVariantList groups;
    for (const QString &g : x.c.groupOrder) {
        QVariantList rows;
        for (size_t i = 0; i < x.c.readings.size(); ++i)
            if (x.c.meta[i].group == g) rows.append(rowMap(x, int(i)));
        if (!rows.isEmpty()) groups.append(QVariantMap{ { QStringLiteral("name"), g }, { QStringLiteral("rows"), rows } });
    }
    m.insert(QStringLiteral("groups"), groups);
    m.insert(QStringLiteral("notes"), QStringLiteral("Apart: past a whisker, which reaches 1.5 times the middle half beyond its quartiles. "
                                                     "Each row has its own scale, the session's lowest to highest. A fired mark is a reading a "
                                                     "diagnostics condition graded and found outside its corridor."));
    m.insert(QStringLiteral("firedElsewhere"), x.c.firedElsewhere.value(x.pick));

    // the family: the lead reading's trace and the other rows (Replay)
    QVariantList famRows;
    if (fam) {
        const bool seq = fam->name == kSeqFamily;
        if (!seq) {
            int figs = 0;
            for (int i : fam->readings) if (figs < 4 && placeOf(x.r(i), x.pick).measured) ++figs;
            QStringList seen;
            std::vector<int> order(fam->readings.begin() + std::min<size_t>(size_t(figs), fam->readings.size()), fam->readings.end());
            for (int k = 0; k < figs && k < int(fam->readings.size()); ++k) if (x.r(fam->readings[size_t(k)]).multi) order.push_back(fam->readings[size_t(k)]);
            for (int i : order) if (!seen.contains(x.r(i).id)) { seen << x.r(i).id; famRows.append(rowMap(x, i)); }
        }
    }
    m.insert(QStringLiteral("familyRows"), famRows);

    // questions (Analyse.qml shotAsks)
    QVariantList asks;
    for (int i : ap) {
        const SessionReading &r = x.r(i);
        const double pv = r.values[size_t(x.pick)];
        std::vector<int> same;
        for (int o : r.box.outliers) if (o != x.pick && (r.values[size_t(o)] - r.box.med) * (pv - r.box.med) > 0) same.push_back(o + 1);
        if (same.empty()) continue;
        std::vector<int> lit = same; lit.push_back(ps.ordinal);
        asks.append(QVariantMap{ { QStringLiteral("id"), QStringLiteral("same") }, { QStringLiteral("text"), r.label + QStringLiteral(": was any other shot out there?") },
                                 { QStringLiteral("answer"), words::cap1(words::shotsList(same)) + (same.size() == 1 ? QStringLiteral(" was") : QStringLiteral(" were"))
                                                             + QStringLiteral(" past the same whisker; lit in every row.") },
                                 { QStringLiteral("lit"), ints(lit) }, { QStringLiteral("go"), QString() }, { QStringLiteral("upId"), QString() } });
        break;
    }
    if (!ap.empty())
        asks.append(QVariantMap{ { QStringLiteral("id"), QStringLiteral("moves") }, { QStringLiteral("text"), x.r(ap[0]).label + QStringLiteral(": what moves with it?") },
                                 { QStringLiteral("answer"), QStringLiteral("Relations opens on it.") }, { QStringLiteral("lit"), QVariantList{} },
                                 { QStringLiteral("go"), QStringLiteral("relations") }, { QStringLiteral("upId"), x.r(ap[0]).id } });
    if (x.c.kinds.method != KindsMethod::None)
        asks.append(QVariantMap{ { QStringLiteral("id"), QStringLiteral("kind") }, { QStringLiteral("text"), QStringLiteral("Shot %1: which kind of swing was it?").arg(ps.ordinal) },
                                 { QStringLiteral("answer"), QStringLiteral("Kinds opens with shot %1 marked.").arg(ps.ordinal) },
                                 { QStringLiteral("lit"), QVariantList{ ps.ordinal } }, { QStringLiteral("go"), QStringLiteral("kinds") }, { QStringLiteral("upId"), QString() } });
    m.insert(QStringLiteral("asks"), asks);
    return m;
}

// ── the sequence (SeqTiming) ────────────────────────────────────────────────────────────────

QVariantMap sequenceMap(const Ctx &x)
{
    const SessionSequence &q = x.c.seq;
    QVariantMap m;
    if (x.n == 0) { m.insert(QStringLiteral("available"), false); return m; }
    m.insert(QStringLiteral("available"), true);
    const ShotSequence *ps = x.pick >= 0 ? &q.shots[size_t(x.pick)] : nullptr;
    QVariantList chain;
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
            chain.append(QVariantMap{ { QStringLiteral("segment"), sequenceSegmentIds()[size_t(order[k])] }, { QStringLiteral("label"), kSegLabels[order[k]] },
                                      { QStringLiteral("probable"), p.probable },
                                      { QStringLiteral("shotMs"), p.timeMs() }, { QStringLiteral("msText"), words::signedMs(p.timeMs()) },
                                      { QStringLiteral("late"), p.late }, { QStringLiteral("lateWord"), p.late ? QStringLiteral("late") : QString() } });
        }
    }
    if (cs.isEmpty()) cs.append(QStringLiteral("No segment placed on this shot."));
    m.insert(QStringLiteral("chain"), chain);
    m.insert(QStringLiteral("chainSentence"), cs.toVariant());
    const QString unplacedText = unplaced.isEmpty() ? QString() : words::list(unplaced) + QStringLiteral(" not placed");
    m.insert(QStringLiteral("unplaced"), unplacedText);
    int same = 0;
    const QString mine = ps ? ps->order.join(QLatin1Char(',')) : QString();
    for (const ShotSequence &s : q.shots) if (!mine.isEmpty() && s.order.join(QLatin1Char(',')) == mine) ++same;
    m.insert(QStringLiteral("orderSame"), same);
    m.insert(QStringLiteral("orderCount"), x.n);
    QStringList parts;
    if (ps && ps->timedCount >= 2)
        parts << (late.isEmpty() ? QStringLiteral("in sequence") : QStringLiteral("out of sequence: the ") + words::list(late) + QStringLiteral(" peaked late"));
    if (!unplacedText.isEmpty()) parts << unplacedText;
    if (!mine.isEmpty()) parts << QStringLiteral("this order on %1 of %2 shots").arg(same).arg(x.n);
    m.insert(QStringLiteral("verdict"), Sentence().append(parts.isEmpty() ? QString() : words::cap1(parts.join(QStringLiteral("; "))) + QStringLiteral(".")).toVariant());

    double lo = -150, hi = 40;
    for (const ShotSequence &s : q.shots) for (const SegmentPeak &p : s.segments) if (p.timed() && fin(p.timeMs())) { lo = std::min(lo, p.timeMs()); hi = std::max(hi, p.timeMs()); }
    lo = std::floor((lo - 10) / 25) * 25; hi = std::ceil((hi + 10) / 25) * 25;
    m.insert(QStringLiteral("axis"), rangeMap(lo, hi));
    QVariantList ticks;
    const double step = (hi - lo) > 300 ? 100 : 50;
    for (double t = std::ceil(lo / step) * step; t <= hi + 1e-9; t += step)
        ticks.append(QVariantMap{ { QStringLiteral("ms"), t }, { QStringLiteral("text"), t == 0 ? QStringLiteral("impact") : signedMsBare(t) } });
    m.insert(QStringLiteral("ticks"), ticks);
    QVariantList rows;
    for (int s = 0; s < kSequenceSegments; ++s) {
        std::vector<double> ms;
        QVariantList lateShots;
        for (const ShotSequence &sh : q.shots) {
            const SegmentPeak &p = sh.segments[size_t(s)];
            ms.push_back(p.timed() ? p.timeMs() : kNaN);
            lateShots.append(p.late);
        }
        const SegmentSummary &sm = q.segments[size_t(s)];
        const double v = ps && ps->segments[size_t(s)].timed() ? ps->segments[size_t(s)].timeMs() : kNaN;
        const int timedN = sm.placed + sm.probable;
        rows.append(QVariantMap{ { QStringLiteral("segment"), sequenceSegmentIds()[size_t(s)] },
                                 { QStringLiteral("label"), kSegLabels[s] + (sm.probable > 0 ? QStringLiteral(" ⚠") : QString()) },
                                 { QStringLiteral("valueText"), signedMsBare(v) }, { QStringLiteral("shotMs"), num(v) },
                                 { QStringLiteral("placed"), timedN },
                                 { QStringLiteral("placedText"), sm.probable > 0 ? QStringLiteral("%1 of %2, probable").arg(timedN).arg(x.n)
                                                                                 : QStringLiteral("%1 of %2").arg(sm.placed).arg(x.n) },
                                 { QStringLiteral("box"), boxMap(boxStats(ms), [](double v) { return signedMsBare(v); }) }, { QStringLiteral("values"), nums(ms) }, { QStringLiteral("lateShots"), lateShots },
                                 { QStringLiteral("hasBox"), timedN >= 3 },
                                 { QStringLiteral("notPlacedText"), timedN == 0 ? QStringLiteral("not placed on any shot")
                                                                    : timedN < 3 ? QStringLiteral("placed on too few shots for a box") : QString() } });
    }
    m.insert(QStringLiteral("rows"), rows);
    const SegmentSummary &pel = q.segments[0];
    Sentence note;
    if (pel.placed >= 3 && pel.afterImpact * 2 >= pel.placed)
        note.append(QStringLiteral("The pelvis peaks after impact on ")).figure(QString::number(pel.afterImpact))
            .append(QStringLiteral(" of ")).figure(QString::number(pel.placed)).append(QStringLiteral(" placed shots. ") + words::latePelvisNote());
    m.insert(QStringLiteral("note"), note.toVariant());
    return m;
}

// ── Relations ──────────────────────────────────────────────────────────────────────────────

QVariantMap relationsMap(Ctx &x, const QString &upWanted, const QString &acrossWanted, const QString &facetWanted, QString *upOut)
{
    QVariantMap m;
    Cache &c = x.c;
    // r needs five shots both readings were read on (session_relations.h pearson), so below five there is
    // nothing to relate: refused with the reason, never an empty ledger.
    if (x.n < 5 || c.readings.size() < 2) {
        m.insert(QStringLiteral("available"), false);
        m.insert(QStringLiteral("refusal"), (x.n < 5 ? tooFew(x, QStringLiteral("to relate readings across a session"), 5)
                                                     : Sentence().append(QStringLiteral("This session has too few readings to relate."))).toVariant());
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
    m.insert(QStringLiteral("available"), true);
    m.insert(QStringLiteral("upId"), Y.id);
    m.insert(QStringLiteral("acrossId"), X.id);
    m.insert(QStringLiteral("up"), briefMap(x, y));
    m.insert(QStringLiteral("across"), briefMap(x, xi));
    m.insert(QStringLiteral("upValues"), nums(Y.values));

    // the pickers' catalogue
    QVariantList cat;
    for (const QString &g : c.groupOrder) {
        QVariantList rs;
        for (size_t i = 0; i < c.readings.size(); ++i) if (c.meta[i].group == g) {
            const QVariantMap b = briefMap(x, int(i));
            rs.append(QVariantMap{ { QStringLiteral("readingId"), b.value(QStringLiteral("readingId")) }, { QStringLiteral("short"), b.value(QStringLiteral("short")) },
                                   { QStringLiteral("tag"), b.value(QStringLiteral("tag")) }, { QStringLiteral("text"), b.value(QStringLiteral("text")) },
                                   { QStringLiteral("label"), b.value(QStringLiteral("label")) } });
        }
        cat.append(QVariantMap{ { QStringLiteral("name"), g }, { QStringLiteral("readings"), rs } });
    }
    m.insert(QStringLiteral("catalogue"), cat);

    // the chance line, said out loud
    const int tests = int(rows.size());
    int clear = 0;
    for (const CorrRow &r : rows) if (r.c.clear) ++clear;
    const int ch = chanceCount(tests), nY = Y.box.n;
    const double rc = rCritical(nY);
    QString upName = lowerLabel(Y.label);
    m.insert(QStringLiteral("head"), Sentence().append(QStringLiteral("What moves with ") + upName).toVariant());
    m.insert(QStringLiteral("short"), Sentence().append(words::cap1(words::word(clear)) + QStringLiteral(" of ")).figure(QString::number(tests))
                                         .append(QStringLiteral(" readings clear chance with ") + x.readingText(y) + QStringLiteral("; chance alone would clear about ")
                                                 + words::word(ch) + QStringLiteral(".")).toVariant());
    Sentence sf;
    sf.append(words::cap1(words::word(clear)) + QStringLiteral(" of ")).figure(QString::number(tests))
      .append(QStringLiteral(" readings moved with ") + x.readingText(y) + QStringLiteral(" closely enough for the 95\u00A0% interval to clear zero"))
      .append(clear <= ch + 1 ? QStringLiteral("; chance alone would clear about ") + words::word(ch) + QStringLiteral(", so read them as leads to test, not findings. ")
                              : QStringLiteral("; chance alone would clear about ") + words::word(ch) + QStringLiteral(". "))
      .append(QStringLiteral("With ") + words::word(nY) + QStringLiteral(" shots an r inside ±")).figure(r2(rc))
      .append(QStringLiteral(" is what chance gives. ") + words::coOccurrenceNote());
    m.insert(QStringLiteral("standfirst"), sf.toVariant());
    m.insert(QStringLiteral("chance"), QVariantMap{
        { QStringLiteral("n"), nY }, { QStringLiteral("tests"), tests }, { QStringLiteral("clearCount"), clear }, { QStringLiteral("chanceCount"), ch },
        { QStringLiteral("rCritical"), rc }, { QStringLiteral("rCriticalText"), r2(rc) },
        { QStringLiteral("line"), Sentence().append(QStringLiteral("|r| under ")).figure(r2(rc)).append(QStringLiteral(" is chance at n = ")).figure(QString::number(nY)).toVariant() },
        { QStringLiteral("sentence"), Sentence().figure(QString::number(clear)).append(QStringLiteral(" of ")).figure(QString::number(tests))
                                         .append(QStringLiteral(" readings clear chance; chance alone would clear about ")).figure(QString::number(ch))
                                         .append(QStringLiteral(". ") + words::coOccurrenceNote()).toVariant() } });

    m.insert(QStringLiteral("scatter"), scatterMap(x, X, Y, {}, {}));
    m.insert(QStringLiteral("upRun"), runMap(x, Y));

    // the ledger, ranked by |r|; a same-quantity row is labelled and not ranked as a finding
    QVariantList ledger;
    int rank = 0;
    for (const CorrRow &r : rows) ledger.append(ledgerRow(x, r, Y, r.same ? 0 : ++rank));
    m.insert(QStringLiteral("ledger"), ledger);
    int restClear = 0;
    for (size_t i = 10; i < rows.size(); ++i) if (rows[i].c.clear) ++restClear;
    m.insert(QStringLiteral("restText"), rows.size() > 10 ? QStringLiteral("The other %1 readings, %2").arg(rows.size() - 10)
                                                                .arg(restClear ? QStringLiteral("%1 of them clearing chance").arg(restClear) : QStringLiteral("all inside chance"))
                                                          : QString());
    m.insert(QStringLiteral("ledgerNote"), Sentence().append(QStringLiteral("r: how closely the two moved together across the shots both were read on, from −1 to 1. "
                                                                            "The interval is 95\u00A0% (Fisher); one that crosses the centre line could be no relation at all. With "))
                                              .figure(QString::number(nY)).append(QStringLiteral(" shots an r inside ±")).figure(r2(rc))
                                              .append(QStringLiteral(" is what chance gives. ‡ The same quantity measured another way, so the pair says little and is not ranked. ")
                                                      + words::coOccurrenceNote()).toVariant());

    // facets offered
    QVariantList facets;
    const std::vector<SessionFacet> cf = conditionFacets(x.S);
    for (size_t i = 0; i < cf.size() && i < 3; ++i) {
        const QString name = x.S.conditions.value(cf[i].id).name.isEmpty() ? cf[i].id : x.S.conditions.value(cf[i].id).name;
        facets.append(QVariantMap{ { QStringLiteral("id"), cf[i].id }, { QStringLiteral("kind"), QStringLiteral("condition") }, { QStringLiteral("name"), name },
                                   { QStringLiteral("label"), lowerLabel(name) + QLatin1Char(' ') + QString::number(cf[i].firedCount) }, { QStringLiteral("n"), cf[i].firedCount } });
    }
    facets.append(QVariantMap{ { QStringLiteral("id"), QStringLiteral("half") }, { QStringLiteral("kind"), QStringLiteral("half") },
                               { QStringLiteral("name"), QStringLiteral("Session half") }, { QStringLiteral("label"), QStringLiteral("session half") }, { QStringLiteral("n"), 0 } });
    const std::optional<SessionFacet> club = clubFacet(x.S);
    if (club) facets.append(QVariantMap{ { QStringLiteral("id"), QStringLiteral("club") }, { QStringLiteral("kind"), QStringLiteral("club") },
                                         { QStringLiteral("name"), QStringLiteral("Club") }, { QStringLiteral("label"), QStringLiteral("club") }, { QStringLiteral("n"), 0 } });
    m.insert(QStringLiteral("facets"), facets);

    // the chosen facet: the same pair as small multiples, each panel its own r
    QVariantMap facet;
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
                    panels.push_back({ QStringLiteral("Shots %1 to %2").arg(g.ordinals.front()).arg(g.ordinals.back()), g.ordinals });
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
            QVariantList pl;
            for (size_t k = 0; k < panels.size(); ++k)
                pl.append(QVariantMap{ { QStringLiteral("title"), panels[k].first }, { QStringLiteral("n"), int(panels[k].second.size()) },
                                       { QStringLiteral("ordinals"), ints(panels[k].second) }, { QStringLiteral("fired"), kind == QLatin1String("condition") && k == 0 },
                                       { QStringLiteral("scatter"), scatterMap(x, X, Y, panels[k].second, mask) } });
            fs.append(words::coOccurrenceNote());
            facet = { { QStringLiteral("id"), facetWanted }, { QStringLiteral("kind"), kind }, { QStringLiteral("name"), name },
                      { QStringLiteral("sentence"), fs.toVariant() }, { QStringLiteral("panels"), pl } };
        }
    }
    m.insert(QStringLiteral("facet"), facet);

    // the family matrix: Up's family, lower triangle
    std::vector<int> mids;
    for (size_t i = 0; i < c.readings.size() && mids.size() < 9; ++i) if (c.meta[i].group == c.meta[size_t(y)].group) mids.push_back(int(i));
    QVariantList mr, cells;
    for (size_t a = 0; a < mids.size(); ++a) {
        mr.append(briefMap(x, mids[a]));
        QVariantList row;
        for (size_t b = 0; b < a; ++b) {
            const Correlation p = pairOf(x.r(mids[a]), x.r(mids[b]));
            const QString ya = x.r(mids[a]).id, xb = x.r(mids[b]).id;
            row.append(QVariantMap{ { QStringLiteral("defined"), p.defined }, { QStringLiteral("r"), num(p.r) }, { QStringLiteral("rText"), r2(p.r) },
                                    { QStringLiteral("clear"), p.clear }, { QStringLiteral("xId"), xb }, { QStringLiteral("yId"), ya },
                                    { QStringLiteral("on"), (ya == Y.id && xb == X.id) || (ya == X.id && xb == Y.id) } });
        }
        cells.append(QVariant(row));
    }
    m.insert(QStringLiteral("matrix"), QVariantMap{ { QStringLiteral("group"), c.meta[size_t(y)].group }, { QStringLiteral("readings"), mr }, { QStringLiteral("cells"), cells } });

    // drift: the drifts that clear chance, strongest first
    QVariantList di;
    int dclear = 0;
    for (const CorrRow &d : c.drifts) {
        if (!d.c.clear) continue;
        ++dclear;
        const SessionReading &r = x.r(d.reading);
        const QString dir = d.c.r > 0 ? QStringLiteral("rising") : QStringLiteral("falling");
        Sentence s;
        s.append(words::cap1(x.readingText(d.reading)) + QStringLiteral(", r ")).figure(r2(d.c.r)).append(QStringLiteral(", ") + dir + QStringLiteral(" through the session"));
        di.append(QVariantMap{ { QStringLiteral("readingId"), r.id }, { QStringLiteral("label"), r.label }, { QStringLiteral("tag"), x.v.tag(r.phase) },
                               { QStringLiteral("r"), d.c.r }, { QStringLiteral("rText"), r2(d.c.r) }, { QStringLiteral("lo"), d.c.lo }, { QStringLiteral("hi"), d.c.hi },
                               { QStringLiteral("ciText"), QStringLiteral("(%1 to %2)").arg(r2(d.c.lo), r2(d.c.hi)) }, { QStringLiteral("clear"), true },
                               { QStringLiteral("direction"), dir }, { QStringLiteral("sentence"), s.toVariant() }, { QStringLiteral("run"), runMap(x, r) } });
    }
    const int dtests = int(c.drifts.size()), dch = chanceCount(dtests);
    m.insert(QStringLiteral("drifts"), QVariantMap{
        { QStringLiteral("clearCount"), dclear }, { QStringLiteral("tests"), dtests }, { QStringLiteral("items"), di },
        { QStringLiteral("sentence"), Sentence().append(words::cap1(words::word(dclear)) + QStringLiteral(" of ")).figure(QString::number(dtests))
                                         .append(QStringLiteral(" readings ran with shot order closely enough to clear chance; chance alone would clear about ")
                                                 + words::word(dch) + QStringLiteral(". ") + words::coOccurrenceNote()).toVariant() } });

    // questions (Analyse.qml sessionAsks)
    QVariantList asks;
    const Correlation f = pairOf(X, Y);
    if (f.defined) {
        std::vector<std::pair<double, int>> res;
        for (int i = 0; i < x.n; ++i)
            if (fin(X.values[size_t(i)]) && fin(Y.values[size_t(i)]))
                res.push_back({ Y.values[size_t(i)] - (f.intercept + f.slope * X.values[size_t(i)]), i });
        std::stable_sort(res.begin(), res.end(), [](const auto &a, const auto &b) { return std::abs(a.first) > std::abs(b.first); });
        if (res.size() >= 2) {
            std::vector<int> two{ res[0].second + 1, res[1].second + 1 };
            asks.append(QVariantMap{ { QStringLiteral("id"), QStringLiteral("line") }, { QStringLiteral("text"), X.label + QStringLiteral(": which shots break the line?") },
                                     { QStringLiteral("answer"), words::cap1(words::shotsList(two)) + QStringLiteral(" sit furthest from it, ")
                                                                 + figU(std::abs(res[0].first), Y.unit) + QStringLiteral(" and ") + figU(std::abs(res[1].first), Y.unit) + QStringLiteral(" off the line.") },
                                     { QStringLiteral("lit"), ints(two) }, { QStringLiteral("go"), QString() }, { QStringLiteral("upId"), QString() } });
        }
    }
    const Correlation d = driftOf(Y);
    if (d.defined) {
        const int third = std::max(1, x.n / 3);
        std::vector<int> lit;
        for (int i = 1; i <= third; ++i) lit.push_back(i);
        for (int i = x.n - third + 1; i <= x.n; ++i) lit.push_back(i);
        asks.append(QVariantMap{ { QStringLiteral("id"), QStringLiteral("drift") }, { QStringLiteral("text"), Y.label + QStringLiteral(": is it drifting through the session?") },
                                 { QStringLiteral("answer"), (d.clear ? (d.r > 0 ? QStringLiteral("Rising: ") : QStringLiteral("Falling: "))
                                                                      : QStringLiteral("Not that %1 shots can show: ").arg(words::word(d.n)))
                                                             + QStringLiteral("r %1 (%2 to %3) with shot order. The first and last thirds are lit.").arg(r2(d.r), r2(d.lo), r2(d.hi)) },
                                 { QStringLiteral("lit"), ints(lit) }, { QStringLiteral("go"), QString() }, { QStringLiteral("upId"), QString() } });
    }
    if (c.kinds.method != KindsMethod::None) {
        QStringList nm;
        if (c.kinds.method == KindsMethod::GapSplit) {
            const QString k = x.r(c.kinds.splitReading).key;
            nm = k.contains(QStringLiteral("peed")) ? QStringList{ QStringLiteral("Faster"), QStringLiteral("slower") } : QStringList{ QStringLiteral("Higher"), QStringLiteral("lower") };
        } else nm = QStringList{ QStringLiteral("Kind A"), QStringLiteral("kind B") };
        asks.append(QVariantMap{ { QStringLiteral("id"), QStringLiteral("kind") }, { QStringLiteral("text"), nm[0] + QStringLiteral(" and ") + nm[1] + QStringLiteral(" swings: what separates them?") },
                                 { QStringLiteral("answer"), QStringLiteral("Kinds opens on them.") }, { QStringLiteral("lit"), QVariantList{} },
                                 { QStringLiteral("go"), QStringLiteral("kinds") }, { QStringLiteral("upId"), QString() } });
    }
    m.insert(QStringLiteral("asks"), asks);
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

QVariantMap kindCurveMap(Ctx &x, int ri, const QStringList &names)
{
    Cache &c = x.c;
    const SessionReading &r = x.r(ri);
    const SessionKinds &k = c.kinds;
    const SessionAlignment &al = c.align();
    const std::vector<TraceCurve> &cs = c.alignedOf(r.key);
    double lo = std::numeric_limits<double>::infinity(), hi = -lo;
    for (const TraceCurve &t : cs) if (!t.empty()) { lo = std::min(lo, t.t.front()); hi = std::max(hi, t.t.back()); }
    if (!fin(lo)) return {};
    if (const CanonicalPhase *a = al.canonicalOf(phase::Address)) lo = std::max(lo, a->tMs - 150.0);
    if (const CanonicalPhase *f = al.canonicalOf(phase::Finish)) hi = std::min(hi, f->tMs + 150.0);
    if (!(hi > lo)) return {};
    const std::vector<double> grid = sessionGrid(lo, hi);
    QVariantList fits;
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
        QVariantList cover;
        for (int o : *groups[g]) {
            const TraceCurve &t = cs[size_t(o - 1)];
            QVariantList runs;
            for (const TimeSpan &s : coverageRuns(t)) runs.append(QVariant(QVariantList{ s.t0, s.t1 }));
            int okN = 0;
            for (uint8_t v : t.ok) okN += v;
            cover.append(QVariantMap{ { QStringLiteral("ordinal"), x.ord(o) }, { QStringLiteral("picked"), o - 1 == x.pick },
                                      { QStringLiteral("frac"), t.t.empty() ? 0.0 : double(okN) / double(t.t.size()) }, { QStringLiteral("runs"), runs } });
        }
        QVariantList nShots;
        for (int v : f.nShots) nShots.append(v);
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
        fits.append(QVariantMap{ { QStringLiteral("name"), names[g] }, { QStringLiteral("n"), n }, { QStringLiteral("contributing"), nc },
                                 { QStringLiteral("label"), f.refused() ? label + QStringLiteral(": too few for a curve") : label },
                                 { QStringLiteral("refused"), f.refused() }, { QStringLiteral("refusal"), refusal },
                                 { QStringLiteral("t"), nums(f.t) }, { QStringLiteral("v"), nums(f.v) }, { QStringLiteral("sd"), nums(f.sd) },
                                 { QStringLiteral("lo"), nums(flo) }, { QStringLiteral("hi"), nums(fhi) }, { QStringLiteral("drawn"), drawn },
                                 { QStringLiteral("nShots"), nShots }, { QStringLiteral("cover"), cover } });
    }
    // THE WINDOW FOLLOWS THE FITS' FINITE VALUES, not the curves' extent: the plot spans where at least
    // one kind has a value, padded by 5 % of that span (at least 2 ms) each side; an end is stretched to a
    // canonical P-position lying within 20 ms beyond the finite span, so its tag is on the plot rather than
    // just off it. Clipped to the fitted grid. A curve with no finite value anywhere keeps its fits'
    // refusals and has no window (null) and no phases. Each fit's arrays are trimmed to the window.
    QVariant window = QVariant::fromValue(nullptr);
    QVariantList phases;
    if (fin(dataLo) && fin(dataHi)) {
        const double pad = std::max(2.0, 0.05 * (dataHi - dataLo));
        double w0 = dataLo - pad, w1 = dataHi + pad;
        for (const CanonicalPhase &p : al.canonical) {
            if (p.tMs < w0 && p.tMs >= dataLo - 20.0) w0 = p.tMs;
            if (p.tMs > w1 && p.tMs <= dataHi + 20.0) w1 = p.tMs;
        }
        w0 = std::max(w0, lo); w1 = std::min(w1, hi);
        window = QVariantMap{ { QStringLiteral("t0"), w0 }, { QStringLiteral("t1"), w1 } };
        for (const CanonicalPhase &p : al.canonical)
            if (p.tMs >= w0 - 1e-9 && p.tMs <= w1 + 1e-9)
                phases.append(QVariantMap{ { QStringLiteral("phase"), p.phase }, { QStringLiteral("tag"), x.v.tag(p.phase) }, { QStringLiteral("tMs"), p.tMs } });
        const QStringList arrays{ QStringLiteral("t"), QStringLiteral("v"), QStringLiteral("sd"), QStringLiteral("lo"), QStringLiteral("hi"), QStringLiteral("nShots") };
        for (QVariant &fv : fits) {
            QVariantMap fm = fv.toMap();
            const QVariantList t = fm.value(QStringLiteral("t")).toList();
            qsizetype a = 0, b = t.size();
            while (a < b && t[a].toDouble() < w0 - 1e-9) ++a;
            while (b > a && t[b - 1].toDouble() > w1 + 1e-9) --b;
            for (const QString &k : arrays) fm.insert(k, fm.value(k).toList().mid(a, b - a));
            fv = fm;
        }
    }
    return { { QStringLiteral("readingId"), r.id }, { QStringLiteral("key"), r.key }, { QStringLiteral("label"), r.label },
             { QStringLiteral("unit"), r.unit }, { QStringLiteral("unitText"), unitText(r.unit) },
             { QStringLiteral("window"), window },
             { QStringLiteral("yRange"), fin(ylo) ? rangeMap(ylo - 0.05 * std::max(1e-6, yhi - ylo), yhi + 0.05 * std::max(1e-6, yhi - ylo)) : rangeMap(0, 1) },
             { QStringLiteral("phases"), phases }, { QStringLiteral("fits"), fits } };
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

QVariantMap kindsCore(Ctx &x, const Cache::Family *fam)
{
    QVariantMap m;
    Cache &c = x.c;
    const SessionKinds &k = c.kinds;
    if (k.method == KindsMethod::None) {
        const Sentence why = x.n < 6 ? tooFew(x, QStringLiteral("to look for kinds of swing"), 6)
                                     : Sentence().append(QStringLiteral("No kinds and no clean split in this session."));
        m.insert(QStringLiteral("available"), false);
        m.insert(QStringLiteral("method"), QStringLiteral("none"));
        m.insert(QStringLiteral("head"), why.toVariant());
        m.insert(QStringLiteral("refusal"), why.toVariant());
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
    m.insert(QStringLiteral("available"), true);
    m.insert(QStringLiteral("method"), km ? QStringLiteral("kmedoids") : QStringLiteral("split"));
    m.insert(QStringLiteral("structure"), km);
    m.insert(QStringLiteral("silhouette"), num(k.silhouette));
    m.insert(QStringLiteral("silhouetteText"), r2(k.silhouette));
    m.insert(QStringLiteral("readingsUsed"), k.readingsUsed);
    m.insert(QStringLiteral("minSize"), k.minSize);
    m.insert(QStringLiteral("names"), nm);

    // the head says how weak the structure is FIRST (D7)
    Sentence head, st, split;
    st.append(QStringLiteral("Taken on all ")).figure(QString::number(k.readingsUsed))
      .append(QStringLiteral(" readings at once, the ") + words::word(x.n) + QStringLiteral(" shots "));
    if (km) {
        head.append(QStringLiteral("Two kinds of swing: ") + words::word(nA) + QStringLiteral(" and ") + words::word(nB));
        st.append(QStringLiteral("fall into two kinds (silhouette ")).figure(r2(k.silhouette)).append(QStringLiteral("). "));
    } else {
        const SessionReading &r = x.r(k.splitReading);
        const QString gap = figU(k.splitHigh - k.splitLow, r.unit);
        head.append(QStringLiteral("No structure; one split: ") + words::word(nA) + QLatin1Char(' ') + nm[0].toLower()
                    + QStringLiteral(", ") + words::word(nB) + QLatin1Char(' ') + nm[1].toLower());
        st.append(QStringLiteral("do not fall into kinds (silhouette ")).figure(r2(k.silhouette))
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
            const QString ps = QStringLiteral("Shot %1 is one of the %2%3.").arg(x.ord(o)).arg(words::word(inA ? nA : nB))
                               .arg(km ? QStringLiteral(" in ") + pickedKind : QLatin1Char(' ') + pickedKind.toLower());
            st.append(ps);
            m.insert(QStringLiteral("pickedSentence"), ps);
        } else if (std::find(missing.begin(), missing.end(), o) != missing.end()) {
            const QString ps = km ? QStringLiteral("Shot %1 could not be grouped.").arg(x.ord(o))
                                  : QStringLiteral("Shot %1 has no %2, so it is in neither.").arg(x.ord(o)).arg(x.readingText(k.splitReading));
            st.append(ps);
            m.insert(QStringLiteral("pickedSentence"), ps);
        }
    }
    m.insert(QStringLiteral("pickedKind"), pickedKind);
    m.insert(QStringLiteral("head"), head.toVariant());
    m.insert(QStringLiteral("standfirst"), st.toVariant());
    m.insert(QStringLiteral("splitLine"), split.toVariant());

    // members
    QVariantList groups;
    const std::vector<int> *gs[2] = { &k.groupA, &k.groupB };
    const int meds[2] = { k.medoidA, k.medoidB };
    for (int g = 0; g < 2; ++g) {
        QVariantList members;
        for (int o : *gs[g]) { QVariantMap mk = x.shotMark(o - 1); mk.insert(QStringLiteral("picked"), o - 1 == x.pick); members.append(mk); }
        groups.append(QVariantMap{ { QStringLiteral("name"), nm[g] }, { QStringLiteral("n"), int(gs[g]->size()) }, { QStringLiteral("members"), members },
                                   { QStringLiteral("medoid"), x.ord(meds[g]) }, { QStringLiteral("medoidText"), QStringLiteral("most typical: shot %1").arg(x.ord(meds[g])) },
                                   { QStringLiteral("neutral"), false } });
    }
    if (nM) {
        QVariantList members;
        for (int o : missing) { QVariantMap mk = x.shotMark(o - 1); mk.insert(QStringLiteral("picked"), o - 1 == x.pick); members.append(mk); }
        groups.append(QVariantMap{ { QStringLiteral("name"), words::cap1(missingWord) }, { QStringLiteral("n"), nM }, { QStringLiteral("members"), members },
                                   { QStringLiteral("medoid"), 0 }, { QStringLiteral("medoidText"), QString() }, { QStringLiteral("neutral"), true },
                                   { QStringLiteral("text"), missingWord + QStringLiteral(": ") + words::shotsList(x.ords(missing)) } });
    }
    m.insert(QStringLiteral("groups"), groups);
    m.insert(QStringLiteral("unplacedCount"), nM);

    // the separation table
    const QString splitKey = k.splitReading >= 0 ? x.r(k.splitReading).key : QString();
    QVariantList seps;
    for (const KindSeparation &s : k.separations) {
        if (seps.size() >= 8) break;
        if (!km && s.reading == k.splitReading) continue;
        const SessionReading &r = x.r(s.reading);
        std::vector<double> va(size_t(x.n), kNaN), vb(size_t(x.n), kNaN);
        for (int o : k.groupA) va[size_t(o - 1)] = r.values[size_t(o - 1)];
        for (int o : k.groupB) vb[size_t(o - 1)] = r.values[size_t(o - 1)];
        const bool same = !km && sameQuantityAs(r.key, splitKey);
        seps.append(QVariantMap{ { QStringLiteral("readingId"), r.id }, { QStringLiteral("key"), r.key }, { QStringLiteral("label"), r.label }, { QStringLiteral("tag"), x.v.tag(r.phase) },
                                 { QStringLiteral("same"), same }, { QStringLiteral("sameMark"), same ? QStringLiteral("‡") : QString() },
                                 { QStringLiteral("d"), s.d }, { QStringLiteral("dText"), dText(s.d) }, { QStringLiteral("clear"), std::abs(s.d) >= k.dCritical },
                                 { QStringLiteral("medAText"), figBare(s.medA, r.unit) }, { QStringLiteral("medBText"), figBare(s.medB, r.unit) },
                                 { QStringLiteral("unitText"), unitText(r.unit) }, { QStringLiteral("scale"), paddedRange(r.box, 0.06) },
                                 { QStringLiteral("boxA"), boxMap(boxStats(va), figOf(r.unit)) }, { QStringLiteral("boxB"), boxMap(boxStats(vb), figOf(r.unit)) },
                                 { QStringLiteral("valuesA"), nums(va) }, { QStringLiteral("valuesB"), nums(vb) } });
    }
    m.insert(QStringLiteral("separation"), QVariantMap{
        { QStringLiteral("dCritical"), k.dCritical }, { QStringLiteral("dCriticalText"), words::fig(k.dCritical, 1) }, { QStringLiteral("rows"), seps },
        { QStringLiteral("note"), Sentence().append(QStringLiteral("d: the gap between the kinds' means in their pooled standard deviation. With ")
                                                    + words::word(nA) + QStringLiteral(" and ") + words::word(nB) + QStringLiteral(" shots, a d under about "))
                                     .figure(words::fig(k.dCritical, 1))
                                     .append(QStringLiteral(" is within what chance gives, and a reading that clears it is still one of "))
                                     .figure(QString::number(k.separations.size()))
                                     .append(QStringLiteral(" tested. ‡ The same quantity measured another way. ") + words::coOccurrenceNote()).toVariant() } });

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
    QVariantList curves;
    for (int ri : chosen) {
        const QString key = x.r(ri).key;
        auto it = c.kindCurves.find(key);
        if (it == c.kindCurves.end()) it = c.kindCurves.insert(key, kindCurveMap(x, ri, nm));
        if (it.value().isEmpty()) continue;
        QVariantMap cm = it.value();
        // the picked flag on coverage rows follows the picked shot, which the cache does not know
        QVariantList fits = cm.value(QStringLiteral("fits")).toList();
        for (QVariant &fv : fits) {
            QVariantMap fm = fv.toMap();
            QVariantList cov = fm.value(QStringLiteral("cover")).toList();
            for (QVariant &cv : cov) { QVariantMap cmm = cv.toMap(); cmm.insert(QStringLiteral("picked"), x.pick >= 0 && cmm.value(QStringLiteral("ordinal")).toInt() == x.ord(x.pick + 1)); cv = cmm; }
            fm.insert(QStringLiteral("cover"), cov); fv = fm;
        }
        cm.insert(QStringLiteral("fits"), fits);
        curves.append(cm);
    }
    m.insert(QStringLiteral("curves"), curves);
    m.insert(QStringLiteral("alignNote"), alignNote(x, c.align()).toVariant());

    // A's curve-shape groups, a second grouping
    QVariantMap shapes{ { QStringLiteral("found"), false } };
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
            QVariantMap sm{ { QStringLiteral("found"), false } };
            const SessionAlignment &al = c.align();
            const CanonicalPhase *t0 = al.canonicalOf(phase::Top), *t1 = al.canonicalOf(phase::P8);
            if (!skeys.isEmpty() && t0 && t1) {
                std::vector<std::vector<TraceCurve>> per;
                for (const QString &key : skeys) per.push_back(c.alignedOf(key));
                const ShapeSplit sp = shapeSplit(per, t0->tMs, t1->tMs);
                if (sp.found) {
                    const auto g = [&](int med, const std::vector<int> &ords) {
                        return QVariantMap{ { QStringLiteral("name"), QStringLiteral("shaped like shot %1").arg(med) },
                                            { QStringLiteral("label"), QStringLiteral("shaped like shot %1 · %2").arg(med).arg(ords.size()) },
                                            { QStringLiteral("medoid"), med }, { QStringLiteral("n"), int(ords.size()) }, { QStringLiteral("ordinals"), ints(ords) } };
                    };
                    sm = { { QStringLiteral("found"), true }, { QStringLiteral("groups"), QVariantList{ g(sp.medoidA, sp.groupA), g(sp.medoidB, sp.groupB) } },
                           { QStringLiteral("sentence"), Sentence().append(QStringLiteral("By the shape of their ") + lowerLabel(fk)
                                                                           + QStringLiteral(" curves from P4 to P8, a second grouping: shaped like shot %1 (%2) and like shot %3 (%4).")
                                                                                 .arg(sp.medoidA).arg(sp.groupA.size()).arg(sp.medoidB).arg(sp.groupB.size())).toVariant() } };
                }
            }
            it = c.shapes.insert(fk, sm);
        }
        shapes = it.value();
    }
    m.insert(QStringLiteral("shapes"), shapes);
    m.insert(QStringLiteral("fitNote"), QStringLiteral("Each kind's curve is fitted to the pooled valid samples of every shot in it (a local straight line, "
                                                       "weighted, over 20 ms either side), on a clock that lines the shots' P-positions up on the session's median "
                                                       "ones; the hairlines either side are the pooled samples' spread, one standard deviation. Under it, each "
                                                       "shot's own measured stretches: a gap is where that shot was bridged or not sampled, and its kind's other "
                                                       "shots carry the curve there. The curve is the kind's, not any shot's, and no shot's number is read from "
                                                       "it. It breaks where fewer than three shots contribute, and a kind of fewer than three has none."));
    m.insert(QStringLiteral("methodNote"), QStringLiteral("How the kinds were found: every reading all %1 shots have, set on a common scale (median and "
                                                          "middle half, held to ±2.5), the shots grouped by k-medoids into two of at least %2. When that "
                                                          "grouping is no better than chance would make it, the single widest gap in any one reading is used "
                                                          "instead, and the sentence above says which. %3 shots can suggest kinds; they cannot confirm them.")
                                              .arg(x.n).arg(k.minSize).arg(words::cap1(words::word(x.n))));
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

QVariantMap kindsMap(Ctx &x, const Cache::Family *fam, bool nested = false);

QVariantMap clubKinds(Ctx &x, const SessionFacet &clubs, const QString &cA, const QString &cB, int agree, int grouped)
{
    Cache &c = x.c;
    const SessionKinds &k = c.kinds;
    QVariantMap m;
    m.insert(QStringLiteral("available"), true);
    m.insert(QStringLiteral("method"), QStringLiteral("clubs"));
    m.insert(QStringLiteral("structure"), false);
    m.insert(QStringLiteral("silhouette"), num(k.silhouette));
    m.insert(QStringLiteral("silhouetteText"), r2(k.silhouette));
    m.insert(QStringLiteral("readingsUsed"), k.readingsUsed);
    m.insert(QStringLiteral("minSize"), k.minSize);
    m.insert(QStringLiteral("names"), QStringList{ cA.toLower(), cB.toLower() });
    m.insert(QStringLiteral("clubAgreement"), QVariantMap{ { QStringLiteral("agree"), agree }, { QStringLiteral("grouped"), grouped } });
    m.insert(QStringLiteral("head"), Sentence().append((agree >= kClubAgreementClean * grouped ? QStringLiteral("The two kinds are the two clubs: ")
                                                                                                : QStringLiteral("The two kinds are mostly the two clubs: "))
                                                       + cA.toLower() + QStringLiteral(" and ") + cB.toLower()).toVariant());

    // within the largest club, when it has the shots kinds need
    const FacetGroup *big = nullptr;
    for (const FacetGroup &g : clubs.groups) if (!big || g.ordinals.size() > big->ordinals.size()) big = &g;
    Sentence within;
    QVariant withinMap = QVariant::fromValue(nullptr);
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
        const QVariantMap inner = kindsMap(sx, nullptr, true);
        QString innerHead = inner.value(QStringLiteral("head")).toMap().value(QStringLiteral("text")).toString();
        if (!innerHead.isEmpty() && innerHead[0].isUpper() && !innerHead.startsWith(QStringLiteral("Six"))) innerHead[0] = innerHead[0].toLower();
        if (!innerHead.endsWith(QLatin1Char('.'))) innerHead += QLatin1Char('.');
        within.append(QStringLiteral("Within the ") + words::word(int(big->ordinals.size())) + QLatin1Char(' ') + big->key.toLower()
                      + QStringLiteral(" shots alone: ") + innerHead);
        withinMap = QVariantMap{ { QStringLiteral("club"), big->key.toLower() }, { QStringLiteral("n"), int(big->ordinals.size()) },
                                 { QStringLiteral("sentence"), within.toVariant() }, { QStringLiteral("kinds"), inner } };
    } else {
        within.append(QStringLiteral("Neither club has the six shots kinds need, so this session cannot be split into kinds of swing."));
    }
    m.insert(QStringLiteral("withinClub"), withinMap);

    Sentence st;
    st.append(QStringLiteral("Taken on all ")).figure(QString::number(k.readingsUsed))
      .append(QStringLiteral(" readings at once, the ") + words::word(x.n) + QStringLiteral(" shots fall into two groups"));
    if (k.method == KindsMethod::KMedoids) st.append(QStringLiteral(" (silhouette ")).figure(r2(k.silhouette)).append(QStringLiteral(")"));
    else if (k.splitReading >= 0) st.append(QStringLiteral(" split on ") + x.readingText(k.splitReading));
    st.append(QStringLiteral(", and ")).figure(QString::number(agree)).append(QStringLiteral(" of the ")).figure(QString::number(grouped))
      .append(QStringLiteral(" grouped shots sit with their own club: the groups say which club was hit, not what kinds of swing there were. "
                             "Kinds across clubs are not meaningful. "));
    for (const auto &r : within.runs) { if (r.fig) st.figure(r.text); else st.append(r.text); }
    m.insert(QStringLiteral("standfirst"), st.toVariant());
    m.insert(QStringLiteral("splitLine"), Sentence().toVariant());

    // the groups ARE the clubs: shown as the clubs
    QVariantList groups;
    QString pickedClub;
    for (const FacetGroup &g : clubs.groups) {
        QVariantList members;
        for (int o : g.ordinals) {
            QVariantMap mk = x.shotMark(o - 1);
            mk.insert(QStringLiteral("picked"), o - 1 == x.pick);
            members.append(mk);
            if (o - 1 == x.pick) pickedClub = g.key.toLower();
        }
        groups.append(QVariantMap{ { QStringLiteral("name"), g.key.isEmpty() ? QStringLiteral("No club") : g.key.toLower() },
                                   { QStringLiteral("n"), int(g.ordinals.size()) }, { QStringLiteral("members"), members },
                                   { QStringLiteral("medoid"), 0 }, { QStringLiteral("medoidText"), QString() }, { QStringLiteral("neutral"), false } });
    }
    m.insert(QStringLiteral("groups"), groups);
    m.insert(QStringLiteral("unplacedCount"), 0);
    m.insert(QStringLiteral("pickedKind"), pickedClub);
    m.insert(QStringLiteral("separation"), QVariantMap{ { QStringLiteral("rows"), QVariantList{} } });
    m.insert(QStringLiteral("curves"), QVariantList{});
    m.insert(QStringLiteral("shapes"), QVariantMap{ { QStringLiteral("found"), false } });
    m.insert(QStringLiteral("alignNote"), Sentence().toVariant());
    m.insert(QStringLiteral("methodNote"), QStringLiteral("The kinds are called the clubs when each kind has a majority club, the two majorities differ, "
                                                          "and at least 75\u00A0% of the grouped shots sit with their kind's majority club "
                                                          "(\u201Care the two clubs\u201D from 90\u00A0%, \u201Cmostly\u201D below that)."));
    return m;
}

QVariantMap kindsMap(Ctx &x, const Cache::Family *fam, bool nested)
{
    const std::optional<SessionFacet> clubs = clubFacet(x.S);
    const QString clubText = clubs ? QStringLiteral("mixed clubs") : (x.n ? x.S.shots.front()->club.toLower() : QString());
    const QString kicker = QStringLiteral("Kinds of swing  ·  ") + words::word(x.n) + (x.n == 1 ? QStringLiteral(" shot") : QStringLiteral(" shots"))
                           + (clubText.isEmpty() ? QString() : QStringLiteral("  ·  ") + clubText);
    QVariantMap m;
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
            m = clubKinds(x, *clubs, cA, cB, agree, grouped);
    }
    if (m.isEmpty()) {
        m = kindsCore(x, fam);
        if (!nested && clubs && m.value(QStringLiteral("available")).toBool()) {
            const QString note = QStringLiteral("The clubs are mixed; these kinds are taken across them, and they are not the clubs.");
            m.insert(QStringLiteral("clubsNote"), note);
            QVariantMap st = m.value(QStringLiteral("standfirst")).toMap();
            QVariantList runs = st.value(QStringLiteral("runs")).toList();
            runs.append(QVariantMap{ { QStringLiteral("text"), QStringLiteral(" ") + note }, { QStringLiteral("fig"), false } });
            st.insert(QStringLiteral("runs"), runs);
            st.insert(QStringLiteral("text"), st.value(QStringLiteral("text")).toString() + QStringLiteral(" ") + note);
            m.insert(QStringLiteral("standfirst"), st);
        }
    }
    if (!m.contains(QStringLiteral("refusal"))) m.insert(QStringLiteral("refusal"), QVariantMap{});
    m.insert(QStringLiteral("kicker"), kicker);
    return m;
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
    QVariantList phaseLinks;
    for (const SessionReading &r : rs)
        phaseLinks.append(QVariantMap{ { QStringLiteral("phase"), r.phase }, { QStringLiteral("tag"), x.v.tag(r.phase) },
                                       { QStringLiteral("seekUs"), phaseUsOf(ps, r.phase) }, { QStringLiteral("current"), false } });
    const double rc = rCritical(x.n);
    for (size_t e = 0; e < rs.size(); ++e) {
        const SessionReading &r = rs[e];
        QVariantMap m;
        m.insert(QStringLiteral("readingId"), r.id);
        m.insert(QStringLiteral("key"), r.key);
        m.insert(QStringLiteral("label"), r.label);
        m.insert(QStringLiteral("tag"), x.v.tag(r.phase));
        m.insert(QStringLiteral("phaseName"), x.v.name(r.phase));
        m.insert(QStringLiteral("head"), Sentence().append(r.label + QStringLiteral(" at ") + x.v.tag(r.phase) + QStringLiteral(", ")
                                                           + x.v.name(r.phase).toLower() + QStringLiteral(", every shot")).toVariant());
        m.insert(QStringLiteral("follows"), QStringLiteral("follows the video"));
        QVariantList links = phaseLinks;
        for (QVariant &l : links) { QVariantMap lm = l.toMap(); lm.insert(QStringLiteral("current"), lm.value(QStringLiteral("phase")).toInt() == r.phase); l = lm; }
        m.insert(QStringLiteral("phases"), links);
        m.insert(QStringLiteral("run"), runMap(x, r));
        const ReadingPlace p = placeOf(r, x.pick);
        Sentence now;
        if (p.measured) {
            now.append(QStringLiteral("This shot ")).figure(figU(p.value, r.unit)).append(QStringLiteral("; typical ")).figure(figU(r.box.med, r.unit))
               .append(QStringLiteral(", the session median"));
            const QString pl = placeWords(p, r.box.n);
            if (!pl.isEmpty()) now.append(QStringLiteral("; ") + pl);
            now.append(QStringLiteral("."));
        }
        // no value on this shot: said plainly; why is said once, in the panel's tail (Mark, 17 Oct 2026)
        if (!p.measured) {
            now.append(QStringLiteral("Not measured on this shot; typical "));
            now.figure(figU(r.box.med, r.unit)).append(QStringLiteral(", the session median."));
        }
        m.insert(QStringLiteral("now"), QVariantMap{ { QStringLiteral("valueText"), figU(p.measured ? p.value : kNaN, r.unit) },
                                                     { QStringLiteral("trustSubject"), QString() },
                                                     { QStringLiteral("medianText"), figU(r.box.med, r.unit) },
                                                     { QStringLiteral("place"), placeWords(p, r.box.n) }, { QStringLiteral("sentence"), now.toVariant() } });
        // partners: every other key's reading against this one
        std::vector<CorrRow> partners;
        for (size_t i = 0; i < x.c.readings.size(); ++i) {
            if (x.c.readings[i].key == key) continue;
            const Correlation c = pearson(x.c.readings[i].values, r.values);
            if (c.defined) partners.push_back(CorrRow{ int(i), sameQuantityAs(x.c.readings[i].key, key), c });
        }
        std::stable_sort(partners.begin(), partners.end(), [](const CorrRow &a, const CorrRow &b) { return std::abs(a.c.r) > std::abs(b.c.r); });
        QVariant partner = QVariant::fromValue(nullptr);
        for (const CorrRow &pr : partners) if (!pr.same) {
            const SessionReading &pr_r = x.r(pr.reading);
            partner = QVariantMap{ { QStringLiteral("readingId"), pr_r.id }, { QStringLiteral("label"), pr_r.label }, { QStringLiteral("tag"), x.v.tag(pr_r.phase) },
                                   { QStringLiteral("rText"), r2(pr.c.r) }, { QStringLiteral("scatter"), scatterMap(x, pr_r, r, {}, {}) } };
            break;
        }
        m.insert(QStringLiteral("partner"), partner);
        QVariantList mw;
        for (size_t i = 0; i < partners.size() && i < 3; ++i) {
            const CorrRow &pr = partners[i];
            const SessionReading &q = x.r(pr.reading);
            Sentence s;
            s.append(q.label + QStringLiteral(" at ") + x.v.tag(q.phase) + (pr.same ? QStringLiteral(" ‡") : QString()) + QStringLiteral(": r "))
             .figure(r2(pr.c.r)).append(QStringLiteral(" (")).figure(r2(pr.c.lo)).append(QStringLiteral(" to ")).figure(r2(pr.c.hi))
             .append(QStringLiteral("), n ")).figure(QString::number(pr.c.n)).append(pr.c.clear ? QString() : QStringLiteral(", inside chance"));
            mw.append(QVariantMap{ { QStringLiteral("readingId"), q.id }, { QStringLiteral("label"), q.label }, { QStringLiteral("tag"), x.v.tag(q.phase) },
                                   { QStringLiteral("same"), pr.same }, { QStringLiteral("sameMark"), pr.same ? QStringLiteral("‡") : QString() },
                                   { QStringLiteral("r"), pr.c.r }, { QStringLiteral("rText"), r2(pr.c.r) }, { QStringLiteral("lo"), pr.c.lo }, { QStringLiteral("hi"), pr.c.hi },
                                   { QStringLiteral("ciText"), QStringLiteral("(%1 to %2)").arg(r2(pr.c.lo), r2(pr.c.hi)) }, { QStringLiteral("n"), pr.c.n },
                                   { QStringLiteral("clear"), pr.c.clear }, { QStringLiteral("sentence"), s.toVariant() } });
        }
        m.insert(QStringLiteral("movesWith"), mw);
        m.insert(QStringLiteral("note"), Sentence().append(QStringLiteral("With ")).figure(QString::number(x.n)).append(QStringLiteral(" shots an r inside ±"))
                                            .figure(r2(rc)).append(QStringLiteral(" is what chance gives. ") + words::coOccurrenceNote()).toVariant());
        Output::ReplayEntry re;
        const qint64 us = phaseUsOf(ps, r.phase);
        re.tUs = us >= 0 ? us : std::numeric_limits<qint64>::max();
        re.map = m;
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
                                                                   ChartsRelationsPresenter::Cache &cache)
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
    for (int i = 0; i < x.n; ++i) if (!picked.isEmpty() && cleanDir(S.shots[size_t(i)]->swingDir) == picked) { x.pick = i; break; }

    int pickIdx = -1;
    for (int i = 0; i < S.count(); ++i) if (!picked.isEmpty() && cleanDir(S.shots[size_t(i)]->swingDir) == picked) { pickIdx = i; break; }
    const int fi = familyIndex(cache, family, reg, cache.solo ? std::min(pickIdx, 0) : pickIdx);
    const ChartsRelationsPresenter::Cache::Family *fam = fi >= 0 ? &cache.families[size_t(fi)] : nullptr;

    // status
    QVariantMap st;
    const ShotSeries *ps = x.pick >= 0 ? S.shots[size_t(x.pick)].get() : nullptr;
    const std::optional<SessionFacet> club = clubFacet(S);
    QString clubText = ps ? ps->club.toLower() : (x.n ? S.shots.front()->club.toLower() : QString());
    st.insert(QStringLiteral("ready"), true);
    st.insert(QStringLiteral("shotCount"), x.n);
    st.insert(QStringLiteral("pickedOrdinal"), ps ? ps->ordinal : 0);
    st.insert(QStringLiteral("pickedShotId"), ps ? ps->shotId : -1);
    st.insert(QStringLiteral("pickedSwingDir"), ps ? ps->swingDir : QString());
    st.insert(QStringLiteral("pickedInSession"), ps && cache.augmentedDir.isEmpty());
    st.insert(QStringLiteral("pickedFromDetail"), ps && !cache.augmentedDir.isEmpty());
    st.insert(QStringLiteral("pickedImpactUs"), ps ? ps->impactUs : -1);
    st.insert(QStringLiteral("club"), club ? QStringLiteral("mixed clubs") : clubText);
    st.insert(QStringLiteral("clubsMixed"), bool(club));
    QString clubs;
    if (club) {
        QStringList parts;
        for (const FacetGroup &g : club->groups)
            parts << (g.key.isEmpty() ? QStringLiteral("no club") : g.key.toLower()) + QStringLiteral(" on %1").arg(words::word(int(g.ordinals.size())));
        clubs = QStringLiteral("The clubs are mixed: ") + words::list(parts) + QStringLiteral("; every shot is shown together unless split by club.");
    }
    st.insert(QStringLiteral("clubsSentence"), clubs);
    st.insert(QStringLiteral("kicker"), ps ? QStringLiteral("Shot %1 of %2").arg(ps->ordinal).arg(x.n) + (clubText.isEmpty() || club ? QString() : QStringLiteral("  ·  ") + clubText)
                                           : QStringLiteral("%1 shots").arg(x.n));
    st.insert(QStringLiteral("family"), fam ? fam->name : QString());
    st.insert(QStringLiteral("readingCount"), int(cache.readings.size()));
    QVariantList shots;
    for (const auto &sh : S.shots)
        shots.append(QVariantMap{ { QStringLiteral("ordinal"), sh->ordinal }, { QStringLiteral("shotId"), sh->shotId },
                                  { QStringLiteral("swingDir"), sh->swingDir }, { QStringLiteral("club"), sh->club } });
    st.insert(QStringLiteral("shots"), shots);
    // the profile's column heads (ProfileTable: "Shot 8" over the value, "15 shots, low to high" over the box)
    st.insert(QStringLiteral("shotLabel"), ps ? QStringLiteral("Shot %1").arg(ps->ordinal) : QString());
    st.insert(QStringLiteral("countLabel"), QStringLiteral("%1 shots, low to high").arg(x.n));
    st.insert(QStringLiteral("countLabelShort"), QStringLiteral("%1 shots").arg(x.n));
    st.insert(QStringLiteral("refused"), x.n < 3);
    st.insert(QStringLiteral("refusal"), x.n < 3 ? tooFew(x, QStringLiteral("to draw the session"), 3).toVariant() : QVariant(QVariantMap{}));
    out->status = st;

    for (size_t i = 0; i < cache.families.size(); ++i) {
        const auto &f = cache.families[i];
        out->families.append(QVariantMap{ { QStringLiteral("name"), f.name }, { QStringLiteral("count"), int(f.readings.size()) },
                                          { QStringLiteral("note"), f.name == kSeqFamily ? QStringLiteral("peak times")
                                                                    : QStringLiteral("%1 %2").arg(f.readings.size()).arg(f.readings.size() == 1 ? QStringLiteral("reading") : QStringLiteral("readings")) },
                                          { QStringLiteral("preset"), f.preset }, { QStringLiteral("current"), int(i) == fi },
                                          { QStringLiteral("sequence"), f.name == kSeqFamily } });
    }

    out->thisShot = thisShotMap(x, fam, out.get());
    // the open row: its curve on this shot's clock, or its run when it has none
    const int oi = cache.indexOf(open);
    if (oi >= 0 && x.n >= 3) {
        const QVariantMap row = rowMap(x, oi);
        const bool trace = row.value(QStringLiteral("opens")).toString() == QLatin1String("trace");
        QVariantMap tr;
        if (trace) { tr = traceMap(x, x.r(oi), &out->openCurve); out->openUnit = x.r(oi).unit; }
        out->openRow = { { QStringLiteral("readingId"), x.r(oi).id }, { QStringLiteral("opens"), trace && !tr.isEmpty() ? QStringLiteral("trace") : QStringLiteral("run") },
                         { QStringLiteral("trace"), tr.isEmpty() ? QVariant::fromValue(nullptr) : QVariant(tr) }, { QStringLiteral("run"), runMap(x, x.r(oi)) } };
    }
    out->sequence = sequenceMap(x);
    out->relations = relationsMap(x, up, across, facet, nullptr);
    out->kinds = kindsMap(x, fam);
    out->replay = replayEntries(x, fam);
    if (ps) { out->pickedImpactUs = ps->impactUs; out->pickedPhases = ps->phases; }
    out->ready = true;
    out->jobMs = double(timer.nsecsElapsed()) / 1e6;
    return out;
}

// ── the QObject ───────────────────────────────────────────────────────────────────────────

ChartsRelationsPresenter::ChartsRelationsPresenter(QObject *parent)
    : QObject(parent), m_cache(std::make_shared<Cache>())
{
    m_pool.setMaxThreadCount(1);
}

ChartsRelationsPresenter::~ChartsRelationsPresenter()
{
    m_pool.waitForDone();
}

#define PP_SET(member, value, sig, recompute) \
    do { if (member == value) return; member = value; emit sig(); if (recompute) schedule(); } while (0)

void ChartsRelationsPresenter::setModel(QObject *m)
{
    if (m_model == m) return;
    if (m_model) disconnect(m_model, nullptr, this, nullptr);
    m_model = m;
    if (auto *sm = qobject_cast<SessionSeriesModel *>(m))
        connect(sm, &SessionSeriesModel::snapshotChanged, this, &ChartsRelationsPresenter::onSnapshotChanged);
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
    if (m_directSnapshot) return m_directSnapshot;
    if (auto *sm = qobject_cast<SessionSeriesModel *>(m_model.data())) return sm->snapshot();
    return nullptr;
}

void ChartsRelationsPresenter::schedule()
{
    if (m_running) { m_dirty = true; return; }
    start();
}

void ChartsRelationsPresenter::start()
{
    m_dirty = false;
    const auto snap = currentSnapshot();
    const QString picked = m_picked, reg = m_register, family = m_family, up = m_upId, across = m_acrossId, facet = m_facetId, open = m_openId;
    const QVariantMap detail = m_pickedDetail;
    const ReadAt readAt = m_readAt;
    const int gen = m_generation;
    const pa::GradePolicy gp = pa::gradePolicyByName(m_gradePolicy);
    const double faultZ = gp.idealMaxZ > 0.0 ? gp.watchMaxZ / gp.idealMaxZ : 3.0;
    const auto norms = pinpoint::analysis::sharedNormProvider();
    auto cache = m_cache;
    if (m_synchronous) {
        m_pool.waitForDone();
        deliver(computeRelations(snap, picked, detail, reg, family, up, across, facet, open, readAt, gen, faultZ, gp, norms, *cache));
        return;
    }
    m_running = true;
    emit busyChanged();
    m_pool.start([this, snap, picked, detail, reg, family, up, across, facet, open, readAt, gen, faultZ, gp, norms, cache]() {
        auto out = computeRelations(snap, picked, detail, reg, family, up, across, facet, open, readAt, gen, faultZ, gp, norms, *cache);
        QMetaObject::invokeMethod(this, [this, out]() {
            m_running = false;
            deliver(out);
            if (m_dirty) start(); else emit busyChanged();
        }, Qt::QueuedConnection);
    });
}

void ChartsRelationsPresenter::deliver(std::shared_ptr<const Output> out)
{
    QElapsedTimer t;
    t.start();
    auto o = std::const_pointer_cast<Output>(out);
    QVariantMap st = o->status;
    m_out = o;
    m_replayIndex = -2;          // force the playhead outputs
    emit resultsChanged();
    updatePlayhead(true);
    QVariantMap tm{ { QStringLiteral("jobMs"), o->jobMs }, { QStringLiteral("applyMs"), double(t.nsecsElapsed()) / 1e6 } };
    st.insert(QStringLiteral("timings"), tm);
    o->status = st;              // read on this thread only
    if (o->jobMs > 250.0)
        ppInfo() << "[Charts] relations recompute" << qRound(o->jobMs) << "ms for" << o->status.value(QStringLiteral("shotCount")).toInt() << "shots";
}

bool ChartsRelationsPresenter::ready() const { return m_out && m_out->ready; }
QVariantMap ChartsRelationsPresenter::status() const { return m_out ? m_out->status : QVariantMap{}; }
QVariantList ChartsRelationsPresenter::families() const { return m_out ? m_out->families : QVariantList{}; }
QVariantMap ChartsRelationsPresenter::thisShot() const { return m_out ? m_out->thisShot : QVariantMap{}; }
QVariantMap ChartsRelationsPresenter::openRow() const { return m_out ? m_out->openRow : QVariantMap{}; }
QVariantMap ChartsRelationsPresenter::sequence() const { return m_out ? m_out->sequence : QVariantMap{}; }
QVariantMap ChartsRelationsPresenter::relations() const { return m_out ? m_out->relations : QVariantMap{}; }
QVariantMap ChartsRelationsPresenter::kinds() const { return m_out ? m_out->kinds : QVariantMap{}; }

QVariantMap ChartsRelationsPresenter::replaySession() const
{
    if (!m_out || m_replayIndex < 0 || size_t(m_replayIndex) >= m_out->replay.size()) return {};
    return m_out->replay[size_t(m_replayIndex)].map;
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

    QVariantMap ph;
    const double ms = (m_out->pickedImpactUs >= 0 && m_playheadUs >= 0) ? double(m_playheadUs - m_out->pickedImpactUs) / 1000.0 : kNaN;
    const auto read = [&](const TraceCurve &c, const QString &unit) {
        if (!fin(ms) || c.empty()) return QString();
        const double v = valueAt(c, ms);
        return fin(v) ? figU(v, unit) : QString();
    };
    ph.insert(QStringLiteral("openValueText"), read(m_out->openCurve, m_out->openUnit));
    ph.insert(QStringLiteral("leadValueText"), read(m_out->leadCurve, m_out->leadUnit));
    ph.insert(QStringLiteral("replayIndex"), idx);
    const bool changed = force || replayMoved || ph != m_playheadMap;
    m_playheadMap = ph;
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

QVariantList ChartsRelationsPresenter::ticks(double lo, double hi, int maxTicks, const QString &unit) const
{
    return axisTicks(lo, hi, maxTicks, unit);
}

QVariantList ChartsRelationsPresenter::axisTicks(double lo, double hi, int maxTicks, const QString &unit)
{
    QVariantList out;
    if (!fin(lo) || !fin(hi)) return out;
    if (!(hi > lo)) { out.append(QVariantMap{ { QStringLiteral("v"), lo }, { QStringLiteral("text"), figBare(lo, shortUnit(unit)) } }); return out; }
    const double raw = (hi - lo) / std::max(1, maxTicks), p = std::pow(10.0, std::floor(std::log10(raw))), f = raw / p;
    const double st = (f <= 1.5 ? 1 : f <= 3 ? 2 : f <= 7 ? 5 : 10) * p;
    const int dec = std::max(words::decimalsFor(shortUnit(unit)), st >= 1 ? 0 : int(std::ceil(-std::log10(st) - 1e-9)));
    for (double t = std::ceil(lo / st - 1e-9) * st; t <= hi + 1e-9 && out.size() < 64; t += st) {
        const double v = std::abs(t) < st * 1e-6 ? 0.0 : t;
        out.append(QVariantMap{ { QStringLiteral("v"), v }, { QStringLiteral("text"), words::fig(v, dec) } });
    }
    return out;
}

bool ChartsRelationsPresenter::waitForIdle(int msTimeout)
{
    if (m_synchronous) return true;
    QDeadlineTimer deadline(msTimeout);
    while ((m_running || m_dirty) && !deadline.hasExpired()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        m_pool.waitForDone(20);
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return !m_running && !m_dirty;
}

// ── What moves with one reading (the Wrist tab's "what moves with the lead wrist at impact") ──────

QVariantMap ChartsRelationsPresenter::movesWith(const std::shared_ptr<const SessionSnapshot> &snapshot,
                                                const QString &readingId, int k, int lmK, const ReadAt &readAt)
{
    QVariantMap m;
    const auto empty = std::make_shared<SessionSnapshot>();
    Cache cache;
    ensureSession(cache, snapshot ? snapshot : empty, QString(), {}, readAt, 0, false);
    Vocab vocab;
    Ctx x{ cache, *cache.snap, vocab };
    x.n = cache.snap->count();
    // the Relations tab's own refusal: r needs five shots both readings were read on
    if (x.n < 5 || cache.readings.size() < 2) {
        m.insert(QStringLiteral("available"), false);
        m.insert(QStringLiteral("refusal"), (x.n < 5 ? tooFew(x, QStringLiteral("to relate readings across a session"), 5)
                                                     : Sentence().append(QStringLiteral("This session has too few readings to relate."))).toVariant());
        return m;
    }
    const int y = cache.indexOf(readingId);
    if (y < 0) {
        m.insert(QStringLiteral("available"), false);
        m.insert(QStringLiteral("refusal"), Sentence().append(QStringLiteral("This session does not read ") + readingId
                                                              + QStringLiteral(" on three shots or more.")).toVariant());
        return m;
    }
    const SessionReading &Y = x.r(y);
    const std::vector<CorrRow> &rows = cache.corrOf(y);
    // the top k readings that are not the same quantity, then the top lmK launch-monitor readings not
    // already among them — each the Relations ledger's own row
    QVariantList out;
    std::vector<int> taken;
    int rank = 0;
    for (const CorrRow &r : rows) {
        if (r.same) continue;
        ++rank;
        if (int(taken.size()) >= std::max(0, k)) break;
        taken.push_back(r.reading);
        out.append(ledgerRow(x, r, Y, rank));
    }
    int lm = 0;
    rank = 0;
    for (const CorrRow &r : rows) {
        if (r.same) continue;
        ++rank;
        if (lm >= std::max(0, lmK)) break;
        if (!x.r(r.reading).key.startsWith(QLatin1String("lm."))) continue;
        if (std::find(taken.begin(), taken.end(), r.reading) != taken.end()) continue;
        out.append(ledgerRow(x, r, Y, rank));
        ++lm;
    }
    const int tests = int(rows.size()), nY = Y.box.n;
    int clear = 0;
    for (const CorrRow &r : rows) if (r.c.clear) ++clear;
    const double rc = rCritical(nY);
    m.insert(QStringLiteral("available"), true);
    m.insert(QStringLiteral("upId"), Y.id);
    m.insert(QStringLiteral("up"), briefMap(x, y));
    m.insert(QStringLiteral("head"), Sentence().append(QStringLiteral("What moves with ") + x.readingText(y)).toVariant());
    m.insert(QStringLiteral("chance"), QVariantMap{
        { QStringLiteral("n"), nY }, { QStringLiteral("tests"), tests }, { QStringLiteral("clearCount"), clear },
        { QStringLiteral("chanceCount"), chanceCount(tests) }, { QStringLiteral("rCritical"), rc }, { QStringLiteral("rCriticalText"), r2(rc) },
        { QStringLiteral("line"), Sentence().append(QStringLiteral("|r| under ")).figure(r2(rc)).append(QStringLiteral(" is chance at n = "))
                                      .figure(QString::number(nY)).toVariant() } });
    m.insert(QStringLiteral("rows"), out);
    m.insert(QStringLiteral("note"), Sentence().append(nY < 10 ? QStringLiteral("A small session. ") : QString())
                                         .append(words::coOccurrenceNote()).toVariant());
    return m;
}

} // namespace pinpoint::charts
