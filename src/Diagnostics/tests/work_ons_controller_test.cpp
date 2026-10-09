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
// And the swing summary the same catch-up ends in (swing_themes.h): reduced off the ledgers once
// every record is current, kept in <athlete>/swing_themes.json under a fingerprint so an unchanged
// library is read rather than recomputed, recomputed when a session arrives or the file is from
// another rule, held while a session is live, and "not yet" on too little data. swing_themes_test
// proves the reduction; this proves the plumbing, on ledgers whose rows carry real core.json
// measure ids with a planted co-movement.
//
// The sessions are LEDGERS ON DISK with empty swing folders beside them: the model loads the
// rows, finds nothing to back-fill, and no swing document is ever read — the catch-up path for
// a library that already has its ledgers, which is the one the app meets first.

#include "Gui/diagnostics/work_ons_controller.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <cmath>
#include <cstdio>
#include <random>
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

// ── Swing-summary fixtures ───────────────────────────────────────────────────────────────
//
// A session whose rows read REAL measures. One latent factor per swing drives the hips sliding
// (three pelvis-sway readings, one family) and the lead knee drifting at the top; two more
// measures are noise. Each session sits at its own level, which the reduction centres away.
// `sway` fires on every swing, so it is what "what we see most" leads with.
static void stageThemed(const QString &athleteDir, const QString &session, qint64 t0, uint64_t seed,
                        int shotCount = 30)
{
    const QString dir = QDir(athleteDir).filePath(session);
    std::mt19937_64 rng(seed);
    std::normal_distribution<double> unit(0.0, 1.0);
    const double offset = 2.0 * unit(rng);      // the session's own level

    const auto row = [](const char *cond, const char *measure, double value, bool fired) {
        ConditionRow r;
        r.conditionId      = QString::fromLatin1(cond);
        r.drivingMeasureId = QString::fromLatin1(measure);
        r.value            = value;
        r.corridorLo = -100.0; r.corridorHi = 100.0;
        r.corridorShape = CorridorShape::TwoSided;
        r.state = fired ? ShotState::Fired : ShotState::Clean;
        r.z = fired ? 3.0 : 0.5;
        r.direction = fired ? 1 : 0;
        return r;
    };
    const auto reading = [](const char *measure, double value) {
        MeasureRow m;
        m.measureId = QString::fromLatin1(measure);
        m.value = value;
        m.corridorLo = -100.0; m.corridorHi = 100.0;
        m.corridorShape = CorridorShape::TwoSided;
        return m;
    };

    std::vector<ShotRecord> shots;
    for (int i = 0; i < shotCount; ++i) {
        QDir().mkpath(dir + QStringLiteral("/swing_%1").arg(i + 1, 4, 10, QLatin1Char('0')));
        ShotRecord s;
        s.shotId = i + 1;
        s.club = QStringLiteral("7I");
        s.timestampMs = t0 + i * 45'000LL;

        const double f = unit(rng);             // the shared factor
        s.rows.push_back(row("sway", "m_pelvisSwayBack", 20.0 + offset + 3.0 * f + 0.8 * unit(rng), true));
        s.rows.push_back(row("lead_knee_drifts_in_at_top", "m_leadKneeDriftTop",
                             5.0 - offset + 3.0 * f + 0.8 * unit(rng), false));
        // A row read by two signals: its readings, not its driving fields, are the evidence.
        ConditionRow slide = row("slide", "m_pelvisSwayDown", 0.0, false);
        slide.readings = { reading("m_pelvisSwayDown",   offset + 3.0 * f + 0.8 * unit(rng)),
                           reading("m_pelvisSwayImpact", offset + 3.0 * f + 0.8 * unit(rng)) };
        slide.value = slide.readings.front().value;
        s.rows.push_back(slide);
        s.rows.push_back(row("transition_rush", "m_tempoRatio", 3.0 + 0.3 * unit(rng), false));
        s.rows.push_back(row("excessive_lag", "m_lagAngleDown", 30.0 + offset + 4.0 * unit(rng), false));
        shots.push_back(s);
    }
    QJsonObject root;
    root[QStringLiteral("schemaVersion")] = 1;
    root[QStringLiteral("ledger")] = toJson(shots);
    QFile f(dir + QStringLiteral("/diagnostics.json"));
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

static QJsonObject themesFile(const QString &athleteDir)
{
    QFile f(WorkOnsController::themesPath(athleteDir));
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

static double themesWrittenAt(const QString &athleteDir)
{
    return themesFile(athleteDir).value(QStringLiteral("derivedFrom")).toObject()
        .value(QStringLiteral("writtenAtMs")).toDouble();
}

// Every string the summary would draw.
static QStringList summaryText(const WorkOnsController &c)
{
    QStringList out;
    if (!c.summarySubtitle().isEmpty()) out << c.summarySubtitle();
    for (const QVariant &v : c.doWellItems()) {
        const QVariantMap m = v.toMap();
        out << m.value(QStringLiteral("text")).toString() << m.value(QStringLiteral("caption")).toString();
    }
    const QVariantMap focus = c.focusItem();
    for (const char *k : { "title", "why", "practiseLabel", "practise", "reason" })
        out << focus.value(QLatin1String(k)).toString();
    for (const QVariant &a : focus.value(QStringLiteral("aimFor")).toList()) out << a.toString();
    QVariantList faults = focus.value(QStringLiteral("rightNow")).toList();
    faults += c.nextItems();
    for (const QVariant &v : faults) {
        const QVariantMap m = v.toMap();
        out << m.value(QStringLiteral("text")).toString() << m.value(QStringLiteral("frequency")).toString();
    }
    for (const QVariant &v : c.togetherItems()) {
        const QVariantMap m = v.toMap();
        out << m.value(QStringLiteral("first")).toString() << m.value(QStringLiteral("second")).toString()
            << m.value(QStringLiteral("startWords")).toString();
    }
    if (!c.togetherNote().isEmpty()) out << c.togetherNote();
    return out;
}

static bool saysCause(const QStringList &lines)
{
    static const char *const words[] = { "because", "cause", "due to", "leads to" };
    for (const QString &l : lines)
        for (const char *w : words)
            if (l.contains(QLatin1String(w), Qt::CaseInsensitive)) return true;
    return false;
}

static void printSummary(const WorkOnsController &c)
{
    const auto pips = [](const QVariant &v) {
        QString out;
        for (const QVariant &b : v.toList()) out += b.toBool() ? QLatin1Char('*') : QLatin1Char('.');
        return out;
    };
    const auto printFault = [&pips](const char *what, const QVariant &v) {
        const QVariantMap m = v.toMap();
        std::printf("      %s: %s — %s (%.3f, trend %d) [%s]\n", what, qPrintable(m.value(QStringLiteral("text")).toString()),
                    qPrintable(m.value(QStringLiteral("frequency")).toString()), m.value(QStringLiteral("share")).toDouble(),
                    m.value(QStringLiteral("trend")).toInt(), qPrintable(pips(m.value(QStringLiteral("sessions")))));
    };
    std::printf("      %s\n", qPrintable(c.summarySubtitle()));
    const QVariantMap focus = c.focusItem();
    if (focus.value(QStringLiteral("present")).toBool()) {
        std::printf("      focus: %s\n", qPrintable(focus.value(QStringLiteral("title")).toString()));
        for (const QVariant &a : focus.value(QStringLiteral("aimFor")).toList())
            std::printf("        aim for: %s\n", qPrintable(a.toString()));
        for (const QVariant &v : focus.value(QStringLiteral("rightNow")).toList()) printFault("  right now", v);
        std::printf("        why: %s\n", qPrintable(focus.value(QStringLiteral("why")).toString()));
        std::printf("        practise (%s): %s\n", qPrintable(focus.value(QStringLiteral("practiseLabel")).toString()),
                    qPrintable(focus.value(QStringLiteral("practise")).toString()));
        std::printf("        %s\n", qPrintable(focus.value(QStringLiteral("reason")).toString()));
    } else {
        std::printf("      focus: none\n");
    }
    for (const QVariant &v : c.doWellItems()) {
        const QVariantMap m = v.toMap();
        std::printf("      do well: %s — %s [%s]\n", qPrintable(m.value(QStringLiteral("text")).toString()),
                    qPrintable(m.value(QStringLiteral("caption")).toString()), qPrintable(pips(m.value(QStringLiteral("sessions")))));
    }
    for (const QVariant &v : c.nextItems()) printFault("next", v);
    for (const QVariant &v : c.togetherItems()) {
        const QVariantMap m = v.toMap();
        std::printf("      together [%s]: %s / %s — %s (stop %d, trend %d)\n",
                    qPrintable(m.value(QStringLiteral("tier")).toString()), qPrintable(m.value(QStringLiteral("first")).toString()),
                    qPrintable(m.value(QStringLiteral("second")).toString()), qPrintable(m.value(QStringLiteral("startWords")).toString()),
                    m.value(QStringLiteral("startStop")).toInt(), m.value(QStringLiteral("trend")).toInt());
    }
    if (!c.togetherNote().isEmpty()) std::printf("      note: %s\n", qPrintable(c.togetherNote()));
}

static bool keysAre(const QVariantMap &m, QStringList want)
{
    QStringList have = m.keys();
    have.sort();
    want.sort();
    return have == want;
}

static bool isPips(const QVariant &v)
{
    if (v.typeId() != QMetaType::QVariantList) return false;
    for (const QVariant &b : v.toList())
        if (b.typeId() != QMetaType::Bool) return false;
    return true;
}

// A stored row (seenMost / doWell of swing_themes.json) with exactly these pips — and, when given,
// this share. Its pips are one per judged session, so a match says the item's are sized to them.
static bool storedRowMatches(const QJsonArray &rows, const QVariantList &sessions, double share = -1.0)
{
    for (const QJsonValue &rv : rows) {
        const QJsonObject r = rv.toObject();
        const QJsonArray p = r.value(QStringLiteral("pips")).toArray();
        if (p.size() != sessions.size() || r.value(QStringLiteral("sessionsJudged")).toInt() != p.size()) continue;
        bool same = true;
        for (qsizetype i = 0; i < p.size() && same; ++i) same = p.at(i).toBool() == sessions.at(i).toBool();
        if (!same) continue;
        if (share >= 0.0 && std::abs(r.value(QStringLiteral("share")).toDouble() - share) > 1e-12) continue;
        return true;
    }
    return false;
}

// A fault item — the focus's "right now" and "next on your list" alike: exactly its five keys, the
// values in range, and its share and pips a stored needs-work row's (one pip per judged session).
static bool isFaultItem(const QVariant &v, const QJsonArray &storedSeen)
{
    const QVariantMap m = v.toMap();
    const QString freq = m.value(QStringLiteral("frequency")).toString();
    const int trend = m.value(QStringLiteral("trend")).toInt();
    return keysAre(m, { QStringLiteral("text"), QStringLiteral("share"), QStringLiteral("frequency"),
                        QStringLiteral("trend"), QStringLiteral("sessions") })
        && !m.value(QStringLiteral("text")).toString().isEmpty()
        && m.value(QStringLiteral("text")).toString().at(0).isUpper()
        && m.value(QStringLiteral("share")).typeId() == QMetaType::Double
        && m.value(QStringLiteral("trend")).typeId() == QMetaType::Int && trend >= -1 && trend <= 1
        && (freq == QStringLiteral("every swing") || freq == QStringLiteral("almost every swing")
            || freq == QStringLiteral("most swings") || freq == QStringLiteral("more than half your swings"))
        && isPips(m.value(QStringLiteral("sessions")))
        && storedRowMatches(storedSeen, m.value(QStringLiteral("sessions")).toList(),
                            m.value(QStringLiteral("share")).toDouble());
}

// The shape of every item the home screen and Swing diagnostics read: exactly the keys
// HmFocus.qml, HmSwingSummary.qml and HmGoesTogether.qml bind to, the values in range, and each
// card's per-session pips those of a stored row (one per judged session).
static void checkSummaryShape(const WorkOnsController &c, const QJsonObject &stored, const char *when)
{
    bool well = true, focus = true, next = true, together = true;
    const QJsonArray storedWell = stored.value(QStringLiteral("doWell")).toArray();
    const QJsonArray storedSeen = stored.value(QStringLiteral("seenMost")).toArray();
    for (const QVariant &v : c.doWellItems()) {
        const QVariantMap m = v.toMap();
        well = well && keysAre(m, { QStringLiteral("text"), QStringLiteral("caption"), QStringLiteral("sessions") })
            && !m.value(QStringLiteral("text")).toString().isEmpty()
            && m.value(QStringLiteral("text")).toString().at(0).isUpper()
            && m.value(QStringLiteral("caption")).toString().startsWith(QStringLiteral("In the ideal range on "))
            && isPips(m.value(QStringLiteral("sessions")))
            && storedRowMatches(storedWell, m.value(QStringLiteral("sessions")).toList());
    }
    // The focus: every key, always; when present a title, at least one fault right now, each aim
    // a capitalised phrase, the drill's words together or not at all, and the reason a sentence.
    const QVariantMap f = c.focusItem();
    const QVariantList rightNow = f.value(QStringLiteral("rightNow")).toList();
    const QVariantList aimFor = f.value(QStringLiteral("aimFor")).toList();
    QStringList faultTexts;
    focus = keysAre(f, { QStringLiteral("present"), QStringLiteral("title"), QStringLiteral("aimFor"),
                         QStringLiteral("rightNow"), QStringLiteral("why"), QStringLiteral("practiseLabel"),
                         QStringLiteral("practise"), QStringLiteral("reason") })
        && f.value(QStringLiteral("present")).typeId() == QMetaType::Bool
        && f.value(QStringLiteral("aimFor")).typeId() == QMetaType::QVariantList
        && f.value(QStringLiteral("rightNow")).typeId() == QMetaType::QVariantList;
    if (focus && f.value(QStringLiteral("present")).toBool()) {
        const QString reason = f.value(QStringLiteral("reason")).toString();
        focus = !f.value(QStringLiteral("title")).toString().isEmpty() && !rightNow.isEmpty()
            && f.value(QStringLiteral("practise")).toString().isEmpty() == f.value(QStringLiteral("practiseLabel")).toString().isEmpty()
            && reason.startsWith(QStringLiteral("Picked first: it ")) && reason.endsWith(QLatin1Char('.'));
        for (const QVariant &a : aimFor)
            focus = focus && a.typeId() == QMetaType::QString && !a.toString().isEmpty() && a.toString().at(0).isUpper();
        for (const QVariant &v : rightNow) {
            focus = focus && isFaultItem(v, storedSeen);
            faultTexts << v.toMap().value(QStringLiteral("text")).toString();
        }
    } else if (focus) {
        focus = rightNow.isEmpty() && aimFor.isEmpty() && c.nextItems().isEmpty();
    }
    // Next on the list: at most four fault items, none told twice nor told already by the focus.
    next = c.nextItems().size() <= 4;
    for (const QVariant &v : c.nextItems()) {
        const QString t = v.toMap().value(QStringLiteral("text")).toString();
        next = next && isFaultItem(v, storedSeen) && !faultTexts.contains(t);
        faultTexts << t;
    }
    for (const QVariant &v : c.togetherItems()) {
        const QVariantMap m = v.toMap();
        const QString tier = m.value(QStringLiteral("tier")).toString();
        const QString first = m.value(QStringLiteral("first")).toString(), second = m.value(QStringLiteral("second")).toString();
        const int stop = m.value(QStringLiteral("startStop")).toInt(), trend = m.value(QStringLiteral("trend")).toInt();
        together = together && keysAre(m, { QStringLiteral("tier"), QStringLiteral("first"), QStringLiteral("second"),
                                            QStringLiteral("startStop"), QStringLiteral("startWords"), QStringLiteral("trend") })
            && (tier == QStringLiteral("firm") || tier == QStringLiteral("probably") || tier == QStringLiteral("possibly"))
            && !first.isEmpty() && (second.isEmpty() || first.startsWith(QStringLiteral("When ")))
            && m.value(QStringLiteral("startStop")).typeId() == QMetaType::Int && stop >= -1 && stop <= 5
            && (stop < 0) == m.value(QStringLiteral("startWords")).toString().isEmpty()
            && m.value(QStringLiteral("trend")).typeId() == QMetaType::Int && trend >= -1 && trend <= 1;
    }
    char label[256];
    std::snprintf(label, sizeof label, "%s: every 'do well' item is {text, caption, sessions}, its pips a stored row's", when);
    check(well, label);
    std::snprintf(label, sizeof label, "%s: at most three 'do well' items", when);
    check(c.doWellItems().size() <= 3, label);
    std::snprintf(label, sizeof label, "%s: the focus is {present, title, aimFor, rightNow, why, practiseLabel, "
                                       "practise, reason}, each 'right now' a fault item a stored row backs", when);
    check(focus, label);
    std::snprintf(label, sizeof label, "%s: 'next on your list' is at most four fault items a stored row backs, "
                                       "none the focus already tells", when);
    check(next, label);
    std::snprintf(label, sizeof label, "%s: every 'goes together' item is {tier, first, second, startStop, startWords, trend}", when);
    check(together, label);
    std::snprintf(label, sizeof label, "%s: the note is said exactly when nothing goes together", when);
    check(c.togetherItems().isEmpty() != c.togetherNote().isEmpty(), label);
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

    // ── The swing summary on too little data ────────────────────────────────────
    check(c.summaryReady() && !c.summaryUpdating() && QFile::exists(WorkOnsController::themesPath(golfer)),
          "the catch-up ends in a swing summary, kept beside the sessions");
    check(c.togetherItems().isEmpty() && c.togetherNote().startsWith(QStringLiteral("Not yet"))
          && c.togetherNote().contains(QStringLiteral("20 swings over 2 sessions")),
          "two small sessions: what goes together says 'not yet', with the count so far");
    check(c.focusItem().value(QStringLiteral("present")).toBool() && c.summarySwings() == 20 && c.summarySessions() == 2,
          "…and there is still a focus");
    check(c.summarySubtitle() == QStringLiteral("From 20 swings over 2 sessions"),
          "the subtitle counts every swing and session read: 'From 20 swings over 2 sessions'");
    {
        // One needs-work fault — the reverse spine angle, on every swing of both sessions (the ball
        // position is a setup condition and the pull an outcome: neither is a fault). It has no
        // drill, so the focus is the fault on its own: titled by its ideal, nothing to practise,
        // and nothing left for next.
        const QVariantMap focus = c.focusItem();
        const QVariantList rightNow = focus.value(QStringLiteral("rightNow")).toList();
        const QVariantMap spine = rightNow.value(0).toMap();
        check(rightNow.size() == 1 && c.nextItems().isEmpty()
              && spine.value(QStringLiteral("sessions")).toList() == QVariantList{ true, true }
              && spine.value(QStringLiteral("frequency")).toString() == QStringLiteral("every swing"),
              "the fault on every swing of both sessions is the focus: 'every swing', a filled pip for each");
        check(focus.value(QStringLiteral("practise")).toString().isEmpty()
              && focus.value(QStringLiteral("practiseLabel")).toString().isEmpty()
              && !focus.value(QStringLiteral("title")).toString().isEmpty()
              && focus.value(QStringLiteral("aimFor")).toList().value(0).toString() == focus.value(QStringLiteral("title")).toString(),
              "…a fault with no drill: titled by its ideal, which is also what to aim for, and nothing to practise");
    }
    checkSummaryShape(c, themesFile(golfer), "two small sessions");

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
    check(!c.summaryReady() && c.summarySubtitle().isEmpty() && c.doWellItems().isEmpty()
          && c.focusItem().isEmpty() && c.nextItems().isEmpty()
          && c.togetherItems().isEmpty() && c.togetherNote().isEmpty()
          && !QFile::exists(WorkOnsController::themesPath(other)),
          "…and so does the summary, with no file written into a folder with no sessions");
    c.setAthleteDir(golfer);
    check(c.items().size() == 2 && c.sessionCount() == 4, "…and coming back reads the records without deriving");

    // ══ The swing summary, threaded ══════════════════════════════════════════════════════
    //
    // Off the GUI thread as the app runs it: the job on its own pool, its replicates over a
    // second, delivered back queued. waitForIdle() waits for all of it.
    const QString themed = tmp.filePath(QStringLiteral("Themed"));
    QDir().mkpath(themed);
    stageThemed(themed, QStringLiteral("2026-07-01_a"), t0,           11);
    stageThemed(themed, QStringLiteral("2026-07-02_b"), t0 + day,     22);
    stageThemed(themed, QStringLiteral("2026-07-03_c"), t0 + 2 * day, 33);

    {
        WorkOnsController t;
        t.setAthleteDir(themed);
        check(t.waitForIdle(300'000), "the threaded catch-up and the reduction settle");
        std::printf("    after three sessions:\n");
        printSummary(t);
        check(QFile::exists(WorkOnsController::themesPath(themed))
              && themesFile(themed).value(QStringLiteral("ruleVersion")).toInt() == kThemeRuleVersion
              && !themesFile(themed).value(QStringLiteral("derivedFrom")).toObject()
                      .value(QStringLiteral("fingerprint")).toString().isEmpty(),
              "swing_themes.json is written, stamped with the rule and a fingerprint");
        check(t.summaryReady() && !t.summaryUpdating() && t.summarySwings() == 90 && t.summarySessions() == 3,
              "the summary is ready: 90 swings over 3 sessions");
        check(!t.togetherItems().isEmpty() || !t.togetherNote().isEmpty(), "what goes together says something");
        const QVariantMap first = t.togetherItems().value(0).toMap();
        const QString firstText = first.value(QStringLiteral("first")).toString() + QLatin1Char(' ')
                                + first.value(QStringLiteral("second")).toString();
        check(t.togetherNote().isEmpty()
              && (firstText.contains(QStringLiteral("hips")) || firstText.contains(QStringLiteral("lead knee")))
              && !first.value(QStringLiteral("tier")).toString().isEmpty(),
              "the planted co-movement is told: the hips and the lead knee, with its tier");
        // The one needs-work fault is the focus, and nothing is left for next.
        const QVariantMap focus = t.focusItem();
        const QVariantList rightNow = focus.value(QStringLiteral("rightNow")).toList();
        const QVariantMap lead = rightNow.value(0).toMap();
        check(focus.value(QStringLiteral("present")).toBool() && rightNow.size() == 1 && t.nextItems().isEmpty()
              && lead.value(QStringLiteral("text")).toString() == QStringLiteral("Your hips sway away from the target going back")
              && lead.value(QStringLiteral("frequency")).toString() == QStringLiteral("every swing")
              && lead.value(QStringLiteral("share")).toDouble() >= 0.995
              && lead.value(QStringLiteral("sessions")).toList() == QVariantList{ true, true, true },
              "the focus is the fault on every swing, 'every swing', a filled pip for each of the 3 sessions; nothing is next");
        check(!focus.value(QStringLiteral("practise")).toString().isEmpty()
              && focus.value(QStringLiteral("title")).toString() == focus.value(QStringLiteral("practiseLabel")).toString()
              && !focus.value(QStringLiteral("aimFor")).toList().isEmpty(),
              "…its title the drill's label, how to practise the drill's instruction, and what to aim for");
        check(focus.value(QStringLiteral("reason")).toString() == QStringLiteral("Picked first: it comes earliest in your swing."),
              "…chosen because it comes earliest (the only group), and covering one thing, it says no more");
        check(t.summarySubtitle() == QStringLiteral("From 90 swings over 3 sessions"),
              "the subtitle: 'From 90 swings over 3 sessions'");
        checkSummaryShape(t, themesFile(themed), "three sessions");
        check(!saysCause(summaryText(t)), "no string says because / cause / due to / leads to");

        // Nothing changed: nothing is recomputed or rewritten.
        const double w = themesWrittenAt(themed);
        const QDateTime mtime = QFileInfo(WorkOnsController::themesPath(themed)).lastModified();
        t.refresh();
        t.waitForIdle(300'000);
        check(w > 0 && themesWrittenAt(themed) == w
              && QFileInfo(WorkOnsController::themesPath(themed)).lastModified() == mtime && t.summaryReady(),
              "a second refresh with nothing changed does not rewrite the file");

        // A session arrives: recomputed.
        stageThemed(themed, QStringLiteral("2026-07-04_d"), t0 + 3 * day, 44);
        t.refresh();
        t.waitForIdle(300'000);
        check(themesWrittenAt(themed) != w && t.summarySwings() == 120 && t.summarySessions() == 4
              && t.summarySubtitle() == QStringLiteral("From 120 swings over 4 sessions"),
              "a new session makes it recompute: 120 swings over 4 sessions");
        checkSummaryShape(t, themesFile(themed), "four sessions");

        // A session goes live part way through a recompute: the job stops, nothing is written, the
        // summary on screen stays — and the pause lifting computes it.
        const double w4 = themesWrittenAt(themed);
        stageThemed(themed, QStringLiteral("2026-07-05_e"), t0 + 4 * day, 55);
        t.refresh();
        QDeadlineTimer until(120'000);
        while (!t.summaryUpdating() && !until.hasExpired())
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        if (t.summaryUpdating()) {
            t.setPaused(true);
            t.waitForIdle(300'000);
            check(!t.summaryUpdating() && t.summaryReady() && t.summarySwings() == 120
                  && themesWrittenAt(themed) == w4,
                  "paused mid-reduction: cancelled, not written, the last summary still shown");
        } else {
            std::printf("  [SKIP] the recompute finished before it could be paused\n");
            t.setPaused(true);
        }
        t.setPaused(false);
        t.waitForIdle(300'000);
        check(t.summaryReady() && t.summarySwings() == 150 && themesWrittenAt(themed) != w4,
              "the pause lifting recomputes it: 150 swings over 5 sessions");
    }

    // Read, not recomputed, by a controller that finds the file current.
    {
        const double w = themesWrittenAt(themed);
        WorkOnsController t;
        t.setAthleteDir(themed);
        t.waitForIdle(300'000);
        check(t.summaryReady() && t.summarySwings() == 150 && themesWrittenAt(themed) == w,
              "a current swing_themes.json is read at start-up, not recomputed");
    }

    // A file from another rule is recomputed.
    {
        QJsonObject r = themesFile(themed);
        r[QStringLiteral("ruleVersion")] = kThemeRuleVersion + 1;
        QFile f(WorkOnsController::themesPath(themed));
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            f.write(QJsonDocument(r).toJson());
        f.close();
        const double w = themesWrittenAt(themed);
        WorkOnsController t;
        t.setAthleteDir(themed);
        t.waitForIdle(300'000);
        check(t.summaryReady() && themesFile(themed).value(QStringLiteral("ruleVersion")).toInt() == kThemeRuleVersion
              && themesWrittenAt(themed) != w,
              "a swing_themes.json written under another rule is recomputed");
    }

    std::printf(g_fail ? "FAILED (%d)\n" : "OK\n", g_fail);
    return g_fail ? 1 : 0;
}
