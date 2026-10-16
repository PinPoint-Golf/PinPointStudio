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

#include "charts_traces_presenter.h"

#include "charts_words.h"
#include "session_curves.h"
#include "session_relations.h"   // the facets (conditionFacets, halfFacet, clubFacet, shapeFacet)
#include "../../Metrics/gate_catalogue.h"   // the gates' words (the out-of-sight bars)
#include "../trust/trust_readings.h"         // the readings a gate touched (the marks open them)
#include "session_sequence.h"
#include "session_series_model.h"
#include "../../Core/pp_debug.h"
#include "../../Export/swing_store.h"   // the ★ reference swing (loadAnalysis), read on the worker

#include "../review/chart_metrics.h"
#include "../review/timeline_labels.h"

#include <QCoreApplication>
#include <QDate>
#include <QDeadlineTimer>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QHash>
#include <QJsonObject>
#include <QLocale>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <optional>

namespace pinpoint::charts {

namespace W = words;

// ── the result, and what the GUI thread keeps to answer the playhead and the pointer ───────────

struct ChartsTracesPresenter::NowRow {
    QString key, label, unit;            // unit: the series' own (figUnit closes it up)
    int dec = 0;
    std::vector<qint64> tUs;
    std::vector<double> mean, raw;
    std::vector<uint8_t> measured;
    double addr = kNaN;                  // the drawn value at Address (Δ from Address)
    bool grossWarn = false;
    std::vector<double> refT, refV;      // Against, on the picked shot's clock
    std::vector<double> medT, medV;      // the session median, canonical axis
};

struct ChartsTracesPresenter::NowCache {
    bool    hasShot = false;
    qint64  impactUs = -1;
    int     pickedOrdinal = 0;
    bool    pickedAligned = false;
    ShotWarp pickedWarp;
    std::vector<NowRow> rows;
    std::shared_ptr<const SessionSnapshot> snapshot;
    std::vector<ShotWarp> warps;         // parallel to snapshot->shots
    QHash<QString, std::shared_ptr<const std::vector<TraceCurve>>> aligned;
};

struct ChartsTracesPresenter::Result {
    QVariantMap choices, thisShot, reference, session;
    NowCache now;
    QVariantMap timings;
};

// ── the worker's caches (touched only by jobs, which never overlap) ────────────────────────────

struct ChartsTracesPresenter::Worker {
    QMutex mutex;                                    // held for a whole job
    std::unique_ptr<ChartMetrics> cm;                // built on first use, on the worker
    std::unique_ptr<TimelineLabels> tl;

    std::shared_ptr<const SessionSnapshot> snap;     // the snapshot the caches below describe
    quint64 revision = 0;
    SessionAlignment alignment;
    SessionSequence sequence;
    std::vector<SessionFacet> conditionFacets;
    std::optional<SessionFacet> clubFacet;

    QHash<QString, std::shared_ptr<const std::vector<TraceCurve>>> aligned;   // key
    QHash<QString, SessionEnvelope> envelopes;       // key|ords|t0|t1
    QHash<QString, GroupFit> fits;                   // key|ords|t0|t1
    QHash<QString, ShapeSplit> shapes;               // keys|t0|t1
    QHash<QString, ShotScore> scores;                // kind|keys|t0|t1|exclude
    QHash<QString, std::vector<double>> peaks;       // key|window → every shot's windowed peak
    QHash<QString, QVariantMap> domains, specs;      // ChartMetrics, per key (catalogue reads)
    QHash<QString, bool> draws;

    // The ★ reference swing (Against "reference"), as last read: by its cleaned dir and its document's
    // stamp, so an unchanged swing is a stat per job. Not per revision: it lives outside the session.
    QString refDir;
    qint64  refSize = -1, refMtimeMs = -1;
    std::shared_ptr<const ShotSeries> refShot;
    QString refWhy;

    void ensureTools()
    {
        if (!cm) cm = std::make_unique<ChartMetrics>();
        if (!tl) tl = std::make_unique<TimelineLabels>();
    }
    const QVariantMap &domainFor(const QString &key)
    {
        auto it = domains.find(key);
        if (it == domains.end()) it = domains.insert(key, cm->domainFor(key));
        return it.value();
    }
    const QVariantMap &specFor(const QString &key)
    {
        auto it = specs.find(key);
        if (it == specs.end()) it = specs.insert(key, cm->cardSpecFor(key));
        return it.value();
    }
    bool drawsCurve(const QString &key)
    {
        auto it = draws.find(key);
        if (it == draws.end()) it = draws.insert(key, cm->drawsCurve(key));
        return it.value();
    }
    // Rebuild the per-revision caches when the snapshot moved. Returns true on a hit.
    bool refresh(const std::shared_ptr<const SessionSnapshot> &s)
    {
        const quint64 rev = s ? s->revision : 0;
        if (s.get() == snap.get() && rev == revision && (s || !snap)) return true;
        snap = s;
        revision = rev;
        aligned.clear(); envelopes.clear(); fits.clear(); shapes.clear(); scores.clear(); peaks.clear();
        alignment = SessionAlignment{};
        sequence = SessionSequence{};
        conditionFacets.clear();
        clubFacet.reset();
        if (s && s->count() > 0) {
            alignment = alignSession(*s);
            sequence = sessionSequence(*s);
            conditionFacets = charts::conditionFacets(*s);
            clubFacet = charts::clubFacet(*s);
        }
        return false;
    }
    std::shared_ptr<const std::vector<TraceCurve>> alignedOf(const QString &key)
    {
        auto it = aligned.find(key);
        if (it != aligned.end()) return it.value();
        auto v = std::make_shared<const std::vector<TraceCurve>>(
            snap ? alignedCurves(*snap, alignment, key) : std::vector<TraceCurve>{});
        aligned.insert(key, v);
        return v;
    }
};

namespace {

// ── small helpers ───────────────────────────────────────────────────────────────────────────

QVariant dl(const std::vector<double> &v) { return QVariant::fromValue(QList<double>(v.begin(), v.end())); }

QVariantList intList(const std::vector<int> &v)
{
    QVariantList out;
    for (int x : v) out << x;
    return out;
}

QString cleanDir(const QString &d) { return d.trimmed().isEmpty() ? QString() : QDir::cleanPath(d.trimmed()); }

QString asciiLower(QString s)
{
    for (QChar &c : s)
        if (c.unicode() < 128) c = c.toLower();
    return s;
}

QString segWord(int seg)
{
    static const char *const w[] = { "Pelvis", "Chest", "Lead arm", "Club" };
    return seg >= 0 && seg < kSequenceSegments ? QString::fromLatin1(w[seg]) : QString();
}

int segOfKey(const QString &key)
{
    for (int i = 0; i < kSequenceSegments; ++i) if (sequenceSeriesKey(i) == key) return i;
    return -1;
}

QString verdictOf(const QString &band)
{
    if (band == QLatin1String("good") || band == QLatin1String("green")) return QStringLiteral("good");
    if (band == QLatin1String("attention") || band == QLatin1String("yellow") || band == QLatin1String("amber"))
        return QStringLiteral("attention");
    if (band == QLatin1String("warn") || band == QLatin1String("red")) return QStringLiteral("warn");
    return QString();
}

QString verdictWord(const QString &v)
{
    if (v == QLatin1String("good")) return QStringLiteral("in range");
    if (v == QLatin1String("attention")) return QStringLiteral("watch");
    if (v == QLatin1String("warn")) return QStringLiteral("outside");
    return QStringLiteral("position, not judged");
}

// Where a value sits among a set (the mock's placeWord): "highest of 15", "top quarter", …
QString placeWord(double v, const std::vector<double> &xs)
{
    std::vector<double> b;
    for (double x : xs) if (std::isfinite(x)) b.push_back(x);
    if (!std::isfinite(v) || b.size() < 4) return QStringLiteral("-");
    const double q1 = quantile7(b, 0.25), q3 = quantile7(b, 0.75);
    const double mx = *std::max_element(b.begin(), b.end()), mn = *std::min_element(b.begin(), b.end());
    if (v >= mx) return QStringLiteral("highest of %1").arg(b.size());
    if (v <= mn) return QStringLiteral("lowest of %1").arg(b.size());
    if (v > q3) return QStringLiteral("top quarter");
    if (v < q1) return QStringLiteral("bottom quarter");
    return QStringLiteral("middle half");
}

size_t nearestIndex(const std::vector<qint64> &t, qint64 us)
{
    if (t.empty()) return 0;
    size_t lo = size_t(std::lower_bound(t.begin(), t.end(), us) - t.begin());
    if (lo >= t.size()) return t.size() - 1;
    if (lo > 0 && (us - t[lo - 1]) <= (t[lo] - us)) return lo - 1;   // ties to the earlier frame
    return lo;
}

// Linear interpolation on an ascending grid, NaN outside or where either side is NaN.
double interpAt(const std::vector<double> &t, const std::vector<double> &v, double x)
{
    if (t.size() < 2 || !std::isfinite(x) || x < t.front() || x > t.back()) return kNaN;
    const size_t i = size_t(std::upper_bound(t.begin(), t.end(), x) - t.begin());
    if (i == 0) return v.front();
    if (i >= t.size()) return v.back();
    const double a = v[i - 1], b = v[i];
    if (!std::isfinite(a) || !std::isfinite(b)) return kNaN;
    const double f = (x - t[i - 1]) / (t[i] - t[i - 1]);
    return a + f * (b - a);
}

QVariantList phasesList(const ShotSeries &s)
{
    QVariantList out;
    for (const PhaseMark &p : s.phases)
        out << QVariantMap{ { QStringLiteral("phase"), p.phase }, { QStringLiteral("t_us"), qlonglong(p.tUs) },
                            { QStringLiteral("conf"), p.conf } };
    return out;
}

qint64 phaseUsOf(const ShotSeries &s, int phase)
{
    for (const PhaseMark &p : s.phases) if (p.phase == phase) return p.tUs;
    return -1;
}

QVariantList tList(const MetricSeries &m) { QVariantList l; l.reserve(int(m.tUs.size())); for (qint64 t : m.tUs) l << qlonglong(t); return l; }
QVariantList vList(const MetricSeries &m) { QVariantList l; l.reserve(int(m.value.size())); for (double v : m.value) l << v; return l; }
QVariantList validList(const MetricSeries &m) { QVariantList l; for (uint8_t b : m.valid) l << int(b); return l; }

QVariantList phaseSamplesList(const MetricSeries &m)
{
    QVariantList out;
    for (const PhaseSample &p : m.phaseSamples)
        out << QVariantMap{ { QStringLiteral("phase"), p.phase }, { QStringLiteral("t_us"), qlonglong(p.tUs) },
                            { QStringLiteral("value"), p.value }, { QStringLiteral("band"), p.band } };
    return out;
}

// The series map seriesGroups reads (key, t_us, phaseSamples).
// analysisDetail (shotReplay) → a ShotSeries, on the same clock (both are window-relative µs).
std::shared_ptr<ShotSeries> shotFromDetail(const QVariantMap &d, QHash<QString, QVariantMap> *maps)
{
    auto sh = std::make_shared<ShotSeries>();
    for (const QVariant &pv : d.value(QStringLiteral("phases")).toList()) {
        const QVariantMap p = pv.toMap();
        PhaseMark pm;
        pm.phase = p.value(QStringLiteral("phase")).toInt();
        pm.tUs = p.value(QStringLiteral("t_us")).toLongLong();
        pm.conf = p.contains(QStringLiteral("conf")) ? p.value(QStringLiteral("conf")).toDouble() : kNaN;
        sh->phases.push_back(pm);
        if (pm.phase == phase::Impact) sh->impactUs = pm.tUs;
    }
    if (d.contains(QStringLiteral("impactUs")) && sh->impactUs < 0)
        sh->impactUs = d.value(QStringLiteral("impactUs")).toLongLong();
    sh->kinematicSequence = d.value(QStringLiteral("kinematicSequence")).toMap();
    sh->gatesVersion = d.value(QStringLiteral("gatesVersion")).toInt();
    sh->gates = d.value(QStringLiteral("gates")).toList();
    for (const QVariant &sv : d.value(QStringLiteral("series")).toList()) {
        const QVariantMap s = sv.toMap();
        MetricSeries m;
        m.key = s.value(QStringLiteral("key")).toString();
        if (m.key.isEmpty()) continue;
        m.label = s.value(QStringLiteral("label")).toString();
        m.unit = s.value(QStringLiteral("unit")).toString();
        for (const QVariant &t : s.value(QStringLiteral("t_us")).toList()) m.tUs.push_back(t.toLongLong());
        for (const QVariant &v : s.value(QStringLiteral("value")).toList()) {
            bool ok = false;
            const double x = v.toDouble(&ok);
            m.value.push_back(ok && !v.isNull() ? x : kNaN);
        }
        bool allValid = true;
        for (const QVariant &v : s.value(QStringLiteral("valid")).toList()) {
            const uint8_t b = v.toInt() != 0 ? 1 : 0;
            m.valid.push_back(b);
            allValid = allValid && b;
        }
        if (allValid) m.valid.clear();
        m.sigma = s.contains(QStringLiteral("sigma")) ? s.value(QStringLiteral("sigma")).toDouble() : kNaN;
        m.sigmaKind = s.contains(QStringLiteral("sigmaKind")) ? s.value(QStringLiteral("sigmaKind")).toInt() : -1;
        for (const QVariant &pv : s.value(QStringLiteral("phaseSamples")).toList()) {
            const QVariantMap p = pv.toMap();
            PhaseSample ps;
            ps.phase = p.value(QStringLiteral("phase")).toInt();
            ps.tUs = p.value(QStringLiteral("t_us")).toLongLong();
            ps.value = p.value(QStringLiteral("value"), kNaN).toDouble();
            ps.sigma = p.contains(QStringLiteral("sigma")) ? p.value(QStringLiteral("sigma")).toDouble() : kNaN;
            ps.band = p.value(QStringLiteral("band")).toString();
            m.phaseSamples.push_back(ps);
        }
        sh->metricOrder << m.key;
        if (maps) maps->insert(m.key, s);
        sh->metrics.insert(m.key, std::move(m));
    }
    return sh;
}

// A warp for a shot the session alignment did not see (a live shot arriving, another session's):
// its P-positions against the session's canonical ladder, impact the origin, walked outward from
// impact keeping only anchors that move strictly away from it on both axes and pass the confidence
// gate. The session gates' outlier test needs the session's segments, which a lone shot cannot
// contribute to; the monotone walk keeps the map a map.
ShotWarp warpAgainst(const ShotSeries &s, const SessionAlignment &al)
{
    if (s.impactUs < 0) return ShotWarp{};
    std::vector<WarpAnchor> before, after;
    const std::vector<int> &ladder = pLadder();
    const auto canonOf = [&](int ph) -> const CanonicalPhase * { return al.canonicalOf(ph); };
    const auto shotMsOf = [&](int ph, double *conf) -> double {
        for (const PhaseMark &p : s.phases)
            if (p.phase == ph) { *conf = p.conf; return double(p.tUs - s.impactUs) / 1000.0; }
        return kNaN;
    };
    const auto it = std::find(ladder.begin(), ladder.end(), int(phase::Impact));
    const long imp = long(it - ladder.begin());
    double lastS = 0.0, lastC = 0.0;
    for (long i = imp - 1; i >= 0; --i) {
        const CanonicalPhase *c = canonOf(ladder[size_t(i)]);
        double conf = kNaN;
        const double ms = shotMsOf(ladder[size_t(i)], &conf);
        if (!c || !std::isfinite(ms) || (std::isfinite(conf) && conf < al.options.minConf)) continue;
        if (!(ms < lastS) || !(c->tMs < lastC)) continue;
        before.push_back(WarpAnchor{ ladder[size_t(i)], Landmark::None, ms, c->tMs });
        lastS = ms; lastC = c->tMs;
    }
    lastS = 0.0; lastC = 0.0;
    for (size_t i = size_t(imp) + 1; i < ladder.size(); ++i) {
        const CanonicalPhase *c = canonOf(ladder[i]);
        double conf = kNaN;
        const double ms = shotMsOf(ladder[i], &conf);
        if (!c || !std::isfinite(ms) || (std::isfinite(conf) && conf < al.options.minConf)) continue;
        if (!(ms > lastS) || !(c->tMs > lastC)) continue;
        after.push_back(WarpAnchor{ ladder[i], Landmark::None, ms, c->tMs });
        lastS = ms; lastC = c->tMs;
    }
    std::vector<WarpAnchor> an(before.rbegin(), before.rend());
    an.push_back(WarpAnchor{ phase::Impact, Landmark::None, 0.0, 0.0 });
    an.insert(an.end(), after.begin(), after.end());
    ShotWarp w = makeWarp(an, al.options.interp);
    w.ordinal = s.ordinal;
    w.impactUs = s.impactUs;
    w.route = an.size() > 1 ? AlignRoute::PAnchors : AlignRoute::ImpactShift;
    return w;
}

// The ★ reference on the PICKED SHOT'S OWN CLOCK: an alignment whose "canonical" P-times are the picked
// shot's own (ms from impact, its low-confidence P-positions left out by the same gate), so warpAgainst
// maps the reference's clock straight onto the picked shot's — the session's ladder, gates and outward
// walk from impact, and no session needed (a reference from another session, a session of one).
ShotWarp warpOnto(const ShotSeries &ref, const ShotSeries &picked)
{
    if (picked.impactUs < 0 || ref.impactUs < 0) return ShotWarp{};
    SessionAlignment al;
    const std::vector<int> &ladder = pLadder();
    for (const PhaseMark &p : picked.phases) {
        if (p.phase == phase::Impact || std::find(ladder.begin(), ladder.end(), p.phase) == ladder.end()) continue;
        if (std::isfinite(p.conf) && p.conf < al.options.minConf) continue;
        if (al.canonicalOf(p.phase)) continue;
        CanonicalPhase cp;
        cp.phase = p.phase;
        cp.tMs = double(p.tUs - picked.impactUs) / 1000.0;
        cp.n = 1;
        al.canonical.push_back(cp);
    }
    return warpAgainst(ref, al);
}

// "shot 3, 18 Aug 2026": which swing the ★ is, from its folder and its session folder's date.
QString referenceLabelOf(const QString &dir)
{
    const QFileInfo fi(dir);
    const int id = SessionSeriesModel::folderShotId(dir);
    const QString session = QFileInfo(fi.absolutePath()).fileName();
    const QDate d = QDate::fromString(session.left(10), QStringLiteral("yyyy-MM-dd"));
    QStringList parts;
    if (id >= 0) parts << QStringLiteral("shot %1").arg(id);
    if (d.isValid()) parts << QLocale(QLocale::English).toString(d, QStringLiteral("d MMM yyyy"));
    return parts.join(QStringLiteral(", "));
}

// The reference swing, read on the worker: the session's own copy when it is one of the session's
// shots, else SwingStore::loadAnalysis → SessionSeriesModel::extractShot (the loader's own path, never
// the pose), cached by the document's stamp. Null with *why when it cannot be read.
std::shared_ptr<const ShotSeries> referenceShot(ChartsTracesPresenter::Worker &w, const QString &rawDir,
                                                const std::shared_ptr<const SessionSnapshot> &snap, QString *why)
{
    const QString dir = cleanDir(rawDir);
    if (dir.isEmpty()) { *why = QStringLiteral("No reference swing is set: ★ marks one, and it stays your reference across sessions."); return {}; }
    if (snap)
        for (const auto &s : snap->shots) if (cleanDir(s->swingDir) == dir) return s;
    const pinpoint::SwingStore::DocInfo info = pinpoint::SwingStore::info(dir);
    if (dir == w.refDir && info.size == w.refSize && info.mtimeMs == w.refMtimeMs) { *why = w.refWhy; return w.refShot; }
    w.refDir = dir; w.refSize = info.size; w.refMtimeMs = info.mtimeMs;
    w.refShot.reset(); w.refWhy.clear();
    if (!info.exists()) {
        w.refWhy = QStringLiteral("The reference swing is no longer on disk.");
    } else {
        QString err, whyNot;
        const QJsonObject root = pinpoint::SwingStore::loadAnalysis(dir, &err);
        auto shot = root.isEmpty() ? nullptr : SessionSeriesModel::extractShot(root, dir, &whyNot);
        if (shot) {
            shot->shotId = SessionSeriesModel::folderShotId(dir);
            w.refShot = shot;
        } else {
            w.refWhy = QStringLiteral("The reference swing cannot be read: ") + (whyNot.isEmpty() ? QStringLiteral("the swing document could not be read") : whyNot) + QStringLiteral(".");
            if (!err.isEmpty()) ppWarn() << "[Charts] reference" << dir << err;
        }
    }
    *why = w.refWhy;
    return w.refShot;
}

// ── the window ──────────────────────────────────────────────────────────────────────────────

struct Win {
    QString key, label;
    qint64 startUs = 0, endUs = 0;
    int phaseA = -1, phaseB = -1;
    bool swing = false, data = false;
};

struct Ctx;   // forward

// ── one series of This shot, resolved once: the drawn line, the masks, the window summary ─────

struct SeriesCalc {
    const MetricSeries *m = nullptr;
    QVariantMap seriesMap;               // for sigmaInfo (grossRisk rides on the detail's samples)
    QVariantMap spec;
    bool hasCurve = false;
    std::vector<double> mean;
    std::vector<uint8_t> measured, inDomain;
    std::vector<uint8_t> validSample;    // a measurement at all: finite and not bridged (domain aside)
    std::vector<double> meanDrawn, rawDrawn;   // NaN where !validSample: a bridged value is not drawn
    qint64 fromUs = 0, toUs = 0;
    QVariantList tl, vl, mask, phaseSamples;
    QVariantMap st;                      // summaryMasked over the domain-clamped window
    bool collapsed = false, valueOk = false, rateOk = false, winPeakOk = false;
    qint64 dStart = 0, dEnd = 0;
    int dec = 0;
};

struct Ctx {
    ChartsTracesPresenter::Worker *w = nullptr;
    const ChartsTracesPresenter::Inputs *in = nullptr;
    std::shared_ptr<const SessionSnapshot> snap;
    // the picked shot
    std::shared_ptr<const ShotSeries> shot;
    QHash<QString, QVariantMap> seriesMaps;
    int index = -1, ordinal = 0, shotId = -1;
    QString swingDir;
    ShotWarp warp;
    bool aligned = false;
    ShotSequence seq;
    // the ★ reference swing (Against "reference"): the shot, why not when null, and its clock mapped
    // onto the picked shot's (warpOnto)
    std::shared_ptr<const ShotSeries> ref;
    QString refWhy, refLabel;
    ShotWarp refWarp;
    bool refIsPicked = false;
    // the preset
    QVariantList groups;
    QString preset;
    QStringList presetKeys;
    QStringList rowKeys;                 // drawn on This shot (on, drawsCurve, has a curve)
    QStringList sessionKeys;             // drawn in Session (on, drawsCurve, ≥ 3 shots have the curve)
    bool sequencePreset = false;
    // the window
    Win win;
    QVariantList windowChips;
    // the session window (canonical)
    double t0 = kNaN, t1 = kNaN;
    std::vector<double> grid;
    QHash<QString, SeriesCalc> calcs;
    QString unitOf(const QString &k) const
    {
        if (shot) if (const MetricSeries *m = shot->metric(k)) return m->unit;
        if (snap) for (const auto &s : snap->shots) if (const MetricSeries *m = s->metric(k)) return m->unit;
        return QString();
    }
    QString labelOf(const QString &k) const
    {
        const QString sh = w->cm->shortLabel(k);
        if (shot) if (const MetricSeries *m = shot->metric(k)) if (!m->label.isEmpty()) return m->label;
        if (snap) for (const auto &s : snap->shots) if (const MetricSeries *m = s->metric(k)) if (!m->label.isEmpty()) return m->label;
        return sh.isEmpty() ? k : sh;
    }
    QString tag(int ph) const { return w->tl->phaseShortTag(ph); }
};

qint64 nearestSampleUs(const std::vector<qint64> &t, qint64 us)
{
    if (t.empty()) return us;
    return t[nearestIndex(t, us)];
}

SeriesCalc calcSeries(Ctx &c, const MetricSeries &m)
{
    SeriesCalc r;
    r.m = &m;
    r.dec = W::decimalsFor(m.unit);
    r.spec = c.w->specFor(m.key);
    r.seriesMap = c.seriesMaps.value(m.key);
    if (r.seriesMap.isEmpty())
        r.seriesMap = QVariantMap{ { QStringLiteral("sigmaKind"), m.sigmaKind }, { QStringLiteral("phaseSamples"), QVariantList{} } };
    r.phaseSamples = phaseSamplesList(m);
    r.hasCurve = m.hasCurve() && m.value.size() == m.tUs.size();
    if (!r.hasCurve) return r;
    const size_t n = m.tUs.size();
    // THE PHASE DOMAIN as instants, snapped to the series' own samples; a side the manifest did not
    // narrow, or a landmark the swing lacks, clips nothing (PpMetricChart._domainWindow).
    const QVariantMap d = c.w->domainFor(m.key);
    const qint64 first = m.tUs.front(), last = m.tUs.back();
    const auto phaseOr = [&](int ph, qint64 fb) { const qint64 u = phaseUsOf(*c.shot, ph); return u >= 0 ? u : fb; };
    r.fromUs = d.value(QStringLiteral("firstNarrowed")).toBool()
                   ? nearestSampleUs(m.tUs, phaseOr(d.value(QStringLiteral("firstPhase")).toInt(), first)) : first;
    r.toUs = d.value(QStringLiteral("lastNarrowed")).toBool()
                 ? nearestSampleUs(m.tUs, phaseOr(d.value(QStringLiteral("lastPhase")).toInt(), last)) : last;
    const bool haveMask = m.valid.size() >= n;      // the short-mask rule
    const bool clips = r.fromUs > first || r.toUs < last;
    r.inDomain.assign(n, 1);
    r.measured.assign(n, 1);
    r.validSample.assign(n, 1);
    bool anyBad = false;
    for (size_t i = 0; i < n; ++i) {
        const bool dom = !(r.toUs > r.fromUs) || (m.tUs[i] >= r.fromUs && m.tUs[i] <= r.toUs);
        r.inDomain[i] = dom ? 1 : 0;
        const bool ok = dom && std::isfinite(m.value[i]) && (!haveMask || m.valid[i] != 0);
        r.measured[i] = ok ? 1 : 0;
        r.validSample[i] = (std::isfinite(m.value[i]) && (!haveMask || m.valid[i] != 0)) ? 1 : 0;
        anyBad = anyBad || !ok;
    }
    r.tl = tList(m);
    r.vl = vList(m);
    // The composed mask every reduction gets (PpMetricChart._reduceMask): [] when there is nothing to say.
    if (haveMask || clips || anyBad) {
        for (size_t i = 0; i < n; ++i) r.mask << int(r.measured[i]);
    }
    if (r.spec.value(QStringLiteral("windowedMean"), true).toBool()) {
        const QVariantList mean = c.w->cm->windowedMean(r.tl, r.vl, r.mask).value(QStringLiteral("mean")).toList();
        r.mean.reserve(n);
        for (size_t i = 0; i < n; ++i) r.mean.push_back(i < size_t(mean.size()) ? mean.at(int(i)).toDouble() : m.value[i]);
    } else {
        r.mean = m.value;
    }
    // What is DRAWN: a bridged sample carries a placeholder (4 July shot 8's thorax: 0 °/s), never a
    // measurement, so the line and the dots are NaN there and the gap is published as a bridge.
    r.meanDrawn = r.mean;
    r.rawDrawn = m.value;
    for (size_t i = 0; i < n; ++i)
        if (!r.validSample[i]) { r.meanDrawn[i] = kNaN; r.rawDrawn[i] = kNaN; }
    // The window CLAMPED to the domain, ordered (PpChartSummary._winStart/_winEnd).
    const qint64 ws = c.win.startUs, we = c.win.endUs;
    r.dStart = std::max(ws, r.fromUs);
    r.dEnd = std::max(r.dStart, std::min(we, r.toUs));
    r.collapsed = r.dEnd <= r.dStart && we > ws;
    r.st = c.w->cm->summaryMasked(r.tl, r.vl, r.mask, r.dStart, r.dEnd);
    r.valueOk = r.st.value(QStringLiteral("edgeOk"), true).toBool() && !r.collapsed;
    r.rateOk = r.st.value(QStringLiteral("rateOk")).toBool() && !r.collapsed;
    r.winPeakOk = r.valueOk && r.st.value(QStringLiteral("extremumOk"), true).toBool();
    return r;
}

struct Reading { bool ok = false; double val = kNaN; qint64 us = -1; double raw = kNaN; };

Reading readingOf(const Ctx &c, const SeriesCalc &s, int ph)
{
    Reading r;
    if (!s.hasCurve) {
        for (const PhaseSample &p : s.m->phaseSamples)
            if (p.phase == ph) { r.ok = std::isfinite(p.value); r.val = p.value; r.us = p.tUs; return r; }
        return r;
    }
    qint64 us = phaseUsOf(*c.shot, ph);
    if (us < 0 && ph == phase::Impact && c.shot->impactUs > 0) us = c.shot->impactUs;
    if (us < 0) {
        if (ph == phase::Impact && s.valueOk) { r.ok = true; r.val = s.st.value(QStringLiteral("end")).toDouble(); r.us = c.win.endUs; }
        return r;
    }
    const size_t i = nearestIndex(s.m->tUs, us);
    r.ok = s.measured[i] != 0;
    r.val = s.mean[i];
    r.raw = s.m->value[i];
    r.us = us;
    return r;
}

QString readLabel(const Ctx &c, int ph, const QString &authored)
{
    if (!authored.isEmpty()) return asciiLower(authored);
    const QString name = c.w->tl->phaseFullName(ph);
    return QStringLiteral("at ") + (name.size() <= 7 ? asciiLower(name) : c.tag(ph));
}

QVariantMap tile(const QString &label, const QString &text, bool ok, const QString &verdict, const QString &sub,
                 const QString &unit, const QString &tip, bool window)
{
    return { { QStringLiteral("label"), label }, { QStringLiteral("text"), text }, { QStringLiteral("ok"), ok },
             { QStringLiteral("verdict"), verdict },
             { QStringLiteral("verdictWord"), verdict.isEmpty() ? QString() : verdictWord(verdict) },
             { QStringLiteral("sub"), sub }, { QStringLiteral("unit"), unit }, { QStringLiteral("tip"), tip },
             { QStringLiteral("window"), window } };
}

QString shortUnitOf(const Ctx &c, const QString &u) { return c.w->cm->shortUnit(u); }

// A companion's tiles (MetricCardSpec::mergeInto): its readings and a fixed-span Δ, read off ITS series.
QVariantList companionTiles(const Ctx &c, const SeriesCalc &host, const SeriesCalc &s)
{
    QVariantList out;
    const QVariantMap sp = s.spec;
    const QString nm = asciiLower(c.w->cm->shortLabel(s.m->key).isEmpty() ? (s.m->label.isEmpty() ? s.m->key : s.m->label)
                                                                         : c.w->cm->shortLabel(s.m->key));
    const QString unit = shortUnitOf(c, s.m->unit) == shortUnitOf(c, host.m->unit) ? QString() : shortUnitOf(c, s.m->unit);
    for (const QVariant &rv : sp.value(QStringLiteral("readAt")).toList()) {
        const QVariantMap ra = rv.toMap();
        const int ph = ra.value(QStringLiteral("phase")).toInt();
        const Reading r = readingOf(c, s, ph);
        const QString v = r.ok ? verdictOf(c.w->cm->bandAtNearest(s.phaseSamples, r.us)) : QString();
        const QString lab = ra.value(QStringLiteral("label")).toString();
        out << tile(lab.isEmpty() ? nm + QLatin1Char(' ') + readLabel(c, ph, QString()) : asciiLower(lab),
                    r.ok ? W::fig(r.val, s.dec) : QStringLiteral("-"), r.ok, v, QString(), r.ok ? unit : QString(),
                    QString(), false);
    }
    if (sp.value(QStringLiteral("delta")).toBool() && sp.value(QStringLiteral("deltaSpan")).toBool()) {
        const int a = sp.value(QStringLiteral("deltaFrom")).toInt(), b = sp.value(QStringLiteral("deltaTo")).toInt();
        const Reading ra = readingOf(c, s, a), rb = readingOf(c, s, b);
        const bool ok = ra.ok && rb.ok;
        const QString lab = sp.value(QStringLiteral("deltaLabel")).toString();
        out << tile(lab.isEmpty() ? QStringLiteral("Δ ") + nm + QLatin1Char(' ') + c.tag(a) + QStringLiteral("→") + c.tag(b)
                                  : asciiLower(lab),
                    ok ? W::fig(rb.val - ra.val, s.dec) : QStringLiteral("-"), ok, QString(), QString(),
                    ok ? unit : QString(), QString(), false);
    }
    return out;
}

// The card of one series (PpChartSummary's tiles, in spec order: readings, PEAK, Δ, PK RATE, companions).
QVariantMap cardOf(Ctx &c, const SeriesCalc &s, const std::vector<const SeriesCalc *> &companions)
{
    QVariantList tiles;
    const QVariantMap sp = s.spec;
    const QString unit = shortUnitOf(c, s.m->unit);
    if (!s.hasCurve) {
        for (const PhaseSample &p : s.m->phaseSamples) {
            const QString v = verdictOf(c.w->cm->bandAtNearest(s.phaseSamples, p.tUs));
            tiles << tile(readLabel(c, p.phase, QString()), W::fig(p.value, s.dec), std::isfinite(p.value), v,
                          QString(), QString(), QString(), false);
        }
    } else {
        for (const QVariant &rv : sp.value(QStringLiteral("readAt")).toList()) {
            const QVariantMap ra = rv.toMap();
            const int ph = ra.value(QStringLiteral("phase")).toInt();
            const Reading r = readingOf(c, s, ph);
            const QString v = r.ok ? verdictOf(c.w->cm->bandAtNearest(s.phaseSamples, r.us)) : QString();
            const bool hasMean = sp.value(QStringLiteral("windowedMean"), true).toBool();
            tiles << tile(readLabel(c, ph, ra.value(QStringLiteral("label")).toString()),
                          r.ok ? W::fig(r.val, s.dec) : QStringLiteral("-"), r.ok, v, QString(), QString(),
                          (r.ok && hasMean && std::isfinite(r.raw))
                              ? QStringLiteral("Drawn value (40 ms windowed mean). Recorded sample there: raw %1.")
                                    .arg(W::fig(r.raw, s.dec))
                              : QString(),
                          false);
        }
        if (sp.value(QStringLiteral("peak")).toBool()) {
            const bool span = sp.value(QStringLiteral("peakSpan")).toBool();
            QVariantMap ps = s.st;
            bool ok = s.winPeakOk;
            QString label = QStringLiteral("peak");
            if (span) {
                const int a = sp.value(QStringLiteral("peakFrom")).toInt(), b = sp.value(QStringLiteral("peakTo")).toInt();
                const qint64 ua = phaseUsOf(*c.shot, a), ub = phaseUsOf(*c.shot, b);
                ps = (ua >= 0 && ub > ua) ? c.w->cm->summaryMasked(s.tl, s.vl, s.mask, ua, ub) : QVariantMap{};
                ok = !ps.isEmpty() && ps.value(QStringLiteral("edgeOk")).toBool() && ps.value(QStringLiteral("extremumOk"), true).toBool();
                label = QStringLiteral("peak ") + c.tag(a) + QStringLiteral("→") + c.tag(b);
            }
            tiles << tile(label, ok ? W::fig(ps.value(QStringLiteral("peak")).toDouble(), s.dec) : QStringLiteral("-"), ok,
                          QString(), ok ? W::pm(ps.value(QStringLiteral("peakSigma")).toDouble(), s.dec) : QString(),
                          QString(), QString(), !span);
        }
        if (sp.value(QStringLiteral("delta")).toBool()) {
            const QString authored = sp.value(QStringLiteral("deltaLabel")).toString();
            if (sp.value(QStringLiteral("deltaSpan")).toBool()) {
                const int a = sp.value(QStringLiteral("deltaFrom")).toInt(), b = sp.value(QStringLiteral("deltaTo")).toInt();
                const Reading ra = readingOf(c, s, a), rb = readingOf(c, s, b);
                const bool ok = ra.ok && rb.ok;
                tiles << tile(authored.isEmpty() ? QStringLiteral("Δ ") + c.tag(a) + QStringLiteral("→") + c.tag(b) : asciiLower(authored),
                              ok ? W::fig(rb.val - ra.val, s.dec) : QStringLiteral("-"), ok, QString(), QString(), QString(),
                              QString(), false);
            } else {
                tiles << tile(authored.isEmpty() ? QStringLiteral("Δ segment") : asciiLower(authored),
                              s.valueOk ? W::fig(s.st.value(QStringLiteral("delta")).toDouble(), s.dec) : QStringLiteral("-"),
                              s.valueOk, QString(), QString(), QString(), QString(), true);
            }
        }
        if (sp.value(QStringLiteral("rate")).toBool()) {
            tiles << tile(QStringLiteral("pk rate"),
                          s.rateOk ? W::fig(std::abs(s.st.value(QStringLiteral("rate")).toDouble()), 0) : QStringLiteral("-"),
                          s.rateOk, QString(), s.rateOk ? W::pm(s.st.value(QStringLiteral("rateSigma")).toDouble(), 0) : QString(),
                          s.rateOk ? unit + QStringLiteral("/100 ms") : QString(), QString(), true);
        }
    }
    for (const SeriesCalc *co : companions) tiles << companionTiles(c, s, *co);
    bool partial = false;
    for (const QVariant &t : tiles)
        if (t.toMap().value(QStringLiteral("window")).toBool())
            partial = partial || s.st.value(QStringLiteral("partial")).toBool() || s.collapsed;
    const QVariantMap si = c.w->cm->sigmaInfo(s.seriesMap);
    const bool gross = si.value(QStringLiteral("warn")).toBool();
    return { { QStringLiteral("key"), s.m->key },
             { QStringLiteral("label"), c.labelOf(s.m->key) },
             { QStringLiteral("unit"), unit },
             { QStringLiteral("tiles"), tiles },
             { QStringLiteral("partial"), partial },
             { QStringLiteral("partialWord"), partial ? QStringLiteral("partial") : QString() },
             { QStringLiteral("sigmaText"), W::pm(s.m->sigma, s.dec) },
             { QStringLiteral("sigmaTip"), std::isfinite(s.m->sigma) && s.m->sigma > 0 ? si.value(QStringLiteral("tip")).toString() : QString() },
             { QStringLiteral("grossWarn"), gross },
             { QStringLiteral("grossWord"), gross ? QStringLiteral("⚠") : QString() },
             { QStringLiteral("grossTip"), si.value(QStringLiteral("warnTip")).toString() } };
}

// The dashed straight BRIDGES across runs of samples that are not measurements: from the last
// measured sample before a run to the first after it, on the curve's own axis ([{ t0, v0, t1, v1 }]).
// A run at either end of the curve has nothing to bridge to and stays a gap.
QVariantList bridgesOf(const std::vector<double> &t, const std::vector<double> &v, const std::vector<uint8_t> &ok,
                       double from = -std::numeric_limits<double>::infinity(),
                       double to = std::numeric_limits<double>::infinity())
{
    QVariantList out;
    const size_t n = std::min({ t.size(), v.size(), ok.size() });
    long last = -1;
    bool gap = false;
    for (size_t i = 0; i < n; ++i) {
        const bool good = ok[i] && std::isfinite(v[i]);
        if (!good) { if (last >= 0) gap = true; continue; }
        if (gap && t[i] >= from && t[size_t(last)] <= to)
            out << QVariantMap{ { QStringLiteral("t0"), t[size_t(last)] }, { QStringLiteral("v0"), v[size_t(last)] },
                                { QStringLiteral("t1"), t[i] }, { QStringLiteral("v1"), v[i] } };
        gap = false;
        last = long(i);
    }
    return out;
}

// Tick values with their figure text ([{ v, text }]): the decimals the tick STEP needs (0.5 → one),
// U+2212 for the minus; signed = "+50" / "−100" (a time axis about impact).
QVariantList tickTexts(const std::vector<double> &vals, bool signedText = false)
{
    double step = kNaN;
    for (size_t i = 1; i < vals.size(); ++i) {
        const double d = std::abs(vals[i] - vals[i - 1]);
        if (d > 0 && (!std::isfinite(step) || d < step)) step = d;
    }
    int dec = 0;
    if (std::isfinite(step) && step < 1.0) dec = std::min(3, int(std::ceil(-std::log10(step) - 1e-9)));
    QVariantList out;
    for (double v : vals) {
        QString t = W::fig(v, dec);
        if (signedText && t != QLatin1String("-") && std::round(v * std::pow(10.0, dec)) > 0) t.prepend(QLatin1Char('+'));
        out << QVariantMap{ { QStringLiteral("v"), v }, { QStringLiteral("text"), t } };
    }
    return out;
}

QVariantList niceTicksList(Ctx &c, double lo, double hi)
{
    return std::isfinite(lo) && std::isfinite(hi) ? c.w->cm->niceTicks(lo, hi, 4) : QVariantList{};
}

QVariantList ticksAsDoubles(const QVariantList &l)
{
    std::vector<double> v;
    for (const QVariant &x : l) v.push_back(x.toDouble());
    return tickTexts(v);
}

// ── the preset ──────────────────────────────────────────────────────────────────────────────

void resolvePreset(Ctx &c)
{
    // The groups are offered on what the picked shot AND the session measured: a key this shot lacks
    // (a curve its camera missed) still has its session curves, and This shot simply has no row for
    // it. seriesGroups asks only "a curve of two samples, or a phase sample", so one representative
    // per key is enough, cut to what that question reads (no whole series crosses into a QVariant).
    QVariantList series;
    QSet<QString> seen;
    const auto offer = [&](const ShotSeries &sh) {
        for (const QString &k : sh.metricOrder) {
            const MetricSeries *m = sh.metric(k);
            if (!m || seen.contains(k)) continue;
            if (!m->hasCurve() && m->phaseSamples.empty()) continue;
            seen.insert(k);
            QVariantList t, ps;
            if (m->hasCurve()) t << qlonglong(m->tUs[0]) << qlonglong(m->tUs[1]);
            if (!m->phaseSamples.empty()) ps << QVariantMap{ { QStringLiteral("phase"), m->phaseSamples[0].phase } };
            series << QVariantMap{ { QStringLiteral("key"), k }, { QStringLiteral("t_us"), t }, { QStringLiteral("phaseSamples"), ps } };
        }
    };
    if (c.shot) offer(*c.shot);
    if (c.snap) for (const auto &sh : c.snap->shots) offer(*sh);
    const ShotSeries *src = c.shot.get();
    c.groups = c.w->cm->seriesGroups(series);
    QStringList names;
    for (const QVariant &g : c.groups) names << g.toMap().value(QStringLiteral("group")).toString();
    const QString def = c.in->reg == QLatin1String("replay") ? QStringLiteral("Club delivery") : QStringLiteral("Kinematic sequence");
    // THE PRESET BY DATA: the reader's saved choice when this shot carries a member of it, else the
    // register's default when it does, else the first preset it does (a swing with no club reading at
    // all opens on whatever it measured). With no shot, the session decides.
    const auto carries = [&](const QString &name) {
        if (!c.shot) return true;
        for (const QVariant &g : c.groups) {
            const QVariantMap gm = g.toMap();
            if (gm.value(QStringLiteral("group")).toString() != name) continue;
            for (const QVariant &k : gm.value(QStringLiteral("keys")).toList())
                if (const MetricSeries *m = c.shot->metric(k.toString()))
                    if (m->hasCurve() || !m->phaseSamples.empty()) return true;
        }
        return false;
    };
    c.preset.clear();
    if (names.contains(c.in->preset) && carries(c.in->preset)) c.preset = c.in->preset;
    else if (names.contains(def) && carries(def)) c.preset = def;
    else for (const QString &nm : names) if (carries(nm)) { c.preset = nm; break; }
    if (c.preset.isEmpty()) c.preset = names.contains(c.in->preset) ? c.in->preset : names.contains(def) ? def : names.value(0);
    for (const QVariant &g : c.groups)
        if (g.toMap().value(QStringLiteral("group")).toString() == c.preset)
            for (const QVariant &k : g.toMap().value(QStringLiteral("keys")).toList()) c.presetKeys << k.toString();
    for (const QString &k : c.presetKeys) {
        if (c.in->metricsOff.contains(k) || !c.w->drawsCurve(k)) continue;
        if (segOfKey(k) >= 0) c.sequencePreset = true;
        if (src) if (const MetricSeries *m = src->metric(k)) if (m->hasCurve()) c.rowKeys << k;
        int n = 0;
        if (c.snap) for (const auto &s : c.snap->shots) if (const MetricSeries *m = s->metric(k)) n += m->hasCurve() ? 1 : 0;
        if (n >= 3) c.sessionKeys << k;
    }
}

// ── the window (INVENTORY §2b: chips, Full recording, the data window, custom) ─────────────

void resolveWindow(Ctx &c)
{
    Win full;
    full.key = QStringLiteral("full");
    full.label = QStringLiteral("Full");
    if (!c.shot) { c.win = full; return; }
    qint64 maxT = 0;
    for (const MetricSeries &m : c.shot->metrics) if (!m.tUs.empty()) maxT = std::max(maxT, m.tUs.back());
    for (const PhaseMark &p : c.shot->phases) maxT = std::max(maxT, p.tUs);
    const qint64 span = c.in->spanUs > 0 ? c.in->spanUs : std::max<qint64>(1, maxT);
    full.startUs = 0;
    full.endUs = span;
    const QVariantList phases = phasesList(*c.shot);
    const QVariantList segs = c.w->cm->segments(phases, span);
    std::optional<Win> swing, data;
    std::vector<Win> pairs;
    for (const QVariant &sv : segs) {
        const QVariantMap s = sv.toMap();
        const int a = s.value(QStringLiteral("phaseA")).toInt(), b = s.value(QStringLiteral("phaseB")).toInt();
        if (a == -1) continue;
        Win w;
        w.startUs = s.value(QStringLiteral("startUs")).toLongLong();
        w.endUs = s.value(QStringLiteral("endUs")).toLongLong();
        w.phaseA = a; w.phaseB = b;
        w.label = c.tag(a) + QStringLiteral("→") + c.tag(b);
        if (s.value(QStringLiteral("swing")).toBool()) { w.key = QStringLiteral("swing"); w.swing = true; swing = w; }
        else { w.key = QStringLiteral("seg:%1-%2").arg(a).arg(b); pairs.push_back(w); }
    }
    // The data window: the P-positions enclosing the drawn curves (Mark, 2026-10-02).
    if (swing) {
        qint64 lo = std::numeric_limits<qint64>::max(), hi = std::numeric_limits<qint64>::min();
        for (const QString &k : c.rowKeys)
            if (const MetricSeries *m = c.shot->metric(k)) { lo = std::min(lo, m->tUs.front()); hi = std::max(hi, m->tUs.back()); }
        if (hi > lo) {
            const QVariantMap dw = c.w->cm->dataWindow(phases, lo, hi, swing->startUs, swing->endUs);
            if (dw.value(QStringLiteral("trimmed")).toBool()) {
                Win w;
                w.key = QStringLiteral("data"); w.data = true;
                w.startUs = dw.value(QStringLiteral("startUs")).toLongLong();
                w.endUs = dw.value(QStringLiteral("endUs")).toLongLong();
                w.phaseA = dw.value(QStringLiteral("phaseA")).toInt();
                w.phaseB = dw.value(QStringLiteral("phaseB")).toInt();
                w.label = (w.phaseA >= 0 ? c.tag(w.phaseA) : QStringLiteral("…")) + QStringLiteral("→")
                          + (w.phaseB >= 0 ? c.tag(w.phaseB) : QStringLiteral("…"));
                data = w;
            }
        }
    }
    const auto byPhases = [&](int a, int b, const QString &key, const QString &label) -> std::optional<Win> {
        const qint64 ua = phaseUsOf(*c.shot, a), ub = phaseUsOf(*c.shot, b);
        if (ua < 0 || ub <= ua) return std::nullopt;
        Win w; w.key = key; w.label = label; w.startUs = ua; w.endUs = ub; w.phaseA = a; w.phaseB = b;
        return w;
    };
    const std::optional<Win> back = byPhases(phase::Address, phase::Top, QStringLiteral("back"), QStringLiteral("backswing"));
    const std::optional<Win> down = byPhases(phase::Top, phase::P8, QStringLiteral("down"), QStringLiteral("downswing"));

    // The chips, in today's order: Full, the swing, the data window (unless a chip has its span),
    // the back- and downswing (the mock's), the adjacent pairs, Custom while it is the window.
    std::vector<Win> chips{ full };
    if (swing) chips.push_back(*swing);
    const auto sameSpan = [&](const Win &w) {
        for (const Win &x : chips) if (x.phaseA == w.phaseA && x.phaseB == w.phaseB && x.phaseA >= 0) return true;
        for (const Win &x : pairs) if (x.phaseA == w.phaseA && x.phaseB == w.phaseB) return true;
        return false;
    };
    if (data && !sameSpan(*data)) chips.push_back(*data);
    else if (data) data.reset();
    if (back && !sameSpan(*back)) chips.push_back(*back);
    if (down && !sameSpan(*down)) chips.push_back(*down);
    for (const Win &p : pairs) chips.push_back(p);

    const QString want = c.in->window.isEmpty() ? QStringLiteral("auto") : c.in->window;
    std::optional<Win> pick;
    if (want == QLatin1String("custom") && c.in->customEndUs > c.in->customStartUs && c.in->customStartUs >= 0) {
        Win w; w.key = QStringLiteral("custom"); w.label = QStringLiteral("Custom");
        w.startUs = c.in->customStartUs; w.endUs = c.in->customEndUs;
        pick = w;
    } else if (want == QLatin1String("data") && data) {
        pick = *data;
    } else {
        for (const Win &x : chips) if (x.key == want) pick = x;
        if (!pick && want == QLatin1String("data") && data) pick = *data;
        if (!pick && want.startsWith(QLatin1String("seg:"))) {
            const QStringList ab = want.mid(4).split(QLatin1Char('-'));
            if (ab.size() == 2) pick = byPhases(ab[0].toInt(), ab[1].toInt(), want,
                                                c.tag(ab[0].toInt()) + QStringLiteral("→") + c.tag(ab[1].toInt()));
        }
    }
    if (!pick) pick = data ? *data : swing ? *swing : full;      // auto
    c.win = *pick;
    if (c.win.key == QLatin1String("custom")) chips.push_back(c.win);
    for (const Win &x : chips)
        c.windowChips << QVariantMap{ { QStringLiteral("key"), x.key }, { QStringLiteral("label"), x.label },
                                      { QStringLiteral("current"), x.key == c.win.key },
                                      { QStringLiteral("startUs"), x.startUs }, { QStringLiteral("endUs"), x.endUs },
                                      { QStringLiteral("phaseA"), x.phaseA }, { QStringLiteral("phaseB"), x.phaseB } };
}

double shotMsOf(const Ctx &c, qint64 us) { return c.shot && c.shot->impactUs >= 0 ? double(us - c.shot->impactUs) / 1000.0 : double(us) / 1000.0; }

// ── THIS SHOT ───────────────────────────────────────────────────────────────────────────────

QVariantMap sequenceBlock(Ctx &c)
{
    const ShotSequence &q = c.seq;
    const QVariantMap table = c.w->cm->sequenceTable(c.shot->kinematicSequence);
    QVariantList rows;
    for (const QVariant &rv : table.value(QStringLiteral("rows")).toList()) {
        QVariantMap r = rv.toMap();
        const int seg = int(std::find(sequenceSegmentIds().begin(), sequenceSegmentIds().end(),
                                      r.value(QStringLiteral("segment")).toString()) - sequenceSegmentIds().begin());
        const bool late = r.value(QStringLiteral("outOfTurn")).toBool();
        r.insert(QStringLiteral("late"), late);
        r.insert(QStringLiteral("lateWord"), late ? QStringLiteral("late") : QString());
        r.insert(QStringLiteral("rising"), seg >= 0 && seg < kSequenceSegments && q.segments[size_t(seg)].risingAtImpact);
        rows << r;
    }
    // THE CHAIN: the placed peaks in the order they happened, the gap on each arrow, the late one worded.
    std::vector<int> placed;
    for (int i = 0; i < kSequenceSegments; ++i) if (q.segments[size_t(i)].placed) placed.push_back(i);
    std::stable_sort(placed.begin(), placed.end(), [&](int a, int b) { return q.segments[size_t(a)].shotMs < q.segments[size_t(b)].shotMs; });
    W::Sentence chain;
    QVariantList links;
    for (size_t i = 0; i < placed.size(); ++i) {
        const SegmentPeak &p = q.segments[size_t(placed[i])];
        chain.append(segWord(placed[i]) + (p.late ? QStringLiteral(", late") : QString()));
        double gap = kNaN;
        if (i + 1 < placed.size()) {
            gap = q.segments[size_t(placed[i + 1])].shotMs - p.shotMs;
            chain.append(QStringLiteral(" — ")).figure(W::figMs(gap)).append(QStringLiteral(" → "));
        }
        links << QVariantMap{ { QStringLiteral("segment"), sequenceSegmentIds()[size_t(placed[i])] },
                              { QStringLiteral("word"), segWord(placed[i]) },
                              { QStringLiteral("late"), p.late },
                              { QStringLiteral("lateWord"), p.late ? QStringLiteral("late") : QString() },
                              { QStringLiteral("gapMs"), gap },
                              { QStringLiteral("gapText"), std::isfinite(gap) ? W::figMs(gap) : QString() } };
    }
    W::Sentence note;
    note.append(QStringLiteral("Expected pelvis, chest, lead arm, club."));
    for (int i = 0; i < kSequenceSegments; ++i) {
        const SegmentPeak &p = q.segments[size_t(i)];
        if (!p.present || p.placed) continue;
        note.append(QStringLiteral(" ") + segWord(i) + QStringLiteral(" ") + unplacedWhy(p).sentence + QStringLiteral("."));
    }
    // THE REASONS, ON DEMAND (trust_and_gates_design.md §7.0): the note says each gate in short; "Why" opens
    // the readings a gate touched — in the Trust panel when it is on, else in place under the strip.
    int touched = 0;
    for (int i = 0; i < kSequenceSegments; ++i)
        if (q.segments[size_t(i)].present && !q.segments[size_t(i)].records.isEmpty()) ++touched;
    return { { QStringLiteral("verdictText"), table.value(QStringLiteral("verdictText")) },
             { QStringLiteral("verdictState"), table.value(QStringLiteral("verdictState")) },
             { QStringLiteral("anyLate"), q.anyLate },
             { QStringLiteral("chain"), chain.toVariant() },
             { QStringLiteral("links"), links },
             { QStringLiteral("note"), note.toVariant() },
             { QStringLiteral("whyText"), touched == 0 ? QString() : QStringLiteral("Why") },
             // a shot analysed before records existed (§6): say so where the "Why" would be
             { QStringLiteral("unrecordedText"), c.shot && c.shot->gatesVersion <= 0
                                                 ? QStringLiteral("Reasons not recorded for this shot; re-analyse it to see which gates fired.")
                                                 : QString() },
             { QStringLiteral("table"), rows },
             // "Sequence checks: n of m" (§10.6 E4) — the engine's own verdicts, as the ledger row
             // carries them (Coaching/sequence_checks.h). Empty when the shot has none recorded.
             { QStringLiteral("checks"), c.shot->diagnostics.sequenceChecks } };
}

QVariantMap shotRow(Ctx &c, const SeriesCalc &s, const QVariantMap &card, std::vector<int> &dotKinds)
{
    const MetricSeries &m = *s.m;
    const size_t n = m.tUs.size();
    std::vector<double> t(n);
    for (size_t i = 0; i < n; ++i) t[i] = shotMsOf(c, m.tUs[i]);
    // runs: measured / bridged (dashed) / outside the phase domain (dimmed, never dashed)
    QVariantList runs;
    const auto styleOf = [&](size_t i) {
        return !s.validSample[i] ? QStringLiteral("bridged") : !s.inDomain[i] ? QStringLiteral("outside") : QStringLiteral("measured");
    };
    size_t a = 0;
    for (size_t i = 1; i <= n; ++i)
        if (i == n || styleOf(i) != styleOf(a)) {
            runs << QVariantMap{ { QStringLiteral("from"), int(a) }, { QStringLiteral("to"), int(i - 1) }, { QStringLiteral("style"), styleOf(a) } };
            a = i;
        }
    // y over the window (the drawn line and the raw dots)
    double lo = kNaN, hi = kNaN;
    for (size_t i = 0; i < n; ++i) {
        if (m.tUs[i] < c.win.startUs || m.tUs[i] > c.win.endUs) continue;
        for (double v : { s.meanDrawn[i], s.rawDrawn[i] })
            if (std::isfinite(v)) { lo = std::isfinite(lo) ? std::min(lo, v) : v; hi = std::isfinite(hi) ? std::max(hi, v) : v; }
    }
    const QVariantList bridges = bridgesOf(t, s.meanDrawn, s.validSample);
    // P dots, measured only, with their verdict and its word
    QVariantList dots;
    for (const PhaseSample &p : m.phaseSamples) {
        if (p.tUs < c.win.startUs || p.tUs > c.win.endUs) continue;
        const size_t i = nearestIndex(m.tUs, p.tUs);
        if (!s.measured[i] || !std::isfinite(p.value)) continue;
        const QString v = verdictOf(p.band);
        const int kind = v == QLatin1String("good") ? 0 : v == QLatin1String("attention") ? 1 : v == QLatin1String("warn") ? 2 : 3;
        if (std::find(dotKinds.begin(), dotKinds.end(), kind) == dotKinds.end()) dotKinds.push_back(kind);
        dots << QVariantMap{ { QStringLiteral("phase"), p.phase }, { QStringLiteral("tag"), c.tag(p.phase) },
                             { QStringLiteral("shotMs"), shotMsOf(c, p.tUs) }, { QStringLiteral("value"), p.value },
                             { QStringLiteral("verdict"), v }, { QStringLiteral("verdictWord"), verdictWord(v) } };
    }
    // peaks: the sequence node on a sequence curve, else the window's peak
    QVariantList peaks, figures;
    QVariantMap seqInfo;
    const int seg = segOfKey(m.key);
    // A value ON THE DRAWN LINE: the mean at a measured sample, else on the bridge across the gap
    // (a sequence peak inside a bridged run sits on the dashed bridge, never on a placeholder).
    const auto meanAt = [&](double ms) {
        const qint64 us = c.shot->impactUs + qint64(std::llround(ms * 1000.0));
        const size_t i = nearestIndex(m.tUs, us);
        if (s.validSample[i]) return s.mean[i];
        for (const QVariant &bv : bridges) {
            const QVariantMap b = bv.toMap();
            const double t0 = b.value(QStringLiteral("t0")).toDouble(), t1 = b.value(QStringLiteral("t1")).toDouble();
            if (ms >= t0 && ms <= t1 && t1 > t0) {
                const double f = (ms - t0) / (t1 - t0);
                return b.value(QStringLiteral("v0")).toDouble() + f * (b.value(QStringLiteral("v1")).toDouble() - b.value(QStringLiteral("v0")).toDouble());
            }
        }
        return kNaN;
    };
    if (seg >= 0 && c.seq.segments[size_t(seg)].present) {
        const SegmentPeak &p = c.seq.segments[size_t(seg)];
        const int placedN = c.seq.placedCount;
        const QVariantList trows = c.w->cm->sequenceTable(c.shot->kinematicSequence).value(QStringLiteral("rows")).toList();
        QVariantMap trow;
        for (const QVariant &r : trows) if (r.toMap().value(QStringLiteral("segment")).toString() == sequenceSegmentIds()[size_t(seg)]) trow = r.toMap();
        peaks << QVariantMap{ { QStringLiteral("shotMs"), p.risingAtImpact ? 0.0 : p.shotMs },
                              { QStringLiteral("value"), meanAt(p.risingAtImpact ? 0.0 : p.shotMs) },
                              { QStringLiteral("text"), p.risingAtImpact ? QStringLiteral("rising") : W::signedMs(p.shotMs) },
                              { QStringLiteral("sigmaMs"), p.risingAtImpact ? kNaN : p.tSigmaMs },
                              { QStringLiteral("valueSigmaText"), W::pm(p.peakSigmaDps) },
                              { QStringLiteral("placed"), p.placed },
                              { QStringLiteral("late"), p.late },
                              { QStringLiteral("lateWord"), p.late ? QStringLiteral("late") : QString() },
                              { QStringLiteral("rising"), p.risingAtImpact },
                              { QStringLiteral("risingWord"), p.risingAtImpact ? QStringLiteral("rising") : QString() } };
        // the reading a gate touched opens its reason from here too (TrustFocus), as from the key figures
        const QString trustSubj = !p.records.isEmpty() ? QStringLiteral("ks.") + sequenceSegmentIds()[size_t(seg)] : QString();
        if (p.placed) {
            figures << QVariantMap{ { QStringLiteral("label"), QStringLiteral("peak") },
                                    { QStringLiteral("text"), W::fig(p.peakDps) + (W::pm(p.peakSigmaDps).isEmpty() ? QString() : QStringLiteral(" ") + W::pm(p.peakSigmaDps)) + QChar(0x00A0) + shortUnitOf(c, m.unit) },
                                    { QStringLiteral("warn"), false } };
            figures << QVariantMap{ { QStringLiteral("label"), QStringLiteral("order") },
                                    { QStringLiteral("text"), W::ordinalWord(p.rank) + QStringLiteral(" of %1 placed").arg(placedN) },
                                    { QStringLiteral("warn"), p.late } };
            // placed but kept out of the findings (the camera's club): say so, and open why
            const bool withheld = std::any_of(p.records.begin(), p.records.end(), [](const QVariant &r) {
                return r.toMap().value(QStringLiteral("effect")).toString() == QLatin1String("withheld"); });
            if (withheld)
                figures << QVariantMap{ { QStringLiteral("label"), QStringLiteral("why") },
                                        { QStringLiteral("text"), QStringLiteral("not judged") },
                                        { QStringLiteral("warn"), false },
                                        { QStringLiteral("trust"), trustSubj } };
        } else {
            // the gate that refused it, short enough for the figure column: the bound and why ("after −129 ms",
            // "out of sight"), else the reason alone
            const UnplacedWhy why = unplacedWhy(p);
            const bool hasFig = why.figure != QLatin1String("-") && !why.unit.isEmpty();
            figures << QVariantMap{ { QStringLiteral("label"), QStringLiteral("peak") },
                                    { QStringLiteral("text"), hasFig ? why.figure + QChar(0x00A0) + why.unit : why.shortText },
                                    { QStringLiteral("warn"), false },
                                    { QStringLiteral("trust"), hasFig ? QString() : trustSubj } };
            if (hasFig)
                figures << QVariantMap{ { QStringLiteral("label"), QStringLiteral("why") },
                                        { QStringLiteral("text"), why.shortText },
                                        { QStringLiteral("warn"), false },
                                        { QStringLiteral("trust"), trustSubj } };
        }
        seqInfo = QVariantMap{ { QStringLiteral("segment"), sequenceSegmentIds()[size_t(seg)] },
                               { QStringLiteral("word"), segWord(seg) },
                               { QStringLiteral("placed"), p.placed },
                               { QStringLiteral("rank"), p.rank },
                               { QStringLiteral("rankText"), trow.value(QStringLiteral("actualRank")).toString() },
                               { QStringLiteral("late"), p.late },
                               { QStringLiteral("lateWord"), p.late ? QStringLiteral("late") : QString() },
                               { QStringLiteral("rising"), p.risingAtImpact },
                               { QStringLiteral("peakText"), trow.value(QStringLiteral("peakText")).toString() },
                               { QStringLiteral("speedText"), trow.value(QStringLiteral("speedText")).toString() },
                               { QStringLiteral("pctText"), trow.value(QStringLiteral("pctText")).toString() } };
    } else if (s.winPeakOk) {
        const double pk = s.st.value(QStringLiteral("peak")).toDouble();
        const double tp = shotMsOf(c, s.st.value(QStringLiteral("tPeakUs")).toLongLong());
        const QString pm = W::pm(s.st.value(QStringLiteral("peakSigma")).toDouble(), s.dec);
        peaks << QVariantMap{ { QStringLiteral("shotMs"), tp }, { QStringLiteral("value"), pk },
                              { QStringLiteral("text"), W::fig(pk, s.dec) }, { QStringLiteral("sigmaMs"), kNaN },
                              { QStringLiteral("valueSigmaText"), pm }, { QStringLiteral("placed"), true },
                              { QStringLiteral("late"), false }, { QStringLiteral("lateWord"), QString() },
                              { QStringLiteral("rising"), false }, { QStringLiteral("risingWord"), QString() } };
        figures << QVariantMap{ { QStringLiteral("label"), QStringLiteral("peak") },
                                { QStringLiteral("text"), W::figUnit(pk, m.unit) + (pm.isEmpty() ? QString() : QStringLiteral(" ") + pm) },
                                { QStringLiteral("warn"), false } };
        figures << QVariantMap{ { QStringLiteral("label"), QStringLiteral("at") }, { QStringLiteral("text"), W::signedMs(tp) },
                                { QStringLiteral("warn"), false } };
    } else {
        figures << QVariantMap{ { QStringLiteral("label"), QStringLiteral("peak") }, { QStringLiteral("text"), QStringLiteral("-") },
                                { QStringLiteral("warn"), false } };
    }
    double atImpact = kNaN;
    for (const PhaseSample &p : m.phaseSamples) if (p.phase == phase::Impact) atImpact = p.value;
    figures << QVariantMap{ { QStringLiteral("label"), QStringLiteral("at P7") }, { QStringLiteral("text"), W::figUnit(atImpact, m.unit) },
                            { QStringLiteral("warn"), false } };

    QVariantMap row = card;
    row.insert(QStringLiteral("unit"), shortUnitOf(c, m.unit));
    row.insert(QStringLiteral("dec"), s.dec);
    row.insert(QStringLiteral("segment"), seg >= 0 ? sequenceSegmentIds()[size_t(seg)] : QString());
    row.insert(QStringLiteral("t"), dl(t));
    row.insert(QStringLiteral("mean"), dl(s.meanDrawn));
    row.insert(QStringLiteral("raw"), dl(s.rawDrawn));
    row.insert(QStringLiteral("runs"), runs);
    row.insert(QStringLiteral("bridges"), bridges);
    row.insert(QStringLiteral("yLo"), lo);
    row.insert(QStringLiteral("yHi"), hi);
    row.insert(QStringLiteral("yTicks"), ticksAsDoubles(niceTicksList(c, lo, hi)));
    row.insert(QStringLiteral("peaks"), peaks);
    row.insert(QStringLiteral("dots"), dots);
    row.insert(QStringLiteral("figures"), figures);
    row.insert(QStringLiteral("windowEmpty"), s.collapsed);
    row.insert(QStringLiteral("sequence"), seqInfo);
    // THE STRETCH THE ROUTE COULD NOT SEE (trust_and_gates_design.md §7.2), a bar along the foot of the plot,
    // labelled with the gate's short words: the sighted band of a face-on trunk, a gap in a two-camera
    // track. From the record, so it is drawn whether or not the band refused the peak — the samples
    // inside it are the ones the gate distrusts. Never back in sight ⇒ t1 = the curve's last sample.
    QVariantList blind;
    if (seg >= 0 && std::isfinite(c.seq.segments[size_t(seg)].blindFromMs)) {
        const SegmentPeak &p = c.seq.segments[size_t(seg)];
        const gates::GateEntry *e = gates::gateEntry(p.blindGate);
        blind << QVariantMap{ { QStringLiteral("t0"), p.blindFromMs },
                              // never back in sight: to the end of the curve, not the edge of the window
                              { QStringLiteral("t1"), std::isfinite(p.blindToMs) ? p.blindToMs : (t.empty() ? p.blindFromMs : t.back()) },
                              { QStringLiteral("gate"), p.blindGate },
                              { QStringLiteral("label"), e ? e->shortText : QString() } };
    }
    row.insert(QStringLiteral("blind"), blind);
    // the reading a click on this curve's marks opens (an out-of-sight bar, an unplaced ring)
    row.insert(QStringLiteral("trustSubject"), seg >= 0 && !c.seq.segments[size_t(seg)].records.isEmpty()
                                                   ? QStringLiteral("ks.") + sequenceSegmentIds()[size_t(seg)] : QString());
    return row;
}

void buildThisShot(Ctx &c, ChartsTracesPresenter::Result &R)
{
    QVariantMap out;
    const int n = c.snap ? c.snap->count() : 0;
    out.insert(QStringLiteral("sessionCount"), n);
    if (!c.shot) {
        W::Sentence e;
        e.append(QStringLiteral("Select a swing to review: the charts show its analysed metric curves."));
        out.insert(QStringLiteral("available"), false);
        out.insert(QStringLiteral("empty"), e.toVariant());
        R.thisShot = out;
        return;
    }
    const QString club = asciiLower(c.shot->club.isEmpty() && c.snap && c.index >= 0 ? c.snap->shots[size_t(c.index)]->club : c.shot->club);
    out.insert(QStringLiteral("available"), true);
    out.insert(QStringLiteral("ordinal"), c.ordinal);
    out.insert(QStringLiteral("shotId"), c.shotId);
    out.insert(QStringLiteral("swingDir"), c.swingDir);
    out.insert(QStringLiteral("inSession"), c.index >= 0);
    QString kicker = c.index >= 0 ? QStringLiteral("Shot %1 of %2").arg(c.ordinal).arg(n)
                                  : QStringLiteral("Shot %1").arg(c.shotId > 0 ? c.shotId : c.ordinal);
    if (!club.isEmpty()) kicker += QStringLiteral("  ·  ") + club;
    out.insert(QStringLiteral("kicker"), kicker);

    // every preset member, resolved once (rows and cards both read these)
    for (const QString &k : c.presetKeys)
        if (const MetricSeries *m = c.shot->metric(k)) c.calcs.insert(k, calcSeries(c, *m));
    // companions: a member whose card names a host that is also shown rides on the host's card
    const auto hostOf = [&](const QString &k) -> QString {
        const QString h = c.w->specFor(k).value(QStringLiteral("mergeInto")).toString();
        return (!h.isEmpty() && c.calcs.contains(h) && !c.in->metricsOff.contains(h)) ? h : QString();
    };
    // THE GLANCE TRIM: Replay sits beside the video, so a preset with a long list of instant readings
    // shows only the few a golfer reads at the mat; Analyse shows them all. Club delivery (the club's
    // motion and its impact numbers, one preset since 16 Oct) keeps path, face, attack angle, low point
    // and dynamic loft — each with its launch-monitor twin where there is one (Mark, 16 Oct).
    static const QHash<QString, QSet<QString>> kReplayGlance{
        { QStringLiteral("Club delivery"),
          { QStringLiteral("clubPath"), QStringLiteral("lm.clubPath"), QStringLiteral("lm.faceAngle"),
            QStringLiteral("attackAngle"), QStringLiteral("lm.attackAngle"),
            QStringLiteral("lowPointAhead"), QStringLiteral("lm.lowPointAhead"), QStringLiteral("lm.dynamicLoft") } } };
    const auto glanceIt = c.in->reg == QLatin1String("replay") ? kReplayGlance.constFind(c.preset) : kReplayGlance.constEnd();
    QVariantList rows, cards;
    std::vector<int> dotKinds;
    for (const QString &k : c.presetKeys) {
        if (!c.calcs.contains(k) || !hostOf(k).isEmpty()) continue;
        const SeriesCalc &s = c.calcs.constFind(k).value();
        const bool drawn = c.rowKeys.contains(k);
        if (!drawn && s.hasCurve && c.w->drawsCurve(k)) continue;      // a curve switched off: no card (today's rule)
        if (!s.hasCurve && s.m->phaseSamples.empty()) continue;         // nothing to show
        std::vector<const SeriesCalc *> cos;
        for (const QString &k2 : c.presetKeys)
            if (k2 != k && c.calcs.contains(k2) && hostOf(k2) == k) cos.push_back(&c.calcs.find(k2).value());
        const QVariantMap card = cardOf(c, s, cos);
        if (drawn) rows << shotRow(c, s, card, dotKinds);
        else if (glanceIt == kReplayGlance.constEnd() || glanceIt->contains(k)) cards << card;
    }
    out.insert(QStringLiteral("rows"), rows);
    out.insert(QStringLiteral("cards"), cards);
    // OVERLAY (F6, kept as an option beside the small multiples): one shared Y across the rows, and
    // whether they share a unit (when they do not, overlay plots each on its own scale's ticks).
    {
        double lo = kNaN, hi = kNaN;
        QSet<QString> units;
        for (const QVariant &rv : rows) {
            const QVariantMap r = rv.toMap();
            const double a = r.value(QStringLiteral("yLo")).toDouble(), b = r.value(QStringLiteral("yHi")).toDouble();
            if (std::isfinite(a)) lo = std::isfinite(lo) ? std::min(lo, a) : a;
            if (std::isfinite(b)) hi = std::isfinite(hi) ? std::max(hi, b) : b;
            units.insert(r.value(QStringLiteral("unit")).toString());
        }
        out.insert(QStringLiteral("overlay"), QVariantMap{ { QStringLiteral("yLo"), lo }, { QStringLiteral("yHi"), hi },
                                                           { QStringLiteral("yTicks"), ticksAsDoubles(niceTicksList(c, lo, hi)) },
                                                           { QStringLiteral("sameUnit"), units.size() == 1 } });
    }
    W::Sentence noTraces;
    if (rows.isEmpty())
        noTraces.append(cards.isEmpty() ? QStringLiteral("No metric traces for this swing.")
                                        : QStringLiteral("This preset is read at instants only: there is no trace to draw."));
    out.insert(QStringLiteral("noTraces"), noTraces.toVariant());
    QVariantList dotKey;
    std::sort(dotKinds.begin(), dotKinds.end());
    static const char *const kv[] = { "good", "attention", "warn", "" };
    for (int k : dotKinds)
        dotKey << QVariantMap{ { QStringLiteral("verdict"), QString::fromLatin1(kv[k]) },
                               { QStringLiteral("word"), verdictWord(QString::fromLatin1(kv[k])) } };
    out.insert(QStringLiteral("dotKey"), dotKey);

    // the window and its P ticks
    const double t0 = shotMsOf(c, c.win.startUs), t1 = shotMsOf(c, c.win.endUs);
    const int nearA = c.w->cm->nearestPhase(phasesList(*c.shot), c.win.startUs);
    const int nearB = c.w->cm->nearestPhase(phasesList(*c.shot), c.win.endUs);
    const int pa = c.win.phaseA >= 0 ? c.win.phaseA : nearA, pb = c.win.phaseB >= 0 ? c.win.phaseB : nearB;
    out.insert(QStringLiteral("window"), QVariantMap{
        { QStringLiteral("key"), c.win.key }, { QStringLiteral("label"), c.win.label },
        { QStringLiteral("header"), c.win.key == QLatin1String("full") ? QStringLiteral("Full recording")
                                                                      : c.w->tl->phaseFullName(pa) + QStringLiteral(" → ") + c.w->tl->phaseFullName(pb) },
        { QStringLiteral("spanText"), W::figMs((c.win.endUs - c.win.startUs) / 1000.0) },
        { QStringLiteral("metricsText"), QStringLiteral("%1/%2 metrics").arg(rows.size()).arg([&] {
              int p = 0; for (const QString &k : c.presetKeys) if (const MetricSeries *m = c.shot->metric(k)) p += (m->hasCurve() && c.w->drawsCurve(k)) ? 1 : 0; return p; }()) },
        { QStringLiteral("tagsText"), c.tag(pa) + QStringLiteral("→") + c.tag(pb) },
        { QStringLiteral("startUs"), c.win.startUs }, { QStringLiteral("endUs"), c.win.endUs },
        { QStringLiteral("t0Ms"), t0 }, { QStringLiteral("t1Ms"), t1 },
        { QStringLiteral("phaseA"), c.win.phaseA }, { QStringLiteral("phaseB"), c.win.phaseB },
        { QStringLiteral("hasImpact"), c.shot->impactUs >= 0 } });
    QVariantList ticks;
    for (int ph : pLadder()) {
        const qint64 u = phaseUsOf(*c.shot, ph);
        if (u < 0 || u < c.win.startUs || u > c.win.endUs) continue;
        ticks << QVariantMap{ { QStringLiteral("phase"), ph }, { QStringLiteral("tag"), c.tag(ph) },
                              { QStringLiteral("name"), c.w->tl->phaseFullName(ph) }, { QStringLiteral("shotMs"), shotMsOf(c, u) },
                              { QStringLiteral("landmark"), ph == phase::Impact ? QStringLiteral("impact") : ph == phase::Top ? QStringLiteral("top") : QString() } };
    }
    out.insert(QStringLiteral("ticks"), ticks);
    // the ms axis of the window (ChartMetrics.timeTicksMs, the step widening with the span)
    {
        std::vector<double> ms;
        if (c.shot->impactUs >= 0)
            for (const QVariant &v : c.w->cm->timeTicksMs(c.win.startUs, c.win.endUs, c.shot->impactUs)) ms.push_back(v.toDouble());
        out.insert(QStringLiteral("msTicks"), tickTexts(ms, true));
    }
    // THE WINDOW BRUSH (F14): the P-positions on the whole-recording clock, and the drawn metrics'
    // windowed means over the WHOLE recording, decimated for the overview sparklines.
    {
        QVariantList rt;
        for (int ph : pLadder()) {
            const qint64 u = phaseUsOf(*c.shot, ph);
            if (u < 0) continue;
            rt << QVariantMap{ { QStringLiteral("phase"), ph }, { QStringLiteral("tag"), c.tag(ph) }, { QStringLiteral("us"), u },
                               { QStringLiteral("landmark"), ph == phase::Impact ? QStringLiteral("impact") : ph == phase::Top ? QStringLiteral("top") : QString() } };
        }
        out.insert(QStringLiteral("recordingTicks"), rt);
        QVariantList ov;
        for (const QString &k : c.rowKeys) {
            const SeriesCalc &s = c.calcs.constFind(k).value();
            const size_t n = s.m->tUs.size();
            std::vector<double> t, v;
            const size_t buckets = 100;
            if (n <= 2 * buckets) {
                for (size_t i = 0; i < n; ++i) { t.push_back(double(s.m->tUs[i])); v.push_back(s.meanDrawn[i]); }
            } else {
                // min and max of each of 100 index buckets, in time order (≤ 200 points); a bucket with
                // no measurement is one NaN, so the sparkline breaks there rather than bridging it
                for (size_t b = 0; b < buckets; ++b) {
                    const size_t i0 = b * n / buckets, i1 = (b + 1) * n / buckets;
                    long lo = -1, hi = -1;
                    for (size_t i = i0; i < i1; ++i) {
                        const double x = s.meanDrawn[i];
                        if (!std::isfinite(x)) continue;
                        if (lo < 0 || x < s.meanDrawn[size_t(lo)]) lo = long(i);
                        if (hi < 0 || x > s.meanDrawn[size_t(hi)]) hi = long(i);
                    }
                    if (lo < 0) { t.push_back(double(s.m->tUs[(i0 + i1) / 2])); v.push_back(kNaN); continue; }
                    const long first = std::min(lo, hi), second = std::max(lo, hi);
                    t.push_back(double(s.m->tUs[size_t(first)])); v.push_back(s.meanDrawn[size_t(first)]);
                    if (second != first) { t.push_back(double(s.m->tUs[size_t(second)])); v.push_back(s.meanDrawn[size_t(second)]); }
                }
            }
            double vlo = kNaN, vhi = kNaN;
            for (double x : v) if (std::isfinite(x)) { vlo = std::isfinite(vlo) ? std::min(vlo, x) : x; vhi = std::isfinite(vhi) ? std::max(vhi, x) : x; }
            ov << QVariantMap{ { QStringLiteral("key"), k }, { QStringLiteral("t"), dl(t) }, { QStringLiteral("v"), dl(v) },
                               { QStringLiteral("vLo"), vlo }, { QStringLiteral("vHi"), vhi } };
        }
        out.insert(QStringLiteral("overview"), ov);
    }

    // the sequence, the head, the standfirst, the figures
    const bool seqShown = c.sequencePreset && !c.shot->kinematicSequence.value(QStringLiteral("nodes")).toList().isEmpty();
    out.insert(QStringLiteral("sequence"), seqShown ? sequenceBlock(c) : QVariantMap{});
    // the readings a gate touched on this shot (trust_readings.h): what a mark's click opens in place
    out.insert(QStringLiteral("trust"), c.shot ? trust::trustReadings(*c.shot) : QVariantList{});
    W::Sentence head, stand, note;
    QVariantList figs;
    const bool replay = c.in->reg == QLatin1String("replay");
    const QString lead = c.rowKeys.value(0);
    if (seqShown) {
        QStringList late;
        bool pelvisLate = false;
        for (int i = 0; i < kSequenceSegments; ++i)
            if (c.seq.segments[size_t(i)].late) { late << QStringLiteral("the ") + segWord(i).toLower(); pelvisLate = pelvisLate || i == 0; }
        if (!late.isEmpty()) {
            head.append(QStringLiteral("Out of sequence: ") + W::list(late) + QStringLiteral(" peaked late"));
        } else {
            // NOTHING PLACED PEAKED LATE: the head judges the placed segments only (one camera places
            // the lead arm and the club and nothing else; ChartMetrics' verdictState still reads an
            // inversion against an unplaced peak as "Out of sequence", which is not what was measured).
            std::vector<int> placed;
            QStringList notPlaced;
            for (int i = 0; i < kSequenceSegments; ++i) {
                if (c.seq.segments[size_t(i)].placed) placed.push_back(i);
                else notPlaced << segWord(i).toLower();
            }
            std::stable_sort(placed.begin(), placed.end(), [&](int a, int b) { return c.seq.segments[size_t(a)].shotMs < c.seq.segments[size_t(b)].shotMs; });
            const bool uncertain = c.w->cm->sequenceTable(c.shot->kinematicSequence).value(QStringLiteral("verdictText")).toString()
                                       .contains(QStringLiteral("? = uncertain"));
            QStringList order;
            for (int i : placed) order << segWord(i).toLower();
            if (int(placed.size()) == kSequenceSegments) head.append(QStringLiteral("In sequence"));
            else if (placed.size() >= 2)
                head.append(W::cap1(order.join(QStringLiteral(" then "))) + (uncertain ? QStringLiteral(", in order so far") : QStringLiteral(", in order")));
            else if (placed.size() == 1) head.append(QStringLiteral("Only the ") + order.first() + QStringLiteral(" placed"));
            else head.append(QStringLiteral("Sequence not placed"));
            // the gate in words when it is the same for every unplaced segment (the face-on blind band, usually)
            bool allOutOfSight = !notPlaced.isEmpty();
            for (int i = 0; i < kSequenceSegments; ++i)
                if (!c.seq.segments[size_t(i)].placed)
                    allOutOfSight = allOutOfSight && c.seq.segments[size_t(i)].present
                                 && unplacedWhy(c.seq.segments[size_t(i)]).shortText == QLatin1String("out of sight");
            if (!placed.empty() && !notPlaced.isEmpty())
                head.append(QStringLiteral("; ") + W::list(notPlaced)
                            + (allOutOfSight ? QStringLiteral(" out of the camera's sight") : QStringLiteral(" not placed")));
        }
        if (pelvisLate) note.append(W::latePelvisNote());
        const SegmentPeak &club = c.seq.segments[3], &pel = c.seq.segments[0], &arm = c.seq.segments[2];
        if (club.placed)
            stand.append(QStringLiteral("The club peaked ")).figure(W::figMs(std::abs(club.shotMs)))
                .append(club.shotMs <= 0 ? QStringLiteral(" before impact") : QStringLiteral(" after impact"));
        if (pel.placed)
            stand.append(club.placed ? QStringLiteral(", the pelvis ") : QStringLiteral("The pelvis peaked "))
                // the pelvis' OWN time from impact (+20 ms on 4 July shot 8), never the club→pelvis gap
                .figure(W::figMs(std::abs(pel.shotMs)))
                .append(pel.shotMs > 0 ? QStringLiteral(" after impact") : QStringLiteral(" before impact"));
        if (!stand.isEmpty()) stand.append(QStringLiteral("."));
        if (pel.placed && pel.shotMs > 0 && n >= 3) {     // a session clause needs a session (≥ 3 shots)
            const SegmentSummary &ps = c.w->sequence.segments[0];
            stand.append(QStringLiteral(" On ")).figure(W::word(ps.afterImpact) + QStringLiteral(" of the ") + W::word(n))
                .append(QStringLiteral(" shots the pelvis peaks after impact."));
        }
        if (replay) {
            if (club.placed)
                figs << QVariantMap{ { QStringLiteral("label"), QStringLiteral("Club peak") },
                                     { QStringLiteral("text"), W::fig(club.peakDps) + (W::pm(club.peakSigmaDps).isEmpty() ? QString() : QStringLiteral(" ") + W::pm(club.peakSigmaDps)) + QStringLiteral("\u00A0°/s") },
                                     { QStringLiteral("warn"), false }, { QStringLiteral("lateWord"), QString() } };
            if (club.placed && arm.placed)
                figs << QVariantMap{ { QStringLiteral("label"), QStringLiteral("Lead arm to club") },
                                     { QStringLiteral("text"), W::figMs(club.shotMs - arm.shotMs) },
                                     { QStringLiteral("warn"), false }, { QStringLiteral("lateWord"), QString() } };
            if (pel.placed)
                figs << QVariantMap{ { QStringLiteral("label"), QStringLiteral("Pelvis peak") },
                                     { QStringLiteral("text"), W::signedMs(pel.shotMs) },
                                     { QStringLiteral("warn"), pel.late }, { QStringLiteral("lateWord"), pel.late ? QStringLiteral("late") : QString() } };
        }
    } else if (!lead.isEmpty()) {
        const MetricSeries *m = c.shot->metric(lead);
        double atImp = kNaN;
        for (const PhaseSample &p : m->phaseSamples) if (p.phase == phase::Impact) atImp = p.value;
        const SeriesCalc &s = c.calcs.constFind(lead).value();
        if (replay && std::isfinite(atImp)) {
            head.append(c.labelOf(lead) + QStringLiteral(" ")).figure(W::figUnit(atImp, m->unit)).append(QStringLiteral(" at impact"));
        } else if (s.winPeakOk) {
            const double tp = shotMsOf(c, s.st.value(QStringLiteral("tPeakUs")).toLongLong());
            head.append(c.labelOf(lead) + QStringLiteral(" peaks ")).figure(W::figMs(std::abs(tp)))
                .append(tp < 0 ? QStringLiteral(" before impact") : QStringLiteral(" after impact"));
        } else {
            head.append(c.preset);
        }
        QStringList bits;
        for (int i = 1; i < std::min<int>(3, c.rowKeys.size()); ++i) {
            const SeriesCalc &q = c.calcs.constFind(c.rowKeys[i]).value();
            if (q.winPeakOk) bits << c.labelOf(c.rowKeys[i]).toLower() + QStringLiteral(" at ") + W::signedMs(shotMsOf(c, q.st.value(QStringLiteral("tPeakUs")).toLongLong()));
        }
        if (!bits.isEmpty()) stand.append(QStringLiteral("Then ") + W::list(bits) + QStringLiteral(" peak."));
        if (replay)
            for (int i = 1; i < std::min<int>(4, c.rowKeys.size()); ++i) {
                const MetricSeries *q = c.shot->metric(c.rowKeys[i]);
                double v = kNaN;
                for (const PhaseSample &p : q->phaseSamples) if (p.phase == phase::Impact) v = p.value;
                figs << QVariantMap{ { QStringLiteral("label"), c.labelOf(c.rowKeys[i]) + QStringLiteral(" at P7") },
                                     { QStringLiteral("text"), W::figUnit(v, q->unit) },
                                     { QStringLiteral("warn"), false }, { QStringLiteral("lateWord"), QString() } };
            }
    } else {
        // NO CURVE (a launch monitor only, Club delivery): the head is the readings themselves, each at
        // impact where it was read there, else at its first instant — "Ball speed 112 mph, launch 18°".
        struct Inst { QString key, unit; double v; };
        std::vector<Inst> inst;
        for (const QString &k : c.presetKeys) {
            if (c.in->metricsOff.contains(k)) continue;
            const MetricSeries *m = c.shot->metric(k);
            if (!m || m->phaseSamples.empty()) continue;
            double v = kNaN;
            for (const PhaseSample &p : m->phaseSamples) if (p.phase == phase::Impact) v = p.value;
            if (!std::isfinite(v)) for (const PhaseSample &p : m->phaseSamples) if (std::isfinite(p.value)) { v = p.value; break; }
            if (std::isfinite(v)) inst.push_back(Inst{ k, m->unit, v });
        }
        if (inst.empty()) {
            head.append(c.preset.isEmpty() ? QStringLiteral("No metric traces for this swing") : c.preset);
        } else {
            // the reading's name without its provenance suffix ("Ball speed (measured)" → "Ball speed")
            const auto plain = [&](const QString &k) {
                QString l = c.labelOf(k);
                const int i = l.lastIndexOf(QStringLiteral(" ("));
                if (i > 0 && l.endsWith(QLatin1Char(')'))) l.truncate(i);
                return l;
            };
            head.append(plain(inst[0].key) + QStringLiteral(" ")).figure(W::figUnit(inst[0].v, inst[0].unit));
            if (inst.size() > 1) {
                QString second = plain(inst[1].key);
                if (!second.isEmpty()) second[0] = second[0].toLower();
                head.append(QStringLiteral(", ") + second + QStringLiteral(" ")).figure(W::figUnit(inst[1].v, inst[1].unit));
            }
            if (replay)
                for (size_t i = 1; i < inst.size() && i < 4; ++i)
                    figs << QVariantMap{ { QStringLiteral("label"), c.labelOf(inst[i].key) },
                                         { QStringLiteral("text"), W::figUnit(inst[i].v, inst[i].unit) },
                                         { QStringLiteral("warn"), false }, { QStringLiteral("lateWord"), QString() } };
        }
    }
    out.insert(QStringLiteral("head"), head.toVariant());
    out.insert(QStringLiteral("note"), note.toVariant());
    out.insert(QStringLiteral("standfirst"), stand.toVariant());
    out.insert(QStringLiteral("figures"), figs);

    // the GUI thread's arrays for the playhead and the hover
    for (const QString &k : c.rowKeys) {
        const SeriesCalc &s = c.calcs.constFind(k).value();
        ChartsTracesPresenter::NowRow nr;
        nr.key = k; nr.label = c.labelOf(k); nr.unit = s.m->unit; nr.dec = s.dec;
        nr.tUs = s.m->tUs; nr.mean = s.mean; nr.raw = s.m->value; nr.measured = s.measured;
        nr.grossWarn = c.w->cm->sigmaInfo(s.seriesMap).value(QStringLiteral("warn")).toBool();
        // Δ from Address: the drawn value at the Address sample when it was measured there, else the
        // producer's own reading (PpMetricChart._addrValue); the first drawn value when there is none.
        nr.addr = s.mean.empty() ? kNaN : s.mean.front();
        for (const PhaseSample &p : s.m->phaseSamples)
            if (p.phase == phase::Address) {
                const size_t i = nearestIndex(s.m->tUs, p.tUs);
                nr.addr = s.measured[i] ? s.mean[i] : p.value;
                break;
            }
        R.now.rows.push_back(std::move(nr));
    }
    R.thisShot = out;
}

// ── THE SESSION'S SHARED PIECES ─────────────────────────────────────────────────────────────

QString ordsKey(const std::vector<int> &o)
{
    QString s;
    for (int x : o) s += QString::number(x) + QLatin1Char(',');
    return s;
}

QString winKey(const Ctx &c) { return QStringLiteral("%1|%2").arg(c.t0, 0, 'f', 3).arg(c.t1, 0, 'f', 3); }

const SessionEnvelope &envOf(Ctx &c, const QString &key, const std::vector<int> &ords)
{
    const QString id = key + QLatin1Char('|') + ordsKey(ords) + QLatin1Char('|') + winKey(c);
    auto it = c.w->envelopes.find(id);
    if (it == c.w->envelopes.end()) {
        const auto curves = c.w->alignedOf(key);
        it = c.w->envelopes.insert(id, envelope(curvesOf(*curves, ords), c.grid));
    }
    return it.value();
}

const GroupFit &fitOf(Ctx &c, const QString &key, const std::vector<int> &ords)
{
    const QString id = key + QLatin1Char('|') + ordsKey(ords) + QLatin1Char('|') + winKey(c);
    auto it = c.w->fits.find(id);
    if (it == c.w->fits.end()) {
        const auto curves = c.w->alignedOf(key);
        it = c.w->fits.insert(id, pooledFit(*curves, ords, c.grid));
    }
    return it.value();
}

std::vector<std::vector<TraceCurve>> perKeyCurves(Ctx &c)
{
    std::vector<std::vector<TraceCurve>> out;
    for (const QString &k : c.sessionKeys) out.push_back(*c.w->alignedOf(k));
    return out;
}

const ShapeSplit &shapeOf(Ctx &c)
{
    const QString id = c.sessionKeys.join(QLatin1Char(',')) + QLatin1Char('|') + winKey(c);
    auto it = c.w->shapes.find(id);
    if (it == c.w->shapes.end())
        it = c.w->shapes.insert(id, c.sessionKeys.isEmpty() || !std::isfinite(c.t0) ? ShapeSplit{} : shapeSplit(perKeyCurves(c), c.t0, c.t1));
    return it.value();
}

ShotScore scoreOf(Ctx &c, bool least, int exclude)
{
    const QString id = (least ? QStringLiteral("L|") : QStringLiteral("T|")) + c.sessionKeys.join(QLatin1Char(',')) + QLatin1Char('|')
                       + winKey(c) + QLatin1Char('|') + QString::number(exclude);
    auto it = c.w->scores.find(id);
    if (it == c.w->scores.end()) {
        ShotScore s;
        if (!c.sessionKeys.isEmpty() && std::isfinite(c.t0))
            s = least ? leastTypicalShot(perKeyCurves(c), c.t0, c.t1, exclude) : typicalShot(perKeyCurves(c), c.t0, c.t1, exclude);
        it = c.w->scores.insert(id, s);
    }
    return it.value();
}

// Every shot's peak of `key` over its own window (the window's P-positions on that shot, else the
// canonical window unwarped onto it), clamped to the metric's domain — ChartMetrics.summaryMasked.
const std::vector<double> &peaksOf(Ctx &c, const QString &key)
{
    const QString id = key + QLatin1Char('|') + c.win.key + QStringLiteral("|%1|%2|").arg(c.win.phaseA).arg(c.win.phaseB) + winKey(c);
    auto it = c.w->peaks.find(id);
    if (it != c.w->peaks.end()) return it.value();
    std::vector<double> out(size_t(c.snap->count()), kNaN);
    const QVariantMap d = c.w->domainFor(key);
    for (size_t i = 0; i < c.snap->shots.size(); ++i) {
        const ShotSeries &s = *c.snap->shots[i];
        const MetricSeries *m = s.metric(key);
        if (!m || !m->hasCurve() || s.impactUs < 0) continue;
        qint64 a = c.win.phaseA >= 0 ? phaseUsOf(s, c.win.phaseA) : -1, b = c.win.phaseB >= 0 ? phaseUsOf(s, c.win.phaseB) : -1;
        const ShotWarp &w = c.w->alignment.shots[i];
        if ((a < 0 || b <= a) && w.aligned() && std::isfinite(c.t0)) { a = w.usAtCanonMs(c.t0); b = w.usAtCanonMs(c.t1); }
        if (b <= a) continue;
        const auto phaseOr = [&](int ph, qint64 fb) { const qint64 u = phaseUsOf(s, ph); return u >= 0 ? u : fb; };
        const qint64 from = d.value(QStringLiteral("firstNarrowed")).toBool() ? nearestSampleUs(m->tUs, phaseOr(d.value(QStringLiteral("firstPhase")).toInt(), m->tUs.front())) : m->tUs.front();
        const qint64 to = d.value(QStringLiteral("lastNarrowed")).toBool() ? nearestSampleUs(m->tUs, phaseOr(d.value(QStringLiteral("lastPhase")).toInt(), m->tUs.back())) : m->tUs.back();
        const qint64 ds = std::max(a, from), de = std::max(ds, std::min(b, to));
        if (de <= ds) continue;
        const QVariantMap st = c.w->cm->summaryMasked(tList(*m), vList(*m), validList(*m), ds, de);
        if (st.value(QStringLiteral("edgeOk")).toBool() && st.value(QStringLiteral("extremumOk"), true).toBool())
            out[i] = st.value(QStringLiteral("peak")).toDouble();
    }
    return c.w->peaks.insert(id, out).value();
}

// A canonical instant said against the nearest canonical P: "at P6", "12 ms after P6".
QString phaseWord(const Ctx &c, double t)
{
    const CanonicalPhase *best = nullptr;
    for (const CanonicalPhase &p : c.w->alignment.canonical)
        if (!c.tag(p.phase).isEmpty() && (!best || std::abs(p.tMs - t) < std::abs(best->tMs - t))) best = &p;
    if (!best) return W::signedMs(t);
    const long d = std::lround(t - best->tMs);
    if (std::labs(d) <= 3) return QStringLiteral("at ") + c.tag(best->phase);
    return QString::number(std::labs(d)) + QChar(0x00A0) + QStringLiteral("ms ") + (d > 0 ? QStringLiteral("after ") : QStringLiteral("before ")) + c.tag(best->phase);
}

QString bareWhen(const QString &s) { return s.startsWith(QLatin1String("at ")) ? s.mid(3) : s; }

// The picked shot's curve of `key` on the canonical axis.
TraceCurve pickedAligned(Ctx &c, const QString &key)
{
    if (c.index >= 0) return (*c.w->alignedOf(key))[size_t(c.index)];
    if (!c.shot || !c.aligned) return TraceCurve{};
    return alignedCurve(shotCurve(*c.shot, key), c.warp);
}

// ── groups for a facet ──────────────────────────────────────────────────────────────────────

struct Group { QString key, label, shortLabel; std::vector<int> ords; bool own = false; };

std::vector<Group> facetGroups(Ctx &c, const QString &facet, QString *kind)
{
    std::vector<int> all;
    for (const auto &s : c.snap->shots) all.push_back(s->ordinal);
    std::vector<Group> g;
    *kind = QStringLiteral("none");
    const int n = c.snap->count();
    if (facet == QLatin1String("half")) {
        *kind = QStringLiteral("half");
        const SessionFacet f = halfFacet(*c.snap);
        const int h = n / 2;
        g.push_back(Group{ f.groups[0].key, QStringLiteral("Shots 1–%1").arg(h), QStringLiteral("First half"), f.groups[0].ordinals });
        g.push_back(Group{ f.groups[1].key, QStringLiteral("Shots %1–%2").arg(h + 1).arg(n), QStringLiteral("Second half"), f.groups[1].ordinals });
    } else if (facet == QLatin1String("shape")) {
        const ShapeSplit &sp = shapeOf(c);
        if (const auto f = shapeFacet(sp)) {
            *kind = QStringLiteral("shape");
            g.push_back(Group{ QStringLiteral("a"), QStringLiteral("Shaped like shot %1").arg(sp.medoidA), QStringLiteral("Like shot %1").arg(sp.medoidA), sp.groupA });
            g.push_back(Group{ QStringLiteral("b"), QStringLiteral("Shaped like shot %1").arg(sp.medoidB), QStringLiteral("Like shot %1").arg(sp.medoidB), sp.groupB });
        }
    } else if (facet == QLatin1String("club")) {
        if (c.w->clubFacet) {
            *kind = QStringLiteral("club");
            for (const FacetGroup &fg : c.w->clubFacet->groups)
                g.push_back(Group{ fg.key, asciiLower(fg.key), asciiLower(fg.key), fg.ordinals });
        }
    } else if (!facet.isEmpty()) {
        for (const SessionFacet &f : c.w->conditionFacets)
            if (f.id == facet) {
                *kind = QStringLiteral("condition");
                const QString nm = c.snap->conditions.value(f.id).name.isEmpty() ? f.id : c.snap->conditions.value(f.id).name;
                g.push_back(Group{ QStringLiteral("with"), nm, QStringLiteral("With"), f.groups[0].ordinals });
                g.push_back(Group{ QStringLiteral("without"), QStringLiteral("Without it"), QStringLiteral("Without"), f.groups[1].ordinals });
            }
    }
    if (g.empty()) g.push_back(Group{ QStringLiteral("all"), QStringLiteral("All %1 shots").arg(n), QStringLiteral("All"), all });
    for (Group &x : g) x.own = std::find(x.ords.begin(), x.ords.end(), c.ordinal) != x.ords.end();
    std::stable_sort(g.begin(), g.end(), [](const Group &a, const Group &b) { return a.own && !b.own; });
    return g;
}

QVariantMap shotIds(const Ctx &c, int ordinal)
{
    QVariantMap m{ { QStringLiteral("ordinal"), ordinal } };
    if (c.snap && ordinal >= 1 && ordinal <= c.snap->count()) {
        m.insert(QStringLiteral("shotId"), c.snap->shots[size_t(ordinal - 1)]->shotId);
        m.insert(QStringLiteral("swingDir"), c.snap->shots[size_t(ordinal - 1)]->swingDir);
    } else {
        m.insert(QStringLiteral("shotId"), c.shotId);
        m.insert(QStringLiteral("swingDir"), c.swingDir);
    }
    return m;
}

QVariantList runsOf(const std::vector<TimeSpan> &r)
{
    QVariantList out;
    for (const TimeSpan &s : r) out << QVariantMap{ { QStringLiteral("t0"), s.t0 }, { QStringLiteral("t1"), s.t1 } };
    return out;
}

// The session's window on the canonical axis: the window's P-positions' canonical times, else the
// picked shot's window warped, clamped to where the aligned curves are.
void resolveSessionWindow(Ctx &c)
{
    const CanonicalPhase *a = c.win.phaseA >= 0 ? c.w->alignment.canonicalOf(c.win.phaseA) : nullptr;
    const CanonicalPhase *b = c.win.phaseB >= 0 ? c.w->alignment.canonicalOf(c.win.phaseB) : nullptr;
    double t0 = kNaN, t1 = kNaN;
    if (a && b && b->tMs > a->tMs) { t0 = a->tMs; t1 = b->tMs; }
    else if (c.shot && c.aligned && c.shot->impactUs >= 0) {
        t0 = c.warp.warpMs(shotMsOf(c, c.win.startUs));
        t1 = c.warp.warpMs(shotMsOf(c, c.win.endUs));
    }
    double lo = kNaN, hi = kNaN;
    for (const QString &k : c.sessionKeys)
        for (const TraceCurve &tc : *c.w->alignedOf(k)) {
            if (tc.empty()) continue;
            lo = std::isfinite(lo) ? std::min(lo, tc.t.front()) : tc.t.front();
            hi = std::isfinite(hi) ? std::max(hi, tc.t.back()) : tc.t.back();
        }
    if (!std::isfinite(t0) || !(t1 > t0)) { t0 = lo; t1 = hi; }
    if (std::isfinite(lo)) { t0 = std::max(t0, lo); t1 = std::min(t1, hi); }
    if (std::isfinite(t0) && t1 > t0) {
        c.t0 = t0; c.t1 = t1;
        c.grid = sessionGrid(t0, t1);
    }
}

QString alignmentSentence(const Ctx &c)
{
    const SessionAlignment &al = c.w->alignment;
    const int n = int(al.shots.size());
    const int pA = al.countRoute(AlignRoute::PAnchors), lm = al.countRoute(AlignRoute::Landmarks),
              tf = al.countRoute(AlignRoute::TempoFitted), im = al.countRoute(AlignRoute::ImpactShift),
              un = al.countRoute(AlignRoute::Unaligned);
    QStringList s;
    if (im == n) return QStringLiteral("Only impact is known: shots are lined up at impact, tempo differences stay in the curves.");
    if (pA == n) s << QStringLiteral("Every shot is lined up on its P-positions; only the time axis is stretched, values keep their units.");
    else if (pA > 0) s << W::cap1(W::word(pA)) + QStringLiteral(" shots are lined up on their P-positions.");
    if (lm > 0) s << W::cap1(W::word(lm)) + (lm == 1 ? QStringLiteral(" shot had only impact: it is") : QStringLiteral(" shots had only impact: each is"))
                     + QStringLiteral(" lined up on landmarks found in its own curves.");
    if (tf > 0) s << W::cap1(W::word(tf)) + (tf == 1 ? QStringLiteral(" shot had only impact: it is") : QStringLiteral(" shots had only impact: each is"))
                     + QStringLiteral(" scaled about impact by a tempo fitted to the median curve, which can fold a timing difference into a shape difference.");
    if (im > 0) s << QStringLiteral("Only impact is known on ") + W::word(im) + (im == 1 ? QStringLiteral(" shot") : QStringLiteral(" shots"))
                     + QStringLiteral(": lined up at impact, tempo differences stay in the curves.");
    if (un > 0) s << W::cap1(W::word(un)) + (un == 1 ? QStringLiteral(" shot has no impact and is") : QStringLiteral(" shots have no impact and are"))
                     + QStringLiteral(" left out.");
    return s.join(QLatin1Char(' '));
}

// The Session axis' caption, from the routes the alignment actually used (§4.1).
QString axisCaption(const Ctx &c)
{
    const SessionAlignment &al = c.w->alignment;
    const int pA = al.countRoute(AlignRoute::PAnchors), lm = al.countRoute(AlignRoute::Landmarks),
              tf = al.countRoute(AlignRoute::TempoFitted), im = al.countRoute(AlignRoute::ImpactShift);
    const int used = (pA > 0) + (lm > 0) + (tf > 0) + (im > 0);
    if (used == 0) return QString();
    if (used == 1) {
        if (pA) return QStringLiteral("ms from impact on the session's P-positions; between them, time runs at the session's median spacing");
        if (lm) return QStringLiteral("ms from impact, lined up on landmarks found in each shot's own curves (only impact was known)");
        if (tf) return QStringLiteral("ms from impact, each shot scaled about impact by a fitted tempo (only impact was known)");
        return QStringLiteral("ms from impact; shots lined up at impact only, tempo differences stay in the curves");
    }
    QStringList parts;
    if (pA) parts << QStringLiteral("%1 on P-positions").arg(pA);
    if (lm) parts << QStringLiteral("%1 on landmarks").arg(lm);
    if (tf) parts << QStringLiteral("%1 tempo-fitted").arg(tf);
    if (im) parts << QStringLiteral("%1 at impact only").arg(im);
    return QStringLiteral("ms from impact, shots aligned ") + W::list(parts);
}

// ── AGAINST ─────────────────────────────────────────────────────────────────────────────────

QString refusedSessionWhy(const Ctx &c)
{
    const int n = c.snap ? c.snap->count() : 0;
    if (n == 0) return QStringLiteral("No session is loaded.");
    return QStringLiteral("Three shots are needed to draw the session; this session has %1.").arg(W::word(n));
}

int alignedCount(const Ctx &c)
{
    int k = 0;
    for (const ShotWarp &w : c.w->alignment.shots) k += w.aligned() ? 1 : 0;
    return k;
}

void buildReference(Ctx &c, ChartsTracesPresenter::Result &R)
{
    QVariantMap out;
    const QString mode = c.in->against.isEmpty() ? QStringLiteral("none") : c.in->against;
    out.insert(QStringLiteral("mode"), mode);
    out.insert(QStringLiteral("difference"), c.in->difference);
    if (mode == QLatin1String("none") || !c.shot) {
        out.insert(QStringLiteral("active"), false);
        R.reference = out;
        return;
    }
    const auto refuse = [&](const QString &why) {
        W::Sentence s; s.append(why);
        out.insert(QStringLiteral("active"), false);
        out.insert(QStringLiteral("refused"), true);
        out.insert(QStringLiteral("why"), s.toVariant());
        R.reference = out;
    };
    const int n = c.snap ? c.snap->count() : 0;
    const bool isRef = mode == QLatin1String("reference");
    // THE ★ REFERENCE NEEDS NO SESSION: it is one swing, lined up on this shot's own clock (warpOnto).
    if (isRef) {
        if (c.in->referenceSwingDir.trimmed().isEmpty() || !c.ref) return refuse(c.refWhy);
        if (c.refIsPicked) return refuse(QStringLiteral("This swing is your reference ★."));
        if (c.shot->impactUs < 0) return refuse(QStringLiteral("This shot has no impact to line it up on."));
        if (!c.refWarp.aligned()) return refuse(QStringLiteral("The reference swing has no impact to line it up on."));
        if (c.rowKeys.isEmpty()) return refuse(QStringLiteral("No trace of this preset is drawn on this shot."));
    } else {
        if (n < 3 || alignedCount(c) < 3) return refuse(refusedSessionWhy(c));
        if (!c.aligned) return refuse(QStringLiteral("This shot has no impact to line it up on."));
        if (c.grid.empty() || c.rowKeys.isEmpty()) return refuse(QStringLiteral("No trace of this preset is drawn on enough shots to compare."));
    }
    // The session's middle half scales the head's "largest gap" when there is a session to scale it.
    const bool sessionScale = n >= 3 && alignedCount(c) >= 3 && c.aligned && !c.grid.empty();

    int refOrd = 0;
    std::vector<int> group;
    QString label, phrase;
    if (isRef) {
        label = QStringLiteral("the reference ★");
        phrase = QStringLiteral("your reference ★");
    } else if (mode == QLatin1String("median")) {
        label = QStringLiteral("session median");
        phrase = QStringLiteral("your usual swing");
    } else if (mode == QLatin1String("typical") || mode == QLatin1String("leastTypical")) {
        const bool least = mode == QLatin1String("leastTypical");
        refOrd = scoreOf(c, least, c.ordinal).ordinal;
        if (refOrd <= 0) return refuse(QStringLiteral("No shot qualifies as the session's most typical."));
        label = QStringLiteral("shot %1, the %2").arg(refOrd).arg(least ? QStringLiteral("least typical") : QStringLiteral("most typical"));
        phrase = least ? QStringLiteral("shot %1, the least typical").arg(refOrd) : QStringLiteral("your most typical swing, shot %1").arg(refOrd);
    } else if (mode == QLatin1String("previous") || mode == QLatin1String("next")) {
        const bool prev = mode == QLatin1String("previous");
        refOrd = prev ? c.ordinal - 1 : c.ordinal + 1;
        if (refOrd < 1) return refuse(QStringLiteral("Shot %1 has no shot before it.").arg(c.ordinal));
        if (refOrd > n) return refuse(QStringLiteral("Shot %1 is the last shot of the session.").arg(c.ordinal));
        if (!c.w->alignment.shots[size_t(refOrd - 1)].aligned()) return refuse(QStringLiteral("Shot %1 has no impact to line it up on.").arg(refOrd));
        label = QStringLiteral("shot %1, the %2").arg(refOrd).arg(prev ? QStringLiteral("previous") : QStringLiteral("next"));
        phrase = prev ? QStringLiteral("the shot before it") : QStringLiteral("the shot after it");
    } else if (mode == QLatin1String("like")) {
        if (c.index < 0) return refuse(QStringLiteral("This shot is not in the session yet, so it has no shape group."));
        const ShapeSplit &sp = shapeOf(c);
        if (!sp.found) return refuse(QStringLiteral("The session does not split into two shapes of at least three shots each."));
        group = std::find(sp.groupA.begin(), sp.groupA.end(), c.ordinal) != sp.groupA.end() ? sp.groupA : sp.groupB;
        if (std::find(group.begin(), group.end(), c.ordinal) == group.end())
            return refuse(QStringLiteral("This shot took no part in the shape split."));
        if (group.size() < 3) return refuse(QStringLiteral("Fewer than three shots are shaped like this one: no group fit."));
        label = QStringLiteral("%1 shots shaped like this one").arg(group.size());
        phrase = QStringLiteral("the swings shaped like it");
    } else {
        return refuse(QStringLiteral("Unknown reference."));
    }
    out.insert(QStringLiteral("active"), true);
    out.insert(QStringLiteral("refused"), false);
    out.insert(QStringLiteral("label"), label);
    out.insert(QStringLiteral("n"), mode == QLatin1String("like") ? int(group.size()) : mode == QLatin1String("median") ? n : 1);
    out.insert(QStringLiteral("group"), intList(group));
    if (refOrd > 0) { const QVariantMap ids = shotIds(c, refOrd); for (auto it = ids.begin(); it != ids.end(); ++it) out.insert(it.key(), it.value()); }
    else if (isRef) {
        // the ★ swing's own identity: its ordinal only when it is a shot of this session
        const bool inSession = c.ref->ordinal > 0 && c.snap && c.ref->ordinal <= n && c.snap->shots[size_t(c.ref->ordinal - 1)] == c.ref;
        out.insert(QStringLiteral("ordinal"), inSession ? c.ref->ordinal : 0);
        out.insert(QStringLiteral("shotId"), c.ref->shotId);
        out.insert(QStringLiteral("swingDir"), c.ref->swingDir);
        out.insert(QStringLiteral("refLabel"), c.refLabel);
    }
    else { out.insert(QStringLiteral("ordinal"), 0); out.insert(QStringLiteral("shotId"), -1); out.insert(QStringLiteral("swingDir"), QString()); }
    out.insert(QStringLiteral("diffLabel"), QStringLiteral("shot %1 − %2").arg(c.ordinal).arg(isRef ? QStringLiteral("the reference ★") : mode == QLatin1String("median") ? QStringLiteral("session median") : mode == QLatin1String("like") ? QStringLiteral("shots like it") : QStringLiteral("shot %1").arg(refOrd)));

    std::vector<int> all;
    if (c.snap) for (const auto &s : c.snap->shots) all.push_back(s->ordinal);
    const double w0 = shotMsOf(c, c.win.startUs), w1 = shotMsOf(c, c.win.endUs), sp = w1 - w0;
    QVariantList rows;
    double bestZ = -1;
    QString bestKey;
    TraceGap bestGap;
    for (const QString &k : c.rowKeys) {
        std::vector<double> rt, rv;          // the reference on THIS shot's clock
        QVariantList refBridges;             // a reference SHOT's bridged runs (a median / fit blank stays blank)
        if (isRef) {
            // the ★ swing's curve moved onto THIS shot's clock by its own P-positions; values untouched
            const TraceCurve on = alignedCurve(shotCurve(*c.ref, k), c.refWarp);
            for (size_t i = 0; i < on.t.size(); ++i) { rt.push_back(on.t[i]); rv.push_back(on.ok[i] ? on.v[i] : kNaN); }
            refBridges = bridgesOf(rt, rv, on.ok);
        } else if (mode == QLatin1String("median")) {
            const SessionEnvelope &e = envOf(c, k, all);
            for (size_t i = 0; i < e.t.size(); ++i) { rt.push_back(c.warp.unwarpMs(e.t[i])); rv.push_back(e.med[i]); }
        } else if (mode == QLatin1String("like")) {
            const GroupFit &f = fitOf(c, k, group);
            if (!f.refused()) for (size_t i = 0; i < f.t.size(); ++i) { rt.push_back(c.warp.unwarpMs(f.t[i])); rv.push_back(f.v[i]); }
        } else {
            const TraceCurve &rc = (*c.w->alignedOf(k))[size_t(refOrd - 1)];
            for (size_t i = 0; i < rc.t.size(); ++i) { rt.push_back(c.warp.unwarpMs(rc.t[i])); rv.push_back(rc.ok[i] ? rc.v[i] : kNaN); }
            refBridges = bridgesOf(rt, rv, rc.ok);
        }
        const TraceCurve mine = shotCurve(*c.shot, k);
        std::vector<double> diff = rt.empty() ? std::vector<double>{} : diffFrom(mine, rt, rv);
        const TraceGap g = gapAt(mine, rt, rv, w0 + 0.1 * sp, w1 - 0.1 * sp);
        // the strip's scale, from the data: symmetric about 0, NaN when nothing overlaps
        double diffMax = 0.0;
        for (double d : diff) if (std::isfinite(d)) diffMax = std::max(diffMax, std::abs(d));
        const QString unit = c.unitOf(k);
        QVariantMap gm{ { QStringLiteral("found"), g.found } };
        if (g.found) {
            gm.insert(QStringLiteral("shotMs"), g.t);
            gm.insert(QStringLiteral("diff"), g.diff);
            gm.insert(QStringLiteral("text"), W::figSigned(g.diff, unit));
            gm.insert(QStringLiteral("when"), W::whenImpact(g.t));
            // the gap as a share of the session's middle half there, so units do not decide the head
            // (with no session to scale it — a ★ reference on a short session — as a share of this
            // shot's own range of the curve)
            double z = 0.0;
            bool headlines = true;
            if (sessionScale) {
                const SessionEnvelope &e = envOf(c, k, all);
                const double ct = c.warp.warpMs(g.t);
                const double iqr = interpAt(e.t, e.q3, ct) - interpAt(e.t, e.q1, ct);
                const int seg = segOfKey(k);
                headlines = seg < 0 || c.w->sequence.segments[size_t(seg)].placed > 0;
                z = std::isfinite(iqr) && iqr > 0 ? std::abs(g.diff) / iqr : 0.0;
            } else {
                double lo = kNaN, hi = kNaN;
                for (size_t i = 0; i < mine.v.size(); ++i)
                    if (mine.ok[i] && std::isfinite(mine.v[i])) { lo = std::isfinite(lo) ? std::min(lo, mine.v[i]) : mine.v[i]; hi = std::isfinite(hi) ? std::max(hi, mine.v[i]) : mine.v[i]; }
                z = std::isfinite(lo) && hi > lo ? std::abs(g.diff) / (hi - lo) : 0.0;
            }
            if (headlines && z > bestZ) { bestZ = z; bestKey = k; bestGap = g; }
        }
        rows << QVariantMap{ { QStringLiteral("key"), k }, { QStringLiteral("t"), dl(rt) }, { QStringLiteral("v"), dl(rv) },
                             { QStringLiteral("diff"), dl(diff) }, { QStringLiteral("gap"), gm },
                             { QStringLiteral("bridges"), refBridges },
                             { QStringLiteral("diffLo"), diffMax > 0 ? -diffMax : kNaN }, { QStringLiteral("diffHi"), diffMax > 0 ? diffMax : kNaN } };
        ChartsTracesPresenter::NowRow *nr = nullptr;
        for (auto &x : R.now.rows) if (x.key == k) nr = &x;
        if (nr) { nr->refT = rt; nr->refV = rv; }
    }
    out.insert(QStringLiteral("rows"), rows);
    W::Sentence head;
    if (!bestKey.isEmpty())
        head.append(QStringLiteral("Shot %1 left %2 furthest ").arg(c.ordinal).arg(phrase))
            .figure(W::whenImpact(bestGap.t))
            .append(QStringLiteral(", in ") + c.labelOf(bestKey).toLower() + QStringLiteral(" ("))
            .figure(W::figSigned(bestGap.diff, c.unitOf(bestKey))).append(QStringLiteral(")"));
    else
        head.append(QStringLiteral("Shot %1 against %2").arg(c.ordinal).arg(label));
    out.insert(QStringLiteral("head"), head.toVariant());
    R.reference = out;
}

// ── SESSION ─────────────────────────────────────────────────────────────────────────────────

QVariantMap sessionSequenceBlock(Ctx &c)
{
    const SessionSequence &q = c.w->sequence;
    const int n = c.snap->count();
    QVariantMap out;
    bool any = false;
    for (const ShotSequence &s : q.shots) any = any || s.placedCount > 0;
    out.insert(QStringLiteral("present"), any && c.sequencePreset);
    if (!any || !c.sequencePreset) return out;
    double lo = 0, hi = 0;
    for (const ShotSequence &s : q.shots)
        for (const SegmentPeak &p : s.segments) if (p.placed) { lo = std::min(lo, p.shotMs); hi = std::max(hi, p.shotMs); }
    const double pad = std::max(10.0, (hi - lo) * 0.08);
    lo = std::floor((lo - pad) / 10) * 10; hi = std::ceil((hi + pad) / 10) * 10;
    out.insert(QStringLiteral("lo"), lo);
    out.insert(QStringLiteral("hi"), hi);
    std::vector<double> ticks;
    const double st = (hi - lo) > 150 ? 50 : 25;
    for (double v = std::ceil(lo / st) * st; v <= hi; v += st) if (v != 0) ticks.push_back(v);
    out.insert(QStringLiteral("ticks"), tickTexts(ticks, true));
    QVariantList pt;
    for (int ph : { int(phase::P5), int(phase::P6) })
        if (const CanonicalPhase *cp = c.w->alignment.canonicalOf(ph))
            if (cp->tMs > lo && cp->tMs < hi) pt << QVariantMap{ { QStringLiteral("tag"), c.tag(ph) }, { QStringLiteral("ms"), cp->tMs } };
    out.insert(QStringLiteral("pTicks"), pt);
    QVariantList segs;
    for (int i = 0; i < kSequenceSegments; ++i) {
        QVariantList cells, points;
        for (const ShotSequence &s : q.shots) {
            const SegmentPeak &p = s.segments[size_t(i)];
            QVariantMap ids = shotIds(c, s.ordinal);
            QVariantMap cell = ids;
            cell.insert(QStringLiteral("rank"), p.placed ? p.rank : 0);
            cell.insert(QStringLiteral("text"), p.placed ? QString::number(p.rank) : QStringLiteral("-"));
            cell.insert(QStringLiteral("late"), p.late);
            cell.insert(QStringLiteral("picked"), s.ordinal == c.ordinal && c.index >= 0);
            cells << cell;
            if (p.placed) {
                QVariantMap pm = ids;
                pm.insert(QStringLiteral("ms"), p.shotMs);
                pm.insert(QStringLiteral("late"), p.late);
                pm.insert(QStringLiteral("picked"), s.ordinal == c.ordinal && c.index >= 0);
                points << pm;
            }
        }
        const SegmentSummary &sm = q.segments[size_t(i)];
        segs << QVariantMap{ { QStringLiteral("segment"), sequenceSegmentIds()[size_t(i)] }, { QStringLiteral("word"), segWord(i) },
                             { QStringLiteral("never"), sm.placed == 0 }, { QStringLiteral("cells"), cells }, { QStringLiteral("points"), points },
                             { QStringLiteral("median"), sm.medianMs }, { QStringLiteral("q1"), sm.q1Ms }, { QStringLiteral("q3"), sm.q3Ms },
                             { QStringLiteral("placed"), sm.placed },
                             { QStringLiteral("figText"), sm.placed ? W::signedMs(sm.medianMs) + QStringLiteral(" median · middle half ")
                                                                          + W::fig(sm.q3Ms - sm.q1Ms) + QStringLiteral("\u00A0ms, %1 of %2").arg(sm.placed).arg(n)
                                                                    : QStringLiteral("-") } };
    }
    out.insert(QStringLiteral("segments"), segs);
    // the standfirst: the commonest order, the segments never placed, the pelvis after impact
    W::Sentence s;
    QStringList words;
    for (const QString &id : q.commonOrder.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const int i = int(std::find(sequenceSegmentIds().begin(), sequenceSegmentIds().end(), id) - sequenceSegmentIds().begin());
        words << segWord(i).toLower();
    }
    if (!words.isEmpty())
        s.append(W::cap1(words.join(QStringLiteral(", then "))) + QStringLiteral(" on ")).figure(QStringLiteral("%1 of %2").arg(q.commonOrderCount).arg(n))
            .append(QStringLiteral(" shots"));
    QStringList never;
    for (int i = 0; i < kSequenceSegments; ++i) if (q.segments[size_t(i)].placed == 0) never << segWord(i).toLower();
    if (!never.isEmpty()) s.append(QStringLiteral("; the ") + never.join(QStringLiteral(" and the ")) + QStringLiteral(" never placed"));
    if (!s.isEmpty()) s.append(QStringLiteral("."));
    const SegmentSummary &pel = q.segments[0];
    if (pel.afterImpact > 0) {
        s.append(QStringLiteral(" The pelvis peaks after impact on ")).figure(QStringLiteral("%1 of %2").arg(pel.afterImpact).arg(pel.placed))
            .append(QStringLiteral(" placed shots. ") + W::latePelvisNote());
    }
    out.insert(QStringLiteral("stand"), s.toVariant());
    return out;
}

void buildSession(Ctx &c, ChartsTracesPresenter::Result &R)
{
    QVariantMap out;
    const int n = c.snap ? c.snap->count() : 0;
    out.insert(QStringLiteral("n"), n);
    const int nA = c.snap ? alignedCount(c) : 0;
    out.insert(QStringLiteral("aligned"), nA);
    const auto refuse = [&](const QString &why) {
        W::Sentence s; s.append(why);
        out.insert(QStringLiteral("available"), false);
        out.insert(QStringLiteral("why"), s.toVariant());
        R.session = out;
    };
    if (n < 3) return refuse(refusedSessionWhy(c));
    if (nA < 3) return refuse(QStringLiteral("Three shots with an impact are needed to line the session up; %1 of the %2 have one.").arg(W::word(nA)).arg(W::word(n)));
    if (c.sessionKeys.isEmpty() || c.grid.empty())
        return refuse(c.presetKeys.isEmpty() ? QStringLiteral("No metric of this preset was measured on three shots.")
                                             : QStringLiteral("No trace of this preset was measured on three shots: it is read at instants only."));
    out.insert(QStringLiteral("available"), true);
    {
        // every club of the session, in strike order of first use ("7 iron and gap wedge"); past three, "mixed clubs"
        QStringList clubs;
        for (const auto &sh : c.snap->shots)
            if (!sh->club.isEmpty() && !clubs.contains(asciiLower(sh->club))) clubs << asciiLower(sh->club);
        const QString cl = clubs.isEmpty() ? QString() : clubs.size() > 3 ? QStringLiteral("mixed clubs") : W::list(clubs);
        out.insert(QStringLiteral("kicker"), QStringLiteral("%1 shots").arg(n) + (cl.isEmpty() ? QString() : QStringLiteral("  ·  ") + cl));
    }
    out.insert(QStringLiteral("axisCaption"), axisCaption(c));
    out.insert(QStringLiteral("t0"), c.t0);
    out.insert(QStringLiteral("t1"), c.t1);
    { W::Sentence a; a.append(alignmentSentence(c)); out.insert(QStringLiteral("alignNote"), a.toVariant()); }
    {
        W::Sentence cn;
        if (c.w->clubFacet && c.in->facet != QLatin1String("club")) {
            QStringList parts;
            for (const FacetGroup &g : c.w->clubFacet->groups) parts << asciiLower(g.key) + QStringLiteral(" on ") + W::word(int(g.ordinals.size()));
            cn.append(QStringLiteral("The clubs are mixed: ") + W::list(parts) + QStringLiteral("; every shot is drawn together."));
        }
        out.insert(QStringLiteral("clubNote"), cn.toVariant());
    }
    // the P ticks, with the middle half of when each P happened (the timing the warp removes)
    QVariantList ticks;
    for (const CanonicalPhase &p : c.w->alignment.canonical) {
        if (p.tMs < c.t0 - 1 || p.tMs > c.t1 + 1 || c.tag(p.phase).isEmpty()) continue;
        ticks << QVariantMap{ { QStringLiteral("phase"), p.phase }, { QStringLiteral("tag"), c.tag(p.phase) },
                              { QStringLiteral("canonMs"), p.tMs }, { QStringLiteral("q1Ms"), p.q1Ms }, { QStringLiteral("q3Ms"), p.q3Ms },
                              { QStringLiteral("n"), p.n },
                              { QStringLiteral("when"), std::abs(p.q3Ms - p.q1Ms) < 0.5 ? W::signedMs(p.q1Ms)
                                                       : W::minus(W::fig(p.q1Ms)) + QStringLiteral(" to ") + W::signedMs(p.q3Ms) } };
    }
    out.insert(QStringLiteral("ticks"), ticks);
    // the canonical ms axis (the same step rule as This shot's)
    {
        std::vector<double> ms;
        for (const QVariant &v : c.w->cm->timeTicksMs(qint64(std::llround(c.t0 * 1000.0)), qint64(std::llround(c.t1 * 1000.0)), 0))
            ms.push_back(v.toDouble());
        out.insert(QStringLiteral("msTicks"), tickTexts(ms, true));
    }

    QString band = c.in->band;
    if (band == QLatin1String("middle half")) band = QStringLiteral("middle");
    if (band == QLatin1String("every shot")) band = QStringLiteral("every");
    if (band == QLatin1String("group fit")) band = QStringLiteral("fit");
    if (band != QLatin1String("every") && band != QLatin1String("fit")) band = QStringLiteral("middle");
    const bool fit = band == QLatin1String("fit");
    out.insert(QStringLiteral("band"), band);
    QString kind;
    const std::vector<Group> groups = facetGroups(c, c.in->facet, &kind);
    const QString facet = kind == QLatin1String("none") ? QString() : c.in->facet;
    out.insert(QStringLiteral("facet"), facet);
    QVariantList gl;
    for (const Group &g : groups)
        gl << QVariantMap{ { QStringLiteral("key"), g.key }, { QStringLiteral("label"), g.label }, { QStringLiteral("short"), g.shortLabel },
                           { QStringLiteral("n"), int(g.ords.size()) }, { QStringLiteral("ordinals"), intList(g.ords) }, { QStringLiteral("own"), g.own } };
    out.insert(QStringLiteral("groups"), gl);

    std::vector<int> all;
    for (const auto &s : c.snap->shots) all.push_back(s->ordinal);
    QVariantList rows;
    struct RowInfo { QString key; PartingPoint part; double relPart = -1; std::vector<double> peaks; double mine = kNaN; int dec = 0; bool headlines = true; };
    std::vector<RowInfo> infos;
    for (const QString &k : c.sessionKeys) {
        const QString unit = c.unitOf(k);
        const int dec = W::decimalsFor(unit);
        const auto curves = c.w->alignedOf(k);
        double lo = kNaN, hi = kNaN;
        const auto grow = [&](double v) { if (std::isfinite(v)) { lo = std::isfinite(lo) ? std::min(lo, v) : v; hi = std::isfinite(hi) ? std::max(hi, v) : v; } };
        QVariantList panels;
        for (size_t gi = 0; gi < groups.size(); ++gi) {
            const Group &g = groups[gi];
            QVariantMap p{ { QStringLiteral("group"), g.key }, { QStringLiteral("label"), g.label },
                           { QStringLiteral("n"), int(curvesOf(*curves, g.ords).size()) }, { QStringLiteral("t"), dl(c.grid) } };
            if (fit) {
                const GroupFit &f = fitOf(c, k, g.ords);
                QVariantMap fm{ { QStringLiteral("refused"), f.refused() },
                                { QStringLiteral("label"), (facet.isEmpty() ? QStringLiteral("All") : g.label) + QStringLiteral("  ·  %1 shots").arg(g.ords.size())
                                                               + (f.refused() ? QString() : QStringLiteral(", fitted")) } };
                if (f.refused()) {
                    fm.insert(QStringLiteral("why"), f.refusal == FitRefusal::GroupTooSmall
                                                         ? QStringLiteral("fewer than three shots: no group fit")
                                                         : QStringLiteral("no measured samples: no group fit"));
                } else {
                    std::vector<double> flo(f.v.size()), fhi(f.v.size());
                    std::vector<double> ns(f.nShots.begin(), f.nShots.end());
                    for (size_t i = 0; i < f.v.size(); ++i) { flo[i] = f.v[i] - f.sd[i]; fhi[i] = f.v[i] + f.sd[i]; grow(flo[i]); grow(fhi[i]); }
                    fm.insert(QStringLiteral("v"), dl(f.v)); fm.insert(QStringLiteral("lo"), dl(flo)); fm.insert(QStringLiteral("hi"), dl(fhi));
                    fm.insert(QStringLiteral("nShots"), dl(ns));
                    fm.insert(QStringLiteral("why"), QString());
                }
                p.insert(QStringLiteral("fit"), fm);
                if (groups.size() > 1) {
                    const GroupFit &of = fitOf(c, k, groups[1 - std::min<size_t>(gi, 1)].ords);
                    if (!of.refused()) p.insert(QStringLiteral("alt"), QVariantMap{ { QStringLiteral("v"), dl(of.v) },
                                                                                   { QStringLiteral("label"), groups[1 - std::min<size_t>(gi, 1)].label },
                                                                                   { QStringLiteral("dashed"), true } });
                }
            } else {
                const SessionEnvelope &e = envOf(c, k, g.ords);
                p.insert(QStringLiteral("q1"), dl(e.q1)); p.insert(QStringLiteral("med"), dl(e.med)); p.insert(QStringLiteral("q3"), dl(e.q3));
                for (size_t i = 0; i < e.t.size(); ++i) { grow(e.q1[i]); grow(e.q3[i]); }
                if (band == QLatin1String("every")) {
                    QVariantList others;
                    for (const TraceCurve *tc : curvesOf(*curves, g.ords)) {
                        if (tc->ordinal == c.ordinal && c.index >= 0) continue;
                        std::vector<double> v(c.grid.size());
                        for (size_t i = 0; i < c.grid.size(); ++i) { v[i] = valueAt(*tc, c.grid[i]); grow(v[i]); }
                        QVariantMap om = shotIds(c, tc->ordinal);
                        om.insert(QStringLiteral("v"), dl(v));
                        // the grid reads NaN across a bridged run (valueAt); the dashed bridge across it
                        om.insert(QStringLiteral("bridges"), bridgesOf(tc->t, tc->v, tc->ok, c.t0, c.t1));
                        others << om;
                    }
                    p.insert(QStringLiteral("others"), others);
                }
                if (groups.size() > 1) {
                    const Group &og = groups[gi == 0 ? 1 : 0];
                    const SessionEnvelope &oe = envOf(c, k, og.ords);
                    p.insert(QStringLiteral("alt"), QVariantMap{ { QStringLiteral("v"), dl(oe.med) }, { QStringLiteral("label"), og.label },
                                                                 { QStringLiteral("dashed"), true } });
                }
            }
            panels << p;
        }
        // this shot, full strength, on the canonical axis
        QVariantMap picked;
        const TraceCurve pc = pickedAligned(c, k);
        if (!pc.empty()) {
            std::vector<double> t, v;
            QVariantList runs;
            int a = -1;
            QString st;
            std::vector<uint8_t> ok;
            for (size_t i = 0; i < pc.t.size(); ++i) {
                if (pc.t[i] < c.t0 - 40 || pc.t[i] > c.t1 + 40) continue;
                // a bridged sample's v is its RAW placeholder (session_curves.h): never drawn
                const double vi = pc.ok[i] ? pc.v[i] : kNaN;
                t.push_back(pc.t[i]); v.push_back(vi); ok.push_back(pc.ok[i]); grow(vi);
            }
            // runs over the kept samples: measured / bridged
            size_t base = 0;
            while (base < pc.t.size() && pc.t[base] < c.t0 - 40) ++base;
            for (size_t i = 0; i < t.size(); ++i) {
                const QString s = pc.ok[base + i] ? QStringLiteral("measured") : QStringLiteral("bridged");
                if (a < 0) { a = int(i); st = s; }
                if (s != st) { runs << QVariantMap{ { QStringLiteral("from"), a }, { QStringLiteral("to"), int(i) - 1 }, { QStringLiteral("style"), st } }; a = int(i); st = s; }
            }
            if (a >= 0) runs << QVariantMap{ { QStringLiteral("from"), a }, { QStringLiteral("to"), int(t.size()) - 1 }, { QStringLiteral("style"), st } };
            picked = shotIds(c, c.ordinal);
            picked.insert(QStringLiteral("t"), dl(t)); picked.insert(QStringLiteral("v"), dl(v)); picked.insert(QStringLiteral("runs"), runs);
            picked.insert(QStringLiteral("bridges"), bridgesOf(t, v, ok));
        }
        // coverage (group fit): one hairline per shot, this shot first
        QVariantList coverage;
        if (fit) {
            std::vector<int> ords;
            for (const Group &g : groups) ords.insert(ords.end(), g.ords.begin(), g.ords.end());
            std::stable_sort(ords.begin(), ords.end(), [&](int a2, int b2) { return (a2 == c.ordinal) > (b2 == c.ordinal) || ((a2 == c.ordinal) == (b2 == c.ordinal) && a2 < b2); });
            for (int o : ords) {
                const TraceCurve &tc = (*curves)[size_t(o - 1)];
                QVariantMap cm2 = shotIds(c, o);
                cm2.insert(QStringLiteral("picked"), o == c.ordinal && c.index >= 0);
                cm2.insert(QStringLiteral("runs"), tc.empty() ? QVariantList{} : runsOf(coverageRuns(tc)));
                coverage << cm2;
            }
        }
        // where the curves part (every shot), the figures
        const SessionEnvelope &ea = envOf(c, k, all);
        RowInfo info;
        info.key = k; info.dec = dec;
        info.part = partAt(ea, int(curvesOf(*curves).size()));
        {
            double mlo = kNaN, mhi = kNaN;
            for (double v : ea.med) if (std::isfinite(v)) { mlo = std::isfinite(mlo) ? std::min(mlo, v) : v; mhi = std::isfinite(mhi) ? std::max(mhi, v) : v; }
            info.relPart = info.part.found && mhi > mlo ? info.part.iqr / (mhi - mlo) : -1;
        }
        const int seg = segOfKey(k);
        info.headlines = seg < 0 || c.w->sequence.segments[size_t(seg)].placed > 0;
        info.peaks = peaksOf(c, k);
        info.mine = c.index >= 0 ? info.peaks[size_t(c.index)] : kNaN;
        if (c.index < 0 && c.calcs.contains(k) && c.calcs.constFind(k).value().winPeakOk) info.mine = c.calcs.constFind(k).value().st.value(QStringLiteral("peak")).toDouble();
        QVariantList figs;
        const auto fg = [&](const QString &l, const QString &t2) { figs << QVariantMap{ { QStringLiteral("label"), l }, { QStringLiteral("text"), t2 } }; };
        if (fit) {
            const GroupFit &f0 = fitOf(c, k, groups[0].ords);
            fg(QStringLiteral("pooled"), f0.refused() ? QStringLiteral("-") : QStringLiteral("%1 samples").arg(f0.samplesPooled));
            if (c.shot) fg(QStringLiteral("shot %1 peak").arg(c.ordinal), W::figUnit(info.mine, unit));
            QString gapText = QStringLiteral("none");
            if (!pc.empty()) {
                for (const TimeSpan &h : coverageHoles(coverageRuns(pc), c.t0, c.t1))
                    if (h.t1 - h.t0 >= 5 && h.t0 > c.t0 + 1 && h.t1 < c.t1 - 1) {
                        gapText = W::fig(c.warp.unwarpMs(h.t0)) + QStringLiteral(" to ") + W::signedMs(c.warp.unwarpMs(h.t1));
                        break;
                    }
            } else gapText = QStringLiteral("-");
            if (c.shot) fg(QStringLiteral("shot %1 gap").arg(c.ordinal), gapText);
        } else {
            fg(QStringLiteral("median peak"), W::figUnit(medianOf(info.peaks), unit));
            if (c.shot) fg(QStringLiteral("shot %1").arg(c.ordinal), W::fig(info.mine, dec) + QStringLiteral(", ") + placeWord(info.mine, info.peaks));
            if (info.part.found) fg(QStringLiteral("widest"), phaseWord(c, info.part.tCanonMs));
        }
        QVariantMap partMap{ { QStringLiteral("found"), info.part.found } };
        if (info.part.found) {
            partMap.insert(QStringLiteral("canonMs"), info.part.tCanonMs);
            partMap.insert(QStringLiteral("q1"), info.part.q1); partMap.insert(QStringLiteral("q3"), info.part.q3);
            partMap.insert(QStringLiteral("med"), info.part.med);
            partMap.insert(QStringLiteral("when"), phaseWord(c, info.part.tCanonMs));
        }
        rows << QVariantMap{ { QStringLiteral("key"), k }, { QStringLiteral("label"), c.labelOf(k) },
                             { QStringLiteral("unit"), shortUnitOf(c, unit) }, { QStringLiteral("dec"), dec },
                             { QStringLiteral("yLo"), lo }, { QStringLiteral("yHi"), hi },
                             { QStringLiteral("yTicks"), ticksAsDoubles(niceTicksList(c, lo, hi)) },
                             { QStringLiteral("panels"), panels }, { QStringLiteral("picked"), picked },
                             { QStringLiteral("coverage"), coverage }, { QStringLiteral("part"), partMap },
                             { QStringLiteral("figures"), figs } };
        infos.push_back(std::move(info));
        // the session median for the playhead's "typical"
        for (auto &nr : R.now.rows)
            if (nr.key == k) { nr.medT = ea.t; nr.medV = ea.med; }
    }
    out.insert(QStringLiteral("rows"), rows);

    // ── the head and the standfirst ──
    W::Sentence head, stand, caveat;
    const RowInfo *partRow = nullptr;
    for (const RowInfo &r : infos) if (r.headlines && r.part.found && (!partRow || r.relPart > partRow->relPart)) partRow = &r;
    if (fit) {
        const Group &g0 = groups[0];
        struct Best { QString key; TimeSpan h; double share = 0; };
        std::optional<Best> best;
        if (g0.own) for (const RowInfo &r : infos) {
            const GroupFit &f = fitOf(c, r.key, g0.ords);
            if (f.refused()) continue;
            const TraceCurve pc = pickedAligned(c, r.key);
            if (pc.empty()) continue;
            for (const TimeSpan &h : coverageHoles(coverageRuns(pc), c.t0, c.t1)) {
                if (!(h.t1 - h.t0 >= 5 && h.t0 > c.t0 + 1 && h.t1 < c.t1 - 1)) continue;
                int inN = 0, fitN = 0;
                for (size_t i = 0; i < f.t.size(); ++i) if (f.t[i] >= h.t0 && f.t[i] <= h.t1) { ++inN; fitN += std::isfinite(f.v[i]) ? 1 : 0; }
                const double share = inN ? double(fitN) / inN : 0.0;
                if (!best || h.t1 - h.t0 > best->h.t1 - best->h.t0) best = Best{ r.key, h, share };
            }
        }
        if (!best) head.append(QStringLiteral("One curve for the group, fitted to every shot’s samples"));
        else {
            const QString span = bareWhen(phaseWord(c, best->h.t0)) + QStringLiteral(" to ") + bareWhen(phaseWord(c, best->h.t1));
            if (best->share >= 0.5) head.append(QStringLiteral("The group fit spans shot %1’s gap in %2, ").arg(c.ordinal).arg(c.labelOf(best->key).toLower())).figure(span);
            else head.append(QStringLiteral("No shot measured %1 ").arg(c.labelOf(best->key).toLower())).figure(span);
        }
        QStringList who;
        if (groups.size() > 1) for (const Group &g : groups) who << QStringLiteral("the ") + W::word(int(g.ords.size())) + QStringLiteral(" ")
                                                                    + (g.key == QLatin1String("with") ? QStringLiteral("shots with ") + g.label.toLower()
                                                                       : g.key == QLatin1String("without") ? QStringLiteral("without it") : g.label.toLower());
        stand.append(QStringLiteral("One curve per group, fitted to the pooled samples of ")
                     + (who.isEmpty() ? QStringLiteral("all ") + W::word(n) + QStringLiteral(" shots") : who.join(QStringLiteral(", and ")))
                     + QStringLiteral(", P-positions aligned. It is the group’s curve, not shot %1’s; no figure for shot %1 is read from it.").arg(c.ordinal));
        if (best && best->share < 0.5) stand.append(QStringLiteral(" The fit stays empty where fewer than three shots were measured."));
    } else if (!facet.isEmpty() && groups.size() > 1) {
        head.append(groups[0].label + QStringLiteral(": ")).figure(QStringLiteral("%1 shots").arg(groups[0].ords.size()))
            .append(QStringLiteral(" against ")).figure(QString::number(groups[1].ords.size()));
        if (!infos.empty()) {
            const RowInfo &r = infos.front();
            const auto medOf = [&](const std::vector<int> &os) { std::vector<double> v; for (int o : os) v.push_back(r.peaks[size_t(o - 1)]); return medianOf(v); };
            const double a = medOf(groups[0].ords), b = medOf(groups[1].ords);
            const QString unit = c.unitOf(r.key);
            const bool half = kind == QLatin1String("half");
            stand.append(c.labelOf(r.key) + QStringLiteral(" peaks at ")).figure(W::figUnit(a, unit))
                .append(QStringLiteral(" on median ") + (half ? QStringLiteral("in the first group, ") : QStringLiteral("in the first, ")))
                .figure(W::figUnit(b, unit)).append(QStringLiteral(" in the other."));
            // gate rule: PP_GATE("rule.charts.leadToWatch")
            if (std::min(groups[0].ords.size(), groups[1].ords.size()) < 5)
                stand.append(QStringLiteral(" One group has fewer than five shots: a lead to watch, not a finding."));
        }
        if (kind == QLatin1String("condition") || kind == QLatin1String("shape")) caveat.append(W::coOccurrenceNote());
    } else if (partRow) {
        head.append(c.labelOf(partRow->key) + QStringLiteral(" parts most ")).figure(phaseWord(c, partRow->part.tCanonMs));
        const QString unit = c.unitOf(partRow->key);
        stand.append(QStringLiteral("There, half the ") + W::word(n) + QStringLiteral(" shots lie within "))
            .figure(W::fig(partRow->part.q1, partRow->dec) + QStringLiteral("–") + W::figUnit(partRow->part.q3, unit)).append(QStringLiteral("."));
        if (c.shot) {
            const QString place = placeWord(partRow->mine, partRow->peaks);
            stand.append(QStringLiteral(" Shot %1’s %2 peak is ").arg(c.ordinal).arg(c.labelOf(partRow->key).toLower())
                         + (place.contains(QStringLiteral(" of ")) ? QStringLiteral("the ") : QStringLiteral("in the ")) + place + QStringLiteral("."));
        }
    } else {
        head.append(W::cap1(W::word(n)) + QStringLiteral(" swings, one shape"));
    }
    out.insert(QStringLiteral("head"), head.toVariant());
    out.insert(QStringLiteral("standfirst"), stand.toVariant());
    out.insert(QStringLiteral("caveat"), caveat.toVariant());
    out.insert(QStringLiteral("sequence"), sessionSequenceBlock(c));

    // ── up to three data-backed questions, answered from the data ──
    QVariantList qs;
    if (partRow && facet.isEmpty()) {
        const auto curves = c.w->alignedOf(partRow->key);
        std::vector<int> lit;
        for (const TraceCurve &tc : *curves) {
            if (tc.empty()) continue;
            const double v = valueAt(tc, partRow->part.tCanonMs);
            if (std::isfinite(v) && (v < partRow->part.q1 || v > partRow->part.q3)) lit.push_back(tc.ordinal);
        }
        W::Sentence ans;
        if (lit.empty()) ans.append(QStringLiteral("No shot lies outside the middle half there."));
        else {
            const bool mine = std::find(lit.begin(), lit.end(), c.ordinal) != lit.end();
            ans.append(W::cap1(W::shotsList(lit)) + (lit.size() == 1 ? QStringLiteral(" lies") : QStringLiteral(" lie"))
                       + QStringLiteral(" outside the middle half there")
                       + (c.ordinal <= 0 ? QStringLiteral(".") : mine ? QStringLiteral(", shot %1 among them.").arg(c.ordinal)
                                                                       : QStringLiteral("; shot %1 is inside it.").arg(c.ordinal)));
        }
        qs << QVariantMap{ { QStringLiteral("id"), QStringLiteral("part") },
                           { QStringLiteral("text"), c.labelOf(partRow->key) + QStringLiteral(": which shots lie outside the middle half where the curves part?") },
                           { QStringLiteral("answer"), ans.toVariant() }, { QStringLiteral("lit"), intList(lit) },
                           { QStringLiteral("setFacet"), QString() }, { QStringLiteral("setBand"), QStringLiteral("every") } };
    }
    if (!c.w->conditionFacets.empty() && facet != c.w->conditionFacets.front().id) {
        const SessionFacet &f = c.w->conditionFacets.front();
        const QString nm = c.snap->conditions.value(f.id).name.isEmpty() ? f.id : c.snap->conditions.value(f.id).name;
        qs << QVariantMap{ { QStringLiteral("id"), QStringLiteral("facet") },
                           { QStringLiteral("text"), nm + QStringLiteral(": do the curves differ on the %1 shots it fired?").arg(f.firedCount) },
                           { QStringLiteral("answer"), W::Sentence{}.toVariant() }, { QStringLiteral("lit"), QVariantList{} },
                           { QStringLiteral("setFacet"), f.id }, { QStringLiteral("setBand"), QString() } };
    }
    if (kind != QLatin1String("half"))
        qs << QVariantMap{ { QStringLiteral("id"), QStringLiteral("half") },
                           { QStringLiteral("text"), QStringLiteral("Did the curves change between the first and second half of the session?") },
                           { QStringLiteral("answer"), W::Sentence{}.toVariant() }, { QStringLiteral("lit"), QVariantList{} },
                           { QStringLiteral("setFacet"), QStringLiteral("half") }, { QStringLiteral("setBand"), QString() } };
    while (qs.size() > 3) qs.removeLast();
    out.insert(QStringLiteral("questions"), qs);
    R.session = out;
}

// ── CHOICES ─────────────────────────────────────────────────────────────────────────────────

void buildChoices(Ctx &c, ChartsTracesPresenter::Result &R)
{
    QVariantMap out;
    out.insert(QStringLiteral("register"), c.in->reg);
    out.insert(QStringLiteral("view"), c.in->view);
    QVariantList presets;
    for (const QVariant &g : c.groups) {
        const QVariantMap m = g.toMap();
        presets << QVariantMap{ { QStringLiteral("id"), m.value(QStringLiteral("group")) }, { QStringLiteral("label"), m.value(QStringLiteral("group")) },
                                { QStringLiteral("count"), m.value(QStringLiteral("keys")).toList().size() } };
    }
    out.insert(QStringLiteral("presets"), presets);
    out.insert(QStringLiteral("preset"), c.preset);
    out.insert(QStringLiteral("presetTitle"), c.preset);
    bool custom = false;
    QVariantList metrics;
    for (const QString &k : c.presetKeys) {
        const bool on = !c.in->metricsOff.contains(k);
        custom = custom || !on;
        bool curve = false;
        if (c.shot) if (const MetricSeries *m = c.shot->metric(k)) curve = m->hasCurve() && c.w->drawsCurve(k);
        metrics << QVariantMap{ { QStringLiteral("key"), k }, { QStringLiteral("label"), c.labelOf(k) },
                                { QStringLiteral("unit"), shortUnitOf(c, c.unitOf(k)) }, { QStringLiteral("on"), on }, { QStringLiteral("curve"), curve } };
    }
    out.insert(QStringLiteral("custom"), custom);
    out.insert(QStringLiteral("metrics"), metrics);
    out.insert(QStringLiteral("windows"), c.windowChips);
    out.insert(QStringLiteral("window"), c.win.key);
    QString band = c.in->band;
    if (band == QLatin1String("middle half")) band = QStringLiteral("middle");
    if (band == QLatin1String("every shot")) band = QStringLiteral("every");
    if (band == QLatin1String("group fit")) band = QStringLiteral("fit");
    if (band != QLatin1String("every") && band != QLatin1String("fit")) band = QStringLiteral("middle");
    QVariantList bands;
    for (const auto &b : { std::pair{ "middle", "middle half" }, std::pair{ "every", "every shot" }, std::pair{ "fit", "group fit" } })
        bands << QVariantMap{ { QStringLiteral("id"), QString::fromLatin1(b.first) }, { QStringLiteral("label"), QString::fromLatin1(b.second) },
                              { QStringLiteral("current"), band == QLatin1String(b.first) } };
    out.insert(QStringLiteral("bands"), bands);
    QVariantList facets;
    const auto addF = [&](const QString &id, const QString &label, const QString &kind, int n) {
        facets << QVariantMap{ { QStringLiteral("id"), id }, { QStringLiteral("label"), label }, { QStringLiteral("kind"), kind },
                               { QStringLiteral("n"), n }, { QStringLiteral("current"), c.in->facet == id } };
    };
    addF(QString(), QStringLiteral("none"), QStringLiteral("none"), c.snap ? c.snap->count() : 0);
    if (c.snap && c.snap->count() >= 6) addF(QStringLiteral("half"), QStringLiteral("halves"), QStringLiteral("half"), c.snap->count());
    for (const SessionFacet &f : c.w->conditionFacets) {
        const QString nm = c.snap->conditions.value(f.id).name.isEmpty() ? f.id : c.snap->conditions.value(f.id).name;
        addF(f.id, nm.toLower() + QStringLiteral(" %1").arg(f.firedCount), QStringLiteral("condition"), f.firedCount);
    }
    if (c.snap && c.snap->count() >= 6 && std::isfinite(c.t0) && shapeOf(c).found)
        addF(QStringLiteral("shape"), QStringLiteral("by shape"), QStringLiteral("shape"), c.snap->count());
    if (c.w->clubFacet) addF(QStringLiteral("club"), QStringLiteral("club"), QStringLiteral("club"), int(c.w->clubFacet->groups.size()));
    out.insert(QStringLiteral("facets"), facets);
    // Against, with what each would refer to, and why one is not available
    const int n = c.snap ? c.snap->count() : 0;
    const bool sessionOk = n >= 3 && alignedCount(c) >= 3 && c.aligned && !c.grid.empty();
    const QString sessionWhy = !c.aligned ? QStringLiteral("this shot has no impact") : refusedSessionWhy(c);
    QVariantList ag;
    const auto addA = [&](const QString &id, const QString &label, bool enabled, const QString &why) {
        ag << QVariantMap{ { QStringLiteral("id"), id }, { QStringLiteral("label"), label },
                           { QStringLiteral("current"), (c.in->against.isEmpty() ? QStringLiteral("none") : c.in->against) == id },
                           { QStringLiteral("enabled"), enabled }, { QStringLiteral("why"), enabled ? QString() : why } };
    };
    addA(QStringLiteral("none"), QStringLiteral("nothing"), true, QString());
    // the ★ reference first (the picker lists it first): any session's swing, so no session is needed
    {
        const bool set = !c.in->referenceSwingDir.trimmed().isEmpty();
        const bool ok = set && c.ref && !c.refIsPicked && c.shot && c.shot->impactUs >= 0 && c.refWarp.aligned();
        const QString why = !set ? QStringLiteral("no reference swing is set: ★ marks one")
                          : !c.ref ? c.refWhy
                          : c.refIsPicked ? QStringLiteral("this swing is your reference")
                          : !c.shot || c.shot->impactUs < 0 ? QStringLiteral("this shot has no impact")
                          : QStringLiteral("the reference swing has no impact to line it up on");
        addA(QStringLiteral("reference"), c.ref && !c.refLabel.isEmpty() ? QStringLiteral("the reference ★, ") + c.refLabel : QStringLiteral("the reference ★"),
             ok, why);
    }
    addA(QStringLiteral("median"), QStringLiteral("the session median"), sessionOk, sessionWhy);
    const int typ = sessionOk ? scoreOf(c, false, c.ordinal).ordinal : 0;
    const int lst = sessionOk ? scoreOf(c, true, c.ordinal).ordinal : 0;
    addA(QStringLiteral("typical"), typ > 0 ? QStringLiteral("the most typical, shot %1").arg(typ) : QStringLiteral("the most typical"), typ > 0, sessionWhy);
    addA(QStringLiteral("leastTypical"), lst > 0 ? QStringLiteral("the least typical, shot %1").arg(lst) : QStringLiteral("the least typical"), lst > 0, sessionWhy);
    addA(QStringLiteral("previous"), QStringLiteral("the previous shot"), sessionOk && c.ordinal > 1, QStringLiteral("there is no shot before this one"));
    addA(QStringLiteral("next"), QStringLiteral("the next shot"), sessionOk && c.ordinal < n, QStringLiteral("this is the last shot"));
    int likeN = 0;
    if (sessionOk && c.index >= 0) {
        const ShapeSplit &sp = shapeOf(c);
        if (sp.found) likeN = int((std::find(sp.groupA.begin(), sp.groupA.end(), c.ordinal) != sp.groupA.end() ? sp.groupA : sp.groupB).size());
    }
    addA(QStringLiteral("like"), likeN > 0 ? QStringLiteral("shots like this one (%1)").arg(likeN) : QStringLiteral("shots like this one"), likeN >= 3,
         QStringLiteral("the session does not split into two shapes of three or more"));
    out.insert(QStringLiteral("against"), ag);
    out.insert(QStringLiteral("difference"), c.in->difference);
    R.choices = out;
}

} // namespace

// ── the job ─────────────────────────────────────────────────────────────────────────────────

static std::shared_ptr<ChartsTracesPresenter::Result> computeAll(ChartsTracesPresenter::Worker &w,
                                                                 const ChartsTracesPresenter::Inputs &in)
{
    QMutexLocker lk(&w.mutex);
    QElapsedTimer all, part;
    all.start();
    w.ensureTools();
    auto R = std::make_shared<ChartsTracesPresenter::Result>();
    const bool hit = w.refresh(in.snapshot);
    Ctx c;
    c.w = &w;
    c.in = &in;
    c.snap = w.snap;

    // the picked shot: its own detail first (a live shot, another session's, n < 3), else the snapshot's
    const QString want = cleanDir(in.pickedSwingDir);
    if (c.snap && !want.isEmpty())
        for (size_t i = 0; i < c.snap->shots.size(); ++i)
            if (cleanDir(c.snap->shots[i]->swingDir) == want) { c.index = int(i); break; }
    if (!in.pickedDetail.value(QStringLiteral("series")).toList().isEmpty()) {
        auto sh = shotFromDetail(in.pickedDetail, &c.seriesMaps);
        if (c.index >= 0) {
            const ShotSeries &s = *c.snap->shots[size_t(c.index)];
            sh->ordinal = s.ordinal; sh->shotId = s.shotId; sh->club = s.club; sh->swingDir = s.swingDir;
            sh->diagnostics = s.diagnostics;
            if (sh->kinematicSequence.isEmpty()) sh->kinematicSequence = s.kinematicSequence;
        } else {
            sh->ordinal = (c.snap ? c.snap->count() : 0) + 1;
            sh->shotId = SessionSeriesModel::folderShotId(want);
            sh->swingDir = want;
        }
        c.shot = sh;
    } else if (c.index >= 0) {
        c.shot = c.snap->shots[size_t(c.index)];
    }
    if (c.shot) {
        c.ordinal = c.shot->ordinal;
        c.shotId = c.shot->shotId;
        c.swingDir = c.shot->swingDir;
        if (c.index >= 0 && size_t(c.index) < w.alignment.shots.size()) c.warp = w.alignment.shots[size_t(c.index)];
        else if (!w.alignment.canonical.empty()) c.warp = warpAgainst(*c.shot, w.alignment);
        c.aligned = c.warp.aligned();
        c.seq = shotSequence(*c.shot);
    }
    // the ★ reference swing, whenever one is set (Against lists it whatever is picked)
    if (!in.referenceSwingDir.trimmed().isEmpty()) {
        c.ref = referenceShot(w, in.referenceSwingDir, c.snap, &c.refWhy);
        if (c.ref) {
            c.refLabel = referenceLabelOf(c.ref->swingDir);
            c.refIsPicked = c.shot && cleanDir(c.ref->swingDir) == cleanDir(c.shot->swingDir);
            if (c.shot) c.refWarp = warpOnto(*c.ref, *c.shot);
        }
    } else {
        c.refWhy = QStringLiteral("No reference swing is set: ★ marks one, and it stays your reference across sessions.");
    }
    resolvePreset(c);
    resolveWindow(c);
    if (c.snap && c.snap->count() > 0) resolveSessionWindow(c);

    part.start();
    buildThisShot(c, *R);
    const qint64 shotNs = part.nsecsElapsed();
    part.restart();
    if (in.view != QLatin1String("session")) buildReference(c, *R);
    else R->reference = QVariantMap{ { QStringLiteral("mode"), in.against }, { QStringLiteral("active"), false } };
    const qint64 refNs = part.nsecsElapsed();
    part.restart();
    if (in.view != QLatin1String("shot")) buildSession(c, *R);
    else R->session = QVariantMap{ { QStringLiteral("available"), false } };
    const qint64 sesNs = part.nsecsElapsed();
    buildChoices(c, *R);

    // what the GUI thread keeps for the playhead and the pointer
    R->now.hasShot = bool(c.shot);
    R->now.impactUs = c.shot ? c.shot->impactUs : -1;
    R->now.pickedOrdinal = c.ordinal;
    R->now.pickedAligned = c.aligned;
    R->now.pickedWarp = c.warp;
    R->now.snapshot = c.snap;
    R->now.warps = w.alignment.shots;
    for (const QString &k : c.sessionKeys) R->now.aligned.insert(k, w.alignedOf(k));

    R->timings = QVariantMap{ { QStringLiteral("jobMs"), all.nsecsElapsed() / 1e6 }, { QStringLiteral("thisShotMs"), shotNs / 1e6 },
                              { QStringLiteral("referenceMs"), refNs / 1e6 }, { QStringLiteral("sessionMs"), sesNs / 1e6 },
                              { QStringLiteral("cacheHit"), hit }, { QStringLiteral("revision"), qulonglong(w.revision) } };
    return R;
}

// ── the object ──────────────────────────────────────────────────────────────────────────────

ChartsTracesPresenter::ChartsTracesPresenter(QObject *parent)
    : QObject(parent), m_worker(std::make_shared<Worker>())
{
    m_pool.setMaxThreadCount(1);
    m_pool.setObjectName(QStringLiteral("ChartsTracesPresenter"));
    schedule();
}

ChartsTracesPresenter::~ChartsTracesPresenter()
{
    m_pool.waitForDone();
}

QObject *ChartsTracesPresenter::model() const { return m_model.data(); }

void ChartsTracesPresenter::setModel(QObject *m)
{
    if (m_model == m) return;
    if (m_model) disconnect(m_model, nullptr, this, nullptr);
    m_model = m;
    if (auto *sm = qobject_cast<SessionSeriesModel *>(m))
        connect(sm, &SessionSeriesModel::snapshotChanged, this, &ChartsTracesPresenter::onSnapshotChanged);
    emit inputsChanged();
    schedule();
}

void ChartsTracesPresenter::onSnapshotChanged() { schedule(); }

void ChartsTracesPresenter::setSnapshot(std::shared_ptr<const SessionSnapshot> snapshot)
{
    m_directSnapshot = std::move(snapshot);
    schedule();
}

std::shared_ptr<const SessionSnapshot> ChartsTracesPresenter::currentSnapshot() const
{
    if (auto *sm = qobject_cast<SessionSeriesModel *>(m_model.data())) return sm->snapshot();
    return m_directSnapshot;
}

#define PP_SETTER(Type, name, field)                                       \
    void ChartsTracesPresenter::name(Type v)                               \
    {                                                                      \
        if (m_in.field == v) return;                                       \
        m_in.field = v;                                                    \
        emit inputsChanged();                                              \
        schedule();                                                        \
    }
PP_SETTER(const QString &, setPickedSwingDir, pickedSwingDir)
PP_SETTER(const QString &, setRegisterName, reg)
PP_SETTER(const QString &, setView, view)
PP_SETTER(const QString &, setPreset, preset)
PP_SETTER(const QStringList &, setMetricsOff, metricsOff)
PP_SETTER(const QString &, setWindow, window)
PP_SETTER(qint64, setCustomStartUs, customStartUs)
PP_SETTER(qint64, setCustomEndUs, customEndUs)
PP_SETTER(qint64, setSpanUs, spanUs)
PP_SETTER(const QString &, setBand, band)
PP_SETTER(const QString &, setFacet, facet)
PP_SETTER(const QString &, setAgainst, against)
PP_SETTER(bool, setDifference, difference)
PP_SETTER(const QString &, setReferenceSwingDir, referenceSwingDir)
#undef PP_SETTER

void ChartsTracesPresenter::setPickedDetail(const QVariantMap &d)
{
    // QVariantMap equality is a deep compare; the replay hands a new map per swing, so identity of
    // the shared data is the cheap and sufficient test.
    if (m_in.pickedDetail.isSharedWith(d)) return;
    m_in.pickedDetail = d;
    emit inputsChanged();
    schedule();
}

void ChartsTracesPresenter::setPlayheadUs(qint64 us)
{
    if (m_playheadUs == us) return;
    m_playheadUs = us;
    emit playheadChanged();
    if (!m_synchronous || !m_dirty) updateNow(false);
}

QVariantMap ChartsTracesPresenter::choices() { flushIfSynchronous(); return m_result ? m_result->choices : QVariantMap{}; }
QVariantMap ChartsTracesPresenter::thisShot() { flushIfSynchronous(); return m_result ? m_result->thisShot : QVariantMap{}; }
QVariantMap ChartsTracesPresenter::reference() { flushIfSynchronous(); return m_result ? m_result->reference : QVariantMap{}; }
QVariantMap ChartsTracesPresenter::session() { flushIfSynchronous(); return m_result ? m_result->session : QVariantMap{}; }
QVariantMap ChartsTracesPresenter::now() { flushIfSynchronous(); return m_now; }

void ChartsTracesPresenter::schedule()
{
    m_dirty = true;
    if (m_synchronous || m_scheduled) return;
    m_scheduled = true;
    QMetaObject::invokeMethod(this, &ChartsTracesPresenter::startJob, Qt::QueuedConnection);
}

void ChartsTracesPresenter::startJob()
{
    m_scheduled = false;
    if (m_synchronous) return;
    if (m_running) return;            // apply() re-runs it, dirty
    Inputs in = m_in;
    in.snapshot = currentSnapshot();
    m_dirty = false;
    m_running = true;
    emit busyChanged();
    auto w = m_worker;
    m_pool.start([this, w, in]() {
        auto r = computeAll(*w, in);
        QMetaObject::invokeMethod(this, [this, r]() { apply(r); }, Qt::QueuedConnection);
    });
}

void ChartsTracesPresenter::apply(std::shared_ptr<Result> r)
{
    const bool wasRunning = m_running;
    m_running = false;
    m_result = std::move(r);
    m_timings = m_result->timings;
    emit resultsChanged();
    updateNow(true);
    if (m_dirty && !m_synchronous) startJob();
    else if (wasRunning) emit busyChanged();
}

void ChartsTracesPresenter::flushIfSynchronous()
{
    if (!m_synchronous || !m_dirty) return;
    m_dirty = false;
    Inputs in = m_in;
    in.snapshot = currentSnapshot();
    apply(computeAll(*m_worker, in));
}

void ChartsTracesPresenter::setSynchronous(bool on)
{
    if (m_synchronous == on) return;
    if (on) m_pool.waitForDone();
    m_synchronous = on;
    m_dirty = true;
    if (!on) schedule();
}

bool ChartsTracesPresenter::waitForIdle(int msTimeout)
{
    if (m_synchronous) { flushIfSynchronous(); return true; }
    QDeadlineTimer deadline(msTimeout);
    while ((m_running || m_scheduled || m_dirty) && !deadline.hasExpired()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        m_pool.waitForDone(20);
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return !m_running && !m_scheduled && !m_dirty;
}

// ── the playhead and the pointer: GUI-thread lookups into the cached arrays ─────────────────

void ChartsTracesPresenter::updateNow(bool force)
{
    QVariantMap out;
    const NowCache *nc = m_result ? &m_result->now : nullptr;
    if (!nc || !nc->hasShot || m_playheadUs < 0) {
        out.insert(QStringLiteral("valid"), false);
        if (force || m_now != out) { m_now = out; emit nowChanged(); }
        return;
    }
    const double shotMs = nc->impactUs >= 0 ? double(m_playheadUs - nc->impactUs) / 1000.0 : double(m_playheadUs) / 1000.0;
    const double canonMs = nc->pickedAligned ? nc->pickedWarp.warpMs(shotMs) : kNaN;
    QVariantList rows;
    QString sig = W::signedMs(shotMs);
    for (const NowRow &r : nc->rows) {
        double v = kNaN;
        bool measured = false;
        if (!r.tUs.empty() && m_playheadUs >= r.tUs.front() && m_playheadUs <= r.tUs.back()) {
            const size_t i = nearestIndex(r.tUs, m_playheadUs);
            measured = r.measured[i] != 0;
            if (measured) v = r.mean[i];
        }
        const double typ = interpAt(r.medT, r.medV, canonMs);
        const double ref = interpAt(r.refT, r.refV, shotMs);
        const QString text = W::figUnit(v, r.unit);
        const QString addr = std::isfinite(v) && std::isfinite(r.addr) ? QStringLiteral("Δ ") + W::figSigned(v - r.addr, r.unit) : QString();
        const QString typT = W::figUnit(typ, r.unit), refT = r.refT.empty() ? QString() : W::figUnit(ref, r.unit);
        const QString gapT = std::isfinite(v) && std::isfinite(ref) ? W::figSigned(v - ref, r.unit) : (r.refT.empty() ? QString() : QStringLiteral("-"));
        sig += QLatin1Char('|') + text + addr + typT + refT + gapT;
        rows << QVariantMap{ { QStringLiteral("key"), r.key }, { QStringLiteral("value"), v }, { QStringLiteral("text"), text },
                             { QStringLiteral("measured"), measured }, { QStringLiteral("addrText"), addr },
                             { QStringLiteral("typical"), typ }, { QStringLiteral("typicalText"), r.medT.empty() ? QString() : typT },
                             { QStringLiteral("refText"), refT }, { QStringLiteral("gapText"), gapT } };
    }
    out = QVariantMap{ { QStringLiteral("valid"), true }, { QStringLiteral("us"), m_playheadUs }, { QStringLiteral("shotMs"), shotMs },
                       { QStringLiteral("canonMs"), canonMs }, { QStringLiteral("when"), W::signedMs(shotMs) },
                       { QStringLiteral("rows"), rows }, { QStringLiteral("sig"), sig } };
    // throttled to changes of a printed value: the playhead moving inside one sample says nothing new
    if (!force && m_now.value(QStringLiteral("sig")).toString() == sig) return;
    m_now = out;
    emit nowChanged();
}

qint64 ChartsTracesPresenter::seekUsFor(int ordinal, double canonMs) const
{
    const NowCache *nc = m_result ? &m_result->now : nullptr;
    if (!nc || !std::isfinite(canonMs)) return -1;
    if (ordinal == nc->pickedOrdinal && nc->pickedAligned) return nc->pickedWarp.usAtCanonMs(canonMs);
    if (ordinal >= 1 && size_t(ordinal) <= nc->warps.size() && nc->warps[size_t(ordinal - 1)].aligned())
        return nc->warps[size_t(ordinal - 1)].usAtCanonMs(canonMs);
    return -1;
}

qint64 ChartsTracesPresenter::seekUsForShotMs(double shotMs) const
{
    const NowCache *nc = m_result ? &m_result->now : nullptr;
    if (!nc || !nc->hasShot || nc->impactUs < 0 || !std::isfinite(shotMs)) return -1;
    return nc->impactUs + qint64(std::llround(shotMs * 1000.0));
}

double ChartsTracesPresenter::canonMsForShotMs(double shotMs) const
{
    const NowCache *nc = m_result ? &m_result->now : nullptr;
    return nc && nc->pickedAligned ? nc->pickedWarp.warpMs(shotMs) : kNaN;
}

QVariantMap ChartsTracesPresenter::shotAt(const QString &key, double canonMs, double value) const
{
    QVariantMap out{ { QStringLiteral("found"), false } };
    const NowCache *nc = m_result ? &m_result->now : nullptr;
    if (!nc || !nc->snapshot) return out;
    const auto it = nc->aligned.constFind(key);
    if (it == nc->aligned.constEnd()) return out;
    double best = std::numeric_limits<double>::infinity();
    int ord = 0;
    double bv = kNaN;
    for (const TraceCurve &c : *it.value()) {
        if (c.empty()) continue;
        const double v = valueAt(c, canonMs);
        if (std::isfinite(v) && std::abs(v - value) < best) { best = std::abs(v - value); ord = c.ordinal; bv = v; }
    }
    if (ord <= 0 || ord > nc->snapshot->count()) return out;
    const ShotSeries &s = *nc->snapshot->shots[size_t(ord - 1)];
    return { { QStringLiteral("found"), true }, { QStringLiteral("ordinal"), ord }, { QStringLiteral("shotId"), s.shotId },
             { QStringLiteral("swingDir"), s.swingDir }, { QStringLiteral("value"), bv } };
}

QVariantMap ChartsTracesPresenter::hoverAt(double shotMs) const
{
    const NowCache *nc = m_result ? &m_result->now : nullptr;
    if (!nc || !nc->hasShot || !std::isfinite(shotMs)) return QVariantMap{ { QStringLiteral("valid"), false } };
    const qint64 us = (nc->impactUs >= 0 ? nc->impactUs : 0) + qint64(std::llround(shotMs * 1000.0));
    QVariantList rows;
    for (const NowRow &r : nc->rows) {
        QString text = QStringLiteral("-"), raw;
        bool measured = false;
        if (!r.tUs.empty()) {
            const size_t i = nearestIndex(r.tUs, us);
            measured = r.measured[i] != 0;
            if (measured) {
                text = W::figUnit(r.mean[i], r.unit);
                raw = QStringLiteral("raw ") + W::fig(r.raw[i], r.dec);
            }
        }
        rows << QVariantMap{ { QStringLiteral("key"), r.key }, { QStringLiteral("label"), r.label }, { QStringLiteral("text"), text },
                             { QStringLiteral("raw"), raw }, { QStringLiteral("measured"), measured }, { QStringLiteral("grossWarn"), r.grossWarn } };
    }
    return { { QStringLiteral("valid"), true }, { QStringLiteral("shotMs"), shotMs },
             { QStringLiteral("when"), W::signedMs(shotMs) + QStringLiteral(" · impact") }, { QStringLiteral("rows"), rows } };
}

} // namespace pinpoint::charts
