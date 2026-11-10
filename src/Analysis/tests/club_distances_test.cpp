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

// club_distances.h — the as-of baseline, the window, the floors, the mishit rule and the classifier,
// one block per rule, plus the schema-11 ledger round trip the reductions read.

#include "../../Coaching/club_distances.h"

#include <QJsonArray>
#include <QJsonDocument>

#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

using namespace pinpoint::analysis;

static int g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}
static bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

// A shot with a clean carry and the readings the reductions look up.
static ShotRecord shotWith(int shotId, const QString &club, double carry)
{
    ShotRecord s;
    s.shotId      = shotId;
    s.club        = club;
    s.carry       = carry;
    s.total       = carry + 10.0;
    s.offline     = 2.0;
    s.ballSpeed   = 110.0;
    s.lmClubSpeed = 82.0;
    s.smash       = 1.33;
    s.shaftAngleP4 = 90.0;
    return s;
}

static ConditionRow firedRow(const QString &id, int nSignals = 1, float confidence = 1.0f)
{
    ConditionRow r;
    r.conditionId      = id;
    r.state            = ShotState::Fired;
    r.firedSignalCount = nSignals;
    r.confidence       = confidence;
    return r;
}

// A session on `day` with the given carries.
static ClubDistanceSession sessionOn(const QString &day, const QString &club, const std::vector<double> &carries,
                                     const char *suffix = "01")
{
    ClubDistanceSession s;
    s.folder = day + QStringLiteral("_Mark_Range_") + QLatin1String(suffix);
    s.day    = sessionDayOfFolder(s.folder);
    int id = 1;
    for (double c : carries) s.shots.push_back(shotWith(id++, club, c));
    return s;
}

static const ClubDistanceItem *itemFor(const std::vector<ClubDistanceItem> &items, const QString &club)
{
    for (const ClubDistanceItem &i : items)
        if (i.club == club) return &i;
    return nullptr;
}

static const QString k7i = QStringLiteral("7 IRON");
static const QString k6i = QStringLiteral("6 IRON");

int main()
{
    std::printf("a mishit with no impact data leaves the pool:\n");
    {
        const auto wedge = [](int id, double carry, double launch, double start, const char *strikeRows) {
            ShotRecord s = shotWith(id, QStringLiteral("GAP WEDGE"), carry);
            s.smash = 1.09;
            s.launchAngle = launch;
            s.startDirection = start;
            s.attackAngle = -3.5;
            for (const char *row : { "chunk", "thin", "shank" }) {
                ConditionRow r;
                r.conditionId = QString::fromLatin1(row);
                r.state = QLatin1String(strikeRows) == QLatin1String("unread") ? ShotState::NotAssessable : ShotState::Clean;
                s.rows.push_back(r);
            }
            return s;
        };
        ClubDistanceSession s;
        s.folder = QStringLiteral("2026-10-05_x_y_01");
        s.day = QDate(2026, 10, 5);
        for (int i = 0; i < 6; ++i) s.shots.push_back(wedge(10 + i, 100.0, 30.0, -2.0, "unread"));
        s.shots.push_back(wedge(20, 11.0, 1.4, 12.6, "unread"));     // the shank
        s.shots.push_back(wedge(21, 60.0, 28.0, -1.0, "unread"));    // far short, nothing else to say
        s.shots.push_back(wedge(22, 60.0, 28.0, -1.0, "measured"));  // measured clean: a real distance

        const std::vector<ClubSessionPool> pools = poolSessionByClub(s);
        check(pools.size() == 1, "one club");
        check(pools[0].shots.size() == 7, "six full shots and the measured short one stay");
        check(pools[0].mishits.size() == 2, "the shank and the far-short shot go");
        check(pools[0].mishits[0].kind == QStringLiteral("shank"), "the shank is named from the flight");
        check(pools[0].mishits[1].kind == QStringLiteral("short"), "the other is only far short");

        ClubDistanceSession fixedSession = s;
        fixedSession.corrections[21] = { ShotCorrection{ QStringLiteral("strike"), QString(), QString() } };
        check(poolSessionByClub(fixedSession)[0].mishits.size() == 1, "the golfer's word keeps the far-short shot in");
    }

    std::printf("swing length, with only a ball speed to go on:\n");
    {
        ClubBaselineEntry usual;
        usual.shaftAngleP4Mean = 90.0;
        usual.speedMean = 80.0;
        usual.ballSpeedMean = 110.0;
        const double nan = std::numeric_limits<double>::quiet_NaN();
        check(classifySwingLength(nan, nan, &usual, 80.0) == QStringLiteral("partial"), "a pitch reads partial from ball speed alone");
        check(classifySwingLength(nan, nan, &usual, 105.0) == QStringLiteral("full"), "a full ball speed is full");
        check(classifySwingLength(90.0, nan, &usual, 80.0) == QStringLiteral("full"), "a full backswing outranks a slow ball: that's a mishit");
        check(classifySwingLength(nan, 80.0, &usual, 80.0) == QStringLiteral("full"), "full club speed outranks it too");
    }

    std::printf("ledger schema round trip:\n");
    {
        check(kDiagSchemaVersion == 14, "the ledger is schema 14");

        ShotRecord s = shotWith(1, k7i, 137.8);
        s.swingLength = QStringLiteral("full");
        s.handedness  = QStringLiteral("Left");
        s.launchAngle = 18.5;
        s.startDirection = -3.1;
        s.attackAngle = -4.2;
        s.descentAngle = 46.5;
        s.peakHeight = 84.0;
        s.spinRate = 6400.0;
        s.spinAxis = -2.5;
        ConditionRow r = firedRow(QStringLiteral("chunk"), 3);
        s.rows.push_back(r);
        const QJsonObject obj = toJson({ s });
        const std::vector<ShotRecord> back = fromJson(obj);
        check(back.size() == 1, "one shot back");
        const ShotRecord &b = back[0];
        check(near(b.carry, 137.8, 1e-12) && near(b.total, 147.8, 1e-12) && near(b.offline, 2.0, 1e-12)
                  && near(b.ballSpeed, 110.0, 1e-12) && near(b.lmClubSpeed, 82.0, 1e-12)
                  && near(b.smash, 1.33, 1e-12) && near(b.shaftAngleP4, 90.0, 1e-12),
              "the LM fields survive");
        check(near(b.launchAngle, 18.5, 1e-12) && near(b.startDirection, -3.1, 1e-12) && near(b.attackAngle, -4.2, 1e-12),
              "the flight readings a strike is read from survive");
        check(near(b.descentAngle, 46.5, 1e-12) && near(b.peakHeight, 84.0, 1e-12) && near(b.spinRate, 6400.0, 1e-12)
                  && near(b.spinAxis, -2.5, 1e-12),
              "schema 13: the landing angle and the apex (feet) the flight's height is read from, and the spin it is modelled from, survive");
        check(b.swingLength == QStringLiteral("full") && b.handedness == QStringLiteral("Left"),
              "swing length and handedness survive");
        check(b.rows[0].firedSignalCount == 3, "firedSignalCount survives");

        ShotRecord bare;
        bare.shotId = 2;
        const QJsonObject oldObj = toJson({ bare });
        const QJsonObject oldShot = oldObj.value(QStringLiteral("shots")).toArray().first().toObject();
        check(!oldShot.contains(QStringLiteral("carry")) && !oldShot.contains(QStringLiteral("swingLength")),
              "absent readings are not written");
        const ShotRecord b2 = fromJson(oldObj)[0];
        check(!std::isfinite(b2.carry) && !std::isfinite(b2.lmClubSpeed) && b2.swingLength.isEmpty(),
              "absent reads back as NaN and empty, never zero");
        const QJsonObject oldRow = toJson({ s }).value(QStringLiteral("shots")).toArray()
                                       .first().toObject().value(QStringLiteral("rows")).toArray()
                                       .first().toObject();
        check(oldRow.value(QStringLiteral("firedSignalCount")).toInt() == 3, "firedSignalCount is written when non-zero");
        ConditionRow quiet; quiet.conditionId = QStringLiteral("q");
        ShotRecord qs; qs.rows.push_back(quiet);
        check(!toJson({ qs }).value(QStringLiteral("shots")).toArray().first().toObject()
                   .value(QStringLiteral("rows")).toArray().first().toObject().contains(QStringLiteral("firedSignalCount")),
              "and absent when zero");
    }

    std::printf("session day from the folder name:\n");
    {
        check(sessionDayOfFolder(QStringLiteral("2026-11-08_Mark_Range_01")) == QDate(2026, 11, 8), "bare name");
        check(sessionDayOfFolder(QStringLiteral("/lib/Mark/2026-11-08_Mark_Range_01/")) == QDate(2026, 11, 8), "a path");
        check(!sessionDayOfFolder(QStringLiteral("no_date_here")).isValid(), "no date is invalid");
    }

    std::printf("as-of membership:\n");
    {
        const ClubDistanceSession d1 = sessionOn(QStringLiteral("2026-10-01"), k7i, { 150, 150, 150 });
        const ClubDistanceSession d2a = sessionOn(QStringLiteral("2026-10-02"), k7i, { 170, 170, 170 }, "01");
        const ClubDistanceSession d2b = sessionOn(QStringLiteral("2026-10-02"), k7i, { 190, 190, 190 }, "02");
        const ClubDistanceSession d3 = sessionOn(QStringLiteral("2026-10-03"), k7i, { 210, 210, 210 });
        const std::vector<ClubDistanceSession> all{ d3, d2b, d1, d2a };

        const auto asOfD2 = clubBaselineAsOf(all, QDate(2026, 10, 2));
        check(asOfD2.contains(k7i) && asOfD2[k7i].n == 3 && near(asOfD2[k7i].carryMean, 150.0, 1e-9),
              "a same-day session is not a predecessor");
        const auto asOfD3 = clubBaselineAsOf(all, QDate(2026, 10, 3));
        check(asOfD3[k7i].n == 9 && near(asOfD3[k7i].carryMean, (150.0 + 170.0 + 190.0) / 3.0, 1e-9),
              "every earlier day counts; the session's own day does not");
        check(!clubBaselineAsOf(all, QDate(2026, 10, 1)).contains(k7i), "nothing earlier: no entry");
        check(clubBaselineAsOf(all, QDate()).isEmpty(), "an undated session has no baseline");
    }

    std::printf("the window:\n");
    {
        std::vector<ClubDistanceSession> many;
        for (int i = 0; i < 12; ++i)
            many.push_back(sessionOn(QStringLiteral("2026-10-%1").arg(i + 1, 2, 10, QLatin1Char('0')), k7i, { 100.0 + i }));
        const auto last10 = clubBaselineAsOf(many, QDate(2026, 11, 1));
        check(last10[k7i].n == 10, "the last 10 sessions");

        const std::vector<double> carries(70, 150.0);
        const auto capped = clubBaselineAsOf({ sessionOn(QStringLiteral("2026-10-01"), k7i, carries) }, QDate(2026, 11, 1));
        check(capped[k7i].n == kClubDistanceWindowShots, "or the last 60 shots, whichever is fewer");

        std::vector<double> mixed(58, 150.0);
        mixed.insert(mixed.begin(), { 300.0, 300.0 });
        const auto cut = clubBaselineAsOf({ sessionOn(QStringLiteral("2026-10-01"), k7i, mixed),
                                            sessionOn(QStringLiteral("2026-10-02"), k7i, { 200.0, 200.0 }) },
                                          QDate(2026, 11, 1));
        check(cut[k7i].n == 60 && near(cut[k7i].carryMean, (58 * 150.0 + 2 * 200.0) / 60.0, 1e-9),
              "the shots kept are the newest");
    }

    std::printf("the statistics:\n");
    {
        const std::vector<ClubDistanceSession> s{ sessionOn(QStringLiteral("2026-10-01"), k7i, { 150, 160, 170 }) };
        const auto items = reduceClubDistances(s);
        const ClubDistanceItem *i = itemFor(items, k7i);
        check(i && near(i->carrySd, 10.0, 1e-9), "SD is n-1");
        check(i && near(i->shortBelow, 160.0 - 1.5 * 10.0, 1e-9), "short is the mean less 1.5 SD");
        check(i && i->carryText == QStringLiteral("160 ± 10 yd"), "the carry words");
        check(i && i->shots == 3 && i->sessions == 1 && i->fewShots, "few shots yet under ten");

        const auto two = reduceClubDistances({ sessionOn(QStringLiteral("2026-10-01"), k7i, { 150, 160 }) });
        const ClubDistanceItem *t = itemFor(two, k7i);
        check(t && !std::isfinite(t->carrySd) && near(t->carryMean, 155.0, 1e-9), "below the floor the mean stands and the SD is absent");
        check(t && t->carryText == QStringLiteral("155 ± — yd"), "…printed as a dash");

        ClubDistanceSession none = sessionOn(QStringLiteral("2026-10-01"), k7i, { 150 });
        none.shots.back().carry = std::numeric_limits<double>::quiet_NaN();
        check(itemFor(reduceClubDistances({ none }), k7i) == nullptr, "no carry reported, no row");

        ClubDistanceSession tenShots = sessionOn(QStringLiteral("2026-10-01"), k7i, std::vector<double>(10, 150.0));
        check(!itemFor(reduceClubDistances({ tenShots }), k7i)->fewShots, "ten shots are enough");
    }

    std::printf("which shots count:\n");
    {
        ClubDistanceSession s = sessionOn(QStringLiteral("2026-10-01"), k7i, { 150, 151, 152, 90, 88, 150 });
        // The two fat shots.
        s.shots[3].rows.push_back(firedRow(QStringLiteral("chunk")));
        s.shots[4].rows.push_back(firedRow(QStringLiteral("chunk")));
        // A shot the golfer corrected as a thin one, and a detected fat the golfer said was not.
        s.corrections[s.shots[0].shotId] = { ShotCorrection{ QStringLiteral("strike"), QString(), QStringLiteral("thin") } };
        s.shots[5].rows.push_back(firedRow(QStringLiteral("chunk")));
        s.corrections[s.shots[5].shotId] = { ShotCorrection{ QStringLiteral("strike"), QStringLiteral("chunk"), QString() } };

        const auto items = reduceClubDistances({ s });
        const ClubDistanceItem *i = itemFor(items, k7i);
        check(i && i->shots == 3, "mishits leave the pool");
        check(i && near(i->carryMean, (151.0 + 152.0 + 150.0) / 3.0, 1e-9), "and never enter the mean");
        check(i && i->mishits.chunk == 2 && i->mishits.thin == 1 && i->mishits.total() == 3, "they are counted, corrections applied");

        ClubDistanceSession partial = sessionOn(QStringLiteral("2026-10-01"), k7i, { 150, 90 });
        partial.shots.back().swingLength = QStringLiteral("partial");
        check(reduceClubDistances({ partial })[0].shots == 1, "a part swing is left out");

        ClubDistanceSession warn = sessionOn(QStringLiteral("2026-10-01"), k7i, { 150, 151 });
        warn.shots.back().dataWarning = true;
        check(reduceClubDistances({ warn })[0].shots == 2, "a data warning does not remove a monitor reading");

        ClubDistanceSession top = sessionOn(QStringLiteral("2026-10-01"), k7i, { 120 });
        top.shots.back().rows = { firedRow(QStringLiteral("thin"), 1), firedRow(QStringLiteral("top"), 3) };
        const ClubDistanceSession carryKeep = sessionOn(QStringLiteral("2026-10-02"), k7i, { 160 });
        const auto topped = reduceClubDistances({ top, carryKeep });
        const ClubDistanceItem *tp = itemFor(topped, k7i);
        check(tp && tp->mishits.top == 1 && tp->mishits.thin == 0 && tp->mishits.total() == 1,
              "a topped shot fires thin too and counts once, as a top");
        check(tp && tp->sessions == 1, "a session of only mishits counts their kinds and no session");

        ClubDistanceSession noClub = sessionOn(QStringLiteral("2026-10-01"), QString(), { 150 });
        check(reduceClubDistances({ noClub }).empty(), "no club, no row");
        ClubDistanceSession legacy = sessionOn(QStringLiteral("2026-10-01"), QStringLiteral("7-Iron"), { 150 });
        check(itemFor(reduceClubDistances({ legacy }), k7i) != nullptr, "legacy club spellings become the bag id");
    }

    std::printf("gapping:\n");
    {
        // 6 iron 160 ± 10 and 7 iron 150 ± 10 overlap; 7 iron and 8 iron 110 ± 10 do not.
        const auto items = reduceClubDistances({ sessionOn(QStringLiteral("2026-10-01"), k6i, { 150, 160, 170 }),
                                                 sessionOn(QStringLiteral("2026-10-02"), k7i, { 140, 150, 160 }),
                                                 sessionOn(QStringLiteral("2026-10-03"), QStringLiteral("8 IRON"), { 100, 110, 120 }) });
        const ClubDistanceItem *six = itemFor(items, k6i), *seven = itemFor(items, k7i), *eight = itemFor(items, QStringLiteral("8 IRON"));
        check(items.size() == 3 && items[0].club == k6i && items[1].club == k7i, "bag order");
        check(six && six->gapWith == QStringLiteral("overlaps 7 iron"), "the overlap is named on one row");
        check(seven && seven->gapWith == QStringLiteral("overlaps 6 iron"), "and on the other");
        check(eight && eight->gapWith.isEmpty(), "a clear gap is not flagged");
        check(seven && near(seven->gapPrev, 10.0, 1e-9) && near(seven->gapNext, 40.0, 1e-9)
                  && seven->gapPrevClub == QStringLiteral("6 iron") && seven->gapNextClub == QStringLiteral("8 iron"),
              "the gaps to both neighbours");
        check(eight && !std::isfinite(eight->gapNext), "no neighbour, no gap");
    }

    std::printf("trend:\n");
    {
        std::vector<ClubDistanceSession> rising;
        for (int i = 1; i <= 3; ++i) rising.push_back(sessionOn(QStringLiteral("2026-09-%1").arg(i, 2, 10, QLatin1Char('0')), k7i, { 148, 150, 152 }));
        for (int i = 4; i <= 6; ++i) rising.push_back(sessionOn(QStringLiteral("2026-09-%1").arg(i, 2, 10, QLatin1Char('0')), k7i, { 153, 155, 157 }));
        const auto risingItems = reduceClubDistances(rising);
        const ClubDistanceItem *r = itemFor(risingItems, k7i);
        check(r && r->trend == QStringLiteral("up") && r->trendWords == QStringLiteral("↑ 5 yd"), "a mean that moved reads as a move");

        std::vector<ClubDistanceSession> steady;
        for (int i = 1; i <= 6; ++i) steady.push_back(sessionOn(QStringLiteral("2026-09-%1").arg(i, 2, 10, QLatin1Char('0')), k7i, { 148, 150, 152 }));
        const auto steadyItems = reduceClubDistances(steady);
        const ClubDistanceItem *st = itemFor(steadyItems, k7i);
        check(st && st->trend == QStringLiteral("steady") && st->trendWords == QStringLiteral("→ steady"), "no move is steady");

        const auto thinItems = reduceClubDistances({ sessionOn(QStringLiteral("2026-09-01"), k7i, { 150, 151, 152 }) });
        const ClubDistanceItem *thin = itemFor(thinItems, k7i);
        check(thin && thin->trend.isEmpty(), "three sessions or fewer: nothing to compare");

        std::vector<ClubDistanceSession> tighter;
        for (int i = 1; i <= 3; ++i) tighter.push_back(sessionOn(QStringLiteral("2026-09-%1").arg(i, 2, 10, QLatin1Char('0')), k7i, { 140, 150, 160 }));
        for (int i = 4; i <= 6; ++i) tighter.push_back(sessionOn(QStringLiteral("2026-09-%1").arg(i, 2, 10, QLatin1Char('0')), k7i, { 148, 150, 152 }));
        const auto tighterItems = reduceClubDistances(tighter);
        const ClubDistanceItem *ti = itemFor(tighterItems, k7i);
        check(ti && ti->trend == QStringLiteral("tighter") && ti->trendWords == QStringLiteral("↓ spread tighter"), "a tighter spread");
    }

    std::printf("the classifier:\n");
    {
        ClubBaselineEntry usual;
        usual.shaftAngleP4Mean = 90.0;
        usual.speedMean = 80.0;
        check(classifySwingLength(90.0, 80.0, &usual) == QStringLiteral("full"), "the usual swing is full");
        check(classifySwingLength(71.0, 80.0, &usual) == QStringLiteral("partial"), "a shaft under 80 % of the usual is partial");
        check(classifySwingLength(90.0, 66.0, &usual) == QStringLiteral("partial"), "a speed under 85 % of the usual is partial");
        check(classifySwingLength(90.0, 68.0, &usual) == QStringLiteral("full"), "on the line is full");
        check(classifySwingLength(71.0, 66.0, nullptr) == QStringLiteral("full"), "no history is full");
        const double nan = std::numeric_limits<double>::quiet_NaN();
        check(classifySwingLength(nan, 66.0, &usual) == QStringLiteral("partial"), "a missing angle leaves the speed test");
        check(classifySwingLength(nan, nan, &usual) == QStringLiteral("full"), "no numbers at all is full");
        ClubBaselineEntry none;
        check(classifySwingLength(30.0, 30.0, &none) == QStringLiteral("full"), "an entry with no usual numbers is full");
    }

    std::printf("the formatter:\n");
    {
        check(yardsMeanSdText(161.4, 6.6) == QStringLiteral("161 ± 7 yd"), "161 ± 7 yd");
        check(yardsMeanSdText(161.4, std::numeric_limits<double>::quiet_NaN()) == QStringLiteral("161 ± — yd"), "a blank SD");
        check(yardsMeanSdText(std::numeric_limits<double>::quiet_NaN(), 6.6) == QStringLiteral("—"), "no mean");
        check(yardsShortText(24.1) == QStringLiteral("24 yd short"), "24 yd short");
    }

    std::printf("the sidecar:\n");
    {
        ClubBaselineSnapshot snap;
        snap.frozenAt = QStringLiteral("2026-11-08T12:00:00");
        ClubBaselineEntry e;
        e.club = k7i;
        e.carryMean = 161.9;
        e.carrySd = std::numeric_limits<double>::quiet_NaN();
        e.ballSpeedMean = 110.0;
        e.speedMean = 82.0;
        e.smashMean = 1.33;
        e.shaftAngleP4Mean = 92.0;
        e.n = 24;
        snap.clubs.insert(k7i, e);

        bool ok = false;
        const ClubBaselineSnapshot back = clubBaselineFromJson(
            QJsonDocument::fromJson(QJsonDocument(clubBaselineToJson(snap)).toJson()).object(), &ok);
        check(ok && back.schema == kClubBaselineSchemaVersion && back.frozenAt == snap.frozenAt, "the header survives");
        const ClubBaselineEntry b = back.clubs.value(k7i);
        check(near(b.carryMean, 161.9, 1e-9) && b.n == 24 && near(b.smashMean, 1.33, 1e-12) && near(b.shaftAngleP4Mean, 92.0, 1e-9),
              "the numbers survive");
        check(!std::isfinite(b.carrySd) && !std::isfinite(b.ballSpeedSd), "absent stays absent");
        bool badOk = true;
        clubBaselineFromJson(QJsonObject{ { QStringLiteral("schema"), 99 } }, &badOk);
        check(!badOk, "another schema is refused");
    }

    std::printf(g_fail ? "FAILED (%d)\n" : "OK\n", g_fail);
    return g_fail ? 1 : 0;
}
