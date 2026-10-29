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

#include "box_map.h"
#include "charts_words.h"
#include "session_delivery.h"
#include "session_series_model.h"

#include "../../Core/pp_debug.h"

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
#include <QSet>

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

// The golfer's day beyond this session (session_delivery.h decision 13): its other sessions' shots, read off their
// swing_phasegrid.json files, keyed by the session folder and stamped by the day's sibling folders and their mtimes.
struct DayEntry {
    QString stamp;
    DeliveryDay day;           // `sessions` counts the OTHER sessions with a reading; the job adds this one
};

} // namespace pinpoint::charts::deliveryview

namespace pinpoint::charts {

using namespace deliveryview;
namespace W = words;

// ── the result and the worker ───────────────────────────────────────────────────────────────

struct ChartsDeliveryPresenter::Result {
    bool available = false;
    QVariantMap choices, thisShot, session, plotLayers;
    QVariantList standfirsts;                          // 7 Sentences (thisShot.standfirsts)
    std::array<qint64, kDeliveryPositions> posUs{};    // the picked shot's P µs, −1 none
};

struct ChartsDeliveryPresenter::Worker {
    QMutex mutex;                                      // held for a whole job
    QHash<const ShotSeries *, CacheEntry> cache;
    QHash<QString, DayEntry> days;                     // by the cleaned session folder
};

namespace {

constexpr int kPos = kDeliveryPositions;

bool fin(double v) { return std::isfinite(v); }
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
    sh->shotId = SessionSeriesModel::folderShotId(dir);
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

// The day's sibling session folders: under the golfer folder, the same "yyyy-MM-dd" prefix, not this one.
QStringList daySiblings(const QString &sessionDir, QString *date)
{
    const QFileInfo fi(sessionDir);
    const QString name = fi.fileName();
    const QString d = name.left(10);
    if (date) *date = QDate::fromString(d, QStringLiteral("yyyy-MM-dd")).isValid() ? d : QString();
    if (!date || date->isEmpty()) return {};
    QStringList out;
    const QDir golfer(fi.absolutePath());
    for (const QString &n : golfer.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
        if (n != name && n.startsWith(d)) out << golfer.filePath(n);
    return out;
}

QString dayStamp(const QStringList &siblings)
{
    QStringList parts;
    for (const QString &s : siblings) parts << QStringLiteral("%1@%2").arg(QFileInfo(s).fileName()).arg(QFileInfo(s).lastModified().toMSecsSinceEpoch());
    return parts.join(QLatin1Char('|'));
}

// Read the siblings' sidecars — never a swing document (a 28 MB parse froze the GUI once): each swing's
// swing_phasegrid.json, its staleness judged by a stat of the document beside it.
DeliveryDay readDay(const QStringList &siblings, const QString &date)
{
    DeliveryDay day;
    day.date = date;
    day.sessions = 0;
    QElapsedTimer t;
    t.start();
    int files = 0, unread = 0;
    for (const QString &sd : siblings) {
        const QDir dir(sd);
        bool any = false;
        for (const QString &sw : dir.entryList({ QStringLiteral("swing_*") }, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            const QString swingDir = dir.filePath(sw);
            QFile f(QDir(swingDir).filePath(QStringLiteral("swing_phasegrid.json")));
            if (!f.open(QIODevice::ReadOnly)) continue;
            ++files;
            const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
            bool ok = false;
            DeliveryShotInput in = deliveryInputFromPhaseGrid(root, swingDir, &ok);
            if (!ok) { ++unread; continue; }
            if (!in.hasPlane() && !in.hasMonitor()) continue;
            in.session = QFileInfo(sd).fileName();
            in.thisSession = false;
            if (in.ordinal <= 0) in.ordinal = SessionSeriesModel::folderShotId(swingDir);
            in.shotId = SessionSeriesModel::folderShotId(swingDir);
            const QJsonObject src = root.value(QStringLiteral("source")).toObject();
            for (const QString &doc : { QStringLiteral("swing.ppsw"), QStringLiteral("swing.json") }) {
                const QFileInfo di(QDir(swingDir).filePath(doc));
                if (!di.exists()) continue;
                if (di.size() != src.value(QStringLiteral("size")).toVariant().toLongLong()
                    || di.lastModified().toMSecsSinceEpoch() != src.value(QStringLiteral("mtime_ms")).toVariant().toLongLong())
                    ++day.staleGrids;
                break;
            }
            day.others.push_back(std::move(in));
            any = true;
        }
        if (any) ++day.sessions;
    }
    ppInfo() << "[Charts] delivery day" << date << ":" << siblings.size() << "other sessions," << files << "phase grids,"
             << day.others.size() << "shots read," << unread << "of another schema," << day.staleGrids << "older than their swing,"
             << t.elapsed() << "ms";
    return day;
}

// The day, through the worker's cache: re-read only when the day's sibling folders change.
DeliveryDay dayOf(ChartsDeliveryPresenter::Worker *w, const QString &sessionDir)
{
    QString date;
    const QStringList siblings = sessionDir.isEmpty() ? QStringList{} : daySiblings(sessionDir, &date);
    DeliveryDay day;
    day.sessions = 0;   // the other sessions with a reading; this one is added below
    if (!siblings.isEmpty()) {
        const QString stamp = dayStamp(siblings);
        if (w) {
            auto it = w->days.find(sessionDir);
            if (it == w->days.end() || it->stamp != stamp) {
                if (w->days.size() > 8) w->days.clear();
                it = w->days.insert(sessionDir, DayEntry{ stamp, readDay(siblings, date) });
            }
            day = it->day;
        } else {
            day = readDay(siblings, date);
        }
    }
    day.date = date;
    day.sessions += 1;   // this session
    return day;
}

// ── the job's context ───────────────────────────────────────────────────────────────────────

struct Ctx {
    const ChartsDeliveryPresenter::Inputs &in;
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

// ── maps ────────────────────────────────────────────────────────────────────────────────────

QString xLabel(DeliveryX x) { return x == DeliveryX::Steepening ? QStringLiteral("Hand plane steepening") : QStringLiteral("Swing plane at P6"); }
QString yLabel(DeliveryY y) { return y == DeliveryY::Attack ? QStringLiteral("Attack angle (monitor)") : QStringLiteral("Club path (monitor)"); }

QVariantMap fitMap(const DeliveryFit &f)
{
    return { { QStringLiteral("key"), deliveryYKey(f.y) + QStringLiteral("~") + deliveryXKey(f.x) },
             { QStringLiteral("x"), deliveryXKey(f.x) }, { QStringLiteral("y"), deliveryYKey(f.y) },
             { QStringLiteral("xLabel"), xLabel(f.x) }, { QStringLiteral("yLabel"), yLabel(f.y) },
             { QStringLiteral("n"), f.n }, { QStringLiteral("slope"), nv(f.slope) }, { QStringLiteral("intercept"), nv(f.intercept) },
             { QStringLiteral("r"), nv(f.r) }, { QStringLiteral("sd"), nv(f.sd) },
             { QStringLiteral("slopeText"), W::fig(f.slope, 2) }, { QStringLiteral("rText"), W::fig(f.r, 2) },
             { QStringLiteral("sdText"), fin(f.sd) ? QStringLiteral("±") + deliveryDeg1(f.sd) : QString() },
             { QStringLiteral("holds"), f.holds }, { QStringLiteral("verdict"), f.verdict }, { QStringLiteral("why"), f.why } };
}

// The scatter: the fit's own points (the day's shots with both readings, this session's marked), the picked one ringed.
QVariantMap scatterMap(const DeliveryFit &f, const std::vector<DeliveryShotInput> &inputs, const DeliveryShotInput *pick)
{
    QVariantList pts;
    double xl = kNaN, xh = kNaN, yl = kNaN, yh = kNaN;
    const auto span = [](double &lo, double &hi, double v) { if (!fin(v)) return; lo = fin(lo) ? std::min(lo, v) : v; hi = fin(hi) ? std::max(hi, v) : v; };
    const QString pickDir = pick ? cleanDir(pick->swingDir) : QString();
    for (const DeliveryShotInput &s : inputs) {
        const double x = deliveryXOf(s, f.x), y = deliveryYOf(s, f.y);
        if (!fin(x) || !fin(y)) continue;
        span(xl, xh, x); span(yl, yh, y);
        pts.append(QVariantMap{ { QStringLiteral("ordinal"), s.ordinal }, { QStringLiteral("swingDir"), s.swingDir },
                                { QStringLiteral("x"), x }, { QStringLiteral("y"), y },
                                { QStringLiteral("thisSession"), s.thisSession }, { QStringLiteral("session"), s.session },
                                { QStringLiteral("picked"), !pickDir.isEmpty() && cleanDir(s.swingDir) == pickDir } });
    }
    QVariant picked = nullv();
    if (pick) {
        const double x = deliveryXOf(*pick, f.x), y = deliveryYOf(*pick, f.y);
        if (fin(x)) { span(xl, xh, x); span(yl, yh, y); picked = QVariantMap{ { QStringLiteral("x"), x }, { QStringLiteral("y"), nv(y) } }; }
    }
    QVariant line = nullv();
    if (f.holds && fin(xl) && fin(xh))
        line = QVariantMap{ { QStringLiteral("x0"), xl }, { QStringLiteral("y0"), f.intercept + f.slope * xl },
                            { QStringLiteral("x1"), xh }, { QStringLiteral("y1"), f.intercept + f.slope * xh } };
    return { { QStringLiteral("xLabel"), xLabel(f.x) }, { QStringLiteral("yLabel"), yLabel(f.y) }, { QStringLiteral("unit"), QStringLiteral("°") },
             { QStringLiteral("holds"), f.holds }, { QStringLiteral("points"), pts }, { QStringLiteral("picked"), picked },
             { QStringLiteral("line"), line }, { QStringLiteral("xLo"), nv(xl) }, { QStringLiteral("xHi"), nv(xh) },
             { QStringLiteral("yLo"), nv(yl) }, { QStringLiteral("yHi"), nv(yh) } };
}

QVariantMap flightMap(bool available, const QString &text, const QString &attackText, const QString &thisLine,
                      const DeliveryFits &fits, const DeliveryPlace &place,
                      const std::vector<DeliveryShotInput> &inputs, const DeliveryDay &day, const DeliveryShotInput *pick)
{
    const DeliveryFit &f = fits.path();
    const std::vector<DeliveryShotInput> pool = deliveryPool(inputs, &day);
    return { { QStringLiteral("available"), available }, { QStringLiteral("text"), text },
             { QStringLiteral("pool"), QVariantMap{ { QStringLiteral("date"), day.date }, { QStringLiteral("sessions"), day.sessions },
                                                    { QStringLiteral("shots"), int(pool.size()) },
                                                    { QStringLiteral("otherShots"), int(day.others.size()) },
                                                    { QStringLiteral("words"), deliveryPoolWords(&day) },
                                                    { QStringLiteral("staleGrids"), day.staleGrids } } },
             { QStringLiteral("attackText"), attackText }, { QStringLiteral("thisLine"), thisLine },
             { QStringLiteral("fit"), fitMap(f) }, { QStringLiteral("attack"), fitMap(fits.attackP6) },
             { QStringLiteral("fits"), QVariantMap{ { QStringLiteral("pathP6"), fitMap(fits.pathP6) },
                                                    { QStringLiteral("pathSteep"), fitMap(fits.pathSteep) },
                                                    { QStringLiteral("attackP6"), fitMap(fits.attackP6) } } },
             { QStringLiteral("place"), QVariantMap{ { QStringLiteral("known"), place.known }, { QStringLiteral("third"), place.third },
                                                     { QStringLiteral("words"), place.words } } },
             { QStringLiteral("scatter"), scatterMap(f, pool, pick) } };
}

QVariantMap shapeMap(const DeliveryShape &s)
{
    return { { QStringLiteral("available"), s.available }, { QStringLiteral("face"), nv(s.face) },
             { QStringLiteral("faceToPath"), nv(s.faceToPath) }, { QStringLiteral("faceText"), deliveryDeg1(s.face) },
             { QStringLiteral("faceToPathText"), deliveryDeg1(s.faceToPath) }, { QStringLiteral("fromBall"), s.fromBall },
             { QStringLiteral("launchDirText"), deliveryDeg1(s.launchDir) }, { QStringLiteral("spinAxisText"), deliveryDeg1(s.spinAxis) },
             { QStringLiteral("start"), s.start },
             { QStringLiteral("curve"), s.curve }, { QStringLiteral("slight"), s.slight },
             { QStringLiteral("startWord"), s.startWord }, { QStringLiteral("curveWord"), s.curveWord }, { QStringLiteral("word"), s.word } };
}

QVariantMap impactMap(const std::vector<DeliveryImpactLine> &lines)
{
    QVariantList out;
    for (const DeliveryImpactLine &l : lines)
        out.append(QVariantMap{ { QStringLiteral("key"), l.key }, { QStringLiteral("text"), l.text }, { QStringLiteral("source"), l.source },
                                { QStringLiteral("value"), nv(l.value) }, { QStringLiteral("face"), nv(l.face) },
                                { QStringLiteral("loft"), nv(l.loft) }, { QStringLiteral("loftSource"), l.loftSource } });
    return { { QStringLiteral("available"), !out.isEmpty() }, { QStringLiteral("lines"), out } };
}

QVariantMap trustMap(const DeliveryTrust &t) { return { { QStringLiteral("lines"), t.lines } }; }

QVariantMap storyMap(const DeliveryStory &s, const DeliveryShotInput &in, const std::array<qint64, kPos> &us)
{
    QVariantList pos;
    for (int p = 0; p < kPos; ++p)
        pos.append(QVariantMap{ { QStringLiteral("index"), p }, { QStringLiteral("tag"), deliveryPositionTag(p) },
                                { QStringLiteral("phase"), deliveryPhaseIds()[size_t(p)] }, { QStringLiteral("name"), deliveryPositionWhen(p) },
                                { QStringLiteral("value"), nv(in.plane[size_t(p)]) }, { QStringLiteral("text"), deliveryDeg(in.plane[size_t(p)]) },
                                { QStringLiteral("hands"), nv(in.hands[size_t(p)]) }, { QStringLiteral("handsText"), deliveryDeg(in.hands[size_t(p)]) },
                                { QStringLiteral("tUs"), qlonglong(us[size_t(p)]) }, { QStringLiteral("seekUs"), qlonglong(us[size_t(p)]) } });
    return { { QStringLiteral("available"), s.available }, { QStringLiteral("text"), s.text }, { QStringLiteral("parts"), s.parts },
             { QStringLiteral("set"), s.set }, { QStringLiteral("back"), s.back }, { QStringLiteral("top"), s.top },
             { QStringLiteral("delivered"), s.delivered }, { QStringLiteral("hands"), s.hands },
             { QStringLiteral("shapeRead"), s.shapeRead }, { QStringLiteral("shapeWord"), s.shapeWord },
             { QStringLiteral("shapeLine"), s.shapeLine }, { QStringLiteral("positions"), pos } };
}

QVariantMap slotMap(const DeliverySlot &s)
{
    QVariantList out;
    for (const DeliverySlotMark &m : s.outside)
        out.append(QVariantMap{ { QStringLiteral("index"), m.pos }, { QStringLiteral("tag"), deliveryPositionTag(m.pos) },
                                { QStringLiteral("side"), m.side }, { QStringLiteral("words"), m.words } });
    const bool both = fin(s.address) && fin(s.shoulders);
    return { { QStringLiteral("available"), s.available }, { QStringLiteral("address"), nv(s.address) },
             { QStringLiteral("shoulders"), nv(s.shoulders) }, { QStringLiteral("addressText"), deliveryDeg(s.address) },
             { QStringLiteral("shouldersText"), deliveryDeg(s.shoulders) },
             { QStringLiteral("widthText"), both ? deliveryDeg(std::round(s.shoulders) - std::round(s.address)) + QStringLiteral(" wide") : QString() },
             { QStringLiteral("outside"), out }, { QStringLiteral("text"), s.text } };
}

// One standfirst per position: what the plane was there, against where it was set and the slot.
QVariantList standfirstsOf(const DeliveryShotInput &in)
{
    QVariantList out;
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
                          .append(QStringLiteral(": a slot ")).figure(deliveryDeg(std::round(sh) - std::round(v))).append(QStringLiteral(" wide."));
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
        out.append(s.toVariant());
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

// ── the sections ────────────────────────────────────────────────────────────────────────────

void buildThisShot(Ctx &c, ChartsDeliveryPresenter::Result &R)
{
    QVariantMap out;
    R.posUs = c.pUs;
    const bool has = c.shot && c.view.available;
    out.insert(QStringLiteral("available"), has);
    if (!has) {
        out.insert(QStringLiteral("empty"), (c.shot || !c.in.pickedSwingDir.trimmed().isEmpty()
                                                 ? QStringLiteral("No swing plane or launch monitor reading for this swing.")
                                                 : QStringLiteral("Select a swing to review.")) + emptyTail());
        R.thisShot = out;
        R.standfirsts.clear();
        return;
    }
    const ShotSeries &s = *c.shot;
    const DeliveryShotView &v = c.view;
    const bool inSession = c.index >= 0;
    out.insert(QStringLiteral("empty"), QString());
    out.insert(QStringLiteral("ordinal"), s.ordinal);
    out.insert(QStringLiteral("shotId"), s.shotId);
    out.insert(QStringLiteral("swingDir"), s.swingDir);
    out.insert(QStringLiteral("inSession"), inSession);
    const QString club = s.club.trimmed().isEmpty() ? QString() : deliveryClubWords(s.club);
    out.insert(QStringLiteral("kicker"), (inSession ? QStringLiteral("Shot %1 of %2").arg(s.ordinal).arg(c.n) : QStringLiteral("This swing"))
                                             + (club.isEmpty() ? QString() : QStringLiteral("  ·  ") + club) + QStringLiteral("  ·  Club delivery"));
    out.insert(QStringLiteral("mode"), v.mode);
    out.insert(QStringLiteral("hasPlane"), v.hasPlane);
    out.insert(QStringLiteral("hasMonitor"), v.hasMonitor);
    out.insert(QStringLiteral("head"), W::Sentence().append(v.head.text).toVariant());
    out.insert(QStringLiteral("headText"), v.head.text);
    out.insert(QStringLiteral("headRule"), v.head.rule);
    out.insert(QStringLiteral("story"), storyMap(v.story, c.pick, c.pUs));
    out.insert(QStringLiteral("slot"), slotMap(v.slot));
    out.insert(QStringLiteral("flight"), flightMap(v.flightAvailable, v.flightText, v.attackText, v.thisLine, v.fits, v.place,
                                                   c.inputs, c.day, &c.pick));
    out.insert(QStringLiteral("impact"), impactMap(v.impact));
    out.insert(QStringLiteral("shape"), shapeMap(v.shape));
    out.insert(QStringLiteral("trust"), trustMap(v.trust));
    out.insert(QStringLiteral("replay"), QVariantMap{ { QStringLiteral("head"), v.head.text },
                                                      { QStringLiteral("story"), v.story.available ? v.story.text : QString() },
                                                      { QStringLiteral("shape"), v.shape.available ? v.shape.word : QString() } });
    R.standfirsts = v.hasPlane ? standfirstsOf(c.pick) : QVariantList{};
    out.insert(QStringLiteral("standfirsts"), R.standfirsts);
    R.thisShot = out;
}

// The session map (Charts' Session, and the report's page with no picked shot).
QVariantMap sessionMapOf(const std::vector<DeliveryShotInput> &inputs, const DeliverySessionView &sv, const DeliveryDay &day,
                         int pickedIndex, const DeliveryShotInput *pick, const QString &noun = QStringLiteral("shots"))
{
    QVariantMap out;
    const int n = int(inputs.size());
    out.insert(QStringLiteral("available"), sv.available);
    out.insert(QStringLiteral("n"), n);
    out.insert(QStringLiteral("shots"), sv.shots);
    out.insert(QStringLiteral("planeShots"), sv.planeShots);
    out.insert(QStringLiteral("monitorShots"), sv.monitorShots);
    out.insert(QStringLiteral("small"), sv.shots < 10);
    if (!sv.available) return out;
    const QString clubs = clubOf(inputs);
    out.insert(QStringLiteral("kicker"), QStringLiteral("%1 %2").arg(sv.shots).arg(noun)
                                             + (clubs.isEmpty() ? QString() : QStringLiteral("  ·  ") + clubs) + QStringLiteral("  ·  Club delivery"));
    out.insert(QStringLiteral("head"), W::Sentence().append(sv.head).toVariant());
    out.insert(QStringLiteral("headText"), sv.head);

    QVariantList pos;
    for (int p = 0; p < kPos; ++p) {
        const box::BoxStats &b = sv.band.boxes[size_t(p)];
        QVariantList values;
        for (const DeliveryShotInput &s : inputs) values.append(nv(s.plane[size_t(p)]));
        pos.append(QVariantMap{ { QStringLiteral("index"), p }, { QStringLiteral("tag"), deliveryPositionTag(p) },
                                { QStringLiteral("name"), deliveryPositionWhen(p) },
                                { QStringLiteral("box"), boxMap(b, [](double v) { return deliveryDeg(v); }) },
                                { QStringLiteral("median"), nv(b.med) }, { QStringLiteral("medianText"), deliveryDeg(b.med) },
                                { QStringLiteral("wide"), nv(b.iqr) }, { QStringLiteral("half"), nv(fin(b.iqr) ? b.iqr / 2 : kNaN) },
                                { QStringLiteral("halfText"), fin(b.iqr) ? QStringLiteral("±") + deliveryDeg1(b.iqr / 2) : QString() },
                                { QStringLiteral("picked"), pick ? nv(pick->plane[size_t(p)]) : nullv() },
                                { QStringLiteral("values"), values } });
    }
    out.insert(QStringLiteral("band"), QVariantMap{ { QStringLiteral("shown"), sv.band.shown }, { QStringLiteral("n"), sv.band.n },
                                                    { QStringLiteral("positions"), pos } });
    out.insert(QStringLiteral("varies"), QVariantMap{
        { QStringLiteral("has"), sv.varies.has },
        { QStringLiteral("tightest"), sv.varies.has ? deliveryPositionTag(sv.varies.tightest) : QString() },
        { QStringLiteral("loosest"), sv.varies.has ? deliveryPositionTag(sv.varies.loosest) : QString() },
        { QStringLiteral("tightHalf"), nv(sv.varies.tightHalf) }, { QStringLiteral("looseHalf"), nv(sv.varies.looseHalf) },
        { QStringLiteral("words"), sv.varies.words } });
    out.insert(QStringLiteral("usual"), QVariantMap{
        { QStringLiteral("address"), nv(sv.usual.plane[0]) }, { QStringLiteral("shoulders"), nv(sv.usual.shoulderPlane) },
        { QStringLiteral("steepening"), nv(sv.usual.steepening) }, { QStringLiteral("addressText"), deliveryDeg(sv.usual.plane[0]) },
        { QStringLiteral("shouldersText"), deliveryDeg(sv.usual.shoulderPlane) },
        { QStringLiteral("steepeningText"), W::figSigned(sv.usual.steepening, QStringLiteral("°")) } });
    DeliveryPlace none;
    out.insert(QStringLiteral("flight"), flightMap(sv.fits.path().n > 0, sv.flightText, sv.attackText, QString(), sv.fits, none,
                                                   inputs, day, pick));
    QVariantList marks;
    for (size_t i = 0; i < inputs.size() && i < sv.shapes.size(); ++i) {
        const DeliveryShape &sh = sv.shapes[i];
        marks.append(QVariantMap{ { QStringLiteral("ordinal"), inputs[i].ordinal }, { QStringLiteral("swingDir"), inputs[i].swingDir },
                                  { QStringLiteral("available"), sh.available }, { QStringLiteral("start"), sh.start },
                                  { QStringLiteral("curve"), sh.curve }, { QStringLiteral("word"), sh.word },
                                  { QStringLiteral("picked"), int(i) == pickedIndex } });
    }
    out.insert(QStringLiteral("shapes"), QVariantMap{ { QStringLiteral("marks"), marks }, { QStringLiteral("line"), sv.shapeLine } });
    out.insert(QStringLiteral("trust"), trustMap(sv.trust));
    return out;
}

// The plot's lines over the swing plane curve's row (§4.2): the picked shot's own (This shot), else the session's medians.
QVariantMap plotLayersOf(Ctx &c)
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
    QVariantList layers;
    if (fin(address))
        layers.append(QVariantMap{ { QStringLiteral("k"), QStringLiteral("hline") }, { QStringLiteral("v"), address },
                                   { QStringLiteral("role"), QStringLiteral("address") }, { QStringLiteral("label"), QStringLiteral("address") },
                                   { QStringLiteral("text"), deliveryDeg(address) } });
    if (fin(shoulders))
        layers.append(QVariantMap{ { QStringLiteral("k"), QStringLiteral("hline") }, { QStringLiteral("v"), shoulders },
                                   { QStringLiteral("role"), QStringLiteral("shoulders") }, { QStringLiteral("label"), QStringLiteral("shoulders") },
                                   { QStringLiteral("text"), deliveryDeg(shoulders) } });
    if (fin(address) && fin(shoulders) && fin(t0) && fin(t1))
        layers.append(QVariantMap{ { QStringLiteral("k"), QStringLiteral("band") }, { QStringLiteral("t"), QVariantList{ t0, t1 } },
                                   { QStringLiteral("lo"), QVariantList{ std::min(address, shoulders), std::min(address, shoulders) } },
                                   { QStringLiteral("hi"), QVariantList{ std::max(address, shoulders), std::max(address, shoulders) } },
                                   { QStringLiteral("role"), QStringLiteral("slot") }, { QStringLiteral("label"), QStringLiteral("the slot") } });
    for (size_t i = 0; i < spans.size(); ++i)
        layers.append(QVariantMap{ { QStringLiteral("k"), QStringLiteral("span") }, { QStringLiteral("t0"), spans[i].t0Ms },
                                   { QStringLiteral("t1"), spans[i].t1Ms }, { QStringLiteral("role"), QStringLiteral("hands") },
                                   { QStringLiteral("label"), QStringLiteral("hands carry the plane here") },
                                   { QStringLiteral("labelled"), i == 0 } });
    if (layers.isEmpty()) return {};
    return { { QStringLiteral("swingPlaneCurve"), layers } };
}

void buildChoices(Ctx &c, ChartsDeliveryPresenter::Result &R)
{
    const QString scope = c.in.scope == QLatin1String("session") ? QStringLiteral("session") : QStringLiteral("shot");
    R.choices = {
        { QStringLiteral("scope"), scope },
        { QStringLiteral("scopes"), QVariantList{
              QVariantMap{ { QStringLiteral("id"), QStringLiteral("shot") }, { QStringLiteral("label"), QStringLiteral("This shot") }, { QStringLiteral("current"), scope == QLatin1String("shot") } },
              QVariantMap{ { QStringLiteral("id"), QStringLiteral("session") }, { QStringLiteral("label"), QStringLiteral("Session") }, { QStringLiteral("current"), scope == QLatin1String("session") } } } },
        { QStringLiteral("onShowKeys"), deliveryOnShowKeys() } };
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
        c.pick = deliveryInputFromShot(*sh);
        c.shot = sh;
    }
    c.hasPick = c.shot && hasDelivery(c.pick);

    c.day = dayOf(&w, sessionDirOf(*c.snap, want));
    c.sess = deliverySession(c.inputs, QStringLiteral("shots"), &c.day);
    if (c.shot) c.view = deliveryShot(c.pick, c.inputs, &c.day);
    c.pUs = positionUs(c.shot.get());

    R->available = c.ready > 0 || c.hasPick;
    buildThisShot(c, *R);
    R->session = sessionMapOf(c.inputs, c.sess, c.day, c.index, c.shot ? &c.pick : nullptr);
    R->plotLayers = plotLayersOf(c);
    buildChoices(c, *R);
    return R;
}

QVariantMap deliverySessionReportPayload(const SessionSnapshot &snapshot, const QString &noun)
{
    const QString many = noun.trimmed().isEmpty() ? QStringLiteral("shots") : noun.trimmed();
    const std::vector<DeliveryShotInput> inputs = readInputs(nullptr, snapshot);
    const DeliveryDay day = dayOf(nullptr, sessionDirOf(snapshot, QString()));
    const DeliverySessionView sv = deliverySession(inputs, many, &day);
    return sessionMapOf(inputs, sv, day, -1, nullptr, many);
}

// ── the object ──────────────────────────────────────────────────────────────────────────────

ChartsDeliveryPresenter::ChartsDeliveryPresenter(QObject *parent)
    : QObject(parent), m_worker(std::make_shared<Worker>())
{
    m_pool.setMaxThreadCount(1);
    m_pool.setObjectName(QStringLiteral("ChartsDeliveryPresenter"));
    schedule();
}

ChartsDeliveryPresenter::~ChartsDeliveryPresenter()
{
    m_pool.waitForDone();
}

QObject *ChartsDeliveryPresenter::model() const { return m_model.data(); }

void ChartsDeliveryPresenter::setModel(QObject *m)
{
    if (m_model == m) return;
    if (m_model) disconnect(m_model, nullptr, this, nullptr);
    m_model = m;
    if (auto *sm = qobject_cast<SessionSeriesModel *>(m))
        connect(sm, &SessionSeriesModel::snapshotChanged, this, &ChartsDeliveryPresenter::schedule);
    emit inputsChanged();
    schedule();
}

std::shared_ptr<const SessionSnapshot> ChartsDeliveryPresenter::currentSnapshot() const
{
    if (auto *sm = qobject_cast<SessionSeriesModel *>(m_model.data())) return sm->snapshot();
    return m_directSnapshot;
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
QVariantMap ChartsDeliveryPresenter::choices() { flushIfSynchronous(); return m_result ? m_result->choices : QVariantMap{}; }
QVariantMap ChartsDeliveryPresenter::thisShot() { flushIfSynchronous(); return m_result ? m_result->thisShot : QVariantMap{}; }
QVariantMap ChartsDeliveryPresenter::session() { flushIfSynchronous(); return m_result ? m_result->session : QVariantMap{}; }
QVariantMap ChartsDeliveryPresenter::plotLayers() { flushIfSynchronous(); return m_result ? m_result->plotLayers : QVariantMap{}; }

QVariantMap ChartsDeliveryPresenter::standfirst() const
{
    if (!m_result || m_selected < 0 || m_selected >= m_result->standfirsts.size()) return {};
    return m_result->standfirsts.at(m_selected).toMap();
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

void ChartsDeliveryPresenter::schedule()
{
    m_dirty = true;
    if (m_synchronous || m_scheduled) return;
    m_scheduled = true;
    QMetaObject::invokeMethod(this, &ChartsDeliveryPresenter::startJob, Qt::QueuedConnection);
}

void ChartsDeliveryPresenter::startJob()
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
        auto r = computeDelivery(*w, in);
        QMetaObject::invokeMethod(this, [this, r]() { apply(r); }, Qt::QueuedConnection);
    });
}

void ChartsDeliveryPresenter::apply(std::shared_ptr<Result> r)
{
    const bool wasRunning = m_running;
    m_running = false;
    m_result = std::move(r);
    emit resultsChanged();
    updateSelected(true);
    if (m_dirty && !m_synchronous) startJob();
    else if (wasRunning) emit busyChanged();
}

void ChartsDeliveryPresenter::flushIfSynchronous()
{
    if (!m_synchronous || !m_dirty) return;
    m_dirty = false;
    Inputs in = m_in;
    in.snapshot = currentSnapshot();
    m_result = computeDelivery(*m_worker, in);
    updateSelected(true);
}

void ChartsDeliveryPresenter::setSynchronous(bool on)
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

bool ChartsDeliveryPresenter::waitForIdle(int msTimeout)
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
