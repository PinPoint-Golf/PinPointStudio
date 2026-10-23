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

#include "session_posture.h"

#include "charts_words.h"
#include "../../Diagnostics/engine/characteristic.h"
#include "../../Diagnostics/engine/condition_reading.h"   // a condition's readings: where it reads on a shot
#include "../../Diagnostics/measures/measure_sample.h"   // buildPhaseGrid, reduceOverGrid: THE sampler
#include "../../Diagnostics/norms/context_tree.h"        // contextIdForClub
#include "../../Diagnostics/norms/norm_provider.h"
#include "../../Diagnostics/packs/pack_provider.h"
#include "../../Metrics/gate_catalogue.h"                // a gate's words for a refused reading
#include "../../Coaching/diagnostic_ledger.h"            // conditionTier, wilsonLowerBound: the session's pattern gate

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>

#include <algorithm>
#include <functional>
#include <map>

namespace pinpoint::charts {

namespace pa = pinpoint::analysis;
namespace W  = words;

namespace {

constexpr int kP2 = 1, kP3 = 2, kP4 = 3, kP5 = 4, kP6 = 5, kP7 = 6, kP8 = 7;

bool fin(double v) { return std::isfinite(v); }
int  ri(PostureRow r) { return int(r); }

std::array<PostureRowSpec, kPostureRows> buildSpecs()
{
    const auto ids = [](const QString &stem, std::array<const char *, kPosturePositions> reuse) {
        std::array<QString, kPosturePositions> out;
        for (int p = 1; p < kPosturePositions; ++p)
            out[size_t(p)] = reuse[size_t(p)] ? QString::fromLatin1(reuse[size_t(p)])
                                              : stem + QStringLiteral("_p") + QString::number(p + 1);
        return out;
    };
    std::array<PostureRowSpec, kPostureRows> s;
    // The reused cells (posture_chart_design.md §7.3); "" names no measure (a masked P8).
    s[0] = { PostureRow::Head, QStringLiteral("head"), QStringLiteral("headLift"), QStringLiteral("Head height"),
             QStringLiteral("head"), QStringLiteral("cm"), 1, PostureCamera::FaceOn,
             QStringLiteral("up"), QStringLiteral("down"), QStringLiteral("up"),
             ids(QStringLiteral("m_headLift"), { nullptr, nullptr, nullptr, "m_headLiftBack", nullptr, nullptr, "m_headLiftImpact", nullptr }) };
    s[1] = { PostureRow::Hips, QStringLiteral("hips"), QStringLiteral("pelvisThrust"), QStringLiteral("Hip depth"),
             QStringLiteral("hips"), QStringLiteral("cm"), 1, PostureCamera::Dtl,
             QStringLiteral("in"), QStringLiteral("back"), QStringLiteral("in"),
             // P3–P5: the butt line (decision 15); the joints there are the fallback, postureHipJointsMeasure()
             ids(QStringLiteral("m_pelvisThrust"), { nullptr, nullptr, "m_hipDepthButt_p3", "m_hipDepthButt_p4", "m_hipDepthButt_p5",
                                                     nullptr, "m_pelvisThrustImpact", "" }) };
    s[2] = { PostureRow::Bend, QStringLiteral("bend"), QStringLiteral("spineForwardBend"), QStringLiteral("Trunk bend"),
             QStringLiteral("trunk bend"), QStringLiteral("°"), 0, PostureCamera::Dtl,
             QStringLiteral("more bent"), QStringLiteral("upright"), QStringLiteral("upright"),
             ids(QStringLiteral("m_spineBendChange"), { nullptr, nullptr, nullptr, "m_spineBendChangeTop", nullptr, nullptr, "m_spineBendChangeImpact", "" }) };
    s[3] = { PostureRow::LeadKnee, QStringLiteral("leadKnee"), QStringLiteral("leadKneeFlexion"), QStringLiteral("Lead knee"),
             QStringLiteral("lead knee"), QStringLiteral("°"), 0, PostureCamera::Dtl,
             QStringLiteral("more bend"), QStringLiteral("straighter"), QStringLiteral("more bend"),
             ids(QStringLiteral("m_leadKneeFlex"), { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr }) };
    s[4] = { PostureRow::TrailKnee, QStringLiteral("trailKnee"), QStringLiteral("trailKneeFlexion"), QStringLiteral("Trail knee"),
             QStringLiteral("trail knee"), QStringLiteral("°"), 0, PostureCamera::Dtl,
             QStringLiteral("more bend"), QStringLiteral("straighter"), QStringLiteral("more bend"),
             ids(QStringLiteral("m_trailKneeFlex"), { nullptr, nullptr, nullptr, "m_trailKneeFlex", nullptr, nullptr, nullptr, nullptr }) };
    s[5] = { PostureRow::PelvisHeight, QStringLiteral("pelvisHeight"), QStringLiteral("pelvisLift"), QStringLiteral("Pelvis height"),
             QStringLiteral("pelvis"), QStringLiteral("% stance"), 0, PostureCamera::FaceOn,
             QStringLiteral("up"), QStringLiteral("down"), QStringLiteral("up"),
             ids(QStringLiteral("m_pelvisLift"), { nullptr, nullptr, nullptr, "m_pelvisLiftTop", nullptr, nullptr, "m_pelvisLiftImpact", "" }) };
    return s;
}

// Phase enum → position index, -1 when the phase is not one of P1..P8.
int posOfPhase(pa::Phase ph)
{
    const auto &ids = posturePhaseIds();
    for (int p = 0; p < kPosturePositions; ++p) if (ids[size_t(p)] == int(ph)) return p;
    return -1;
}

bool matchEligible(PostureRow r, int pos) { return r != PostureRow::TrailKnee && pos >= kP2 && pos <= kP7; }

QString withUnit(const QString &fig, const QString &unit)
{
    if (fig == QLatin1String("-")) return fig;
    return unit == QStringLiteral("°") ? fig + unit : fig + QChar(0x00A0) + unit;
}
// "+6.5 cm", "−10°"
QString signedFig(PostureRow r, double v) { return withUnit(postureFigure(r, v), postureRowSpec(r).unit); }
// "6.5 cm", "10°"
QString absFig(PostureRow r, double v)
{
    const PostureRowSpec &s = postureRowSpec(r);
    return withUnit(W::fig(std::fabs(v), s.decimals), s.unit);
}

// ── departures (§4.3) ──

// `row`, `lo`…`hi`: the row and the positions that DEFINE the departure (departureFor below) — where the head
// line reads a fired condition that names it (decision 11).
struct DepartureDef { const char *id, *name; std::vector<const char *> conds; PostureRow row; int lo, hi; };
const std::vector<DepartureDef> &departureDefs()
{
    static const std::vector<DepartureDef> d{
        // loss_of_posture names no departure (decision 8): a composite of several parts, it stays a Finding
        { "D1", "Hips in",                { "early_extension" },                                           PostureRow::Hips,     kP5, kP7 },
        { "D2", "Standing up",            { "coming_out_of_it" },                                          PostureRow::Head,     kP6, kP7 },
        { "D3", "Standing up going back", { "loss_of_posture_backswing" },                                 PostureRow::Head,     kP2, kP4 },
        { "D4", "Early or deep dip",      { "head_drop_backswing", "posture_sinks_backswing" },            PostureRow::Head,     kP2, kP4 },
        { "D5", "Dip then rise",          { "head_drop_backswing", "posture_sinks_backswing", "coming_out_of_it" }, PostureRow::Head, kP7, kP7 },
        { "D6", "Staying down",           { "posture_sinks_impact", "diving" },                            PostureRow::Head,     kP7, kP7 },
        { "D7", "Lead knee folds",        { "lead_knee_folds_backswing" },                                 PostureRow::LeadKnee, kP3, kP4 } };
    return d;
}
const DepartureDef *departureDef(const QString &id)
{
    for (const DepartureDef &d : departureDefs()) if (id == QLatin1String(d.id)) return &d;
    return nullptr;
}
QStringList departureConditions(const QString &id, const PostureEnv &env)
{
    QStringList out;
    if (const DepartureDef *d = departureDef(id))
        for (const char *c : d->conds) {
            const QString cid = QString::fromLatin1(c);
            if (!env.pack || env.pack->condition(cid)) out << cid;
        }
    return out;
}

// The head is "up"/"down" only outside the band AND off level (decision 2).
bool cellWarned(const PostureFlags &warn, PostureRow r, int pos)
{
    return !postureWarnReason(r, pos).isEmpty() || warn[size_t(ri(r))][size_t(pos)];
}
double cellZ(const PostureGrid &x, const PostureBandGrid &b, PostureRow r, int pos)
{
    return b[size_t(ri(r))][size_t(pos)].z(x[size_t(ri(r))][size_t(pos)]);
}
bool headLevel(double v) { return fin(v) && std::fabs(v) < kPostureHeadLevelCm; }
bool headUp(const PostureGrid &x, const PostureFlags &w, const PostureBandGrid &b, int pos)
{
    const double v = x[0][size_t(pos)], z = cellZ(x, b, PostureRow::Head, pos);
    return fin(v) && !cellWarned(w, PostureRow::Head, pos) && !headLevel(v) && fin(z) && z > 1.0;
}
bool headDown(const PostureGrid &x, const PostureFlags &w, const PostureBandGrid &b, int pos)
{
    const double v = x[0][size_t(pos)], z = cellZ(x, b, PostureRow::Head, pos);
    return fin(v) && !cellWarned(w, PostureRow::Head, pos) && !headLevel(v) && fin(z) && z < -1.0;
}
bool hipsIn(const PostureGrid &x, const PostureFlags &w, const PostureBandGrid &b, int pos)
{
    const double z = cellZ(x, b, PostureRow::Hips, pos);
    return !cellWarned(w, PostureRow::Hips, pos) && fin(z) && z > 1.0;
}

// The departure that describes an exit, or "" (with `name` "the trunk alone" for a lone bend loss).
QString departureFor(PostureRow r, int pos, double z, const PostureGrid &x, const PostureFlags &w,
                     const PostureBandGrid &b, QString *name)
{
    *name = QString();
    QString id;
    switch (r) {
    case PostureRow::Head:
        if (pos >= kP2 && pos <= kP4) id = z > 0 ? QStringLiteral("D3") : QStringLiteral("D4");   // D3 from the head only
        else if ((pos == kP6 || pos == kP7) && z > 0) id = QStringLiteral("D2");
        else if (pos == kP7 && z < 0) id = QStringLiteral("D6");
        break;
    case PostureRow::Hips:
        if (z > 0 && pos >= kP5 && pos <= kP7) id = QStringLiteral("D1");
        break;
    case PostureRow::LeadKnee:
        if (z > 0 && (pos == kP3 || pos == kP4)) id = QStringLiteral("D7");
        break;
    case PostureRow::Bend:
        if (z < 0 && (pos == kP6 || pos == kP7)) {
            if (headUp(x, w, b, pos)) id = QStringLiteral("D2");
            else if (hipsIn(x, w, b, pos)) id = QStringLiteral("D1");
            else *name = QStringLiteral("the trunk alone");
        }
        break;
    default:
        break;
    }
    if (const DepartureDef *d = departureDef(id)) *name = QString::fromLatin1(d->name);
    return id;
}

// ── gates: the words behind a refused or missing reading ──
//
// The wrist presenter's rule (charts_wrist_presenter.cpp wristGate), on a posture series: a record
// naming the series (its subject or a cascade's covers) that REFUSED or BOUNDED it warns every
// position; a MASKED record names a "-" whose instant lies in its stretch.
struct GateHit { QString words, why; bool warn = false; };   // why: the refusal's evidence, filled
GateHit gateFor(const ShotSeries &s, const QString &key, double atMs)
{
    GateHit mask;
    if (s.gatesVersion <= 0 || s.gates.isEmpty()) return mask;
    for (const QVariant &rv : s.gates) {
        const QVariantMap r = rv.toMap();
        QStringList ks = r.value(QStringLiteral("covers")).toStringList();
        ks.prepend(r.value(QStringLiteral("subject")).toString());
        if (!ks.contains(key)) continue;
        const gates::GateEntry *e = gates::gateEntry(r.value(QStringLiteral("gate")).toString());
        if (!e) continue;
        const QString eff = r.value(QStringLiteral("effect")).toString();
        if (eff == QLatin1String("refused") || eff == QLatin1String("bounded"))
            return GateHit{ gates::claimWords(*e), gates::evidenceText(*e, r.value(QStringLiteral("evidence")).toMap()), true };
        if (eff == QLatin1String("masked") && mask.words.isEmpty() && fin(atMs)) {
            const QVariantMap ev = r.value(QStringLiteral("evidence")).toMap();
            if (!ev.contains(QStringLiteral("fromMs")) || !ev.contains(QStringLiteral("toMs"))) continue;
            const double a = ev.value(QStringLiteral("fromMs")).toDouble(), b = ev.value(QStringLiteral("toMs")).toDouble();
            if (atMs >= std::min(a, b) - 2.0 && atMs <= std::max(a, b) + 2.0) mask = GateHit{ e->shortText, QString(), false };
        }
    }
    return mask;
}

// ── row views ──

using FiredCount = std::function<int(const QString &conditionId)>;

PostureRowView rowView(PostureRow r, const PostureGrid &x, bool present, const PostureFlags &gateWarn,
                       const PostureWords &gateWords, const PostureFlags &sourceWarn, const PostureBandGrid &bands,
                       const PostureFaultGrid &faults, const FiredCount &firedCount, const PostureTrailStyle &trail,
                       const std::array<double, kPosturePositions> &backLine)
{
    const PostureRowSpec &s = postureRowSpec(r);
    const size_t R = size_t(ri(r));
    PostureRowView v;
    v.row = r; v.key = s.key; v.label = s.label; v.metricKey = s.metricKey; v.camera = postureCameraWords(s.camera);
    v.unit = s.unit; v.poleWord = s.poleWord; v.plusWord = s.plusWord; v.minusWord = s.minusWord;
    v.present = present;
    v.style = r == PostureRow::TrailKnee;
    if (v.style) v.styleWord = trail.word;
    for (int p = 0; p < kPosturePositions; ++p) {
        PostureCellView &c = v.cells[size_t(p)];
        c.pos = p;
        c.tag = posturePositionTag(p);
        c.value = x[R][size_t(p)];
        c.text = postureFigure(r, c.value);
        c.gateWords = gateWords[R][size_t(p)];
        // by construction (§2.3), else the reading's own source (decision 15: hip depth on the joints), else a gate
        const QString built = !postureWarnReason(r, p).isEmpty() ? postureWarnReason(r, p)
                            : sourceWarn[R][size_t(p)] ? postureHipJointsReason() : QString();
        c.warned = !built.isEmpty() || gateWarn[R][size_t(p)];
        c.warnReason = !built.isEmpty() ? built : (gateWarn[R][size_t(p)] ? c.gateWords : QString());
        c.band = bands[R][size_t(p)];
        c.z = c.band.z(c.value);
        c.counted = present && matchEligible(r, p) && !c.warned && fin(c.value) && c.band.has;
        // decision 1: a ⚠ cell is never outside; the word is the side of the band, with no number
        c.outside = !c.warned && fin(c.z) && std::fabs(c.z) > 1.0;
        if (c.outside) c.ring = c.z > 0 ? s.plusWord : s.minusWord;
        const bool exitEligible = r != PostureRow::TrailKnee && !c.warned && fin(c.value)
                                  && ((p >= kP2 && p <= kP7) || (p == kP8 && c.band.has));
        c.exit = exitEligible && fin(c.z) && std::fabs(c.z) > kPostureExitZ;
        if (p > 0) c.fault = faults[R][size_t(p)];
        c.beyondFault = fin(c.value) && ((c.fault.hi.has && c.value > c.fault.hi.edge) || (c.fault.lo.has && c.value < c.fault.lo.edge));
        if (firedCount) {
            if (c.fault.hi.has) c.firedShots += firedCount(c.fault.hi.conditionId);
            if (c.fault.lo.has && c.fault.lo.conditionId != c.fault.hi.conditionId) c.firedShots += firedCount(c.fault.lo.conditionId);
        }
        // W-D6 (wristmotion rule): outside the band on a side a condition grades, and it did not fire.
        const PostureFault &side = c.value > c.band.mu ? c.fault.hi : c.fault.lo;
        if (c.outside && side.has && firedCount && firedCount(side.conditionId) == 0)
            c.note = postureWd6Words(p);
        // decision 16: trunk bend's second reading at P3–P5, the back line — under its own ⚠, never matched or ringed
        if (r == PostureRow::Bend && postureBackLinePos(p) && present) {
            c.alt = true;
            c.altValue = backLine[size_t(p)];
            c.altLabel = postureBackLineLabel();
            c.altFigure = postureFigure(r, c.altValue);
            c.altText = c.altLabel + QLatin1Char(' ') + c.altFigure;
            c.altWarned = fin(c.altValue);
            c.altWarnShort = c.altWarned ? postureBackLineWarnShort() : QString();
        }
    }
    return v;
}

QString exitWords(const PostureExit &e)
{
    const QString head = posturePositionTag(e.pos) + QLatin1Char(' ') + postureRowSpec(e.row).noun + QLatin1Char(' ')
                         + signedFig(e.row, e.value) + QStringLiteral(": ");
    return head + (e.named ? e.name : postureWd6Words(e.pos));
}

// "Your hips moved 7.6 cm toward the ball by impact; good players stay between −1.5 and +3.5 cm."
QString exitHeadline(const PostureExit &e, const PostureBand &b)
{
    const QString amt = absFig(e.row, e.value);
    const QString when = posturePositionWhen(e.pos);
    const bool up = e.value > 0;
    QString s;
    switch (e.row) {
    case PostureRow::Head:     s = up ? QStringLiteral("Your head rose %1 by %2") : QStringLiteral("Your head dropped %1 by %2"); break;
    case PostureRow::Hips:     s = up ? QStringLiteral("Your hips moved %1 toward the ball by %2") : QStringLiteral("Your hips moved %1 away from the ball by %2"); break;
    case PostureRow::Bend:     s = up ? QStringLiteral("You gained %1 of trunk bend by %2") : QStringLiteral("You lost %1 of trunk bend by %2"); break;
    case PostureRow::LeadKnee: s = up ? QStringLiteral("Your lead knee flexed %1 more by %2") : QStringLiteral("Your lead knee straightened %1 by %2"); break;
    case PostureRow::TrailKnee:s = up ? QStringLiteral("Your trail knee flexed %1 more by %2") : QStringLiteral("Your trail knee straightened %1 by %2"); break;
    case PostureRow::PelvisHeight: s = up ? QStringLiteral("Your pelvis rose %1 by %2") : QStringLiteral("Your pelvis dropped %1 by %2"); break;
    }
    s = s.arg(amt, when);
    if (b.has)
        s += QStringLiteral("; good players stay between %1 and %2.").arg(postureFigure(e.row, b.lo), signedFig(e.row, b.hi));
    else
        s += QLatin1Char('.');
    return s;
}

QString fitShort(const PostureMatch &m)
{
    if (m.fit == QLatin1String("close"))   return QStringLiteral("a close match");
    if (m.fit == QLatin1String("between")) return QStringLiteral("between the two, nearer");
    if (m.fit == QLatin1String("none"))    return QStringLiteral("no close fit; nearest");
    return QStringLiteral("nearest");
}

// Band sources actually used (§5.6), from the rows' citations. One sentence, the same in Charts and
// the report. The shipped content names no commercial organisation or product (core_pack_test), so
// the non-literature sources are recognised by the generic wording the norm rows carry for them:
// "motion-capture app" for a vendor's published figures, "a coaching chain's published" for a
// coaching chain's published observation. Change the rows' wording and these markers together.
QString bandSourcesOf(const std::vector<const PostureBand *> &used)
{
    bool kim = false, evans = false, vendor = false, coaching = false, authored = false;
    for (const PostureBand *b : used) {
        const bool k = b->citation.contains(QLatin1String("Kim SE")), e = b->citation.contains(QLatin1String("Evans K")),
                   s = b->citation.contains(QLatin1String("motion-capture app")),
                   g = b->citation.contains(QLatin1String("a coaching chain's published"));
        kim |= k; evans |= e; vendor |= s; coaching |= g;
        if (!k && !e && !s && !g) authored = true;
    }
    QStringList parts;
    if (kim) parts << QStringLiteral("20 tour professionals (Kim 2021)");
    if (evans) parts << QStringLiteral("29 skilled men (Evans 2008)");
    if (vendor) parts << QStringLiteral("a vendor's published figures");
    if (coaching) parts << QStringLiteral("a coaching chain's published observations");
    QString s;
    if (!parts.isEmpty()) {
        const QString last = parts.takeLast();
        s = QStringLiteral("Bands from ") + (parts.isEmpty() ? last : parts.join(QStringLiteral(", ")) + QStringLiteral(" and ") + last);
        if (authored) s += QStringLiteral("; authored seeds where no source gives one");
    } else if (authored) {
        s = QStringLiteral("Bands from authored seeds where no source gives one");
    }
    return s.isEmpty() ? s : s + QLatin1Char('.');
}

// A row with no value at any position (decision 13).
bool rowHasValue(const std::array<double, kPosturePositions> &v) { return std::any_of(v.begin(), v.end(), fin); }

// "no ruler: no ball to measure with in either view" — a gate's words, then its evidence lower-cased, no stop.
QString refusalWords(const QString &words, const QString &why)
{
    QString w = why.trimmed();
    while (w.endsWith(QLatin1Char('.'))) w.chop(1);
    if (!w.isEmpty() && !(w.size() > 1 && w.at(1).isUpper())) w = w.left(1).toLower() + w.mid(1);
    if (words.isEmpty()) return w;
    return w.isEmpty() || w.compare(words, Qt::CaseInsensitive) == 0 ? words : words + QStringLiteral(": ") + w;
}
// The row's reason on one shot: the first refusing gate's words over its positions, and its evidence.
QString absentWhyOf(const PostureShotInput &s, PostureRow r)
{
    const size_t R = size_t(ri(r));
    QString words;
    for (const QString &g : s.gateWords[R]) if (!g.isEmpty()) { words = g; break; }
    return refusalWords(words, s.gateWhy[R]);
}
// "Hip depth: not measured on this shot — no ruler: …." Rows with the same reason share a line, in row order.
// `where`: "on this shot" | "on any swing" | "on 2 of 9 swings" per row; `why` per row ("" none).
QStringList absentLines(const std::array<QString, kPostureRows> &where, const std::array<QString, kPostureRows> &why)
{
    QStringList out;
    std::array<bool, kPostureRows> said{};
    for (int r = 0; r < kPostureRows; ++r) {
        if (where[size_t(r)].isEmpty() || said[size_t(r)]) continue;
        QStringList labels;
        for (int k = r; k < kPostureRows; ++k)
            if (!said[size_t(k)] && where[size_t(k)] == where[size_t(r)] && why[size_t(k)] == why[size_t(r)]) {
                labels << (labels.isEmpty() ? postureRowSpecs()[size_t(k)].label : postureRowSpecs()[size_t(k)].label.toLower());
                said[size_t(k)] = true;
            }
        out << W::list(labels) + QStringLiteral(": not measured ") + where[size_t(r)]
                   + (why[size_t(r)].isEmpty() ? QString() : QStringLiteral(" — ") + why[size_t(r)]) + QLatin1Char('.');
    }
    return out;
}

// Decision 15: which reading hip depth at P3–P5 holds, from the shots on each source per cell (k = 0..2 for
// P3..P5). `oneShot`: the shot view ("at P5"); else the session's counts ("on 26 of 27 shots"). `warn` is set
// when any cell is the joints'. "" when no cell has a value.
QString hipSourceLine(const std::array<int, 3> &butt, const std::array<int, 3> &joints, bool oneShot,
                      const QString &one, const QString &many, bool *warn)
{
    *warn = false;
    std::vector<int> cells, b, j;
    for (int k = 0; k < 3; ++k) {
        if (butt[size_t(k)] + joints[size_t(k)] == 0) continue;
        cells.push_back(k);
        if (butt[size_t(k)] > 0) b.push_back(k);
        if (joints[size_t(k)] > 0) { j.push_back(k); *warn = true; }
    }
    if (cells.empty()) return QString();
    const auto tags = [](const std::vector<int> &ks) -> QString {
        if (ks.size() == 3) return QStringLiteral("P3–P5");
        QStringList t;
        for (int k : ks) t << posturePositionTag(kP3 + k);
        return W::list(t);
    };
    const QString turn = QStringLiteral("the back of the hips seen down the line, which the turn does not drag");
    const QString why = postureHipJointsReason();
    if (oneShot) {
        if (j.empty()) return QStringLiteral("Hip depth at %1: the butt line, %2.").arg(tags(cells), turn);
        if (b.empty()) return QStringLiteral("Hip depth at %1: the hip joints (⚠), no butt line on this %2 — %3.").arg(tags(cells), one, why);
        return QStringLiteral("Hip depth at %1: the butt line at %2; the hip joints (⚠) at %3 — %4.").arg(tags(cells), tags(b), tags(j), why);
    }
    const auto counts = [&](int k) -> QString {
        const int nb = butt[size_t(k)], nj = joints[size_t(k)];
        if (nj == 0) return QStringLiteral("the butt line on every %1").arg(one);
        if (nb == 0) return QStringLiteral("the hip joints (⚠) on every %1, no butt line").arg(one);
        return QStringLiteral("the butt line on %1 of %2 %3; the hip joints (⚠) on %4").arg(nb).arg(nb + nj).arg(many).arg(nj);
    };
    const bool same = std::all_of(cells.begin(), cells.end(), [&](int k) {
        return butt[size_t(k)] == butt[size_t(cells.front())] && joints[size_t(k)] == joints[size_t(cells.front())];
    });
    QString line;
    if (same) {
        line = QStringLiteral("Hip depth at %1: %2").arg(tags(cells), counts(cells.front()));
    } else {
        QStringList parts;
        for (int k : cells) parts << QStringLiteral("at %1 %2").arg(posturePositionTag(kP3 + k), counts(k));
        line = QStringLiteral("Hip depth ") + parts.join(QStringLiteral("; "));
    }
    return line + (*warn ? QStringLiteral(" — ") + why + QLatin1Char('.') : QStringLiteral(", ") + turn + QLatin1Char('.'));
}

PostureTrust trustOf(const std::array<PostureRowView, kPostureRows> &rows, const QStringList &address, const QStringList &absent = {},
                     const QString &hipLine = {}, bool hipWarn = false)
{
    PostureTrust t;
    t.hipSource = hipLine;
    QStringList faceOn, dtl;
    std::vector<const PostureBand *> used;
    QStringList gateLines;
    for (const PostureRowView &r : rows) {
        if (!r.present) continue;
        (postureRowSpec(r.row).camera == PostureCamera::FaceOn ? faceOn : dtl) << r.label.toLower();
        // a row with no value says its reason once, in `absent`
        const bool anyValue = std::any_of(r.cells.begin(), r.cells.end(), [](const PostureCellView &c) { return fin(c.value); });
        for (const PostureCellView &c : r.cells) {
            if (c.band.has) used.push_back(&c.band);
            if (anyValue && !c.gateWords.isEmpty()) {
                const QString g = r.label + QStringLiteral(": ") + c.gateWords + QLatin1Char('.');
                if (!gateLines.contains(g)) gateLines << g;
            }
        }
        // decision 16: trunk bend with a back-line reading says both readings in one sentence; pelvis height
        // says its band there is an authored seed
        const bool backLine = r.row == PostureRow::Bend
                              && std::any_of(r.cells.begin(), r.cells.end(), [](const PostureCellView &c) { return fin(c.altValue); });
        const bool seeded = r.row == PostureRow::PelvisHeight
                            && std::any_of(r.cells.begin() + kP3, r.cells.begin() + kP5 + 1, [](const PostureCellView &c) { return c.band.has; });
        if (backLine)
            t.warnings << postureBackLineTrust();
        else if (const QString why = postureWarnReason(r.row, kP3); !why.isEmpty())
            t.warnings << QStringLiteral("⚠ %1 at P3–P5: %2%3.").arg(r.label, why,
                                                                  seeded ? QStringLiteral("; the band there is an authored seed") : QString());
        if (r.row == PostureRow::Hips && hipWarn && !hipLine.isEmpty()) t.warnings << hipLine;   // decision 15
    }
    if (!faceOn.isEmpty()) t.cameras << W::cap1(W::list(faceOn)) + QStringLiteral(": the face-on camera.");
    if (!dtl.isEmpty()) t.cameras << W::cap1(W::list(dtl)) + QStringLiteral(": the down-the-line camera.");
    t.warnings << gateLines;
    t.absent = absent;
    t.bandSources = bandSourcesOf(used);
    if (rows[size_t(ri(PostureRow::Bend))].present)
        t.trunkDatum = QStringLiteral("Trunk bend is published on the line from the base of the neck to the sacrum; ours reads "
                                      "the hip and shoulder joints; expect a few degrees between them.");
    if (address.contains(postureRigRule())) t.rigRule = postureRigRule();
    t.lines << t.cameras;
    if (!hipWarn && !hipLine.isEmpty()) t.lines << hipLine;   // the butt line: said, not warned
    t.lines << t.absent << t.warnings;
    if (!t.bandSources.isEmpty()) t.lines << t.bandSources;
    if (!t.trunkDatum.isEmpty()) t.lines << t.trunkDatum;
    t.lines << address;   // the "At address" block's: the rig rule first, then its set-up key, sources, what is not read
    return t;
}

QString modal(const QStringList &xs)
{
    QHash<QString, int> n;
    QString best;
    int bestN = 0;
    for (const QString &x : xs) {
        const int k = ++n[x];
        if (k > bestN) { bestN = k; best = x; }
    }
    return best;
}

QSet<QString> postureConditionIds(const PostureEnv &env)
{
    QSet<QString> out;
    for (const PostureFaultLink &l : env.faultLinks) out.insert(l.conditionId);
    for (const DepartureDef &d : departureDefs()) for (const char *c : d.conds) out.insert(QString::fromLatin1(c));
    out.insert(QStringLiteral("trail_knee_straighten"));
    out.insert(QStringLiteral("loss_of_posture"));   // a Finding when it fires; it names no departure
    if (env.pack)
        for (auto it = out.begin(); it != out.end();) it = env.pack->condition(*it) ? std::next(it) : out.erase(it);
    return out;
}

} // namespace

// ── vocabulary ──────────────────────────────────────────────────────────────────────────────────

const std::array<PostureRowSpec, kPostureRows> &postureRowSpecs()
{
    static const std::array<PostureRowSpec, kPostureRows> s = buildSpecs();
    return s;
}
const PostureRowSpec &postureRowSpec(PostureRow r) { return postureRowSpecs()[size_t(ri(r))]; }

const std::array<int, kPosturePositions> &posturePhaseIds()
{
    static const std::array<int, kPosturePositions> p{
        int(pa::Phase::Address), int(pa::Phase::ShaftParallelBack), int(pa::Phase::MidBackswing), int(pa::Phase::Top),
        int(pa::Phase::ArmParallelDown), int(pa::Phase::Delivery), int(pa::Phase::Impact), int(pa::Phase::ShaftParallelThrough) };
    return p;
}

QString posturePositionTag(int pos) { return QStringLiteral("P%1").arg(pos + 1); }
QString posturePositionWhen(int pos)
{
    if (pos == 0) return QStringLiteral("address");
    if (pos == kP4) return QStringLiteral("the top");
    if (pos == kP7) return QStringLiteral("impact");
    return posturePositionTag(pos);
}
QString posturePatternName(PosturePattern p) { return p == PosturePattern::SquatPush ? QStringLiteral("Squat and push") : QStringLiteral("Level"); }
QString posturePatternContext(PosturePattern p) { return p == PosturePattern::SquatPush ? QStringLiteral("posture_squat_push") : QStringLiteral("posture_level"); }
QString postureCameraWords(PostureCamera c) { return c == PostureCamera::FaceOn ? QStringLiteral("face-on") : QStringLiteral("down the line"); }
QString postureTrailReleasedContext() { return QStringLiteral("posture_trail_released"); }

int postureWorkOnRank(const QStringList *order, const QString &conditionId)
{
    const int i = order ? int(order->indexOf(conditionId)) : -1;
    return i >= 0 ? i : std::numeric_limits<int>::max();
}
int postureDepartureRank(const QStringList *order, const QStringList &conditionIds, const PostureFiredTest &counts)
{
    int best = std::numeric_limits<int>::max();
    for (const QString &id : conditionIds)
        if (!counts || counts(id)) best = std::min(best, postureWorkOnRank(order, id));
    return best;
}

QString postureWarnReason(PostureRow r, int pos)
{
    if (pos < kP3 || pos > kP5) return QString();
    switch (r) {
    case PostureRow::Bend:         return QStringLiteral("the shoulders turning out of the down-the-line picture move the joint line");
    case PostureRow::PelvisHeight: return QStringLiteral("the hip joints migrate at the top");
    default:                       return QString();   // hip depth: the shot's own, decision 15
    }
}
bool postureHipButtPos(int pos) { return pos >= kP3 && pos <= kP5; }
QString postureHipJointsMeasure(int pos)
{
    return postureHipButtPos(pos) ? QStringLiteral("m_pelvisThrust_p%1").arg(pos + 1) : QString();
}
QString postureHipJointsReason() { return QStringLiteral("the hip points turn with the pelvis"); }
QString postureHipButtKey() { return QStringLiteral("hipDepthButt"); }

PostureFlags postureShotWarn(const PostureShotInput &shot)
{
    PostureFlags w = shot.gateWarn;
    const size_t H = size_t(ri(PostureRow::Hips));
    for (int p = kP3; p <= kP5; ++p)
        if (shot.hipSource[size_t(p)] == PostureHipSource::Joints) w[H][size_t(p)] = true;
    return w;
}
bool    postureBackLinePos(int pos) { return pos >= kP3 && pos <= kP5; }
QString postureBackLineKey() { return QStringLiteral("spineBackLine"); }
QString postureBackLineMeasure(int pos)
{
    return postureBackLinePos(pos) ? QStringLiteral("m_spineBackLine_p%1").arg(pos + 1) : QString();
}
QString postureBackLineLabel() { return QStringLiteral("back line"); }
QString postureBackLineWarnShort() { return QStringLiteral("can catch the arms at the top"); }
QString postureBackLineTrust()
{
    return QStringLiteral("Trunk bend at P3–P5 is shown two ways: from the hip and shoulder joints (⚠ the turn moves them) and "
                          "from the back line in the picture (⚠ it can catch the arms at the top). Neither is trusted yet; more "
                          "swings will decide.");
}

std::vector<PostureDepartureSpec> postureDepartureSpecs(const PostureEnv &env)
{
    std::vector<PostureDepartureSpec> out;
    for (const DepartureDef &d : departureDefs()) {
        const QString id = QString::fromLatin1(d.id);
        out.push_back({ id, QString::fromLatin1(d.name), departureConditions(id, env) });
    }
    return out;
}

QStringList postureConditionList(const PostureEnv &env)
{
    const QSet<QString> mine = postureConditionIds(env);
    QStringList out;
    if (env.pack) {
        for (const pa::Condition &c : env.pack->conditions) if (mine.contains(c.id) && !out.contains(c.id)) out << c.id;
    } else {
        out = QStringList(mine.begin(), mine.end());
        out.sort();
    }
    return out;
}

QString postureFigure(PostureRow r, double v)
{
    const QString f = W::fig(v, postureRowSpec(r).decimals);
    if (f == QLatin1String("-")) return f;
    const double p = std::pow(10.0, postureRowSpec(r).decimals);
    return (std::round(v * p) / p > 0 ? QStringLiteral("+") : QString()) + f;
}

std::array<double, kPosturePositions> postureEmptyCells()
{
    std::array<double, kPosturePositions> a;
    a.fill(kNaN);
    return a;
}

PostureGrid postureEmptyGrid()
{
    PostureGrid g;
    for (auto &row : g) row.fill(kNaN);
    return g;
}

QString postureRigRule()
{
    return QStringLiteral("How bent you are at address depends on where the camera stands, so read the good-player band as a "
                          "guide and your own sessions from this set-up as the real comparison.");
}

QString postureRigWarnShort() { return QStringLiteral("depends on where the camera stands"); }

QString postureAddressFigure(double v, const QString &unit)
{
    return withUnit(W::fig(v, unit == QStringLiteral("cm") ? 1 : 0), unit);
}

double PostureBand::z(double v) const
{
    if (!has || !fin(v)) return kNaN;
    const double s = v < mu ? sigmaLo : sigmaHi;
    return s > 0.0 ? (v - mu) / s : kNaN;
}

// ── environment ─────────────────────────────────────────────────────────────────────────────────

namespace {
// One cell's band at a context, `exactOnly`: only a row written AT that node (no fallback up the tree).
PostureBand resolveBand(const PostureEnv &env, const QString &id, const QString &ctx, bool exactOnly)
{
    PostureBand b;
    if (!env.norms || id.isEmpty()) return b;
    const pa::NormResolution res = env.norms->resolve(id, ctx);
    if (!res.found() || (exactOnly && res.contextId != ctx)) return b;
    const pa::Measure *m = env.pack ? env.pack->measure(id) : nullptr;
    const pa::Shape shape = m ? m->shape : pa::Shape::Target;
    const pa::NormBandEdges e = pa::bandEdgesOf(*res.norm, shape, env.policy);
    b.has = true;
    b.mu = res.norm->mu; b.sigmaLo = res.norm->sigmaLo; b.sigmaHi = res.norm->sigmaHi;
    b.lo = e.idealLo; b.hi = e.idealHi; b.watchLo = e.watchLo; b.watchHi = e.watchHi;
    b.measureId = id; b.contextId = res.contextId; b.inherited = res.inherited;
    b.source = pa::normSourceName(res.norm->source);
    b.citation = res.norm->citation;
    b.n = res.norm->n;
    return b;
}
} // namespace

PostureBandGrid postureBands(const PostureEnv &env, PosturePattern pat)
{
    PostureBandGrid g;
    if (!env.norms) return g;
    const QString ctx = posturePatternContext(pat);
    for (int r = 0; r < kPostureRows; ++r) {
        const PostureRowSpec &s = postureRowSpecs()[size_t(r)];
        for (int p = 1; p < kPosturePositions; ++p) {
            g[size_t(r)][size_t(p)] = resolveBand(env, s.measureIds[size_t(p)], ctx, false);
        }
    }
    return g;
}

PostureBandGrid postureBandsFor(const PostureEnv &env, PosturePattern p, const PostureTrailStyle &trail)
{
    PostureBandGrid g = env.bands(p);
    if (!trail.released()) return g;
    const size_t T = size_t(ri(PostureRow::TrailKnee));
    for (int pos = kP2; pos <= kP4; ++pos)
        if (env.trailReleased[T][size_t(pos)].has) g[T][size_t(pos)] = env.trailReleased[T][size_t(pos)];
    return g;
}

namespace {
std::vector<PostureFaultLink> faultLinksOf(const pa::CharacteristicPack &pack)
{
    std::vector<PostureFaultLink> out;
    for (const pa::Condition &c : pack.conditions) {
        if (c.state == pa::ConditionState::Retired || c.state == pa::ConditionState::Superseded) continue;
        for (const QString &sid : c.detectedBy) {
            const pa::Signal *sg = pack.signal(sid);
            if (!sg || sg->test != pa::SignalTest::OutsideCorridor || sg->measures.size() != 1) continue;
            const pa::Measure *m = pack.measure(sg->measures.front());
            if (!m || m->kind == pa::MeasureKind::Composite) continue;
            // anchored at address: the change from address this grid draws (decision 5)
            if (!m->reducer.anchor || *m->reducer.anchor != pa::Phase::Address) continue;
            int a = -1, b = -1;
            if (m->reducer.kind == pa::ReducerKind::Delta) {
                a = b = posOfPhase(m->reducer.window.second);
            } else if (m->reducer.kind == pa::ReducerKind::Extremum) {
                a = posOfPhase(m->reducer.window.first);
                b = posOfPhase(m->reducer.window.second);
            } else {
                continue;
            }
            if (a < 0 || b < 0) continue;
            const QStringList ladder = pa::measureKeyLadder(*m);
            for (int r = 0; r < kPostureRows; ++r) {
                const PostureRowSpec &s = postureRowSpecs()[size_t(r)];
                if (!ladder.contains(s.metricKey)) continue;
                for (int p = std::max(1, std::min(a, b)); p <= std::max(a, b); ++p) {
                    PostureFaultLink l;
                    l.row = s.row; l.pos = p;
                    l.high = sg->direction.value_or(pa::Direction::High) == pa::Direction::High;
                    l.conditionId = c.id; l.label = c.label.isEmpty() ? c.id : c.label;
                    l.measureId = m->id; l.shape = m->shape;
                    out.push_back(l);
                }
            }
        }
    }
    return out;
}
} // namespace

PostureEnv postureEnv(const pa::CharacteristicPack *pack, std::shared_ptr<const pa::INormProvider> norms,
                      const pa::GradePolicy &policy)
{
    PostureEnv e;
    e.pack = pack;
    e.norms = std::move(norms);
    e.policy = policy;
    e.level = postureBands(e, PosturePattern::Level);
    e.squatPush = postureBands(e, PosturePattern::SquatPush);
    {
        const PostureRowSpec &t = postureRowSpec(PostureRow::TrailKnee);
        for (int p = kP2; p <= kP4; ++p)
            e.trailReleased[size_t(ri(PostureRow::TrailKnee))][size_t(p)] =
                resolveBand(e, t.measureIds[size_t(p)], postureTrailReleasedContext(), true);
    }
    if (pack) {
        e.faultLinks = faultLinksOf(*pack);
        for (const pa::Measure &m : pack->measures) {
            if (!m.rigSensitive || m.kind == pa::MeasureKind::Composite) continue;
            const QStringList ladder = pa::measureKeyLadder(m);
            if (ladder.isEmpty()) continue;
            PostureAbsoluteSpec a;
            a.measureId = m.id; a.label = m.label.isEmpty() ? m.id : m.label; a.unit = m.unit; a.metricKey = ladder.front();
            // the golfer's words, the block's title saying "at address" once
            QString nm = m.golferName.trimmed();
            for (const QString &tail : { QStringLiteral(" at address"), QStringLiteral(" at P1") })
                if (nm.endsWith(tail, Qt::CaseInsensitive)) nm.chop(tail.size());
            a.name = W::cap1(nm.isEmpty() ? a.label : nm);
            // the view it reads: the measure's own, else the posture row on the same series, else down the line
            if (m.viewNeeded == pa::ViewNeeded::FaceOn) a.camera = PostureCamera::FaceOn;
            else if (m.viewNeeded == pa::ViewNeeded::DownTheLine) a.camera = PostureCamera::Dtl;
            else {
                a.camera = PostureCamera::Dtl;
                for (const PostureRowSpec &rs : postureRowSpecs())
                    if (ladder.contains(rs.metricKey)) { a.camera = rs.camera; break; }
            }
            e.absolutes.push_back(a);
        }
    }
    return e;
}

const PostureEnv &postureSharedEnv()
{
    static const std::unique_ptr<pa::ICharacteristicPackProvider> provider = pa::makeCharacteristicPackProvider();
    static const PostureEnv env = postureEnv(provider ? &provider->pack() : nullptr, pa::sharedNormProvider());
    return env;
}

PostureFaultGrid postureFaults(const PostureEnv &env, const QString &gradingContextId)
{
    PostureFaultGrid g;
    if (!env.norms) return g;
    for (const PostureFaultLink &l : env.faultLinks) {
        const pa::NormResolution res = env.norms->resolve(l.measureId, gradingContextId);
        if (!res.found()) continue;
        double dlo = 0.0, dhi = 0.0;
        pa::deviationEdges(*res.norm, l.shape, env.policy, dlo, dhi);
        const pa::NormBandEdges e = pa::bandEdgesOf(*res.norm, l.shape, env.policy);
        const bool open = l.high ? e.highOpen : e.lowOpen;
        if (open) continue;
        const double edge = l.high ? std::max(dhi, e.idealHi) : std::min(dlo, e.idealLo);
        if (!fin(edge)) continue;
        PostureFault &f = l.high ? g[size_t(ri(l.row))][size_t(l.pos)].hi : g[size_t(ri(l.row))][size_t(l.pos)].lo;
        // two conditions on one side: the line drawn is the one that fires first
        if (f.has && (l.high ? f.edge <= edge : f.edge >= edge)) continue;
        f.has = true; f.edge = edge; f.conditionId = l.conditionId; f.label = l.label; f.measureId = l.measureId;
        f.contextId = res.contextId;
    }
    return g;
}

// ── input ───────────────────────────────────────────────────────────────────────────────────────

int postureConditionPos(const ShotSeries *s, const PostureEnv &env, const QString &id)
{
    const auto posOf = [](int ph) { return ph >= 0 ? posOfPhase(static_cast<pa::Phase>(ph)) : -1; };
    if (s) {
        for (const auto *h : { &s->diagnostics.drivenBy, &s->diagnostics.gradedBy }) {
            const auto it = h->constFind(id);
            if (it == h->constEnd()) continue;
            int ph = it->phase;
            if (ph < 0 && env.pack && !it->measureId.isEmpty()) ph = pa::measureReading(*env.pack, it->measureId).phase;
            if (const int p = posOf(ph); p >= 0) return p;
        }
    }
    if (env.pack)
        for (const pa::ConditionReading &r : pa::conditionReadings(*env.pack, id))
            if (r.found && r.phase >= 0) { if (const int p = posOf(r.phase); p >= 0) return p; }
    return -1;
}

PostureShotInput postureInputFromValues(const PostureGrid &changes, int ordinal, const QStringList &fired)
{
    PostureShotInput in;
    in.ordinal = ordinal;
    in.value = changes;
    in.fired = fired;
    in.graded = !fired.isEmpty();
    for (int r = 0; r < kPostureRows; ++r)
        in.present[size_t(r)] = std::any_of(changes[size_t(r)].begin(), changes[size_t(r)].end(), fin);
    // decision 15: a fed hip depth at P3–P5 is the joints' (the §2.1 table's numbers are)
    for (int p = kP3; p <= kP5; ++p)
        if (fin(changes[size_t(ri(PostureRow::Hips))][size_t(p)])) in.hipSource[size_t(p)] = PostureHipSource::Joints;
    return in;
}

PostureShotInput postureInputFromShot(const ShotSeries &shot, const PostureEnv &env)
{
    PostureShotInput in;
    in.ordinal = shot.ordinal; in.shotId = shot.shotId; in.swingDir = shot.swingDir; in.club = shot.club;

    // The keys the cells and the absolutes read, and the shot's series for them as the analysis block
    // spells them — so the ONE sampler (buildPhaseGrid) reads them exactly as it reads swing.json. A
    // non-finite curve value is handed over as a bridged sample (valid 0), never as a number.
    QStringList keys;
    for (const PostureRowSpec &s : postureRowSpecs()) keys << s.metricKey;
    keys << postureHipButtKey();   // hip depth at P3–P5 (decision 15)
    keys << postureBackLineKey();  // trunk bend's second reading at P3–P5 (decision 16)
    for (const PostureAbsoluteSpec &a : env.absolutes)
        if (const pa::Measure *m = env.pack ? env.pack->measure(a.measureId) : nullptr)
            for (const QString &k : pa::measureKeyLadder(*m)) if (!keys.contains(k)) keys << k;

    QJsonArray phases, metrics;
    for (const PhaseMark &pm : shot.phases)
        phases.append(QJsonObject{ { QStringLiteral("phase"), pm.phase }, { QStringLiteral("t_us"), QJsonValue(qint64(pm.tUs)) } });
    for (const QString &k : keys) {
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
    const pa::SwingPhaseGrid grid = pa::buildPhaseGrid(
        QJsonObject{ { QStringLiteral("phases"), phases }, { QStringLiteral("metrics"), metrics } }, pa::PhaseGridConfig{});

    // The instant of each position, ms from impact (for a masking gate's stretch).
    std::array<double, kPosturePositions> atMs;
    atMs.fill(kNaN);
    for (int p = 0; p < kPosturePositions; ++p)
        for (const PhaseMark &pm : shot.phases)
            if (pm.phase == posturePhaseIds()[size_t(p)] && shot.impactUs >= 0) { atMs[size_t(p)] = double(pm.tUs - shot.impactUs) / 1000.0; break; }

    // One cell through the ONE sampler: the pack's measure, else the change from address on `key`.
    const auto readCell = [&](const QString &measureId, const QString &key, int p) {
        const pa::Measure *m = env.pack ? env.pack->measure(measureId) : nullptr;
        std::optional<double> o;
        if (m) {
            o = pa::reduceOverGrid(grid, *m);
        } else {
            pa::Reducer red;
            red.kind = pa::ReducerKind::Delta;
            red.anchor = pa::Phase::Address;
            red.window = { pa::Phase::Address, static_cast<pa::Phase>(posturePhaseIds()[size_t(p)]) };
            o = pa::reduceOverGrid(grid, key, red);
        }
        return o && fin(*o) ? *o : kNaN;
    };
    const QString buttKey = postureHipButtKey();
    for (int r = 0; r < kPostureRows; ++r) {
        const PostureRowSpec &s = postureRowSpecs()[size_t(r)];
        const bool hips = s.row == PostureRow::Hips;
        in.present[size_t(r)] = shot.metric(s.metricKey) != nullptr || (hips && shot.metric(buttKey) != nullptr);
        const pa::MetricPhaseGrid *mg = grid.metric(s.metricKey);
        const pa::MetricPhaseGrid *bg = hips ? grid.metric(buttKey) : nullptr;
        for (int p = 0; p < kPosturePositions; ++p) {
            double v = kNaN;
            QString gateKey = s.metricKey;
            if (p == 0) {
                if ((mg && mg->at(pa::Phase::Address)) || (bg && bg->at(pa::Phase::Address))) v = 0.0;
            } else if (hips && postureHipButtPos(p)) {
                // decision 15: the butt line; where it is refused, the joints, under the ⚠
                v = readCell(s.measureIds[size_t(p)], buttKey, p);
                if (fin(v)) {
                    in.hipSource[size_t(p)] = PostureHipSource::ButtLine;
                    gateKey = buttKey;
                } else {
                    v = readCell(postureHipJointsMeasure(p), s.metricKey, p);
                    if (fin(v)) in.hipSource[size_t(p)] = PostureHipSource::Joints;
                }
            } else if (!s.measureIds[size_t(p)].isEmpty()) {
                v = readCell(s.measureIds[size_t(p)], s.metricKey, p);
            }
            in.value[size_t(r)][size_t(p)] = v;
            const GateHit g = gateFor(shot, gateKey, atMs[size_t(p)]);
            if (g.warn || !fin(v)) in.gateWords[size_t(r)][size_t(p)] = g.words;
            in.gateWarn[size_t(r)][size_t(p)] = g.warn;
            if (g.warn && in.gateWhy[size_t(r)].isEmpty()) in.gateWhy[size_t(r)] = g.why;
        }
    }

    // decision 16: the back line at P3–P5, through the same sampler (no series: NaN, drawn "-")
    if (shot.metric(postureBackLineKey()))
        for (int p = kP3; p <= kP5; ++p) in.backLine[size_t(p)] = readCell(postureBackLineMeasure(p), postureBackLineKey(), p);

    in.absolutes.assign(env.absolutes.size(), kNaN);
    for (size_t i = 0; i < env.absolutes.size(); ++i)
        if (const pa::Measure *m = env.pack ? env.pack->measure(env.absolutes[i].measureId) : nullptr)
            if (const std::optional<double> o = pa::reduceOverGrid(grid, *m); o && fin(*o)) in.absolutes[i] = *o;

    const ShotDiagnostics &d = shot.diagnostics;
    in.fired = d.fired; in.clean = d.clean;
    in.graded = !d.fired.isEmpty() || !d.clean.isEmpty();
    for (const QString &id : d.fired) in.conditionPos.insert(id, postureConditionPos(&shot, env, id));
    for (auto it = d.gradedBy.constBegin(); it != d.gradedBy.constEnd() && in.contextId.isEmpty(); ++it)
        in.contextId = it.value().contextId;
    if (in.contextId.isEmpty() && !shot.club.isEmpty()) in.contextId = pa::contextIdForClub(shot.club);
    return in;
}

// ── matching ────────────────────────────────────────────────────────────────────────────────────

PostureMatch postureMatch(const PostureGrid &x, const std::array<bool, kPostureRows> &present,
                          const PostureFlags &warn, const PostureEnv &env)
{
    PostureMatch m;
    double sum[2] = { 0.0, 0.0 };
    int n[2] = { 0, 0 };
    QStringList rows;
    for (int r = 0; r < kPostureRows; ++r) {
        const PostureRow row = PostureRow(r);
        if (!present[size_t(r)]) continue;
        for (int p = kP2; p <= kP7; ++p) {
            if (!matchEligible(row, p) || cellWarned(warn, row, p)) continue;
            const double v = x[size_t(r)][size_t(p)];
            if (!fin(v)) continue;
            for (int k = 0; k < 2; ++k) {
                const double z = env.bands(PosturePattern(k))[size_t(r)][size_t(p)].z(v);
                if (!fin(z)) continue;
                sum[k] += z * z; ++n[k];
                if (!rows.contains(postureRowSpecs()[size_t(r)].key)) rows << postureRowSpecs()[size_t(r)].key;
            }
        }
    }
    m.rows = rows;
    m.cells = std::max(n[0], n[1]);
    if (!n[0] || !n[1]) { m.basis = QStringLiteral("none"); m.fit = QStringLiteral("none"); return m; }
    m.ok = true;
    m.dLevel = std::sqrt(sum[0] / n[0]);
    m.dSquatPush = std::sqrt(sum[1] / n[1]);
    m.nearest = m.dSquatPush < m.dLevel ? PosturePattern::SquatPush : PosturePattern::Level;
    const double dNear = std::min(m.dLevel, m.dSquatPush);
    m.gap = std::fabs(std::round(m.dSquatPush * 100.0) - std::round(m.dLevel * 100.0)) / 100.0;   // decision 3
    const QString near = posturePatternName(m.nearest);
    if (m.gap <= kPostureCloseCall + 1e-9 && dNear <= 2.0) {
        m.fit = QStringLiteral("between");
        m.words = QStringLiteral("between Level and Squat and push (%1 nearer)").arg(near);
    } else if (dNear < 1.0) {
        m.fit = QStringLiteral("close");
        m.words = QStringLiteral("a close match to %1").arg(near);
    } else if (dNear <= 2.0) {
        m.fit = QStringLiteral("nearest");
        m.words = QStringLiteral("nearest to %1").arg(near);
    } else {
        m.fit = QStringLiteral("none");
        m.words = QStringLiteral("no pattern fits closely; nearest is %1").arg(near);
    }
    const auto has = [&](const char *k) { return rows.contains(QLatin1String(k)); };
    const bool face = has("head") || has("pelvisHeight");
    const bool dtl = has("hips") || has("bend") || has("leadKnee");
    QStringList labels;
    for (const QString &k : rows)
        for (const PostureRowSpec &s : postureRowSpecs()) if (s.key == k) labels << s.label.toLower();
    if (face && dtl) {
        m.basis = QStringLiteral("both");
    } else if (face) {
        m.basis = QStringLiteral("faceOn");
        m.basisWords = QStringLiteral("Matched on %1 alone: this capture has no down-the-line camera.").arg(W::list(labels));
    } else {
        m.basis = QStringLiteral("dtl");
        m.basisWords = QStringLiteral("Matched on %1 alone: this capture has no face-on camera.").arg(W::list(labels));
    }
    return m;
}

// ── exits and departures ────────────────────────────────────────────────────────────────────────

QString postureWd6Words(int pos)
{
    return QStringLiteral("outside the good-player band at %1, not far enough to be a fault").arg(posturePositionTag(pos));
}

std::vector<PostureExit> postureExits(const PostureGrid &x, const std::array<bool, kPostureRows> &present,
                                      const PostureFlags &warn, const PostureBandGrid &bands, const PostureEnv &env,
                                      const PostureFiredTest &fired)
{
    std::vector<PostureExit> out;
    for (int p = kP2; p <= kP8; ++p) {
        for (int r = 0; r < kPostureRows; ++r) {
            const PostureRow row = PostureRow(r);
            if (row == PostureRow::TrailKnee || !present[size_t(r)] || cellWarned(warn, row, p)) continue;
            const double v = x[size_t(r)][size_t(p)];
            const double z = cellZ(x, bands, row, p);
            if (!fin(v) || !fin(z) || std::fabs(z) <= kPostureExitZ) continue;
            PostureExit e;
            e.row = row; e.pos = p; e.value = v; e.z = z;
            e.departure = departureFor(row, p, z, x, warn, bands, &e.name);
            e.conditionIds = departureConditions(e.departure, env);
            e.named = fired && std::any_of(e.conditionIds.begin(), e.conditionIds.end(), fired);
            e.words = exitWords(e);
            out.push_back(e);
        }
    }
    return out;
}

std::vector<PostureDeparture> postureDepartures(const std::vector<PostureExit> &exits, const PostureGrid &x,
                                                const PostureEnv &env, const PostureFiredTest &fired)
{
    std::vector<PostureDeparture> out;
    const auto add = [&](const QString &id, int pos) {
        if (id.isEmpty()) return;
        for (PostureDeparture &d : out)
            if (d.id == id) { if (std::find(d.positions.begin(), d.positions.end(), pos) == d.positions.end()) d.positions.push_back(pos); return; }
        PostureDeparture d;
        d.id = id;
        if (const DepartureDef *def = departureDef(id)) d.name = QString::fromLatin1(def->name);
        d.conditionIds = departureConditions(id, env);
        d.firstPos = pos;
        d.positions = { pos };
        out.push_back(d);
    };
    for (const PostureExit &e : exits) if (e.named) add(e.departure, e.pos);
    // D5: a dip going back (a D4 candidate, named or not), then the head above address by impact — named
    // only when one of its own conditions fired
    const bool dipped = std::any_of(exits.begin(), exits.end(), [](const PostureExit &e) { return e.departure == QLatin1String("D4"); });
    const QStringList d5 = departureConditions(QStringLiteral("D5"), env);
    if (dipped && fin(x[0][kP7]) && x[0][kP7] > 0.0 && fired && std::any_of(d5.begin(), d5.end(), fired))
        add(QStringLiteral("D5"), kP7);
    return out;
}

// ── "what moved" and the trail knee ─────────────────────────────────────────────────────────────

PostureTrailStyle postureTrailStyle(const PostureGrid &x)
{
    PostureTrailStyle t;
    const double v = x[size_t(ri(PostureRow::TrailKnee))][kP4];
    if (!fin(v)) return t;
    t.known = true;
    t.changeAtTop = v;
    t.kept = v > -kPostureTrailKeptDeg;
    t.word = t.kept ? QStringLiteral("kept") : QStringLiteral("released");
    t.words = t.kept ? QStringLiteral("trail knee kept its flex")
                     : QStringLiteral("trail leg straightened %1").arg(absFig(PostureRow::TrailKnee, v));
    return t;
}

PostureMoved postureWhatMoved(const PostureGrid &x, const PostureFlags &warn, const PostureBandGrid &bands,
                              int pos, const PostureTrailStyle &trail)
{
    PostureMoved m;
    m.pos = pos;
    m.when = pos == kP4 ? QStringLiteral("At the top") : pos == kP7 ? QStringLiteral("At impact")
                        : QStringLiteral("At ") + posturePositionTag(pos);
    const double hv = x[size_t(ri(PostureRow::Hips))][size_t(pos)];
    const double head = x[0][size_t(pos)];
    const double bend = x[size_t(ri(PostureRow::Bend))][size_t(pos)];
    const double lead = x[size_t(ri(PostureRow::LeadKnee))][size_t(pos)];
    const double bendZ = cellZ(x, bands, PostureRow::Bend, pos), leadZ = cellZ(x, bands, PostureRow::LeadKnee, pos);

    // 1. hips in — never said under a ⚠ (the head decides there)
    m.hipsWarned = fin(hv) && cellWarned(warn, PostureRow::Hips, pos);
    m.hipsIn = fin(hv) && hipsIn(x, warn, bands, pos);
    // 2. the head
    const bool up = headUp(x, warn, bands, pos), down = headDown(x, warn, bands, pos);
    if (fin(head)) m.head = up ? QStringLiteral("up") : down ? QStringLiteral("down") : headLevel(head) ? QStringLiteral("level") : QStringLiteral("inside");
    // 3. the bend
    m.bendWarned = fin(bend) && cellWarned(warn, PostureRow::Bend, pos);
    if (fin(bendZ)) m.bend = bendZ < -1.0 ? QStringLiteral("lost") : bendZ > 1.0 ? QStringLiteral("gained") : QString();
    m.attribution = m.bend.isEmpty() ? QString()
                  : (m.hipsIn && up) ? QStringLiteral("hips in, then up")
                  : m.hipsIn ? QStringLiteral("hips in")
                  : up ? QStringLiteral("standing up")
                  : down ? QStringLiteral("down with the head")
                  : QStringLiteral("the trunk alone");

    QStringList clauses;
    bool levelUnderHips = false;
    if (m.hipsIn) {
        QString c = QStringLiteral("hips %1 in").arg(absFig(PostureRow::Hips, hv));
        if (m.head == QLatin1String("level")) { c += QStringLiteral(" under a level head"); levelUnderHips = true; }
        clauses << c;
    }
    if (!levelUnderHips && !m.head.isEmpty()) {
        if (m.head == QLatin1String("up")) clauses << QStringLiteral("head %1 up").arg(absFig(PostureRow::Head, head));
        else if (m.head == QLatin1String("down")) {
            QString c = QStringLiteral("down %1").arg(absFig(PostureRow::Head, head));
            if (fin(leadZ) && leadZ > 1.0 && !cellWarned(warn, PostureRow::LeadKnee, pos))
                c += QStringLiteral(", most of it the lead knee folding %1").arg(absFig(PostureRow::LeadKnee, lead));
            clauses << c;
        } else if (m.head == QLatin1String("level")) clauses << QStringLiteral("head level");
        else clauses << QStringLiteral("head %1, inside the band").arg(signedFig(PostureRow::Head, head));
    }
    if (pos == kP4 && trail.known) clauses << trail.words;
    QString line = clauses.join(QStringLiteral(", "));
    if (!m.bend.isEmpty()) {
        if (m.bendWarned) {
            line += (line.isEmpty() ? QString() : QStringLiteral("; ")) + QStringLiteral("trunk %1 %2 ⚠")
                        .arg(m.bend == QLatin1String("lost") ? QStringLiteral("more upright") : QStringLiteral("more bent"),
                             absFig(PostureRow::Bend, bend));
        } else if (levelUnderHips && m.bend == QLatin1String("lost")) {
            line += QStringLiteral("; that is where the %1 of bend went").arg(absFig(PostureRow::Bend, bend));
        } else {
            line += (line.isEmpty() ? QString() : QStringLiteral(", ")) + QStringLiteral("bend %1: %2")
                        .arg(signedFig(PostureRow::Bend, bend), m.attribution);
        }
    }
    if (m.hipsWarned) line += QStringLiteral(" (hips ⚠)");
    m.line = line;
    return m;
}

// ── views ───────────────────────────────────────────────────────────────────────────────────────

namespace {

std::array<PostureRowView, kPostureRows> rowViews(const PostureGrid &x, const std::array<bool, kPostureRows> &present,
                                                  const PostureFlags &gateWarn, const PostureWords &gateWords,
                                                  const PostureFlags &sourceWarn, const PostureBandGrid &bands,
                                                  const PostureFaultGrid &faults, const FiredCount &fired,
                                                  const PostureTrailStyle &trail, const std::array<double, kPosturePositions> &backLine)
{
    std::array<PostureRowView, kPostureRows> out;
    for (int r = 0; r < kPostureRows; ++r)
        out[size_t(r)] = rowView(PostureRow(r), x, present[size_t(r)], gateWarn, gateWords, sourceWarn, bands, faults, fired, trail,
                                 backLine);
    return out;
}

const PostureExit *largestExit(const std::vector<PostureExit> &exits)
{
    const PostureExit *best = nullptr;
    for (const PostureExit &e : exits)
        if (!best || std::fabs(e.z) > std::fabs(best->z)) best = &e;
    return best;
}

// Where a departure is read (decision 11): `want` when it lies in the departure's window and reads (a value,
// no ⚠), else the window's readable cell farthest from the band, else its latest readable cell; −1 none.
int departureCellPos(const DepartureDef &d, const PostureGrid &x, const PostureFlags &warn, const PostureBandGrid &bands, int want)
{
    const size_t R = size_t(ri(d.row));
    const auto reads = [&](int p) { return p >= 0 && p < kPosturePositions && fin(x[R][size_t(p)]) && !cellWarned(warn, d.row, p); };
    if (want >= d.lo && want <= d.hi && reads(want)) return want;
    int best = -1, latest = -1;
    double bestZ = -1.0;
    for (int p = d.lo; p <= d.hi; ++p) {
        if (!reads(p)) continue;
        latest = p;
        const double z = cellZ(x, bands, d.row, p);
        if (fin(z) && std::fabs(z) > bestZ) { bestZ = std::fabs(z); best = p; }
    }
    return best >= 0 ? best : latest;
}

// The departure a fired condition names on a shot: the one the shot left by that names it, else the first in
// the table that does (the presenter's Findings take the same one); null when it names none.
const DepartureDef *departureOfCondition(const QString &id, const std::vector<PostureDeparture> &left, const PostureEnv &env)
{
    for (const PostureDeparture &d : left)
        if (d.conditionIds.contains(id)) if (const DepartureDef *def = departureDef(d.id)) return def;
    for (const DepartureDef &d : departureDefs())
        if (departureConditions(QString::fromLatin1(d.id), env).contains(id)) return &d;
    return nullptr;
}

// "Your hips moved 5.3 cm toward the ball by impact; good players stay between …" at one cell.
QString cellHeadline(PostureRow row, int pos, const PostureGrid &x, const PostureBandGrid &bands)
{
    PostureExit e;
    e.row = row; e.pos = pos; e.value = x[size_t(ri(row))][size_t(pos)];
    return exitHeadline(e, bands[size_t(ri(row))][size_t(pos)]);
}

// Decision 11, this shot: the top-ranked FIRED condition that names a departure, read on the departure's row
// at the position the condition read; "" when none fired (or none reads).
QString firedHeadline(const PostureShotInput &shot, const std::vector<PostureDeparture> &left, const PostureBandGrid &bands,
                      const PostureEnv &env, const QStringList *order)
{
    struct Lead { const DepartureDef *def; int pos, rank; double z; };
    std::vector<Lead> leads;
    QSet<QString> seen;
    for (const QString &id : shot.fired) {
        if (seen.contains(id)) continue;
        seen.insert(id);
        const DepartureDef *def = departureOfCondition(id, left, env);
        if (!def) continue;
        const int want = shot.conditionPos.contains(id) ? shot.conditionPos.value(id) : postureConditionPos(nullptr, env, id);
        const int pos = departureCellPos(*def, shot.value, postureShotWarn(shot), bands, want);
        if (pos < 0) continue;
        const double z = cellZ(shot.value, bands, def->row, pos);
        leads.push_back({ def, pos, postureWorkOnRank(order, id), fin(z) ? std::fabs(z) : 0.0 });
    }
    if (leads.empty()) return QString();
    std::stable_sort(leads.begin(), leads.end(), [](const Lead &a, const Lead &b) {
        if (a.rank != b.rank) return a.rank < b.rank;
        return a.z > b.z;
    });
    return cellHeadline(leads.front().def->row, leads.front().pos, shot.value, bands);
}

QString headlineOf(const std::vector<PostureExit> &exits, const std::array<PostureRowView, kPostureRows> &rows,
                   const PosturePattern pattern, const QString &fired)
{
    // decision 11: a fired departure condition leads; else the largest exit, in the W-D6 words; else the
    // first W-D6 cell; else inside
    if (!fired.isEmpty()) return fired;
    const PostureExit *best = largestExit(exits);
    if (best)
        return QStringLiteral("Your %1 is %2.").arg(postureRowSpec(best->row).noun, postureWd6Words(best->pos));
    for (const PostureRowView &r : rows)
        for (const PostureCellView &c : r.cells)
            if (!c.note.isEmpty()) return QStringLiteral("Your %1 is %2.").arg(postureRowSpec(r.row).noun, c.note);
    return QStringLiteral("Your posture stays inside the %1 band from P2 to P7.").arg(posturePatternName(pattern));
}

// ── THE "AT ADDRESS" BLOCK (§7.4.2) ──

// The row's good-player band at the grading context (`ctx`), and its words.
void addressBand(PostureAbsoluteView &a, const PostureEnv &env, const QString &ctx)
{
    a.band = resolveBand(env, a.measureId, ctx.isEmpty() ? QStringLiteral("any") : ctx, false);
    if (a.band.has && fin(a.band.lo) && fin(a.band.hi)) {
        const int dec = a.unit == QStringLiteral("cm") ? 1 : 0;
        // "good players −5 to 1°": words between the ends, never a dash — a dash after a minus sign reads as a sum
        a.bandText = QStringLiteral("good players ") + withUnit(W::fig(a.band.lo, dec) + QStringLiteral(" to ") + W::fig(a.band.hi, dec), a.unit);
    } else {
        a.band.has = false;
        a.bandText = QStringLiteral("no good-player band yet");
    }
}

QString addressCountWords(int n, const QString &one, const QString &many)
{
    return QStringLiteral("%1 %2").arg(n).arg(n == 1 ? one : many);
}

// Every rig-sensitive reading the capture can produce, over `pool` (this session's shots), this shot's own value
// when `thisShot` is given (else the pool's median), the band at `ctx` and the history. `absent` receives, per camera,
// the readings this capture cannot produce (no such camera): said once in the trust fold, never as a row.
std::vector<PostureAbsoluteView> addressRows(const PostureEnv &env, const std::vector<const PostureShotInput *> &pool,
                                             const PostureShotInput *thisShot, const QString &ctx,
                                             const PostureAddressHistory *h, const QString &one, const QString &many,
                                             std::array<QStringList, 2> *absent)
{
    std::vector<PostureAbsoluteView> out;
    for (size_t i = 0; i < env.absolutes.size(); ++i) {
        const PostureAbsoluteSpec &sp = env.absolutes[i];
        std::vector<double> vals;
        for (const PostureShotInput *s : pool) vals.push_back(i < s->absolutes.size() ? s->absolutes[i] : kNaN);
        PostureAbsoluteView a;
        a.measureId = sp.measureId; a.label = sp.label; a.unit = sp.unit; a.metricKey = sp.metricKey;
        a.name = sp.name; a.camera = sp.camera;
        a.box = box::boxStats(vals);
        const double own = thisShot ? (i < thisShot->absolutes.size() ? thisShot->absolutes[i] : kNaN) : kNaN;
        // can this capture produce it? the session's streams say, when they were read; else any reading says
        const PostureAddressHistory::View *view = h ? &h->views[size_t(int(sp.camera))] : nullptr;
        const bool canRead = view && view->known ? view->present : (a.box.n > 0 || fin(own));
        if (!canRead) {
            if (absent) (*absent)[size_t(int(sp.camera))] << sp.name.left(1).toLower() + sp.name.mid(1);
            continue;
        }
        a.value = thisShot ? own : a.box.med;
        a.text = postureAddressFigure(a.value, a.unit);
        addressBand(a, env, ctx);
        a.boxText = a.box.n > 0 ? QStringLiteral("this session: median %1, n %2").arg(postureAddressFigure(a.box.med, a.unit)).arg(a.box.n)
                                : QStringLiteral("this session: no reading");
        if (h) {
            a.history = box::boxStats(h->values.value(sp.measureId));
            a.historySessions = h->sessions.value(sp.measureId);
        }
        if (a.history.n > 0)
            a.historyText = QStringLiteral("your earlier sessions: median %1, n %2 over %3")
                                .arg(postureAddressFigure(a.history.med, a.unit), addressCountWords(a.history.n, one, many),
                                     addressCountWords(a.historySessions, QStringLiteral("session"), QStringLiteral("sessions")));
        else
            a.historyWhy = QStringLiteral("No earlier session of yours from this set-up yet.");
        a.warned = true;   // no capture can be declared trusted yet (§7.4.3)
        a.warn = QStringLiteral("⚠");
        a.warnShort = postureRigWarnShort();
        a.trust = postureRigRule();
        out.push_back(a);
    }
    return out;
}

// The block's trust lines, said once: the rig rule, what "this set-up" is keyed on, where the bands come from, and the
// readings this capture cannot produce.
QStringList addressLines(const std::vector<PostureAbsoluteView> &rows, const std::array<QStringList, 2> &absent,
                         const PostureAddressHistory *h, const QString &one, const QString &many)
{
    QStringList out;
    if (!rows.empty()) {
        out << postureRigRule();
        for (const int c : { int(PostureCamera::Dtl), int(PostureCamera::FaceOn) }) {   // down the line first, as the rows
            const bool any = std::any_of(rows.begin(), rows.end(), [c](const PostureAbsoluteView &a) { return int(a.camera) == c; });
            if (!any) continue;
            const QString cam = postureCameraWords(PostureCamera(c));
            const PostureAddressHistory::View v = h ? h->views[size_t(c)] : PostureAddressHistory::View{};
            QString l = QStringLiteral("Set-up not recorded (where the camera stood): your earlier sessions at address are all your %1 "
                                       "sessions on the same camera").arg(cam);
            if (v.kept > 0)
                l += QStringLiteral(", %1 with %2.").arg(addressCountWords(v.kept, QStringLiteral("session"), QStringLiteral("sessions")),
                                                         addressCountWords(v.swings, one, many));
            else
                l += QStringLiteral("; there is none before this one yet.");
            if (v.otherCamera > 0)
                l += QStringLiteral(" %1 on another %2 camera %3 left out.")
                         .arg(W::cap1(addressCountWords(v.otherCamera, QStringLiteral("session"), QStringLiteral("sessions"))), cam,
                              v.otherCamera == 1 ? QStringLiteral("is") : QStringLiteral("are"));
            if (v.unrecorded > 0)
                l += QStringLiteral(" The camera of %1 is not recorded; %2 kept.")
                         .arg(v.unrecorded == 1 ? QStringLiteral("one of them") : QStringLiteral("%1 of them").arg(v.unrecorded),
                              v.unrecorded == 1 ? QStringLiteral("it is") : QStringLiteral("they are"));
            out << l;
        }
        // the earlier sessions' swings with a data warning: counted, as this session's box counts its own
        if (h && h->dataWarned > 0)
            out << QStringLiteral("Your earlier sessions include %1 with a data warning (a recording flagged as possibly broken); "
                                  "%2 counted, as this session's own are.")
                       .arg(addressCountWords(h->dataWarned, one, many), h->dataWarned == 1 ? QStringLiteral("it is") : QStringLiteral("they are"));
        // where the good-player bands at address come from: a published source names itself, a seed says so
        QStringList seeded, published, none;
        for (const PostureAbsoluteView &a : rows) {
            const QString nm = a.name.left(1).toLower() + a.name.mid(1);
            if (!a.band.has) none << nm;
            else if (a.band.source == QLatin1String("literature")) {
                QString cite = a.band.citation.trimmed();
                if (const qsizetype dot = cite.indexOf(QLatin1String(". ")); dot > 0) cite = cite.left(dot);
                published << (cite.isEmpty() ? nm : nm + QStringLiteral(" (") + cite + QLatin1Char(')'));
            } else seeded << nm;
        }
        if (!published.isEmpty())
            out << QStringLiteral("Good-player bands at address from published measurement: %1.").arg(W::list(published));
        if (!seeded.isEmpty())
            out << QStringLiteral("Good-player bands at address for %1 are our own authored seeds (heuristic), not yet measured on good players.")
                       .arg(W::list(seeded));
        if (!none.isEmpty())
            out << QStringLiteral("No good-player band yet for %1.").arg(W::list(none));
    }
    for (const int c : { int(PostureCamera::Dtl), int(PostureCamera::FaceOn) })
        if (!absent[size_t(c)].isEmpty())
            out << QStringLiteral("Not read at address on this capture: %1 — %2 the %3 camera.")
                       .arg(W::list(absent[size_t(c)]), absent[size_t(c)].size() == 1 ? QStringLiteral("it needs") : QStringLiteral("they need"),
                            postureCameraWords(PostureCamera(c)));
    return out;
}

} // namespace

PostureSessionView postureSession(const std::vector<PostureShotInput> &shots, const PostureEnv &env, int patternMode,
                                  const QHash<QString, QString> *tiers, const QStringList *workOnOrder, const QString &noun,
                                  const PostureAddressHistory *history)
{
    // decision 14: what the words count ("shots" | "swings"), and one of them
    const QString many = noun.trimmed().isEmpty() ? QStringLiteral("shots") : noun.trimmed();
    const QString one = many.endsWith(QLatin1Char('s')) ? many.chopped(1) : many;
    PostureSessionView v;
    v.patternMode = patternMode;
    std::vector<const PostureShotInput *> with;
    for (const PostureShotInput &s : shots)
        if (std::any_of(s.present.begin(), s.present.end(), [](bool b) { return b; })) with.push_back(&s);
    v.shots = int(with.size());
    if (with.empty()) return v;
    v.available = true;

    // medians: drawn (every value) and matched (no gate-warned value). Hip depth at P3–P5 (decision 15): the box
    // holds each shot's own source; the match reads the butt-line shots alone when any has one, else the joints,
    // and then the cell is under the ⚠ (sourceWarn)
    PostureGrid draw = postureEmptyGrid(), matchX = postureEmptyGrid();
    std::array<bool, kPostureRows> present{};
    std::array<std::array<box::BoxStats, kPosturePositions>, kPostureRows> boxes;
    PostureFlags sourceWarn{};
    std::array<int, 3> hipButt{}, hipJoints{};
    const size_t H = size_t(ri(PostureRow::Hips));
    for (int p = kP3; p <= kP5; ++p)
        for (const PostureShotInput *s : with) {
            if (s->hipSource[size_t(p)] == PostureHipSource::ButtLine) ++hipButt[size_t(p - kP3)];
            else if (s->hipSource[size_t(p)] == PostureHipSource::Joints) ++hipJoints[size_t(p - kP3)];
        }
    for (int r = 0; r < kPostureRows; ++r) {
        for (const PostureShotInput *s : with) present[size_t(r)] = present[size_t(r)] || s->present[size_t(r)];
        for (int p = 0; p < kPosturePositions; ++p) {
            const bool hipCell = size_t(r) == H && postureHipButtPos(p);
            const bool buttOnly = hipCell && hipButt[size_t(p - kP3)] > 0;
            if (hipCell && !buttOnly && hipJoints[size_t(p - kP3)] > 0) sourceWarn[H][size_t(p)] = true;
            std::vector<double> all, kept;
            for (const PostureShotInput *s : with) {
                const double x = s->value[size_t(r)][size_t(p)];
                all.push_back(x);
                const bool off = s->gateWarn[size_t(r)][size_t(p)]
                                 || (buttOnly && s->hipSource[size_t(p)] != PostureHipSource::ButtLine);
                kept.push_back(off ? kNaN : x);
            }
            boxes[size_t(r)][size_t(p)] = box::boxStats(all);
            draw[size_t(r)][size_t(p)] = boxes[size_t(r)][size_t(p)].med;
            matchX[size_t(r)][size_t(p)] = box::boxStats(kept).med;   // the ONE box statistic's median
        }
    }
    const PostureFlags noWarn{};
    v.match = postureMatch(matchX, present, sourceWarn, env);
    v.fromSession = patternMode >= 0 || v.shots >= kPostureSessionShots;
    v.pattern = patternMode >= 0 ? PosturePattern(patternMode) : v.match.nearest;
    v.patternName = posturePatternName(v.pattern);
    if (patternMode >= 0) {
        v.patternSource = QStringLiteral("chosen");
        v.patternSourceWords = QStringLiteral("your choice");
    } else if (v.fromSession) {
        v.patternSource = QStringLiteral("session");
        v.patternSourceWords = QStringLiteral("of your session");
    } else {
        v.patternSource = QStringLiteral("shot");
        v.patternSourceWords = QStringLiteral("fewer than five %1: each %2 is matched on its own").arg(many, one);
    }
    // the session's trail-knee style decides that row's band at P2–P4 (decision 10)
    v.trail = postureTrailStyle(matchX);
    const PostureBandGrid bands = postureBandsFor(env, v.pattern, v.trail);

    QStringList ctxs;
    for (const PostureShotInput *s : with) ctxs << s->contextId;
    v.gradingContextId = modal(ctxs);
    const PostureFaultGrid faults = postureFaults(env, v.gradingContextId);
    const FiredCount fired = [&](const QString &id) {
        int n = 0;
        for (const PostureShotInput *s : with) if (s->fired.contains(id)) ++n;
        return n;
    };

    // decision 8 on the medians: the condition is a session PATTERN — the ledger's tier, else (a condition
    // the tiers do not hold) the ledger's own gate over these shots' fired / clean counts, unweighted
    const PostureFiredTest sessionPattern = [&](const QString &id) {
        if (tiers) {
            const auto it = tiers->constFind(id);
            if (it != tiers->constEnd() && !it->isEmpty()) return *it == QLatin1String("pattern");
        }
        int assessable = 0, n = 0;
        for (const PostureShotInput *s : with) {
            if (s->fired.contains(id)) { ++n; ++assessable; }
            else if (s->clean.contains(id)) ++assessable;
        }
        return pa::conditionTier(assessable, n, pa::wilsonLowerBound(n, assessable)) == pa::Tier::Pattern;
    };

    PostureWords noWords;
    // decision 16: the back line under each P3–P5 trunk-bend cell, the shots' median
    std::array<double, kPosturePositions> backLine = postureEmptyCells();
    for (int p = kP3; p <= kP5; ++p) {
        std::vector<double> xs;
        for (const PostureShotInput *s : with) xs.push_back(s->backLine[size_t(p)]);
        backLine[size_t(p)] = box::boxStats(xs).med;
    }
    const auto views = rowViews(draw, present, noWarn, noWords, sourceWarn, bands, faults, fired, v.trail, backLine);
    for (int r = 0; r < kPostureRows; ++r) { v.rows[size_t(r)].view = views[size_t(r)]; v.rows[size_t(r)].boxes = boxes[size_t(r)]; }

    v.exits = postureExits(matchX, present, sourceWarn, bands, env, sessionPattern);
    v.departures = postureDepartures(v.exits, matchX, env, sessionPattern);
    v.top = postureWhatMoved(matchX, sourceWarn, bands, kP4, v.trail);
    v.impact = postureWhatMoved(matchX, sourceWarn, bands, kP7, v.trail);

    // the strip, and the departures shot by shot against the session's pattern
    std::map<QString, PostureDepartureCount> counts;
    QStringList order;
    for (const PostureShotInput *s : with) {
        const PostureFlags sWarn = postureShotWarn(*s);
        const PostureMatch m = postureMatch(s->value, s->present, sWarn, env);
        PosturePatternMark mk;
        mk.ordinal = s->ordinal; mk.ok = m.ok; mk.nearest = m.nearest; mk.between = m.fit == QLatin1String("between");
        mk.dLevel = m.dLevel; mk.dSquatPush = m.dSquatPush;
        mk.differs = !m.ok || mk.between || m.nearest != v.pattern;
        if (m.ok && !mk.between && m.nearest == v.pattern) ++v.onPattern;
        v.strip.push_back(mk);
        // counted only where named: the departure's condition fired on THAT shot
        const PostureFiredTest firedHere = [s](const QString &id) { return s->fired.contains(id); };
        const auto ex = postureExits(s->value, s->present, sWarn, bands, env, firedHere);
        for (const PostureDeparture &d : postureDepartures(ex, s->value, env, firedHere)) {
            PostureDepartureCount &c = counts[d.id];
            if (c.id.isEmpty()) { c.id = d.id; c.name = d.name; c.conditionIds = d.conditionIds; c.firstPos = d.firstPos; order << d.id; }
            ++c.shots;
            c.ordinals.push_back(s->ordinal);
            c.firstPos = std::min(c.firstPos, d.firstPos);
        }
    }
    for (const QString &id : order) v.departureCounts.push_back(counts[id]);
    std::stable_sort(v.departureCounts.begin(), v.departureCounts.end(),
                     [](const PostureDepartureCount &a, const PostureDepartureCount &b) { return a.shots > b.shots; });

    // repeatability (decision 6): the middle half over the band's σ
    for (int r = 0; r < kPostureRows; ++r)
        for (int p = kP2; p < kPosturePositions; ++p) {
            const box::BoxStats &b = boxes[size_t(r)][size_t(p)];
            const PostureBand &band = v.rows[size_t(r)].view.cells[size_t(p)].band;
            if (b.n < 3 || !band.has || !fin(b.iqr)) continue;
            const double sigma = 0.5 * (band.sigmaLo + band.sigmaHi);
            if (!(sigma > 0.0)) continue;
            PostureRepeat rp;
            rp.has = true; rp.row = PostureRow(r); rp.pos = p; rp.iqr = b.iqr; rp.iqrOverSigma = b.iqr / sigma;
            rp.words = QStringLiteral("%1 at %2, middle half %3").arg(postureRowSpec(rp.row).noun, posturePositionTag(p),
                                                                    absFig(rp.row, b.iqr));
            if (!v.tightest.has || rp.iqrOverSigma < v.tightest.iqrOverSigma) v.tightest = rp;
            if (!v.loosest.has || rp.iqrOverSigma > v.loosest.iqrOverSigma) v.loosest = rp;
        }
    if (v.tightest.has)
        v.repeatWords = QStringLiteral("the tightest: %1 · the loosest: %2").arg(v.tightest.words, v.loosest.words);

    // the head line (§5.5) and the pattern line
    if (!v.fromSession) {
        v.headline = QStringLiteral("%1 %2: too few to choose your pattern, so each %3 is matched on its own.")
                         .arg(W::cap1(W::word(v.shots)), v.shots == 1 ? one : many, one);
    } else {
        // decision 12: a session between the two says every shot's word; a clear pattern its count
        QString h;
        if (patternMode < 0 && v.match.fit == QLatin1String("between")) {
            int level = 0, squat = 0, between = 0;
            for (const PosturePatternMark &m : v.strip) {
                if (!m.ok) continue;
                if (m.between) ++between;
                else if (m.nearest == PosturePattern::Level) ++level;
                else ++squat;
            }
            QStringList parts;
            if (level) parts << QStringLiteral("%1 on %2").arg(posturePatternName(PosturePattern::Level)).arg(level);
            if (squat) parts << QStringLiteral("%1 on %2").arg(posturePatternName(PosturePattern::SquatPush)).arg(squat);
            if (between) parts << QStringLiteral("between on %1").arg(between);
            h = QStringLiteral("Between %1 and %2").arg(posturePatternName(PosturePattern::Level), posturePatternName(PosturePattern::SquatPush));
            h += parts.isEmpty() ? QStringLiteral(": no %1 matched on its own").arg(one)
                                 : QStringLiteral(": %1 of %2 %3").arg(parts.join(QStringLiteral(", "))).arg(v.shots).arg(many);
        } else {
            h = QStringLiteral("%1 on %2 of %3 %4").arg(v.patternName).arg(v.onPattern).arg(v.shots).arg(many);
        }
        // the tail leads with the top-ranked departure (decision 9: the work-on order), one of whose conditions
        // is a session pattern (decision 11: whether or not any shot left by it), the most shots between equals;
        // none: the departures the shots left by, as counted. The list below stays most shots first.
        const auto countOf = [&](const QString &id) -> const PostureDepartureCount * {
            for (const PostureDepartureCount &d : v.departureCounts) if (d.id == id) return &d;
            return nullptr;
        };
        const DepartureDef *patternLead = nullptr;
        int patternPos = -1, leadRank = std::numeric_limits<int>::max(), leadShots = -1;
        for (const DepartureDef &d : departureDefs()) {
            const QString id = QString::fromLatin1(d.id);
            const QStringList conds = departureConditions(id, env);
            if (!std::any_of(conds.begin(), conds.end(), sessionPattern)) continue;
            const PostureDepartureCount *c = countOf(id);
            const int pos = c ? c->firstPos : departureCellPos(d, matchX, sourceWarn, bands, -1);
            if (!c && pos < 0) continue;   // no shot left by it, and its medians read nowhere
            const int rank = postureDepartureRank(workOnOrder, conds, sessionPattern);
            const int n = c ? c->shots : 0;
            if (!patternLead || rank < leadRank || (rank == leadRank && n > leadShots)) {
                patternLead = &d; patternPos = pos; leadRank = rank; leadShots = n;
            }
        }
        const PostureDepartureCount *lead = patternLead ? countOf(QString::fromLatin1(patternLead->id)) : nullptr;
        if (!patternLead) {
            leadRank = std::numeric_limits<int>::max();
            for (const PostureDepartureCount &d : v.departureCounts) {
                const int rank = postureDepartureRank(workOnOrder, d.conditionIds, sessionPattern);
                if (!lead || rank < leadRank) { lead = &d; leadRank = rank; }
            }
        }
        const int most = v.departureCounts.empty() ? 0 : v.departureCounts.front().shots;
        if (lead && lead->shots == v.shots)
            h += QStringLiteral("; you leave it the same way every time: %1 from %2.")
                     .arg(lead->name.toLower(), posturePositionTag(lead->firstPos));
        else if (lead && lead->shots == most)
            h += QStringLiteral("; most often you leave it by %1 (%2 of %3 %4).")
                     .arg(lead->name.toLower()).arg(lead->shots).arg(v.shots).arg(many);
        else if (lead)
            h += QStringLiteral("; you leave it by %1 (%2 of %3 %4).")
                     .arg(lead->name.toLower()).arg(lead->shots).arg(v.shots).arg(many);
        else if (patternLead) {   // a session pattern no shot left the band by: its median reading, with the band
            const QString c = cellHeadline(patternLead->row, patternPos, matchX, bands);
            h += QStringLiteral("; ") + c.left(1).toLower() + c.mid(1);
        } else if (const PostureExit *far = largestExit(v.exits))   // outside, but no fault fired: W-D6
            h += QStringLiteral("; your %1 is %2.").arg(postureRowSpec(far->row).noun, postureWd6Words(far->pos));
        else
            h += QStringLiteral("; you stay inside its band from P2 to P7.");
        v.headline = h;
    }
    v.patternLine = QStringLiteral("%1 (%2) · trail knee %3")
                        .arg(v.patternName, patternMode >= 0 ? v.patternSourceWords : fitShort(v.match) + QStringLiteral(", ") + v.patternSourceWords,
                             v.trail.known ? v.trail.word : QStringLiteral("-"));

    {
        std::array<QStringList, 2> absent;
        v.absolutes = addressRows(env, with, nullptr, v.gradingContextId, history, one, many, &absent);
        v.addressLines = addressLines(v.absolutes, absent, history, one, many);
    }
    // decision 13: the rows no value reached, on how many shots, and the reason they share most often
    std::array<QString, kPostureRows> where, why;
    for (int r = 0; r < kPostureRows; ++r) {
        QStringList reasons;
        for (const PostureShotInput *s : with)
            if (!rowHasValue(s->value[size_t(r)])) reasons << absentWhyOf(*s, PostureRow(r));
        const int k = int(reasons.size());
        if (k == 0) continue;
        where[size_t(r)] = k == v.shots ? QStringLiteral("on any %1").arg(one) : QStringLiteral("on %1 of %2 %3").arg(k).arg(v.shots).arg(many);
        QStringList named;
        for (const QString &x : reasons) if (!x.isEmpty()) named << x;
        const QString top = modal(named);
        const bool shared = !top.isEmpty() && named.size() == k && named.count(top) == k;
        why[size_t(r)] = top.isEmpty() ? QString() : shared ? top : QStringLiteral("most often ") + top;
        v.rows[size_t(r)].view.absentWhy = why[size_t(r)];
    }
    bool hipWarn = false;
    const QString hipLine = hipSourceLine(hipButt, hipJoints, false, one, many, &hipWarn);
    v.trust = trustOf(views, v.addressLines, absentLines(where, why), hipLine, hipWarn);
    return v;
}

PostureShotView postureShot(const PostureShotInput &shot, const PostureEnv &env, int patternMode,
                            const PostureSessionView *session, const QStringList *workOnOrder,
                            const PostureAddressHistory *history)
{
    PostureShotView v;
    v.ordinal = shot.ordinal;
    v.patternMode = patternMode;
    v.available = std::any_of(shot.present.begin(), shot.present.end(), [](bool b) { return b; });
    if (!v.available) return v;

    const PostureFlags warn = postureShotWarn(shot);   // its gates', and hip depth on the joints (decision 15)
    v.match = postureMatch(shot.value, shot.present, warn, env);
    if (patternMode >= 0) {
        v.pattern = PosturePattern(patternMode);
        v.patternSource = QStringLiteral("chosen");
        v.patternSourceWords = QStringLiteral("your choice");
    } else if (session && session->available && session->fromSession) {
        v.pattern = session->pattern;
        v.sessionMatch = session->match;
        v.patternSource = QStringLiteral("session");
        v.patternSourceWords = QStringLiteral("of your session");
    } else {
        v.pattern = v.match.nearest;
        v.patternSource = QStringLiteral("shot");
        v.patternSourceWords = QStringLiteral("from this shot: fewer than five shots in the session");
    }
    v.patternName = posturePatternName(v.pattern);
    // this shot's own trail-knee style decides that row's band at P2–P4 (decision 10)
    v.trail = postureTrailStyle(shot.value);
    const PostureBandGrid bands = postureBandsFor(env, v.pattern, v.trail);
    const PostureFaultGrid faults = postureFaults(env, shot.contextId);
    const FiredCount fired = shot.graded ? FiredCount([&](const QString &id) { return shot.fired.contains(id) ? 1 : 0; })
                                         : FiredCount();
    PostureFlags sourceWarn{};
    for (int p = kP3; p <= kP5; ++p)
        sourceWarn[size_t(ri(PostureRow::Hips))][size_t(p)] = shot.hipSource[size_t(p)] == PostureHipSource::Joints;
    v.rows = rowViews(shot.value, shot.present, shot.gateWarn, shot.gateWords, sourceWarn, bands, faults, fired, v.trail,
                      shot.backLine);
    // decision 8: named only by what fired on this shot
    const PostureFiredTest firedHere = [&shot](const QString &id) { return shot.fired.contains(id); };
    v.exits = postureExits(shot.value, shot.present, warn, bands, env, firedHere);
    v.departures = postureDepartures(v.exits, shot.value, env, firedHere);
    v.top = postureWhatMoved(shot.value, warn, bands, kP4, v.trail);
    v.impact = postureWhatMoved(shot.value, warn, bands, kP7, v.trail);
    v.headline = headlineOf(v.exits, v.rows, v.pattern, firedHeadline(shot, v.departures, bands, env, workOnOrder));
    const PostureMatch &used = v.patternSource == QLatin1String("session") ? v.sessionMatch : v.match;
    v.patternLine = QStringLiteral("%1 (%2) · trail knee %3")
                        .arg(v.patternName, patternMode >= 0 ? v.patternSourceWords : fitShort(used) + QStringLiteral(", ") + v.patternSourceWords,
                             v.trail.known ? v.trail.word : QStringLiteral("-"));
    const QSet<QString> mine = postureConditionIds(env);
    for (const QString &id : shot.fired) if (mine.contains(id)) v.firedConditions << id;

    {
        // this shot's number on the session's boxes (the session's rows, re-banded at this shot's grading context),
        // else on its own; the history is the same either way
        std::array<QStringList, 2> absent;
        if (session) {
            v.absolutes = session->absolutes;
            for (PostureAbsoluteView &a : v.absolutes) {
                for (size_t i = 0; i < env.absolutes.size(); ++i)
                    if (env.absolutes[i].measureId == a.measureId) a.value = i < shot.absolutes.size() ? shot.absolutes[i] : kNaN;
                a.text = postureAddressFigure(a.value, a.unit);
                addressBand(a, env, shot.contextId);
            }
            // the readings the session's capture cannot produce: every rig-sensitive measure that is not one of its rows
            for (const PostureAbsoluteSpec &sp : env.absolutes)
                if (std::none_of(v.absolutes.begin(), v.absolutes.end(), [&sp](const PostureAbsoluteView &a) { return a.measureId == sp.measureId; }))
                    absent[size_t(int(sp.camera))] << sp.name.left(1).toLower() + sp.name.mid(1);
        } else {
            const std::vector<const PostureShotInput *> pool{ &shot };
            v.absolutes = addressRows(env, pool, &shot, shot.contextId, history, QStringLiteral("shot"), QStringLiteral("shots"), &absent);
        }
        v.addressLines = addressLines(v.absolutes, absent, history, QStringLiteral("shot"), QStringLiteral("shots"));
    }
    // decision 13: a row with no value anywhere, and why
    std::array<QString, kPostureRows> where, why;
    for (int r = 0; r < kPostureRows; ++r) {
        if (rowHasValue(shot.value[size_t(r)])) continue;
        v.rows[size_t(r)].absentWhy = absentWhyOf(shot, PostureRow(r));
        where[size_t(r)] = QStringLiteral("on this shot");
        why[size_t(r)] = v.rows[size_t(r)].absentWhy;
    }
    std::array<int, 3> hipButt{}, hipJoints{};
    for (int p = kP3; p <= kP5; ++p) {
        hipButt[size_t(p - kP3)] = shot.hipSource[size_t(p)] == PostureHipSource::ButtLine ? 1 : 0;
        hipJoints[size_t(p - kP3)] = shot.hipSource[size_t(p)] == PostureHipSource::Joints ? 1 : 0;
    }
    bool hipWarn = false;
    const QString hipLine = hipSourceLine(hipButt, hipJoints, true, QStringLiteral("shot"), QStringLiteral("shots"), &hipWarn);
    v.trust = trustOf(v.rows, v.addressLines, absentLines(where, why), hipLine, hipWarn);
    return v;
}

} // namespace pinpoint::charts
