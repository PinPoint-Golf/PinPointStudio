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
// And Home's "Your swing" areas (swing_areas.h, built in publish() from the same records): the
// shape QML binds to, the display order, the grade beside a typical value, and the newest session.
// swing_areas_test proves the counting; this proves the plumbing and the words.
//
// And each session's Consistency table (consistency_summary.h), kept by the same scan on its own
// fingerprint and its own queue, over REAL swing documents staged from rich_7iron: written on the
// first scan, the same table the tab computes, left alone when fresh, derived again when a swing or
// the rule moves, never touching the work-on records or the summary, holding while a session is live,
// and rolled up into the three keys Home reads. session_consistency_test proves the rules.
//
// The other sessions are LEDGERS ON DISK with empty swing folders beside them: the model loads the
// rows, finds nothing to back-fill, and no swing document is ever read — the catch-up path for
// a library that already has its ledgers, which is the one the app meets first.

#include "Gui/diagnostics/work_ons_controller.h"
#include "Gui/charts/consistency_env.h"
#include "Gui/charts/session_consistency.h"
#include "Gui/charts/session_series_model.h"
#include "Gui/charts/session_warp.h"
#include "Coaching/swing_areas_pack.h"
#include "Diagnostics/packs/characteristic_pack.h"
#include "Export/swing_store.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDeadlineTimer>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QTemporaryDir>
#include <QThread>

#include <algorithm>
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
                        QStringLiteral("trend"), QStringLiteral("sessions"), QStringLiteral("conditionId") })
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

// The shape of every item the home screen reads: exactly the keys
// HmFocus.qml, HmSwingSummary.qml and HmGoesTogether.qml bind to, the values in range, and each
// card's per-session pips those of a stored row (one per judged session).
static void checkSummaryShape(const WorkOnsController &c, const QJsonObject &stored, const char *when)
{
    bool well = true, focus = true, next = true, together = true;
    const QJsonArray storedWell = stored.value(QStringLiteral("doWell")).toArray();
    const QJsonArray storedSeen = stored.value(QStringLiteral("seenMost")).toArray();
    for (const QVariant &v : c.doWellItems()) {
        const QVariantMap m = v.toMap();
        const double share = m.value(QStringLiteral("share")).toDouble();
        well = well && keysAre(m, { QStringLiteral("text"), QStringLiteral("caption"), QStringLiteral("share"),
                                    QStringLiteral("sessions") })
            && m.value(QStringLiteral("share")).typeId() == QMetaType::Double && share >= 0.0 && share <= 1.0
            && !m.value(QStringLiteral("text")).toString().isEmpty()
            && m.value(QStringLiteral("text")).toString().at(0).isUpper()
            && m.value(QStringLiteral("caption")).toString().startsWith(QStringLiteral("In the ideal range on "))
            && isPips(m.value(QStringLiteral("sessions")))
            && storedRowMatches(storedWell, m.value(QStringLiteral("sessions")).toList(), share);
    }
    // The focus: every key, always; when present a title, at least one fault right now, each aim
    // a capitalised phrase, the drill's words together or not at all, and the reason a sentence.
    const QVariantMap f = c.focusItem();
    const QVariantList rightNow = f.value(QStringLiteral("rightNow")).toList();
    const QVariantList aimFor = f.value(QStringLiteral("aimFor")).toList();
    QStringList faultTexts;
    focus = keysAre(f, { QStringLiteral("present"), QStringLiteral("title"), QStringLiteral("aimFor"),
                         QStringLiteral("rightNow"), QStringLiteral("why"), QStringLiteral("practiseLabel"),
                         QStringLiteral("practise"), QStringLiteral("reason"), QStringLiteral("reasonShort"),
                         QStringLiteral("focusConditionId") })
        && f.value(QStringLiteral("present")).typeId() == QMetaType::Bool
        && f.value(QStringLiteral("aimFor")).typeId() == QMetaType::QVariantList
        && f.value(QStringLiteral("rightNow")).typeId() == QMetaType::QVariantList
        && f.value(QStringLiteral("focusConditionId")).typeId() == QMetaType::QString;
    // The focus condition: the first "right now" fault's id — a stored needs-work row with that
    // fault's share and pips, in a stored focus group, with a name to show; "" with no focus.
    const QString focusId = f.value(QStringLiteral("focusConditionId")).toString();
    bool focusIdOk = rightNow.isEmpty() == focusId.isEmpty();
    if (focusIdOk && !focusId.isEmpty()) {
        const QVariantMap first = rightNow.first().toMap();
        bool row = false, grouped = false;
        for (const QJsonValue &rv : storedSeen) {
            const QJsonObject r = rv.toObject();
            if (r.value(QStringLiteral("id")).toString() != focusId) continue;
            row = storedRowMatches(QJsonArray{ r }, first.value(QStringLiteral("sessions")).toList(),
                                   first.value(QStringLiteral("share")).toDouble());
        }
        for (const QJsonValue &gv : stored.value(QStringLiteral("focusOrder")).toArray())
            grouped = grouped || gv.toObject().value(QStringLiteral("conditionIds")).toArray().contains(QJsonValue(focusId));
        focusIdOk = row && grouped && !c.conditionLabel(focusId).isEmpty();
    }
    if (focus && f.value(QStringLiteral("present")).toBool()) {
        const QString reason = f.value(QStringLiteral("reason")).toString();
        // The short reason: the same clauses, never the sentence — non-empty, no "Picked first", no
        // full stop, and its "why" clause the sentence's own.
        const QString shortReason = f.value(QStringLiteral("reasonShort")).toString();
        const QString why = shortReason.section(QStringLiteral(" · "), -1);
        focus = !f.value(QStringLiteral("title")).toString().isEmpty() && !rightNow.isEmpty()
            && !shortReason.isEmpty() && !shortReason.startsWith(QStringLiteral("Picked")) && !shortReason.endsWith(QLatin1Char('.'))
            && reason.startsWith(QStringLiteral("Picked first: it ") + why)
            && (rightNow.size() < 2) == !shortReason.contains(QStringLiteral(" · "))
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
    std::snprintf(label, sizeof label, "%s: every 'do well' item is {text, caption, share, sessions}, its share in 0..1 and "
                                       "its share and pips a stored row's", when);
    check(well, label);
    std::snprintf(label, sizeof label, "%s: at most three 'do well' items", when);
    check(c.doWellItems().size() <= 3, label);
    std::snprintf(label, sizeof label, "%s: the focus is {present, title, aimFor, rightNow, why, practiseLabel, "
                                       "practise, reason, reasonShort, focusConditionId}, each 'right now' a fault item a "
                                       "stored row backs, and a present focus has a short reason", when);
    check(focus, label);
    std::snprintf(label, sizeof label, "%s: focusConditionId is the first 'right now' fault's stored row, in a stored "
                                       "focus group, with a label (\"\" when nothing is right now)", when);
    check(focusIdOk, label);
    std::snprintf(label, sizeof label, "%s: 'next on your list' is at most four fault items a stored row backs, "
                                       "none the focus already tells", when);
    check(next, label);
    std::snprintf(label, sizeof label, "%s: every 'goes together' item is {tier, first, second, startStop, startWords, trend}", when);
    check(together, label);
    std::snprintf(label, sizeof label, "%s: the note is said exactly when nothing goes together", when);
    check(c.togetherItems().isEmpty() != c.togetherNote().isEmpty(), label);
}

// ── The areas ────────────────────────────────────────────────────────────────────────────

static QVariantMap area(const WorkOnsController &c, const char *group)
{
    for (const QVariant &v : c.areaItems())
        if (v.toMap().value(QStringLiteral("group")).toString() == QLatin1String(group)) return v.toMap();
    return {};
}

static QVariantMap areaItem(const QVariantMap &a, const char *id)
{
    for (const QVariant &v : a.value(QStringLiteral("items")).toList())
        if (v.toMap().value(QStringLiteral("conditionId")).toString() == QLatin1String(id)) return v.toMap();
    return {};
}

static QStringList areaGroups(const WorkOnsController &c)
{
    QStringList out;
    for (const QVariant &v : c.areaItems()) out << v.toMap().value(QStringLiteral("group")).toString();
    return out;
}

// What Home's "Your swing" QML binds to: exactly these keys on every area and every item, the values in range,
// and the two orders — areas measurable first and by habits, items habits first, then occasional,
// then the rest, by share within each — and a corridor grade exactly when there is a typical value;
// without one, only a yes/no condition's Action.
static void checkAreasShape(const WorkOnsController &c, const char *when)
{
    static const QStringList bands{ QStringLiteral("habit"), QStringLiteral("occasional"),
                                    QStringLiteral("clean"), QStringLiteral("unclear") };
    static const QStringList grades{ QStringLiteral("ideal"), QStringLiteral("good"),
                                     QStringLiteral("watch"), QStringLiteral("action") };
    static const QStringList workOns{ QString(), QStringLiteral("active"), QStringLiteral("easing"), QStringLiteral("new") };
    const auto rank = [](const QString &band) {
        return band == QLatin1String("habit") ? 0 : band == QLatin1String("occasional") ? 1 : 2;
    };

    bool areaKeys = true, itemKeys = true, values = true, areaOrder = true, itemOrder = true, gradeIff = true;
    bool seenUnmeasurable = false;
    int lastHabits = 1 << 30;
    for (const QVariant &av : c.areaItems()) {
        const QVariantMap a = av.toMap();
        areaKeys = areaKeys && keysAre(a, { QStringLiteral("group"), QStringLiteral("label"), QStringLiteral("measurable"),
                                            QStringLiteral("measured"), QStringLiteral("habits"), QStringLiteral("occasional"),
                                            QStringLiteral("clean"), QStringLiteral("unclear"), QStringLiteral("habitTrend"),
                                            QStringLiteral("hasTrend"), QStringLiteral("topFault"), QStringLiteral("topFaultId"),
                                            QStringLiteral("workOnsActive"), QStringLiteral("workOnsNew"), QStringLiteral("items"),
                                            QStringLiteral("consistencyWord"), QStringLiteral("consistencyLoosest"),
                                            QStringLiteral("consistencySpans") });
        const bool measurable = a.value(QStringLiteral("measurable")).toBool();
        const int habits = a.value(QStringLiteral("habits")).toInt();
        const int counted = habits + a.value(QStringLiteral("occasional")).toInt() + a.value(QStringLiteral("clean")).toInt()
                          + a.value(QStringLiteral("unclear")).toInt();
        const QVariantList items = a.value(QStringLiteral("items")).toList();
        values = values && !a.value(QStringLiteral("label")).toString().isEmpty()
            && a.value(QStringLiteral("measured")).toInt() == counted
            && int(items.size()) <= std::min(counted, 5)
            && a.value(QStringLiteral("topFaultId")).toString().isEmpty() == a.value(QStringLiteral("topFault")).toString().isEmpty()
            && (a.value(QStringLiteral("hasTrend")).toBool() || a.value(QStringLiteral("habitTrend")).toInt() == 0)
            && a.value(QStringLiteral("workOnsNew")).toInt() <= a.value(QStringLiteral("workOnsActive")).toInt()
            && (measurable || (counted == 0 && items.isEmpty()));
        // Measurable first, by habits; then the unmeasurable ones.
        if (!measurable) seenUnmeasurable = true;
        else {
            areaOrder = areaOrder && !seenUnmeasurable && habits <= lastHabits;
            lastHabits = habits;
        }

        int lastRank = -1;
        double lastShare = 2.0;
        for (const QVariant &iv : items) {
            const QVariantMap it = iv.toMap();
            itemKeys = itemKeys && keysAre(it, { QStringLiteral("conditionId"), QStringLiteral("label"), QStringLiteral("golfer"),
                                                 QStringLiteral("band"), QStringLiteral("share"), QStringLiteral("fired"),
                                                 QStringLiteral("assessable"), QStringLiteral("measureId"), QStringLiteral("hasTypical"),
                                                 QStringLiteral("typical"), QStringLiteral("corridorLo"), QStringLiteral("corridorHi"),
                                                 QStringLiteral("corridorShape"), QStringLiteral("tail"), QStringLiteral("unit"), QStringLiteral("grade"),
                                                 QStringLiteral("workOn"), QStringLiteral("sessionsListed") });
            const QString band = it.value(QStringLiteral("band")).toString();
            const double share = it.value(QStringLiteral("share")).toDouble();
            const int fired = it.value(QStringLiteral("fired")).toInt(), assessable = it.value(QStringLiteral("assessable")).toInt();
            values = values && bands.contains(band) && share >= 0.0 && share <= 1.0
                && assessable > 0 && fired >= 0 && fired <= assessable
                && !it.value(QStringLiteral("label")).toString().isEmpty()
                && workOns.contains(it.value(QStringLiteral("workOn")).toString())
                // A work-on word only for something ever listed; a CLEARED one was listed and reads "".
                && (it.value(QStringLiteral("workOn")).toString().isEmpty() || it.value(QStringLiteral("sessionsListed")).toInt() > 0);
            const bool typical = it.value(QStringLiteral("hasTypical")).toBool();
            const QString grade = it.value(QStringLiteral("grade")).toString();
            // Without a typical value only a yes/no condition grades, and only Action (areaBinaryFires).
            gradeIff = gradeIff && (typical ? grades.contains(grade) : (grade.isEmpty() || grade == QLatin1String("action")))
                && typical == !it.value(QStringLiteral("corridorShape")).toString().isEmpty();
            const int r = rank(band);
            itemOrder = itemOrder && (r > lastRank || (r == lastRank && share <= lastShare));
            lastRank = r;
            lastShare = share;
        }
    }

    char label[256];
    std::snprintf(label, sizeof label, "%s: every area is {group, label, measurable, measured, habits, occasional, clean, "
                                       "unclear, habitTrend, hasTrend, topFault, topFaultId, workOnsActive, workOnsNew, items}", when);
    check(areaKeys, label);
    std::snprintf(label, sizeof label, "%s: every item is {conditionId, label, golfer, band, share, fired, assessable, "
                                       "measureId, hasTypical, typical, corridorLo, corridorHi, corridorShape, tail, unit, grade, "
                                       "workOn, sessionsListed}", when);
    check(itemKeys, label);
    std::snprintf(label, sizeof label, "%s: the counts add up, at most five items, a band word, share in 0..1, "
                                       "a work-on word only when it was ever listed (cleared reads \"\")", when);
    check(values, label);
    std::snprintf(label, sizeof label, "%s: areas measurable first, most habits first; unmeasurable ones last", when);
    check(areaOrder, label);
    std::snprintf(label, sizeof label, "%s: items habits, then occasional, then the rest, by share within each", when);
    check(itemOrder, label);
    std::snprintf(label, sizeof label, "%s: a corridor grade exactly when there is a typical value; without one, only a yes/no condition's action", when);
    check(gradeIff, label);
}

// ── Consistency fixtures ─────────────────────────────────────────────────────────────────

namespace pc = pinpoint::charts;

#ifndef PP_LIVE_SWINGS_DIR
#  define PP_LIVE_SWINGS_DIR "."
#endif

// A real analysis block, less its pose: what SwingStore::loadAnalysis leaves of rich_7iron.
static const QJsonObject &richSwing()
{
    static const QJsonObject root = pinpoint::SwingStore::loadAnalysis(
        QStringLiteral(PP_LIVE_SWINGS_DIR "/rich_7iron"));
    return root;
}

// One session of the real swing, each shot's every reading scaled by its own small factor so that the
// spreads aren't nothing, written by the real writer, one wall clock apart from the next.
static void stageSwings(const QString &athleteDir, const QString &session, int shots, const QDateTime &when)
{
    for (int i = 0; i < shots; ++i) {
        const double scale = 1.0 + 0.03 * double((i * 7) % 5 - 2);
        QJsonObject root = richSwing();
        QJsonObject clock = root.value(QStringLiteral("clock")).toObject();
        clock[QStringLiteral("wallclock")] = when.addSecs(45 * i).toUTC().toString(Qt::ISODateWithMs);
        root[QStringLiteral("clock")] = clock;

        QJsonObject analysis = root.value(QStringLiteral("analysis")).toObject();
        QJsonArray metrics = analysis.value(QStringLiteral("metrics")).toArray();
        for (qsizetype m = 0; m < metrics.size(); ++m) {
            QJsonObject metric = metrics.at(m).toObject();
            QJsonArray values = metric.value(QStringLiteral("value")).toArray();
            for (qsizetype k = 0; k < values.size(); ++k) values[k] = values.at(k).toDouble() * scale;
            metric[QStringLiteral("value")] = values;
            QJsonArray samples = metric.value(QStringLiteral("phaseSamples")).toArray();
            for (qsizetype k = 0; k < samples.size(); ++k) {
                QJsonObject sample = samples.at(k).toObject();
                sample[QStringLiteral("value")] = sample.value(QStringLiteral("value")).toDouble() * scale;
                samples[k] = sample;
            }
            metric[QStringLiteral("phaseSamples")] = samples;
            metrics[m] = metric;
        }
        analysis[QStringLiteral("metrics")] = metrics;
        root[QStringLiteral("analysis")] = analysis;

        const QString dir = QDir(athleteDir).filePath(session) + QStringLiteral("/swing_%1").arg(i + 1, 4, 10, QLatin1Char('0'));
        QDir().mkpath(dir);
        QString why;
        if (!pinpoint::SwingStore::save(dir, root, &why))
            std::printf("  could not stage %s: %s\n", qPrintable(dir), qPrintable(why));
    }
}

static QString tablePath(const QString &athleteDir, const QString &session)
{
    return pc::consistencySummaryPath(QDir(athleteDir).filePath(session));
}

static QByteArray bytesOf(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

static QJsonObject jsonOf(const QString &path) { return QJsonDocument::fromJson(bytesOf(path)).object(); }

static QString fingerprintOf(const QString &path)
{
    return jsonOf(path).value(QStringLiteral("derivedFrom")).toObject().value(QStringLiteral("fingerprint")).toString();
}

// Backdated, so that a rewrite shows however fast the test runs.
static void backdate(const QString &path, const QDateTime &when)
{
    QFile f(path);
    if (f.open(QIODevice::ReadWrite)) f.setFileTime(when, QFileDevice::FileModificationTime);
}

static QDateTime mtimeOf(const QString &path) { return QFileInfo(path).lastModified(); }

// Every cell of a stored table set to `rest`, then the named ones ("area|column") to their word. The
// derivedFrom stays, so the file reads as fresh and the scan carries these words to Home.
static void setWords(const QString &path, const QHash<QString, QString> &words, const QString &rest)
{
    QJsonObject root = jsonOf(path);
    QJsonArray cells = root.value(QStringLiteral("cells")).toArray();
    for (qsizetype i = 0; i < cells.size(); ++i) {
        QJsonObject c = cells.at(i).toObject();
        const QString id = c.value(QStringLiteral("area")).toString() + QLatin1Char('|')
                         + QString::number(c.value(QStringLiteral("column")).toInt());
        c[QStringLiteral("word")] = words.value(id, rest);
        cells[i] = c;
    }
    root[QStringLiteral("cells")] = cells;
    QFile f(path);
    if (f.open(QIODevice::WriteOnly)) f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

static bool areaSays(const WorkOnsController &c, const char *group, const char *word, const char *loosest, const QStringList &spans)
{
    const QVariantMap a = area(c, group);
    QStringList have;
    for (const QVariant &v : a.value(QStringLiteral("consistencySpans")).toList()) {
        const QVariantMap m = v.toMap();
        have << m.value(QStringLiteral("span")).toString() + QLatin1Char(' ') + m.value(QStringLiteral("word")).toString();
    }
    const bool same = a.value(QStringLiteral("consistencyWord")).toString() == QLatin1String(word)
                   && a.value(QStringLiteral("consistencyLoosest")).toString() == QLatin1String(loosest) && have == spans;
    if (!same)
        std::printf("      %s: %s / %s / %s\n", group, qPrintable(a.value(QStringLiteral("consistencyWord")).toString()),
                    qPrintable(a.value(QStringLiteral("consistencyLoosest")).toString()), qPrintable(have.join(QStringLiteral(", "))));
    return same;
}

static void checkConsistency()
{
    std::printf("consistency.json\n");
    QTemporaryDir tmp;
    const QDateTime sep1 = QDateTime::fromMSecsSinceEpoch(1'790'000'000'000LL), sep2 = sep1.addDays(1);
    const QDateTime old = QDateTime::currentDateTime().addDays(-30);

    // ── The file, on the first scan ──────────────────────────────────────────────────
    const QString golfer = tmp.filePath(QStringLiteral("Tables"));
    QDir().mkpath(golfer);
    const QString first = QStringLiteral("s_first"), second = QStringLiteral("s_second");
    stageSwings(golfer, first, 8, sep1);
    stageSwings(golfer, second, 8, sep2);

    WorkOnsController c;
    c.setSynchronous(true);
    c.setAthleteDir(golfer);
    const QString tableA = tablePath(golfer, first), tableB = tablePath(golfer, second);
    const QString recordA = WorkOnsController::recordPath(QDir(golfer).filePath(first));
    const QString recordB = WorkOnsController::recordPath(QDir(golfer).filePath(second));
    const QString themes = WorkOnsController::themesPath(golfer);

    const QJsonObject file = jsonOf(tableA);
    check(QFile::exists(tableA) && QFile::exists(tableB) && c.consistencyJobs() == 2, "the first scan writes a table for each session");
    check(keysAre(file.toVariantMap(), { QStringLiteral("schema"), QStringLiteral("derivedFrom"), QStringLiteral("shots"),
                                         QStringLiteral("sessionDate"), QStringLiteral("cells"), QStringLiteral("afterImpactMs") })
          && keysAre(file.value(QStringLiteral("derivedFrom")).toObject().toVariantMap(),
                     { QStringLiteral("fingerprint"), QStringLiteral("ruleVersion") }),
          "…{schema, derivedFrom:{fingerprint, ruleVersion}, shots, sessionDate, cells, afterImpactMs}, nothing more");
    check(file.value(QStringLiteral("schema")).toInt() == pc::kConsistencySummarySchema
          && fingerprintOf(tableA).size() == 16
          && file.value(QStringLiteral("derivedFrom")).toObject().value(QStringLiteral("ruleVersion")).toInt() == pc::kConsistencyRuleVersion
          && file.value(QStringLiteral("shots")).toInt() == 8
          && qint64(file.value(QStringLiteral("sessionDate")).toDouble()) == sep1.toMSecsSinceEpoch()
          && file.value(QStringLiteral("afterImpactMs")).toDouble() > 0.0,
          "…stamped, 8 shots, the first shot's wall clock, P7-P8's length");
    check(file.value(QStringLiteral("cells")).toArray().size() == 33
          && keysAre(file.value(QStringLiteral("cells")).toArray().at(0).toObject().toVariantMap(),
                     { QStringLiteral("area"), QStringLiteral("column"), QStringLiteral("word") }),
          "…33 cells of {area, column, word}");
    check(bytesOf(tableA).size() < 4096, "…a file Home can read in a blink");

    // The table the tab computes: the model's snapshot, the same two layers.
    const pc::ConsistencyEnv env = pc::consistencyEnvFromPacks();
    {
        pc::SessionSeriesModel model;
        model.setSynchronous(true);
        model.activateSession(QDir(golfer).filePath(first));
        const auto snapshot = model.snapshot();
        const pc::ConsistencyResult tab = pc::consistencyResult(pc::consistencyInput(*snapshot, pc::alignSession(*snapshot), env));
        pc::ConsistencySummary stored;
        const bool read = pc::consistencySummaryFromJson(file, &stored);
        bool same = read && stored.cells.size() == tab.cells.size();
        int judged = 0;
        for (size_t i = 0; same && i < tab.cells.size(); ++i) {
            same = stored.cells[i].area == tab.cells[i].area && stored.cells[i].column == tab.cells[i].column
                && stored.cells[i].word == tab.cells[i].word;
            if (tab.cells[i].word != pc::ConsistencyWord::Dash) ++judged;
        }
        check(same && judged > 0, "…the very table the Consistency tab computes, with words in it");
        std::printf("      %d of %d cells judged\n", judged, int(tab.cells.size()));
    }

    // ── Fresh: left alone ────────────────────────────────────────────────────────────
    for (const QString &p : { tableA, tableB, recordA, recordB, themes }) backdate(p, old);
    QByteArray before = bytesOf(tableA);
    int jobs = c.consistencyJobs();
    c.refresh();
    check(c.consistencyJobs() == jobs && bytesOf(tableA) == before && mtimeOf(tableA) == mtimeOf(tableB) && mtimeOf(tableA).date() == old.date(),
          "a fresh table is not derived again or rewritten");

    // ── A work-on freshness of its own ───────────────────────────────────────────────
    const QByteArray recordBytes = bytesOf(recordA), themesBytes = bytesOf(themes);
    QFile::remove(tableA);
    c.refresh();
    check(c.consistencyJobs() == jobs + 1 && QFile::exists(tableA) && mtimeOf(tableB).date() == old.date(),
          "fresh work-ons, a stale table: only the table is queued");
    check(bytesOf(recordA) == recordBytes && mtimeOf(recordA).date() == old.date()
          && bytesOf(themes) == themesBytes && mtimeOf(themes).date() == old.date(),
          "…work_ons.json and swing_themes.json are not touched");
    backdate(tableA, old);

    jobs = c.consistencyJobs();
    QFile::remove(recordB);
    c.refresh();
    check(c.consistencyJobs() == jobs && QFile::exists(recordB) && mtimeOf(tableB).date() == old.date(),
          "fresh table, a stale work-on record: only the record is derived");
    backdate(recordB, old);

    // ── The work-on fingerprint is what it was ───────────────────────────────────────
    {
        const QDir d(QDir(golfer).filePath(first));
        const QJsonObject from = jsonOf(recordA).value(QStringLiteral("derivedFrom")).toObject();
        QCryptographicHash h(QCryptographicHash::Sha1);
        h.addData(QByteArray::number(kWorkOnRuleVersion));
        h.addData(from.value(QStringLiteral("contentStamp")).toString().toUtf8());
        for (const QString &name : d.entryList({ QStringLiteral("swing_*") }, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            const pinpoint::SwingStore::DocInfo doc = pinpoint::SwingStore::info(d.filePath(name));
            h.addData(name.toUtf8());
            h.addData(QByteArray::number(doc.size));
            h.addData(QByteArray::number(doc.mtimeMs));
        }
        check(from.value(QStringLiteral("fingerprint")).toString() == QString::fromLatin1(h.result().toHex().left(16)),
              "work_ons.json's fingerprint is still rule, content and swings, nothing of Consistency's");
    }

    // ── A new rule ───────────────────────────────────────────────────────────────────
    jobs = c.consistencyJobs();
    const QString fpA = fingerprintOf(tableA);
    const QByteArray recordA2 = bytesOf(recordA), themes2 = bytesOf(themes);
    c.setConsistencyRuleVersion(pc::kConsistencyRuleVersion + 1);
    c.refresh();
    check(c.consistencyJobs() == jobs + 2 && fingerprintOf(tableA) != fpA
          && jsonOf(tableB).value(QStringLiteral("derivedFrom")).toObject().value(QStringLiteral("ruleVersion")).toInt() == pc::kConsistencyRuleVersion + 1,
          "a new rule version derives every table again");
    check(bytesOf(recordA) == recordA2 && mtimeOf(recordA).date() == old.date()
          && bytesOf(themes) == themes2 && mtimeOf(themes).date() == old.date(),
          "…and moves no record and no summary");
    c.setConsistencyRuleVersion(pc::kConsistencyRuleVersion);
    c.refresh();
    backdate(tableA, old);
    backdate(tableB, old);

    // ── A swing changes ──────────────────────────────────────────────────────────────
    jobs = c.consistencyJobs();
    const QString fpB = fingerprintOf(tableB);
    backdate(pinpoint::SwingStore::ppswPath(QDir(golfer).filePath(second) + QStringLiteral("/swing_0003")), old.addDays(1));
    c.refresh();
    check(c.consistencyJobs() == jobs + 1 && fingerprintOf(tableB) != fpB && mtimeOf(tableB).date() != old.date()
          && mtimeOf(tableA).date() == old.date(),
          "a swing changing makes its session's table stale, and only that one");
    stageSwings(golfer, second, 9, sep2);       // shot 9 arrives, every document rewritten
    jobs = c.consistencyJobs();
    c.refresh();
    check(c.consistencyJobs() == jobs + 1 && jsonOf(tableB).value(QStringLiteral("shots")).toInt() == 9,
          "a new shot arrives by the same route");

    // ── Corrupt, empty ───────────────────────────────────────────────────────────────
    {
        const QString broken = QDir(golfer).filePath(QStringLiteral("2036-01-02_broken"));
        QDir().mkpath(broken + QStringLiteral("/swing_0001"));
        QFile f(broken + QStringLiteral("/swing_0001/swing.json"));
        if (f.open(QIODevice::WriteOnly)) f.write("{ this is not a swing");
        f.close();
        QDir().mkpath(QDir(golfer).filePath(QStringLiteral("2036-01-01_empty/swing_0001")));
        QFile::remove(tableA);          // dated today by its folder, so behind those two in the queue
        QStringList order;
        c.setConsistencyJobHook([&order](const QString &dir) { order << QFileInfo(dir).fileName(); });
        jobs = c.consistencyJobs();
        c.refresh();
        c.setConsistencyJobHook({});
        check(c.consistencyJobs() == jobs + 3 && !QFile::exists(tablePath(golfer, QStringLiteral("2036-01-02_broken")))
              && !QFile::exists(tablePath(golfer, QStringLiteral("2036-01-01_empty"))),
              "a corrupt session and an empty one write nothing");
        check(QFile::exists(tableA) && order.value(2) == first, "…and the queue goes on past them");
        jobs = c.consistencyJobs();
        c.refresh();
        check(c.consistencyJobs() == jobs, "…they are not tried again until their swings change");
        std::atomic<bool> stop{ true };
        pc::ConsistencySummary none;
        check(!pc::deriveConsistencySummary(QDir(golfer).filePath(first), 8, env, &stop, &none), "a raised cancel stops a job");
    }

    // ── Paused ───────────────────────────────────────────────────────────────────────
    const QString held = tmp.filePath(QStringLiteral("Held"));
    QDir().mkpath(held);
    stageSwings(held, first, 6, sep1);
    {
        WorkOnsController p;
        p.setSynchronous(true);
        p.setPaused(true);
        p.setAthleteDir(held);
        check(p.consistencyJobs() == 0 && !QFile::exists(tablePath(held, first)), "a live session holds the queue");
        p.setPaused(false);
        check(p.consistencyJobs() == 1 && QFile::exists(tablePath(held, first)), "…and the pause lifting runs it");
    }

    // ── On its own thread ────────────────────────────────────────────────────────────
    const QString threaded = tmp.filePath(QStringLiteral("Threaded"));
    QDir().mkpath(threaded);
    for (int i = 0; i < 3; ++i) stageSwings(threaded, QStringLiteral("s%1").arg(i), 8, sep1.addDays(i));
    {
        WorkOnsController t;
        t.setAthleteDir(threaded);
        QDeadlineTimer deadline(60000);
        while (t.consistencyJobs() == 0 && !deadline.hasExpired())
            QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
        t.setPaused(true);
        const int started = t.consistencyJobs();
        t.waitForIdle();
        int written = 0;
        for (int i = 0; i < 3; ++i) written += QFile::exists(tablePath(threaded, QStringLiteral("s%1").arg(i))) ? 1 : 0;
        check(started >= 1 && started < 3 && t.consistencyJobs() == started && written < 3,
              "going live mid-queue stops it: the job is cancelled and no other starts");
        t.setPaused(false);
        check(t.waitForIdle(), "…the pause lifting drains it on the job's own thread");
        written = 0;
        for (int i = 0; i < 3; ++i) written += QFile::exists(tablePath(threaded, QStringLiteral("s%1").arg(i))) ? 1 : 0;
        check(written == 3, "…and every session has its table");
    }

    // ── Things going wrong around a job ──────────────────────────────────────────────
    const QString trouble = tmp.filePath(QStringLiteral("Trouble"));
    const QString otherAthlete = tmp.filePath(QStringLiteral("TroubleOther"));
    QDir().mkpath(trouble);
    QDir().mkpath(otherAthlete);
    stageSwings(trouble, first, 6, sep1);
    stageSwings(otherAthlete, first, 6, sep1);
    {
        WorkOnsController t;
        t.setSynchronous(true);
        t.setAthleteDir(trouble);
        const QString session = QDir(trouble).filePath(first), table = tablePath(trouble, first);
        const QString shown = area(t, "setup").value(QStringLiteral("consistencyWord")).toString();

        // A share that won't take the file: the temporary's name is held by a folder.
        QFile::remove(table);
        QDir().mkpath(table + QStringLiteral(".tmp"));
        int n = t.consistencyJobs();
        t.refresh();
        check(t.consistencyJobs() == n + 1 && !QFile::exists(table) && shown != QLatin1String("-")
              && area(t, "setup").value(QStringLiteral("consistencyWord")).toString() == shown,
              "a write that fails keeps the table in memory, and the word");
        n = t.consistencyJobs();
        t.refresh();
        t.refresh();
        check(t.consistencyJobs() == n && area(t, "setup").value(QStringLiteral("consistencyWord")).toString() == shown,
              "…and a read-only share isn't derived again at every look");
        QDir().rmdir(table + QStringLiteral(".tmp"));

        // A swing that won't read: nothing stored under a fingerprint that names every swing.
        QDir().mkpath(session + QStringLiteral("/swing_0007"));
        QFile bad(session + QStringLiteral("/swing_0007/swing.json"));
        if (bad.open(QIODevice::WriteOnly)) bad.write("{ this is not a swing");
        bad.close();
        n = t.consistencyJobs();
        t.refresh();
        check(t.consistencyJobs() == n + 1 && !QFile::exists(table)
              && area(t, "setup").value(QStringLiteral("consistencyWord")).toString() == shown,
              "a partial read is not persisted, and the word stands");
        n = t.consistencyJobs();
        t.refresh();
        check(t.consistencyJobs() == n, "…nor derived again this run");
        QFile::remove(bad.fileName());
        QDir(session + QStringLiteral("/swing_0007")).removeRecursively();
        t.refresh();
        check(QFile::exists(table) && jsonOf(table).value(QStringLiteral("shots")).toInt() == 6,
              "…the swings right again, the table is written");

        // A corrupt file reads as no table and is derived again.
        const QByteArray good = bytesOf(table);
        QFile corrupt(table);
        if (corrupt.open(QIODevice::WriteOnly)) corrupt.write("{ not a table");
        corrupt.close();
        n = t.consistencyJobs();
        t.refresh();
        check(t.consistencyJobs() == n + 1 && bytesOf(table) == good, "a corrupt consistency.json is derived again");

        // A swing moves between the job's reading and its landing: the scan's newer fingerprint wins.
        bool once = true;
        t.setConsistencyJobHook([&](const QString &dir) {
            if (!once) return;
            once = false;
            backdate(pinpoint::SwingStore::ppswPath(dir + QStringLiteral("/swing_0002")), old);
            t.refresh();
        });
        const QString before = fingerprintOf(table);
        t.setConsistencyRuleVersion(pc::kConsistencyRuleVersion + 7);
        n = t.consistencyJobs();
        t.refresh();
        t.setConsistencyJobHook({});
        const int rounds = t.consistencyJobs() - n;
        const QString settled = fingerprintOf(table);
        t.refresh();
        check(rounds == 2 && settled != before && fingerprintOf(table) == settled && t.consistencyJobs() == n + 2,
              "a swing moving mid-job goes round again, and ends fresh");

        // The athlete changes mid-job: nothing written for the old one, the new one's tables land.
        QFile::remove(table);
        once = true;
        t.setConsistencyJobHook([&](const QString &) {
            if (!once) return;
            once = false;
            t.setAthleteDir(otherAthlete);
        });
        t.refresh();
        t.setConsistencyJobHook({});
        check(!QFile::exists(table) && QFile::exists(tablePath(otherAthlete, first)) && t.waitForIdle(1000)
              && area(t, "setup").value(QStringLiteral("consistencyWord")).toString() != QLatin1String("-"),
              "the athlete changing mid-job writes nothing for the old one, and the new one's words land");

        // The session is trashed mid-job: no orphan folder holding a table.
        t.setAthleteDir(trouble);
        t.setConsistencyRuleVersion(pc::kConsistencyRuleVersion);
        t.setConsistencyJobHook([](const QString &dir) { QDir(dir).removeRecursively(); });
        t.refresh();
        t.setConsistencyJobHook({});
        check(!QDir(session).exists(), "a session trashed mid-job is not raised again by writing its table");
        t.refresh();
        check(t.waitForIdle(1000), "…and the queue carries on");
    }

    // ── A document gone between the scan and the job ─────────────────────────────────
    // As a share dropping shows it: the stat fails, so the read is never tried. The newer session's job
    // takes the older one's document away after the scan has counted it.
    const QString shortfall = tmp.filePath(QStringLiteral("Shortfall"));
    QDir().mkpath(shortfall);
    stageSwings(shortfall, QStringLiteral("newer"), 6, sep2);
    stageSwings(shortfall, QStringLiteral("older"), 6, sep1);
    {
        WorkOnsController u;
        u.setSynchronous(true);
        u.setAthleteDir(shortfall);
        const QString older = QDir(shortfall).filePath(QStringLiteral("older")), table = tablePath(shortfall, QStringLiteral("older"));
        const QString doc = pinpoint::SwingStore::ppswPath(older + QStringLiteral("/swing_0003"));
        const QByteArray bytes = bytesOf(doc);
        const QDateTime stamp = mtimeOf(doc);
        const QByteArray before = bytesOf(table);
        const QString word = area(u, "setup").value(QStringLiteral("consistencyWord")).toString();

        bool once = true;
        u.setConsistencyJobHook([&](const QString &) {
            if (!once) return;
            once = false;
            QFile::remove(doc);
        });
        u.setConsistencyRuleVersion(pc::kConsistencyRuleVersion + 1);
        const int n = u.consistencyJobs();
        u.refresh();
        u.setConsistencyJobHook({});
        check(u.consistencyJobs() == n + 2 && bytesOf(table) == before
              && area(u, "setup").value(QStringLiteral("consistencyWord")).toString() == word,
              "a document gone between the scan and the job: nothing stored under the full fingerprint, the word stands");

        // A blip: the document back as it was, so the same fingerprint. Nothing stale was ever written over it.
        QFile back(doc);
        if (back.open(QIODevice::WriteOnly)) back.write(bytes);
        back.close();
        backdate(doc, stamp);
        const int m = u.consistencyJobs();
        u.refresh();
        check(u.consistencyJobs() == m, "…the document back, the session waits for the next launch rather than re-derive");

        // Really gone: the scan counts five, the fingerprint moves, and the table is stored as five.
        QFile::remove(doc);
        u.refresh();
        check(jsonOf(table).value(QStringLiteral("shots")).toInt() == 5, "…really gone, the scan's own count stores the table");
    }

    // ── Queue order ──────────────────────────────────────────────────────────────────
    // Played order against folder mtime: the oldest-played folder is the last one touched.
    const QString ordered = tmp.filePath(QStringLiteral("Ordered"));
    QDir().mkpath(ordered);
    stageSwings(ordered, QStringLiteral("a_played_third"), 6, sep1.addDays(2));
    stageSwings(ordered, QStringLiteral("b_played_second"), 6, sep1.addDays(1));
    stageSwings(ordered, QStringLiteral("c_played_first"), 6, sep1);
    {
        WorkOnsController q;
        q.setSynchronous(true);
        q.setAthleteDir(ordered);
        for (const char *name : { "a_played_third", "b_played_second", "c_played_first" }) {
            QFile touch(QDir(ordered).filePath(QLatin1String(name)) + QStringLiteral("/touch"));
            if (touch.open(QIODevice::WriteOnly)) touch.close();
            QFile::remove(touch.fileName());
            QThread::msleep(20);
        }
        QStringList order;
        q.setConsistencyJobHook([&order](const QString &dir) { order << QFileInfo(dir).fileName(); });
        q.setConsistencyRuleVersion(pc::kConsistencyRuleVersion + 1);
        q.refresh();
        check(order == QStringList{ "a_played_third", "b_played_second", "c_played_first" },
              "the catch-up runs newest played first, whatever the folders' mtimes say");
    }

    // ── The word Home publishes ──────────────────────────────────────────────────────
    // Dates against names, so a sort by name would read them backwards. The middle session judged nothing.
    const QString words = tmp.filePath(QStringLiteral("Words"));
    QDir().mkpath(words);
    stageSwings(words, QStringLiteral("a_newest"), 6, sep1.addDays(2));
    stageSwings(words, QStringLiteral("b_nothing"), 6, sep1.addDays(1));
    stageSwings(words, QStringLiteral("c_oldest"), 6, sep1);
    stage(words, QStringLiteral("d_no_table"), sep1.toMSecsSinceEpoch(), { { "pull", 1 } });  // a ledger, no swings: no table, and a ball-flight row
    {
        WorkOnsController w;
        w.setSynchronous(true);
        w.setAthleteDir(words);
        setWords(tablePath(words, QStringLiteral("a_newest")),
                 { { "setup|0", "steady" }, { "impact|6", "loose" }, { "finish|7", "steady" },
                   { "release|5", "loose" }, { "release|6", "steady" }, { "release|7", "loose" } }, QStringLiteral("dash"));
        setWords(tablePath(words, QStringLiteral("b_nothing")), {}, QStringLiteral("dash"));
        setWords(tablePath(words, QStringLiteral("c_oldest")),
                 { { "setup|0", "some" }, { "impact|6", "steady" }, { "finish|7", "nearError" },
                   { "release|5", "loose" }, { "release|6", "steady" }, { "release|7", "loose" } }, QStringLiteral("dash"));
        int jobs2 = w.consistencyJobs();
        int areas = 0;
        QObject::connect(&w, &WorkOnsController::areasChanged, [&areas] { ++areas; });
        w.refresh();
        check(w.consistencyJobs() == jobs2 && areas > 0, "tables read by the scan reach areaItems, with no job");
        // 0 against ½ at a fade of .75: .214. Were the empty session to age the older by another .75, .18: Steady.
        check(areaSays(w, "setup", "Some", "P1", { QStringLiteral("P1 Some") }),
              "the sessions that can judge it: a session that judged nothing takes no place in the fade");
        check(areaSays(w, "impact", "Loose", "P6-P7", { QStringLiteral("P6-P7 Loose") }),
              "newest first by when it was played, not by name: 1 then 0 is .57");
        check(areaSays(w, "release", "Loose", "P5-P6", { QStringLiteral("P5-P6 Loose"), QStringLiteral("P6-P7 Steady") }),
              "P7-P8 counts only for Finish: release is loose, steady, so .5, and no third span");
        check(areaSays(w, "finish", "Steady", "", { QStringLiteral("P7-P8 Steady") }),
              "Finish reads P7-P8; a ⚠ scores as steady; steady names no span");
        check(areaSays(w, "sequence", "-", "", {}), "an area nothing judged says -");
        check(!area(w, "ballFlight").isEmpty() && areaSays(w, "ballFlight", "-", "", {}), "ball flight says -: it has no spans");

        // A table lands: once for the scan's publish, once more with no scan when the job delivers.
        areas = 0;
        QFile::remove(tablePath(words, QStringLiteral("c_oldest")));
        w.refresh();
        check(areas == 2 && area(w, "sequence").value(QStringLiteral("consistencyWord")).toString() != QLatin1String("-"),
              "a table landing moves the words by itself");

        // Every table real now. The same tables again under a new rule: no word moves, nothing rebuilds Home's rows.
        w.setConsistencyRuleVersion(pc::kConsistencyRuleVersion + 1);
        w.refresh();
        areas = 0;
        w.setConsistencyRuleVersion(pc::kConsistencyRuleVersion);
        w.refresh();
        check(areas == 1, "a landing that moves no word announces nothing: only the scan's publish");
    }

    // A stale table's word stands until the new one lands, read from the file on a fresh start.
    {
        for (const char *name : { "a_newest", "b_nothing", "c_oldest" })
            setWords(tablePath(words, QLatin1String(name)), {}, QStringLiteral("loose"));
        WorkOnsController s;
        s.setSynchronous(true);
        s.setConsistencyRuleVersion(pc::kConsistencyRuleVersion + 2);
        s.setPaused(true);
        s.setAthleteDir(words);
        check(s.consistencyJobs() == 0 && area(s, "setup").value(QStringLiteral("consistencyWord")).toString() == QLatin1String("Loose"),
              "a stale table's word shows until it is replaced, never a dash");
        s.setPaused(false);
        check(s.consistencyJobs() == 4 && QFile::exists(tablePath(words, QStringLiteral("a_newest")))
              && jsonOf(tablePath(words, QStringLiteral("a_newest"))).value(QStringLiteral("derivedFrom")).toObject()
                     .value(QStringLiteral("ruleVersion")).toInt() == pc::kConsistencyRuleVersion + 2,
              "…and the replacement lands");
    }

    // Only a verdict takes the words away: every swing readable, and none of them analysed.
    const QString verdict = tmp.filePath(QStringLiteral("Verdict"));
    QDir().mkpath(verdict);
    stageSwings(verdict, first, 6, sep1);
    {
        WorkOnsController v;
        v.setSynchronous(true);
        v.setAthleteDir(verdict);
        check(area(v, "setup").value(QStringLiteral("consistencyWord")).toString() != QLatin1String("-"), "a session has its word");
        for (int i = 1; i <= 6; ++i)
            QFile::remove(pinpoint::SwingStore::ppswPath(QDir(verdict).filePath(first) + QStringLiteral("/swing_%1").arg(i, 4, 10, QLatin1Char('0'))));
        v.refresh();
        check(area(v, "setup").value(QStringLiteral("consistencyWord")).toString() == QLatin1String("-"),
              "…a no-table verdict clears it, stale file or not");
        const int n = v.consistencyJobs();
        v.refresh();
        check(v.consistencyJobs() == n && area(v, "setup").value(QStringLiteral("consistencyWord")).toString() == QLatin1String("-"),
              "…and it stays cleared");
    }

    // A catch-up's words on the real thread: landings coalesced, the last one kept.
    const QString coalesced = tmp.filePath(QStringLiteral("Coalesced"));
    QDir().mkpath(coalesced);
    for (int i = 0; i < 3; ++i) stageSwings(coalesced, QStringLiteral("s%1").arg(i), 8, sep1.addDays(i));
    {
        WorkOnsController a;
        a.setAthleteDir(coalesced);
        a.waitForIdle();
        const QString real = area(a, "setup").value(QStringLiteral("consistencyWord")).toString();
        for (int i = 0; i < 3; ++i)
            setWords(tablePath(coalesced, QStringLiteral("s%1").arg(i)),
                     { { "setup|0", real == QLatin1String("Loose") ? "steady" : "loose" } }, QStringLiteral("dash"));
        a.refresh();
        a.waitForIdle();                    // the planted words are showing
        const QString planted = area(a, "setup").value(QStringLiteral("consistencyWord")).toString();
        int areas = 0;
        QObject::connect(&a, &WorkOnsController::areasChanged, [&areas] { ++areas; });
        QElapsedTimer t;
        t.start();
        a.setConsistencyRuleVersion(pc::kConsistencyRuleVersion + 3);
        a.refresh();
        a.waitForIdle();
        const int allowed = 1 + 1 + int(t.elapsed() / 3000);    // the scan's, and one per 3 s of landings
        check(planted != real && area(a, "setup").value(QStringLiteral("consistencyWord")).toString() == real && areas <= allowed,
              "three landings that change words give one area update, and the last word is in it");
        std::printf("      %d updates in %d ms\n", areas, int(t.elapsed()));
    }

    // ── What it costs ────────────────────────────────────────────────────────────────
    const QString cost = tmp.filePath(QStringLiteral("Cost"));
    QDir().mkpath(cost);
    const int shots = 30;
    stageSwings(cost, QStringLiteral("s"), shots, sep1);
    {
        pc::ConsistencySummary out;
        QElapsedTimer t;
        t.start();
        const bool ok = pc::deriveConsistencySummary(QDir(cost).filePath(QStringLiteral("s")), shots, env, nullptr, &out);
        const double ms = double(t.nsecsElapsed()) / 1e6;
        check(ok && out.shots == shots, "a session of 30 is derived");
        std::printf("      %.1f ms for %d shots: %.2f ms a swing (%d metrics each, this build unoptimised)\n", ms, shots, ms / shots,
                    int(richSwing().value(QStringLiteral("analysis")).toObject().value(QStringLiteral("metrics")).toArray().size()));
    }
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
    int areasSignals = 0;
    QObject::connect(&c, &WorkOnsController::areasChanged, [&areasSignals] { ++areasSignals; });
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
        // position is a setup condition and the pull an outcome: neither is a fault). Since the
        // session report's content (2026-10-20) every listable fault carries a drill — this one
        // "Club across the chest" — so the focus is titled by that drill, aims for the fault's ideal,
        // and names what to practise, with nothing left for next.
        const QVariantMap focus = c.focusItem();
        const QVariantList rightNow = focus.value(QStringLiteral("rightNow")).toList();
        const QVariantMap spine = rightNow.value(0).toMap();
        check(rightNow.size() == 1 && c.nextItems().isEmpty()
              && spine.value(QStringLiteral("sessions")).toList() == QVariantList{ true, true }
              && spine.value(QStringLiteral("frequency")).toString() == QStringLiteral("every swing"),
              "the fault on every swing of both sessions is the focus: 'every swing', a filled pip for each");
        check(!focus.value(QStringLiteral("practise")).toString().isEmpty()
              && focus.value(QStringLiteral("practiseLabel")).toString() == QStringLiteral("Club across the chest")
              && focus.value(QStringLiteral("title")).toString() == QStringLiteral("Club across the chest")
              && !focus.value(QStringLiteral("aimFor")).toList().value(0).toString().isEmpty(),
              "…a fault with its drill: titled by the drill, its ideal as what to aim for, the drill to practise");
        // The focus names its condition, and conditionLabel() names it as the list does.
        const QString focusId = focus.value(QStringLiteral("focusConditionId")).toString();
        check(focusId == QStringLiteral("reverse_spine_p4")
              && c.conditionLabel(focusId) == item(c, "reverse_spine_p4").value(QStringLiteral("name")).toString()
              && !c.conditionLabel(focusId).isEmpty() && c.conditionLabel(focusId) != focusId,
              "focusConditionId is the focus fault's condition, and conditionLabel() gives the name the list shows");
        check(c.conditionLabel(QStringLiteral("no_such_condition")) == QStringLiteral("no_such_condition")
              && c.conditionLabel(QString()).isEmpty(),
              "conditionLabel() falls back to the id for a condition the pack does not know, \"\" for \"\"");
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
    {
        // Every work-on names its swing area by the areas' own token (Home places it in its area by it).
        QStringList areaGroups;
        for (const QVariant &v : c.areaItems()) areaGroups << v.toMap().value(QStringLiteral("group")).toString();
        bool grouped = true;
        for (const QVariant &v : c.items()) {
            const QVariantMap m = v.toMap();
            const QString g = m.value(QStringLiteral("group")).toString();
            if (!m.contains(QStringLiteral("group")) || g.isEmpty() || !areaGroups.contains(g)) grouped = false;
        }
        check(grouped, "every work-on carries its area's group token");
    }

    // ── The areas ───────────────────────────────────────────────────────────────
    //
    // s1: the reverse spine, sway and the pull fire on all ten; s2: the spine and the ball position
    // fire, sway is quiet. The pull reads only a launch-monitor key, so ball flight is shown.
    checkAreasShape(c, "two small sessions");
    check(areasSignals > 0, "publishing the list announces the areas (areasChanged)");
    {
        const int before = areasSignals;
        c.refresh();
        check(areasSignals > before, "…and every publish does, a refresh with nothing new included");
    }
    check(areaGroups(c) == QStringList{ "setup", "posture", "ballFlight", "lateral",
                                        "sequence", "armsAndClub", "release", "impact", "finish" },
          "area order: one habit each, tied on share, address to finish; then the occasional; then the unmeasured "
          "in address-to-finish order");
    {
        const QVariantMap posture = area(c, "posture"), lateral = area(c, "lateral"), setup = area(c, "setup");
        check(lateral.value(QStringLiteral("label")).toString() == QStringLiteral("Lateral motion")
              && posture.value(QStringLiteral("label")).toString() == QStringLiteral("Posture"),
              "area labels are conditionGroupLabel's: 'Lateral motion', 'Posture'");
        check(posture.value(QStringLiteral("habits")).toInt() == 1 && posture.value(QStringLiteral("measured")).toInt() == 1
              && posture.value(QStringLiteral("topFaultId")).toString() == QStringLiteral("reverse_spine_p4")
              && posture.value(QStringLiteral("topFault")).toString() == QStringLiteral("Reverse spine angle at P4"),
              "posture: one habit, its top fault named by the pack label");
        check(lateral.value(QStringLiteral("habits")).toInt() == 0 && lateral.value(QStringLiteral("occasional")).toInt() == 1,
              "lateral: sway on half the swings, the older half faded, is occasional");
        check(posture.value(QStringLiteral("hasTrend")).toBool() && posture.value(QStringLiteral("habitTrend")).toInt() == 0
              && setup.value(QStringLiteral("habitTrend")).toInt() == 1 && lateral.value(QStringLiteral("habitTrend")).toInt() == -1,
              "trend against the counts without s2: posture steady, the ball position a new habit, sway cleared as one");
        check(!area(c, "sequence").value(QStringLiteral("measurable")).toBool()
              && area(c, "sequence").value(QStringLiteral("items")).toList().isEmpty()
              && !area(c, "sequence").value(QStringLiteral("hasTrend")).toBool(),
              "an area nothing assessed is unmeasurable, with no items and no trend");

        const QVariantMap spineItem = areaItem(posture, "reverse_spine_p4"), swayItem = areaItem(lateral, "sway"),
                          ballItem = areaItem(setup, "ball_back");
        check(spineItem.value(QStringLiteral("band")).toString() == QStringLiteral("habit")
              && spineItem.value(QStringLiteral("fired")).toInt() == 20 && spineItem.value(QStringLiteral("assessable")).toInt() == 20
              && spineItem.value(QStringLiteral("workOn")).toString() == QStringLiteral("active")
              && spineItem.value(QStringLiteral("sessionsListed")).toInt() == 2,
              "the spine: a habit, 20 of 20 swings, an active work-on listed twice");
        check(spineItem.value(QStringLiteral("hasTypical")).toBool() && spineItem.value(QStringLiteral("typical")).toDouble() == 6.0
              && spineItem.value(QStringLiteral("corridorShape")).toString() == QStringLiteral("twoSided")
              && spineItem.value(QStringLiteral("grade")).toString() == QStringLiteral("watch"),
              "…typically 6 against a two-sided −2..2 band: ON the fault line (3σ) grades watch, as grade() does");
        check(swayItem.value(QStringLiteral("band")).toString() == QStringLiteral("occasional")
              && swayItem.value(QStringLiteral("typical")).toDouble() == 1.0
              && swayItem.value(QStringLiteral("grade")).toString() == QStringLiteral("ideal")
              && swayItem.value(QStringLiteral("workOn")).toString() == QStringLiteral("easing"),
              "sway: its newest session's readings, all of them (D9), are inside the band — ideal; the work-on easing");
        check(ballItem.value(QStringLiteral("workOn")).toString() == QStringLiteral("new")
              && setup.value(QStringLiteral("workOnsNew")).toInt() == 1 && setup.value(QStringLiteral("workOnsActive")).toInt() == 1,
              "the ball position, listed only in the newest session, is a new work-on");
    }

    // ── The newest session ──────────────────────────────────────────────────────
    {
        const QVariantMap last = c.lastSession();
        const QDate d = QDateTime::fromMSecsSinceEpoch(t0 + day).date();
        const QString want = QLocale::c().toString(d, d.year() == QDate::currentDate().year()
                                                          ? QStringLiteral("ddd d MMM") : QStringLiteral("ddd d MMM yyyy"));
        check(keysAre(last, { QStringLiteral("label"), QStringLiteral("shots") })
              && last.value(QStringLiteral("label")).toString() == want && last.value(QStringLiteral("shots")).toInt() == 10,
              "lastSession is {label, shots}: s2's day with its weekday ('Mon 26 Oct'), and its 10 shots");
    }

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
          && !item(c, "reverse_spine_p4").value(QStringLiteral("statusText")).toString().startsWith(QStringLiteral("Not measured since")),
          "a fault the newest session could not measure says so, and stays");

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
    check(c.areaItems().isEmpty() && c.lastSession().isEmpty(),
          "…and so do the areas and the newest session: no rows for a library with nothing in it");
    c.setAthleteDir(golfer);
    check(c.items().size() == 2 && c.sessionCount() == 4, "…and coming back reads the records without deriving");
    checkAreasShape(c, "four sessions, back again");
    check(areaItem(area(c, "lateral"), "sway").value(QStringLiteral("sessionsListed")).toInt() == 1
          && areaItem(area(c, "lateral"), "sway").value(QStringLiteral("workOn")).toString().isEmpty(),
          "sway, cleared off the list, is still listed once but is no longer a work-on");
    check(c.lastSession().value(QStringLiteral("shots")).toInt() == 10, "…and the newest session is s4's again");

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
        check(focus.value(QStringLiteral("reasonShort")).toString() == QStringLiteral("comes earliest in your swing"),
              "…and its short reason is that clause alone");
        checkAreasShape(t, "three sessions, threaded");
        check(area(t, "lateral").value(QStringLiteral("measurable")).toBool()
              && !areaItem(area(t, "lateral"), "sway").isEmpty(),
              "the threaded catch-up publishes the areas too: sway is in lateral motion");
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

    // ══ One measure, two conditions: each graded on its own tail (#112) ══════════════════
    //
    // cupped_at_top (low) and bowed_at_top (high) read the same P4 wrist angle. A wrist cupped to
    // −18° against a −6..16 corridor fires cupped on every swing and bowed on none; the bowed row
    // must not borrow the cup's grade.
    {
        const QString wrist = tmp.filePath(QStringLiteral("Wrist"));
        const QString dir = QDir(wrist).filePath(QStringLiteral("s1"));
        std::vector<ShotRecord> shots;
        for (int i = 0; i < 6; ++i) {
            QDir().mkpath(dir + QStringLiteral("/swing_%1").arg(i + 1, 4, 10, QLatin1Char('0')));
            ShotRecord s;
            s.shotId = i + 1;
            s.club = QStringLiteral("7I");
            s.timestampMs = t0 + i * 45'000LL;
            for (const char *id : { "cupped_at_top", "bowed_at_top" }) {
                ConditionRow r;
                r.conditionId = QString::fromLatin1(id);
                r.drivingMeasureId = QStringLiteral("m_leadWristFlexExt_p4");
                r.value = -18.0;
                r.corridorLo = -6.0; r.corridorHi = 16.0;
                r.corridorShape = CorridorShape::TwoSided;
                const bool cupped = r.conditionId == QStringLiteral("cupped_at_top");
                r.state = cupped ? ShotState::Fired : ShotState::Clean;
                r.z = cupped ? -2.1 : 0.0;
                r.direction = cupped ? -1 : 0;
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
        f.close();

        WorkOnsController w;
        w.setSynchronous(true);
        w.setAthleteDir(wrist);
        const QVariantMap arms = area(w, "armsAndClub");
        const QVariantMap cup = areaItem(arms, "cupped_at_top"), bow = areaItem(arms, "bowed_at_top");
        check(cup.value(QStringLiteral("tail")).toString() == QStringLiteral("low")
              && bow.value(QStringLiteral("tail")).toString() == QStringLiteral("high"),
              "cupped at the top watches the low tail of the P4 wrist angle, bowed at the top the high");
        check(cup.value(QStringLiteral("hasTypical")).toBool() && bow.value(QStringLiteral("hasTypical")).toBool()
              && cup.value(QStringLiteral("typical")).toDouble() == -18.0 && bow.value(QStringLiteral("typical")).toDouble() == -18.0,
              "…both typically −18°");
        check(!cup.value(QStringLiteral("grade")).toString().isEmpty()
              && cup.value(QStringLiteral("grade")).toString() != QStringLiteral("ideal")
              && bow.value(QStringLiteral("grade")).toString() == QStringLiteral("ideal"),
              "…a cupped wrist grades the cup and leaves bowed at the top ideal, never 'watch' on both");
    }

    // ── Which shipped conditions are yes/no: every detecting signal an order test or a threshold ─────
    {
        QFile pf(qEnvironmentVariable("PINPOINT_CORE_PACK"));
        const PackLoadResult pr = pf.open(QIODevice::ReadOnly) ? loadPack(pf.readAll(), QStringLiteral("core.json"))
                                                               : PackLoadResult{};
        check(pr.parsed, "the shipped core.json loads");
        const QHash<QString, AreaConditionInfo> info = areaConditionInfoForPack(pr.pack);
        const QStringList yesNo{ "sequence_order", "arms_before_chest", "club_before_arms", "upper_body_starts_down",
                                 "hips_peak_after_impact", "chest_peak_after_impact", "arms_peak_after_impact",
                                 "club_peak_after_impact" };
        bool allYesNo = true;
        for (const QString &id : yesNo) allYesNo = allYesNo && info.contains(id) && info.value(id).binary;
        check(allYesNo, "the kinematic sequence's order tests and the four peaks after impact are yes/no");
        check(info.contains(QStringLiteral("transition_rush")) && !info.value(QStringLiteral("transition_rush")).binary,
              "a corridor condition (rushed transition) is not");
        check(info.contains(QStringLiteral("arms_ride_the_turn")) && !info.value(QStringLiteral("arms_ride_the_turn")).binary,
              "nor is a ratio (arms ride the turn): a gain is a matter of degree");
    }

    checkConsistency();

    std::printf(g_fail ? "FAILED (%d)\n" : "OK\n", g_fail);
    return g_fail ? 1 : 0;
}
