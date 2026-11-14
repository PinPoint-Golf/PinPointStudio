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

#include "report_lm.h"

#include "report_variability.h"
#include "report_words.h"

#include "../../Coaching/diagnostic_ledger.h"          // medianOf — the report's one median
#include "../../Core/figure_format.h"
#include "../../LaunchMonitor/launch_monitor_reading.h" // lm::fieldDefs()
#include "../../LaunchMonitor/lm_session_reductions.h"  // lmDecimals

#include <QDir>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>

namespace pinpoint::report {

namespace {

namespace W = pinpoint::report::words;
using pinpoint::lm::lmSameClub;
using pinpoint::analysis::lmDecimals;

constexpr int kBallGroupCap = 4;   // the cover's ball block: four clubs at most, then "N more clubs in your numbers"

const QString kNA = QStringLiteral("-");
// A map key that is there and empty: QML reads it as null (REPORT_MAP.md: "null: no block").
const QVariant kNull = QVariant::fromValue(nullptr);

const pinpoint::lm::FieldDef *lmDef(const QString &key)
{
    for (const pinpoint::lm::FieldDef &d : pinpoint::lm::fieldDefs()) if (key == QLatin1String(d.key)) return &d;
    return nullptr;
}
QString unitOf(const QString &key) { const pinpoint::lm::FieldDef *d = lmDef(key); return d ? QString::fromUtf8(d->unit) : QString(); }
// The unit as a table prints it: a ratio has none.
QString shownUnit(const QString &key) { const QString u = unitOf(key); return u == QLatin1String("ratio") ? QString() : u; }
// Grouped thousands ("5,009") for the readings that run to four figures.
bool thousandsFor(const QString &unit) { return unit == QStringLiteral("rpm") || unit == QStringLiteral("°/s"); }

// A launch-monitor figure (figure_format.h): the true minus, "-" for none, thousands grouped where asked ("5,009").
QString lmFig(double v, int dp, bool thousands = false)
{
    return thousands ? pinpoint::figures::figThousands(v, dp) : pinpoint::figures::fig(v, dp);
}
// A reading at the golfer pages' precision.
QString golferFig(double v, const QString &key) { return lmFig(v, lmGolferDecimals(key), thousandsFor(unitOf(key))); }
// A reading at the board's own precision (the appendix).
QString lmCell(double v, const QString &unit) { return lmFig(v, lmDecimals(unit), thousandsFor(unit)); }

double median(const std::vector<double> &v) { return pinpoint::analysis::medianOf(v); }

QVariantList ballTiles(const LmFigures &f, const QString *club)
{
    QVariantList tiles;
    auto range = [](const std::vector<double> &v, const QString &key) {
        const auto [lo, hi] = std::minmax_element(v.begin(), v.end());
        return golferFig(*lo, key) + QChar(0x2013) + golferFig(*hi, key);
    };
    // The tile's words are the cover's own (an editorial label); its unit is the catalogue's, a degree set on the value.
    auto tile = [&](const char *k, const QString &label) {
        const QString key = QString::fromLatin1(k);
        const std::vector<double> v = f.series(key, club);
        if (v.empty()) return;
        const QString unit = shownUnit(key);
        const bool degrees = unit == QStringLiteral("°");
        QString value = golferFig(median(v), key);
        if (degrees) value += unit;
        tiles << QVariantMap{ { QStringLiteral("label"), label }, { QStringLiteral("value"), value },
                              { QStringLiteral("unit"), degrees ? QString() : unit }, { QStringLiteral("range"), range(v, key) } };
    };
    tile("lm.carryDistance", QStringLiteral("Carry"));
    tile("lm.ballSpeed", QStringLiteral("Ball speed"));
    tile("lm.clubheadSpeed", QStringLiteral("Club speed"));
    tile("lm.smashFactor", QStringLiteral("Smash"));
    tile("lm.launchAngle", QStringLiteral("Launch"));
    tile("lm.spinRate", QStringLiteral("Spin"));
    {
        const std::vector<double> v = f.series(QStringLiteral("lm.clubPath"), club);
        if (!v.empty()) {
            const double med = median(v);
            // GC Quad's path is + to the right of the target line: in-to-out for a right-hander.
            const bool square = std::fabs(med) < 0.5;
            const bool inToOut = f.leftHanded ? med < 0 : med > 0;
            const auto [lo, hi] = std::minmax_element(v.begin(), v.end());
            tiles << QVariantMap{ { QStringLiteral("label"), QStringLiteral("Club path") },
                                  { QStringLiteral("value"), lmFig(std::fabs(med), 0) + QStringLiteral("°") },
                                  { QStringLiteral("unit"), square ? QStringLiteral("square") : inToOut ? QStringLiteral("in-to-out") : QStringLiteral("out-to-in") },
                                  { QStringLiteral("range"), pinpoint::figures::figSigned(*lo, 1) + QStringLiteral(" to ") + pinpoint::figures::figSigned(*hi, 1) } };
        }
    }
    {
        const std::vector<double> v = f.series(QStringLiteral("lm.offline"), club);
        if (!v.empty()) {
            double widest = 0.0;
            for (double d : v) widest = std::max(widest, std::fabs(d));
            tiles << QVariantMap{ { QStringLiteral("label"), QStringLiteral("Offline") }, { QStringLiteral("value"), lmFig(widest, 0) },
                                  { QStringLiteral("unit"), shownUnit(QStringLiteral("lm.offline")) },
                                  { QStringLiteral("range"), QStringLiteral("widest, either side") } };
        }
    }
    return tiles;
}

} // namespace

double LmSwing::reading(const QString &key) const
{
    return shot ? shot->reading(key) : std::numeric_limits<double>::quiet_NaN();
}

std::vector<double> LmFigures::series(const QString &key, const QString *club) const
{
    std::vector<double> v;
    for (const LmSwing &s : swings) {
        if (!s.inFigures()) continue;
        if (club && !lmSameClub(s.club(), *club)) continue;
        const double d = s.reading(key);
        if (std::isfinite(d)) v.push_back(d);
    }
    return v;
}

LmFigures lmFiguresOf(const std::vector<pinpoint::lm::LmBoardShot> &board, const QStringList &swingDirs,
                      const QStringList &ledgerClubs, const QString &device, bool leftHanded)
{
    LmFigures f;
    f.device = device;
    f.leftHanded = leftHanded;
    for (int k = 0; k < swingDirs.size(); ++k) {
        LmSwing s;
        s.number = k + 1;
        s.ledgerClub = k < ledgerClubs.size() ? ledgerClubs[k] : QString();
        const QString dir = QDir::cleanPath(swingDirs[k]);
        for (const pinpoint::lm::LmBoardShot &b : board)
            if (!b.swingDir.isEmpty() && QDir::cleanPath(b.swingDir) == dir) { s.shot = b; break; }
        if (s.measured()) ++f.measured;
        if (s.inFigures()) ++f.counted;
        if (s.out() && s.measured()) f.leftOut << s.number;
        if (s.relabelled()) f.anyRelabelled = true;
        const QString c = s.club().trimmed();
        if (!c.isEmpty() && std::none_of(f.clubs.cbegin(), f.clubs.cend(), [&](const QString &x) { return lmSameClub(x, c); }))
            f.clubs << c;
        f.swings.push_back(std::move(s));
    }
    std::stable_sort(f.clubs.begin(), f.clubs.end(), pinpoint::lm::lmClubBagLess);
    return f;
}

int lmGolferDecimals(const QString &key)
{
    const QString unit = unitOf(key);
    if (unit == QLatin1String("ratio")) return lmDecimals(unit);
    if (unit == QStringLiteral("°") && pinpoint::lm::lmIsClubDelivery(key)) return lmDecimals(unit);
    return 0;
}

QVariant lmBallBlock(const LmFigures &f)
{
    if (!f.has()) return kNull;
    QVariantList groups;
    const bool several = f.clubs.size() > 1;
    if (!several) {
        groups << QVariantMap{ { QStringLiteral("club"), QString() }, { QStringLiteral("tiles"), ballTiles(f, nullptr) } };
    } else {
        for (int i = 0; i < f.clubs.size() && i < kBallGroupCap; ++i)
            groups << QVariantMap{ { QStringLiteral("club"), W::cap1(W::clubWord(f.clubs[i])) }, { QStringLiteral("tiles"), ballTiles(f, &f.clubs[i]) } };
    }
    return QVariantMap{ { QStringLiteral("device"), f.device },
                        { QStringLiteral("byline"), W::ballByline(f.device, f.measured, f.N()) },
                        { QStringLiteral("groups"), groups },
                        { QStringLiteral("more"), W::ballMore(std::max<int>(0, int(f.clubs.size()) - kBallGroupCap)) },
                        { QStringLiteral("note"), W::ballNote(f.counted, several, f.leftOut) } };
}

QVariant lmNumbersBlock(const LmFigures &f, bool typical, const QStringList &habitsCells, int workOnsPerSwing)
{
    if (!f.has()) return kNull;
    // The readings a golfer reads swing by swing, in this order (an editorial list, like the appendix's two tables);
    // the head and unit are the board's (lm::fieldDefs()).
    static const char *kCols[] = { "lm.carryDistance", "lm.totalDistance", "lm.ballSpeed", "lm.clubheadSpeed", "lm.smashFactor",
                                   "lm.launchAngle", "lm.spinRate", "lm.clubPath", "lm.faceAngle", "lm.faceToPath",
                                   "lm.attackAngle", "lm.offline" };
    const bool several = f.clubs.size() > 1;
    const bool habits = !habitsCells.isEmpty();
    QStringList cols;
    for (const char *k : kCols) {
        const QString key = QString::fromLatin1(k);
        if (!lmDef(key)) continue;
        if (std::any_of(f.swings.cbegin(), f.swings.cend(), [&](const LmSwing &s) { return std::isfinite(s.reading(key)); })) cols << key;
    }

    QStringList heads{ QStringLiteral("Swing") }, units{ QString() };
    if (several) { heads << QStringLiteral("Club"); units << QString(); }
    for (const QString &c : std::as_const(cols)) { heads << QString::fromUtf8(lmDef(c)->abbrev); units << shownUnit(c); }

    QVariantList rows;
    bool anyOut = false, anyRelabelled = false;
    for (int k = 0; k < f.N(); ++k) {
        const LmSwing &s = f.swings[size_t(k)];
        const bool out = s.out() && s.measured();
        const bool relab = s.relabelled() && s.measured();
        anyOut = anyOut || out;
        anyRelabelled = anyRelabelled || relab;
        QStringList cells{ QString::number(s.number) + (relab ? QStringLiteral("*") : QString()) + (out ? QStringLiteral(" out") : QString()) };
        if (several) cells << W::clubWord(s.club());
        for (const QString &c : std::as_const(cols)) cells << golferFig(s.reading(c), c);
        QVariantMap row{ { QStringLiteral("cells"), cells } };
        if (habits) row[QStringLiteral("habitsText")] = k < habitsCells.size() ? habitsCells[k] : QString();
        rows << row;
    }
    QVariantList typ;
    if (typical) {
        const QStringList groups = several ? f.clubs : QStringList{ QString() };
        for (const QString &club : groups) {
            QStringList cells{ QStringLiteral("Typical") };
            if (several) cells << W::clubWord(club);
            for (const QString &c : std::as_const(cols)) {
                const std::vector<double> v = f.series(c, several ? &club : nullptr);
                cells << (v.empty() ? kNA : golferFig(median(v), c));
            }
            typ << QVariantMap{ { QStringLiteral("cells"), cells } };
        }
    }
    return QVariantMap{ { QStringLiteral("cols"), heads }, { QStringLiteral("units"), units }, { QStringLiteral("rows"), rows },
                        { QStringLiteral("typical"), typ },
                        { QStringLiteral("habitsHead"), habits ? QStringLiteral("Habits") : QString() },
                        { QStringLiteral("note"), W::numbersNote(f.leftHanded, workOnsPerSwing, f.counted, habits, several,
                                                                 anyOut, anyRelabelled) } };
}

QVariantList lmGridCarry(const LmFigures &f)
{
    QVariantList carry;
    if (!f.has()) return carry;
    for (const LmSwing &s : f.swings) {
        const double c = s.inFigures() ? s.reading(QStringLiteral("lm.carryDistance")) : std::numeric_limits<double>::quiet_NaN();
        carry << (std::isfinite(c) ? QVariant(double(std::lround(c))) : kNull);
    }
    return carry;
}

QStringList lmGridCarryText(const LmFigures &f)
{
    QStringList text;
    if (!f.has()) return text;
    for (const LmSwing &s : f.swings) {
        const double c = s.reading(QStringLiteral("lm.carryDistance"));
        text << (s.out() && std::isfinite(c) ? QStringLiteral("out")
                 : s.inFigures() ? lmFig(c, 0) : kNA);
    }
    return text;
}

std::vector<double> lmReferenceCarry(const LmFigures &f)
{
    std::vector<double> carry;
    carry.reserve(f.swings.size());
    for (const LmSwing &s : f.swings)
        carry.push_back(s.inFigures() ? s.reading(QStringLiteral("lm.carryDistance")) : std::numeric_limits<double>::quiet_NaN());
    return carry;
}

QString lmReferenceFigure(const LmFigures &f, int k, const QString &key)
{
    if (k < 0 || k >= f.N() || !f.swings[size_t(k)].inFigures()) return kNA;
    return lmFig(f.swings[size_t(k)].reading(key), 0);
}

// ── The launch monitor appendix (§3, appendix A) ──
// From the launch monitor panel's own model (LmSessionModel over the session's shots), so the figures, the shot
// tables and the summary agree with the panel: the same effective club per shot, the same shots left out, the same
// per-club statistics (lmSessionStats: the n−1 SD, the extremes). The register draws the figures from `graphics`
// and sets the tables from the strings here.
QVariant lmAppendixBlock(const LmFigures &f, const QVariantMap &g)
{
    if (!f.has()) return kNull;
    const QVariantList shots = g.value(QStringLiteral("shots")).toList();
    const QVariantList clubs = g.value(QStringLiteral("clubs")).toList();
    if (shots.isEmpty() || clubs.isEmpty()) return kNull;

    QHash<int, int> swingNoOfShot;
    for (const LmSwing &s : f.swings) if (s.shot) swingNoOfShot.insert(s.shot->id, s.number);

    // The two tables' columns, in the order a coach reads them (an editorial list, like the panel's headline):
    // the ball as it launched, spun and flew; the club as it was delivered and where it struck.
    static const char *kBall[] = { "lm.ballSpeed", "lm.launchAngle", "lm.launchDirection", "lm.spinRate", "lm.backSpin",
                                   "lm.sideSpin", "lm.spinAxis", "lm.carryDistance", "lm.totalDistance", "lm.offline",
                                   "lm.peakHeight", "lm.descentAngle", "lm.distanceToPin" };
    static const char *kClub[] = { "lm.clubheadSpeed", "lm.smashFactor", "lm.attackAngle", "lm.clubPath", "lm.faceAngle",
                                   "lm.faceToPath", "lm.dynamicLoft", "lm.spinLoft", "lm.lieAngle", "lm.closureRate",
                                   "lm.lowPointAhead", "lm.strikeLocation", "lm.strikeHeight" };
    auto valueOf = [](const QVariantMap &shot, const QString &key) {
        const QVariant v = shot.value(QStringLiteral("values")).toMap().value(key);
        return v.isValid() ? v.toDouble() : std::numeric_limits<double>::quiet_NaN();
    };
    auto keysPresent = [&](const char *const *keys, int n) {
        QStringList out;
        for (int i = 0; i < n; ++i) {
            const QString k = QString::fromLatin1(keys[i]);
            if (!lmDef(k)) continue;
            for (const QVariant &s : shots) if (std::isfinite(valueOf(s.toMap(), k))) { out << k; break; }
        }
        return out;
    };
    const QStringList ballKeys = keysPresent(kBall, int(std::size(kBall)));
    const QStringList clubKeys = keysPresent(kClub, int(std::size(kClub)));
    auto labelOf = [](const QString &key) {
        const pinpoint::lm::FieldDef *d = lmDef(key);
        return d ? QString::fromUtf8(d->label).remove(QStringLiteral(" (measured)")) : key;
    };

    // the clubs, in the panel's (bag) order; then any club whose shots were all left out
    QStringList groupClubs;
    for (const QVariant &c : clubs) groupClubs << c.toMap().value(QStringLiteral("club")).toString();
    for (const QVariant &v : shots) {
        const QString c = v.toMap().value(QStringLiteral("club")).toString();
        if (std::none_of(groupClubs.cbegin(), groupClubs.cend(), [&](const QString &k) { return lmSameClub(k, c); })) groupClubs << c;
    }
    auto clubName = [](const QString &c) { return c.isEmpty() ? QStringLiteral("Club not recorded") : W::cap1(W::clubWord(c)); };
    auto swingNo = [&](const QVariantMap &s) {
        const int id = s.value(QStringLiteral("id")).toInt();
        return swingNoOfShot.value(id, s.value(QStringLiteral("number")).toInt());
    };

    bool anyOut = false, anyRelabelled = false;
    QList<int> noReading;
    for (const QVariant &v : shots) {
        const QVariantMap s = v.toMap();
        if (s.value(QStringLiteral("values")).toMap().isEmpty()) noReading << swingNo(s);
    }
    std::sort(noReading.begin(), noReading.end());
    auto table = [&](const QString &id, const QStringList &keys) -> QVariant {
        if (keys.isEmpty()) return kNull;
        QVariantList cols;
        for (const QString &k : keys) {
            const pinpoint::lm::FieldDef *d = lmDef(k);
            cols << QVariantMap{ { QStringLiteral("key"), k }, { QStringLiteral("head"), QString::fromUtf8(d->abbrev) },
                                 { QStringLiteral("unit"), shownUnit(k) } };
        }
        QVariantList groups;
        for (const QString &club : std::as_const(groupClubs)) {
            QVariantList rows;
            for (const QVariant &v : shots) {
                const QVariantMap s = v.toMap();
                if (!lmSameClub(s.value(QStringLiteral("club")).toString(), club)) continue;
                QStringList cells;
                bool any = false;
                for (const QString &k : keys) {
                    const double d = valueOf(s, k);
                    any = any || std::isfinite(d);
                    cells << lmCell(d, unitOf(k));
                }
                if (!any) continue;
                const bool out = s.value(QStringLiteral("excluded")).toBool();
                const bool relab = !lmSameClub(s.value(QStringLiteral("recordedClub")).toString(), club);
                anyOut = anyOut || out;
                anyRelabelled = anyRelabelled || relab;
                rows << QVariantMap{ { QStringLiteral("swing"), QString::number(swingNo(s)) + (relab ? QStringLiteral("*") : QString()) },
                                     { QStringLiteral("out"), out }, { QStringLiteral("cells"), cells } };
            }
            if (rows.isEmpty()) continue;
            groups << QVariantMap{ { QStringLiteral("club"), clubName(club) },
                                   { QStringLiteral("count"), rows.size() == 1 ? QStringLiteral("1 shot") : QStringLiteral("%1 shots").arg(rows.size()) },
                                   { QStringLiteral("rows"), rows } };
        }
        if (groups.isEmpty()) return kNull;
        return QVariantMap{ { QStringLiteral("id"), id }, { QStringLiteral("cols"), cols }, { QStringLiteral("groups"), groups } };
    };
    const QVariant ball = table(QStringLiteral("ball"), ballKeys);
    const QVariant club = table(QStringLiteral("club"), clubKeys);

    // the summary: per club, every reading the club's included shots carried
    QVariantList statGroups, figures;
    for (const QVariant &cv : clubs) {
        const QVariantMap c = cv.toMap();
        const QString cl = c.value(QStringLiteral("club")).toString();
        const QVariantMap stats = c.value(QStringLiteral("stats")).toMap();
        QVariantList rows;
        auto addRows = [&](const QString &sub, const QStringList &keys) {
            QVariantList part;
            for (const QString &k : keys) {
                const QVariantMap st = stats.value(k).toMap();
                const int n = st.value(QStringLiteral("n")).toInt();
                if (n <= 0) continue;
                const QString u = unitOf(k);
                const double lo = st.value(QStringLiteral("min")).toDouble(), hi = st.value(QStringLiteral("max")).toDouble();
                // The row's consistency badge (R4b, a technical-layer pair beside the SD): the club's grade as the panel's
                // model published it, club-delivery readings only (report_variability.h), else null.
                part << lmSummaryRow(labelOf(k), shownUnit(k),
                                     QStringList{ QString::number(n),
                                                  lmCell(st.value(QStringLiteral("mean")).toDouble(), u),
                                                  st.value(QStringLiteral("hasSpread")).toBool() ? lmCell(st.value(QStringLiteral("sd")).toDouble(), u) : kNA,
                                                  lmCell(lo, u), lmCell(hi, u),
                                                  n >= 2 ? lmCell(hi - lo, u) : kNA },
                                     st);
            }
            if (part.isEmpty()) return;
            rows << QVariantMap{ { QStringLiteral("sub"), sub } };
            rows << part;
        };
        addRows(QStringLiteral("Ball"), ballKeys);
        addRows(QStringLiteral("Club"), clubKeys);
        const int n = c.value(QStringLiteral("n")).toInt();
        if (!rows.isEmpty())
            statGroups << QVariantMap{ { QStringLiteral("club"), clubName(cl) },
                                       { QStringLiteral("count"), n == 1 ? QStringLiteral("1 shot") : QStringLiteral("%1 shots").arg(n) },
                                       { QStringLiteral("rows"), rows } };
        const QVariantMap carry = stats.value(QStringLiteral("lm.carryDistance")).toMap();
        const int cn = carry.value(QStringLiteral("n")).toInt();
        figures << QVariantMap{
            { QStringLiteral("key"), cl.toLower() },
            { QStringLiteral("name"), clubName(cl) },
            { QStringLiteral("headline"), W::lmClubHeadline(
                  cn > 0 ? lmFig(carry.value(QStringLiteral("mean")).toDouble(), 0) : QString(),
                  cn >= 2 ? lmFig(carry.value(QStringLiteral("max")).toDouble() - carry.value(QStringLiteral("min")).toDouble(), 0) : QString(), n) } };
    }
    QStringList clubWords;
    for (const QString &c : std::as_const(groupClubs)) if (!c.isEmpty()) clubWords << c;

    return QVariantMap{
        { QStringLiteral("kicker"), QStringLiteral("Appendix") },
        { QStringLiteral("title"), QStringLiteral("Launch monitor") },
        { QStringLiteral("standfirst"), W::lmAppendixStandfirst(f.device, f.measured, f.N(), clubWords) },
        { QStringLiteral("graphics"), g },
        { QStringLiteral("figures"), figures },
        { QStringLiteral("ball"), ball },
        { QStringLiteral("club"), club },
        { QStringLiteral("tablesNote"), W::lmTablesNote(f.leftHanded, anyOut, anyRelabelled, W::lmNoReading(noReading)) },
        { QStringLiteral("stats"), statGroups.isEmpty() ? kNull : QVariant(QVariantMap{ { QStringLiteral("groups"), statGroups } }) },
        { QStringLiteral("statsNote"), W::lmStatsNote(anyOut) } };
}

} // namespace pinpoint::report
