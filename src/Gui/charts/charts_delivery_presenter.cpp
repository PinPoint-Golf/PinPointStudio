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

#include "charts_delivery_presenter.h"

#include "charts_gadget.h"   // chartsGadgetMap: the report's payload and the still maker's requests
#include "delivery_picture.h"
#include "charts_words.h"
#include "session_delivery.h"
#include "session_series_extract.h"   // extractShot, folderShotId (refactor 5.5a)
#include "../../Stores/series_store.h"   // seriesSwingLabelOf: the ★ label
#include "../../DomainModel/rule_data.h"   // RuleData's snapshot: the pack the direction rule's boundaries come from
#include "../../Diagnostics/norms/context_tree.h"
#include "../../Diagnostics/norms/shot_direction_bounds.h"   // the one direction rule's boundaries (refactor 6.3b)

#include "../../Core/pp_debug.h"
#include "../../Stores/swing_store.h"
#include "../../Stores/swing_paths.h"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDate>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>
#include <QSet>
#include <QStandardPaths>

#include <algorithm>
#include <cmath>

// The value types the worker keeps. A NAMED namespace, not an anonymous one: Worker (a member type of a class with
// external linkage) holds them.
namespace pinpoint::charts::deliveryview {

// One shot's reading, keyed by the shot's shared_ptr: a re-analysed shot is a new pointer.
struct CacheEntry {
    std::weak_ptr<const ShotSeries> shot;
    DeliveryShotInput in;
};

} // namespace pinpoint::charts::deliveryview

namespace pinpoint::charts {

using namespace deliveryview;
namespace W = words;
namespace U = deliveryui;

// ── the result and the worker ───────────────────────────────────────────────────────────────

struct ChartsDeliveryPresenter::Result {
    bool available = false;
    U::Choices choices;
    U::ThisShot thisShot;
    U::Session session;
    U::PlotLayers plotLayers;
    QList<U::Sentence> standfirsts;                    // 7 Sentences (thisShot.standfirsts)
    std::array<qint64, kDeliveryPositions> posUs{};    // the picked shot's P µs, −1 none
    QVariantList pendingStills;                        // the picture's stills not made yet (thisShot.picture), as the maker reads them
};

struct ChartsDeliveryPresenter::Worker {
    QMutex mutex;                                      // held for a whole job
    QHash<const ShotSeries *, CacheEntry> cache;
    // the picture strip's documents (delivery_picture.h), by cleaned dir and document stamp; a few kept
    struct PictureEntry { qint64 size = -1, mtimeMs = -1; bool pinned = false; DeliveryPictureDoc doc; };
    QHash<QString, PictureEntry> pictures;
    QStringList pictureOrder;                          // least recent first
    // the stills the maker could not make: outPath → why (written on the GUI thread, read by a job)
    QMutex stillMutex;
    QHash<QString, QString> stillErrors;
};

namespace {

constexpr int kPos = kDeliveryPositions;

using W::fin;
QVariant nv(double v) { return fin(v) ? QVariant(v) : QVariant::fromValue(nullptr); }
QVariant nullv() { return QVariant::fromValue(nullptr); }
QString cleanDir(const QString &d) { return d.trimmed().isEmpty() ? QString() : QDir::cleanPath(d.trimmed()); }

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
    for (int p = 0; p < kPos; ++p) out[size_t(p)] = phaseUs(*s, deliveryPhaseIds()[size_t(p)]);
    return out;
}

bool hasDelivery(const DeliveryShotInput &in) { return in.hasPlane() || in.hasMonitor(); }

// shotReplay.analysisDetail → the part of a ShotSeries the delivery core reads (phases, impact, the delivery series
// with their phase samples), on the same window-relative clock. For the picked shot when it is not in the snapshot
// yet (the posture presenter's postureShotFromDetail, on this section's keys).
std::shared_ptr<ShotSeries> deliveryShotFromDetail(const QVariantMap &d, const QString &dir)
{
    const QStringList &keys = deliveryMetricKeys();
    auto sh = std::make_shared<ShotSeries>();
    sh->swingDir = dir;
    sh->shotId = folderShotId(dir);
    sh->club = d.value(QStringLiteral("club")).toString();
    for (const QVariant &pv : d.value(QStringLiteral("phases")).toList()) {
        const QVariantMap p = pv.toMap();
        PhaseMark pm;
        pm.phase = p.value(QStringLiteral("phase"), -1).toInt();
        pm.tUs = p.value(QStringLiteral("t_us")).toLongLong();
        pm.conf = p.contains(QStringLiteral("conf")) ? p.value(QStringLiteral("conf")).toDouble() : kNaN;
        if (pm.phase == deliveryPhaseIds()[size_t(kPos - 1)] && sh->impactUs < 0) sh->impactUs = pm.tUs;
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

// ── the golfer's day ────────────────────────────────────────────────────────────────────────

// The session folder the snapshot is (its own, else its first shot's parent, else the picked shot's).
QString sessionDirOf(const SessionSnapshot &snap, const QString &picked)
{
    if (!snap.sessionDir.trimmed().isEmpty()) return cleanDir(snap.sessionDir);
    if (!snap.shots.empty() && !snap.shots.front()->swingDir.isEmpty()) return cleanDir(QFileInfo(snap.shots.front()->swingDir).absolutePath());
    return picked.isEmpty() ? QString() : cleanDir(QFileInfo(picked).absolutePath());
}

// THE GOLFER'S DAY FROM THE OTHER SESSIONS' SUMMARIES (refactor 5.5d; DEC-74; session_delivery.h decision 13): the
// sessions on this session's local day (SessionIdentity's, as the athlete level places them), in their place in the day,
// each one's delivery readings as its own SessionDiagnostics wrote them into its summary (`dayReadings.delivery`, off its
// shots' phase grids by the one rule). Never another session's files. A session not summed up yet, or a newer build's, is
// counted (notSummedUp, newer) and said; `date` is the day ("yyyy-MM-dd"), "" when this session has none.
} // namespace

DeliveryDay deliveryDayFrom(const SessionNeighbours &n, const QString &sessionDir)
{
    DeliveryDay day;
    day.sessions = 0;   // the other sessions with a reading; this one is added below
    if (n.known && n.self.day.isValid()) {
        day.date = n.self.day.toString(QStringLiteral("yyyy-MM-dd"));
        // A neighbour's swing folder, as its index places it beside this one (path text only: nothing is read).
        const QString athleteDir = cleanDir(QFileInfo(cleanDir(sessionDir)).absolutePath());
        std::vector<const NeighbourSession *> same;
        for (const NeighbourSession &o : n.others)
            if (o.place.day == n.self.day) same.push_back(&o);
        std::stable_sort(same.begin(), same.end(), [](const NeighbourSession *a, const NeighbourSession *b) { return a->place.nOfDay < b->place.nOfDay; });
        for (const NeighbourSession *o : same) {
            if (o->state == NeighbourSession::State::NotYet) { ++day.notSummedUp; continue; }
            if (o->state == NeighbourSession::State::Newer) { ++day.newer; continue; }
            bool any = false;
            for (const pinpoint::analysis::ShotDeliveryReading &r : o->readings.delivery) {
                DeliveryShotInput in;
                in.ordinal      = r.ordinal;
                in.shotId       = r.shotId;
                in.swingDir     = athleteDir.isEmpty() ? QString()
                                                       : athleteDir + QLatin1Char('/') + o->place.folder + QLatin1Char('/')
                                                             + pinpoint::SwingPaths::swingFolderName(r.shotId);
                in.session      = o->place.folder;
                in.sessionOfDay = o->place.nOfDay;
                in.thisSession  = false;
                in.plane[5]     = r.planeP6;   // P6
                in.steepening   = r.steepening;
                in.lmPath       = r.lmPath;
                in.lmAttack     = r.lmAttack;
                day.others.push_back(std::move(in));
                any = true;
            }
            if (any) ++day.sessions;
        }
    }
    day.sessions += 1;   // this session
    if (day.notSummedUp > 0 || day.newer > 0)   // said in the trust note too (show with a warning, never hide)
        ppInfo() << "[Charts] delivery day" << day.date << ":" << (day.sessions - 1) << "other sessions read from their summaries,"
                 << day.others.size() << "shots," << day.notSummedUp << "not summed up yet," << day.newer << "of a newer build";
    return day;
}

namespace {

// ── the job's context ───────────────────────────────────────────────────────────────────────

struct Ctx {
    const ChartsDeliveryPresenter::Inputs &in;
    ChartsDeliveryPresenter::Worker *w = nullptr;
    std::shared_ptr<const SessionSnapshot> snap;
    Ctx(const ChartsDeliveryPresenter::Inputs &i, std::shared_ptr<const SessionSnapshot> s) : in(i), snap(std::move(s)) {}
    int n = 0;
    std::vector<DeliveryShotInput> inputs;       // parallel to snap->shots
    int ready = 0;                               // shots with a delivery reading
    int index = -1;                              // the picked shot's index, −1 none
    std::shared_ptr<const ShotSeries> shot;      // the picked shot (the snapshot's, or its own detail)
    DeliveryShotInput pick;
    bool hasPick = false;
    DeliveryShotView view;
    DeliverySessionView sess;
    DeliveryDay day;                             // the golfer's other sessions on this date (decision 13)
    std::array<qint64, kPos> pUs{};
};

// ── the typed results (charts_delivery_types.h) ─────────────────────────────────────────────────

QString xLabel(DeliveryX x) { return x == DeliveryX::Steepening ? QStringLiteral("Hand plane steepening") : QStringLiteral("Swing plane at P6"); }
QString yLabel(DeliveryY y) { return y == DeliveryY::Attack ? QStringLiteral("Attack angle (monitor)") : QStringLiteral("Club path (monitor)"); }

// words::Sentence as QML takes it, with its first clause (the words module's rule, never cut in a view).
U::Sentence sentenceOf(const W::Sentence &s)
{
    U::Sentence out;
    out.text = s.text();
    for (const W::Sentence::Run &r : s.runs) out.runs.append(U::Run{ r.text, r.fig });
    out.clause = W::firstClauseOf(out.text);
    return out;
}

// box_map.h's BOX, typed: NaN as null, the outliers as ordinals, the texts in the reading's rounding.
U::Box boxOf(const box::BoxStats &b, const std::function<QString(double)> &fmt)
{
    U::Box out;
    out.n = b.n;
    out.min = nv(b.min); out.max = nv(b.max);
    out.q1 = nv(b.q1); out.med = nv(b.med); out.q3 = nv(b.q3);
    out.lo = nv(b.lo); out.hi = nv(b.hi);
    for (int i : b.outliers) out.outliers.append(i + 1);   // values are per shot: index + 1 = ordinal
    if (fmt && b.n > 0) out.texts = U::BoxTexts{ fmt(b.lo), fmt(b.q1), fmt(b.med), fmt(b.q3), fmt(b.hi) };
    return out;
}

U::Fit fitOf(const DeliveryFit &f)
{
    U::Fit out;
    out.key = deliveryYKey(f.y) + QStringLiteral("~") + deliveryXKey(f.x);
    out.x = deliveryXKey(f.x);
    out.y = deliveryYKey(f.y);
    out.xLabel = xLabel(f.x);
    out.yLabel = yLabel(f.y);
    out.n = f.n;
    out.slope = nv(f.slope); out.intercept = nv(f.intercept); out.r = nv(f.r); out.sd = nv(f.sd);
    out.slopeText = W::fig(f.slope, 2);
    out.rText = W::fig(f.r, 2);
    out.sdText = fin(f.sd) ? QStringLiteral("±") + deliveryDeg1(f.sd) : QString();
    out.holds = f.holds;
    out.verdict = f.verdict;
    out.why = f.why;
    return out;
}

// The scatter: the fit's own points (the day's shots with both readings, this session's marked), the picked one ringed.
U::Scatter scatterOf(const DeliveryFit &f, const std::vector<DeliveryShotInput> &inputs, const DeliveryShotInput *pick)
{
    U::Scatter out;
    double xl = kNaN, xh = kNaN, yl = kNaN, yh = kNaN;
    const auto span = [](double &lo, double &hi, double v) { if (!fin(v)) return; lo = fin(lo) ? std::min(lo, v) : v; hi = fin(hi) ? std::max(hi, v) : v; };
    const QString pickDir = pick ? cleanDir(pick->swingDir) : QString();
    for (const DeliveryShotInput &s : inputs) {
        const double x = deliveryXOf(s, f.x), y = deliveryYOf(s, f.y);
        if (!fin(x) || !fin(y)) continue;
        span(xl, xh, x); span(yl, yh, y);
        // shotNo: the swing number its label prints (step 2.7b); sessionLabel: another session's place in the day,
        // by SessionIdentity's n-of-day ("session 2"), never parsed out of its folder's name in the view.
        U::ScatterPoint p;
        p.ordinal = s.ordinal;
        p.swingDir = s.swingDir;
        p.shotNo = W::shotNumber(s.shotId, s.ordinal);
        p.x = x;
        p.y = y;
        p.thisSession = s.thisSession;
        p.session = s.session;
        p.sessionLabel = s.thisSession ? QString()
                       : s.sessionOfDay > 0 ? QStringLiteral("session %1").arg(s.sessionOfDay)
                                            : QStringLiteral("another session");
        p.picked = !pickDir.isEmpty() && cleanDir(s.swingDir) == pickDir;
        out.points.append(p);
    }
    if (pick) {
        const double x = deliveryXOf(*pick, f.x), y = deliveryYOf(*pick, f.y);
        if (fin(x)) {
            span(xl, xh, x); span(yl, yh, y);
            U::ScatterPicked pk;
            pk.x = x;
            pk.y = nv(y);
            out.picked = QVariant::fromValue(pk);
        }
    }
    if (f.holds && fin(xl) && fin(xh))
        out.line = QVariant::fromValue(U::ScatterLine{ xl, f.intercept + f.slope * xl, xh, f.intercept + f.slope * xh });
    out.xLabel = xLabel(f.x);
    out.yLabel = yLabel(f.y);
    out.unit = QStringLiteral("°");
    out.holds = f.holds;
    // rule C3: the hands' steepening is a signed quantity (steeper +, flatter −), the plane at P6 an angle
    out.xSigned = f.x == DeliveryX::Steepening;
    out.xLo = nv(xl); out.xHi = nv(xh);
    out.yLo = nv(yl); out.yHi = nv(yh);
    return out;
}

U::Flight flightOf(bool available, const QString &text, const QString &attackText, const QString &thisLine,
                   const DeliveryFits &fits, const DeliveryPlace &place,
                   const std::vector<DeliveryShotInput> &inputs, const DeliveryDay &day, const DeliveryShotInput *pick)
{
    const DeliveryFit &f = fits.path();
    const std::vector<DeliveryShotInput> pool = deliveryPool(inputs, &day);
    U::Flight out;
    out.available = available;
    out.text = text;
    out.pool.date = day.date;
    out.pool.sessions = day.sessions;
    out.pool.shots = int(pool.size());
    out.pool.otherShots = int(day.others.size());
    out.pool.words = deliveryPoolWords(&day);
    out.pool.notSummedUp = day.notSummedUp;
    out.pool.newer = day.newer;
    out.attackText = attackText;
    out.thisLine = thisLine;
    out.fit = fitOf(f);
    out.attack = fitOf(fits.attackP6);
    out.fits.pathP6 = fitOf(fits.pathP6);
    out.fits.pathSteep = fitOf(fits.pathSteep);
    out.fits.attackP6 = fitOf(fits.attackP6);
    out.place.known = place.known;
    out.place.third = place.third;
    out.place.words = place.words;
    out.scatter = scatterOf(f, pool, pick);
    return out;
}

U::Shape shapeOf(const DeliveryShape &s)
{
    U::Shape out;
    out.available = s.available;
    out.face = nv(s.face);
    out.faceToPath = nv(s.faceToPath);
    out.faceText = deliveryDeg1(s.face);
    out.faceToPathText = deliveryDeg1(s.faceToPath);
    out.fromBall = s.fromBall;
    out.launchDirText = deliveryDeg1(s.launchDir);
    out.spinAxisText = deliveryDeg1(s.spinAxis);
    out.start = s.start;
    out.curve = s.curve;
    out.slight = s.slight;
    out.startWord = s.startWord;
    out.curveWord = s.curveWord;
    out.word = s.word;
    return out;
}

U::Impact impactOf(const std::vector<DeliveryImpactLine> &lines)
{
    U::Impact out;
    for (const DeliveryImpactLine &l : lines) {
        U::ImpactLine m;
        m.key = l.key;
        m.text = l.text;
        m.source = l.source;
        m.value = nv(l.value);
        m.face = nv(l.face);
        m.loft = nv(l.loft);
        m.loftSource = l.loftSource;
        out.lines.append(m);
    }
    out.available = !out.lines.isEmpty();
    return out;
}

U::Trust trustOf(const DeliveryTrust &t) { U::Trust out; out.lines = t.lines; return out; }

U::Story storyOf(const DeliveryStory &s, const DeliveryShotInput &in, const std::array<qint64, kPos> &us)
{
    U::Story out;
    for (int p = 0; p < kPos; ++p) {
        U::Position pos;
        pos.index = p;
        pos.tag = deliveryPositionTag(p);
        pos.phase = deliveryPhaseIds()[size_t(p)];
        pos.name = deliveryPositionWhen(p);
        pos.value = nv(in.plane[size_t(p)]);
        pos.text = deliveryDeg(in.plane[size_t(p)]);
        pos.hands = nv(in.hands[size_t(p)]);
        pos.handsText = deliveryDeg(in.hands[size_t(p)]);
        pos.tUs = us[size_t(p)];
        pos.seekUs = us[size_t(p)];
        out.positions.append(pos);
    }
    out.available = s.available;
    out.text = s.text;
    out.parts = s.parts;
    out.set = s.set;
    out.back = s.back;
    out.top = s.top;
    out.delivered = s.delivered;
    out.hands = s.hands;
    out.shapeRead = s.shapeRead;
    out.shapeWord = s.shapeWord;
    out.shapeLine = s.shapeLine;
    return out;
}

U::Slot slotOf(const DeliverySlot &s)
{
    U::Slot out;
    for (const DeliverySlotMark &m : s.outside) out.outside.append(U::SlotMark{ m.pos, deliveryPositionTag(m.pos), m.side, m.words });
    const bool both = fin(s.address) && fin(s.shoulders);
    out.available = s.available;
    out.address = nv(s.address);
    out.shoulders = nv(s.shoulders);
    out.addressText = deliveryDeg(s.address);
    out.shouldersText = deliveryDeg(s.shoulders);
    out.widthText = both ? deliveryDeg(std::round(s.shoulders) - std::round(s.address)) + QStringLiteral(" wide") : QString();
    out.text = s.text;
    return out;
}

// One standfirst per position: what the plane was there, against where it was set and the on-plane window.
QList<U::Sentence> standfirstsOf(const DeliveryShotInput &in)
{
    QList<U::Sentence> out;
    const double a = in.plane[0], sh = in.shoulderPlane;
    for (int p = 0; p < kPos; ++p) {
        W::Sentence s;
        const double v = in.plane[size_t(p)];
        const QString when = deliveryPositionWhen(p);
        if (!fin(v)) {
            s.append(QStringLiteral("At %1 the plane was not read.").arg(when));
        } else if (p == 0) {
            s.append(QStringLiteral("At address the plane is ")).figure(deliveryDeg(v));
            if (fin(sh)) s.append(QStringLiteral(", your shoulders' ")).figure(deliveryDeg(sh))
                          .append(QStringLiteral(": an on-plane window ")).figure(deliveryDeg(std::round(sh) - std::round(v))).append(QStringLiteral(" wide."));
            else s.append(QStringLiteral("."));
        } else {
            s.append(QStringLiteral("At %1 the plane was ").arg(when)).figure(deliveryDeg(v));
            if (fin(a)) {
                const double d = std::round(v) - std::round(a);
                if (std::fabs(d) <= kDeliveryDeliveredSameDeg) s.append(QStringLiteral(", where you set it"));
                else s.append(QStringLiteral(", ")).figure(deliveryDeg(std::fabs(d)))
                       .append(d > 0 ? QStringLiteral(" above where you set it") : QStringLiteral(" below where you set it"));
            }
            if (fin(sh) && v > sh) s.append(QStringLiteral("; above your shoulder plane"));
            else if (fin(a) && v < a) s.append(QStringLiteral("; under your address line"));
            const double t = in.posMs[size_t(p)];
            const bool hands = fin(t) && std::any_of(in.handsSpans.begin(), in.handsSpans.end(),
                                                     [t](const DeliverySpan &sp) { return t >= sp.t0Ms - 1e-6 && t <= sp.t1Ms + 1e-6; });
            s.append(hands ? QStringLiteral(" (the hands carry it here).") : QStringLiteral("."));
        }
        out.append(sentenceOf(s));
    }
    return out;
}
QString clubOf(const std::vector<DeliveryShotInput> &inputs)
{
    QStringList clubs;
    for (const DeliveryShotInput &s : inputs) {
        if (!hasDelivery(s) || s.club.trimmed().isEmpty()) continue;
        const QString w = deliveryClubWords(s.club);
        if (!clubs.contains(w)) clubs << w;
    }
    return clubs.join(QStringLiteral(", "));
}

QString emptyTail()
{
    return QStringLiteral(" The Club delivery view reads the swing plane from the down-the-line camera and the club's "
                          "delivery from the launch monitor.");
}

// ── the picture strip (stage 6, delivery_picture.h) ──────────────────────────────────────────────

// The swing's document → the picture's inputs, cached by the document's stamp (a re-analysed swing is read again).
// A pinned entry (the tests' seam) is kept whatever the disk says. The posture presenter's pictureDocOf.
const DeliveryPictureDoc &pictureDocOf(ChartsDeliveryPresenter::Worker &w, const QString &swingDir)
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
    ChartsDeliveryPresenter::Worker::PictureEntry e;
    e.size = info.size;
    e.mtimeMs = info.mtimeMs;
    if (!info.exists()) {
        e.doc.swingDir = dir;
        e.doc.why = QStringLiteral("the swing's document is not on disk");
    } else {
        QString err;
        const QJsonObject root = pinpoint::SwingStore::load(dir, &err);
        e.doc = deliveryPictureDocFrom(root, dir, QStringLiteral("%1-%2").arg(info.size).arg(info.mtimeMs));
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

// Where Charts caches the picture's stills.
QString pictureStillDir()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    return QDir(base.isEmpty() ? QDir::tempPath() : base).filePath(QStringLiteral("delivery-picture"));
}

// The stills a picture still needs made, as the still maker reads them (a map each: main.cpp's maker).
QVariantList pendingStillsOf(const U::Picture &picture)
{
    QVariantList out;
    for (const U::PictureGroup &g : picture.groups)
        for (const U::PictureFrame &f : g.frames) {
            if (f.still.metaType() != QMetaType::fromType<U::PictureStill>()) continue;
            const U::PictureStill still = f.still.value<U::PictureStill>();
            if (f.src.isEmpty() && f.imageWhy.isEmpty() && !still.outPath.isEmpty())
                out.append(chartsGadgetMap(still));
        }
    return out;
}

// Each frame's note, from the plane story (the presenter's own numbers, never the picture's): the address frame names
// the two lines, the top and P6 the plane there and where it sat against them — "plane 65° · inside the on-plane window".
U::Picture withFrameNotes(U::Picture pic, const DeliveryShotInput &in, const DeliverySlot &slot)
{
    for (U::PictureGroup &g : pic.groups)
        for (U::PictureFrame &f : g.frames) {
            const int pos = f.pos;
            QStringList parts;
            if (pos == 0) {
                if (fin(in.plane[0])) parts << QStringLiteral("address line %1").arg(deliveryDeg(in.plane[0]));
                if (fin(in.shoulderPlane)) parts << QStringLiteral("shoulder plane %1").arg(deliveryDeg(in.shoulderPlane));
            } else if (pos >= 0 && pos < kPos && fin(in.plane[size_t(pos)])) {
                parts << QStringLiteral("plane %1").arg(deliveryDeg(in.plane[size_t(pos)]));
                if (slot.available) {
                    QString where = QStringLiteral("inside the on-plane window");
                    for (const DeliverySlotMark &m : slot.outside) if (m.pos == pos) where = m.words;
                    parts << where;
                }
            }
            f.note = parts.join(QStringLiteral("  ·  "));
        }
    return pic;
}

// ── the sections ────────────────────────────────────────────────────────────────────────────

void buildThisShot(Ctx &c, ChartsDeliveryPresenter::Result &R)
{
    U::ThisShot out;
    R.posUs = c.pUs;
    const bool has = c.shot && c.view.available;
    out.available = has;
    if (!has) {
        out.empty = (c.shot || !c.in.pickedSwingDir.trimmed().isEmpty()
                         ? QStringLiteral("No swing plane or launch monitor reading for this swing.")
                         : QStringLiteral("Select a swing to review.")) + emptyTail();
        R.thisShot = out;
        R.standfirsts.clear();
        return;
    }
    const ShotSeries &s = *c.shot;
    const DeliveryShotView &v = c.view;
    const bool inSession = c.index >= 0;
    out.ordinal = s.ordinal;
    out.shotId = s.shotId;
    out.swingDir = s.swingDir;
    out.inSession = inSession;
    const QString club = s.club.trimmed().isEmpty() ? QString() : deliveryClubWords(s.club);
    out.kicker = (inSession ? W::shotOfText(s.shotId, s.ordinal, c.n) : QStringLiteral("This swing"))
                 + (club.isEmpty() ? QString() : QStringLiteral("  ·  ") + club) + QStringLiteral("  ·  Club delivery");
    out.mode = v.mode;
    out.hasPlane = v.hasPlane;
    out.hasMonitor = v.hasMonitor;
    out.head = sentenceOf(W::Sentence().append(v.head.text));
    out.headText = v.head.text;
    out.headRule = v.head.rule;
    out.story = storyOf(v.story, c.pick, c.pUs);
    out.slot = slotOf(v.slot);
    out.flight = flightOf(v.flightAvailable, v.flightText, v.attackText, v.thisLine, v.fits, v.place, c.inputs, c.day, &c.pick);
    out.impact = impactOf(v.impact);
    out.shape = shapeOf(v.shape);
    out.trust = trustOf(v.trust);
    out.replay.head = v.head.text;
    out.replay.story = v.story.available ? v.story.text : QString();
    out.replay.shape = v.shape.available ? v.shape.word : QString();
    R.standfirsts = v.hasPlane ? standfirstsOf(c.pick) : QList<U::Sentence>{};
    out.standfirsts = R.standfirsts;

    // THE PICTURE (stage 6): this swing's down-the-line frames at address, the top and P6, the address line and the
    // shoulder plane frozen on all three
    if (c.w && !s.swingDir.trimmed().isEmpty()) {
        DeliveryPictureOptions po;
        po.stillDir = pictureStillDir();
        po.ordinal = s.ordinal;
        {
            QMutexLocker sl(&c.w->stillMutex);
            po.stillErrors = c.w->stillErrors;
        }
        out.picture = withFrameNotes(deliveryPicture(pictureDocOf(*c.w, s.swingDir), po), c.pick, v.slot);
        R.pendingStills = pendingStillsOf(out.picture);
    } else {
        out.picture = deliveryPictureUnread(s.swingDir, QStringLiteral("this swing has no folder"));
    }
    R.thisShot = out;
}

// The session (Charts' Session, and the report's page with no picked shot).
U::Session sessionOf(const std::vector<DeliveryShotInput> &inputs, const DeliverySessionView &sv, const DeliveryDay &day,
                     int pickedIndex, const DeliveryShotInput *pick, const QString &noun = QStringLiteral("shots"))
{
    U::Session out;
    const int n = int(inputs.size());
    out.available = sv.available;
    out.n = n;
    out.shots = sv.shots;
    out.planeShots = sv.planeShots;
    out.monitorShots = sv.monitorShots;
    out.isSmall = sv.shots < 10;
    if (!sv.available) return out;
    const QString clubs = clubOf(inputs);
    out.kicker = QStringLiteral("%1 %2").arg(sv.shots).arg(noun)
                 + (clubs.isEmpty() ? QString() : QStringLiteral("  ·  ") + clubs) + QStringLiteral("  ·  Club delivery");
    out.head = sentenceOf(W::Sentence().append(sv.head));
    out.headText = sv.head;

    for (int p = 0; p < kPos; ++p) {
        const box::BoxStats &b = sv.band.boxes[size_t(p)];
        U::BandPosition bp;
        for (const DeliveryShotInput &s : inputs) bp.values.append(s.plane[size_t(p)]);   // NaN: the shot's plane unread there
        bp.index = p;
        bp.tag = deliveryPositionTag(p);
        bp.name = deliveryPositionWhen(p);
        bp.box = boxOf(b, [](double v) { return deliveryDeg(v); });
        bp.median = nv(b.med);
        bp.medianText = deliveryDeg(b.med);
        bp.wide = nv(b.iqr);
        bp.half = nv(fin(b.iqr) ? b.iqr / 2 : kNaN);
        bp.halfText = fin(b.iqr) ? QStringLiteral("±") + deliveryDeg1(b.iqr / 2) : QString();
        bp.picked = pick ? nv(pick->plane[size_t(p)]) : nullv();
        out.band.positions.append(bp);
    }
    out.band.shown = sv.band.shown;
    out.band.n = sv.band.n;
    out.varies.has = sv.varies.has;
    out.varies.tightest = sv.varies.has ? deliveryPositionTag(sv.varies.tightest) : QString();
    out.varies.loosest = sv.varies.has ? deliveryPositionTag(sv.varies.loosest) : QString();
    out.varies.tightHalf = nv(sv.varies.tightHalf);
    out.varies.looseHalf = nv(sv.varies.looseHalf);
    out.varies.words = sv.varies.words;
    out.usual.address = nv(sv.usual.plane[0]);
    out.usual.shoulders = nv(sv.usual.shoulderPlane);
    out.usual.steepening = nv(sv.usual.steepening);
    out.usual.addressText = deliveryDeg(sv.usual.plane[0]);
    out.usual.shouldersText = deliveryDeg(sv.usual.shoulderPlane);
    out.usual.steepeningText = W::figSigned(sv.usual.steepening, QStringLiteral("°"));
    DeliveryPlace none;
    out.flight = flightOf(sv.fits.path().n > 0, sv.flightText, sv.attackText, QString(), sv.fits, none, inputs, day, pick);
    for (size_t i = 0; i < inputs.size() && i < sv.shapes.size(); ++i) {
        const DeliveryShape &sh = sv.shapes[i];
        U::ShapeMark m;
        m.ordinal = inputs[i].ordinal;
        m.swingDir = inputs[i].swingDir;
        m.shotNo = W::shotNumber(inputs[i].shotId, inputs[i].ordinal);   // its label's number (2.7b)
        m.available = sh.available;
        m.start = sh.start;
        m.curve = sh.curve;
        m.word = sh.word;
        m.fromBall = sh.fromBall;
        m.picked = int(i) == pickedIndex;
        // rule C6: the grid's words while the dot is hovered, by the swing number (the ordinal when it has none)
        m.hoverText = QStringLiteral("Shot %1  %2").arg(m.shotNo ? m.shotNo : m.ordinal).arg(sh.word);
        out.shapes.marks.append(m);
    }
    out.shapes.line = sv.shapeLine;
    out.trust = trustOf(sv.trust);
    return out;
}

// The plot's lines over the swing plane curve's row (§4.2): the picked shot's own (This shot), else the session's medians.
U::PlotLayers plotLayersOf(Ctx &c)
{
    const bool sessionScope = c.in.scope == QLatin1String("session");
    double address = kNaN, shoulders = kNaN, t0 = kNaN, t1 = kNaN;
    std::vector<DeliverySpan> spans;
    if (!sessionScope) {
        if (!c.shot || !c.pick.hasPlane()) return {};
        address = c.pick.plane[0];
        shoulders = c.pick.shoulderPlane;
        t0 = c.pick.curveFromMs; t1 = c.pick.curveToMs;
        spans = c.pick.handsSpans;
    } else {
        if (c.sess.planeShots == 0) return {};
        address = c.sess.usual.plane[0];
        shoulders = c.sess.usual.shoulderPlane;
        for (const DeliveryShotInput &s : c.inputs) {
            if (fin(s.curveFromMs)) t0 = fin(t0) ? std::min(t0, s.curveFromMs) : s.curveFromMs;
            if (fin(s.curveToMs)) t1 = fin(t1) ? std::max(t1, s.curveToMs) : s.curveToMs;
        }
    }
    U::PlotLayers out;
    QList<U::PlotLayer> &layers = out.swingPlaneCurve;
    const auto hline = [&](double v, const QString &role) {
        U::PlotLayer l;
        l.k = QStringLiteral("hline");
        l.v = v;
        l.role = role;
        l.label = role;
        l.text = deliveryDeg(v);
        layers.append(l);
    };
    if (fin(address)) hline(address, QStringLiteral("address"));
    if (fin(shoulders)) hline(shoulders, QStringLiteral("shoulders"));
    if (fin(address) && fin(shoulders) && fin(t0) && fin(t1)) {
        U::PlotLayer l;
        l.k = QStringLiteral("band");
        l.t = { t0, t1 };
        l.lo = { std::min(address, shoulders), std::min(address, shoulders) };
        l.hi = { std::max(address, shoulders), std::max(address, shoulders) };
        l.role = QStringLiteral("slot");
        l.label = QStringLiteral("the on-plane window");
        layers.append(l);
    }
    for (size_t i = 0; i < spans.size(); ++i) {
        U::PlotLayer l;
        l.k = QStringLiteral("span");
        l.t0 = spans[i].t0Ms;
        l.t1 = spans[i].t1Ms;
        l.role = QStringLiteral("hands");
        l.label = QStringLiteral("hands carry the plane here");
        l.labelled = i == 0;
        layers.append(l);
    }
    return out;
}

void buildChoices(Ctx &c, ChartsDeliveryPresenter::Result &R)
{
    const QString scope = c.in.scope == QLatin1String("session") ? QStringLiteral("session") : QStringLiteral("shot");
    R.choices.scope = scope;
    R.choices.scopes = { U::ScopeChoice{ QStringLiteral("shot"), QStringLiteral("This shot"), scope == QLatin1String("shot") },
                         U::ScopeChoice{ QStringLiteral("session"), QStringLiteral("Session"), scope == QLatin1String("session") } };
    R.choices.onShowKeys = deliveryOnShowKeys();
}

// THE ONE DIRECTION RULE'S BOUNDARIES (refactor 6.3b, F-05) for each shot's club: the pack and the norms the app fires
// against (RuleData's snapshot), at its grade policy, so this section names a shot as the factors and Tendencies do.
// With no rule data (a test's bare process) a shot keeps the shipped default, which is the same numbers.
void resolveDirection(std::vector<DeliveryShotInput> &inputs)
{
    const std::shared_ptr<const pinpoint::RuleDataSnapshot> rules = pinpoint::RuleData::current();
    if (!rules || !rules->pack || !rules->norms) return;
    const pinpoint::analysis::CharacteristicPack &pack = rules->pack->pack();
    const pinpoint::analysis::GradePolicy policy = pinpoint::analysis::gradePolicyByName(rules->gradePolicy);
    QHash<QString, pinpoint::analysis::ShotDirectionBounds> byClub;
    for (DeliveryShotInput &in : inputs) {
        auto it = byClub.find(in.club);
        if (it == byClub.end())
            it = byClub.insert(in.club, pinpoint::analysis::shotDirectionBounds(
                                            pack, *rules->norms,
                                            in.club.trimmed().isEmpty() ? QString() : pinpoint::analysis::contextIdForClub(in.club),
                                            policy));
        in.direction = *it;
    }
}

// The shots' readings, through the worker's cache.
std::vector<DeliveryShotInput> readInputs(ChartsDeliveryPresenter::Worker *w, const SessionSnapshot &snap)
{
    std::vector<DeliveryShotInput> out;
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
        out.push_back(deliveryInputFromShot(*s));
        if (w) fresh.insert(s.get(), CacheEntry{ s, out.back() });
    }
    if (w) w->cache = std::move(fresh);
    resolveDirection(out);
    return out;
}

} // namespace

// ── the job ─────────────────────────────────────────────────────────────────────────────────

static std::shared_ptr<ChartsDeliveryPresenter::Result> computeDelivery(ChartsDeliveryPresenter::Worker &w, const ChartsDeliveryPresenter::Inputs &in)
{
    QMutexLocker lk(&w.mutex);
    auto R = std::make_shared<ChartsDeliveryPresenter::Result>();
    std::shared_ptr<const SessionSnapshot> snap = in.snapshot;
    if (!snap) snap = std::make_shared<SessionSnapshot>();
    Ctx c(in, snap);
    c.w = &w;
    c.n = c.snap->count();
    c.inputs = readInputs(&w, *c.snap);
    for (const DeliveryShotInput &s : c.inputs) if (hasDelivery(s)) ++c.ready;

    // the picked shot: the snapshot's, else its own detail (a live shot, another session's)
    const QString want = cleanDir(in.pickedSwingDir);
    if (!want.isEmpty())
        for (int i = 0; i < c.n; ++i)
            if (cleanDir(c.snap->shots[size_t(i)]->swingDir) == want) { c.index = i; break; }
    if (c.index >= 0) {
        c.shot = c.snap->shots[size_t(c.index)];
        c.pick = c.inputs[size_t(c.index)];
    } else if (!want.isEmpty() && !in.pickedDetail.value(QStringLiteral("series")).toList().isEmpty()) {
        auto sh = deliveryShotFromDetail(in.pickedDetail, want);
        sh->ordinal = c.n + 1;
        std::vector<DeliveryShotInput> one{ deliveryInputFromShot(*sh) };
        resolveDirection(one);
        c.pick = one.front();
        c.shot = sh;
    }
    c.hasPick = c.shot && hasDelivery(c.pick);

    c.day = deliveryDayFrom(in.neighbours, sessionDirOf(*c.snap, want));
    c.sess = deliverySession(c.inputs, QStringLiteral("shots"), &c.day);
    if (c.shot) c.view = deliveryShot(c.pick, c.inputs, &c.day);
    c.pUs = positionUs(c.shot.get());

    R->available = c.ready > 0 || c.hasPick;
    buildThisShot(c, *R);
    R->session = sessionOf(c.inputs, c.sess, c.day, c.index, c.shot ? &c.pick : nullptr);
    R->plotLayers = plotLayersOf(c);
    buildChoices(c, *R);
    return R;
}

DeliverySessionReport deliverySessionReport(const SessionSnapshot &snapshot, const QString &noun, const SessionNeighbours &neighbours)
{
    return deliverySessionReportOfDay(snapshot, noun, deliveryDayFrom(neighbours, sessionDirOf(snapshot, QString())));
}

DeliverySessionReport deliverySessionReportOfDay(const SessionSnapshot &snapshot, const QString &noun, const DeliveryDay &day)
{
    const QString many = noun.trimmed().isEmpty() ? QStringLiteral("shots") : noun.trimmed();
    const std::vector<DeliveryShotInput> inputs = readInputs(nullptr, snapshot);
    const DeliverySessionView sv = deliverySession(inputs, many, &day);
    return DeliverySessionReport{ sessionOf(inputs, sv, day, -1, nullptr, many) };
}

QVariantMap reportPayloadOf(const DeliverySessionReport &page)
{
    return chartsGadgetMap(page.session);
}

// ── the still maker (process-wide: the app sets it once) ────────────────────────────────────────
static QMutex &stillMakerMutex() { static QMutex m; return m; }
static ChartsDeliveryPresenter::StillMaker &stillMakerRef() { static ChartsDeliveryPresenter::StillMaker f; return f; }
static ChartsDeliveryPresenter::StillMaker stillMaker()
{
    QMutexLocker l(&stillMakerMutex());
    return stillMakerRef();
}

// ── the object ──────────────────────────────────────────────────────────────────────────────

ChartsDeliveryPresenter::ChartsDeliveryPresenter(QObject *parent)
    : QObject(parent), m_worker(std::make_shared<Worker>())
{
    schedule();
}

ChartsDeliveryPresenter::~ChartsDeliveryPresenter()
{
    // No wait: a job still running finishes into nothing (charts_work.h: the lane is cancelled, #173).
}

SeriesSource *ChartsDeliveryPresenter::model() const { return m_model.data(); }

void ChartsDeliveryPresenter::setModel(SeriesSource *m)
{
    if (m_model == m) return;
    if (m_model) disconnect(m_model, nullptr, this, nullptr);
    m_model = m;
    if (m)
        connect(m, &SeriesSource::snapshotChanged, this, &ChartsDeliveryPresenter::schedule);
    emit inputsChanged();
    schedule();
}

std::shared_ptr<const SessionSnapshot> ChartsDeliveryPresenter::currentSnapshot() const
{
    return chosenSnapshot(m_directSnapshot, m_model.data());   // P2: a direct snapshot wins
}

void ChartsDeliveryPresenter::setSnapshot(std::shared_ptr<const SessionSnapshot> snapshot)
{
    m_directSnapshot = std::move(snapshot);
    schedule();
}

#define PP_SETTER(Type, name, field)                                       \
    void ChartsDeliveryPresenter::name(Type v)                             \
    {                                                                      \
        if (m_in.field == v) return;                                       \
        m_in.field = v;                                                    \
        emit inputsChanged();                                              \
        schedule();                                                        \
    }
PP_SETTER(const QString &, setPickedSwingDir, pickedSwingDir)
PP_SETTER(const QString &, setRegisterName, reg)
PP_SETTER(const QString &, setScope, scope)
#undef PP_SETTER

void ChartsDeliveryPresenter::setPickedDetail(const QVariantMap &d)
{
    if (m_in.pickedDetail.isSharedWith(d)) return;
    m_in.pickedDetail = d;
    emit inputsChanged();
    schedule();
}

void ChartsDeliveryPresenter::setPlayheadUs(qint64 us)
{
    if (m_playheadUs == us) return;
    m_playheadUs = us;
    emit playheadChanged();
    updateSelected(false);
}

bool ChartsDeliveryPresenter::available() { flushIfSynchronous(); return m_result && m_result->available; }
U::Choices ChartsDeliveryPresenter::choices() { flushIfSynchronous(); return m_result ? m_result->choices : U::Choices{}; }
U::ThisShot ChartsDeliveryPresenter::thisShot() { flushIfSynchronous(); return m_result ? m_result->thisShot : U::ThisShot{}; }
U::Session ChartsDeliveryPresenter::session() { flushIfSynchronous(); return m_result ? m_result->session : U::Session{}; }
U::PlotLayers ChartsDeliveryPresenter::plotLayers() { flushIfSynchronous(); return m_result ? m_result->plotLayers : U::PlotLayers{}; }

U::Sentence ChartsDeliveryPresenter::standfirst() const
{
    if (!m_result || m_selected < 0 || m_selected >= m_result->standfirsts.size()) return {};
    return m_result->standfirsts.at(m_selected);
}

qint64 ChartsDeliveryPresenter::seekUsFor(const QString &tag) const
{
    if (!m_result) return -1;
    const QString t = tag.trimmed().toUpper();
    if (!t.startsWith(QLatin1Char('P'))) return -1;
    bool ok = false;
    const int i = t.mid(1).toInt(&ok) - 1;
    return ok && i >= 0 && i < kPos ? m_result->posUs[size_t(i)] : -1;
}

void ChartsDeliveryPresenter::startJob()
{
    if (!m_sched.begin()) return;     // synchronous, or a job in flight: apply() re-runs a dirty presenter
    Inputs in = m_in;
    in.snapshot = currentSnapshot();
    // THE DAY'S OTHER SESSIONS, from their summaries (refactor 5.5d): taken here, on the GUI thread, from the athlete level,
    // and handed to the job by value; heard again when that athlete level changes (a summary written).
    const QString dir = in.snapshot ? sessionDirOf(*in.snapshot, in.pickedSwingDir) : QString();
    in.neighbours = neighboursOf(dir);
    watchDay(dir);
    emit busyChanged();
    auto w = m_worker;
    askLane(m_pool, [w, in]() { return computeDelivery(*w, in); },
            [this](auto r) { apply(r); });
}

void ChartsDeliveryPresenter::watchDay(const QString &sessionDir)
{
    if (sessionDir == m_watchedDir) return;
    for (const QMetaObject::Connection &c : std::as_const(m_dayWatch)) disconnect(c);
    m_watchedDir = sessionDir;
    m_dayWatch = sessionDir.isEmpty() ? QList<QMetaObject::Connection>{}
                                      : watchNeighbours(this, sessionDir, [this] { schedule(); });
}

void ChartsDeliveryPresenter::apply(std::shared_ptr<Result> r)
{
    const bool wasRunning = m_sched.land();
    m_result = std::move(r);
    emit resultsChanged();
    updateSelected(true);
    m_sched.afterLanding(wasRunning);
    makeStills();
}

void ChartsDeliveryPresenter::flushIfSynchronous()
{
    if (!m_sched.takeFlush()) return;
    Inputs in = m_in;
    in.snapshot = currentSnapshot();
    in.neighbours = neighboursOf(in.snapshot ? sessionDirOf(*in.snapshot, in.pickedSwingDir) : QString());   // refactor 5.5d
    m_result = computeDelivery(*m_worker, in);
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
            m_result = computeDelivery(*m_worker, in);
        }
    }
    updateSelected(true);
}

// The picture's stills, made once each off the GUI thread by the app's maker; the picture is read again when they land.
void ChartsDeliveryPresenter::makeStills()
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

void ChartsDeliveryPresenter::setStillMaker(StillMaker maker)
{
    QMutexLocker l(&stillMakerMutex());
    stillMakerRef() = std::move(maker);
}

void ChartsDeliveryPresenter::setPictureDocumentForTest(const QString &swingDir, const QString &jsonFile)
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
            e.doc = deliveryPictureDocFrom(root, dir, QStringLiteral("test"));
            if (root.isEmpty()) e.doc.why = QStringLiteral("the test's document %1 could not be read").arg(jsonFile);
            m_worker->pictures.insert(dir, std::move(e));
        }
    }
    schedule();
}

void ChartsDeliveryPresenter::setSynchronous(bool on)
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

bool ChartsDeliveryPresenter::waitForIdle(int msTimeout)
{
    return m_sched.waitForIdle(m_pool, msTimeout, [this] { flushIfSynchronous(); });
}

// The playhead's position: the latest present position at or before it, else the first present one (the posture
// presenter's rule). Fires only when it moves to another position.
void ChartsDeliveryPresenter::updateSelected(bool force)
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

// moc for the typed results' gadgets (charts_delivery_types.h has no .cpp of its own)
#include "moc_charts_delivery_types.cpp"
