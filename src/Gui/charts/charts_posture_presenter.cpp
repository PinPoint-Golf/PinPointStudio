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

#include "charts_posture_presenter.h"

#include "box_map.h"
#include "charts_words.h"
#include "posture_picture.h"
#include "session_posture.h"
#include "session_series_model.h"

#include "../../Core/pp_debug.h"
#include "../../Diagnostics/engine/characteristic.h"
#include "../../Diagnostics/engine/condition_reading.h"
#include "../../Export/swing_store.h"
#include "../../Coaching/diagnostic_ledger.h"   // the ledger's own deserialiser: the earlier sessions' readings

#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QHash>
#include <QJsonObject>
#include <QLocale>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>
#include <QSet>
#include <QThreadPool>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

// The value types the worker keeps. A NAMED namespace, not an anonymous one: Worker (a member type of a
// class with external linkage) holds them.
namespace pinpoint::charts::postureview {

// One shot's reading, keyed by the shot's shared_ptr: a re-analysed shot is a new pointer.
struct CacheEntry {
    std::weak_ptr<const ShotSeries> shot;
    PostureShotInput in;
};

} // namespace pinpoint::charts::postureview

namespace pinpoint::charts {

using namespace postureview;
namespace W = words;
namespace pa = pinpoint::analysis;

// ── the result and the worker ───────────────────────────────────────────────────────────────

struct ChartsPosturePresenter::Result {
    bool available = false;
    QVariantMap choices, thisShot, session;
    QVariantList standfirsts;                          // 8 Sentences (thisShot.standfirsts)
    std::array<qint64, kPosturePositions> posUs{};     // the picked shot's P µs, −1 none
    QVariantList pendingStills;                        // the picture's stills not made yet (thisShot.picture)
};

struct ChartsPosturePresenter::Worker {
    QMutex mutex;                                      // held for a whole job
    QHash<const ShotSeries *, CacheEntry> cache;

    // the ★ reference swing outside the session, by its cleaned dir and document stamp
    QString refDir;
    qint64  refSize = -1, refMtimeMs = -1;
    bool    refOk = false;
    PostureShotInput refIn;
    QString refWhy;

    // the picture strip's documents (posture_picture.h), by cleaned dir and document stamp; a few kept
    struct PictureEntry { qint64 size = -1, mtimeMs = -1; bool pinned = false; PictureDoc doc; };
    QHash<QString, PictureEntry> pictures;
    QStringList pictureOrder;                          // least recent first
    // the stills the maker could not make: outPath → why (written on the GUI thread, read by a job)
    QMutex stillMutex;
    QHash<QString, QString> stillErrors;

    // the "At address" block's history (the golfer's earlier sessions from the same set-up), by the session folder
    // and the stamp of every sibling ledger (readAddressHistory)
    QString histStamp;
    PostureAddressHistory hist;
};

namespace {

constexpr int kPos = kPosturePositions;
const QString kTrailKneeCondition = QStringLiteral("trail_knee_straighten");
// Fired, it is a Finding though it names no departure (§4.3, 22 Oct): several parts at once, a composite.
const QString kLossOfPosture = QStringLiteral("loss_of_posture");

// ── small words and numbers ─────────────────────────────────────────────────────────────────

bool fin(double v) { return std::isfinite(v); }
QVariant nv(double v) { return fin(v) ? QVariant(v) : QVariant::fromValue(nullptr); }
QVariant nullv() { return QVariant::fromValue(nullptr); }
QString cleanDir(const QString &d) { return d.trimmed().isEmpty() ? QString() : QDir::cleanPath(d.trimmed()); }
QString golferSafe(const QString &t) { return t.contains(QLatin1Char('`')) ? QString() : t; }   // a backtick = an author's note

QString joinUnit(const QString &fig, const QString &unit)
{
    if (fig == QLatin1String("-") || unit.isEmpty()) return fig;
    return unit == QStringLiteral("°") ? fig + unit : fig + QChar(0x00A0) + unit;
}
const PostureRowSpec &specOf(PostureRow r) { return postureRowSpec(r); }
QString valueText(PostureRow r, double v) { return joinUnit(postureFigure(r, v), specOf(r).unit); }
QString bareText(PostureRow r, double v) { return joinUnit(W::fig(std::fabs(v), specOf(r).decimals), specOf(r).unit); }

bool hasPosture(const PostureShotInput &in) { return std::any_of(in.present.begin(), in.present.end(), [](bool b) { return b; }); }

qint64 phaseUs(const ShotSeries &s, int ph)
{
    for (const PhaseMark &p : s.phases) if (p.phase == ph) return p.tUs;
    return -1;
}
std::array<qint64, kPos> positionUs(const ShotSeries *s)
{
    std::array<qint64, kPos> out;
    out.fill(-1);
    if (!s) return out;
    for (int p = 0; p < kPos; ++p) out[size_t(p)] = phaseUs(*s, posturePhaseIds()[size_t(p)]);
    return out;
}
qint64 usAt(const std::array<qint64, kPos> &us, int pos) { return pos >= 0 && pos < kPos ? us[size_t(pos)] : -1; }

// "P5–P7" (a run), "P3, P5" (not), "P5" (one)
QString rangeText(std::vector<int> pos)
{
    std::sort(pos.begin(), pos.end());
    pos.erase(std::unique(pos.begin(), pos.end()), pos.end());
    if (pos.empty()) return QString();
    if (pos.size() == 1) return posturePositionTag(pos.front());
    bool run = true;
    for (size_t i = 1; i < pos.size(); ++i) run = run && pos[i] == pos[i - 1] + 1;
    if (run) return posturePositionTag(pos.front()) + QChar(0x2013) + posturePositionTag(pos.back());
    QStringList t;
    for (int p : pos) t << posturePositionTag(p);
    return t.join(QStringLiteral(", "));
}
QStringList tagsOf(std::vector<int> pos)
{
    std::sort(pos.begin(), pos.end());
    pos.erase(std::unique(pos.begin(), pos.end()), pos.end());
    QStringList t;
    for (int p : pos) t << posturePositionTag(p);
    return t;
}

// A row's own scale: address (0), values, band edges, fault edges, ghost and boxes; at least `minSpan`.
struct Span {
    double lo = 0.0, hi = 0.0;
    void add(double v) { if (fin(v)) { lo = std::min(lo, v); hi = std::max(hi, v); } }
    void finish(double minSpan)
    {
        if (hi - lo < minSpan) { const double m = (hi + lo) / 2; lo = m - minSpan / 2; hi = m + minSpan / 2; }
        const double pad = 0.08 * (hi - lo);
        lo -= pad; hi += pad;
    }
};
double minSpanOf(PostureRow r) { return specOf(r).unit == QStringLiteral("cm") ? 4.0 : 10.0; }

// ── shots ───────────────────────────────────────────────────────────────────────────────────

// The keys postureInputFromShot reads: the six rows' series, the butt line, and every rung of the absolutes' ladders.
QSet<QString> postureKeys(const PostureEnv &env)
{
    QSet<QString> keys;
    for (const PostureRowSpec &s : postureRowSpecs()) keys.insert(s.metricKey);
    keys.insert(postureHipButtKey());   // hip depth at P3–P5 reads the butt line (session_posture.h decision 15)
    for (const PostureAbsoluteSpec &a : env.absolutes) {
        keys.insert(a.metricKey);
        if (const pa::Measure *m = env.pack ? env.pack->measure(a.measureId) : nullptr)
            for (const QString &k : pa::measureKeyLadder(*m)) keys.insert(k);
    }
    return keys;
}

// shotReplay.analysisDetail → the part of a ShotSeries the posture core reads (phases, impact, gates, the
// posture series with their phase samples), on the same window-relative clock. For the picked shot when
// it is not in the snapshot yet (the wrist presenter's wristShotFromDetail; the traces' shotFromDetail).
std::shared_ptr<ShotSeries> postureShotFromDetail(const QVariantMap &d, const QString &dir, const QSet<QString> &keys)
{
    auto sh = std::make_shared<ShotSeries>();
    sh->swingDir = dir;
    sh->shotId = SessionSeriesModel::folderShotId(dir);
    sh->club = d.value(QStringLiteral("club")).toString();
    sh->gatesVersion = d.value(QStringLiteral("gatesVersion")).toInt();
    sh->gates = d.value(QStringLiteral("gates")).toList();
    for (const QVariant &pv : d.value(QStringLiteral("phases")).toList()) {
        const QVariantMap p = pv.toMap();
        PhaseMark pm;
        pm.phase = p.value(QStringLiteral("phase"), -1).toInt();
        pm.tUs = p.value(QStringLiteral("t_us")).toLongLong();
        pm.conf = p.contains(QStringLiteral("conf")) ? p.value(QStringLiteral("conf")).toDouble() : kNaN;
        if (pm.phase == int(pa::Phase::Impact) && sh->impactUs < 0) sh->impactUs = pm.tUs;
        sh->phases.push_back(pm);
    }
    if (sh->impactUs < 0 && d.contains(QStringLiteral("impactUs"))) sh->impactUs = d.value(QStringLiteral("impactUs")).toLongLong();
    for (const QVariant &sv : d.value(QStringLiteral("series")).toList()) {
        const QVariantMap s = sv.toMap();
        MetricSeries m;
        m.key = s.value(QStringLiteral("key")).toString();
        if (m.key.isEmpty() || !keys.contains(m.key) || sh->metrics.contains(m.key)) continue;
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
        const size_t n = std::min(m.tUs.size(), m.value.size());
        m.tUs.resize(n);
        m.value.resize(n);
        if (!m.valid.empty()) m.valid.resize(n, 1);
        m.sigma = s.contains(QStringLiteral("sigma")) ? s.value(QStringLiteral("sigma")).toDouble() : kNaN;
        m.sigmaKind = s.contains(QStringLiteral("sigmaKind")) ? s.value(QStringLiteral("sigmaKind")).toInt() : -1;
        m.lineAngle = s.value(QStringLiteral("lineAngle")).toBool();
        for (const QVariant &pv : s.value(QStringLiteral("phaseSamples")).toList()) {
            const QVariantMap p = pv.toMap();
            PhaseSample ps;
            ps.phase = p.value(QStringLiteral("phase"), -1).toInt();
            ps.tUs = p.value(QStringLiteral("t_us")).toLongLong();
            ps.value = p.value(QStringLiteral("value"), kNaN).toDouble();
            ps.sigma = p.contains(QStringLiteral("sigma")) ? p.value(QStringLiteral("sigma")).toDouble() : kNaN;
            ps.band = p.value(QStringLiteral("band")).toString();
            m.phaseSamples.push_back(ps);
        }
        sh->metricOrder << m.key;
        sh->metrics.insert(m.key, std::move(m));
    }
    return sh;
}

QString referenceLabelOf(const QString &dir)
{
    const QFileInfo fi(dir);
    const int id = SessionSeriesModel::folderShotId(dir);
    const QDate d = QDate::fromString(QFileInfo(fi.absolutePath()).fileName().left(10), QStringLiteral("yyyy-MM-dd"));
    QStringList parts;
    if (id >= 0) parts << QStringLiteral("shot %1").arg(id);
    if (d.isValid()) parts << QLocale(QLocale::English).toString(d, QStringLiteral("d MMM yyyy"));
    return parts.join(QStringLiteral(", "));
}

// ── the job's context ───────────────────────────────────────────────────────────────────────

struct Ctx {
    ChartsPosturePresenter::Worker &w;
    const ChartsPosturePresenter::Inputs &in;
    std::shared_ptr<const SessionSnapshot> snap;
    const PostureEnv &env;
    Ctx(ChartsPosturePresenter::Worker &wk, const ChartsPosturePresenter::Inputs &i,
        std::shared_ptr<const SessionSnapshot> s, const PostureEnv &e)
        : w(wk), in(i), snap(std::move(s)), env(e) {}
    int n = 0;
    std::vector<PostureShotInput> inputs;        // parallel to snap->shots
    int ready = 0;                               // shots with a posture reading
    int index = -1;                              // the picked shot's index, −1 none
    std::shared_ptr<const ShotSeries> shot;      // the picked shot (the snapshot's, or its own detail)
    PostureShotInput pick;
    bool hasPick = false;                        // the picked shot reads a posture row
    PostureSessionView sess;
    PostureShotView view;
    std::array<qint64, kPos> pUs{};
    // the ★ reference
    bool refSet = false, refOk = false, refIsPicked = false;
    PostureShotInput refIn;
    QString refWhy, refLabel, refDir;
};

// The reference: the session's own reading when it is one of its shots, else SwingStore::loadAnalysis →
// extractShot → postureInputFromShot, cached by the document's stamp (the wrist presenter's rule).
void resolveReference(Ctx &c)
{
    const QString dir = cleanDir(c.in.referenceSwingDir);
    c.refDir = dir;
    c.refSet = !dir.isEmpty();
    if (!c.refSet) { c.refWhy = QStringLiteral("No reference swing is set: ★ marks one, and it stays your reference across sessions."); return; }
    c.refLabel = referenceLabelOf(dir);
    c.refIsPicked = c.shot && cleanDir(c.shot->swingDir) == dir;
    for (int i = 0; i < c.n; ++i)
        if (cleanDir(c.snap->shots[size_t(i)]->swingDir) == dir) { c.refOk = true; c.refIn = c.inputs[size_t(i)]; return; }
    ChartsPosturePresenter::Worker &w = c.w;
    const pinpoint::SwingStore::DocInfo info = pinpoint::SwingStore::info(dir);
    if (!(dir == w.refDir && info.size == w.refSize && info.mtimeMs == w.refMtimeMs)) {
        w.refDir = dir; w.refSize = info.size; w.refMtimeMs = info.mtimeMs;
        w.refOk = false; w.refIn = PostureShotInput{}; w.refWhy.clear();
        if (!info.exists()) {
            w.refWhy = QStringLiteral("The reference swing is no longer on disk.");
        } else {
            QString err, whyNot;
            const QJsonObject root = pinpoint::SwingStore::loadAnalysis(dir, &err);
            std::shared_ptr<ShotSeries> shot;
            if (!root.isEmpty()) shot = SessionSeriesModel::extractShot(root, dir, &whyNot);
            if (shot) { w.refOk = true; w.refIn = postureInputFromShot(*shot, c.env); }
            else w.refWhy = QStringLiteral("The reference swing cannot be read: %1.")
                                .arg(whyNot.isEmpty() ? QStringLiteral("the swing document could not be read") : whyNot);
            if (!err.isEmpty()) ppWarn() << "[Charts] posture reference" << dir << err;
        }
    }
    c.refOk = w.refOk;
    c.refIn = w.refIn;
    c.refWhy = w.refWhy;
}

// ── maps ────────────────────────────────────────────────────────────────────────────────────

QVariantMap posMap(int i, qint64 us)
{
    return { { QStringLiteral("index"), i }, { QStringLiteral("tag"), posturePositionTag(i) },
             { QStringLiteral("phase"), posturePhaseIds()[size_t(i)] }, { QStringLiteral("name"), posturePositionWhen(i) },
             { QStringLiteral("available"), us >= 0 }, { QStringLiteral("tUs"), qlonglong(us) }, { QStringLiteral("seekUs"), qlonglong(us) } };
}
QVariantList positionsList(const std::array<qint64, kPos> &us)
{
    QVariantList out;
    for (int i = 0; i < kPos; ++i) out.append(posMap(i, us[size_t(i)]));
    return out;
}

QVariant faultMap(const PostureFault &f, PostureRow r)
{
    if (!f.has) return nullv();
    return QVariantMap{ { QStringLiteral("edge"), nv(f.edge) }, { QStringLiteral("edgeText"), postureFigure(r, f.edge) },
                        { QStringLiteral("conditionId"), f.conditionId }, { QStringLiteral("label"), f.label } };
}

QString boxFmt(PostureRow r, double v) { return postureFigure(r, v); }

// A box, its outliers re-numbered from the core's pool order (the shots with a posture reading) to ordinals.
QVariantMap boxOf(const box::BoxStats &b, PostureRow r, const std::vector<int> &poolOrdinals)
{
    QVariantMap m = boxMap(b, [r](double v) { return boxFmt(r, v); });
    QVariantList out;
    for (int i : b.outliers) out.append(i >= 0 && size_t(i) < poolOrdinals.size() ? poolOrdinals[size_t(i)] : i + 1);
    m.insert(QStringLiteral("outliers"), out);
    return m;
}

// The ⚠'s short words, under the cell's figure (the full reason is said once, in the trust fold): the gate's
// own words when a gate bounded the reading, else the capture's reason in a few words.
QString warnShortOf(PostureRow r, const PostureCellView &c)
{
    if (!c.warned) return QString();
    if (!c.gateWords.isEmpty()) return c.gateWords;
    switch (r) {
    case PostureRow::Bend:         return QStringLiteral("the turn");
    case PostureRow::Hips:         return QStringLiteral("hip turn");
    case PostureRow::PelvisHeight: return QStringLiteral("joint drift");
    default:                       return QString();
    }
}

QVariantMap cellMap(const PostureRowView &rv, const PostureCellView &c, double ghost, qint64 seekUs, double picked)
{
    const PostureRow r = rv.row;
    const bool avail = fin(c.value);
    const PostureBand &b = c.band;
    // decision 16: the cell's second reading (trunk bend at P3–P5: the back line), under its own ⚠
    const bool altAvail = c.alt && fin(c.altValue);
    return {
        { QStringLiteral("alt"), c.alt }, { QStringLiteral("altAvailable"), altAvail }, { QStringLiteral("altValue"), nv(c.altValue) },
        { QStringLiteral("altLabel"), c.altLabel }, { QStringLiteral("altFigure"), c.altFigure }, { QStringLiteral("altText"), c.altText },
        { QStringLiteral("altWarn"), c.altWarned && altAvail ? QStringLiteral("⚠") : QString() },
        { QStringLiteral("altWarnShort"), altAvail ? c.altWarnShort : QString() },
        { QStringLiteral("index"), c.pos }, { QStringLiteral("tag"), c.tag }, { QStringLiteral("available"), avail },
        { QStringLiteral("value"), nv(c.value) }, { QStringLiteral("text"), c.text }, { QStringLiteral("valueText"), valueText(r, c.value) },
        // the ⚠ is DRAWN only on a figure: a refused reading is a plain "-" (its reason is said once, in TRUST)
        { QStringLiteral("warned"), c.warned }, { QStringLiteral("warn"), c.warned && avail ? QStringLiteral("⚠") : QString() },
        { QStringLiteral("warnReason"), c.warnReason }, { QStringLiteral("warnShort"), avail ? warnShortOf(r, c) : QString() },
        { QStringLiteral("gateWords"), c.gateWords },
        { QStringLiteral("counted"), c.counted },
        { QStringLiteral("banded"), b.has }, { QStringLiteral("bandLo"), nv(b.lo) }, { QStringLiteral("bandHi"), nv(b.hi) },
        { QStringLiteral("bandMu"), nv(b.mu) }, { QStringLiteral("watchLo"), nv(b.watchLo) }, { QStringLiteral("watchHi"), nv(b.watchHi) },
        { QStringLiteral("bandContext"), b.contextId }, { QStringLiteral("bandInherited"), b.inherited },
        { QStringLiteral("bandSource"), b.source }, { QStringLiteral("wash"), b.has ? QStringLiteral("band") : QStringLiteral("none") },
        { QStringLiteral("z"), nv(c.z) }, { QStringLiteral("outside"), c.outside }, { QStringLiteral("ring"), c.outside },
        { QStringLiteral("ringWord"), c.ring }, { QStringLiteral("exit"), c.exit }, { QStringLiteral("beyondFault"), c.beyondFault },
        { QStringLiteral("faultHi"), faultMap(c.fault.hi, r) }, { QStringLiteral("faultLo"), faultMap(c.fault.lo, r) },
        { QStringLiteral("firedShots"), c.firedShots }, { QStringLiteral("note"), c.note },
        { QStringLiteral("mark"), c.outside ? QStringLiteral("ring") : avail ? QStringLiteral("dot") : QStringLiteral("none") },
        { QStringLiteral("ghost"), nv(ghost) }, { QStringLiteral("ghostText"), postureFigure(r, ghost) },
        { QStringLiteral("seekUs"), qlonglong(seekUs) },
        { QStringLiteral("picked"), nv(picked) }, { QStringLiteral("pickedText"), postureFigure(r, picked) } };
}

// Each shot's value at one cell, index = ordinal − 1, null where unmeasured (PpChBoxCell's `values`: the
// shots past the whiskers and the picked one are drawn from these).
QVariantList valuesOf(const std::vector<PostureShotInput> *inputs, int maxOrdinal, PostureRow r, int p)
{
    QVariantList out;
    if (!inputs || maxOrdinal <= 0) return out;
    for (int i = 0; i < maxOrdinal; ++i) out.append(nullv());
    for (const PostureShotInput &s : *inputs)
        if (s.ordinal >= 1 && s.ordinal <= maxOrdinal) out[s.ordinal - 1] = nv(s.value[size_t(r)][size_t(p)]);
    return out;
}

// One row. `ghost` / `picked` NaN-filled when absent; `boxes` non-null on Session.
QVariantMap rowMap(const PostureRowView &rv, const std::array<double, kPos> &ghost, const std::array<double, kPos> &picked,
                   const std::array<qint64, kPos> &us, const std::array<box::BoxStats, kPos> *boxes,
                   const std::vector<int> &poolOrdinals, const std::vector<PostureShotInput> *inputs = nullptr)
{
    int maxOrdinal = 0;
    if (inputs) for (const PostureShotInput &s : *inputs) maxOrdinal = std::max(maxOrdinal, s.ordinal);
    const PostureRow r = rv.row;
    const PostureRowSpec &s = specOf(r);
    Span sc;
    QVariantList cells, bx;
    int outCount = 0;
    bool hasAlt = false;   // decision 16: a second reading under some cell (the QML gives the row its line)
    for (int p = 0; p < kPos; ++p) {
        const PostureCellView &c = rv.cells[size_t(p)];
        cells.append(cellMap(rv, c, ghost[size_t(p)], us[size_t(p)], picked[size_t(p)]));
        sc.add(c.value); sc.add(ghost[size_t(p)]); sc.add(picked[size_t(p)]);
        if (c.band.has) { sc.add(c.band.lo); sc.add(c.band.hi); }
        if (c.fault.hi.has) sc.add(c.fault.hi.edge);
        if (c.fault.lo.has) sc.add(c.fault.lo.edge);
        if (c.outside) ++outCount;
        if (c.alt) hasAlt = true;
        if (boxes) {
            const box::BoxStats &b = (*boxes)[size_t(p)];
            sc.add(b.min); sc.add(b.max);
            // P1 is address itself: its figure alone ("0"), never a "0 cm wide" line
            const double med = b.n >= 1 ? b.med : kNaN, wide = b.n >= 3 && p > 0 ? b.iqr : kNaN;
            bx.append(QVariantMap{ { QStringLiteral("box"), boxOf(b, r, poolOrdinals) }, { QStringLiteral("n"), b.n },
                                   { QStringLiteral("median"), nv(med) }, { QStringLiteral("medianText"), postureFigure(r, med) },
                                   { QStringLiteral("wide"), nv(wide) },
                                   { QStringLiteral("wideText"), fin(wide) ? bareText(r, wide) + QStringLiteral(" wide") : QString() },
                                   { QStringLiteral("picked"), nv(picked[size_t(p)]) },
                                   { QStringLiteral("values"), valuesOf(inputs, maxOrdinal, r, p) } });
        }
    }
    sc.finish(minSpanOf(r));
    return {
        { QStringLiteral("key"), rv.key }, { QStringLiteral("label"), rv.label }, { QStringLiteral("metricKey"), rv.metricKey },
        { QStringLiteral("hueKey"), rv.metricKey }, { QStringLiteral("camera"), rv.camera }, { QStringLiteral("unit"), rv.unit },
        { QStringLiteral("decimals"), s.decimals }, { QStringLiteral("poleWord"), rv.poleWord },
        { QStringLiteral("poleText"), rv.poleWord.isEmpty() ? QString() : rv.poleWord + QStringLiteral(" ▲") },
        { QStringLiteral("plusWord"), rv.plusWord }, { QStringLiteral("minusWord"), rv.minusWord },
        { QStringLiteral("present"), rv.present }, { QStringLiteral("style"), rv.style }, { QStringLiteral("styleWord"), rv.styleWord },
        { QStringLiteral("styleText"), rv.style && !rv.styleWord.isEmpty() ? QStringLiteral("style: ") + rv.styleWord : QString() },
        { QStringLiteral("outCount"), outCount }, { QStringLiteral("hasAlt"), hasAlt }, { QStringLiteral("yLo"), sc.lo }, { QStringLiteral("yHi"), sc.hi },
        { QStringLiteral("cells"), cells }, { QStringLiteral("boxes"), boxes ? QVariant(bx) : nullv() } };
}

QVariantMap trailMap(const PostureTrailStyle &t)
{
    return { { QStringLiteral("known"), t.known }, { QStringLiteral("kept"), t.kept }, { QStringLiteral("word"), t.word },
             { QStringLiteral("words"), t.words }, { QStringLiteral("changeAtTop"), nv(t.changeAtTop) },
             { QStringLiteral("changeText"), valueText(PostureRow::TrailKnee, t.changeAtTop) } };
}

QVariantMap matchFields(const PostureMatch &m)
{
    return { { QStringLiteral("ok"), m.ok }, { QStringLiteral("nearest"), m.ok ? QVariant(int(m.nearest)) : nullv() },
             { QStringLiteral("nearestName"), m.ok ? posturePatternName(m.nearest) : QString() },
             { QStringLiteral("fit"), m.fit }, { QStringLiteral("fitWords"), m.words }, { QStringLiteral("basis"), m.basis },
             { QStringLiteral("basisWords"), m.basisWords }, { QStringLiteral("dLevel"), nv(m.dLevel) },
             { QStringLiteral("dSquatPush"), nv(m.dSquatPush) }, { QStringLiteral("dLevelText"), W::fig(m.dLevel, 2) },
             { QStringLiteral("dSquatPushText"), W::fig(m.dSquatPush, 2) }, { QStringLiteral("gap"), nv(m.gap) },
             { QStringLiteral("cells"), m.cells }, { QStringLiteral("rows"), m.rows } };
}

QVariantMap patternMap(int mode, PosturePattern pat, const QString &name, const QString &source, const QString &sourceWords,
                       const QString &line, const PostureMatch &used, const PostureMatch &own, const PostureTrailStyle &trail)
{
    QVariantMap m = matchFields(used);
    m.insert(QStringLiteral("mode"), mode);
    m.insert(QStringLiteral("pattern"), int(pat));
    m.insert(QStringLiteral("name"), name);
    m.insert(QStringLiteral("source"), source);
    m.insert(QStringLiteral("sourceWords"), sourceWords);
    m.insert(QStringLiteral("line"), line);
    m.insert(QStringLiteral("own"), matchFields(own));
    m.insert(QStringLiteral("trail"), trailMap(trail));
    return m;
}

QVariantMap exitMap(const PostureExit &e, const std::array<qint64, kPos> &us)
{
    return { { QStringLiteral("row"), specOf(e.row).key }, { QStringLiteral("rowLabel"), specOf(e.row).label },
             { QStringLiteral("pos"), e.pos }, { QStringLiteral("tag"), posturePositionTag(e.pos) },
             { QStringLiteral("value"), nv(e.value) }, { QStringLiteral("valueText"), valueText(e.row, e.value) },
             { QStringLiteral("z"), nv(e.z) }, { QStringLiteral("zText"), W::fig(e.z, 1) },
             { QStringLiteral("departure"), e.departure }, { QStringLiteral("name"), e.name },
             { QStringLiteral("conditionIds"), e.conditionIds }, { QStringLiteral("named"), e.named },
             { QStringLiteral("words"), e.words },
             { QStringLiteral("seekUs"), qlonglong(usAt(us, e.pos)) } };
}

QVariantMap departureMap(const PostureDeparture &d, const std::array<qint64, kPos> &us)
{
    return { { QStringLiteral("id"), d.id }, { QStringLiteral("name"), d.name }, { QStringLiteral("conditionIds"), d.conditionIds },
             { QStringLiteral("positions"), tagsOf(d.positions) }, { QStringLiteral("positionsText"), rangeText(d.positions) },
             { QStringLiteral("firstTag"), d.firstPos >= 0 ? posturePositionTag(d.firstPos) : QString() },
             { QStringLiteral("seekUs"), qlonglong(usAt(us, d.firstPos)) } };
}

QVariantMap movedMap(const PostureMoved &m, const std::array<qint64, kPos> &us)
{
    return { { QStringLiteral("pos"), m.pos }, { QStringLiteral("tag"), posturePositionTag(m.pos) }, { QStringLiteral("when"), m.when },
             { QStringLiteral("line"), m.line }, { QStringLiteral("head"), m.head }, { QStringLiteral("bend"), m.bend },
             { QStringLiteral("attribution"), m.attribution }, { QStringLiteral("hipsIn"), m.hipsIn },
             { QStringLiteral("hipsWarned"), m.hipsWarned }, { QStringLiteral("bendWarned"), m.bendWarned },
             { QStringLiteral("seekUs"), qlonglong(usAt(us, m.pos)) } };
}

// ── THE "AT ADDRESS" BLOCK (posture_chart_design.md §7.4.2) ─────────────────────────────────────

// The block as the QML draws it: a row per reading the capture can produce (the core's), each with its scale
// (xLo…xHi over the band, both boxes and the number), this session's box with a value per shot (index = ordinal − 1)
// for its marks, and the history box (no shot marks: its swings are other sessions'). `pool`: the ordinals the core's
// session box was taken over (its outliers are indices into it). `shotView`: the dot is this shot's number.
QVariantMap addressMapOf(const std::vector<PostureAbsoluteView> &rows, const QStringList &lines, const PostureEnv &env,
                         const std::vector<PostureShotInput> &inputs, const std::vector<int> &pool, bool shotView,
                         const QString &many)
{
    int maxOrd = 0;
    for (const PostureShotInput &s : inputs) maxOrd = std::max(maxOrd, s.ordinal);
    QVariantList out;
    for (const PostureAbsoluteView &a : rows) {
        size_t spec = env.absolutes.size();
        for (size_t i = 0; i < env.absolutes.size(); ++i) if (env.absolutes[i].measureId == a.measureId) spec = i;
        const auto fmt = [u = a.unit](double v) { return postureAddressFigure(v, u); };
        QVariantMap box = boxMap(a.box, fmt);
        QVariantList ol;
        for (int i : a.box.outliers) ol.append(i >= 0 && size_t(i) < pool.size() ? pool[size_t(i)] : i + 1);
        box.insert(QStringLiteral("outliers"), ol);
        QVariantList values;
        for (int k = 0; k < maxOrd; ++k) values.append(nullv());
        for (const PostureShotInput &s : inputs)
            if (s.ordinal >= 1 && s.ordinal <= maxOrd && spec < s.absolutes.size() && std::find(pool.begin(), pool.end(), s.ordinal) != pool.end())
                values[s.ordinal - 1] = nv(s.absolutes[spec]);
        QVariant hist = nullv();
        if (a.history.n > 0) {
            QVariantMap hb = boxMap(a.history, fmt);
            hb.insert(QStringLiteral("outliers"), QVariantList{});   // other sessions' swings: no marks here
            hist = hb;
        }
        // the row's scale: the band, both boxes and the number, never narrower than a few units
        Span sp;   // an absolute: no address zero in it
        sp.lo = std::numeric_limits<double>::infinity();
        sp.hi = -std::numeric_limits<double>::infinity();
        if (a.band.has) { sp.add(a.band.lo); sp.add(a.band.hi); }
        for (const pinpoint::box::BoxStats *b : { &a.box, &a.history }) if (b->n > 0) { sp.add(b->min); sp.add(b->max); }
        sp.add(a.value);
        if (!fin(sp.lo) || !fin(sp.hi)) { sp.lo = 0; sp.hi = 1; }
        sp.finish(a.unit == QStringLiteral("cm") ? 4.0 : 10.0);
        const bool banded = a.band.has && fin(a.band.lo) && fin(a.band.hi);
        out.append(QVariantMap{
            { QStringLiteral("measureId"), a.measureId }, { QStringLiteral("label"), a.name }, { QStringLiteral("measureLabel"), a.label },
            { QStringLiteral("unit"), a.unit }, { QStringLiteral("camera"), postureCameraWords(a.camera) },
            { QStringLiteral("cameraKey"), a.camera == PostureCamera::FaceOn ? QStringLiteral("faceOn") : QStringLiteral("dtl") },
            { QStringLiteral("metricKey"), a.metricKey }, { QStringLiteral("hueKey"), a.metricKey },
            { QStringLiteral("value"), nv(a.value) }, { QStringLiteral("text"), a.text },
            { QStringLiteral("valueWord"), shotView ? QStringLiteral("this shot") : QStringLiteral("median") },
            { QStringLiteral("banded"), banded }, { QStringLiteral("bandLo"), banded ? nv(a.band.lo) : nullv() },
            { QStringLiteral("bandHi"), banded ? nv(a.band.hi) : nullv() }, { QStringLiteral("bandMu"), banded ? nv(a.band.mu) : nullv() },
            { QStringLiteral("bandText"), a.bandText }, { QStringLiteral("bandSource"), banded ? a.band.source : QString() },
            { QStringLiteral("bandContext"), banded ? a.band.contextId : QString() }, { QStringLiteral("bandInherited"), banded && a.band.inherited },
            { QStringLiteral("box"), box }, { QStringLiteral("boxN"), a.box.n }, { QStringLiteral("boxText"), a.boxText },
            { QStringLiteral("values"), values },
            { QStringLiteral("dot"), shotView ? nv(a.value) : nullv() },
            { QStringLiteral("history"), hist }, { QStringLiteral("historyN"), a.history.n },
            { QStringLiteral("historySessions"), a.historySessions }, { QStringLiteral("hasHistory"), a.history.n > 0 },
            { QStringLiteral("historyText"), a.historyText }, { QStringLiteral("historyWhy"), a.historyWhy },
            { QStringLiteral("warned"), a.warned }, { QStringLiteral("warn"), a.warned ? a.warn : QString() },
            { QStringLiteral("warnShort"), a.warned ? a.warnShort : QString() },
            { QStringLiteral("xLo"), sp.lo }, { QStringLiteral("xHi"), sp.hi }, { QStringLiteral("trust"), a.trust } });
    }
    const QString one = many.endsWith(QLatin1Char('s')) ? many.chopped(1) : many;
    return { { QStringLiteral("available"), !out.isEmpty() }, { QStringLiteral("title"), QStringLiteral("At address") },
             { QStringLiteral("sub"), shotView ? QStringLiteral("this %1's number against the good-player band, this session and your earlier sessions").arg(one)
                                               : QStringLiteral("this session against the good-player band, and your earlier sessions") },
             { QStringLiteral("rows"), out },
             { QStringLiteral("legend"), shotView
                   ? QStringLiteral("Shaded: the good-player band. Upper box: this session, the dot this %1. Lower box: your earlier sessions from this set-up.").arg(one)
                   : QStringLiteral("Shaded: the good-player band. Upper box: this session. Lower box: your earlier sessions from this set-up.") },
             { QStringLiteral("lines"), lines } };
}

// ── the golfer's earlier sessions from the same set-up (§7.4.2) ──

// A session's cameras, from its first swing document's streams: the view each stands on (setup.perspectiveName,
// else the alias) and its serial (source.serial). `known` false when no document was read.
struct SessionCameras { bool known = false; std::array<bool, 2> present{}; std::array<QString, 2> serial; };
SessionCameras camerasOf(const QString &sessionDir)
{
    SessionCameras c;
    const QDir sd(sessionDir);
    const QStringList swings = sd.entryList({ QStringLiteral("swing_*") }, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &sw : swings) {
        const QString dir = sd.filePath(sw);
        if (!pinpoint::SwingStore::hasDocument(dir)) continue;
        const QJsonObject root = pinpoint::SwingStore::loadAnalysis(dir);
        if (root.isEmpty()) continue;
        c.known = true;
        for (const QJsonValue &sv : root.value(QStringLiteral("streams")).toArray()) {
            const QJsonObject st = sv.toObject();
            if (st.value(QStringLiteral("kind")).toString() != QLatin1String("video")) continue;
            const QString persp = st.value(QStringLiteral("setup")).toObject().value(QStringLiteral("perspectiveName")).toString();
            const QString alias = st.value(QStringLiteral("alias")).toString();
            int cam = -1;
            if (persp == QLatin1String("DownTheLine") || (persp.isEmpty() && alias.compare(QLatin1String("DTL"), Qt::CaseInsensitive) == 0))
                cam = int(PostureCamera::Dtl);
            else if (persp == QLatin1String("FaceOn") || (persp.isEmpty() && alias.startsWith(QLatin1String("Face"), Qt::CaseInsensitive)))
                cam = int(PostureCamera::FaceOn);
            if (cam < 0) continue;
            c.present[size_t(cam)] = true;
            c.serial[size_t(cam)] = st.value(QStringLiteral("source")).toObject().value(QStringLiteral("serial")).toVariant().toString();
        }
        break;
    }
    return c;
}

// A measure's graded reading on one ledger shot: a row driven or read by it that the engine could assess, else a
// composite's part read by it. NaN when the shot carries none.
// A shot with a DATA WARNING (session_diagnostics_model detectShot) has every row turned NotAssessable after it was read,
// its value kept: such a row counts when the corridor was applied to it (corridorHi > corridorLo) — it was measured
// and then withheld, unlike a row the engine never assessed, whose 0 nobody measured.
double ledgerReading(const pa::ShotRecord &shot, const QString &measureId)
{
    for (const pa::ConditionRow &r : shot.rows) {
        if (r.state == pa::ShotState::NotAssessable && !(shot.dataWarning && r.corridorHi > r.corridorLo)) continue;
        for (const pa::MeasureRow &m : pa::rowReadings(r))
            if (m.measureId == measureId && fin(m.value)) return m.value;
    }
    for (const pa::ConditionRow &r : shot.rows) {
        for (const pa::CompositePartRow &p : r.composite.parts)
            if (p.measureId == measureId && fin(p.value)) return p.value;
        for (const pa::MeasureRow &m : r.readings)
            for (const pa::CompositePartRow &p : m.composite.parts)
                if (p.measureId == measureId && fin(p.value)) return p.value;
    }
    return kNaN;
}

// The stamp of what the history reads: the session folder and every sibling ledger's name, size and time.
QString addressHistoryStamp(const QString &sessionDir)
{
    const QFileInfo fi(cleanDir(sessionDir));
    if (sessionDir.trimmed().isEmpty() || !fi.isDir()) return cleanDir(sessionDir) + QStringLiteral("|none");
    QStringList parts{ fi.absoluteFilePath() };
    const QDir athlete = fi.dir();
    for (const QString &name : athlete.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        const QFileInfo d(athlete.filePath(name) + QStringLiteral("/diagnostics.json"));
        if (d.exists()) parts << QStringLiteral("%1:%2:%3").arg(name).arg(d.size()).arg(d.lastModified().toMSecsSinceEpoch());
    }
    return parts.join(QLatin1Char('|'));
}

// THE HISTORY: every EARLIER session in the athlete folder (the session folder's siblings whose name sorts before it —
// a session folder starts with its date) whose diagnostics ledger reads the measure, kept per view when its camera on
// that view is this session's (by serial; a serial not recorded on either side keeps it, counted as unrecorded).
// One reading per swing: the ledger's graded value — the measure engine's own number, as Session diagnostics graded it.
PostureAddressHistory readAddressHistory(const QString &sessionDir, const PostureEnv &env)
{
    PostureAddressHistory h;
    const QFileInfo fi(cleanDir(sessionDir));
    if (sessionDir.trimmed().isEmpty() || !fi.isDir()) return h;
    const QDir athlete = fi.dir();
    if (!athlete.exists()) return h;
    h.looked = true;
    const SessionCameras mine = camerasOf(fi.absoluteFilePath());
    for (int c = 0; c < 2; ++c) {
        h.views[size_t(c)].known = mine.known;
        h.views[size_t(c)].present = mine.present[size_t(c)];
        h.views[size_t(c)].serial = mine.serial[size_t(c)];
    }
    const QString me = fi.fileName();
    for (const QString &name : athlete.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        if (name >= me) continue;   // earlier sessions only
        const QString dir = athlete.filePath(name);
        QFile f(dir + QStringLiteral("/diagnostics.json"));
        if (!f.open(QIODevice::ReadOnly)) continue;
        QJsonParseError pe{};
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
        if (pe.error != QJsonParseError::NoError || !doc.isObject()) continue;
        const std::vector<pa::ShotRecord> shots = pa::fromJson(doc.object().value(QStringLiteral("ledger")).toObject());
        QHash<QString, std::vector<double>> got;
        std::array<int, 2> swingsWith{};
        // A swing with a data warning COUNTS, as it does in this session's box (the session's shots are boxed
        // whatever their warning): the two medians side by side are one rule. How many are in is said once
        // (addressLines). One swing read on both views is one swing.
        std::vector<std::array<bool, 2>> warnedWith;
        for (const pa::ShotRecord &shot : shots) {
            std::array<bool, 2> any{};
            for (const PostureAbsoluteSpec &sp : env.absolutes) {
                const double v = ledgerReading(shot, sp.measureId);
                if (!fin(v)) continue;
                got[sp.measureId].push_back(v);
                any[size_t(int(sp.camera))] = true;
            }
            for (int c = 0; c < 2; ++c) if (any[size_t(c)]) ++swingsWith[size_t(c)];
            if (shot.dataWarning && (any[0] || any[1])) warnedWith.push_back(any);
        }
        if (got.isEmpty()) continue;
        const SessionCameras theirs = camerasOf(dir);
        std::array<bool, 2> keptView{};
        for (int c = 0; c < 2; ++c) {
            if (swingsWith[size_t(c)] == 0) continue;
            PostureAddressHistory::View &v = h.views[size_t(c)];
            const QString &a = mine.serial[size_t(c)], &b = theirs.serial[size_t(c)];
            if (!a.isEmpty() && !b.isEmpty() && a != b) { ++v.otherCamera; continue; }
            keptView[size_t(c)] = true;
            if (a.isEmpty() || b.isEmpty()) ++v.unrecorded;
            ++v.kept;
            v.swings += swingsWith[size_t(c)];
            for (const PostureAbsoluteSpec &sp : env.absolutes) {
                if (int(sp.camera) != c) continue;
                const auto it = got.constFind(sp.measureId);
                if (it == got.constEnd() || it->empty()) continue;
                std::vector<double> &dst = h.values[sp.measureId];
                dst.insert(dst.end(), it->begin(), it->end());
                ++h.sessions[sp.measureId];
            }
        }
        for (const std::array<bool, 2> &w : warnedWith)
            if ((w[0] && keptView[0]) || (w[1] && keptView[1])) ++h.dataWarned;
    }
    return h;
}

// Through the worker's cache (re-read when a sibling ledger, or the session, changes).
const PostureAddressHistory &addressHistoryOf(ChartsPosturePresenter::Worker &w, const QString &sessionDir, const PostureEnv &env)
{
    const QString stamp = addressHistoryStamp(sessionDir);
    if (stamp != w.histStamp) {
        w.hist = readAddressHistory(sessionDir, env);
        w.histStamp = stamp;
    }
    return w.hist;
}

QVariantMap trustMap(const PostureTrust &t)
{
    return { { QStringLiteral("lines"), t.lines }, { QStringLiteral("cameras"), t.cameras }, { QStringLiteral("absent"), t.absent },
             { QStringLiteral("warnings"), t.warnings },
             { QStringLiteral("bandSources"), t.bandSources }, { QStringLiteral("trunkDatum"), t.trunkDatum },
             { QStringLiteral("rigRule"), t.rigRule } };
}

std::array<double, kPos> nanRow()
{
    std::array<double, kPos> a;
    a.fill(kNaN);
    return a;
}

// ── findings and working well (the shot's diagnostics) ─────────────────────────────────────

// The P a condition is read at on this shot: the core's (postureConditionPos), one rule with the head line.
int conditionPos(const ShotSeries *s, const PostureEnv &env, const QString &id) { return postureConditionPos(s, env, id); }

std::vector<int> faultPositions(const PostureEnv &env, const QString &id)
{
    std::vector<int> out;
    for (const PostureFaultLink &l : env.faultLinks) if (l.conditionId == id) out.push_back(l.pos);
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

QString sessionTier(const SessionSnapshot *snap, const QString &id, bool fired)
{
    const QString t = snap ? snap->tiers.value(id) : QString();
    if (!t.isEmpty()) return t;
    return fired ? QStringLiteral("watching") : QStringLiteral("clean");
}

QString unitOfCondition(const PostureEnv &env, const ShotSeries *s, const QString &id)
{
    if (!env.pack) return QString();
    QString mid;
    if (s) {
        const auto it = s->diagnostics.drivenBy.constFind(id);
        if (it != s->diagnostics.drivenBy.constEnd()) mid = it->measureId;
    }
    if (mid.isEmpty()) {
        const auto rs = pa::conditionReadings(*env.pack, id);
        if (!rs.empty()) mid = rs.front().measureId;
    }
    const pa::Measure *m = mid.isEmpty() ? nullptr : env.pack->measure(mid);
    return m ? m->unit : QString();
}

// A finding's figure is shown only in a posture row's own unit (cm, °, % stance): a composite's reading
// is its z, which is never shown (posture_metrics_design.md §8).
bool rowUnit(const PostureEnv &env, const ShotSeries *s, const QString &id, const QString &unit)
{
    if (!(unit == QLatin1String("cm") || unit == QStringLiteral("°") || unit.startsWith(QLatin1Char('%')))) return false;
    if (!env.pack) return true;
    QString mid;
    if (s) { const auto it = s->diagnostics.drivenBy.constFind(id); if (it != s->diagnostics.drivenBy.constEnd()) mid = it->measureId; }
    if (mid.isEmpty()) { const auto rs = pa::conditionReadings(*env.pack, id); if (!rs.empty()) mid = rs.front().measureId; }
    const pa::Measure *m = mid.isEmpty() ? nullptr : env.pack->measure(mid);
    return !m || m->kind != pa::MeasureKind::Composite;
}

QVariantMap findingMap(const PostureEnv &env, const SessionSnapshot *snap, const ShotSeries *s, const QString &id,
                       const QString &kind, const PostureDepartureSpec *dep, const std::vector<int> &positions, int seekPos,
                       const std::array<qint64, kPos> &us)
{
    const pa::Condition *c = env.pack ? env.pack->condition(id) : nullptr;
    const bool working = kind == QLatin1String("working");
    const QString label = c && !c->label.isEmpty() ? c->label : id;
    const QString golfer = c ? golferSafe(c->golfer) : QString();
    const QString well = c ? golferSafe(c->golferWell) : QString();
    const QString why = c ? golferSafe(c->golferWhy) : QString();
    const QString tier = sessionTier(snap, id, !working);
    const QString severity = working ? QStringLiteral("good") : tier == QLatin1String("pattern") ? QStringLiteral("pattern") : QStringLiteral("watching");
    ShotDiagnostics::Value val;
    if (s) val = s->diagnostics.values.value(id);
    const QString unit = unitOfCondition(env, s, id);
    const bool figure = !working && rowUnit(env, s, id, unit);
    const QString tag = seekPos >= 0 ? posturePositionTag(seekPos) : QString();
    const qint64 seek = usAt(us, seekPos);
    return {
        { QStringLiteral("id"), id },
        { QStringLiteral("name"), working ? W::cap1(well) : label },
        { QStringLiteral("kind"), kind },
        { QStringLiteral("departure"), dep ? dep->id : QString() },
        { QStringLiteral("departureName"), dep ? dep->name : QString() },
        { QStringLiteral("severity"), severity },
        { QStringLiteral("severityWord"), working ? QStringLiteral("working") : severity },
        { QStringLiteral("tier"), tier },
        { QStringLiteral("positions"), tagsOf(positions) },
        { QStringLiteral("positionsText"), positions.empty() ? QString()
                                           : positions.size() == 1 ? QStringLiteral("at ") + posturePositionTag(positions.front())
                                                                   : rangeText(positions) },
        { QStringLiteral("tag"), tag },
        { QStringLiteral("seekUs"), qlonglong(seek) },
        { QStringLiteral("seekText"), tag.isEmpty() || seek < 0 ? QString() : QStringLiteral("Go to ") + tag },
        { QStringLiteral("value"), figure ? nv(val.value) : nullv() },
        { QStringLiteral("valueText"), figure ? W::figUnit(val.value, unit) : QString() },
        { QStringLiteral("z"), nv(val.z) },
        { QStringLiteral("explanation"), working ? QString() : (why.isEmpty() ? W::cap1(golfer) : why) },
        // Working well says its golferWell line only: golferWhy describes the FAULT
        { QStringLiteral("protect"), QString() } };
}

// Findings: the fired conditions that name a departure, plus trail_knee_straighten and loss_of_posture;
// in the session's WORK-ON ORDER (roots before symptoms: SessionSnapshot::workOnOrder, the work-on list's
// own reduction — session_posture.h decision 9), then the session's other patterns, then by how far out
// the ledger graded them. Working well: per axis, the first posture condition graded clean, inside its
// band, with golfer words — none where the axis fired.
void ledgersOf(const PostureEnv &env, const SessionSnapshot *snap, const ShotSeries *s, const PostureShotInput &in,
               const PostureShotView &v, const std::array<qint64, kPos> &us, QVariantList *findings, QVariantList *working)
{
    if (!in.graded) return;
    const std::vector<PostureDepartureSpec> specs = postureDepartureSpecs(env);
    const QStringList *order = snap ? &snap->workOnOrder : nullptr;
    struct F { QVariantMap m; int rank; bool pattern; double z; };
    std::vector<F> fs;
    for (const QString &id : in.fired) {
        // the departure this shot left by that names it, else the first in the table that does
        const PostureDepartureSpec *dep = nullptr;
        const PostureDeparture *exited = nullptr;
        for (const PostureDeparture &d : v.departures) {
            if (!d.conditionIds.contains(id)) continue;
            for (const PostureDepartureSpec &sp : specs) if (sp.id == d.id) dep = &sp;
            exited = &d;
            break;
        }
        if (!dep) for (const PostureDepartureSpec &sp : specs) if (sp.conditionIds.contains(id)) { dep = &sp; break; }
        const bool trail = id == kTrailKneeCondition, lop = id == kLossOfPosture;
        if (!dep && !trail && !lop) continue;
        if (env.pack && !env.pack->condition(id)) continue;
        std::vector<int> positions = exited ? exited->positions : faultPositions(env, id);
        const int at = conditionPos(s, env, id);
        if (positions.empty() && at >= 0) positions.push_back(at);
        const int seek = at >= 0 ? at : (positions.empty() ? -1 : positions.front());
        const double z = s ? s->diagnostics.values.value(id).z : kNaN;
        const QString kind = dep ? QStringLiteral("departure") : trail ? QStringLiteral("trailKnee") : QStringLiteral("finding");
        fs.push_back({ findingMap(env, snap, s, id, kind, dep, positions, seek, us), postureWorkOnRank(order, id),
                       sessionTier(snap, id, true) == QLatin1String("pattern"), fin(z) ? std::fabs(z) : 0.0 });
    }
    std::stable_sort(fs.begin(), fs.end(), [](const F &a, const F &b) {
        if (a.rank != b.rank) return a.rank < b.rank;
        if (a.pattern != b.pattern) return a.pattern;
        return a.z > b.z;
    });
    for (const F &f : fs) findings->append(f.m);

    const QStringList mine = postureConditionList(env);
    const auto inside = [&](const QString &id) {
        bool known = false;
        if (s && s->diagnostics.values.contains(id)) {
            const double z = s->diagnostics.values.value(id).z;
            if (fin(z) && std::fabs(z) > 1.0) return false;
            known = fin(z);
        }
        for (const PostureFaultLink &l : env.faultLinks) {
            if (l.conditionId != id) continue;
            const PostureCellView &c = v.rows[size_t(int(l.row))].cells[size_t(l.pos)];
            if (!fin(c.value) || c.warned || !c.band.has) continue;
            if (c.outside) return false;
            known = true;
        }
        return known;
    };
    QStringList seenAxis, said;
    for (const QString &id : mine) {
        const pa::Condition *c = env.pack ? env.pack->condition(id) : nullptr;
        if (!c) continue;
        const QString axis = c->axis.isEmpty() ? id : c->axis;
        if (seenAxis.contains(axis)) continue;
        seenAxis << axis;
        bool anyFired = false;
        QString speak;
        for (const QString &t : mine) {
            const pa::Condition *tc = env.pack->condition(t);
            if (!tc || (tc->axis.isEmpty() ? t : tc->axis) != axis) continue;
            if (in.fired.contains(t)) anyFired = true;
            if (speak.isEmpty() && in.clean.contains(t) && !golferSafe(tc->golferWell).isEmpty() && inside(t)) speak = t;
        }
        if (anyFired || speak.isEmpty()) continue;
        const QString words = golferSafe(env.pack->condition(speak)->golferWell);
        if (said.contains(words)) continue;
        said << words;
        const int at = conditionPos(s, env, speak);
        std::vector<int> positions;
        if (at >= 0) positions.push_back(at);
        working->append(findingMap(env, snap, s, speak, QStringLiteral("working"), nullptr, positions, at, us));
    }
}

// ── the standfirsts ─────────────────────────────────────────────────────────────────────────

QVariantList standfirstsOf(const PostureShotView &v, const std::array<qint64, kPos> &us)
{
    QVariantList out;
    for (int p = 0; p < kPos; ++p) {
        W::Sentence st;
        const QString tag = posturePositionTag(p), when = posturePositionWhen(p);
        const QString at = when == tag ? QStringLiteral("At %1").arg(tag) : QStringLiteral("At %1, %2").arg(tag, when);
        bool anyValue = false;
        for (const PostureRowView &r : v.rows) anyValue = anyValue || (r.present && fin(r.cells[size_t(p)].value));
        if (p == 0) {
            st.append(at + QStringLiteral(": every reading is measured from here."));
        } else if (us[size_t(p)] < 0 && !anyValue) {
            st.append(QStringLiteral("At %1, this swing has no %1 to read.").arg(tag));
        } else {
            st.append(at + QStringLiteral(": "));
            bool first = true;
            for (const PostureRowView &r : v.rows) {
                if (!r.present) continue;
                const PostureCellView &c = r.cells[size_t(p)];
                if (!first) st.append(QStringLiteral(", "));
                first = false;
                const QString name = r.label.toLower();
                if (!fin(c.value) && !c.gateWords.isEmpty()) { st.append(name + QLatin1Char(' ') + c.gateWords); continue; }
                st.append(name + QLatin1Char(' ')).figure(valueText(r.row, c.value));
                if (c.warned) st.append(QStringLiteral(" ⚠"));
                if (c.alt && fin(c.altValue)) st.append(QStringLiteral(" (") + c.altText + QStringLiteral(" ⚠)"));
                if (c.outside) st.append(QStringLiteral(" (outside the band: ") + c.ring + QStringLiteral(")"));
            }
            st.append(QStringLiteral(", from address."));
        }
        out.append(st.toVariant());
    }
    return out;
}

// ── the session map (also the report's page) ────────────────────────────────────────────────

QString clubsWords(const std::vector<PostureShotInput> &inputs)
{
    QStringList clubs;
    for (const PostureShotInput &s : inputs) { const QString k = s.club.toLower(); if (!k.isEmpty() && !clubs.contains(k)) clubs << k; }
    return clubs.size() == 1 ? QStringLiteral("  ·  ") + clubs.first() : clubs.size() > 1 ? QStringLiteral("  ·  mixed clubs") : QString();
}

QVariantMap repeatMap(const PostureRepeat &r)
{
    return { { QStringLiteral("has"), r.has }, { QStringLiteral("row"), r.has ? specOf(r.row).key : QString() },
             { QStringLiteral("rowLabel"), r.has ? specOf(r.row).label : QString() },
             { QStringLiteral("tag"), r.has ? posturePositionTag(r.pos) : QString() },
             { QStringLiteral("iqr"), nv(r.iqr) }, { QStringLiteral("iqrText"), r.has ? bareText(r.row, r.iqr) : QStringLiteral("-") },
             { QStringLiteral("words"), r.words } };
}

// `picked` may be a shot outside the snapshot (its own detail): then nothing in the strip is picked. `noun`:
// what the words count, plural ("shots" on Charts, "swings" in the report) — the core's sentences take the same.
QVariantMap sessionMapOf(const std::vector<PostureShotInput> &inputs, const PostureSessionView &sv, int patternMode,
                         int pickedIndex, const PostureShotInput *picked, const std::array<qint64, kPos> &us,
                         const QString &noun = QStringLiteral("shots"))
{
    QVariantMap out;
    const QString many = noun, one = noun.endsWith(QLatin1Char('s')) ? noun.chopped(1) : noun;
    const int n = int(inputs.size());
    out.insert(QStringLiteral("n"), n);
    out.insert(QStringLiteral("shots"), sv.shots);
    out.insert(QStringLiteral("small"), sv.shots < 10);
    out.insert(QStringLiteral("available"), sv.available);
    out.insert(QStringLiteral("fromSession"), sv.fromSession);
    out.insert(QStringLiteral("kicker"), QStringLiteral("%1 %2").arg(n).arg(n == 1 ? one : many)
                                             + clubsWords(inputs) + QStringLiteral("  ·  Posture"));
    out.insert(QStringLiteral("departuresEmpty"), QStringLiteral("No %1 left the pattern's band.").arg(one));
    if (!sv.available) {
        const QString h = QStringLiteral("No %1 in this session carries a posture reading").arg(one);
        out.insert(QStringLiteral("head"), W::Sentence().append(h).toVariant());
        out.insert(QStringLiteral("headText"), h);
        out.insert(QStringLiteral("patternLine"), QString());
        out.insert(QStringLiteral("departures"), QVariantList{});
        out.insert(QStringLiteral("strip"), QVariantMap{ { QStringLiteral("marks"), QVariantList{} }, { QStringLiteral("onPattern"), 0 },
                                                          { QStringLiteral("line"), QString() }, { QStringLiteral("legend"), QString() } });
        return out;
    }
    std::vector<int> pool;   // the core's pool (shots with a reading), as ordinals
    for (const PostureShotInput &s : inputs) if (hasPosture(s)) pool.push_back(s.ordinal);
    const auto dirOf = [&](int ordinal) {
        for (const PostureShotInput &s : inputs) if (s.ordinal == ordinal) return s.swingDir;
        return QString();
    };
    const auto idOf = [&](int ordinal) {
        for (const PostureShotInput &s : inputs) if (s.ordinal == ordinal) return s.shotId;
        return -1;
    };
    const int pickedOrdinal = pickedIndex >= 0 && picked ? picked->ordinal : -1;

    out.insert(QStringLiteral("head"), W::Sentence().append(sv.headline).toVariant());
    out.insert(QStringLiteral("headText"), sv.headline);
    out.insert(QStringLiteral("patternLine"), sv.patternLine);
    out.insert(QStringLiteral("pattern"), patternMap(patternMode, sv.pattern, sv.patternName, sv.patternSource, sv.patternSourceWords,
                                                     sv.patternLine, sv.match, sv.match, sv.trail));

    // THE PROFILE: the medians against the session's pattern, a box per position, this shot ringed
    QVariantList rows;
    for (int r = 0; r < kPostureRows; ++r) {
        const PostureSessionRow &sr = sv.rows[size_t(r)];
        std::array<double, kPos> pk = nanRow();
        if (picked) pk = picked->value[size_t(r)];
        rows.append(rowMap(sr.view, nanRow(), pk, us, &sr.boxes, pool, &inputs));
    }
    out.insert(QStringLiteral("profile"), QVariantMap{
        { QStringLiteral("positions"), positionsList(us) }, { QStringLiteral("rows"), rows },
        { QStringLiteral("sub"), QStringLiteral("change from address  ·  a box per position, n %1%2  ·  under each: the median, and how wide the middle half is")
                                     .arg(sv.shots).arg(pickedOrdinal >= 0 ? QStringLiteral(", this %1 ringed").arg(one) : QString()) },
        { QStringLiteral("caption"), QStringLiteral("Shaded: the %1 band, where good players are at each position. The hairline: where a fault "
                                                    "fires. A ring and a word: the session's median outside the band, and on which side. "
                                                    "⚠: drawn, not matched, never ringed.").arg(sv.patternName) } });

    // THE PATTERN BY SHOT
    QVariantList marks;
    for (const PosturePatternMark &m : sv.strip) {
        const QString word = !m.ok ? QStringLiteral("-") : m.between ? QStringLiteral("between") : posturePatternName(m.nearest);
        marks.append(QVariantMap{ { QStringLiteral("ordinal"), m.ordinal }, { QStringLiteral("shotId"), idOf(m.ordinal) },
                                  { QStringLiteral("swingDir"), dirOf(m.ordinal) }, { QStringLiteral("ok"), m.ok },
                                  { QStringLiteral("nearest"), m.ok ? QVariant(int(m.nearest)) : nullv() },
                                  { QStringLiteral("nearestName"), m.ok ? posturePatternName(m.nearest) : QString() },
                                  { QStringLiteral("between"), m.between }, { QStringLiteral("differs"), m.differs },
                                  { QStringLiteral("picked"), m.ordinal == pickedOrdinal },
                                  { QStringLiteral("dLevel"), nv(m.dLevel) }, { QStringLiteral("dSquatPush"), nv(m.dSquatPush) },
                                  { QStringLiteral("word"), word },
                                  { QStringLiteral("label"), QStringLiteral("%1 %2: %3").arg(one).arg(m.ordinal).arg(word) } });
    }
    out.insert(QStringLiteral("strip"), QVariantMap{
        { QStringLiteral("marks"), marks }, { QStringLiteral("onPattern"), sv.onPattern },
        { QStringLiteral("line"), QStringLiteral("%1 on %2 of %3 %4").arg(sv.patternName).arg(sv.onPattern).arg(sv.shots).arg(many) },
        { QStringLiteral("legend"), QStringLiteral("each mark is a %1's nearest pattern; ringed: not the session's (%2), or between the two").arg(one, sv.patternName) } });

    // THE DEPARTURES, across shots
    QVariantList deps;
    for (const PostureDepartureCount &d : sv.departureCounts) {
        QVariantList dm;
        for (int o : d.ordinals) dm.append(QVariantMap{ { QStringLiteral("ordinal"), o }, { QStringLiteral("swingDir"), dirOf(o) } });
        QVariantList ords;
        for (int o : d.ordinals) ords.append(o);
        const QString count = QStringLiteral("%1 of %2 %3").arg(d.shots).arg(sv.shots).arg(many);
        const QString first = d.firstPos >= 0 ? posturePositionTag(d.firstPos) : QString();
        deps.append(QVariantMap{ { QStringLiteral("id"), d.id }, { QStringLiteral("name"), d.name },
                                 { QStringLiteral("conditionIds"), d.conditionIds }, { QStringLiteral("shots"), d.shots },
                                 { QStringLiteral("countText"), count }, { QStringLiteral("ordinals"), ords },
                                 { QStringLiteral("marks"), dm }, { QStringLiteral("firstTag"), first },
                                 { QStringLiteral("line"), d.name + (first.isEmpty() ? QString() : QStringLiteral(", from ") + first)
                                                               + QStringLiteral(": ") + count },
                                 { QStringLiteral("detail"), (first.isEmpty() ? QString() : QStringLiteral("from ") + first + QStringLiteral("  ·  "))
                                                                 + count } });
    }
    out.insert(QStringLiteral("departures"), deps);
    out.insert(QStringLiteral("repeat"), QVariantMap{ { QStringLiteral("tightest"), repeatMap(sv.tightest) },
                                                       { QStringLiteral("loosest"), repeatMap(sv.loosest) },
                                                       { QStringLiteral("words"), sv.repeatWords } });
    out.insert(QStringLiteral("whatMoved"), QVariantMap{ { QStringLiteral("top"), movedMap(sv.top, us) },
                                                          { QStringLiteral("impact"), movedMap(sv.impact, us) } });
    const QVariantMap address = addressMapOf(sv.absolutes, sv.addressLines, postureSharedEnv(), inputs, pool, false, many);
    out.insert(QStringLiteral("address"), address);
    out.insert(QStringLiteral("absolutes"), address.value(QStringLiteral("rows")));   // the same rows (ABSOLUTE = ADDRESS)
    out.insert(QStringLiteral("trust"), trustMap(sv.trust));
    return out;
}

// ── This shot ───────────────────────────────────────────────────────────────────────────────

// ── the picture strip (posture_chart_design.md §5.4, stage 5) ─────────────────────────────────

// The swing's document → the picture's inputs, cached by the document's stamp (a re-analysed swing is read
// again). A pinned entry (the tests' seam) is kept whatever the disk says.
const PictureDoc &pictureDocOf(ChartsPosturePresenter::Worker &w, const QString &swingDir)
{
    const QString dir = cleanDir(swingDir);
    auto it = w.pictures.find(dir);
    if (it != w.pictures.end() && it->pinned) return it->doc;
    const pinpoint::SwingStore::DocInfo info = pinpoint::SwingStore::info(dir);
    if (it != w.pictures.end() && it->size == info.size && it->mtimeMs == info.mtimeMs) {
        w.pictureOrder.removeAll(dir);
        w.pictureOrder << dir;
        return it->doc;
    }
    ChartsPosturePresenter::Worker::PictureEntry e;
    e.size = info.size;
    e.mtimeMs = info.mtimeMs;
    if (!info.exists()) {
        e.doc.swingDir = dir;
        e.doc.why = QStringLiteral("the swing's document is not on disk");
    } else {
        QString err;
        const QJsonObject root = pinpoint::SwingStore::load(dir, &err);
        e.doc = postureDocFrom(root, dir, QStringLiteral("%1-%2").arg(info.size).arg(info.mtimeMs));
        if (root.isEmpty()) e.doc.why = QStringLiteral("the swing's document could not be read%1").arg(err.isEmpty() ? QString() : QStringLiteral(": ") + err);
    }
    w.pictureOrder.removeAll(dir);
    w.pictureOrder << dir;
    while (w.pictureOrder.size() > 6) {
        const QString old = w.pictureOrder.takeFirst();
        auto o = w.pictures.find(old);
        if (o != w.pictures.end() && !o->pinned) w.pictures.erase(o);
    }
    return w.pictures.insert(dir, std::move(e)).value().doc;
}

// The bands at impact the picture draws: the pattern in use's own bands at P7 (the norms, not the golfer's cell — a
// shot whose hip reading is "-" still has a band to draw on its own butt line), as the profile washes them.
PictureBands pictureBandsOf(const PostureEnv &env, PosturePattern pat, const QString &patternName)
{
    PictureBands b;
    b.patternName = patternName;
    const PostureBand &hip = env.bands(pat)[size_t(PostureRow::Hips)][6];
    const PostureBand &head = env.bands(pat)[size_t(PostureRow::Head)][6];
    b.hipHas = hip.has && std::isfinite(hip.lo) && std::isfinite(hip.hi);
    b.hipLo = hip.lo; b.hipHi = hip.hi;
    b.headHas = head.has && std::isfinite(head.lo) && std::isfinite(head.hi);
    b.headLo = head.lo; b.headHi = head.hi;
    b.hipHueKey = postureRowSpec(PostureRow::Hips).metricKey;
    b.headHueKey = postureRowSpec(PostureRow::Head).metricKey;
    return b;
}

// Where Charts caches the picture's stills.
QString pictureStillDir()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    return QDir(base.isEmpty() ? QDir::tempPath() : base).filePath(QStringLiteral("posture-picture"));
}

// The stills a picture still needs made.
QVariantList pendingStillsOf(const QVariantMap &picture)
{
    QVariantList out;
    for (const QVariant &g : picture.value(QStringLiteral("groups")).toList())
        for (const QVariant &fv : g.toMap().value(QStringLiteral("frames")).toList()) {
            const QVariantMap f = fv.toMap();
            const QVariantMap still = f.value(QStringLiteral("still")).toMap();
            if (f.value(QStringLiteral("src")).toString().isEmpty() && f.value(QStringLiteral("imageWhy")).toString().isEmpty()
                && !still.value(QStringLiteral("outPath")).toString().isEmpty())
                out.append(still);
        }
    return out;
}

QString emptyTail()
{
    return QStringLiteral(" The Posture view reads head height and pelvis height from the face-on camera, and hip depth, "
                          "trunk bend and both knees down the line, at P1 to P8.");
}

void buildThisShot(Ctx &c, ChartsPosturePresenter::Result &R)
{
    QVariantMap out;
    R.posUs = c.pUs;
    const bool has = c.shot && c.view.available;
    out.insert(QStringLiteral("available"), has);
    if (!has) {
        out.insert(QStringLiteral("empty"), (c.shot || !c.in.pickedSwingDir.trimmed().isEmpty()
                                                 ? QStringLiteral("No posture readings for this swing.") : QStringLiteral("Select a swing to review."))
                                                + emptyTail());
        R.thisShot = out;
        return;
    }
    const ShotSeries &s = *c.shot;
    const PostureShotView &v = c.view;
    const bool inSession = c.index >= 0;
    out.insert(QStringLiteral("empty"), QString());
    out.insert(QStringLiteral("ordinal"), s.ordinal);
    out.insert(QStringLiteral("shotId"), s.shotId);
    out.insert(QStringLiteral("swingDir"), s.swingDir);
    out.insert(QStringLiteral("inSession"), inSession);
    const QString club = s.club.toLower();
    out.insert(QStringLiteral("kicker"), (inSession ? QStringLiteral("Shot %1 of %2").arg(s.ordinal).arg(c.n) : QStringLiteral("This swing"))
                                             + (club.isEmpty() ? QString() : QStringLiteral("  ·  ") + club) + QStringLiteral("  ·  Posture"));
    out.insert(QStringLiteral("graded"), c.pick.graded);
    out.insert(QStringLiteral("head"), W::Sentence().append(v.headline).toVariant());
    out.insert(QStringLiteral("headText"), v.headline);
    const PostureMatch &used = v.patternSource == QLatin1String("session") ? v.sessionMatch : v.match;
    out.insert(QStringLiteral("pattern"), patternMap(c.in.pattern, v.pattern, v.patternName, v.patternSource, v.patternSourceWords,
                                                     v.patternLine, used, v.match, v.trail));
    out.insert(QStringLiteral("patternLine"), v.patternLine);

    // THE GHOST (Against)
    const QString ag = c.in.against;
    PostureGrid ghost = postureEmptyGrid();
    QString ghostWord, ghostRefusal;
    if (ag == QLatin1String("previous")) {
        const int pi = c.index >= 0 ? c.index - 1 : c.n - 1;
        if (pi >= 0 && pi < c.n && hasPosture(c.inputs[size_t(pi)])) { ghost = c.inputs[size_t(pi)].value; ghostWord = QStringLiteral("previous swing"); }
        else ghostRefusal = QStringLiteral("The swing before this one has no posture reading.");
    } else if (ag == QLatin1String("reference")) {
        if (!c.refOk) ghostRefusal = c.refWhy;
        else if (c.refIsPicked) ghostRefusal = QStringLiteral("This swing is your reference ★.");
        else if (!hasPosture(c.refIn)) ghostRefusal = QStringLiteral("The reference swing carries no posture reading.");
        else { ghost = c.refIn.value; ghostWord = QStringLiteral("the reference ★"); }
    } else if (ag == QLatin1String("median")) {
        if (c.ready >= 3) {
            for (int r = 0; r < kPostureRows; ++r)
                for (int p = 0; p < kPos; ++p) {
                    const box::BoxStats &b = c.sess.rows[size_t(r)].boxes[size_t(p)];
                    ghost[size_t(r)][size_t(p)] = b.n >= 3 ? b.med : kNaN;
                }
            ghostWord = QStringLiteral("session median, n %1").arg(c.ready);
        } else {
            ghostRefusal = QStringLiteral("Three shots with a posture reading are needed for a session median; this session has %1.").arg(W::countWord(c.ready));
        }
    }

    QVariantList rows;
    for (int r = 0; r < kPostureRows; ++r)
        rows.append(rowMap(v.rows[size_t(r)], ghost[size_t(r)], c.pick.value[size_t(r)], c.pUs, nullptr, {}));
    QString sub = QStringLiteral("change from address");
    if (!ghostWord.isEmpty()) sub += QStringLiteral("  ·  dashed: ") + ghostWord;
    out.insert(QStringLiteral("profile"), QVariantMap{
        { QStringLiteral("positions"), positionsList(c.pUs) }, { QStringLiteral("rows"), rows },
        { QStringLiteral("ghostWord"), ghostWord }, { QStringLiteral("ghostRefusal"), ghostRefusal }, { QStringLiteral("sub"), sub },
        { QStringLiteral("caption"), QStringLiteral("Shaded: the %1 band, where good players are at each position. The hairline: where a "
                                                    "fault fires. A ring and a word: outside the band, and on which side. ⚠: drawn, not "
                                                    "matched, never ringed.").arg(v.patternName) } });

    QVariantList exits, deps;
    for (const PostureExit &e : v.exits) exits.append(exitMap(e, c.pUs));
    for (const PostureDeparture &d : v.departures) deps.append(departureMap(d, c.pUs));
    out.insert(QStringLiteral("exits"), exits);
    out.insert(QStringLiteral("departures"), deps);
    out.insert(QStringLiteral("whatMoved"), QVariantMap{ { QStringLiteral("top"), movedMap(v.top, c.pUs) },
                                                          { QStringLiteral("impact"), movedMap(v.impact, c.pUs) } });

    QVariantList findings, working;
    ledgersOf(c.env, c.snap.get(), c.shot.get(), c.pick, v, c.pUs, &findings, &working);
    out.insert(QStringLiteral("findings"), findings);
    out.insert(QStringLiteral("workingWell"), working);
    out.insert(QStringLiteral("findingsEmpty"), c.pick.graded ? QStringLiteral("No posture fault fired on this swing.")
                                                              : QStringLiteral("Diagnostics have not graded this swing yet."));
    out.insert(QStringLiteral("workingWellEmpty"), c.pick.graded ? QStringLiteral("Nothing in the posture stood out as working well on this swing.")
                                                                 : QStringLiteral("Diagnostics have not graded this swing yet."));

    // the absolutes' boxes are the session's pool (the shots with a reading), else this shot alone
    std::vector<int> pool;
    if (c.sess.available) {
        for (const PostureShotInput &s2 : c.inputs) if (hasPosture(s2)) pool.push_back(s2.ordinal);
    } else {
        pool.push_back(s.ordinal);
    }
    const QVariantMap address = addressMapOf(v.absolutes, v.addressLines, c.env, c.sess.available ? c.inputs : std::vector<PostureShotInput>{ c.pick },
                                             pool, true, QStringLiteral("shots"));
    out.insert(QStringLiteral("address"), address);
    out.insert(QStringLiteral("absolutes"), address.value(QStringLiteral("rows")));   // the same rows (ABSOLUTE = ADDRESS)
    out.insert(QStringLiteral("trust"), trustMap(v.trust));

    // THE PICTURE: this swing's frames at address, the top and impact, the frozen coach lines, the bands at impact
    {
        PictureOptions po;
        po.stillDir = pictureStillDir();
        po.ordinal = s.ordinal;
        {
            QMutexLocker sl(&c.w.stillMutex);
            po.stillErrors = c.w.stillErrors;
        }
        const QVariantMap pic = posturePicture(pictureDocOf(c.w, s.swingDir), pictureBandsOf(c.env, v.pattern, v.patternName), po);
        out.insert(QStringLiteral("picture"), pic);
        R.pendingStills = pendingStillsOf(pic);
    }
    R.standfirsts = standfirstsOf(v, c.pUs);
    out.insert(QStringLiteral("standfirsts"), R.standfirsts);
    R.thisShot = out;
}

// ── the choices ─────────────────────────────────────────────────────────────────────────────

void buildChoices(Ctx &c, ChartsPosturePresenter::Result &R)
{
    QVariantMap out;
    const QString scope = c.in.scope == QLatin1String("session") ? QStringLiteral("session") : QStringLiteral("shot");
    out.insert(QStringLiteral("scope"), scope);
    out.insert(QStringLiteral("scopes"), QVariantList{
        QVariantMap{ { QStringLiteral("id"), QStringLiteral("shot") }, { QStringLiteral("label"), QStringLiteral("This shot") }, { QStringLiteral("current"), scope == QLatin1String("shot") } },
        QVariantMap{ { QStringLiteral("id"), QStringLiteral("session") }, { QStringLiteral("label"), QStringLiteral("Session") }, { QStringLiteral("current"), scope == QLatin1String("session") } } });

    const QString ag = c.in.against;
    const int pi = c.index >= 0 ? c.index - 1 : c.n - 1;
    const bool prevOk = pi >= 0 && pi < c.n && hasPosture(c.inputs[size_t(pi)]);
    const bool refOk = c.refOk && !c.refIsPicked && hasPosture(c.refIn);
    const QString refWhy = !c.refSet ? QStringLiteral("no reference swing is set: ★ marks one")
                         : !c.refOk ? c.refWhy
                         : c.refIsPicked ? QStringLiteral("this swing is your reference")
                         : QStringLiteral("the reference swing carries no posture reading");
    QVariantList ags;
    const auto addA = [&](const QString &id, const QString &label, bool enabled, const QString &why) {
        ags.append(QVariantMap{ { QStringLiteral("id"), id }, { QStringLiteral("label"), label }, { QStringLiteral("current"), ag == id },
                                { QStringLiteral("enabled"), enabled }, { QStringLiteral("why"), enabled ? QString() : why } });
    };
    addA(QStringLiteral("address"), QStringLiteral("address"), true, QString());
    addA(QStringLiteral("previous"), QStringLiteral("previous"), prevOk, QStringLiteral("the swing before this one has no posture reading"));
    addA(QStringLiteral("reference"), QStringLiteral("reference ★"), refOk, refWhy);
    addA(QStringLiteral("median"), QStringLiteral("session median"), c.ready >= 3,
         QStringLiteral("three shots with a posture reading are needed; this session has %1").arg(W::countWord(c.ready)));
    out.insert(QStringLiteral("against"), ag);
    out.insert(QStringLiteral("againstOptions"), ags);

    QVariantList pats;
    static const char *const names[] = { "Auto", "Level", "Squat and push" };
    for (int p = -1; p <= 1; ++p)
        pats.append(QVariantMap{ { QStringLiteral("id"), p }, { QStringLiteral("label"), QString::fromLatin1(names[p + 1]) },
                                 { QStringLiteral("current"), c.in.pattern == p } });
    out.insert(QStringLiteral("pattern"), c.in.pattern);
    out.insert(QStringLiteral("patterns"), pats);
    const bool resolvedKnown = c.view.available || c.sess.available;
    const PosturePattern resolved = c.view.available ? c.view.pattern : c.sess.pattern;
    out.insert(QStringLiteral("patternResolved"), resolvedKnown ? QVariant(int(resolved)) : nullv());
    const QString after = c.in.pattern < 0 && resolvedKnown ? QStringLiteral("→ ") + posturePatternName(resolved) : QString();
    out.insert(QStringLiteral("patternAfter"), after);

    out.insert(QStringLiteral("reference"), QVariantMap{
        { QStringLiteral("set"), c.refSet }, { QStringLiteral("swingDir"), c.refDir },
        { QStringLiteral("label"), c.refLabel }, { QStringLiteral("isThisShot"), c.refIsPicked },
        { QStringLiteral("linkText"), c.refIsPicked ? QStringLiteral("★ this swing is your reference") : QStringLiteral("★ set this swing as your reference") },
        { QStringLiteral("canSet"), bool(c.shot) } });

    QString agLabel = QStringLiteral("address");
    for (const QVariant &a : ags) if (a.toMap().value(QStringLiteral("current")).toBool()) agLabel = a.toMap().value(QStringLiteral("label")).toString();
    out.insert(QStringLiteral("summary"), QStringLiteral("Pattern %1%2  ·  against %3")
                                              .arg(QString::fromLatin1(names[c.in.pattern + 1]), after.isEmpty() ? QString() : QStringLiteral(" ") + after, agLabel));
    // the series whose curve on show adds the Posture section (the rows' own, and the head's rise)
    QStringList keys;
    for (const PostureRowSpec &sp : postureRowSpecs()) keys << sp.metricKey;
    keys << QStringLiteral("headRise");
    out.insert(QStringLiteral("onShowKeys"), keys);
    R.choices = out;
}

// The shots' readings, through the worker's cache.
std::vector<PostureShotInput> readInputs(ChartsPosturePresenter::Worker *w, const SessionSnapshot &snap, const PostureEnv &env)
{
    std::vector<PostureShotInput> out;
    out.reserve(snap.shots.size());
    QHash<const ShotSeries *, CacheEntry> fresh;
    for (const auto &s : snap.shots) {
        if (w) {
            const auto it = w->cache.constFind(s.get());
            if (it != w->cache.constEnd() && it->shot.lock() == s) {
                out.push_back(it->in);
                fresh.insert(s.get(), *it);
                continue;
            }
        }
        out.push_back(postureInputFromShot(*s, env));
        if (w) fresh.insert(s.get(), CacheEntry{ s, out.back() });
    }
    if (w) w->cache = std::move(fresh);
    return out;
}

} // namespace

// ── the still maker (process-wide: the app sets it once) ────────────────────────────────────────

static QMutex &stillMakerMutex() { static QMutex m; return m; }
static ChartsPosturePresenter::StillMaker &stillMakerRef() { static ChartsPosturePresenter::StillMaker f; return f; }
static ChartsPosturePresenter::StillMaker stillMaker()
{
    QMutexLocker l(&stillMakerMutex());
    return stillMakerRef();
}

// ── the job ─────────────────────────────────────────────────────────────────────────────────

static std::shared_ptr<ChartsPosturePresenter::Result> computePosture(ChartsPosturePresenter::Worker &w, const ChartsPosturePresenter::Inputs &in)
{
    QMutexLocker lk(&w.mutex);
    const PostureEnv &env = postureSharedEnv();
    auto R = std::make_shared<ChartsPosturePresenter::Result>();
    std::shared_ptr<const SessionSnapshot> snap = in.snapshot;
    if (!snap) snap = std::make_shared<SessionSnapshot>();
    Ctx c(w, in, snap, env);
    c.n = c.snap->count();
    c.inputs = readInputs(&w, *c.snap, env);
    for (const PostureShotInput &s : c.inputs) if (hasPosture(s)) ++c.ready;

    // the picked shot: the snapshot's, else its own detail (a live shot, another session's)
    const QString want = cleanDir(in.pickedSwingDir);
    if (!want.isEmpty())
        for (int i = 0; i < c.n; ++i)
            if (cleanDir(c.snap->shots[size_t(i)]->swingDir) == want) { c.index = i; break; }
    if (c.index >= 0) {
        c.shot = c.snap->shots[size_t(c.index)];
        c.pick = c.inputs[size_t(c.index)];
    } else if (!want.isEmpty() && !in.pickedDetail.value(QStringLiteral("series")).toList().isEmpty()) {
        auto sh = postureShotFromDetail(in.pickedDetail, want, postureKeys(env));
        sh->ordinal = c.n + 1;
        c.pick = postureInputFromShot(*sh, env);
        c.shot = sh;
    }
    c.hasPick = c.shot && hasPosture(c.pick);
    resolveReference(c);

    const PostureAddressHistory &hist = addressHistoryOf(w, c.snap->sessionDir, env);
    c.sess = postureSession(c.inputs, env, in.pattern, &c.snap->tiers, &c.snap->workOnOrder, QStringLiteral("shots"), &hist);
    if (c.shot) c.view = postureShot(c.pick, env, in.pattern, c.sess.available ? &c.sess : nullptr, &c.snap->workOnOrder, &hist);
    c.pUs = positionUs(c.shot.get());

    R->available = c.ready > 0 || c.hasPick;
    buildThisShot(c, *R);
    // the session's positions: the picked shot's, else the first shot with a reading
    std::array<qint64, kPos> sessUs = c.pUs;
    if (!c.shot)
        for (int i = 0; i < c.n; ++i) if (hasPosture(c.inputs[size_t(i)])) { sessUs = positionUs(c.snap->shots[size_t(i)].get()); break; }
    R->session = sessionMapOf(c.inputs, c.sess, in.pattern, c.index, c.shot ? &c.pick : nullptr, sessUs);
    buildChoices(c, *R);
    return R;
}

QVariantMap postureSessionReportPayload(const SessionSnapshot &snapshot, int patternMode, const QString &noun)
{
    const int mode = patternMode < -1 || patternMode > 1 ? -1 : patternMode;
    const QString many = noun.trimmed().isEmpty() ? QStringLiteral("shots") : noun.trimmed();
    const PostureEnv &env = postureSharedEnv();
    const std::vector<PostureShotInput> inputs = readInputs(nullptr, snapshot, env);
    const PostureAddressHistory hist = readAddressHistory(snapshot.sessionDir, env);
    const PostureSessionView sv = postureSession(inputs, env, mode, &snapshot.tiers, &snapshot.workOnOrder, many, &hist);
    std::array<qint64, kPos> us = positionUs(nullptr);
    for (size_t i = 0; i < inputs.size(); ++i) if (hasPosture(inputs[i])) { us = positionUs(snapshot.shots[i].get()); break; }
    QVariantMap out = sessionMapOf(inputs, sv, mode, -1, nullptr, us, many);
    if (!sv.available) return out;

    // THE PICTURE (§5.7): the session's TYPICAL swing — the one whose distance to the session's pattern is nearest the
    // session's median distance (ties: the earlier) — else the first swing with a reading. Its stills are the caller's
    // to make (no stillDir: each frame carries its `still`).
    std::vector<std::pair<int, double>> ds;   // ordinal, d to the session's pattern
    for (const PosturePatternMark &m : sv.strip) {
        const double d = sv.pattern == PosturePattern::Level ? m.dLevel : m.dSquatPush;
        if (m.ok && std::isfinite(d)) ds.emplace_back(m.ordinal, d);
    }
    int typical = -1;
    double median = kNaN;
    if (!ds.empty()) {
        std::vector<double> v;
        for (const auto &p : ds) v.push_back(p.second);
        std::sort(v.begin(), v.end());
        median = v.size() % 2 ? v[v.size() / 2] : 0.5 * (v[v.size() / 2 - 1] + v[v.size() / 2]);
        double best = std::numeric_limits<double>::infinity();
        for (const auto &p : ds) if (std::fabs(p.second - median) < best) { best = std::fabs(p.second - median); typical = p.first; }
    }
    if (typical < 0) for (const PostureShotInput &s : inputs) if (hasPosture(s)) { typical = s.ordinal; break; }
    QString dir;
    for (const PostureShotInput &s : inputs) if (s.ordinal == typical) dir = s.swingDir;
    if (dir.isEmpty()) return out;
    const pinpoint::SwingStore::DocInfo info = pinpoint::SwingStore::info(dir);
    QString err;
    const QJsonObject root = info.exists() ? pinpoint::SwingStore::load(dir, &err) : QJsonObject{};
    PictureDoc doc = postureDocFrom(root, dir, QStringLiteral("%1-%2").arg(info.size).arg(info.mtimeMs));
    if (root.isEmpty()) doc.why = info.exists() ? QStringLiteral("the swing's document could not be read") : QStringLiteral("the swing's document is not on disk");
    PictureOptions po;
    po.ordinal = typical;
    po.maxPx = 1400;
    QVariantMap pic = posturePicture(doc, pictureBandsOf(env, sv.pattern, sv.patternName), po);
    const QString one = many.endsWith(QLatin1Char('s')) ? many.chopped(1) : many;
    pic.insert(QStringLiteral("typical"), QStringLiteral("A typical %1: %1 %2").arg(one).arg(typical));
    pic.insert(QStringLiteral("typicalWhy"), std::isfinite(median)
        ? QStringLiteral("the %1 whose fit to %2 is nearest the session's median").arg(one, sv.patternName)
        : QStringLiteral("the first %1 with a posture reading").arg(one));
    out.insert(QStringLiteral("picture"), pic);
    return out;
}

// ── the object ──────────────────────────────────────────────────────────────────────────────

ChartsPosturePresenter::ChartsPosturePresenter(QObject *parent)
    : QObject(parent), m_worker(std::make_shared<Worker>())
{
    m_pool.setMaxThreadCount(1);
    m_pool.setObjectName(QStringLiteral("ChartsPosturePresenter"));
    schedule();
}

ChartsPosturePresenter::~ChartsPosturePresenter()
{
    m_pool.waitForDone();
}

QObject *ChartsPosturePresenter::model() const { return m_model.data(); }

void ChartsPosturePresenter::setModel(QObject *m)
{
    if (m_model == m) return;
    if (m_model) disconnect(m_model, nullptr, this, nullptr);
    m_model = m;
    if (auto *sm = qobject_cast<SessionSeriesModel *>(m))
        connect(sm, &SessionSeriesModel::snapshotChanged, this, &ChartsPosturePresenter::schedule);
    emit inputsChanged();
    schedule();
}

std::shared_ptr<const SessionSnapshot> ChartsPosturePresenter::currentSnapshot() const
{
    if (auto *sm = qobject_cast<SessionSeriesModel *>(m_model.data())) return sm->snapshot();
    return m_directSnapshot;
}

void ChartsPosturePresenter::setSnapshot(std::shared_ptr<const SessionSnapshot> snapshot)
{
    m_directSnapshot = std::move(snapshot);
    schedule();
}

#define PP_SETTER(Type, name, field)                                       \
    void ChartsPosturePresenter::name(Type v)                              \
    {                                                                      \
        if (m_in.field == v) return;                                       \
        m_in.field = v;                                                    \
        emit inputsChanged();                                              \
        schedule();                                                        \
    }
PP_SETTER(const QString &, setPickedSwingDir, pickedSwingDir)
PP_SETTER(const QString &, setRegisterName, reg)
PP_SETTER(const QString &, setScope, scope)
PP_SETTER(const QString &, setAgainst, against)
PP_SETTER(const QString &, setReferenceSwingDir, referenceSwingDir)
#undef PP_SETTER

void ChartsPosturePresenter::setPattern(int p)
{
    const int v = (p < -1 || p > 1) ? -1 : p;
    if (m_in.pattern == v) return;
    m_in.pattern = v;
    emit inputsChanged();
    schedule();
}

void ChartsPosturePresenter::setPickedDetail(const QVariantMap &d)
{
    if (m_in.pickedDetail.isSharedWith(d)) return;
    m_in.pickedDetail = d;
    emit inputsChanged();
    schedule();
}

void ChartsPosturePresenter::setPlayheadUs(qint64 us)
{
    if (m_playheadUs == us) return;
    m_playheadUs = us;
    emit playheadChanged();
    updateSelected(false);
}

bool ChartsPosturePresenter::available() { flushIfSynchronous(); return m_result && m_result->available; }
QVariantMap ChartsPosturePresenter::choices() { flushIfSynchronous(); return m_result ? m_result->choices : QVariantMap{}; }
QVariantMap ChartsPosturePresenter::thisShot() { flushIfSynchronous(); return m_result ? m_result->thisShot : QVariantMap{}; }
QVariantMap ChartsPosturePresenter::session() { flushIfSynchronous(); return m_result ? m_result->session : QVariantMap{}; }

QVariantMap ChartsPosturePresenter::standfirst() const
{
    if (!m_result || m_selected < 0 || m_selected >= m_result->standfirsts.size()) return {};
    return m_result->standfirsts.at(m_selected).toMap();
}

qint64 ChartsPosturePresenter::seekUsFor(const QString &tag) const
{
    if (!m_result) return -1;
    const QString t = tag.trimmed().toUpper();
    if (!t.startsWith(QLatin1Char('P'))) return -1;
    bool ok = false;
    const int i = t.mid(1).toInt(&ok) - 1;
    return ok && i >= 0 && i < kPos ? m_result->posUs[size_t(i)] : -1;
}

void ChartsPosturePresenter::schedule()
{
    m_dirty = true;
    if (m_synchronous || m_scheduled) return;
    m_scheduled = true;
    QMetaObject::invokeMethod(this, &ChartsPosturePresenter::startJob, Qt::QueuedConnection);
}

void ChartsPosturePresenter::startJob()
{
    m_scheduled = false;
    if (m_synchronous || m_running) return;     // apply() re-runs a dirty presenter
    Inputs in = m_in;
    in.snapshot = currentSnapshot();
    m_dirty = false;
    m_running = true;
    emit busyChanged();
    auto w = m_worker;
    m_pool.start([this, w, in]() {
        auto r = computePosture(*w, in);
        QMetaObject::invokeMethod(this, [this, r]() { apply(r); }, Qt::QueuedConnection);
    });
}

void ChartsPosturePresenter::apply(std::shared_ptr<Result> r)
{
    const bool wasRunning = m_running;
    m_running = false;
    m_result = std::move(r);
    emit resultsChanged();
    updateSelected(true);
    if (m_dirty && !m_synchronous) startJob();
    else if (wasRunning) emit busyChanged();
    makeStills();
}

void ChartsPosturePresenter::flushIfSynchronous()
{
    if (!m_synchronous || !m_dirty) return;
    m_dirty = false;
    Inputs in = m_in;
    in.snapshot = currentSnapshot();
    m_result = computePosture(*m_worker, in);
    // synchronous (tests): the stills are made inline and the result read once more with them
    if (m_result && !m_result->pendingStills.isEmpty() && stillMaker()) {
        QVariantList todo;
        for (const QVariant &v : std::as_const(m_result->pendingStills)) {
            const QString out = v.toMap().value(QStringLiteral("outPath")).toString();
            if (!m_stillsAsked.contains(out)) { m_stillsAsked.insert(out); todo.append(v); }
        }
        if (!todo.isEmpty()) {
            const QHash<QString, QString> errs = stillMaker()(todo);
            {
                QMutexLocker sl(&m_worker->stillMutex);
                for (auto it = errs.cbegin(); it != errs.cend(); ++it) if (!it.value().isEmpty()) m_worker->stillErrors.insert(it.key(), it.value());
            }
            m_result = computePosture(*m_worker, in);
        }
    }
    updateSelected(true);
}

// The picture's stills, made once each off the GUI thread by the app's maker; the picture is read again when they land.
void ChartsPosturePresenter::makeStills()
{
    if (m_synchronous || m_stillsBusy || !m_result || m_result->pendingStills.isEmpty() || !stillMaker()) return;
    QVariantList todo;
    for (const QVariant &v : std::as_const(m_result->pendingStills)) {
        const QString out = v.toMap().value(QStringLiteral("outPath")).toString();
        if (!m_stillsAsked.contains(out)) { m_stillsAsked.insert(out); todo.append(v); }
    }
    if (todo.isEmpty()) return;
    m_stillsBusy = true;
    QPointer<ChartsPosturePresenter> self(this);
    auto w = m_worker;
    const StillMaker maker = stillMaker();
    QThreadPool::globalInstance()->start([self, w, todo, maker]() {
        const QHash<QString, QString> errs = maker(todo);
        {
            QMutexLocker sl(&w->stillMutex);
            for (auto it = errs.cbegin(); it != errs.cend(); ++it) if (!it.value().isEmpty()) w->stillErrors.insert(it.key(), it.value());
        }
        QMetaObject::invokeMethod(qApp, [self]() {
            if (!self) return;
            self->m_stillsBusy = false;
            self->schedule();
        }, Qt::QueuedConnection);
    });
}

void ChartsPosturePresenter::setStillMaker(StillMaker maker)
{
    QMutexLocker l(&stillMakerMutex());
    stillMakerRef() = std::move(maker);
}

void ChartsPosturePresenter::setPictureDocumentForTest(const QString &swingDir, const QString &jsonFile)
{
    const QString dir = cleanDir(swingDir);
    {
        QMutexLocker lk(&m_worker->mutex);
        if (jsonFile.trimmed().isEmpty()) {
            m_worker->pictures.remove(dir);
        } else {
            QFile f(jsonFile);
            QJsonObject root;
            if (f.open(QIODevice::ReadOnly)) root = QJsonDocument::fromJson(f.readAll()).object();
            Worker::PictureEntry e;
            e.pinned = true;
            e.doc = postureDocFrom(root, dir, QStringLiteral("test"));
            if (root.isEmpty()) e.doc.why = QStringLiteral("the test's document %1 could not be read").arg(jsonFile);
            m_worker->pictures.insert(dir, std::move(e));
        }
    }
    schedule();
}

void ChartsPosturePresenter::setSynchronous(bool on)
{
    m_synchronous = on;
    if (on) {
        m_pool.waitForDone();
        QCoreApplication::processEvents();
        m_dirty = true;
    } else if (m_dirty) {
        schedule();
    }
}

bool ChartsPosturePresenter::waitForIdle(int msTimeout)
{
    if (m_synchronous) { flushIfSynchronous(); return true; }
    QDeadlineTimer deadline(msTimeout);
    while ((m_running || m_dirty || m_scheduled) && !deadline.hasExpired()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        m_pool.waitForDone(20);
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return !m_running && !m_dirty;
}

// The playhead's position: the latest present position at or before it, else the first present one
// (the wrist presenter's rule). Fires only when it moves to another position.
void ChartsPosturePresenter::updateSelected(bool force)
{
    int best = -1;
    if (m_result) {
        for (int i = 0; i < kPos; ++i)
            if (m_result->posUs[size_t(i)] >= 0 && m_result->posUs[size_t(i)] <= m_playheadUs) best = i;
        if (best < 0)
            for (int i = 0; i < kPos; ++i) if (m_result->posUs[size_t(i)] >= 0) { best = i; break; }
    }
    if (best < 0) best = 0;
    if (best != m_selected || force) {
        m_selected = best;
        emit selectedChanged();
    }
}

} // namespace pinpoint::charts
