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

// WorkOnsController — the catch-up and the home list (docs/design/work_ons_design.md).
// work_ons_test proves the two reductions; this proves the join: that sessions which were never
// "ended" get a record by being looked at, that a current record is left alone, that a session
// which changed is re-derived and only that one, that a live session holds derivation, and that
// the list is the shipped pack's wording over the aggregate.
//
// The sessions are LEDGERS ON DISK with empty swing folders beside them: the model loads the
// rows, finds nothing to back-fill, and no swing document is ever read — the catch-up path for
// a library that already has its ledgers, which is the one the app meets first.

#include "Gui/diagnostics/work_ons_controller.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <cstdio>
#include <vector>

using namespace pinpoint::analysis;

static int g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}

struct Plan { const char *id; int fired; };   // fired on every shot (1) or none (0)

static void stage(const QString &athleteDir, const QString &session, qint64 t0, const std::vector<Plan> &plans,
                  int shotCount = 10)
{
    const QString dir = QDir(athleteDir).filePath(session);
    std::vector<ShotRecord> shots;
    for (int i = 0; i < shotCount; ++i) {
        QDir().mkpath(dir + QStringLiteral("/swing_%1").arg(i + 1, 4, 10, QLatin1Char('0')));
        ShotRecord s;
        s.shotId = i + 1;
        s.club = QStringLiteral("7I");
        s.timestampMs = t0 + i * 45'000LL;
        for (const Plan &p : plans) {
            ConditionRow r;
            r.conditionId = QString::fromLatin1(p.id);
            r.drivingMeasureId = r.conditionId + QStringLiteral(".measure");
            r.corridorLo = -2.0; r.corridorHi = 2.0;
            r.corridorShape = CorridorShape::TwoSided;
            r.state = p.fired ? ShotState::Fired : ShotState::Clean;
            r.z = p.fired ? 3.0 : 0.5;
            r.value = r.z * 2.0;
            r.direction = p.fired ? 1 : 0;
            s.rows.push_back(r);
        }
        shots.push_back(s);
    }
    QJsonObject root;
    root[QStringLiteral("schemaVersion")] = 1;
    root[QStringLiteral("ledger")] = toJson(shots);
    QFile f(dir + QStringLiteral("/diagnostics.json"));
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

static QJsonObject record(const QString &athleteDir, const QString &session)
{
    QFile f(WorkOnsController::recordPath(QDir(athleteDir).filePath(session)));
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

static double writtenAt(const QString &athleteDir, const QString &session)
{
    return record(athleteDir, session).value(QStringLiteral("derivedFrom")).toObject()
        .value(QStringLiteral("writtenAtMs")).toDouble();
}

static QVariantMap item(const WorkOnsController &c, const char *id)
{
    for (const QVariant &v : c.items())
        if (v.toMap().value(QStringLiteral("id")).toString() == QLatin1String(id)) return v.toMap();
    return {};
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    std::printf("work_ons_controller_test\n");

    QTemporaryDir tmp;
    const QString golfer = tmp.filePath(QStringLiteral("Golfer"));
    const QString other  = tmp.filePath(QStringLiteral("Other"));
    QDir().mkpath(golfer);
    QDir().mkpath(other);
    const qint64 day = 86'400'000LL, t0 = 1'780'000'000'000LL;

    stage(golfer, QStringLiteral("s1"), t0,
          { { "reverse_spine_p4", 1 }, { "sway", 1 }, { "pull", 1 } });
    stage(golfer, QStringLiteral("s2"), t0 + day,
          { { "reverse_spine_p4", 1 }, { "sway", 0 }, { "ball_back", 1 } });
    QDir().mkpath(golfer + QStringLiteral("/empty_session"));     // nothing hit: not a session to record

    WorkOnsController c;
    c.setSynchronous(true);
    c.setAthleteDir(golfer);

    // ── Catch-up: neither session was ever "ended" ──────────────────────────────
    check(c.sessionsFound() == 2 && c.sessionCount() == 2 && !c.updating(), "both sessions were read; the empty folder is not one");
    check(!record(golfer, QStringLiteral("s1")).isEmpty() && !record(golfer, QStringLiteral("s2")).isEmpty(),
          "each session now carries a work_ons.json");
    check(record(golfer, QStringLiteral("s1")).value(QStringLiteral("workOns")).toArray().size() == 2,
          "s1 lists its two movement faults and not the ball-flight outcome");
    check(!QFile::exists(golfer + QStringLiteral("/fault_profile.json")),
          "deriving a record does not close the session or write the fault profile");

    // ── The list ────────────────────────────────────────────────────────────────
    check(c.items().size() == 3 && c.clearedCount() == 0, "three work-ons, the outcome is not one of them");
    const QVariantMap spine = item(c, "reverse_spine_p4"), sway = item(c, "sway"), ball = item(c, "ball_back");
    check(c.items().first().toMap().value(QStringLiteral("id")).toString() == QStringLiteral("reverse_spine_p4"),
          "the fault in both sessions leads");
    check(spine.value(QStringLiteral("name")).toString() == QStringLiteral("Reverse spine angle at P4")
          && spine.value(QStringLiteral("countText")).toString() == QStringLiteral("20 of 20 swings")
          && spine.value(QStringLiteral("status")).toString() == QStringLiteral("active"),
          "the pack's name, a count of swings, active");
    check(spine.value(QStringLiteral("ticks")).toList().size() == 2
          && spine.value(QStringLiteral("sessionDir")).toString().endsWith(QStringLiteral("/s2"))
          && spine.value(QStringLiteral("latestText")).toString().contains(QStringLiteral("10 of 10 measurable shots"))
          && !spine.value(QStringLiteral("consequence")).toString().isEmpty(),
          "one tick per session, the newest pattern session to review, the latest count, the consequence");
    check(sway.value(QStringLiteral("status")).toString() == QStringLiteral("easing")
          && sway.value(QStringLiteral("statusText")).toString().startsWith(QStringLiteral("Easing")),
          "sway was quiet in the later session: easing");
    check(ball.value(QStringLiteral("countText")).toString() == QStringLiteral("10 of 20 swings")
          && ball.value(QStringLiteral("coverageText")).toString() == QStringLiteral("not measurable on 10 of them")
          && spine.value(QStringLiteral("coverageText")).toString().isEmpty(),
          "every row counts against the same swings; the ones it could not be judged on are said apart");
    check(!sway.value(QStringLiteral("drillLabel")).toString().isEmpty(), "sway carries the pack's drill");

    // ── A current record is left alone ──────────────────────────────────────────
    const double w1 = writtenAt(golfer, QStringLiteral("s1")), w2 = writtenAt(golfer, QStringLiteral("s2"));
    c.refresh();
    check(writtenAt(golfer, QStringLiteral("s1")) == w1 && writtenAt(golfer, QStringLiteral("s2")) == w2
          && c.items().size() == 3, "a second pass derives nothing and lists the same");

    // ── A new session: only it is derived ───────────────────────────────────────
    stage(golfer, QStringLiteral("s3"), t0 + 2 * day, { { "reverse_spine_p4", 1 }, { "sway", 0 } });
    c.refresh();
    check(c.sessionCount() == 3 && writtenAt(golfer, QStringLiteral("s1")) == w1
          && !record(golfer, QStringLiteral("s3")).isEmpty(), "the new session is recorded and the old ones are not rewritten");
    check(item(c, "sway").isEmpty() && c.clearedCount() == 1, "sway quiet twice running is cleared off the list");
    check(item(c, "ball_back").value(QStringLiteral("statusText")).toString().startsWith(QStringLiteral("Not measured since"))
          && item(c, "ball_back").value(QStringLiteral("unconfirmed")).toBool()
          && item(c, "ball_back").value(QStringLiteral("lastSeenText")).toString().startsWith(QStringLiteral("last seen "))
          && !item(c, "reverse_spine_p4").value(QStringLiteral("unconfirmed")).toBool(),
          "a fault the newest session could not measure says so, is flagged unconfirmed, and stays");

    // ── A session that changed, and a record from another rule ──────────────────
    QDir().mkpath(golfer + QStringLiteral("/s2/swing_0011"));      // a swing arrived
    {
        QJsonObject r = record(golfer, QStringLiteral("s1"));
        r[QStringLiteral("ruleVersion")] = kWorkOnRuleVersion + 1;
        QFile f(WorkOnsController::recordPath(golfer + QStringLiteral("/s1")));
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            f.write(QJsonDocument(r).toJson());
    }
    const double w3 = writtenAt(golfer, QStringLiteral("s3"));
    c.refresh();
    check(record(golfer, QStringLiteral("s1")).value(QStringLiteral("ruleVersion")).toInt() == kWorkOnRuleVersion,
          "a record written under another rule is re-derived");
    check(writtenAt(golfer, QStringLiteral("s2")) != w2 && writtenAt(golfer, QStringLiteral("s3")) == w3,
          "the session that gained a swing is re-derived; the untouched one is not");
    c.refresh();
    check(c.sessionCount() == 3 && !c.updating(), "…and is then current, though the new swing had no document to read");

    // ── A live session holds derivation, and ending it is the catch-up ──────────
    c.setPaused(true);
    stage(golfer, QStringLiteral("s4"), t0 + 3 * day, { { "reverse_spine_p4", 0 } });
    c.refresh();
    check(c.sessionsFound() == 4 && c.sessionCount() == 3 && record(golfer, QStringLiteral("s4")).isEmpty(),
          "paused: the new session is found and not derived");
    check(c.items().size() == 2, "…and the list still stands on the records there are");
    c.setPaused(false);
    check(c.sessionCount() == 4 && item(c, "reverse_spine_p4").value(QStringLiteral("status")).toString() == QStringLiteral("easing"),
          "the pause lifting derives it — which is all 'end session' is");

    // ── Another golfer ──────────────────────────────────────────────────────────
    c.setAthleteDir(other);
    check(c.items().isEmpty() && c.sessionCount() == 0 && c.sessionsFound() == 0, "another athlete's list starts empty");
    c.setAthleteDir(golfer);
    check(c.items().size() == 2 && c.sessionCount() == 4, "…and coming back reads the records without deriving");

    std::printf(g_fail ? "FAILED (%d)\n" : "OK\n", g_fail);
    return g_fail ? 1 : 0;
}
