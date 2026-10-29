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

#include "session_delivery.h"

#include "charts_words.h"
#include "../../Diagnostics/measures/measure_sample.h"   // buildPhaseGrid: THE sampler

#include <QHash>
#include <QJsonArray>
#include <QFileInfo>
#include <QJsonObject>
#include <QLocale>
#include <QDate>
#include <QRegularExpression>

#include <algorithm>
#include <map>

namespace pinpoint::charts {

namespace pa = pinpoint::analysis;
namespace W  = words;

namespace {

constexpr int kP1 = 0, kP2 = 1, kP3 = 2, kP4 = 3, kP5 = 4, kP6 = 5, kP7 = 6;
const QString kDot = QStringLiteral(" · ");
constexpr double kPi = 3.14159265358979323846;

bool fin(double v) { return std::isfinite(v); }
double med(std::vector<double> v)
{
    v.erase(std::remove_if(v.begin(), v.end(), [](double x) { return !fin(x); }), v.end());
    return v.empty() ? kNaN : box::quantile7(std::move(v), 0.5);
}
QString absDeg(double v) { return deliveryDeg(std::fabs(v)); }
QString absDeg1(double v) { return deliveryDeg1(std::fabs(v)); }
QString steeperWord(double d) { return d >= 0 ? QStringLiteral("steeper") : QStringLiteral("flatter"); }
// "the shots" → "shot" for one
QString one(const QString &noun) { return noun.endsWith(QLatin1Char('s')) ? noun.chopped(1) : noun; }

// The curve and its validity on the shot's own clock, ms from impact.
void curveOf(const ShotSeries &shot, const QString &key, std::vector<double> &tMs, std::vector<double> &v,
             std::vector<uint8_t> &valid)
{
    tMs.clear(); v.clear(); valid.clear();
    const MetricSeries *m = shot.metric(key);
    if (!m || !m->hasCurve()) return;
    const qint64 zero = shot.impactUs >= 0 ? shot.impactUs : 0;
    for (size_t i = 0; i < m->tUs.size() && i < m->value.size(); ++i) {
        tMs.push_back(double(m->tUs[i] - zero) / 1000.0);
        v.push_back(m->value[i]);
        valid.push_back(m->isValid(i) && fin(m->value[i]) ? 1 : 0);
    }
}

// The hands' plane at t, linear between its two neighbouring measured samples; NaN outside or across a bridge.
double handAt(const std::vector<double> &t, const std::vector<double> &v, const std::vector<uint8_t> &valid, double x)
{
    if (t.empty()) return kNaN;
    const auto it = std::lower_bound(t.begin(), t.end(), x);
    const size_t j = size_t(it - t.begin());
    const auto ok = [&](size_t i) { return i < v.size() && fin(v[i]) && (valid.empty() || (i < valid.size() && valid[i])); };
    if (j < t.size() && std::fabs(t[j] - x) < 1e-6) return ok(j) ? v[j] : kNaN;
    if (j == 0 || j >= t.size() || !ok(j - 1) || !ok(j)) return kNaN;
    const double a = (x - t[j - 1]) / std::max(1e-9, t[j] - t[j - 1]);
    return v[j - 1] + a * (v[j] - v[j - 1]);
}

// The plane story's readings, by name (decision 6), and their sentence against the usual.
struct StoryReading { const char *name; int pos; };   // pos −1 = the steepening
const std::array<StoryReading, 6> kStoryReadings{ { { "P1", kP1 }, { "P3", kP3 }, { "P4", kP4 }, { "P6", kP6 },
                                                    { "P7", kP7 }, { "steepening", -1 } } };
QString usualWords(int pos, double d)
{
    const QString fig = absDeg(d);
    switch (pos) {
    case kP1: return QStringLiteral("You set up %1 %2 than your usual.").arg(fig, steeperWord(d));
    case kP3: return QStringLiteral("You went back %1 %2 than your usual at P3.").arg(fig, steeperWord(d));
    case kP4: return QStringLiteral("You reached the top %1 %2 than your usual.").arg(fig, steeperWord(d));
    case kP6: return QStringLiteral("You came down %1 %2 than your usual.").arg(fig, steeperWord(d));
    case kP7: return QStringLiteral("You delivered %1 %2 than your usual.").arg(fig, steeperWord(d));
    default:  return QStringLiteral("Your hands steepened %1 %2 than your usual coming down.")
                     .arg(fig, d >= 0 ? QStringLiteral("more") : QStringLiteral("less"));
    }
}

// "Your hands came down 12° steeper than they went up" (the head's form of the hands' clause).
QString handsHead(double st)
{
    if (st >= kDeliveryHandsDeg) return QStringLiteral("Your hands came down %1 steeper than they went up").arg(absDeg(st));
    if (st <= -kDeliveryHandsDeg) return QStringLiteral("Your hands came down %1 flatter than they went up").arg(absDeg(st));
    return QStringLiteral("Your hands came down on the plane they went up");
}

// What a fit's reading is called, in the refusal's words.
QString xNoun(DeliveryX x)
{
    return x == DeliveryX::Steepening ? QStringLiteral("how much your hands steepen") : QStringLiteral("your plane");
}

} // namespace

// ── vocabulary ──────────────────────────────────────────────────────────────────────────────────

const std::array<int, kDeliveryPositions> &deliveryPhaseIds()
{
    static const std::array<int, kDeliveryPositions> ids{
        int(pa::Phase::Address), int(pa::Phase::ShaftParallelBack), int(pa::Phase::MidBackswing), int(pa::Phase::Top),
        int(pa::Phase::ArmParallelDown), int(pa::Phase::Delivery), int(pa::Phase::Impact) };
    return ids;
}

QString deliveryPositionTag(int pos) { return QStringLiteral("P%1").arg(pos + 1); }

QString deliveryPositionWhen(int pos)
{
    switch (pos) {
    case kP1: return QStringLiteral("address");
    case kP4: return QStringLiteral("the top");
    case kP7: return QStringLiteral("impact");
    default:  return deliveryPositionTag(pos);
    }
}

double deliveryNominalLoft(const QString &club)
{
    // athlete_controller.cpp defaultClubRecordFor: the factory lofts every new bag starts with.
    static const QHash<QString, double> lofts{
        { QStringLiteral("DRIVER"), 10.0 }, { QStringLiteral("3 WOOD"), 15.0 }, { QStringLiteral("5 WOOD"), 18.0 },
        { QStringLiteral("3 HYBRID"), 21.0 }, { QStringLiteral("4 HYBRID"), 24.0 },
        { QStringLiteral("3 IRON"), 21.0 }, { QStringLiteral("4 IRON"), 24.0 }, { QStringLiteral("5 IRON"), 27.0 },
        { QStringLiteral("6 IRON"), 30.0 }, { QStringLiteral("7 IRON"), 34.0 }, { QStringLiteral("8 IRON"), 38.0 },
        { QStringLiteral("9 IRON"), 42.0 }, { QStringLiteral("PITCHING WEDGE"), 46.0 }, { QStringLiteral("GAP WEDGE"), 52.0 },
        { QStringLiteral("SAND WEDGE"), 56.0 }, { QStringLiteral("LOB WEDGE"), 60.0 } };
    QString k = club.trimmed().toUpper();
    k.replace(QLatin1Char('-'), QLatin1Char(' '));
    return lofts.value(k, kNaN);
}

QString deliveryClubWords(const QString &club)
{
    QString k = club.trimmed().toLower();
    if (k.isEmpty()) return QStringLiteral("club");
    k.replace(QRegularExpression(QStringLiteral("^(\\d) (iron|wood|hybrid)$")), QStringLiteral("\\1-\\2"));
    return k;
}

const QStringList &deliveryMetricKeys()
{
    static const QStringList k{
        QStringLiteral("swingPlaneCurve"), QStringLiteral("handPlaneCurve"), QStringLiteral("handPlaneSteepening"),
        QStringLiteral("shoulderPlane"), QStringLiteral("shaftLie"), QStringLiteral("impactShaftLean"),
        QStringLiteral("lowPointAhead"), QStringLiteral("clubPath"), QStringLiteral("attackAngle"),
        QStringLiteral("lm.clubPath"), QStringLiteral("lm.faceAngle"), QStringLiteral("lm.faceToPath"),
        QStringLiteral("lm.attackAngle"), QStringLiteral("lm.dynamicLoft"), QStringLiteral("lm.lieAngle"),
        QStringLiteral("lm.lowPointAhead") };
    return k;
}

const QStringList &deliveryOnShowKeys()
{
    static const QStringList k{
        QStringLiteral("swingPlaneCurve"), QStringLiteral("handPlaneCurve"), QStringLiteral("handPlaneSteepening"),
        QStringLiteral("shoulderPlane"),
        QStringLiteral("lm.clubPath"), QStringLiteral("lm.faceAngle"), QStringLiteral("lm.faceToPath"),
        QStringLiteral("lm.attackAngle"), QStringLiteral("lm.dynamicLoft"), QStringLiteral("lm.lieAngle"),
        QStringLiteral("lm.lowPointAhead") };
    return k;
}

QString deliveryDeg(double v) { const QString f = W::fig(v, 0); return f == QLatin1String("-") ? f : f + QStringLiteral("°"); }
QString deliveryDeg1(double v) { const QString f = W::fig(v, 1); return f == QLatin1String("-") ? f : f + QStringLiteral("°"); }

QString deliveryDeg1Signed(double v)
{
    const QString f = deliveryDeg1(v);
    return fin(v) && std::round(v * 10.0) > 0 ? QStringLiteral("+") + f : f;
}

QString deliveryDirection(double v)
{
    if (!fin(v)) return QStringLiteral("-");
    if (std::fabs(v) < 0.05) return QStringLiteral("square");
    return absDeg1(v) + (v < 0 ? QStringLiteral(" left") : QStringLiteral(" right"));
}

// ── one shot's input ────────────────────────────────────────────────────────────────────────────

DeliveryShotInput::DeliveryShotInput()
{
    plane.fill(kNaN);
    hands.fill(kNaN);
    posMs.fill(kNaN);
}

bool DeliveryShotInput::hasPlane() const { return std::any_of(plane.begin(), plane.end(), fin); }

bool DeliveryShotInput::hasMonitor() const
{
    return fin(lmPath) || fin(lmFace) || fin(lmFaceToPath) || fin(lmAttack) || fin(lmLoft) || fin(lmLie) || fin(lmLowPoint);
}

std::vector<DeliverySpan> deliveryHandsSpans(const std::vector<double> &tMs, const std::vector<double> &curve,
                                             const std::vector<uint8_t> &curveValid,
                                             const std::vector<double> &handMs, const std::vector<double> &hand,
                                             const std::vector<uint8_t> &handValid, double fromMs)
{
    std::vector<DeliverySpan> out;
    int start = -1, last = -1;
    const auto close = [&]() {
        if (start >= 0 && tMs[size_t(last)] - tMs[size_t(start)] >= kDeliveryHandsCarryMinMs - 1e-6)
            out.push_back({ tMs[size_t(start)], tMs[size_t(last)] });
        start = last = -1;
    };
    for (size_t i = 0; i < tMs.size() && i < curve.size(); ++i) {
        const bool measured = fin(curve[i]) && (curveValid.empty() || (i < curveValid.size() && curveValid[i]));
        const double h = measured && (!fin(fromMs) || tMs[i] > fromMs) ? handAt(handMs, hand, handValid, tMs[i]) : kNaN;
        const bool on = fin(h) && std::fabs(curve[i] - h) <= kDeliveryHandsCarryDeg;
        if (on) { if (start < 0) start = int(i); last = int(i); }
        else close();
    }
    close();
    return out;
}

pa::SwingPhaseGrid deliveryPhaseGridOf(const ShotSeries &shot)
{
    // The shot's series for the keys, as the analysis block spells them, so the ONE sampler (buildPhaseGrid)
    // reads them exactly as it reads swing.json; a non-finite curve value goes over as a bridged sample.
    QJsonArray phases, metrics;
    for (const PhaseMark &pm : shot.phases)
        phases.append(QJsonObject{ { QStringLiteral("phase"), pm.phase }, { QStringLiteral("t_us"), QJsonValue(qint64(pm.tUs)) } });
    for (const QString &k : deliveryMetricKeys()) {
        const MetricSeries *m = shot.metric(k);
        if (!m) continue;
        QJsonArray t, v, valid;
        bool anyInvalid = false;
        for (size_t i = 0; i < m->tUs.size() && i < m->value.size(); ++i) {
            const bool ok = m->isValid(i) && fin(m->value[i]);
            t.append(QJsonValue(qint64(m->tUs[i])));
            v.append(ok ? m->value[i] : 0.0);
            valid.append(ok ? 1 : 0);
            anyInvalid |= !ok;
        }
        QJsonArray samples;
        for (const PhaseSample &ps : m->phaseSamples) {
            if (!fin(ps.value)) continue;
            QJsonObject o{ { QStringLiteral("phase"), ps.phase }, { QStringLiteral("t_us"), QJsonValue(qint64(ps.tUs)) },
                           { QStringLiteral("value"), ps.value } };
            if (fin(ps.sigma) && ps.sigma > 0.0) o.insert(QStringLiteral("sigma"), ps.sigma);
            samples.append(o);
        }
        QJsonObject mo{ { QStringLiteral("key"), k }, { QStringLiteral("unit"), m->unit },
                        { QStringLiteral("t_us"), t }, { QStringLiteral("value"), v }, { QStringLiteral("phaseSamples"), samples } };
        if (anyInvalid) mo.insert(QStringLiteral("valid"), valid);
        if (fin(m->sigma) && m->sigma > 0.0) {
            mo.insert(QStringLiteral("sigma"), m->sigma);
            if (m->sigmaKind >= 0) mo.insert(QStringLiteral("sigmaKind"), m->sigmaKind);
        }
        metrics.append(mo);
    }
    pa::SwingPhaseGrid grid = pa::buildPhaseGrid(
        QJsonObject{ { QStringLiteral("phases"), phases }, { QStringLiteral("metrics"), metrics } }, pa::PhaseGridConfig{});
    grid.swingDir = shot.swingDir;
    grid.club = shot.club;
    grid.ordinal = shot.ordinal;
    return grid;
}

DeliveryShotInput deliveryInputFromGrid(const pa::SwingPhaseGrid &grid)
{
    DeliveryShotInput in;
    in.ordinal = grid.ordinal; in.swingDir = grid.swingDir; in.club = grid.club; in.session = grid.sessionId;
    in.nominalLoft = deliveryNominalLoft(grid.club);
    const auto at = [&](const QString &key, int phase) {
        const pa::MetricPhaseGrid *g = grid.metric(key);
        const pa::PhaseGridValue *v = g ? g->at(static_cast<pa::Phase>(phase)) : nullptr;
        return v && fin(v->value) ? v->value : kNaN;
    };
    // An instant reading: at its own phase, else the first the grid holds (a phase sample anywhere).
    const auto instant = [&](const QString &key, pa::Phase phase) {
        const double v = at(key, int(phase));
        if (fin(v)) return v;
        if (const pa::MetricPhaseGrid *g = grid.metric(key))
            for (const pa::PhaseGridValue &x : g->values) if (fin(x.value)) return x.value;
        return kNaN;
    };
    for (int p = 0; p < kDeliveryPositions; ++p) {
        const int ph = deliveryPhaseIds()[size_t(p)];
        in.plane[size_t(p)] = at(QStringLiteral("swingPlaneCurve"), ph);
        in.hands[size_t(p)] = at(QStringLiteral("handPlaneCurve"), ph);
    }
    if (const pa::MetricPhaseGrid *g = grid.metric(QStringLiteral("swingPlaneCurve"))) in.planeSigma = g->seriesSigma;
    in.steepening    = instant(QStringLiteral("handPlaneSteepening"), pa::Phase::Top);
    in.shoulderPlane = instant(QStringLiteral("shoulderPlane"), pa::Phase::Address);
    in.lieP1         = at(QStringLiteral("shaftLie"), int(pa::Phase::Address));
    in.lieP7         = at(QStringLiteral("shaftLie"), int(pa::Phase::Impact));
    in.shaftLean     = instant(QStringLiteral("impactShaftLean"), pa::Phase::Impact);
    in.lowPoint      = instant(QStringLiteral("lowPointAhead"), pa::Phase::Impact);
    in.ourPath       = instant(QStringLiteral("clubPath"), pa::Phase::Impact);
    in.ourAttack     = instant(QStringLiteral("attackAngle"), pa::Phase::Impact);
    in.lmPath        = instant(QStringLiteral("lm.clubPath"), pa::Phase::Impact);
    in.lmFace        = instant(QStringLiteral("lm.faceAngle"), pa::Phase::Impact);
    in.lmFaceToPath  = instant(QStringLiteral("lm.faceToPath"), pa::Phase::Impact);
    in.lmAttack      = instant(QStringLiteral("lm.attackAngle"), pa::Phase::Impact);
    in.lmLoft        = instant(QStringLiteral("lm.dynamicLoft"), pa::Phase::Impact);
    in.lmLie         = instant(QStringLiteral("lm.lieAngle"), pa::Phase::Impact);
    in.lmLowPoint    = instant(QStringLiteral("lm.lowPointAhead"), pa::Phase::Impact);
    return in;
}

DeliveryShotInput deliveryInputFromPhaseGrid(const QJsonObject &root, const QString &swingDir, bool *read)
{
    // THE SIDECAR'S OWN LOADER (loadPhaseGrid), handed the sidecar's own guard: the guard against the swing document is
    // the caller's to state (it stats the document, never opens it); a sidecar of another schema or reduction reads
    // as nothing, never as a number.
    const QJsonObject src = root.value(QStringLiteral("source")).toObject();
    pa::PhaseGridConfig cfg;
    bool ok = false;
    pa::SwingPhaseGrid grid = pa::loadPhaseGrid(root, src.value(QStringLiteral("size")).toVariant().toLongLong(),
                                                src.value(QStringLiteral("mtime_ms")).toVariant().toLongLong(), &ok, cfg);
    if (read) *read = ok;
    if (!ok) { DeliveryShotInput none; none.swingDir = swingDir; return none; }
    grid.swingDir = swingDir;
    DeliveryShotInput in = deliveryInputFromGrid(grid);
    in.shotId = in.ordinal;
    return in;
}

DeliveryShotInput deliveryInputFromShot(const ShotSeries &shot)
{
    DeliveryShotInput in = deliveryInputFromGrid(deliveryPhaseGridOf(shot));
    in.ordinal = shot.ordinal; in.shotId = shot.shotId; in.swingDir = shot.swingDir;
    in.session = shot.swingDir.trimmed().isEmpty() ? QString() : QFileInfo(QFileInfo(shot.swingDir).absolutePath()).fileName();
    for (int p = 0; p < kDeliveryPositions; ++p)
        for (const PhaseMark &pm : shot.phases)
            if (pm.phase == deliveryPhaseIds()[size_t(p)]) { in.posMs[size_t(p)] = double(pm.tUs - (shot.impactUs >= 0 ? shot.impactUs : 0)) / 1000.0; break; }

    // The curve as drawn, and where the hands carry it (decision 5).
    std::vector<double> ct, cv, ht, hv;
    std::vector<uint8_t> cok, hok;
    curveOf(shot, QStringLiteral("swingPlaneCurve"), ct, cv, cok);
    curveOf(shot, QStringLiteral("handPlaneCurve"), ht, hv, hok);
    if (!ct.empty()) { in.curveFromMs = ct.front(); in.curveToMs = ct.back(); }
    in.handsSpans = deliveryHandsSpans(ct, cv, cok, ht, hv, hok, in.posMs[kP1]);
    return in;
}

// ── the plane story and the slot ────────────────────────────────────────────────────────────────

DeliveryStory deliveryPlaneStory(const DeliveryShotInput &in)
{
    DeliveryStory s;
    const auto &P = in.plane;
    if (!fin(P[kP1])) return s;
    s.available = true;
    s.set = QStringLiteral("set on %1").arg(deliveryDeg(P[kP1]));
    s.parts << s.set;
    if (fin(P[kP3])) {
        const double d = P[kP3] - P[kP1];
        s.back = std::fabs(d) >= kDeliveryGoingBackDeg ? QStringLiteral("%1 %2 at P3").arg(absDeg(d), steeperWord(d))
                                                       : QStringLiteral("on that plane at P3");
        s.parts << s.back;
    }
    // decision 2: the maximum of P2..P5
    int top = -1, read = 0;
    for (int p = kP2; p <= kP5; ++p) {
        if (!fin(P[size_t(p)])) continue;
        ++read;
        if (top < 0 || P[size_t(p)] > P[size_t(top)]) top = p;
    }
    if (read >= 2) {
        s.top = std::abs(top - kP4) <= 1 ? QStringLiteral("peaked at the top") : QStringLiteral("peaked at %1").arg(deliveryPositionTag(top));
        s.parts << s.top;
    }
    if (fin(P[kP7])) {
        // decision 1: the difference of the two figures as printed
        const double d = std::round(P[kP7]) - std::round(P[kP1]);
        s.delivered = std::fabs(d) <= kDeliveryDeliveredSameDeg
            ? QStringLiteral("delivered at %1, where you set it").arg(deliveryDeg(P[kP7]))
            : QStringLiteral("delivered at %1, %2 %3 where you set it").arg(deliveryDeg(P[kP7]), absDeg(d),
                                                                           d > 0 ? QStringLiteral("above") : QStringLiteral("below"));
        s.parts << s.delivered;
    }
    if (fin(in.steepening)) {
        const double st = in.steepening;
        s.hands = st >= kDeliveryHandsDeg ? QStringLiteral("Hands %1 steeper coming down").arg(absDeg(st))
                : st <= -kDeliveryHandsDeg ? QStringLiteral("Hands %1 flatter coming down").arg(absDeg(st))
                : QStringLiteral("Hands came down on the plane they went up");
    }
    s.text = s.parts.join(kDot) + QStringLiteral(".") + (s.hands.isEmpty() ? QString() : QStringLiteral(" ") + s.hands + QStringLiteral("."));

    // decision 3: the published shape
    if (fin(P[kP3]) && fin(P[kP4]) && fin(P[kP7])) {
        s.shapeRead = true;
        const double a = P[kP3] - P[kP7], b = P[kP4] - P[kP7];
        if (a > kDeliveryShapeP3Deg + kDeliveryShapeBandDeg)       s.shapeWord = QStringLiteral("is steep from P3");
        else if (b > kDeliveryShapeP4Deg + kDeliveryShapeBandDeg)  s.shapeWord = QStringLiteral("is steep at the top");
        else if (b < kDeliveryShapeP4Deg - kDeliveryShapeBandDeg || a < kDeliveryShapeP3Deg - kDeliveryShapeBandDeg)
                                                                   s.shapeWord = QStringLiteral("stays under");
        else                                                       s.shapeWord = QStringLiteral("matches");
        s.shapeLine = QStringLiteral("Most good players go back a little under the plane they deliver on, reach the top a few "
                                     "degrees above it and are back on it by impact; yours %1.").arg(s.shapeWord);
    }
    return s;
}

DeliverySlot deliverySlotWords(const DeliveryShotInput &in)
{
    DeliverySlot s;
    s.address = in.plane[kP1];
    s.shoulders = in.shoulderPlane;
    if (!fin(s.address) || !fin(s.shoulders)) return s;
    s.available = true;
    const double margin = fin(in.planeSigma) && in.planeSigma > 0 ? in.planeSigma : 0.0;   // decision 4
    QStringList words;
    for (int p = kP3; p <= kP6; ++p) {
        const double v = in.plane[size_t(p)];
        if (!fin(v)) continue;
        DeliverySlotMark m;
        m.pos = p;
        if (v > s.shoulders + margin) {
            m.side = QStringLiteral("above");
            m.words = QStringLiteral("above the shoulder plane at %1 — %2").arg(deliveryPositionTag(p),
                          p <= kP4 ? QStringLiteral("a steep backswing") : QStringLiteral("steep coming down"));
        } else if (v < s.address - margin) {
            m.side = QStringLiteral("under");
            m.words = QStringLiteral("under the address line at %1 — under the plane").arg(deliveryPositionTag(p));
        } else {
            continue;
        }
        words << m.words;
        s.outside.push_back(m);
    }
    s.text = words.isEmpty() ? QStringLiteral("inside the slot") : words.join(QStringLiteral("; "));
    return s;
}

// ── the fits ────────────────────────────────────────────────────────────────────────────────────

double deliveryXOf(const DeliveryShotInput &in, DeliveryX x)
{
    return x == DeliveryX::Steepening ? in.steepening : in.plane[kP6];
}

double deliveryYOf(const DeliveryShotInput &in, DeliveryY y)
{
    return y == DeliveryY::Attack ? in.lmAttack : in.lmPath;
}

QString deliveryXKey(DeliveryX x) { return x == DeliveryX::Steepening ? QStringLiteral("steepening") : QStringLiteral("planeP6"); }
QString deliveryYKey(DeliveryY y) { return y == DeliveryY::Attack ? QStringLiteral("attack") : QStringLiteral("path"); }

DeliveryFit deliveryFit(const std::vector<double> &xs, const std::vector<double> &ys, int wantSign)
{
    DeliveryFit f;
    for (size_t i = 0; i < xs.size() && i < ys.size(); ++i)
        if (fin(xs[i]) && fin(ys[i])) { f.xs.push_back(xs[i]); f.ys.push_back(ys[i]); }
    f.n = int(f.xs.size());
    if (f.n == 0) { f.verdict = QStringLiteral("none"); f.why = QStringLiteral("no shot carries both readings"); return f; }
    if (f.n >= 2) {
        double mx = 0, my = 0;
        for (int i = 0; i < f.n; ++i) { mx += f.xs[size_t(i)]; my += f.ys[size_t(i)]; }
        mx /= f.n; my /= f.n;
        double sxx = 0, sxy = 0, syy = 0;
        for (int i = 0; i < f.n; ++i) {
            const double dx = f.xs[size_t(i)] - mx, dy = f.ys[size_t(i)] - my;
            sxx += dx * dx; sxy += dx * dy; syy += dy * dy;
        }
        if (sxx > 0) {
            f.slope = sxy / sxx;
            f.intercept = my - f.slope * mx;
            if (syy > 0) f.r = sxy / std::sqrt(sxx * syy);
            if (f.n > 2) {
                double ss = 0;
                for (int i = 0; i < f.n; ++i) { const double e = f.ys[size_t(i)] - (f.intercept + f.slope * f.xs[size_t(i)]); ss += e * e; }
                f.sd = std::sqrt(ss / (f.n - 2));
            }
        }
    }
    if (f.n < kDeliveryFitMinShots) {
        f.verdict = QStringLiteral("tooFew");
        f.why = QStringLiteral("%1 shots are needed; there are %2").arg(kDeliveryFitMinShots).arg(f.n);
    } else if (!fin(f.r) || std::fabs(f.r) < kDeliveryFitMinR) {
        f.verdict = QStringLiteral("weak");
        f.why = QStringLiteral("|r| is under %1").arg(kDeliveryFitMinR, 0, 'f', 1);
    } else if ((f.slope < 0 ? -1 : 1) != wantSign) {
        f.verdict = QStringLiteral("wrongSign");
        f.why = QStringLiteral("the slope runs against the physics");
    } else {
        f.verdict = QStringLiteral("shown");
        f.holds = true;
    }
    return f;
}

DeliveryFit deliveryFitOf(const std::vector<DeliveryShotInput> &shots, DeliveryX x, DeliveryY y)
{
    std::vector<double> xs, ys;
    std::vector<int> ords;
    for (const DeliveryShotInput &s : shots) {
        const double a = deliveryXOf(s, x), b = deliveryYOf(s, y);
        xs.push_back(a); ys.push_back(b);
        if (fin(a) && fin(b)) ords.push_back(s.ordinal);
    }
    const bool lefty = !shots.empty() && shots.front().leftHanded;
    DeliveryFit f = deliveryFit(xs, ys, y == DeliveryY::Path && lefty ? 1 : -1);
    f.x = x; f.y = y;
    f.ordinals = std::move(ords);
    return f;
}

const DeliveryFit &DeliveryFits::path() const
{
    // decision 7
    if (pathP6.holds != pathSteep.holds) return pathP6.holds ? pathP6 : pathSteep;
    const double a = fin(pathP6.r) ? std::fabs(pathP6.r) : -1.0, b = fin(pathSteep.r) ? std::fabs(pathSteep.r) : -1.0;
    if (pathP6.n == 0 && pathSteep.n > 0) return pathSteep;
    return b > a ? pathSteep : pathP6;
}

DeliveryFits deliveryFits(const std::vector<DeliveryShotInput> &shots)
{
    DeliveryFits f;
    f.pathP6    = deliveryFitOf(shots, DeliveryX::PlaneP6, DeliveryY::Path);
    f.pathSteep = deliveryFitOf(shots, DeliveryX::Steepening, DeliveryY::Path);
    f.attackP6  = deliveryFitOf(shots, DeliveryX::PlaneP6, DeliveryY::Attack);
    return f;
}

QString deliveryFitSentence(const DeliveryFit &f, const QString &noun, const QString &pool)
{
    const QString on = pool.isEmpty() ? QString() : QStringLiteral(" ") + pool;
    if (f.verdict == QLatin1String("none") || f.n == 0)
        return pool.isEmpty() ? QStringLiteral("No %1 in this session has both the monitor's path and the plane.").arg(one(noun))
                              : QStringLiteral("No %1 %2 has both the monitor's path and the plane.").arg(one(noun), pool);
    if (f.verdict == QLatin1String("tooFew"))
        return QStringLiteral("%1 %2 with the monitor are needed to see how your path follows your plane; %3.")
            .arg(kDeliveryFitMinShots).arg(noun, pool.isEmpty() ? QStringLiteral("this session has %1").arg(f.n)
                                                                : QStringLiteral("you have %1 %2").arg(f.n).arg(pool));
    if (!f.holds)
        return QStringLiteral("On your %1 %2%3 your path doesn't follow %4 (r %5) — the cause is elsewhere.")
            .arg(f.n).arg(noun, on, xNoun(f.x), W::fig(f.r, 2));
    const QString per = f.x == DeliveryX::Steepening ? QStringLiteral("each degree more your hands steepen")
                                                     : QStringLiteral("each degree steeper at P6");
    return QStringLiteral("On your %1 %2 with the monitor%3, %4 moves your path %5 %6.")
        .arg(f.n).arg(noun, on, per, absDeg1(f.slope), f.slope < 0 ? QStringLiteral("left") : QStringLiteral("right"));
}

std::vector<DeliveryShotInput> deliveryPool(const std::vector<DeliveryShotInput> &shots, const DeliveryDay *day)
{
    std::vector<DeliveryShotInput> pool = shots;
    if (day) {
        pool.reserve(shots.size() + day->others.size());
        for (const DeliveryShotInput &s : day->others) { pool.push_back(s); pool.back().thisSession = false; }
    }
    return pool;
}

QString deliveryPoolWords(const DeliveryDay *day)
{
    if (!day || day->sessions <= 1) return QString();
    const QDate d = QDate::fromString(day->date, QStringLiteral("yyyy-MM-dd"));
    const QString when = d.isValid() ? QStringLiteral("on ") + QLocale(QLocale::English).toString(d, QStringLiteral("d MMM"))
                                     : QStringLiteral("on the day");
    return QStringLiteral("%1 (%2 sessions)").arg(when).arg(day->sessions);
}

QString deliveryAttackSentence(const DeliveryFit &f)
{
    if (!f.holds) return QString();
    return QStringLiteral("Each degree steeper at P6 makes your attack %1 steeper.").arg(absDeg1(f.slope));
}

DeliveryPlace deliveryPlace(const std::vector<DeliveryShotInput> &shots, DeliveryX x, double v)
{
    DeliveryPlace p;
    std::vector<double> vals;
    for (const DeliveryShotInput &s : shots) if (const double a = deliveryXOf(s, x); fin(a)) vals.push_back(a);
    if (!fin(v) || vals.size() < 3) return p;
    p.known = true;
    const double lo = box::quantile7(vals, 1.0 / 3.0), hi = box::quantile7(vals, 2.0 / 3.0);
    p.third = v >= hi ? 1 : v <= lo ? -1 : 0;
    p.words = p.third > 0 ? QStringLiteral("your steepest third") : p.third < 0 ? QStringLiteral("your flattest third")
                                                                                : QStringLiteral("the middle of your range");
    return p;
}

// ── at impact and the shot shape ────────────────────────────────────────────────────────────────

std::vector<DeliveryImpactLine> deliveryImpactLines(const DeliveryShotInput &in)
{
    std::vector<DeliveryImpactLine> out;
    const QString club = deliveryClubWords(in.club);

    // Lie → face (decision 11)
    {
        DeliveryImpactLine l;
        l.key = QStringLiteral("lie");
        const bool mon = fin(in.lmLie);
        l.value = mon ? in.lmLie : (fin(in.lieP1) && fin(in.lieP7) ? in.lieP7 - in.lieP1 : kNaN);
        if (fin(l.value)) {
            l.source = mon ? QStringLiteral("monitor") : QStringLiteral("camera");
            l.loft = fin(in.lmLoft) ? in.lmLoft : in.nominalLoft;
            l.loftSource = fin(in.lmLoft) ? QStringLiteral("monitor") : fin(in.nominalLoft) ? QStringLiteral("club") : QString();
            const double sign = in.leftHanded ? -1.0 : 1.0;   // toe up turns a right-hander's face left
            l.face = fin(l.loft) ? sign * l.value * std::sin(l.loft * kPi / 180.0) : kNaN;
            const QString lie = mon ? QStringLiteral("Toe %1 %2 at impact").arg(absDeg1(l.value), l.value > 0 ? QStringLiteral("up") : QStringLiteral("down"))
                                    : QStringLiteral("Shaft %1 %2 at impact than at address").arg(absDeg(l.value),
                                          l.value > 0 ? QStringLiteral("more upright") : QStringLiteral("flatter"));
            if (std::fabs(l.value) < kDeliveryLieFlatDeg) {
                l.text = mon ? QStringLiteral("The sole sat level at impact: the lie does not turn the face.")
                             : QStringLiteral("The shaft came back to its address lie: the lie does not turn the face.");
            } else if (!fin(l.face)) {
                l.text = lie + QStringLiteral(".");
            } else if (l.loft <= kDeliveryLieBarelyLoftDeg) {
                l.text = QStringLiteral("%1 → face about %2 %3: on a %4 the lie barely matters.")
                             .arg(lie, absDeg1(l.face), l.face > 0 ? QStringLiteral("left") : QStringLiteral("right"), club);
            } else {
                l.text = QStringLiteral("%1 → face about %2 %3 on this %4.")
                             .arg(lie, absDeg1(l.face), l.face > 0 ? QStringLiteral("left") : QStringLiteral("right"), club);
            }
            out.push_back(l);
        }
    }
    // Lean → loft
    if (fin(in.shaftLean) || fin(in.lmLoft)) {
        DeliveryImpactLine l;
        l.key = QStringLiteral("lean");
        l.value = in.shaftLean;
        l.loft = in.lmLoft;
        l.loftSource = fin(in.lmLoft) ? QStringLiteral("monitor") : QString();
        QString lean;
        if (fin(in.shaftLean))
            lean = in.shaftLean >= 0.5 ? QStringLiteral("Hands %1 ahead").arg(absDeg(in.shaftLean))
                 : in.shaftLean <= -0.5 ? QStringLiteral("Hands %1 behind").arg(absDeg(in.shaftLean))
                 : QStringLiteral("Shaft upright at impact");
        const QString loft = fin(in.lmLoft) ? QStringLiteral("the monitor saw %1 of loft").arg(deliveryDeg1(in.lmLoft)) : QString();
        l.text = lean.isEmpty() ? W::cap1(loft) + QStringLiteral(".")
               : loft.isEmpty() ? lean + QStringLiteral(".") : lean + QStringLiteral("; ") + loft + QStringLiteral(".");
        l.source = fin(in.shaftLean) && fin(in.lmLoft) ? QStringLiteral("both") : fin(in.lmLoft) ? QStringLiteral("monitor") : QStringLiteral("camera");
        out.push_back(l);
    }
    // Attack and low point: the monitor's, else ours
    {
        const bool monA = fin(in.lmAttack), monL = fin(in.lmLowPoint);
        const double a = monA ? in.lmAttack : in.ourAttack, lp = monL ? in.lmLowPoint : in.lowPoint;
        if (fin(a) || fin(lp)) {
            DeliveryImpactLine l;
            l.key = QStringLiteral("attack");
            l.value = a;
            QStringList parts;
            if (fin(a)) parts << QStringLiteral("Attack %1 (%2)").arg(monA ? deliveryDeg1(a) : deliveryDeg(a),
                                                                     monA ? QStringLiteral("the monitor's") : QStringLiteral("the camera's"));
            if (fin(lp)) parts << QStringLiteral("%1 %2 in %3 the ball").arg(parts.isEmpty() ? QStringLiteral("Low point") : QStringLiteral("low point"),
                                                                            W::fig(std::fabs(lp), 1),
                                                                            lp >= 0 ? QStringLiteral("ahead of") : QStringLiteral("behind"));
            l.text = parts.join(kDot) + QStringLiteral(".");
            const bool anyMon = (fin(a) && monA) || (fin(lp) && monL), anyCam = (fin(a) && !monA) || (fin(lp) && !monL);
            l.source = anyMon && anyCam ? QStringLiteral("both") : anyMon ? QStringLiteral("monitor") : QStringLiteral("camera");
            out.push_back(l);
        }
    }
    return out;
}

DeliveryShape deliveryShotShape(const DeliveryShotInput &in)
{
    DeliveryShape s;
    s.face = in.lmFace;
    s.faceToPath = fin(in.lmFaceToPath) ? in.lmFaceToPath : (fin(in.lmFace) && fin(in.lmPath) ? in.lmFace - in.lmPath : kNaN);
    if (!fin(s.face) || !fin(s.faceToPath)) return s;
    s.available = true;
    s.start = s.face > kDeliveryShapeStartDeg ? 1 : s.face < -kDeliveryShapeStartDeg ? -1 : 0;
    s.curve = s.faceToPath > kDeliveryShapeCurveDeg ? 1 : s.faceToPath < -kDeliveryShapeCurveDeg ? -1 : 0;
    s.slight = s.curve != 0 && std::fabs(s.faceToPath) <= kDeliverySlightCurveDeg;
    // the words mirror for a left-handed golfer: right of the target is a pull, a curve right a draw
    const int st = in.leftHanded ? -s.start : s.start, cv = in.leftHanded ? -s.curve : s.curve;
    s.startWord = st > 0 ? QStringLiteral("push") : st < 0 ? QStringLiteral("pull") : QStringLiteral("straight");
    const QString base = cv > 0 ? QStringLiteral("fade") : QStringLiteral("draw");
    s.curveWord = s.curve == 0 ? QStringLiteral("straight") : (s.slight ? QStringLiteral("slight ") : QString()) + base;
    if (s.start == 0 && s.curve == 0)  s.word = QStringLiteral("Straight");
    else if (s.curve == 0)             s.word = W::cap1(s.startWord);
    else if (s.start == 0)             s.word = QStringLiteral("Straight start, ") + s.curveWord;
    else                               s.word = W::cap1(s.startWord) + QStringLiteral(", ") + s.curveWord;
    return s;
}

// ── the session ─────────────────────────────────────────────────────────────────────────────────

DeliveryUsual::DeliveryUsual() { plane.fill(kNaN); }

DeliveryUsual deliveryUsual(const std::vector<DeliveryShotInput> &shots)
{
    DeliveryUsual u;
    std::array<std::vector<double>, kDeliveryPositions> v;
    std::vector<double> st, sh;
    for (const DeliveryShotInput &s : shots) {
        if (!s.hasPlane()) continue;
        ++u.n;
        for (int p = 0; p < kDeliveryPositions; ++p) v[size_t(p)].push_back(s.plane[size_t(p)]);
        st.push_back(s.steepening);
        sh.push_back(s.shoulderPlane);
    }
    for (int p = 0; p < kDeliveryPositions; ++p) u.plane[size_t(p)] = med(v[size_t(p)]);
    u.steepening = med(st);
    u.shoulderPlane = med(sh);
    return u;
}

DeliveryBand deliverySessionBand(const std::vector<DeliveryShotInput> &shots)
{
    DeliveryBand b;
    std::array<std::vector<double>, kDeliveryPositions> v;
    for (const DeliveryShotInput &s : shots) {
        if (s.hasPlane()) ++b.n;
        for (int p = 0; p < kDeliveryPositions; ++p) v[size_t(p)].push_back(s.plane[size_t(p)]);
    }
    for (int p = 0; p < kDeliveryPositions; ++p) b.boxes[size_t(p)] = box::boxStats(v[size_t(p)]);
    b.shown = b.n >= kDeliverySessionBandShots;
    return b;
}

DeliveryVaries deliveryVariesMost(const DeliveryBand &band, const DeliveryFits &fits)
{
    DeliveryVaries v;
    if (!band.shown) return v;
    for (int p = 0; p < kDeliveryPositions; ++p) {
        const box::BoxStats &b = band.boxes[size_t(p)];
        if (b.n < kDeliverySessionBandShots || !fin(b.iqr)) continue;
        if (v.loosest < 0 || b.iqr > band.boxes[size_t(v.loosest)].iqr) v.loosest = p;
        if (v.tightest < 0 || b.iqr < band.boxes[size_t(v.tightest)].iqr) v.tightest = p;
    }
    if (v.loosest < 0) return v;
    v.has = true;
    v.tightHalf = band.boxes[size_t(v.tightest)].iqr / 2.0;
    v.looseHalf = band.boxes[size_t(v.loosest)].iqr / 2.0;
    const DeliveryFit &f = fits.path();
    const QString tail = f.holds ? QStringLiteral(" — your path follows your plane (r %1).").arg(W::fig(f.r, 2))
                       : f.n >= kDeliveryFitMinShots ? QStringLiteral(" — your path does not follow your plane (r %1).").arg(W::fig(f.r, 2))
                       : QStringLiteral(".");
    v.words = QStringLiteral("Steadiest at %1 (middle half ±%2), loosest at %3 (±%4)%5")
                  .arg(deliveryPositionTag(v.tightest), deliveryDeg1(v.tightHalf), deliveryPositionTag(v.loosest),
                       deliveryDeg1(v.looseHalf), tail);
    return v;
}

// ── the head and the trust lines ────────────────────────────────────────────────────────────────

DeliveryHead deliveryHead(const DeliveryShotInput &in, const std::vector<DeliveryShotInput> &shots,
                          const DeliveryFits &fits, const DeliveryUsual &usual)
{
    DeliveryHead h;
    const bool haveUsual = usual.n >= kDeliverySessionBandShots;
    // rule 4: no down-the-line plane — the monitor alone
    if (!in.hasPlane()) {
        if (!in.hasMonitor()) return h;
        h.rule = 4;
        const DeliveryShape sh = deliveryShotShape(in);
        if (sh.available)
            h.text = QStringLiteral("%1: face %2, %3 %4 to the path.")
                         .arg(sh.word, deliveryDirection(sh.face), absDeg1(sh.faceToPath),
                              (sh.faceToPath >= 0) != in.leftHanded ? QStringLiteral("open") : QStringLiteral("closed"));
        else if (in.hasPath())
            h.text = QStringLiteral("Path %1.").arg(deliveryDirection(in.lmPath));
        else
            h.text = QStringLiteral("The monitor read this shot; the swing plane was not read.");
        return h;
    }
    const double p6 = in.plane[kP6];
    if (in.hasPath()) {
        const DeliveryFit &f = fits.path();
        const double x = deliveryXOf(in, f.x);
        const QString path = QStringLiteral("the path went %1").arg(deliveryDirection(in.lmPath));
        if (f.holds && fin(x)) {
            h.rule = 1;
            if (f.x == DeliveryX::Steepening) {
                h.text = handsHead(x) + QStringLiteral(", and ") + path + QStringLiteral(".");
            } else {
                const DeliveryPlace place = deliveryPlace(shots, DeliveryX::PlaneP6, p6);
                const QString usualPart = place.known ? QStringLiteral(", in ") + place.words : QString();
                h.text = QStringLiteral("At P6 your plane was %1%2, and %3.").arg(deliveryDeg(p6), usualPart, path);
            }
            return h;
        }
        // rule 2: the path and the plane side by side
        h.rule = 2;
        QString plane;
        if (fin(p6) && haveUsual && fin(usual.plane[kP6])) {
            const double d = p6 - usual.plane[kP6];
            plane = std::fabs(d) < kDeliveryUsualDeg
                ? QStringLiteral("your plane was in its usual place, so look at the face and the strike.")
                : QStringLiteral("your plane was %1 %2 than your usual at P6.").arg(absDeg(d), steeperWord(d));
        } else if (fin(p6)) {
            plane = QStringLiteral("your plane was %1 at P6.").arg(deliveryDeg(p6));
        } else {
            plane = QStringLiteral("your plane at P6 was not read.");
        }
        h.text = QStringLiteral("Path %1; %2").arg(deliveryDirection(in.lmPath), plane);
        return h;
    }
    // rule 3: no monitor — the story's largest departure from the golfer's usual
    h.rule = 3;
    if (haveUsual) {
        int best = -2;
        double bestD = 0.0;
        for (const StoryReading &r : kStoryReadings) {
            const double v = r.pos < 0 ? in.steepening : in.plane[size_t(r.pos)];
            const double m = r.pos < 0 ? usual.steepening : usual.plane[size_t(r.pos)];
            if (!fin(v) || !fin(m)) continue;
            if (best == -2 || std::fabs(v - m) > std::fabs(bestD)) { best = r.pos; bestD = v - m; }
        }
        if (best != -2) {
            h.text = std::fabs(bestD) >= kDeliveryUsualDeg ? usualWords(best, bestD) : QStringLiteral("Your plane was where it usually is.");
            return h;
        }
    }
    const DeliveryStory s = deliveryPlaneStory(in);
    QStringList parts;
    if (!s.set.isEmpty()) parts << s.set;
    if (!s.delivered.isEmpty()) parts << s.delivered;
    h.text = parts.isEmpty() ? QStringLiteral("Your plane was read on part of this swing.")
                             : QStringLiteral("Your plane: ") + parts.join(QStringLiteral(", ")) + QStringLiteral(".");
    return h;
}

DeliveryTrust deliveryTrust(const std::vector<DeliveryShotInput> &shots, const DeliveryShotInput *shot,
                            const DeliveryFits &fits, const QString &noun, const DeliveryDay *day)
{
    DeliveryTrust t;
    const bool anyPlane = (shot && shot->hasPlane()) || std::any_of(shots.begin(), shots.end(), [](const DeliveryShotInput &s) { return s.hasPlane(); });
    if (anyPlane)
        t.lines << QStringLiteral("Plane angles come from the down-the-line camera, which is not calibrated: they compare within this "
                                  "session and this camera, and the address line and the shoulder plane drawn are your own.");
    if (shot && !shot->hasPlane())
        t.lines << QStringLiteral("The swing plane was not read on this shot (it needs the down-the-line camera's club track), so the "
                                  "plane story and its link to the ball are absent; the monitor's shot shape is shown.");
    if (shot ? !shot->handsSpans.empty() : anyPlane)
        t.lines << QStringLiteral("Where the shaft points along the target line (P2, the top and the stretch after it) the hands carry "
                                  "the plane: the curve there is the plane through the ball line and your hands, shaded on the plot.");
    const QString pool = deliveryPoolWords(day);
    if (!pool.isEmpty())
        t.lines << QStringLiteral("The fits pool your day, %1: one camera set-up, so its offset cancels; the other sessions' "
                                  "%2 are read from their cached readings.").arg(pool, noun)
                       + (day->staleGrids > 0 ? QStringLiteral(" %1 of them were cached before their swing was last analysed.")
                                                    .arg(day->staleGrids) : QString());
    const auto fitLine = [&](const DeliveryFit &f, const QString &what) {
        if (f.n == 0) return;
        QString state = f.holds ? QStringLiteral("shown") : QStringLiteral("not shown: ") + f.why;
        t.lines << QStringLiteral("%1: %2 %3%4, r %5%6 — yours alone, %7.")
                       .arg(what).arg(f.n).arg(f.n == 1 ? one(noun) : noun, pool.isEmpty() ? QString() : QStringLiteral(" ") + pool, W::fig(f.r, 2),
                            fin(f.sd) ? QStringLiteral(", scatter ±") + deliveryDeg1(f.sd) : QString(), state);
    };
    fitLine(fits.pathP6, QStringLiteral("Path against the plane at P6"));
    fitLine(fits.pathSteep, QStringLiteral("Path against the hands' steepening"));
    fitLine(fits.attackP6, QStringLiteral("Attack against the plane at P6"));

    std::vector<const DeliveryShotInput *> all;
    for (const DeliveryShotInput &s : shots) all.push_back(&s);
    if (shot && std::none_of(shots.begin(), shots.end(), [&](const DeliveryShotInput &s) { return s.swingDir == shot->swingDir && !s.swingDir.isEmpty(); }))
        all.push_back(shot);
    const bool anyMon = std::any_of(all.begin(), all.end(), [](const DeliveryShotInput *s) { return s->hasMonitor(); });
    if (!anyMon) {
        t.lines << QStringLiteral("No launch monitor reading on this session: the plane is not linked to the ball.");
    } else {
        QStringList lacked;
        const auto none = [&](double DeliveryShotInput::*f) {
            return std::none_of(all.begin(), all.end(), [&](const DeliveryShotInput *s) { return s->hasMonitor() && fin(s->*f); });
        };
        if (none(&DeliveryShotInput::lmFace)) lacked << QStringLiteral("no face");
        if (none(&DeliveryShotInput::lmLie))  lacked << QStringLiteral("no lie");
        if (none(&DeliveryShotInput::lmLoft)) lacked << QStringLiteral("no loft");
        t.lines << QStringLiteral("The launch monitor is primary for path, face, attack, lie and loft; the camera's readings show only "
                                  "where it gave none.")
                       + (lacked.isEmpty() ? QString() : QStringLiteral(" On this session it gave %1.").arg(W::list(lacked)));
    }
    if (anyPlane)
        t.lines << QStringLiteral("In the literature the plane moves through the swing and settles into one plane by impact (Kwon "
                                  "et al. 2012); good players go back a little under the plane they deliver on and reach the top a "
                                  "few degrees above it (Morrison et al. 2018). Those are shapes, never targets: no book angle is "
                                  "compared with yours.");
    return t;
}

// ── the views ───────────────────────────────────────────────────────────────────────────────────

DeliveryShotView deliveryShot(const DeliveryShotInput &in, const std::vector<DeliveryShotInput> &shots, const DeliveryDay *day)
{
    DeliveryShotView v;
    v.hasPlane = in.hasPlane();
    v.hasMonitor = in.hasMonitor();
    v.available = v.hasPlane || v.hasMonitor;
    if (!v.available) return v;
    v.mode = !v.hasPlane ? QStringLiteral("noDtl") : !v.hasMonitor ? QStringLiteral("noMonitor") : QStringLiteral("full");
    const std::vector<DeliveryShotInput> pool = deliveryPool(shots, day);
    v.fits = deliveryFits(pool);
    v.poolShots = int(pool.size());
    v.poolWords = deliveryPoolWords(day);
    const DeliveryUsual usual = deliveryUsual(shots);
    v.head = deliveryHead(in, pool, v.fits, usual);
    v.shape = deliveryShotShape(in);
    v.impact = deliveryImpactLines(in);
    v.trust = deliveryTrust(shots, &in, v.fits, QStringLiteral("shots"), day);
    if (!v.hasPlane) return v;   // §4.3 rule 4: no story, no slot, no flight

    v.story = deliveryPlaneStory(in);
    v.slot = deliverySlotWords(in);
    const DeliveryFit &f = v.fits.path();
    v.flightAvailable = f.n > 0 || in.hasPath();
    if (!v.flightAvailable) return v;
    v.flightText = f.n > 0 ? deliveryFitSentence(f, QStringLiteral("shots"), v.poolWords) : QString();
    v.attackText = deliveryAttackSentence(v.fits.attackP6);
    const double x = deliveryXOf(in, f.x);
    v.place = deliveryPlace(pool, f.x, x);
    QString xPart;
    if (fin(x))
        xPart = f.x == DeliveryX::Steepening ? QStringLiteral("hands %1 %2").arg(absDeg(x), steeperWord(x))
                                             : QStringLiteral("%1 at P6").arg(deliveryDeg(x));
    if (xPart.isEmpty()) {
        if (in.hasPath()) v.thisLine = QStringLiteral("This one: path %1.").arg(deliveryDeg1Signed(in.lmPath));
        return v;
    }
    const QString place = f.holds && v.place.known ? QStringLiteral(", ") + v.place.words : QString();
    if (in.hasPath()) {
        v.thisLine = QStringLiteral("This one: %1%2 → path %3.").arg(xPart, place, deliveryDeg1Signed(in.lmPath));
    } else if (f.holds) {
        const double pred = f.intercept + f.slope * x;   // never shown as a measurement (§6.2)
        const QString path = std::lround(pred) == 0 ? QStringLiteral("a path about square")
                           : QStringLiteral("a path about %1 %2").arg(absDeg(pred), pred < 0 ? QStringLiteral("left") : QStringLiteral("right"));
        v.thisLine = QStringLiteral("This one: %1%2; on your fit, a plane like this goes with %3.").arg(xPart, place, path);
    } else {
        v.thisLine = QStringLiteral("This one: %1.").arg(xPart);
    }
    return v;
}

DeliverySessionView deliverySession(const std::vector<DeliveryShotInput> &shots, const QString &noun, const DeliveryDay *day)
{
    DeliverySessionView v;
    for (const DeliveryShotInput &s : shots) {
        if (s.hasPlane()) ++v.planeShots;
        if (s.hasMonitor()) ++v.monitorShots;
        if (s.hasPlane() || s.hasMonitor()) ++v.shots;
    }
    v.available = v.shots > 0;
    if (!v.available) return v;
    v.fits = deliveryFits(deliveryPool(shots, day));
    v.poolShots = int(shots.size() + (day ? day->others.size() : 0));
    v.poolWords = deliveryPoolWords(day);
    v.usual = deliveryUsual(shots);
    v.band = deliverySessionBand(shots);
    v.varies = deliveryVariesMost(v.band, v.fits);
    const DeliveryFit &f = v.fits.path();
    if (f.n > 0) {
        v.flightText = deliveryFitSentence(f, noun, v.poolWords);
        v.attackText = deliveryAttackSentence(v.fits.attackP6);
    }
    std::map<QString, int> counts;
    int shaped = 0;
    for (const DeliveryShotInput &s : shots) {
        v.shapes.push_back(deliveryShotShape(s));
        if (v.shapes.back().available) { ++shaped; ++counts[v.shapes.back().word]; }
    }
    if (shaped > 0) {
        auto best = counts.begin();
        for (auto it = counts.begin(); it != counts.end(); ++it) if (it->second > best->second) best = it;
        v.shapeLine = QStringLiteral("%1 on %2 of %3 %4").arg(best->first).arg(best->second).arg(shaped).arg(shaped == 1 ? one(noun) : noun);
    }
    // the head (§4.7): "(16 shots on 20 Oct, 3 sessions)" for a day of several
    QString poolTail = v.poolWords;
    if (!poolTail.isEmpty()) poolTail = QStringLiteral(" ") + poolTail.replace(QStringLiteral(" ("), QStringLiteral(", ")).remove(QLatin1Char(')'));
    if (f.holds) {
        v.head = f.x == DeliveryX::Steepening
            ? QStringLiteral("Your path follows your hands: a degree more steepening, %1 further left (%2 %3%4).").arg(absDeg1(f.slope)).arg(f.n).arg(noun, poolTail)
            : QStringLiteral("Your path follows your plane: steeper at P6 by a degree, %1 further left (%2 %3%4).").arg(absDeg1(f.slope)).arg(f.n).arg(noun, poolTail);
        if (f.slope > 0) v.head.replace(QStringLiteral("further left"), QStringLiteral("further right"));
    } else if (f.n > 0) {
        v.head = deliveryFitSentence(f, noun, v.poolWords);
    } else if (v.planeShots > 0) {
        QStringList parts;
        if (fin(v.usual.plane[kP1])) parts << QStringLiteral("set on %1").arg(deliveryDeg(v.usual.plane[kP1]));
        if (fin(v.usual.plane[kP7])) parts << QStringLiteral("delivered at %1").arg(deliveryDeg(v.usual.plane[kP7]));
        if (fin(v.usual.steepening))
            parts << (v.usual.steepening >= kDeliveryHandsDeg ? QStringLiteral("hands %1 steeper coming down").arg(absDeg(v.usual.steepening))
                    : v.usual.steepening <= -kDeliveryHandsDeg ? QStringLiteral("hands %1 flatter coming down").arg(absDeg(v.usual.steepening))
                    : QStringLiteral("hands on the plane they went up"));
        v.head = QStringLiteral("Your plane: %1 (%2 %3).").arg(parts.join(QStringLiteral(", "))).arg(v.planeShots)
                     .arg(v.planeShots == 1 ? one(noun) : noun);
    } else if (!v.shapeLine.isEmpty()) {
        v.head = v.shapeLine + QStringLiteral(".");
    }
    v.trust = deliveryTrust(shots, nullptr, v.fits, noun, day);
    return v;
}

} // namespace pinpoint::charts
