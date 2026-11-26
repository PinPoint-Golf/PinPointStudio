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

#include "charts_gadget.h"   // chartsGadgetMap: the report's payload and the still maker's maps, off the typed results
#include "../typeset/box_stats.h"
#include "charts_words.h"
#include "posture_picture.h"
#include "session_posture.h"
#include "session_series_extract.h"   // folderShotId (refactor 5.5a)
#include "../../Stores/series_store.h"   // seriesSwingLabelOf: the ★ label
#include "../../Diagnostics/derived/series/variability_clubs.h"                              // perClubInputs: one window per club (R2b)

#include "../variability/variability_facade.h"              // VariabilityResultView: the `variabilityResult` QML reads
#include "../../Vocabulary/Metrics/variability/variability_grader.h"

#include "../../Core/pp_debug.h"
#include "../../Diagnostics/model/characteristic.h"
#include "../../Diagnostics/content/condition_reading.h"
#include "../../Diagnostics/derived/session_day_readings_rules.h"   // sessionCamerasOf: this session's cameras (refactor 5.5d)

#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QDeadlineTimer>
#include <QDir>
#include <QStandardPaths>
#include <QHash>
#include <QJsonObject>
#include <QLocale>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
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
namespace U = postureui;

// ── the result and the worker ───────────────────────────────────────────────────────────────

struct ChartsPosturePresenter::Result {
    bool available = false;
    postureui::Choices  choices;
    postureui::ThisShot thisShot;
    postureui::Session  session;
    QList<postureui::Sentence> standfirsts;            // 8 Sentences (thisShot.standfirsts)
    std::array<qint64, kPosturePositions> posUs{};     // the picked shot's P µs, −1 none
    QVariantList pendingStills;                        // the picture's stills not made yet, as the still maker reads them
};

struct ChartsPosturePresenter::Worker {
    QMutex mutex;                                      // held for a whole job
    QHash<const ShotSeries *, CacheEntry> cache;

    // the picture strip's documents (posture_picture.h), by cleaned dir and document stamp; a few kept
    struct PictureEntry { qint64 size = -1, mtimeMs = -1; bool pinned = false; PictureDoc doc; };
    QHash<QString, PictureEntry> pictures;
    QStringList pictureOrder;                          // least recent first
    // the stills the maker could not make: outPath → why (written on the GUI thread, read by a job)
    QMutex stillMutex;
    QHash<QString, QString> stillErrors;

    // THIS session's cameras, off the first of its shots whose document records them, read through the snapshot's own
    // door (the earlier sessions' come from their summaries: refactor 5.5d), kept per session folder once read
    QString camerasDir;
    pinpoint::analysis::SessionCameras cameras;

    // The env the caches above were read through (postureSharedEnv's snapshot). A new one — the shared
    // norm set moved after a corridor save — empties them, so no shot keeps an input read over the old.
    std::shared_ptr<const PostureEnv> env;
};

namespace {

constexpr int kPos = kPosturePositions;
const QString kTrailKneeCondition = QStringLiteral("trail_knee_straighten");
// Fired, it is a Finding though it names no departure (§4.3, 22 Oct): several parts at once, a composite.
const QString kLossOfPosture = QStringLiteral("loss_of_posture");

// ── small words and numbers ─────────────────────────────────────────────────────────────────

using W::fin;
QVariant nv(double v) { return fin(v) ? QVariant(v) : QVariant::fromValue(nullptr); }
QVariant nullv() { return QVariant::fromValue(nullptr); }
double nanv() { return std::numeric_limits<double>::quiet_NaN(); }

// words::Sentence as the gadget QML reads: its text, its runs and its first clause (Sentence::toVariant's shape).
U::Sentence sentenceOf(const W::Sentence &s)
{
    U::Sentence out;
    out.text = s.text();
    for (const W::Sentence::Run &r : s.runs) out.runs.append(U::Run{ r.text, r.fig });
    out.clause = W::firstClauseOf(out.text);
    return out;
}
U::Sentence sentenceOf(const QString &text) { return sentenceOf(W::Sentence().append(text)); }
QString cleanDir(const QString &d) { return d.trimmed().isEmpty() ? QString() : QDir::cleanPath(d.trimmed()); }
QString golferSafe(const QString &t) { return t.contains(QLatin1Char('`')) ? QString() : t; }   // a backtick = an author's note

const PostureRowSpec &specOf(PostureRow r) { return postureRowSpec(r); }
QString valueText(PostureRow r, double v) { return W::withUnit(postureFigure(r, v), specOf(r).unit); }
QString bareText(PostureRow r, double v) { return W::withUnit(W::fig(std::fabs(v), specOf(r).decimals), specOf(r).unit); }

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

// The keys postureInputFromShot reads: the rows' series, the butt line, and every rung of the absolutes' ladders.
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
    sh->shotId = folderShotId(dir);
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

// "shot 3, 18 Aug 2026": which swing the ★ is — its swing number and its session's day (SessionIdentity).
QString referenceLabelOf(const QString &dir) { return pinpoint::seriesSwingLabelOf(dir); }

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

// The reference: the session's own reading when it is one of its shots; else the ★ the GUI thread picked through the ★
// swing's own session (referencePickOf, charts_presenter_core.h: that session's shot as its series read it, without its
// place or findings), read here as any shot is (postureInputFromShot); else why, in its words.
void resolveReference(Ctx &c)
{
    const QString dir = cleanDir(c.in.referenceSwingDir);
    c.refDir = dir;
    c.refSet = !dir.isEmpty();
    if (!c.refSet) { c.refWhy = c.in.reference.why; return; }
    c.refLabel = referenceLabelOf(dir);
    c.refIsPicked = c.shot && cleanDir(c.shot->swingDir) == dir;
    for (int i = 0; i < c.n; ++i)
        if (cleanDir(c.snap->shots[size_t(i)]->swingDir) == dir) { c.refOk = true; c.refIn = c.inputs[size_t(i)]; return; }
    if (c.in.reference.shot) {
        c.refOk = true;
        c.refIn = postureInputFromShot(*c.in.reference.shot, c.env);
    } else {
        c.refWhy = c.in.reference.why;
    }
}

// ── the typed values (charts_posture_types.h) ───────────────────────────────────────────────

U::Position positionOf(int i, qint64 us)
{
    U::Position p;
    p.index = i;
    p.tag = posturePositionTag(i);
    p.phase = posturePhaseIds()[size_t(i)];
    p.name = posturePositionWhen(i);
    p.available = us >= 0;
    p.tUs = us;
    p.seekUs = us;
    return p;
}
QList<U::Position> positionsList(const std::array<qint64, kPos> &us)
{
    QList<U::Position> out;
    for (int i = 0; i < kPos; ++i) out.append(positionOf(i, us[size_t(i)]));
    return out;
}

// FAULT, or null when the cell has no fault line on that side.
QVariant faultOf(const PostureFault &f, PostureRow r)
{
    if (!f.has) return nullv();
    U::Fault out;
    out.edge = nv(f.edge);
    out.edgeText = postureFigure(r, f.edge);
    out.conditionId = f.conditionId;
    out.label = f.label;
    return QVariant::fromValue(out);
}

QString boxFmt(PostureRow r, double v) { return postureFigure(r, v); }

// box_map.h's BOX, typed: the outliers as `i + 1` (values are per shot) and the texts once there is a shot.
U::Box boxGadget(const box::BoxStats &b, const std::function<QString(double)> &fmt)
{
    U::Box out;
    out.n = b.n;
    out.min = nv(b.min); out.max = nv(b.max);
    out.q1 = nv(b.q1); out.med = nv(b.med); out.q3 = nv(b.q3);
    out.lo = nv(b.lo); out.hi = nv(b.hi);
    for (int i : b.outliers) out.outliers.append(i + 1);
    if (fmt && b.n > 0) out.texts = U::BoxTexts{ fmt(b.lo), fmt(b.q1), fmt(b.med), fmt(b.q3), fmt(b.hi) };
    return out;
}

// A box, its outliers re-numbered from the core's pool order (the shots with a posture reading) to ordinals.
U::Box boxOf(const box::BoxStats &b, PostureRow r, const std::vector<int> &poolOrdinals)
{
    U::Box out = boxGadget(b, [r](double v) { return boxFmt(r, v); });
    out.outliers.clear();
    for (int i : b.outliers) out.outliers.append(i >= 0 && size_t(i) < poolOrdinals.size() ? poolOrdinals[size_t(i)] : i + 1);
    return out;
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

U::Cell cellOf(const PostureRowView &rv, const PostureCellView &c, double ghost, qint64 seekUs, double picked)
{
    const PostureRow r = rv.row;
    const bool avail = fin(c.value);
    const PostureBand &b = c.band;
    // decision 16: the cell's second reading (trunk bend at P3–P5: the back line), under its own ⚠
    const bool altAvail = c.alt && fin(c.altValue);
    U::Cell out;
    out.alt = c.alt;
    out.altAvailable = altAvail;
    out.altValue = nv(c.altValue);
    out.altLabel = c.altLabel;
    out.altFigure = c.altFigure;
    out.altText = c.altText;
    out.altWarn = c.altWarned && altAvail ? QStringLiteral("⚠") : QString();
    out.altWarnShort = altAvail ? c.altWarnShort : QString();
    out.index = c.pos;
    out.tag = c.tag;
    out.available = avail;
    out.value = nv(c.value);
    out.text = c.text;
    out.valueText = valueText(r, c.value);
    // the ⚠ is DRAWN only on a figure: a refused reading is a plain "-" (its reason is said once, in TRUST)
    out.warned = c.warned;
    out.warn = c.warned && avail ? QStringLiteral("⚠") : QString();
    out.warnReason = c.warnReason;
    out.warnShort = avail ? warnShortOf(r, c) : QString();
    out.gateWords = c.gateWords;
    out.counted = c.counted;
    out.banded = b.has;
    out.bandLo = nv(b.lo); out.bandHi = nv(b.hi); out.bandMu = nv(b.mu);
    out.watchLo = nv(b.watchLo); out.watchHi = nv(b.watchHi);
    out.bandContext = b.contextId;
    out.bandInherited = b.inherited;
    out.bandSource = b.source;
    out.wash = b.has ? QStringLiteral("band") : QStringLiteral("none");
    out.z = nv(c.z);
    out.outside = c.outside;
    out.ring = c.outside;
    out.ringWord = c.ring;
    out.exit = c.exit;
    out.beyondFault = c.beyondFault;
    out.faultHi = faultOf(c.fault.hi, r);
    out.faultLo = faultOf(c.fault.lo, r);
    out.firedShots = c.firedShots;
    out.note = c.note;
    out.mark = c.outside ? QStringLiteral("ring") : avail ? QStringLiteral("dot") : QStringLiteral("none");
    out.ghost = nv(ghost);
    out.ghostText = postureFigure(r, ghost);
    out.seekUs = seekUs;
    out.picked = nv(picked);
    out.pickedText = postureFigure(r, picked);
    return out;
}

// Each shot's value at one cell, index = ordinal − 1, NaN where unmeasured (PpChBoxCell's `values`: the shots past the
// whiskers and the picked one are drawn from these; it reads NaN as it read null).
QList<double> valuesOf(const std::vector<PostureShotInput> *inputs, int maxOrdinal, PostureRow r, int p)
{
    QList<double> out;
    if (!inputs || maxOrdinal <= 0) return out;
    for (int i = 0; i < maxOrdinal; ++i) out.append(nanv());
    for (const PostureShotInput &s : *inputs)
        if (s.ordinal >= 1 && s.ordinal <= maxOrdinal) {
            const double v = s.value[size_t(r)][size_t(p)];
            out[s.ordinal - 1] = fin(v) ? v : nanv();
        }
    return out;
}

// One row. `ghost` / `picked` NaN-filled when absent; `boxes` non-null on Session (This shot's row has none).
U::Row rowOf(const PostureRowView &rv, const std::array<double, kPos> &ghost, const std::array<double, kPos> &picked,
             const std::array<qint64, kPos> &us, const std::array<box::BoxStats, kPos> *boxes,
             const std::vector<int> &poolOrdinals, const std::vector<PostureShotInput> *inputs = nullptr)
{
    int maxOrdinal = 0;
    if (inputs) for (const PostureShotInput &s : *inputs) maxOrdinal = std::max(maxOrdinal, s.ordinal);
    const PostureRow r = rv.row;
    const PostureRowSpec &s = specOf(r);
    Span sc;
    U::Row out;
    int outCount = 0;
    bool hasAlt = false;   // decision 16: a second reading under some cell (the QML gives the row its line)
    for (int p = 0; p < kPos; ++p) {
        const PostureCellView &c = rv.cells[size_t(p)];
        out.cells.append(cellOf(rv, c, ghost[size_t(p)], us[size_t(p)], picked[size_t(p)]));
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
            U::BoxCell bc;
            bc.box = boxOf(b, r, poolOrdinals);
            bc.n = b.n;
            bc.median = nv(med);
            bc.medianText = postureFigure(r, med);
            bc.wide = nv(wide);
            bc.wideText = fin(wide) ? bareText(r, wide) + QStringLiteral(" wide") : QString();
            bc.picked = nv(picked[size_t(p)]);
            bc.values = valuesOf(inputs, maxOrdinal, r, p);
            out.boxes.append(bc);
        }
    }
    sc.finish(minSpanOf(r));
    out.key = rv.key;
    out.label = rv.label;
    out.metricKey = rv.metricKey;
    out.hueKey = rv.metricKey;
    out.camera = rv.camera;
    out.unit = rv.unit;
    out.decimals = s.decimals;
    out.poleWord = rv.poleWord;
    out.poleText = rv.poleWord.isEmpty() ? QString() : rv.poleWord + QStringLiteral(" ▲");
    out.plusWord = rv.plusWord;
    out.minusWord = rv.minusWord;
    out.present = rv.present;
    out.style = rv.style;
    out.styleWord = rv.styleWord;
    out.styleText = rv.style && !rv.styleWord.isEmpty() ? QStringLiteral("style: ") + rv.styleWord : QString();
    out.outCount = outCount;
    out.hasAlt = hasAlt;
    out.yLo = sc.lo;
    out.yHi = sc.hi;
    return out;
}

U::Trail trailOf(const PostureTrailStyle &t)
{
    U::Trail out;
    out.known = t.known;
    out.kept = t.kept;
    out.word = t.word;
    out.words = t.words;
    out.changeAtTop = nv(t.changeAtTop);
    out.changeText = valueText(PostureRow::TrailKnee, t.changeAtTop);
    return out;
}

U::Match matchOf(const PostureMatch &m)
{
    U::Match out;
    out.ok = m.ok;
    out.nearest = m.ok ? QVariant(int(m.nearest)) : nullv();
    out.nearestName = m.ok ? posturePatternName(m.nearest) : QString();
    out.fit = m.fit;
    out.fitWords = m.words;
    out.basis = m.basis;
    out.basisWords = m.basisWords;
    out.dLevel = nv(m.dLevel);
    out.dSquatPush = nv(m.dSquatPush);
    out.dLevelText = W::fig(m.dLevel, 2);
    out.dSquatPushText = W::fig(m.dSquatPush, 2);
    out.gap = nv(m.gap);
    out.cells = m.cells;
    out.rows = m.rows;
    return out;
}

U::Pattern patternOf(int mode, PosturePattern pat, const QString &name, const QString &source, const QString &sourceWords,
                     const QString &line, const PostureMatch &used, const PostureMatch &own, const PostureTrailStyle &trail)
{
    const U::Match m = matchOf(used);
    U::Pattern out;
    out.ok = m.ok;
    out.nearest = m.nearest;
    out.nearestName = m.nearestName;
    out.fit = m.fit;
    out.fitWords = m.fitWords;
    out.basis = m.basis;
    out.basisWords = m.basisWords;
    out.dLevel = m.dLevel;
    out.dSquatPush = m.dSquatPush;
    out.dLevelText = m.dLevelText;
    out.dSquatPushText = m.dSquatPushText;
    out.gap = m.gap;
    out.cells = m.cells;
    out.rows = m.rows;
    out.mode = mode;
    out.pattern = int(pat);
    out.name = name;
    out.source = source;
    out.sourceWords = sourceWords;
    out.line = line;
    out.own = matchOf(own);
    out.trail = trailOf(trail);
    return out;
}

U::Exit exitOf(const PostureExit &e, const std::array<qint64, kPos> &us)
{
    U::Exit out;
    out.row = specOf(e.row).key;
    out.rowLabel = specOf(e.row).label;
    out.pos = e.pos;
    out.tag = posturePositionTag(e.pos);
    out.value = nv(e.value);
    out.valueText = valueText(e.row, e.value);
    out.z = nv(e.z);
    out.zText = W::fig(e.z, 1);
    out.departure = e.departure;
    out.name = e.name;
    out.conditionIds = e.conditionIds;
    out.named = e.named;
    out.words = e.words;
    out.seekUs = usAt(us, e.pos);
    return out;
}

U::Departure departureOf(const PostureDeparture &d, const std::array<qint64, kPos> &us)
{
    U::Departure out;
    out.id = d.id;
    out.name = d.name;
    out.conditionIds = d.conditionIds;
    out.positions = tagsOf(d.positions);
    out.positionsText = rangeText(d.positions);
    out.firstTag = d.firstPos >= 0 ? posturePositionTag(d.firstPos) : QString();
    out.seekUs = usAt(us, d.firstPos);
    return out;
}

U::Moved movedOf(const PostureMoved &m, const std::array<qint64, kPos> &us)
{
    U::Moved out;
    out.pos = m.pos;
    out.tag = posturePositionTag(m.pos);
    out.when = m.when;
    out.line = m.line;
    out.head = m.head;
    out.bend = m.bend;
    out.attribution = m.attribution;
    out.hipsIn = m.hipsIn;
    out.hipsWarned = m.hipsWarned;
    out.bendWarned = m.bendWarned;
    out.seekUs = usAt(us, m.pos);
    return out;
}
U::WhatMoved whatMovedOf(const PostureMoved &top, const PostureMoved &impact, const std::array<qint64, kPos> &us)
{
    U::WhatMoved out;
    out.top = movedOf(top, us);
    out.impact = movedOf(impact, us);
    return out;
}

// ── THE "AT ADDRESS" BLOCK (posture_chart_design.md §7.4.2) ─────────────────────────────────────

// The block as the QML draws it: a row per reading the capture can produce (the core's), each with its scale
// (xLo…xHi over the band, both boxes and the number), this session's box with a value per shot (index = ordinal − 1)
// for its marks, and the history box (no shot marks: its swings are other sessions'). `pool`: the ordinals the core's
// session box was taken over (its outliers are indices into it). `shotView`: the dot is this shot's number.
U::AddressBlock addressOf(const std::vector<PostureAbsoluteView> &rows, const QStringList &lines, const PostureEnv &env,
                          const std::vector<PostureShotInput> &inputs, const std::vector<int> &pool, bool shotView,
                          const QString &many)
{
    int maxOrd = 0;
    for (const PostureShotInput &s : inputs) maxOrd = std::max(maxOrd, s.ordinal);
    U::AddressBlock block;
    for (const PostureAbsoluteView &a : rows) {
        size_t spec = env.absolutes.size();
        for (size_t i = 0; i < env.absolutes.size(); ++i) if (env.absolutes[i].measureId == a.measureId) spec = i;
        const auto fmt = [u = a.unit](double v) { return postureAddressFigure(v, u); };
        U::Box box = boxGadget(a.box, fmt);
        box.outliers.clear();
        for (int i : a.box.outliers) box.outliers.append(i >= 0 && size_t(i) < pool.size() ? pool[size_t(i)] : i + 1);
        QList<double> values;
        for (int k = 0; k < maxOrd; ++k) values.append(nanv());
        for (const PostureShotInput &s : inputs)
            if (s.ordinal >= 1 && s.ordinal <= maxOrd && spec < s.absolutes.size() && std::find(pool.begin(), pool.end(), s.ordinal) != pool.end())
                values[s.ordinal - 1] = fin(s.absolutes[spec]) ? s.absolutes[spec] : nanv();
        QVariant hist = nullv();
        if (a.history.n > 0) {
            U::Box hb = boxGadget(a.history, fmt);
            hb.outliers.clear();   // other sessions' swings: no marks here
            hist = QVariant::fromValue(hb);
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
        U::AddressRow r;
        r.measureId = a.measureId;
        r.label = a.name;
        r.measureLabel = a.label;
        r.unit = a.unit;
        r.camera = postureCameraWords(a.camera);
        r.cameraKey = a.camera == PostureCamera::FaceOn ? QStringLiteral("faceOn") : QStringLiteral("dtl");
        r.metricKey = a.metricKey;
        r.hueKey = a.metricKey;
        r.value = nv(a.value);
        r.text = a.text;
        r.valueWord = shotView ? QStringLiteral("this shot") : QStringLiteral("median");
        r.banded = banded;
        r.bandLo = banded ? nv(a.band.lo) : nullv();
        r.bandHi = banded ? nv(a.band.hi) : nullv();
        r.bandMu = banded ? nv(a.band.mu) : nullv();
        r.bandText = a.bandText;
        r.bandSource = banded ? a.band.source : QString();
        r.bandContext = banded ? a.band.contextId : QString();
        r.bandInherited = banded && a.band.inherited;
        r.box = box;
        r.boxN = a.box.n;
        r.boxText = a.boxText;
        r.values = values;
        r.dot = shotView ? nv(a.value) : nullv();
        r.history = hist;
        r.historyN = a.history.n;
        r.historySessions = a.historySessions;
        r.hasHistory = a.history.n > 0;
        r.historyText = a.historyText;
        r.historyWhy = a.historyWhy;
        r.warned = a.warned;
        r.warn = a.warned ? a.warn : QString();
        r.warnShort = a.warned ? a.warnShort : QString();
        r.xLo = sp.lo;
        r.xHi = sp.hi;
        r.trust = a.trust;
        block.rows.append(r);
    }
    const QString one = many.endsWith(QLatin1Char('s')) ? many.chopped(1) : many;
    block.available = !block.rows.isEmpty();
    block.title = QStringLiteral("At address");
    block.sub = shotView ? QStringLiteral("this %1's number against the good-player band, this session and your earlier sessions").arg(one)
                         : QStringLiteral("this session against the good-player band, and your earlier sessions");
    block.legend = shotView
        ? QStringLiteral("Shaded: the good-player band. Upper box: this session, the dot this %1. Lower box: your earlier sessions from this set-up.").arg(one)
        : QStringLiteral("Shaded: the good-player band. Upper box: this session. Lower box: your earlier sessions from this set-up.");
    block.lines = lines;
    return block;
}

// ── the golfer's earlier sessions from the same set-up (§7.4.2) ──

// THIS session's cameras, from the streams of the first of its shots whose document records them (the one rule:
// session_day_readings_rules.h sessionCamerasOf), read through the snapshot's own door (SnapshotShotReader: the
// session's files are its own, P11), shot by shot in the snapshot's order. `known` false when none was read, and with
// no door (a test's hand-made snapshot). The earlier sessions' cameras are their summaries'.
pa::SessionCameras camerasOf(const SessionSnapshot &snap)
{
    if (!snap.reader) return {};
    for (const auto &s : snap.shots) {
        if (!s) continue;
        const SnapshotShotRead r = snap.reader->analysis(s->swingDir);
        if (r.root.isEmpty()) continue;
        const pa::SessionCameras c = pa::sessionCamerasOf(r.root);
        if (c.known) return c;
    }
    return {};
}

const pa::SessionCameraView &viewOf(const pa::SessionCameras &&, int) = delete;   // a temporary would leave the result dangling (#247, #248)
const pa::SessionCameraView &viewOf(const pa::SessionCameras &c, int cam)
{
    return cam == int(PostureCamera::Dtl) ? c.dtl : c.faceOn;
}

} // namespace

// THE HISTORY (refactor 5.5d; DEC-74): every EARLIER session of the golfer — earlier by SessionIdentity's place, as the
// athlete level places them (an earlier local day, or the same day and a lower n-of-day), never by folder-name order
// (F-06); a session with no day has no history — from ITS SUMMARY's day readings, never its files: per swing the
// measure's graded reading as its own SessionDiagnostics wrote it (`dayReadings.address`, off the rows its diagnostics
// file holds), kept per view when its camera on that view is this session's (`dayReadings.cameras`, by serial; a serial
// not recorded on either side keeps it, counted as unrecorded). An earlier session not summed up yet, or a newer build's,
// is counted and said (notSummedUp, newer).
PostureAddressHistory postureHistoryFrom(const SessionNeighbours &n, const pa::SessionCameras &mine, const PostureEnv &env)
{
    PostureAddressHistory h;
    h.looked = true;
    for (int c = 0; c < 2; ++c) {
        h.views[size_t(c)].known = mine.known;
        h.views[size_t(c)].present = viewOf(mine, c).present;
        h.views[size_t(c)].serial = viewOf(mine, c).serial;
    }
    if (!n.known || !n.self.day.isValid()) return h;
    for (const NeighbourSession &o : n.others) {   // oldest first
        if (!o.place.day.isValid() || !pa::sessionPlaceBefore(o.place, n.self)) continue;
        if (o.state == NeighbourSession::State::NotYet) { ++h.notSummedUp; continue; }
        if (o.state == NeighbourSession::State::Newer) { ++h.newer; continue; }
        QHash<QString, std::vector<double>> got;
        std::array<int, 2> swingsWith{};
        // A swing with a data warning COUNTS, as it does in this session's box (the session's shots are boxed
        // whatever their warning): the two medians side by side are one rule. How many are in is said once
        // (addressLines). One swing read on both views is one swing.
        std::vector<std::array<bool, 2>> warnedWith;
        for (const pa::ShotAddressReading &shot : o.readings.address) {
            std::array<bool, 2> any{};
            for (const PostureAbsoluteSpec &sp : env.absolutes) {
                const auto v = shot.values.constFind(sp.measureId);
                if (v == shot.values.constEnd() || !fin(*v)) continue;
                got[sp.measureId].push_back(*v);
                any[size_t(int(sp.camera))] = true;
            }
            for (int c = 0; c < 2; ++c) if (any[size_t(c)]) ++swingsWith[size_t(c)];
            if (shot.dataWarning && (any[0] || any[1])) warnedWith.push_back(any);
        }
        if (got.isEmpty()) continue;
        const pa::SessionCameras &theirs = o.readings.cameras;
        std::array<bool, 2> keptView{};
        for (int c = 0; c < 2; ++c) {
            if (swingsWith[size_t(c)] == 0) continue;
            PostureAddressHistory::View &v = h.views[size_t(c)];
            const QString &a = viewOf(mine, c).serial, &b = viewOf(theirs, c).serial;
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

namespace {

// This session's cameras through the worker's cache (once read, kept for the session folder).
const pa::SessionCameras &camerasHeld(ChartsPosturePresenter::Worker &w, const SessionSnapshot &snap)
{
    const QString dir = cleanDir(snap.sessionDir);
    if (w.camerasDir != dir || !w.cameras.known) {
        w.cameras = dir.isEmpty() ? pa::SessionCameras{} : camerasOf(snap);
        w.camerasDir = dir;
    }
    return w.cameras;
}

U::Trust trustOf(const PostureTrust &t)
{
    U::Trust out;
    out.lines = t.lines;
    out.cameras = t.cameras;
    out.absent = t.absent;
    out.warnings = t.warnings;
    out.bandSources = t.bandSources;
    out.trunkDatum = t.trunkDatum;
    out.rigRule = t.rigRule;
    return out;
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

U::Finding findingOf(const PostureEnv &env, const SessionSnapshot *snap, const ShotSeries *s, const QString &id,
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
    ChartShotFindings::Value val;
    if (s) val = s->diagnostics.values.value(id);
    const QString unit = unitOfCondition(env, s, id);
    const bool figure = !working && rowUnit(env, s, id, unit);
    const QString tag = seekPos >= 0 ? posturePositionTag(seekPos) : QString();
    const qint64 seek = usAt(us, seekPos);
    U::Finding out;
    out.id = id;
    out.name = working ? W::cap1(well) : label;
    out.kind = kind;
    out.departure = dep ? dep->id : QString();
    out.departureName = dep ? dep->name : QString();
    out.severity = severity;
    out.severityWord = working ? QStringLiteral("working") : severity;
    out.tier = tier;
    out.positions = tagsOf(positions);
    out.positionsText = positions.empty() ? QString()
                      : positions.size() == 1 ? QStringLiteral("at ") + posturePositionTag(positions.front())
                                              : rangeText(positions);
    out.tag = tag;
    out.seekUs = seek;
    out.seekText = tag.isEmpty() || seek < 0 ? QString() : QStringLiteral("Go to ") + tag;
    out.value = figure ? nv(val.value) : nullv();
    out.valueText = figure ? W::figUnit(val.value, unit) : QString();
    out.z = nv(val.z);
    out.explanation = working ? QString() : (why.isEmpty() ? W::cap1(golfer) : why);
    // Working well says its golferWell line only: golferWhy describes the FAULT
    out.protect = QString();
    return out;
}

// Findings: the fired conditions that name a departure, plus trail_knee_straighten and loss_of_posture;
// in the session's WORK-ON ORDER (roots before symptoms: SessionSnapshot::workOnOrder, the work-on list's
// own reduction — session_posture.h decision 9), then the session's other patterns, then by how far out
// the ledger graded them. Working well: per axis, the first posture condition graded clean, inside its
// band, with golfer words — none where the axis fired.
void ledgersOf(const PostureEnv &env, const SessionSnapshot *snap, const ShotSeries *s, const PostureShotInput &in,
               const PostureShotView &v, const std::array<qint64, kPos> &us, QList<U::Finding> *findings, QList<U::Finding> *working)
{
    if (!in.graded) return;
    const std::vector<PostureDepartureSpec> specs = postureDepartureSpecs(env);
    const QStringList *order = snap ? &snap->workOnOrder : nullptr;
    struct F { U::Finding m; int rank; bool pattern; double z; };
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
        fs.push_back({ findingOf(env, snap, s, id, kind, dep, positions, seek, us), postureWorkOnRank(order, id),
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
        working->append(findingOf(env, snap, s, speak, QStringLiteral("working"), nullptr, positions, at, us));
    }
}

// ── the standfirsts ─────────────────────────────────────────────────────────────────────────

QList<U::Sentence> standfirstsOf(const PostureShotView &v, const std::array<qint64, kPos> &us)
{
    QList<U::Sentence> out;
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
                if (r.row == PostureRow::SideBend && p != 6) continue;   // read at impact alone: no "side bend -" at every other position
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
        out.append(sentenceOf(st));
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

// ── consistency (session_posture.h decision 6; Mark's rulings R2b, R7, R11) ─────────────────────
//
// Each (row, position) from P2 on is graded per club: the club's swings' change from address there
// (postureGradedValues: the match's readings, no ⚠), against the band the profile draws — its σ, (σLo + σHi)/2, as
// the grader's `variabilityScale`, so the basis reads "of scale". A cell with no band is ungraded: no badge, and it
// stays out of the ranking. No propagated σ is known for a posture cell, so none is passed (no measurement-limited
// test). The grade words are the vocabularies' (QML); this file spells none.

namespace pv = pinpoint::variability;

// One club's grade at every cell, [row][pos]; `has` false where the cell is ungraded (no band, or no reading).
struct PostureClubGrades {
    QString club;
    int     shots = 0;    // the club's shots carrying a posture reading: the order the clubs are named in
    std::array<std::array<pv::VariabilityResult, kPos>, kPostureRows> result{};
    std::array<std::array<bool, kPos>, kPostureRows> has{};
};

std::vector<PostureClubGrades> postureGrades(const std::vector<PostureShotInput> &inputs, const PostureSessionView &sv)
{
    std::vector<PostureClubGrades> out;
    // the clubs, most shots first, then by label (as perClubInputs orders a cell's windows)
    std::map<QString, int> count;
    for (const PostureShotInput &s : inputs) if (hasPosture(s)) ++count[s.club];
    for (const auto &[club, n] : count) { PostureClubGrades g; g.club = club; g.shots = n; out.push_back(g); }
    std::stable_sort(out.begin(), out.end(), [](const PostureClubGrades &a, const PostureClubGrades &b) { return a.shots > b.shots; });
    if (out.empty()) return out;
    const auto grader = pv::makeVariabilityGrader(pv::Basis::Scalar);
    for (int r = 0; r < kPostureRows; ++r)
        for (int p = 1; p < kPos; ++p) {   // P1 is address itself: the change from it is 0
            const double sigma = sv.rows[size_t(r)].view.cells[size_t(p)].band.sigma();
            if (!fin(sigma)) continue;
            const std::vector<double> xs = postureGradedValues(inputs, PostureRow(r), p);
            std::vector<VariabilityClubValue> swings;
            for (size_t i = 0; i < inputs.size(); ++i)
                if (hasPosture(inputs[i])) swings.push_back({ inputs[i].club, xs[i] });
            pv::VariabilityInput base;
            base.scaleType = pv::ScaleType::Interval;
            base.variabilityScale = sigma;
            for (const VariabilityClubWindow &w : perClubInputs(swings, base))
                for (PostureClubGrades &g : out)
                    if (g.club == w.club) {
                        g.result[size_t(r)][size_t(p)] = grader->grade(w.input);
                        g.has[size_t(r)][size_t(p)] = g.result[size_t(r)][size_t(p)].graded;
                    }
        }
    return out;
}

pv::VariabilityResultView postureResultOf(const pv::VariabilityResult &res, PostureRow r)
{
    return pv::VariabilityResultView::fromResult(res, specOf(r).unit, specOf(r).decimals);
}

// A club's cells on one row are all "Not enough swings": the row names the club once, with that grade.
bool postureRowTooFew(const PostureClubGrades &g, int r)
{
    bool any = false;
    for (int p = 1; p < kPos; ++p) {
        if (!g.has[size_t(r)][size_t(p)]) continue;
        if (g.result[size_t(r)][size_t(p)].grade != 0) return false;
        any = true;
    }
    return any;
}

// ROW.grades: per club, the graded cells (or the one grade-0 result for a club too few to grade).
QList<U::Grade> postureRowGrades(const std::vector<PostureClubGrades> &grades, int r)
{
    QList<U::Grade> out;
    const bool named = grades.size() > 1;
    for (const PostureClubGrades &g : grades) {
        QList<U::GradeCell> cells;
        for (int p = 1; p < kPos; ++p) {
            if (!g.has[size_t(r)][size_t(p)]) continue;
            cells.append(U::GradeCell{ p, posturePositionTag(p), postureResultOf(g.result[size_t(r)][size_t(p)], PostureRow(r)) });
        }
        if (cells.isEmpty()) continue;
        U::Grade gr;
        gr.club = g.club;
        gr.named = named;
        gr.tooFew = postureRowTooFew(g, r);
        gr.result = gr.tooFew ? QVariant::fromValue(cells.first().result) : nullv();
        if (!gr.tooFew) gr.cells = cells;
        out.append(gr);
    }
    return out;
}

// ROW.rowGrades (Mark's ruling R20): per club, ONE grade for the row — the cell rule (R1, cellGrade: the loosest third)
// over the row's graded positions for that club, as a plain map (toMap) so it travels to paper. A club with no graded
// position on the row has no entry. The report's Posture and Wrist pages draw it beside the row's name; the Charts
// screens keep ROW.grades, the badge per position.
QList<U::RowGrade> postureRowCellGrades(const std::vector<PostureClubGrades> &grades, int r)
{
    QList<U::RowGrade> out;
    const bool named = grades.size() > 1;
    for (const PostureClubGrades &g : grades) {
        std::vector<pv::VariabilityResult> readings;
        for (int p = 1; p < kPos; ++p)
            if (g.has[size_t(r)][size_t(p)]) readings.push_back(g.result[size_t(r)][size_t(p)]);
        if (readings.empty()) continue;
        const pv::CellGrade cell = pv::cellGrade(readings);
        if (!cell.graded) continue;
        out.append(U::RowGrade{ g.club, named, pv::VariabilityResultView::fromCellGrade(cell) });
    }
    return out;
}

// consistency.lines: per club, its most and least consistent cell from P2 on, ranked by ρ (graded 1–5 only).
U::Consistency postureConsistency(const std::vector<PostureClubGrades> &grades)
{
    U::Consistency out;
    const bool named = grades.size() > 1;
    for (const PostureClubGrades &g : grades) {
        struct Ranked { int r, p; double rho; };
        std::vector<Ranked> ranked;
        QVariant zero;
        for (int r = 0; r < kPostureRows; ++r)
            for (int p = 1; p < kPos; ++p) {
                if (!g.has[size_t(r)][size_t(p)]) continue;
                const pv::VariabilityResult &res = g.result[size_t(r)][size_t(p)];
                if (res.grade >= 1 && fin(res.rho)) ranked.push_back({ r, p, res.rho });
                else if (res.grade == 0 && !zero.isValid()) zero = QVariant::fromValue(postureResultOf(res, PostureRow(r)));
            }
        U::ConsistencyLine line;   // most and least null, their texts "", until ranked
        line.club = g.club;
        line.named = named;
        if (ranked.empty()) {
            if (!zero.isValid()) continue;   // nothing graded for this club: no line
            line.tooFew = true;
            line.result = zero;
            out.lines.append(line);
            continue;
        }
        std::stable_sort(ranked.begin(), ranked.end(), [](const Ranked &a, const Ranked &b) { return a.rho < b.rho; });
        const Ranked &lo = ranked.front(), &hi = ranked.back();
        const auto where = [](const Ranked &x) {
            return QStringLiteral("%1 at %2").arg(postureRowSpec(PostureRow(x.r)).noun, posturePositionTag(x.p));
        };
        line.tooFew = false;
        line.mostText = QStringLiteral("Most: ") + where(lo);
        line.most = QVariant::fromValue(postureResultOf(g.result[size_t(lo.r)][size_t(lo.p)], PostureRow(lo.r)));
        const bool two = ranked.size() >= 2;
        line.leastText = two ? QStringLiteral("Least: ") + where(hi) : QString();
        line.least = two ? QVariant::fromValue(postureResultOf(g.result[size_t(hi.r)][size_t(hi.p)], PostureRow(hi.r))) : nullv();
        out.lines.append(line);
    }
    return out;
}

// `picked` may be a shot outside the snapshot (its own detail): then nothing in the strip is picked. `noun`:
// what the words count, plural ("shots" on Charts, "swings" in the report) — the core's sentences take the same.
U::Session sessionOf(const PostureEnv &env, const std::vector<PostureShotInput> &inputs, const PostureSessionView &sv,
                     int patternMode, int pickedIndex, const PostureShotInput *picked, const std::array<qint64, kPos> &us,
                     const QString &noun = QStringLiteral("shots"))
{
    U::Session out;
    const QString many = noun, one = noun.endsWith(QLatin1Char('s')) ? noun.chopped(1) : noun;
    const int n = int(inputs.size());
    out.n = n;
    out.shots = sv.shots;
    out.isSmall = sv.shots < 10;
    out.available = sv.available;
    out.fromSession = sv.fromSession;
    out.kicker = QStringLiteral("%1 %2").arg(n).arg(n == 1 ? one : many) + clubsWords(inputs) + QStringLiteral("  ·  Posture");
    out.departuresEmpty = QStringLiteral("No %1 left the pattern's band.").arg(one);
    if (!sv.available) {
        // the rest stays at its defaults (no departures, an empty strip, no profile): every reader tests `available`
        const QString h = QStringLiteral("No %1 in this session carries a posture reading").arg(one);
        out.head = sentenceOf(h);
        out.headText = h;
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

    out.head = sentenceOf(sv.headline);
    out.headText = sv.headline;
    out.patternLine = sv.patternLine;
    out.pattern = patternOf(patternMode, sv.pattern, sv.patternName, sv.patternSource, sv.patternSourceWords,
                            sv.patternLine, sv.match, sv.match, sv.trail);

    // THE PROFILE: the medians against the session's pattern, a box per position, this shot ringed
    // consistency, per club against the band (decision 6, R7): each row's badges, and each box marked graded or not
    const std::vector<PostureClubGrades> grades = postureGrades(inputs, sv);
    for (int r = 0; r < kPostureRows; ++r) {
        const PostureSessionRow &sr = sv.rows[size_t(r)];
        std::array<double, kPos> pk = nanRow();
        if (picked) pk = picked->value[size_t(r)];
        U::Row rm = rowOf(sr.view, nanRow(), pk, us, &sr.boxes, pool, &inputs);
        for (int p = 0; p < rm.boxes.size(); ++p) {
            bool graded = false;
            for (const PostureClubGrades &g : grades) graded = graded || (p > 0 && p < kPos && g.has[size_t(r)][size_t(p)]);
            rm.boxes[p].graded = graded;
        }
        rm.grades = postureRowGrades(grades, r);
        rm.rowGrades = postureRowCellGrades(grades, r);
        out.profile.rows.append(rm);
    }
    out.profile.positions = positionsList(us);
    out.profile.sub = QStringLiteral("change from address  ·  a box per position, n %1%2  ·  under each: the median, and how wide the middle half is")
                          .arg(sv.shots).arg(pickedOrdinal >= 0 ? QStringLiteral(", this %1 ringed").arg(one) : QString());
    out.profile.caption = QStringLiteral("Shaded: the %1 band, where good players are at each position. The hairline: where a fault "
                                         "fires. A ring and a word: the session's median outside the band, and on which side. "
                                         "⚠: drawn, not matched, never ringed.").arg(sv.patternName);

    // THE PATTERN BY SHOT
    for (const PosturePatternMark &m : sv.strip) {
        const QString word = !m.ok ? QStringLiteral("-") : m.between ? QStringLiteral("between") : posturePatternName(m.nearest);
        U::StripMark mk;
        mk.ordinal = m.ordinal;
        mk.shotId = idOf(m.ordinal);
        mk.swingDir = dirOf(m.ordinal);
        mk.ok = m.ok;
        mk.nearest = m.ok ? QVariant(int(m.nearest)) : nullv();
        mk.nearestName = m.ok ? posturePatternName(m.nearest) : QString();
        mk.between = m.between;
        mk.differs = m.differs;
        mk.picked = m.ordinal == pickedOrdinal;
        mk.dLevel = nv(m.dLevel);
        mk.dSquatPush = nv(m.dSquatPush);
        mk.word = word;
        mk.label = QStringLiteral("%1 %2: %3").arg(one).arg(W::shotNumber(idOf(m.ordinal), m.ordinal)).arg(word);
        out.strip.marks.append(mk);
    }
    out.strip.onPattern = sv.onPattern;
    out.strip.line = QStringLiteral("%1 on %2 of %3 %4").arg(sv.patternName).arg(sv.onPattern).arg(sv.shots).arg(many);
    out.strip.legend = QStringLiteral("each mark is a %1's nearest pattern; ringed: not the session's (%2), or between the two").arg(one, sv.patternName);

    // THE DEPARTURES, across shots
    for (const PostureDepartureCount &d : sv.departureCounts) {
        U::SessionDeparture dp;
        for (int o : d.ordinals) {
            dp.marks.append(U::DepartureMark{ o, dirOf(o) });
            dp.ordinals.append(o);
        }
        const QString count = QStringLiteral("%1 of %2 %3").arg(d.shots).arg(sv.shots).arg(many);
        const QString first = d.firstPos >= 0 ? posturePositionTag(d.firstPos) : QString();
        dp.id = d.id;
        dp.name = d.name;
        dp.conditionIds = d.conditionIds;
        dp.shots = d.shots;
        dp.countText = count;
        dp.firstTag = first;
        dp.line = d.name + (first.isEmpty() ? QString() : QStringLiteral(", from ") + first) + QStringLiteral(": ") + count;
        dp.detail = (first.isEmpty() ? QString() : QStringLiteral("from ") + first + QStringLiteral("  ·  ")) + count;
        out.departures.append(dp);
    }
    out.consistency = postureConsistency(grades);
    out.whatMoved = whatMovedOf(sv.top, sv.impact, us);
    out.address = addressOf(sv.absolutes, sv.addressLines, env, inputs, pool, false, many);
    out.absolutes = out.address.rows;   // the same rows (ABSOLUTE = ADDRESS)
    out.trust = trustOf(sv.trust);
    return out;
}

// ── This shot ───────────────────────────────────────────────────────────────────────────────

// ── the picture strip (posture_chart_design.md §5.4, stage 5) ─────────────────────────────────

// The swing's document → the picture's inputs, read through the snapshot's own door (SnapshotShotReader: the session's
// files are its own, P11) and cached by the document's stamp (a re-analysed swing is read again). No door (a test's
// hand-made snapshot) reads as no document. A pinned entry (the tests' seam) is kept whatever the session says.
const PictureDoc &pictureDocOf(ChartsPosturePresenter::Worker &w, const SnapshotShotReader *reader, const QString &swingDir)
{
    const QString dir = cleanDir(swingDir);
    auto it = w.pictures.find(dir);
    if (it != w.pictures.end() && it->pinned) return it->doc;
    const SnapshotShotRead info = reader ? reader->stamp(dir) : SnapshotShotRead{};
    if (it != w.pictures.end() && it->size == info.size && it->mtimeMs == info.mtimeMs) {
        w.pictureOrder.removeAll(dir);
        w.pictureOrder << dir;
        return it->doc;
    }
    ChartsPosturePresenter::Worker::PictureEntry e;
    e.size = info.size;
    e.mtimeMs = info.mtimeMs;
    if (!info.exists) {
        e.doc.swingDir = dir;
        e.doc.why = QStringLiteral("the swing's document is not on disk");
    } else {
        const SnapshotShotRead read = reader->document(dir);
        const QString &err = read.error;
        e.doc = postureDocFrom(read.root, dir, QStringLiteral("%1-%2").arg(info.size).arg(info.mtimeMs));
        if (read.root.isEmpty()) e.doc.why = QStringLiteral("the swing's document could not be read%1").arg(err.isEmpty() ? QString() : QStringLiteral(": ") + err);
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

// The stills a picture still needs made, as the still maker reads them (maps: main.cpp's maker).
QVariantList pendingStillsOf(const U::Picture &picture)
{
    QVariantList out;
    for (const U::PictureGroup &g : picture.groups)
        for (const U::PictureFrame &f : g.frames) {
            if (f.still.metaType() != QMetaType::fromType<U::PictureStill>()) continue;   // null: nothing to make
            const U::PictureStill still = f.still.value<U::PictureStill>();
            if (f.src.isEmpty() && f.imageWhy.isEmpty() && !still.outPath.isEmpty())
                out.append(chartsGadgetMap(still));
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
    U::ThisShot out;
    R.posUs = c.pUs;
    const bool has = c.shot && c.view.available;
    out.available = has;
    if (!has) {
        // the rest stays at its defaults: every reader tests `available`
        out.empty = (c.shot || !c.in.pickedSwingDir.trimmed().isEmpty()
                         ? QStringLiteral("No posture readings for this swing.") : QStringLiteral("Select a swing to review."))
                    + emptyTail();
        R.thisShot = out;
        return;
    }
    const ShotSeries &s = *c.shot;
    const PostureShotView &v = c.view;
    const bool inSession = c.index >= 0;
    out.empty = QString();
    out.ordinal = s.ordinal;
    out.shotId = s.shotId;
    out.swingDir = s.swingDir;
    out.inSession = inSession;
    const QString club = s.club.toLower();
    out.kicker = (inSession ? W::shotOfText(s.shotId, s.ordinal, c.n) : QStringLiteral("This swing"))
                 + (club.isEmpty() ? QString() : QStringLiteral("  ·  ") + club) + QStringLiteral("  ·  Posture");
    out.graded = c.pick.graded;
    out.head = sentenceOf(v.headline);
    out.headText = v.headline;
    const PostureMatch &used = v.patternSource == QLatin1String("session") ? v.sessionMatch : v.match;
    out.pattern = patternOf(c.in.pattern, v.pattern, v.patternName, v.patternSource, v.patternSourceWords,
                            v.patternLine, used, v.match, v.trail);
    out.patternLine = v.patternLine;

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

    for (int r = 0; r < kPostureRows; ++r)
        out.profile.rows.append(rowOf(v.rows[size_t(r)], ghost[size_t(r)], c.pick.value[size_t(r)], c.pUs, nullptr, {}));
    QString sub = QStringLiteral("change from address");
    if (!ghostWord.isEmpty()) sub += QStringLiteral("  ·  dashed: ") + ghostWord;
    out.profile.positions = positionsList(c.pUs);
    out.profile.ghostWord = ghostWord;
    out.profile.ghostRefusal = ghostRefusal;
    out.profile.sub = sub;
    out.profile.caption = QStringLiteral("Shaded: the %1 band, where good players are at each position. The hairline: where a "
                                         "fault fires. A ring and a word: outside the band, and on which side. ⚠: drawn, not "
                                         "matched, never ringed.").arg(v.patternName);

    for (const PostureExit &e : v.exits) out.exits.append(exitOf(e, c.pUs));
    for (const PostureDeparture &d : v.departures) out.departures.append(departureOf(d, c.pUs));
    out.whatMoved = whatMovedOf(v.top, v.impact, c.pUs);

    ledgersOf(c.env, c.snap.get(), c.shot.get(), c.pick, v, c.pUs, &out.findings, &out.workingWell);
    out.findingsEmpty = c.pick.graded ? QStringLiteral("No posture fault fired on this swing.")
                                      : QStringLiteral("Diagnostics have not graded this swing yet.");
    out.workingWellEmpty = c.pick.graded ? QStringLiteral("Nothing in the posture stood out as working well on this swing.")
                                         : QStringLiteral("Diagnostics have not graded this swing yet.");

    // the absolutes' boxes are the session's pool (the shots with a reading), else this shot alone
    std::vector<int> pool;
    if (c.sess.available) {
        for (const PostureShotInput &s2 : c.inputs) if (hasPosture(s2)) pool.push_back(s2.ordinal);
    } else {
        pool.push_back(s.ordinal);
    }
    out.address = addressOf(v.absolutes, v.addressLines, c.env, c.sess.available ? c.inputs : std::vector<PostureShotInput>{ c.pick },
                            pool, true, QStringLiteral("shots"));
    out.absolutes = out.address.rows;   // the same rows (ABSOLUTE = ADDRESS)
    out.trust = trustOf(v.trust);

    // THE PICTURE: this swing's frames at address, the top and impact, the frozen coach lines, the bands at impact
    {
        PictureOptions po;
        po.stillDir = pictureStillDir();
        po.ordinal = s.ordinal;
        {
            QMutexLocker sl(&c.w.stillMutex);
            po.stillErrors = c.w.stillErrors;
        }
        out.picture = posturePicture(pictureDocOf(c.w, c.snap->reader.get(), s.swingDir), pictureBandsOf(c.env, v.pattern, v.patternName), po);
        R.pendingStills = pendingStillsOf(out.picture);
    }
    R.standfirsts = standfirstsOf(v, c.pUs);
    out.standfirsts = R.standfirsts;
    R.thisShot = out;
}

// ── the choices ─────────────────────────────────────────────────────────────────────────────

void buildChoices(Ctx &c, ChartsPosturePresenter::Result &R)
{
    U::Choices out;
    const QString scope = c.in.scope == QLatin1String("session") ? QStringLiteral("session") : QStringLiteral("shot");
    out.scope = scope;
    out.scopes = { U::ScopeChoice{ QStringLiteral("shot"), QStringLiteral("This shot"), scope == QLatin1String("shot") },
                   U::ScopeChoice{ QStringLiteral("session"), QStringLiteral("Session"), scope == QLatin1String("session") } };

    const QString ag = c.in.against;
    const int pi = c.index >= 0 ? c.index - 1 : c.n - 1;
    const bool prevOk = pi >= 0 && pi < c.n && hasPosture(c.inputs[size_t(pi)]);
    const bool refOk = c.refOk && !c.refIsPicked && hasPosture(c.refIn);
    const QString refWhy = !c.refSet ? QStringLiteral("no reference swing is set: ★ marks one")
                         : !c.refOk ? c.refWhy
                         : c.refIsPicked ? QStringLiteral("this swing is your reference")
                         : QStringLiteral("the reference swing carries no posture reading");
    QList<U::AgainstOption> ags;
    const auto addA = [&](const QString &id, const QString &label, bool enabled, const QString &why) {
        ags.append(U::AgainstOption{ id, label, ag == id, enabled, enabled ? QString() : why });
    };
    addA(QStringLiteral("address"), QStringLiteral("address"), true, QString());
    addA(QStringLiteral("previous"), QStringLiteral("previous"), prevOk, QStringLiteral("the swing before this one has no posture reading"));
    addA(QStringLiteral("reference"), QStringLiteral("reference ★"), refOk, refWhy);
    addA(QStringLiteral("median"), QStringLiteral("session median"), c.ready >= 3,
         QStringLiteral("three shots with a posture reading are needed; this session has %1").arg(W::countWord(c.ready)));
    out.against = ag;
    out.againstOptions = ags;

    static const char *const names[] = { "Auto", "Level", "Squat and push" };
    for (int p = -1; p <= 1; ++p)
        out.patterns.append(U::PatternChoice{ p, QString::fromLatin1(names[p + 1]), c.in.pattern == p });
    out.pattern = c.in.pattern;
    const bool resolvedKnown = c.view.available || c.sess.available;
    const PosturePattern resolved = c.view.available ? c.view.pattern : c.sess.pattern;
    out.patternResolved = resolvedKnown ? QVariant(int(resolved)) : nullv();
    const QString after = c.in.pattern < 0 && resolvedKnown ? QStringLiteral("→ ") + posturePatternName(resolved) : QString();
    out.patternAfter = after;

    out.reference.set = c.refSet;
    out.reference.swingDir = c.refDir;
    out.reference.label = c.refLabel;
    out.reference.isThisShot = c.refIsPicked;
    out.reference.linkText = c.refIsPicked ? QStringLiteral("★ this swing is your reference") : QStringLiteral("★ set this swing as your reference");
    out.reference.canSet = bool(c.shot);

    QString agLabel = QStringLiteral("address");
    for (const U::AgainstOption &a : std::as_const(ags)) if (a.current) agLabel = a.label;
    out.summary = QStringLiteral("Pattern %1%2  ·  against %3")
                      .arg(QString::fromLatin1(names[c.in.pattern + 1]), after.isEmpty() ? QString() : QStringLiteral(" ") + after, agLabel);
    // the series whose curve on show adds the Posture section (the rows' own, and the head's rise)
    for (const PostureRowSpec &sp : postureRowSpecs()) out.onShowKeys << sp.metricKey;
    out.onShowKeys << QStringLiteral("headRise");
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

// THE REPORT'S PAYLOAD AS IT HAS ALWAYS BEEN (until 5.6 types the report): the typed session as a map (chartsGadgetMap),
// with what the reflection would change put back, so the page, the still maker and the parity tools read the same map:
//   · each consistency result stays the `variabilityResult` value it was (a grade's, a graded position's, a consistency
//     line's; null where there is none), and a row grade its plain map (toMap: ρ null when there is none);
//   · a per-shot value list (a box cell's, an address row's) carries null, not NaN, for an unmeasured shot;
//   · the profile carries no `ghostWord` / `ghostRefusal` (This shot's only), and a box with no shot no `texts`.
QVariantList nullsForNaN(const QList<double> &values)
{
    QVariantList out;
    for (double v : values) out.append(fin(v) ? QVariant(v) : nullv());
    return out;
}
QVariantList reportAddressRows(const QVariantList &rows, const QList<U::AddressRow> &typed)
{
    QVariantList out = rows;
    for (int i = 0; i < out.size() && i < typed.size(); ++i) {
        QVariantMap r = out[i].toMap();
        r.insert(QStringLiteral("values"), nullsForNaN(typed.at(i).values));
        out[i] = r;
    }
    return out;
}
QVariantMap reportMapOf(const U::Session &s)
{
    QVariantMap out = chartsGadgetMap(s);
    QVariantMap prof = out.value(QStringLiteral("profile")).toMap();
    prof.remove(QStringLiteral("ghostWord"));
    prof.remove(QStringLiteral("ghostRefusal"));
    QVariantList rows = prof.value(QStringLiteral("rows")).toList();
    for (int r = 0; r < rows.size() && r < s.profile.rows.size(); ++r) {
        const U::Row &row = s.profile.rows.at(r);
        QVariantMap rm = rows[r].toMap();
        QVariantList bx = rm.value(QStringLiteral("boxes")).toList();
        for (int b = 0; b < bx.size() && b < row.boxes.size(); ++b) {
            QVariantMap bm = bx[b].toMap();
            bm.insert(QStringLiteral("values"), nullsForNaN(row.boxes.at(b).values));
            // a box with no shot carried no `texts` (box_map.h's BOX); the gadget's are "" there
            QVariantMap box = bm.value(QStringLiteral("box")).toMap();
            if (box.value(QStringLiteral("n")).toInt() <= 0 && box.remove(QStringLiteral("texts")) > 0) bm.insert(QStringLiteral("box"), box);
            bx[b] = bm;
        }
        rm.insert(QStringLiteral("boxes"), bx);
        QVariantList gs = rm.value(QStringLiteral("grades")).toList();
        for (int g = 0; g < gs.size() && g < row.grades.size(); ++g) {
            const U::Grade &grade = row.grades.at(g);
            QVariantMap gm = gs[g].toMap();
            gm.insert(QStringLiteral("result"), grade.result);
            QVariantList cs = gm.value(QStringLiteral("cells")).toList();
            for (int k = 0; k < cs.size() && k < grade.cells.size(); ++k) {
                QVariantMap cm = cs[k].toMap();
                cm.insert(QStringLiteral("result"), QVariant::fromValue(grade.cells.at(k).result));
                cs[k] = cm;
            }
            gm.insert(QStringLiteral("cells"), cs);
            gs[g] = gm;
        }
        rm.insert(QStringLiteral("grades"), gs);
        QVariantList rg = rm.value(QStringLiteral("rowGrades")).toList();
        for (int g = 0; g < rg.size() && g < row.rowGrades.size(); ++g) {
            QVariantMap gm = rg[g].toMap();
            gm.insert(QStringLiteral("result"), row.rowGrades.at(g).result.toMap());
            rg[g] = gm;
        }
        rm.insert(QStringLiteral("rowGrades"), rg);
        rows[r] = rm;
    }
    prof.insert(QStringLiteral("rows"), rows);
    out.insert(QStringLiteral("profile"), prof);
    QVariantMap cons = out.value(QStringLiteral("consistency")).toMap();
    QVariantList lines = cons.value(QStringLiteral("lines")).toList();
    for (int i = 0; i < lines.size() && i < s.consistency.lines.size(); ++i) {
        const U::ConsistencyLine &l = s.consistency.lines.at(i);
        QVariantMap lm = lines[i].toMap();
        lm.insert(QStringLiteral("result"), l.result);
        lm.insert(QStringLiteral("most"), l.most);
        lm.insert(QStringLiteral("least"), l.least);
        lines[i] = lm;
    }
    cons.insert(QStringLiteral("lines"), lines);
    out.insert(QStringLiteral("consistency"), cons);
    QVariantMap address = out.value(QStringLiteral("address")).toMap();
    address.insert(QStringLiteral("rows"), reportAddressRows(address.value(QStringLiteral("rows")).toList(), s.address.rows));
    out.insert(QStringLiteral("address"), address);
    out.insert(QStringLiteral("absolutes"), reportAddressRows(out.value(QStringLiteral("absolutes")).toList(), s.absolutes));
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
    // One snapshot for the whole pass, held here so a corridor save mid-job cannot change it under us.
    const std::shared_ptr<const PostureEnv> envHeld = postureSharedEnv();
    if (w.env != envHeld) {
        w.cache.clear();
        w.env = envHeld;
    }
    const PostureEnv &env = *envHeld;
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

    const PostureAddressHistory hist = postureHistoryFrom(in.neighbours, camerasHeld(w, *c.snap), env);
    c.sess = postureSession(c.inputs, env, in.pattern, &c.snap->tiers, &c.snap->workOnOrder, QStringLiteral("shots"), &hist);
    if (c.shot) c.view = postureShot(c.pick, env, in.pattern, c.sess.available ? &c.sess : nullptr, &c.snap->workOnOrder, &hist);
    c.pUs = positionUs(c.shot.get());

    R->available = c.ready > 0 || c.hasPick;
    buildThisShot(c, *R);
    // the session's positions: the picked shot's, else the first shot with a reading
    std::array<qint64, kPos> sessUs = c.pUs;
    if (!c.shot)
        for (int i = 0; i < c.n; ++i) if (hasPosture(c.inputs[size_t(i)])) { sessUs = positionUs(c.snap->shots[size_t(i)].get()); break; }
    R->session = sessionOf(env, c.inputs, c.sess, in.pattern, c.index, c.shot ? &c.pick : nullptr, sessUs);
    buildChoices(c, *R);
    return R;
}

PostureSessionReport postureSessionReport(const SessionSnapshot &snapshot, int patternMode, const QString &noun,
                                          const SessionNeighbours &neighbours)
{
    const std::shared_ptr<const PostureEnv> envHeld = postureSharedEnv();
    const PostureAddressHistory hist = postureHistoryFrom(neighbours, cleanDir(snapshot.sessionDir).isEmpty() ? pa::SessionCameras{}
                                                                                                              : camerasOf(snapshot),
                                                          *envHeld);
    return postureSessionReportOfHistory(snapshot, patternMode, noun, hist);
}

PostureSessionReport postureSessionReportOfHistory(const SessionSnapshot &snapshot, int patternMode, const QString &noun,
                                                   const PostureAddressHistory &hist)
{
    const int mode = patternMode < -1 || patternMode > 1 ? -1 : patternMode;
    const QString many = noun.trimmed().isEmpty() ? QStringLiteral("shots") : noun.trimmed();
    const std::shared_ptr<const PostureEnv> envHeld = postureSharedEnv();
    const PostureEnv &env = *envHeld;
    const std::vector<PostureShotInput> inputs = readInputs(nullptr, snapshot, env);
    const PostureSessionView sv = postureSession(inputs, env, mode, &snapshot.tiers, &snapshot.workOnOrder, many, &hist);
    std::array<qint64, kPos> us = positionUs(nullptr);
    for (size_t i = 0; i < inputs.size(); ++i) if (hasPosture(inputs[i])) { us = positionUs(snapshot.shots[i].get()); break; }
    PostureSessionReport out;
    out.session = sessionOf(env, inputs, sv, mode, -1, nullptr, us, many);
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
    // the swing's document through the snapshot's own door (P11); no door reads as no document
    const SnapshotShotReader *reader = snapshot.reader.get();
    const SnapshotShotRead info = reader ? reader->stamp(dir) : SnapshotShotRead{};
    const QJsonObject root = info.exists ? reader->document(dir).root : QJsonObject{};
    PictureDoc doc = postureDocFrom(root, dir, QStringLiteral("%1-%2").arg(info.size).arg(info.mtimeMs));
    if (root.isEmpty()) doc.why = info.exists ? QStringLiteral("the swing's document could not be read") : QStringLiteral("the swing's document is not on disk");
    PictureOptions po;
    po.ordinal = typical;
    po.maxPx = 1400;
    out.picture = posturePicture(doc, pictureBandsOf(env, sv.pattern, sv.patternName), po);
    const QString one = many.endsWith(QLatin1Char('s')) ? many.chopped(1) : many;
    out.picture.typical = QStringLiteral("A typical %1: %1 %2").arg(one).arg(typical);
    out.typicalWhy = std::isfinite(median)
        ? QStringLiteral("the %1 whose fit to %2 is nearest the session's median").arg(one, sv.patternName)
        : QStringLiteral("the first %1 with a posture reading").arg(one);
    out.hasPicture = true;
    return out;
}

QVariantMap reportPayloadOf(const PostureSessionReport &page)
{
    QVariantMap out = reportMapOf(page.session);
    if (!page.hasPicture) return out;
    QVariantMap pic = chartsGadgetMap(page.picture);
    pic.insert(QStringLiteral("typicalWhy"), page.typicalWhy);
    out.insert(QStringLiteral("picture"), pic);
    return out;
}


// ── the object ──────────────────────────────────────────────────────────────────────────────

ChartsPosturePresenter::ChartsPosturePresenter(QObject *parent)
    : QObject(parent), m_worker(std::make_shared<Worker>())
{
    schedule();
}

ChartsPosturePresenter::~ChartsPosturePresenter()
{
    // No wait: a job still running finishes into nothing (charts_work.h: the lane is cancelled, #173).
}

SeriesSource *ChartsPosturePresenter::model() const { return m_model.data(); }

void ChartsPosturePresenter::setModel(SeriesSource *m)
{
    if (m_model == m) return;
    m_model = m;
    rewireSources();
    emit inputsChanged();
    schedule();
}

SeriesSource *ChartsPosturePresenter::referenceModel() const { return m_referenceModel.data(); }

void ChartsPosturePresenter::setReferenceModel(SeriesSource *m)
{
    if (m_referenceModel == m) return;
    m_referenceModel = m;
    rewireSources();
    emit inputsChanged();
    schedule();
}

// What re-runs the job: the model's snapshot; the ★ swing's session's snapshot and busy (its ★ lands when that session
// has been read). The same series in both roles is heard once. Kept by handle, so a change of one role leaves the other.
void ChartsPosturePresenter::rewireSources()
{
    for (const QMetaObject::Connection &c : std::as_const(m_sourceWatch)) disconnect(c);
    m_sourceWatch.clear();
    if (m_model)
        m_sourceWatch << connect(m_model.data(), &SeriesSource::snapshotChanged, this, &ChartsPosturePresenter::schedule);
    if (m_referenceModel) {
        if (m_referenceModel != m_model)
            m_sourceWatch << connect(m_referenceModel.data(), &SeriesSource::snapshotChanged, this, &ChartsPosturePresenter::schedule);
        m_sourceWatch << connect(m_referenceModel.data(), &SeriesSource::busyChanged, this, &ChartsPosturePresenter::schedule);
    }
}

std::shared_ptr<const SessionSnapshot> ChartsPosturePresenter::currentSnapshot() const
{
    return chosenSnapshot(m_directSnapshot, m_model.data());   // P2: a direct snapshot wins
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
postureui::Choices ChartsPosturePresenter::choices() { flushIfSynchronous(); return m_result ? m_result->choices : postureui::Choices{}; }
postureui::ThisShot ChartsPosturePresenter::thisShot() { flushIfSynchronous(); return m_result ? m_result->thisShot : postureui::ThisShot{}; }
postureui::Session ChartsPosturePresenter::session() { flushIfSynchronous(); return m_result ? m_result->session : postureui::Session{}; }

// None (no result, no position): the empty sentence, which no view draws (PpChRoles.has), as the empty map was.
postureui::Sentence ChartsPosturePresenter::standfirst() const
{
    if (!m_result || m_selected < 0 || m_selected >= m_result->standfirsts.size()) return {};
    return m_result->standfirsts.at(m_selected);
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

void ChartsPosturePresenter::startJob()
{
    if (!m_sched.begin()) return;     // synchronous, or a job in flight: apply() re-runs a dirty presenter
    Inputs in = m_in;
    in.snapshot = currentSnapshot();
    in.reference = referencePickOf(m_in.referenceSwingDir, m_referenceModel.data(), in.snapshot, m_refMemo);
    // THE GOLFER'S EARLIER SESSIONS, from their summaries (refactor 5.5d): taken here, on the GUI thread, from the athlete
    // level, and handed to the job by value; heard again when that athlete level changes (a summary written).
    const QString dir = in.snapshot ? in.snapshot->sessionDir : QString();
    in.neighbours = neighboursOf(dir);
    watchHistory(dir);
    emit busyChanged();
    auto w = m_worker;
    askLane(m_pool, [w, in]() { return computePosture(*w, in); },
            [this](auto r) { apply(r); });
}

void ChartsPosturePresenter::watchHistory(const QString &sessionDir)
{
    if (sessionDir == m_watchedDir) return;
    for (const QMetaObject::Connection &c : std::as_const(m_historyWatch)) disconnect(c);
    m_watchedDir = sessionDir;
    m_historyWatch = sessionDir.isEmpty() ? QList<QMetaObject::Connection>{}
                                          : watchNeighbours(this, sessionDir, [this] { schedule(); });
}

void ChartsPosturePresenter::apply(std::shared_ptr<Result> r)
{
    const bool wasRunning = m_sched.land();
    m_result = std::move(r);
    emit resultsChanged();
    updateSelected(true);
    m_sched.afterLanding(wasRunning);
    makeStills();
}

void ChartsPosturePresenter::flushIfSynchronous()
{
    if (!m_sched.takeFlush()) return;
    Inputs in = m_in;
    in.snapshot = currentSnapshot();
    in.reference = referencePickOf(m_in.referenceSwingDir, m_referenceModel.data(), in.snapshot, m_refMemo);
    in.neighbours = neighboursOf(in.snapshot ? in.snapshot->sessionDir : QString());   // refactor 5.5d
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
    if (m_sched.synchronous() || m_stillsBusy || !m_result || m_result->pendingStills.isEmpty() || !stillMaker()) return;
    QVariantList todo;
    for (const QVariant &v : std::as_const(m_result->pendingStills)) {
        const QString out = v.toMap().value(QStringLiteral("outPath")).toString();
        if (!m_stillsAsked.contains(out)) { m_stillsAsked.insert(out); todo.append(v); }
    }
    if (todo.isEmpty()) return;
    m_stillsBusy = true;
    auto w = m_worker;
    const StillMaker maker = stillMaker();
    askLane(m_stillsLane, [w, todo, maker]() {
        const QHash<QString, QString> errs = maker(todo);
        {
            QMutexLocker sl(&w->stillMutex);
            for (auto it = errs.cbegin(); it != errs.cend(); ++it) if (!it.value().isEmpty()) w->stillErrors.insert(it.key(), it.value());
        }
        return true;
    }, [this](bool) {
        m_stillsBusy = false;
        schedule();
    });
}

void ChartsPosturePresenter::setStillMaker(StillMaker maker)
{
    QMutexLocker l(&stillMakerMutex());
    stillMakerRef() = std::move(maker);
}

void ChartsPosturePresenter::setPictureDocumentForTest(const QString &swingDir, const QJsonObject &root, const QString &source)
{
    const QString dir = cleanDir(swingDir);
    {
        QMutexLocker lk(&m_worker->mutex);
        if (source.trimmed().isEmpty()) {
            m_worker->pictures.remove(dir);
        } else {
            Worker::PictureEntry e;
            e.pinned = true;
            e.doc = postureDocFrom(root, dir, QStringLiteral("test"));
            if (root.isEmpty()) e.doc.why = QStringLiteral("the test's document %1 could not be read").arg(source);
            m_worker->pictures.insert(dir, std::move(e));
        }
    }
    schedule();
}

void ChartsPosturePresenter::setSynchronous(bool on)
{
    m_sched.setSynchronousFlag(on);
    if (on) {
        m_pool.waitForDone();
        QCoreApplication::processEvents();
        m_sched.markDirty();
    } else if (m_sched.dirty()) {
        schedule();
    }
}

bool ChartsPosturePresenter::waitForIdle(int msTimeout)
{
    return m_sched.waitForIdle(m_pool, msTimeout, [this] { flushIfSynchronous(); });
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

// moc for the typed results' gadgets (charts_posture_types.h has no .cpp of its own)
#include "moc_charts_posture_types.cpp"
