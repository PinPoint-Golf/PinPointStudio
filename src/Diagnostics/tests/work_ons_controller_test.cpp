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
// And Home's consistency marks (R5, R10): the Consistency column grades the measure behind each area's
// biggest fault over its last 20 readings across the ledgers, newest first, centred per
// club; a yes/no condition is ungraded; under 8 readings is grade 0; an unchanged library reads no
// ledger, and a changed ledger is graded again. variability_grader_test proves the grade.
//
// And Home's club scope and shot filter (club_scope.h, outcome_scope.h): the tabs reduce from the chosen shots, each
// choice is stored in its own file and read back, and under a filter (strike, direction, flight) each fault is set
// against the other shots.
//
// The other sessions are LEDGERS ON DISK with empty swing folders beside them: the model loads the
// rows, finds nothing to back-fill, and no swing document is ever read — the catch-up path for
// a library that already has its ledgers, which is the one the app meets first.

#include "Gui/diagnostics/work_ons_controller.h"
#include "Gui/variability/variability_measures.h"
#include "Coaching/frequency_words.h"
#include "Coaching/swing_themes.h"
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

// The words a stored needs-work row is said in (themeFrequency of the row: its weighted share's, or
// its latest session's when only that session listed it — Stage 0.14). "" when no row has the id.
static QString storedFrequency(const QJsonArray &storedSeen, const QString &conditionId)
{
    for (const QJsonValue &v : storedSeen) {
        const QJsonObject o = v.toObject();
        if (o.value(QStringLiteral("id")).toString() != conditionId) continue;
        SeenMostRow r;
        r.share       = o.value(QStringLiteral("share")).toDouble();
        r.latestShare = o.value(QStringLiteral("latestShare")).toDouble();
        return themeFrequency(r);
    }
    return QString();
}

// A fault item — the focus's "right now" and "next on your list" alike: exactly its five keys, the
// values in range, and its share and pips a stored needs-work row's (one pip per judged session).
static bool isFaultItem(const QVariant &v, const QJsonArray &storedSeen)
{
    const QVariantMap m = v.toMap();
    const QString freq = m.value(QStringLiteral("frequency")).toString();
    // The band's words, alone or said as the latest session's (a row only that session listed).
    QString band = freq;
    if (band.endsWith(themeLatestSessionWords())) band.chop(themeLatestSessionWords().size());
    const int trend = m.value(QStringLiteral("trend")).toInt();
    return keysAre(m, { QStringLiteral("text"), QStringLiteral("share"), QStringLiteral("frequency"),
                        QStringLiteral("trend"), QStringLiteral("sessions"), QStringLiteral("conditionId"),
                        QStringLiteral("onTheBall"), QStringLiteral("outcomeContrast") })
        && !m.value(QStringLiteral("text")).toString().isEmpty()
        && m.value(QStringLiteral("text")).toString().at(0).isUpper()
        && m.value(QStringLiteral("share")).typeId() == QMetaType::Double
        && m.value(QStringLiteral("trend")).typeId() == QMetaType::Int && trend >= -1 && trend <= 1
        && (band == QStringLiteral("every swing") || band == QStringLiteral("almost every swing")
            || band == QStringLiteral("most swings") || band == QStringLiteral("more than half your swings")
            || band == QStringLiteral("fairly often") || band == QStringLiteral("sometimes") || band == QStringLiteral("rarely"))
        // the one rule (frequency_words.h), on the evidence that listed the row (seenMostBasis)
        && freq == storedFrequency(storedSeen, m.value(QStringLiteral("conditionId")).toString())
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
                         QStringLiteral("focusConditionId"), QStringLiteral("onTheBall"), QStringLiteral("outcomeContrast") })
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
                                            QStringLiteral("startStop"), QStringLiteral("startWords"), QStringLiteral("trend"),
                                            QStringLiteral("also"), QStringLiteral("faults") })
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
                                       "practise, reason, reasonShort, focusConditionId, onTheBall, outcomeContrast}, each 'right now' a fault item a "
                                       "stored row backs, and a present focus has a short reason", when);
    check(focus, label);
    std::snprintf(label, sizeof label, "%s: focusConditionId is the first 'right now' fault's stored row, in a stored "
                                       "focus group, with a label (\"\" when nothing is right now)", when);
    check(focusIdOk, label);
    std::snprintf(label, sizeof label, "%s: 'next on your list' is at most four fault items a stored row backs, "
                                       "none the focus already tells", when);
    check(next, label);
    std::snprintf(label, sizeof label, "%s: every 'goes together' item is {tier, first, second, startStop, startWords, trend, also, faults}", when);
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
                                            QStringLiteral("consistency") });
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
                                                 QStringLiteral("workOn"), QStringLiteral("sessionsListed"),
                                                 QStringLiteral("consistency") });
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
                                       "unclear, habitTrend, hasTrend, topFault, topFaultId, workOnsActive, workOnsNew, items, consistency}", when);
    check(areaKeys, label);
    std::snprintf(label, sizeof label, "%s: every item is {conditionId, label, golfer, band, share, fired, assessable, "
                                       "measureId, hasTypical, typical, corridorLo, corridorHi, corridorShape, tail, unit, grade, "
                                       "workOn, sessionsListed, consistency}", when);
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

// ── Consistency marks ────────────────────────────────────────────────────────────────────
//
// Home's Consistency column and the badges beside its faults (R5, R10): the measure behind each area's
// biggest fault, graded over that fault's last 20 readings across the ledgers, newest first, unassessed
// rows left out — data-warning shots whose rows the ledger kept are IN (R13; the 5 Oct library
// session is 29 data-warning shots of 31, with dozens of assessed rows) — centred per club, against the
// norm corridor half-width.

namespace pv = pinpoint::variability;

// One reading on one shot: the condition, its measure, the value; NotAssessable when !assessed.
struct MarkRow { const char *condition; const char *measure; double value; bool assessed; };
struct MarkShot { QString club; std::vector<MarkRow> rows; bool dataWarning = false; };

static void stageMarks(const QString &athleteDir, const QString &session, qint64 t0, const std::vector<MarkShot> &plan)
{
    const QString dir = QDir(athleteDir).filePath(session);
    std::vector<ShotRecord> shots;
    for (int i = 0; i < int(plan.size()); ++i) {
        QDir().mkpath(dir + QStringLiteral("/swing_%1").arg(i + 1, 4, 10, QLatin1Char('0')));
        ShotRecord s;
        s.shotId = i + 1;
        s.club = plan[size_t(i)].club;
        s.dataWarning = plan[size_t(i)].dataWarning;
        s.timestampMs = t0 + i * 45'000LL;
        for (const MarkRow &m : plan[size_t(i)].rows) {
            ConditionRow r;
            r.conditionId = QString::fromLatin1(m.condition);
            r.drivingMeasureId = QString::fromLatin1(m.measure);
            r.value = m.value;
            r.corridorLo = -2.0; r.corridorHi = 2.0;
            r.corridorShape = CorridorShape::TwoSided;
            r.state = m.assessed ? ShotState::Fired : ShotState::NotAssessable;
            r.z = m.assessed ? 3.0 : 0.0;
            r.direction = m.assessed ? 1 : 0;
            if (!m.assessed) r.notAssessableReason = QStringLiteral("not seen");
            s.rows.push_back(r);
        }
        shots.push_back(s);
    }
    QJsonObject root;
    root[QStringLiteral("schemaVersion")] = 1;
    root[QStringLiteral("ledger")] = toJson(shots);
    QFile f(dir + QStringLiteral("/diagnostics.json"));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

static pv::VariabilityResultView markOf(const QVariant &v) { return v.value<pv::VariabilityResultView>(); }

static void checkConsistencyMarks(const QString &root)
{
    std::printf("consistency marks\n");
    {
        // The outcome rule, by measure, on the shipped pack (R4, R4b): ball flight and smash factor are ungraded,
        // club delivery is not — strike height and low point included, though outcome conditions (thin, top) read them.
        const std::unique_ptr<ICharacteristicPackProvider> prov = makeCharacteristicPackProvider();
        const CharacteristicPack &pk = prov->pack();
        bool outcomes = true, delivery = true;
        for (const char *m : { "m_carryDistance", "m_lmLaunchDirection", "m_spinAxis", "m_spinRate", "m_ballSpeed",
                               "m_smashFactor", "m_compoundMiss", "m_lmOffline", "m_lmPeakHeight" })
            if (!pk.measure(QString::fromLatin1(m)) || !pv::isOutcomeMeasure(pk.measure(QString::fromLatin1(m)))) { outcomes = false; std::printf("      not an outcome: %s\n", m); }
        for (const char *m : { "m_lmStrikeHeight", "m_strikeLocation", "m_lowPointAhead", "m_attackAngle", "m_faceToPath",
                               "m_clubPathAtImpact", "m_leadKneeDriftTop" })
            if (!pk.measure(QString::fromLatin1(m)) || pv::isOutcomeMeasure(pk.measure(QString::fromLatin1(m)))) { delivery = false; std::printf("      an outcome: %s\n", m); }
        check(outcomes && delivery, "the outcome rule by measure: ball flight and smash ungraded, club delivery eligible");
    }
    const QString golfer = QDir(root).filePath(QStringLiteral("Marks"));
    QDir().mkpath(golfer);
    const qint64 day = 86'400'000LL, t0 = 1'780'000'000'000LL;

    // The lead knee (lateral) fires on every swing: its area's biggest fault. Its measure carries a
    // two-sided norm (σ 8 each side). The head rise (posture) is assessed on five swings only. The
    // sequence order (rotation) is a yes/no condition.
    const char *knee = "lead_knee_drifts_in_at_top", *kneeM = "m_leadKneeDriftTop";
    const char *spine = "head_rise_backswing", *spineM = "m_headLiftBack";
    const char *order = "sequence_order", *orderM = "m_pelvisPeakTime";
    // A tight reading: each club about its own level (7-iron +6, driver −6), a few tenths apart. Pooled
    // raw, the two levels alone would be ρ ≈ 0.75 of the corridor; centred per club, ρ ≈ 0.03.
    const auto tight = [](int i, const QString &club) {
        static const double jitter[] = { -0.3, 0.1, 0.4, -0.2, 0.0, 0.2, -0.1, 0.3, -0.4, 0.1 };
        return (club == QLatin1String("7I") ? 6.0 : -6.0) + jitter[i % 10];
    };
    // Wild: ±60, alternating within each club (the clubs alternate shot by shot).
    const auto wild = [](int i) { return (i / 2) % 2 ? 60.0 : -60.0; };
    const auto shot = [&](const QString &club, double kneeValue, bool spineSeen) {
        return MarkShot{ club, { { knee, kneeM, kneeValue, true }, { spine, spineM, 2.0, spineSeen },
                                         { order, orderM, 1.0, true } } };
    };
    const auto clubOf = [](int i) { return i % 2 ? QStringLiteral("D") : QStringLiteral("7I"); };

    // m1, the oldest: ten wild readings. m2: six wild, then six tight. m3, the newest: fourteen, all
    // tight; four of them carry a data warning whose rows the ledger
    // kept (as the 5 Oct session's do), and a fifteenth is a broken recording (its rows NotAssessable, as
    // the ledger's integrity exclusion writes them). The newest 20 that count are m3's fourteen and m2's
    // last six: all tight. Leave the data-warning shots out and the window reaches m2's
    // wild readings: Scattered.
    std::vector<MarkShot> m1, m2, m3;
    for (int i = 0; i < 10; ++i) m1.push_back(shot(clubOf(i), wild(i), false));
    for (int i = 0; i < 12; ++i) m2.push_back(shot(clubOf(i), i < 6 ? wild(i) : tight(i, clubOf(i)), false));
    for (int i = 0; i < 14; ++i) {
        MarkShot m = shot(clubOf(i), tight(i, clubOf(i)), i >= 2 && i < 7);
        m.dataWarning = i == 3 || i == 5 || i == 8 || i == 11;
        m3.push_back(std::move(m));
    }
    {
        MarkShot broken = shot(clubOf(14), wild(14), false);
        broken.dataWarning = true;
        for (MarkRow &r : broken.rows) r.assessed = false;
        m3.push_back(std::move(broken));
    }
    stageMarks(golfer, QStringLiteral("m1"), t0, m1);
    stageMarks(golfer, QStringLiteral("m2"), t0 + day, m2);
    stageMarks(golfer, QStringLiteral("m3"), t0 + 2 * day, m3);

    WorkOnsController c;
    c.setSynchronous(true);
    c.setAthleteDir(golfer);
    check(c.waitForIdle(), "the catch-up settles");

    const QVariantMap lateral = area(c, "lateral"), posture = area(c, "posture"), rotation = area(c, "rotation");
    const pv::VariabilityResultView col = markOf(lateral.value(QStringLiteral("consistency")));
    const pv::VariabilityResultView row = markOf(areaItem(lateral, knee).value(QStringLiteral("consistency")));
    std::printf("      lateral: top %s, graded %d, grade %d, n %d, rho %.3f\n",
                qPrintable(lateral.value(QStringLiteral("topFaultId")).toString()), col.graded(), col.grade(), col.n(), col.rho());
    check(lateral.value(QStringLiteral("topFaultId")).toString() == QLatin1String(knee)
          && lateral.contains(QStringLiteral("consistency")),
          "the lateral area's biggest fault is the lead knee, and the area carries a consistency mark");
    check(col.graded() && col.grade() == 5 && col.basis() == QLatin1String("scalar") && col.domain() == QLatin1String("consistency"),
          "the column grades the biggest fault's measure: Grooved, a scalar consistency grade");
    check(col.n() == pv::kNWindow,
          "the window is the last 20 readings across sessions, newest first (m3's fourteen, m2's last six)");
    check(col.hasRho() && col.rho() < 0.25,
          "…centred per club (two clubs at their own levels), kept data-warning shots in it (R13), "
          "the broken recording not");
    check(row == col, "the opened item's own badge is the same mark");
    check(markOf(c.faultDetail(QString::fromLatin1(knee)).value(QStringLiteral("consistency"))) == col,
          "…and so is the one its detail shows beside the work-on's status (R10)");

    const pv::VariabilityResultView few = markOf(posture.value(QStringLiteral("consistency")));
    check(posture.value(QStringLiteral("topFaultId")).toString() == QLatin1String(spine)
          && few.graded() && few.grade() == 0 && few.n() < pv::kNMin && few.ungradedReason() == QLatin1String("tooFewSwings"),
          "five assessed readings, the rest unassessed: grade 0, \"Not enough swings\"");

    const pv::VariabilityResultView yesNo = markOf(rotation.value(QStringLiteral("consistency")));
    check(rotation.value(QStringLiteral("topFaultId")).toString() == QLatin1String(order) && !yesNo.graded()
          && !markOf(areaItem(rotation, order).value(QStringLiteral("consistency"))).graded(),
          "a yes/no condition (an order test) is ungraded: \"-\", no badge");

    bool everyItem = true;
    for (const QVariant &av : c.areaItems()) {
        const QVariantMap a = av.toMap();
        if (!a.value(QStringLiteral("consistency")).canConvert<pv::VariabilityResultView>()) everyItem = false;
        for (const QVariant &iv : a.value(QStringLiteral("items")).toList())
            if (!iv.toMap().value(QStringLiteral("consistency")).canConvert<pv::VariabilityResultView>()) everyItem = false;
        if (a.value(QStringLiteral("topFaultId")).toString().isEmpty() && markOf(a.value(QStringLiteral("consistency"))).graded())
            everyItem = false;
    }
    check(everyItem, "every area and every item carries a variabilityResult; no biggest fault, no grade");

    // Nothing moved: the job stats the ledgers and reads none of them.
    const int read = c.consistencyLedgersRead();
    c.refresh();
    check(c.waitForIdle() && c.consistencyLedgersRead() == read && markOf(area(c, "lateral").value(QStringLiteral("consistency"))) == col,
          "a refresh with no ledger changed reads no ledger and keeps the marks");

    // The newest session's ledger changes: its twelve readings go wild, and the column follows.
    std::vector<MarkShot> m3b;
    for (int i = 0; i < 14; ++i) m3b.push_back(shot(clubOf(i), wild(i), i >= 2 && i < 7));
    QThread::msleep(20);    // a new mtime, whatever the file system's resolution
    stageMarks(golfer, QStringLiteral("m3"), t0 + 2 * day, m3b);
    c.refresh();
    const pv::VariabilityResultView after = markOf(area(c, "lateral").value(QStringLiteral("consistency")));
    check(c.waitForIdle() && c.consistencyLedgersRead() > read && after.graded() && after.grade() == 1,
          "a changed ledger is read again: the newest readings now scatter, and the column says so");
}

// ── Tendencies and the frozen baseline (shot_outcomes_design.md §7.1, Contract A) ──────────

// A session of `shots` driver shots: every shot slices with over_the_top fired.
// Dated folders, because the baseline and the catch-up order go by the day.
static void stageTendencySession(const QString &athleteDir, const QString &session, qint64 t0, int shots,
                                 const QString &club = QStringLiteral("DRIVER"),
                                 const QJsonObject &extra = QJsonObject())
{
    const QString dir = QDir(athleteDir).filePath(session);
    std::vector<ShotRecord> out;
    for (int i = 0; i < shots; ++i) {
        QDir().mkpath(dir + QStringLiteral("/swing_%1").arg(i + 1, 4, 10, QLatin1Char('0')));
        ShotRecord s;
        s.shotId = i + 1;
        s.club   = club;
        s.contextId = QStringLiteral("driver");
        s.timestampMs = t0 + i * 45'000LL;
        s.carry  = 200.0 + double(i % 5);
        s.total  = 220.0;
        s.offline = 3.0;
        s.ballSpeed = 150.0;
        s.lmClubSpeed = 100.0;
        s.smash = 1.45;
        s.swingLength = QStringLiteral("full");
        for (const char *id : { "slice", "over_the_top" }) {
            ConditionRow r;
            r.conditionId = QString::fromLatin1(id);
            r.drivingMeasureId = QStringLiteral("m_handPathLoop");
            r.state = ShotState::Fired;
            r.value = 10.0;
            r.corridorLo = 0.0; r.corridorHi = 4.0;
            r.corridorShape = CorridorShape::Ceiling;
            r.z = 3.0;
            r.direction = 1;
            r.firedSignalCount = 1;
            s.rows.push_back(r);
        }
        out.push_back(s);
    }
    QJsonObject root = extra;
    root[QStringLiteral("schemaVersion")] = 1;
    root[QStringLiteral("ledger")] = toJson(out);
    QFile f(dir + QStringLiteral("/diagnostics.json"));
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

static QVariantMap tendencyItem(const WorkOnsController &c, const char *key)
{
    for (const QVariant &v : c.tendencyItems())
        if (v.toMap().value(QStringLiteral("key")).toString() == QLatin1String(key)) return v.toMap();
    return {};
}

static void checkTendencies(const QString &root)
{
    std::printf("\ntendencies: earned, building, corrections, the catch-up order\n");
    const qint64 day = 86'400'000LL, t0 = 1'780'000'000'000LL;

    // ── Oldest first by folder day while a baseline is missing ──
    {
        const QString golfer = root + QStringLiteral("/tOrder");
        QDir().mkpath(golfer);
        const QString a = QStringLiteral("2026-03-01_M_01"), b = QStringLiteral("2026-03-02_M_01"), d = QStringLiteral("2026-03-03_M_01");
        stageTendencySession(golfer, a, t0, 40);
        QThread::msleep(25);
        stageTendencySession(golfer, b, t0 + day, 40);
        QThread::msleep(25);
        stageTendencySession(golfer, d, t0 + 2 * day, 40);

        WorkOnsController c;
        c.setSynchronous(true);
        c.setAthleteDir(golfer);
        check(QFile::exists(golfer + QLatin1Char('/') + a + QStringLiteral("/club_baseline.json"))
                  && QFile::exists(golfer + QLatin1Char('/') + d + QStringLiteral("/club_baseline.json")),
              "every session reached has its sidecar");
        check(writtenAt(golfer, a) <= writtenAt(golfer, b) && writtenAt(golfer, b) <= writtenAt(golfer, d)
                  && writtenAt(golfer, a) < writtenAt(golfer, d),
              "a missing baseline: derived oldest first by FOLDER DAY, not by mtime");

        // And with every baseline present, the record is left alone.
        const double aWritten = writtenAt(golfer, a);
        c.refresh();
        check(writtenAt(golfer, a) == aWritten, "a session with a current record and a baseline is not derived again");
    }

    // ── Earned, building, and the correction ──
    const QString golfer = root + QStringLiteral("/tEarned");
    QDir().mkpath(golfer);
    stageTendencySession(golfer, QStringLiteral("2026-04-01_M_01"), t0, 12);
    stageTendencySession(golfer, QStringLiteral("2026-04-02_M_01"), t0 + day, 12);
    // A shorter run with an iron: it cannot earn yet.
    stageTendencySession(golfer, QStringLiteral("2026-04-03_M_01"), t0 + 2 * day, 7, QStringLiteral("7 IRON"));

    WorkOnsController c;
    c.setSynchronous(true);
    c.setAthleteDir(golfer);
    check(c.waitForIdle(1000) && c.tendenciesReady() && QFile::exists(WorkOnsController::tendenciesPath(golfer)),
          "the catch-up ends in tendencies.json");
    {
        const QVariantMap driver = tendencyItem(c, "DRIVER|full");
        const QVariantMap dir = driver.value(QStringLiteral("direction")).toMap();
        const QVariantMap strike = driver.value(QStringLiteral("strike")).toMap();
        const QVariantMap flight = driver.value(QStringLiteral("flight")).toMap();
        check(keysAre(driver, { QStringLiteral("key"), QStringLiteral("club"), QStringLiteral("clubLine"), QStringLiteral("clubGroup"),
                                QStringLiteral("groupLabel"), QStringLiteral("distance"), QStringLiteral("length"),
                                QStringLiteral("handedness"), QStringLiteral("sessions"), QStringLiteral("shots"),
                                QStringLiteral("workOnText"), QStringLiteral("strike"), QStringLiteral("direction"),
                                QStringLiteral("flight") })
                  && keysAre(dir, { QStringLiteral("factor"), QStringLiteral("value"), QStringLiteral("label"), QStringLiteral("fault"),
                                    QStringLiteral("watch"), QStringLiteral("building"), QStringLiteral("share"), QStringLiteral("frequency"),
                                    QStringLiteral("sessions"), QStringLiteral("shots"), QStringLiteral("seenWith"),
                                    QStringLiteral("seenWithId"), QStringLiteral("strength"), QStringLiteral("trend"),
                                    QStringLiteral("trendWords"), QStringLiteral("workOn"), QStringLiteral("workOnText"),
                                    QStringLiteral("mix") })
                  && keysAre(strike, dir.keys()) && keysAre(flight, dir.keys()),
              "a Tendencies row: the club and its three factors, each the same shape");
        check(driver.value(QStringLiteral("shots")).toInt() == 24
                  && dir.value(QStringLiteral("factor")).toString() == QStringLiteral("direction")
                  && !dir.value(QStringLiteral("building")).toBool() && dir.value(QStringLiteral("fault")).toBool()
                  && dir.value(QStringLiteral("value")).toString() == QStringLiteral("slice")
                  && dir.value(QStringLiteral("label")).toString() == QStringLiteral("Slice")
                  && dir.value(QStringLiteral("seenWithId")).toString() == QStringLiteral("over_the_top")
                  && dir.value(QStringLiteral("shots")).toInt() == 24,
              "the driver earned its direction: a slice, two sessions of twelve shots, every one counted");
        check(strike.value(QStringLiteral("label")).toString() == QStringLiteral("Solid") && !strike.value(QStringLiteral("fault")).toBool()
                  && strike.value(QStringLiteral("seenWithId")).toString().isEmpty() && strike.value(QStringLiteral("workOn")).toString().isEmpty(),
              "its strike is the usual value, Solid, said plainly: no fault, nothing seen with it");
        check(flight.value(QStringLiteral("label")).toString() == QStringLiteral("-") && flight.value(QStringLiteral("shots")).toInt() == 0
                  && !flight.value(QStringLiteral("fault")).toBool(),
              "its flight was never read (no landing angle, apex or launch): '-'");
        {
            const QVariantMap mix = dir.value(QStringLiteral("mix")).toMap();
            const QVariantList parts = mix.value(QStringLiteral("parts")).toList();
            check(parts.size() == 1 && parts[0].toMap().value(QStringLiteral("key")).toString() == QStringLiteral("slice")
                      && parts[0].toMap().value(QStringLiteral("kind")).toString() == QStringLiteral("main")
                      && parts[0].toMap().value(QStringLiteral("percent")).toInt() == 100
                      && mix.value(QStringLiteral("line")).toString() == QStringLiteral("Slice 100%"),
                  "the direction's mix: every shot a slice, the main part");
        }
        const QString dirStatus = dir.value(QStringLiteral("workOn")).toString();
        check(dirStatus == item(c, "over_the_top").value(QStringLiteral("status")).toString()
                  && driver.value(QStringLiteral("workOnText")).toString()
                         == (dirStatus.isEmpty() ? QString() : dirStatus.left(1).toUpper() + dirStatus.mid(1))
                  && dir.value(QStringLiteral("workOnText")).toString() == driver.value(QStringLiteral("workOnText")).toString(),
              "the fault's work-on status, and the row says it in words");
        std::printf("    (driver work-on: \"%s\")\n", qPrintable(driver.value(QStringLiteral("workOnText")).toString()));
        const QVariantMap iron = tendencyItem(c, "7 IRON|full");
        check(iron.value(QStringLiteral("direction")).toMap().value(QStringLiteral("building")).toBool()
                  && iron.value(QStringLiteral("sessions")).toInt() == 1,
              "one short iron session is still building");
        check(c.tendencyNote() == QStringLiteral("From 31 shots over 3 sessions"),
              "the evidence line counts what was counted");
        check(c.clubDistance(QStringLiteral("driver")).value(QStringLiteral("earned")).toBool()
                  && c.clubDistance(QStringLiteral("DRIVER")).value(QStringLiteral("n")).toInt() == 24,
              "the per-club distance is there, by the canonical club name");
        const QVariantMap detail = c.tendencyDetail(QStringLiteral("DRIVER|full"));
        const QVariantMap dirDetail = detail.value(QStringLiteral("direction")).toMap();
        check(keysAre(detail, { QStringLiteral("strike"), QStringLiteral("direction"), QStringLiteral("flight"),
                                QStringLiteral("together") })
                  && keysAre(detail.value(QStringLiteral("together")).toMap(),
                             { QStringLiteral("pairs"), QStringLiteral("combos"), QStringLiteral("moreCombos") })
                  && keysAre(dirDetail, { QStringLiteral("chain"), QStringLiteral("alsoSeen"), QStringLiteral("earlyNote"),
                                          QStringLiteral("playTo"), QStringLiteral("practiseConditionId") }),
              "a tendency's detail: one block per factor, and how they came together");
        check(!dirDetail.value(QStringLiteral("playTo")).toString().isEmpty()
                  && dirDetail.value(QStringLiteral("practiseConditionId")).toString() == QStringLiteral("over_the_top")
                  && !dirDetail.value(QStringLiteral("chain")).toList().isEmpty()
                  && !dirDetail.value(QStringLiteral("chain")).toList().first().toMap().value(QStringLiteral("text")).toString().isEmpty(),
              "the direction's: the chain, play-to and what to practise");
        check(detail.value(QStringLiteral("strike")).toMap().isEmpty() && detail.value(QStringLiteral("flight")).toMap().isEmpty(),
              "a factor with nothing to say (Solid; never read) is empty");
        check(c.tendencyDetail(QStringLiteral("PUTTER|full")).isEmpty(), "no such row: nothing");
        // The tendency flow (Sankey): every row of the group pooled, laid out, every word final. The fixture reads no
        // flight, so every shot is excluded and the flow is under its minimum: the words say so.
        const QVariantMap flow = c.tendencyFlow(QString(), 360.0, 12.0);
        check(keysAre(flow, { QStringLiteral("enough"), QStringLiteral("total"), QStringLiteral("excluded"), QStringLiteral("idealPct"),
                              QStringLiteral("micro"), QStringLiteral("headline"), QStringLiteral("subtitle"), QStringLiteral("emptyLine"),
                              QStringLiteral("excludedLine"), QStringLiteral("columns"), QStringLiteral("groups"), QStringLiteral("nodes"),
                              QStringLiteral("links") })
                  && flow.value(QStringLiteral("groups")).toList().size() == 4,
              "the tendency flow: its shape, four groups");
        check(!flow.value(QStringLiteral("enough")).toBool() && flow.value(QStringLiteral("total")).toInt() == 0
                  && flow.value(QStringLiteral("excluded")).toInt() > 0
                  && flow.value(QStringLiteral("excludedLine")).toString().contains(QStringLiteral("not shown"))
                  && flow.value(QStringLiteral("groups")).toList().first().toMap().value(QStringLiteral("name")).toString()
                         == QStringLiteral("Solid, on line, mid or high"),
              "…with no flight read every shot is excluded and said so; the ideal is named from the good values");
        std::printf("    (flow: \"%s\" · \"%s\")\n", qPrintable(flow.value(QStringLiteral("headline")).toString()),
                    qPrintable(flow.value(QStringLiteral("excludedLine")).toString()));
        const QVariantMap ironDetail = c.tendencyDetail(QStringLiteral("7 IRON|full")).value(QStringLiteral("direction")).toMap();
        check(ironDetail.value(QStringLiteral("earlyNote")).toString().startsWith(QStringLiteral("Early days"))
                  && ironDetail.value(QStringLiteral("playTo")).toString().isEmpty(),
              "a building fault's block says it is early, and no play-to yet");
        // On the ball names the row and the factor its fault is.
        const QVariantMap onBall = c.faultDetail(QStringLiteral("over_the_top")).value(QStringLiteral("onBall")).toMap();
        check(onBall.value(QStringLiteral("tendencyKey")).toString() == QStringLiteral("DRIVER|full")
                  && onBall.value(QStringLiteral("factor")).toString() == QStringLiteral("direction")
                  && onBall.value(QStringLiteral("value")).toString() == QStringLiteral("slice")
                  && onBall.value(QStringLiteral("tendencyText")).toString() == QStringLiteral("Feeds your “Driver: slice” tendency"),
              "over the top's On the ball: keyed on the driver row's direction fault");
    }

    // ── Intended shapes leave the tally; the correction re-reduces ──
    {
        const int jobs = c.tendencyJobs();
        const QString dir = QDir(golfer).filePath(QStringLiteral("2026-04-02_M_01"));
        QFile f(dir + QStringLiteral("/diagnostics.json"));
        if (f.open(QIODevice::ReadOnly)) {
            QJsonObject root_ = QJsonDocument::fromJson(f.readAll()).object();
            f.close();
            // One shot marked as an intended fade: it leaves the tally.
            root_[QStringLiteral("shotIntent")] = QJsonObject{ { QStringLiteral("10"), QStringLiteral("fade") } };
            // Every slice was really a hook, in this session.
            QJsonObject corrections;
            for (int id = 1; id <= 12; ++id)
                corrections.insert(QString::number(id),
                                   QJsonArray{ QJsonObject{ { QStringLiteral("family"), QStringLiteral("direction") },
                                                            { QStringLiteral("from"), QStringLiteral("slice") },
                                                            { QStringLiteral("to"), QStringLiteral("hook") } } });
            root_[QStringLiteral("corrections")] = corrections;
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
                f.write(QJsonDocument(root_).toJson(QJsonDocument::Compact));
            f.close();
        }
        c.refresh();
        check(c.tendencyJobs() == jobs + 1, "a changed ledger re-reduces the tendencies");
        const QVariantMap driver = tendencyItem(c, "DRIVER|full");
        const QVariantMap dirF = driver.value(QStringLiteral("direction")).toMap();
        std::printf("    (after the fade and the corrections: %d shots, direction %s %s, mix \"%s\")\n",
                    driver.value(QStringLiteral("shots")).toInt(), qPrintable(dirF.value(QStringLiteral("value")).toString()),
                    qPrintable(dirF.value(QStringLiteral("frequency")).toString()),
                    qPrintable(dirF.value(QStringLiteral("mix")).toMap().value(QStringLiteral("line")).toString()));
        check(driver.value(QStringLiteral("shots")).toInt() == 24 && !dirF.value(QStringLiteral("building")).toBool(),
              "an intended shape no longer leaves the row (a shot is judged on what the ball did); the tendency still earned");
        check(dirF.value(QStringLiteral("mix")).toMap().value(QStringLiteral("line")).toString() == QStringLiteral("Hook 50% · Slice 50%"),
              "the direction's mix: the corrected session's hooks beside the first session's slices");
        check(dirF.value(QStringLiteral("label")).toString() == QStringLiteral("Hook"),
              "a correction changes the direction's usual without re-deriving a session");
    }

    // ── Cancelled while a session is live; the pause lifting computes it ──
    {
        const QString paused = root + QStringLiteral("/tPaused");
        QDir().mkpath(paused);
        stageTendencySession(paused, QStringLiteral("2026-05-01_M_01"), t0, 12);
        WorkOnsController p;
        p.setSynchronous(true);
        p.setPaused(true);
        p.setAthleteDir(paused);
        check(!QFile::exists(WorkOnsController::tendenciesPath(paused)) && !p.tendenciesUpdating(),
              "no tendency job while a session is live");
        p.setPaused(false);
        check(p.waitForIdle(1000) && QFile::exists(WorkOnsController::tendenciesPath(paused)),
              "the pause lifting computes them");
    }
}

// ── The club scope (club_scope.h): Coaching, Your swing and Matchups on one club or group ──────────

static void checkClubScope(const QString &root)
{
    std::printf("club scope\n");
    const qint64 day = 86'400'000LL, t0 = 1'782'000'000'000LL;
    const QString golfer = root + QStringLiteral("/scope");
    QDir().mkpath(golfer);
    stageTendencySession(golfer, QStringLiteral("2026-06-01_M_01"), t0, 12);
    stageTendencySession(golfer, QStringLiteral("2026-06-02_M_01"), t0 + day, 12);
    stageTendencySession(golfer, QStringLiteral("2026-06-03_M_01"), t0 + 2 * day, 7, QStringLiteral("7 IRON"));

    WorkOnsController c;
    c.setSynchronous(true);
    c.setAthleteDir(golfer);
    check(c.waitForIdle(), "the catch-up settles");

    const QVariantList groups = c.clubScopes();
    check(groups.size() == 2 && groups[0].toMap().value(QStringLiteral("value")).toString() == QStringLiteral("group:driver")
              && groups[1].toMap().value(QStringLiteral("value")).toString() == QStringLiteral("group:mid_irons"),
          "the options are the groups this golfer has hit, in the bag's order");
    check(c.clubScope() == QStringLiteral("all") && c.summarySubtitle() == QStringLiteral("From 31 swings over 3 sessions"),
          "every club to begin with");

    const QVariantList itemsBefore = c.items(), tendenciesBefore = c.tendencyItems();
    const QVariantMap lastBefore = c.lastSession();
    int itemsSignals = 0, detailsSignals = 0;
    QObject::connect(&c, &WorkOnsController::itemsChanged, [&itemsSignals] { ++itemsSignals; });
    QObject::connect(&c, &WorkOnsController::detailsChanged, [&detailsSignals] { ++detailsSignals; });

    c.setClubScope(QStringLiteral("club:7 IRON"));
    check(c.waitForIdle(), "a club settles");
    check(c.clubScope() == QStringLiteral("club:7 IRON") && !c.clubScopeRefreshing() && !c.matchupsRefreshing()
              && !c.clubScopeWorking(), "the screens have caught up with it");
    check(c.summarySubtitle() == QStringLiteral("From 7 swings over 1 session"), "the summary reads only that club's swings");
    check(c.togetherNote().startsWith(QStringLiteral("Not yet")) && c.togetherNote().contains(QStringLiteral("so far")),
          "Matchups follows it, and says when there are too few swings");
    const QString file = WorkOnsController::themesPath(golfer, resolveClubScope(QStringLiteral("club:7 IRON"), clubScopeOptions({ "7 IRON" }, [](const QString &) { return QStringLiteral("mid_irons"); })));
    check(file.endsWith(QStringLiteral("swing_themes.club_7_iron.json")) && QFile::exists(file), "the scope is stored beside the summary, in a file of its own");
    check(itemsSignals == 0 && detailsSignals > 0, "a scoped landing rebuilds the details, and the all-club list is not touched");
    check(c.items() == itemsBefore && c.lastSession() == lastBefore, "the work-on list and the last session stay every club's");
    check(c.tendencyItems() == tendenciesBefore, "and so does the Tendencies tab");

    c.setClubScope(QStringLiteral("group:driver"));
    check(c.waitForIdle() && c.summarySubtitle() == QStringLiteral("From 24 swings over 2 sessions"), "a group reads all its clubs' swings");

    c.setClubScope(QStringLiteral("club:4 IRON"));
    check(c.waitForIdle() && c.clubScope() == QStringLiteral("all") && c.summarySubtitle() == QStringLiteral("From 31 swings over 3 sessions"),
          "a club this golfer has not hit reads as all");

    c.setClubScope(QStringLiteral("club:7 IRON"));
    c.waitForIdle();
    c.setClubScope(QStringLiteral("all"));
    check(c.waitForIdle() && c.clubScope() == QStringLiteral("all") && c.summarySubtitle() == QStringLiteral("From 31 swings over 3 sessions")
              && !c.clubScopeRefreshing() && c.items() == itemsBefore, "back to all, and everything is every club's again");

    // A stored scope is read rather than reduced: its file is not rewritten by the next golfer's look at it.
    {
        const QDateTime stamp = QFileInfo(file).lastModified();
        QThread::msleep(25);
        WorkOnsController again;
        again.setSynchronous(true);
        again.setAthleteDir(golfer);
        again.waitForIdle();
        again.setClubScope(QStringLiteral("club:7 IRON"));
        check(again.waitForIdle() && again.summarySubtitle() == QStringLiteral("From 7 swings over 1 session"), "a second look reads the same");
        check(QFileInfo(file).lastModified() == stamp, "…from the stored file, with nothing reduced");
    }

    // A session partly in scope is re-reduced from its in-scope shots; the rest of the ledger is left alone.
    {
        const QString mixed = root + QStringLiteral("/scopeMixed");
        QDir().mkpath(mixed);
        const QString name = QStringLiteral("2026-06-10_M_01");
        stageTendencySession(mixed, name, t0, 12);
        const QString path = QDir(mixed).filePath(name) + QStringLiteral("/diagnostics.json");
        QFile f(path);
        check(f.open(QIODevice::ReadOnly), "the mixed ledger is there");
        QJsonObject doc = QJsonDocument::fromJson(f.readAll()).object();
        f.close();
        QJsonObject ledger = doc.value(QStringLiteral("ledger")).toObject();
        QJsonArray shots = ledger.value(QStringLiteral("shots")).toArray();
        for (int i = 0; i < 4; ++i) {
            QJsonObject s = shots[i].toObject();
            s[QStringLiteral("club")] = QStringLiteral("7 IRON");
            shots[i] = s;
        }
        ledger[QStringLiteral("shots")] = shots;
        doc[QStringLiteral("ledger")] = ledger;
        check(f.open(QIODevice::WriteOnly | QIODevice::Truncate), "it can be rewritten");
        f.write(QJsonDocument(doc).toJson(QJsonDocument::Compact));
        f.close();

        WorkOnsController m;
        m.setSynchronous(true);
        m.setAthleteDir(mixed);
        m.waitForIdle();
        m.setClubScope(QStringLiteral("club:7 IRON"));
        check(m.waitForIdle() && m.summarySubtitle() == QStringLiteral("From 4 swings over 1 session"),
              "a mixed session counts only the scope's swings, from the first shot on");
    }

    // Asked twice in quick succession off the test seam: the last choice is what lands.
    {
        WorkOnsController a;
        a.setAthleteDir(golfer);
        check(a.waitForIdle(), "the background catch-up settles");
        a.setClubScope(QStringLiteral("group:driver"));
        a.setClubScope(QStringLiteral("club:7 IRON"));
        check(a.waitForIdle() && a.clubScope() == QStringLiteral("club:7 IRON")
                  && a.summarySubtitle() == QStringLiteral("From 7 swings over 1 session"), "a superseded choice is dropped");
    }
    check(WorkOnsController::themesPath(golfer, ClubScope()) == WorkOnsController::themesPath(golfer), "all keeps the one file it always had");

    // The consistency marks read the scope's shots: its own last readings, not the other club's.
    {
        const QString marks = root + QStringLiteral("/scopeMarks");
        QDir().mkpath(marks);
        const char *knee = "lead_knee_drifts_in_at_top", *kneeM = "m_leadKneeDriftTop";
        static const double jitter[] = { -0.3, 0.1, 0.4, -0.2, 0.0, 0.2, -0.1, 0.3, -0.4, 0.1 };
        std::vector<MarkShot> plan;
        for (int i = 0; i < 20; ++i) {
            const bool iron = i % 2 == 0;
            plan.push_back(MarkShot{ iron ? QStringLiteral("7 IRON") : QStringLiteral("DRIVER"),
                                     { { knee, kneeM, (iron ? 6.0 : -6.0) + jitter[i % 10], true } } });
        }
        stageMarks(marks, QStringLiteral("2026-06-20_M_01"), t0, plan);
        WorkOnsController m;
        m.setSynchronous(true);
        m.setAthleteDir(marks);
        check(m.waitForIdle() && markOf(area(m, "lateral").value(QStringLiteral("consistency"))).n() == 20, "every club: all twenty readings");
        m.setClubScope(QStringLiteral("club:7 IRON"));
        check(m.waitForIdle() && markOf(area(m, "lateral").value(QStringLiteral("consistency"))).n() == 10, "one club: only its own ten");
    }
}

// ── The shot filter (outcome_scope.h): Coaching, Your swing and Matchups on the shots that did one thing ──

// One shot of a session: whether it sliced, whether over_the_top fired, whether the swing was measured, and whether
// it came off the toe. early_extension fires on every shot, so it is no more common on any choice than on the rest.
// A shot whose swing was not measured (a launch-monitor-only shot) has its fault rows NotAssessable; its ball rows
// (the slice, the toe) still judge it.
struct OutcomeShot { bool slice; bool overTheTop; bool measured = true; bool toe = false; };

static void stageOutcomeSession(const QString &athleteDir, const QString &session, qint64 t0, const QString &club,
                                const std::vector<OutcomeShot> &plan)
{
    const QString dir = QDir(athleteDir).filePath(session);
    std::vector<ShotRecord> out;
    for (int i = 0; i < int(plan.size()); ++i) {
        QDir().mkpath(dir + QStringLiteral("/swing_%1").arg(i + 1, 4, 10, QLatin1Char('0')));
        ShotRecord s;
        s.shotId = i + 1;
        s.club   = club;
        s.contextId = QStringLiteral("driver");
        s.timestampMs = t0 + i * 45'000LL;
        s.carry  = 200.0 + double(i % 5);
        s.total  = 220.0;
        s.offline = 3.0;
        s.ballSpeed = 150.0;
        s.lmClubSpeed = 100.0;
        s.smash = 1.45;
        s.swingLength = QStringLiteral("full");
        const OutcomeShot &p = plan[size_t(i)];
        const std::pair<const char *, bool> rows[] = {
            { "slice", p.slice }, { "strike_toe", p.toe }, { "over_the_top", p.overTheTop }, { "early_extension", true },
        };
        for (const auto &[id, fired] : rows) {
            ConditionRow r;
            r.conditionId = QString::fromLatin1(id);
            r.drivingMeasureId = QStringLiteral("m_handPathLoop");
            r.state = fired ? ShotState::Fired : ShotState::Clean;
            const bool ballRow = qstrcmp(id, "slice") == 0 || qstrcmp(id, "strike_toe") == 0;
            if (!p.measured && !ballRow) {
                r.state = ShotState::NotAssessable;
                s.rows.push_back(r);
                continue;
            }
            r.value = fired ? 10.0 : 1.0;
            r.corridorLo = 0.0; r.corridorHi = 4.0;
            r.corridorShape = CorridorShape::Ceiling;
            r.z = fired ? 3.0 : 0.5;
            r.direction = fired ? 1 : 0;
            r.firedSignalCount = fired ? 1 : 0;
            s.rows.push_back(r);
        }
        out.push_back(s);
    }
    QJsonObject root;
    root[QStringLiteral("schemaVersion")] = 1;
    root[QStringLiteral("ledger")] = toJson(out);
    QFile f(dir + QStringLiteral("/diagnostics.json"));
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

// Every fault item on Coaching: the focus's "right now" and Next on your list.
static QVariantList faultItems(const WorkOnsController &c)
{
    QVariantList out = c.focusItem().value(QStringLiteral("rightNow")).toList();
    out += c.nextItems();
    return out;
}

// The contrast the fault item for `id` carries, or "?" when no fault item is that condition.
static QString itemContrast(const WorkOnsController &c, const char *id)
{
    for (const QVariant &v : faultItems(c))
        if (v.toMap().value(QStringLiteral("conditionId")).toString() == QLatin1String(id))
            return v.toMap().value(QStringLiteral("outcomeContrast")).toString();
    return QStringLiteral("?");
}

static bool countsAre(const QJsonArray &a, std::initializer_list<int> want)
{
    if (a.size() != qsizetype(want.size())) return false;
    int i = 0;
    for (int w : want)
        if (a.at(i++).toInt(-1) != w) return false;
    return true;
}

static QString storedFingerprint(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("derivedFrom")).toObject()
        .value(QStringLiteral("fingerprint")).toString();
}

// One factor's options as one line, "any Any 38 · solid Solid 32 · strike_toe Toe 6", so a failure prints what it saw.
static QString optionLine(const WorkOnsController &c, const char *factor)
{
    QStringList parts;
    for (const QVariant &v : c.shotFilterOptions().value(QString::fromLatin1(factor)).toList()) {
        const QVariantMap m = v.toMap();
        parts << QStringLiteral("%1 %2 %3").arg(m.value(QStringLiteral("value")).toString(), m.value(QStringLiteral("label")).toString(),
                                               QString::number(m.value(QStringLiteral("count")).toInt()));
    }
    return parts.join(QStringLiteral(" · "));
}

static bool optionsAre(const WorkOnsController &c, const char *strike, const char *direction, const char *flight)
{
    const bool ok = optionLine(c, "strike") == QString::fromUtf8(strike) && optionLine(c, "direction") == QString::fromUtf8(direction)
                 && optionLine(c, "flight") == QString::fromUtf8(flight) && c.shotFilterOptions().size() == 3;
    if (!ok)
        std::printf("    options: strike [%s] direction [%s] flight [%s]\n", qPrintable(optionLine(c, "strike")),
                    qPrintable(optionLine(c, "direction")), qPrintable(optionLine(c, "flight")));
    return ok;
}

static bool lineIs(const WorkOnsController &c, const QString &want)
{
    if (c.shotFilterLine() == want) return true;
    std::printf("    line: \"%s\"\n", qPrintable(c.shotFilterLine()));
    return false;
}

static bool contrastIs(const WorkOnsController &c, const char *id, const QString &want)
{
    const QString have = c.faultDetail(QString::fromLatin1(id)).value(QStringLiteral("outcomeContrast")).toString();
    if (have == want) return true;
    std::printf("    %s contrast: \"%s\"\n", id, qPrintable(have));
    return false;
}

static void checkShotFilter(const QString &root)
{
    std::printf("\nshot filter\n");
    const qint64 day = 86'400'000LL, t0 = 1'784'000'000'000LL;
    const QString golfer = root + QStringLiteral("/outcome");
    QDir().mkpath(golfer);
    // Two driver sessions of 14: nine slices with over_the_top each, three of them off the toe, and five straight
    // shots (one of them with over_the_top too, in the second); then ten straight 7 iron shots. 18 slices, 20
    // straight; 6 toe strikes, 32 solid; no flight read (no landing angle, apex or launch).
    std::vector<OutcomeShot> first, second, iron;
    for (int i = 0; i < 14; ++i) first.push_back({ i < 9, i < 9, true, i < 3 });
    for (int i = 0; i < 14; ++i) second.push_back({ i < 9, i < 9 || i == 9, true, i < 3 });
    for (int i = 0; i < 10; ++i) iron.push_back({ false, false });
    stageOutcomeSession(golfer, QStringLiteral("2026-08-01_M_01"), t0, QStringLiteral("DRIVER"), first);
    stageOutcomeSession(golfer, QStringLiteral("2026-08-02_M_01"), t0 + day, QStringLiteral("DRIVER"), second);
    stageOutcomeSession(golfer, QStringLiteral("2026-08-03_M_01"), t0 + 2 * day, QStringLiteral("7 IRON"), iron);

    WorkOnsController c;
    c.setSynchronous(true);
    check(optionsAre(c, "any Any 0", "any Any 0", "any Any 0"), "before any tendencies: each factor's list is Any alone");
    c.setAthleteDir(golfer);
    check(c.waitForIdle() && c.tendenciesReady(), "the catch-up settles, the tendencies with it");
    check(c.shotFilter() == QStringLiteral("all") && c.shotFilterLine().isEmpty()
              && c.summarySubtitle() == QStringLiteral("From 38 swings over 3 sessions"), "every shot to begin with, and no line");
    check(c.outcomeTooFewNote().isEmpty(), "under All there is no too-few-measured note");

    // The options: Any first, then each value the shots show, in the table's order, each counted.
    check(optionsAre(c, "any Any 38 · solid Solid 32 · strike_toe Toe 6", "any Any 38 · straight Straight 20 · slice Slice 18",
                     "any Any 38"),
          "each factor: Any first, then its values in the table's order with their counts; a factor never read has Any alone");

    for (const QVariant &v : faultItems(c))
        check(v.toMap().value(QStringLiteral("outcomeContrast")).toString().isEmpty(), "under All a fault item says no contrast");
    check(c.faultDetail(QStringLiteral("over_the_top")).value(QStringLiteral("outcomeContrast")).toString().isEmpty(),
          "nor does its detail");

    const QString allFile = WorkOnsController::themesPath(golfer);
    const QString allPrint = storedFingerprint(allFile);
    const QDateTime allStamp = QFileInfo(allFile).lastModified();
    const QVariantList itemsBefore = c.items(), tendenciesBefore = c.tendencyItems();
    const QVariantMap lastBefore = c.lastSession();
    int itemsSignals = 0;
    QObject::connect(&c, &WorkOnsController::itemsChanged, [&itemsSignals] { ++itemsSignals; });

    // ── One direction ──
    const QString slice = QStringLiteral("strike:any|direction:slice|flight:any");
    const int revision = c.detailsRevision();
    c.setShotFilter(QStringLiteral("direction:slice"));
    check(c.waitForIdle() && c.shotFilter() == slice && !c.clubScopeRefreshing() && !c.matchupsRefreshing()
              && !c.clubScopeWorking(), "a direction settles, read back in full, and the screens have caught up with it");
    check(c.clubScope() == QStringLiteral("all"), "the club choice is left as it was");
    check(c.summarySubtitle() == QStringLiteral("From 18 swings over 2 sessions"), "the summary reads only the slices");
    check(c.detailsRevision() > revision, "the landing moves detailsRevision, so an open fault reads its contrast again");
    const QString ottSlice = QStringLiteral("On your sliced shots 18 of 18 · other shots 1 of 20");
    const QString eeSlice  = QStringLiteral("On your sliced shots 18 of 18 · other shots 20 of 20 — just as often on your other shots");
    check(contrastIs(c, "over_the_top", ottSlice),
          "over the top's detail: on every slice, hardly ever on the rest, and the counts say it alone");
    check(contrastIs(c, "early_extension", eeSlice), "early extension's: on every shot, so it says just as often on the others");
    {
        bool any = false, right = true;
        for (const QVariant &v : faultItems(c)) {
            const QVariantMap m = v.toMap();
            const QString id = m.value(QStringLiteral("conditionId")).toString();
            if (id != QLatin1String("over_the_top") && id != QLatin1String("early_extension")) continue;
            any = true;
            right = right && m.value(QStringLiteral("outcomeContrast")).toString() == (id == QLatin1String("over_the_top") ? ottSlice : eeSlice);
        }
        check(any && right, "the focus and Next on your list carry the same line on each fault");
        const QString focusId = c.focusItem().value(QStringLiteral("focusConditionId")).toString();
        check(!focusId.isEmpty()
                  && c.focusItem().value(QStringLiteral("outcomeContrast")).toString() == itemContrast(c, focusId.toLatin1().constData()),
              "and the focus says its first fault's");
    }
    check(lineIs(c, QStringLiteral("Your sliced shots: 18 over 2 sessions, against 20 others. Your swing was measured on all of them.")),
          "the line says what the choice covers, and that the swing was measured on every one of them");
    check(c.outcomeTooFewNote().isEmpty(), "18 measured shots are enough: no too-few-measured note");
    check(optionsAre(c, "any Any 18 · solid Solid 12 · strike_toe Toe 6", "any Any 38 · straight Straight 20 · slice Slice 18",
                     "any Any 18"),
          "the other factors' counts are the slices'; the direction's own list keeps the others' choices (Any) and so counts every shot");
    const QString sliceFile = WorkOnsController::themesPath(golfer, ClubScope(), shotFilterFromKey(QStringLiteral("direction:slice")));
    {
        QFile f(sliceFile);
        const QJsonObject stored = f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll()).object() : QJsonObject();
        const QJsonObject o = stored.value(QStringLiteral("outcome")).toObject();
        check(sliceFile.endsWith(QStringLiteral("swing_themes.all.strike_any_direction_slice_flight_any.json")) && !stored.isEmpty(),
              "the choice is stored in a file of its own, beside the summary");
        check(o.value(QStringLiteral("key")).toString() == slice && o.value(QStringLiteral("chosen")).toInt() == 18
                  && o.value(QStringLiteral("sessions")).toInt() == 2 && o.value(QStringLiteral("others")).toInt() == 20
                  && o.value(QStringLiteral("measured")).toInt() == 18 && o.value(QStringLiteral("rule")).toInt() == kOutcomeScopeRuleVersion
                  && countsAre(o.value(QStringLiteral("contrasts")).toObject().value(QStringLiteral("over_the_top")).toArray(), { 18, 18, 1, 20 })
                  && !stored.value(QStringLiteral("records")).toArray().isEmpty(),
              "with its records, the line's counts and each fault's contrast counts");
    }
    check(itemsSignals == 0 && c.items() == itemsBefore && c.lastSession() == lastBefore, "the work-on list and the last session stay every shot's");
    check(c.tendencyItems() == tendenciesBefore, "and so does the Tendencies tab");

    // ── Two factors at once: a shot must match both ──
    const QString toeSlice = QStringLiteral("strike:strike_toe|direction:slice|flight:any");
    c.setShotFilter(toeSlice);
    check(c.waitForIdle() && c.shotFilter() == toeSlice && c.summarySubtitle() == QStringLiteral("From 6 swings over 2 sessions"),
          "toe AND slice: only the sliced shots off the toe");
    check(lineIs(c, QStringLiteral("Your sliced, toe shots: 6 over 2 sessions, against 32 others. Your swing was measured on all of them.")),
          "the line names both, direction first");
    check(contrastIs(c, "over_the_top", QStringLiteral("On your sliced, toe shots 6 of 6 · other shots 13 of 32")),
          "the contrast is against every other judged shot, its words the filter's");
    check(optionsAre(c, "any Any 18 · solid Solid 12 · strike_toe Toe 6", "any Any 6 · straight Straight 0 · slice Slice 6", "any Any 6"),
          "each list keeps the other choices: under the toe, a straight shot leaves none and is still listed with 0");
    check(c.outcomeTooFewNote() == QStringLiteral("Too few of these shots had your swing measured to say what comes with them yet."),
          "six measured shots are under the floor of eight: the note says so");

    // ── A part naming no option reads back Any; the request is kept ──
    c.setShotFilter(QStringLiteral("strike:thin|direction:slice|flight:sky"));
    check(c.waitForIdle() && c.shotFilter() == slice && c.summarySubtitle() == QStringLiteral("From 18 swings over 2 sessions"),
          "no thin and no sky among these shots: those parts read any, the slice stands");
    check(lineIs(c, QStringLiteral("Your sliced shots: 18 over 2 sessions, against 20 others. Your swing was measured on all of them.")),
          "and the line is the slice's");

    // ── Within a club choice ──
    c.setShotFilter(toeSlice);
    c.setClubScope(QStringLiteral("group:driver"));
    check(c.waitForIdle() && c.clubScope() == QStringLiteral("group:driver") && c.shotFilter() == toeSlice,
          "a club choice and a filter together");
    check(lineIs(c, QStringLiteral("Your sliced, toe driver shots: 6 over 2 sessions, against 22 others. Your swing was measured on all of them.")),
          "the line names the club choice, and the other shots are that choice's");
    check(contrastIs(c, "over_the_top", QStringLiteral("On your sliced, toe shots 6 of 6 · other shots 13 of 22")),
          "the contrast is against the club choice's other shots");
    check(optionsAre(c, "any Any 18 · solid Solid 12 · strike_toe Toe 6", "any Any 6 · straight Straight 0 · slice Slice 6", "any Any 6"),
          "the options count the club choice's shots");

    // A filter the club choice has never had: All, with the request kept, part by part.
    c.setClubScope(QStringLiteral("club:7 IRON"));
    check(c.waitForIdle() && c.shotFilter() == QStringLiteral("all") && c.shotFilterLine().isEmpty()
              && c.summarySubtitle() == QStringLiteral("From 10 swings over 1 session"),
          "a 7 iron never sliced nor came off the toe here: the filter reads all, and the summary is every 7 iron shot");
    check(optionsAre(c, "any Any 10 · solid Solid 10", "any Any 10 · straight Straight 10", "any Any 10"),
          "its options are its own shots' values");
    {
        bool none = true;
        for (const QVariant &v : faultItems(c)) none = none && v.toMap().value(QStringLiteral("outcomeContrast")).toString().isEmpty();
        check(none && c.focusItem().value(QStringLiteral("outcomeContrast")).toString().isEmpty(), "and no fault says a contrast");
    }
    c.setClubScope(QStringLiteral("group:driver"));
    check(c.waitForIdle() && c.shotFilter() == toeSlice && c.summarySubtitle() == QStringLiteral("From 6 swings over 2 sessions"),
          "both parts come back with the club choice that has them");

    // The club's words: an iron group's hyphenated, one club its own name; one session and no others in the singular.
    c.setShotFilter(QStringLiteral("direction:straight"));
    c.setClubScope(QStringLiteral("group:mid_irons"));
    check(c.waitForIdle() && lineIs(c, QStringLiteral("Your straight mid-iron shots: 10 over 1 session, and no others. Your swing was measured on all of them.")),
          "a group's words, one session, and no other shots to set them against");
    c.setClubScope(QStringLiteral("club:7 IRON"));
    check(c.waitForIdle() && lineIs(c, QStringLiteral("Your straight 7 iron shots: 10 over 1 session, and no others. Your swing was measured on all of them.")),
          "one club's words");
    c.setClubScope(QStringLiteral("all"));
    c.setShotFilter(slice);
    check(c.waitForIdle() && c.shotFilter() == slice && c.summarySubtitle() == QStringLiteral("From 18 swings over 2 sessions"),
          "back to every club's slices");

    // ── Back to All × All: today's file and fingerprint, untouched ──
    c.setShotFilter(QStringLiteral("all"));
    check(c.waitForIdle() && c.shotFilter() == QStringLiteral("all") && c.shotFilterLine().isEmpty()
              && c.summarySubtitle() == QStringLiteral("From 38 swings over 3 sessions") && c.items() == itemsBefore,
          "back to every shot");
    check(storedFingerprint(allFile) == allPrint && QFileInfo(allFile).lastModified() == allStamp,
          "All × All is the same file under the same fingerprint, never rewritten");
    check(WorkOnsController::themesPath(golfer, ClubScope(), ShotFilter()) == allFile
              && WorkOnsController::themesPath(golfer, resolveClubScope(QStringLiteral("group:driver"),
                                                                      clubScopeOptions({ "DRIVER" }, [](const QString &) { return QStringLiteral("driver"); })),
                                               ShotFilter())
                     .endsWith(QStringLiteral("swing_themes.group_driver.json")),
          "a filter of All keeps the club choice's own file");

    // ── A second look reads the stored choice, with its contrast, and reduces nothing ──
    {
        const QDateTime stamp = QFileInfo(sliceFile).lastModified();
        QThread::msleep(25);
        WorkOnsController again;
        again.setSynchronous(true);
        again.setShotFilter(QStringLiteral("direction:slice"));
        check(again.shotFilter() == QStringLiteral("all"), "asked before there are options: all for now");
        again.setAthleteDir(golfer);
        check(again.waitForIdle() && again.shotFilter() == slice
                  && again.summarySubtitle() == QStringLiteral("From 18 swings over 2 sessions"),
              "the request resolves once the tendencies are in hand");
        check(QFileInfo(sliceFile).lastModified() == stamp, "…from the stored file, with nothing reduced");
        check(contrastIs(again, "over_the_top", ottSlice)
                  && lineIs(again, QStringLiteral("Your sliced shots: 18 over 2 sessions, against 20 others. Your swing was measured on all of them.")),
              "the contrast and the line come back from it exactly");
    }

    // ── A correction, which only touches the ledger, moves the choice's fingerprint ──
    {
        const QString before = storedFingerprint(sliceFile);
        c.setShotFilter(QStringLiteral("direction:slice"));
        check(c.waitForIdle() && storedFingerprint(sliceFile) == before, "the slice again, from its stored file");
        QThread::msleep(25);    // a new mtime, whatever the file system's resolution
        const QString path = QDir(golfer).filePath(QStringLiteral("2026-08-02_M_01")) + QStringLiteral("/diagnostics.json");
        QFile f(path);
        QJsonObject doc;
        if (f.open(QIODevice::ReadOnly)) {
            doc = QJsonDocument::fromJson(f.readAll()).object();
            f.close();
        }
        // The first shot of the second session was really a hook.
        doc[QStringLiteral("corrections")] = QJsonObject{
            { QStringLiteral("1"), QJsonArray{ QJsonObject{ { QStringLiteral("family"), QStringLiteral("direction") },
                                                            { QStringLiteral("from"), QStringLiteral("slice") },
                                                            { QStringLiteral("to"), QStringLiteral("hook") } } } } };
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(QJsonDocument(doc).toJson(QJsonDocument::Compact));
        f.close();
        c.refresh();
        check(c.waitForIdle() && !storedFingerprint(sliceFile).isEmpty() && storedFingerprint(sliceFile) != before,
              "a correction moves the fingerprint, and the choice is reduced again");
        check(c.summarySubtitle() == QStringLiteral("From 17 swings over 2 sessions")
                  && lineIs(c, QStringLiteral("Your sliced shots: 17 over 2 sessions, against 21 others. Your swing was measured on all of them.")),
              "the corrected shot has left the slices");
        check(optionsAre(c, "any Any 17 · solid Solid 12 · strike_toe Toe 5", "any Any 38 · straight Straight 20 · slice Slice 17 · hook Hook 1",
                         "any Any 17"),
              "and is a direction of its own, as a hook, after the slice in the table's order");
        check(storedFingerprint(allFile) == allPrint, "All × All's fingerprint does not follow a correction");
    }

    // ── Measured: a choice whose shots mostly had no swing fault assessed (launch-monitor-only) says so ──
    {
        const QString lmOnly = root + QStringLiteral("/outcomeMeasured");
        QDir().mkpath(lmOnly);
        // One driver session of 20: ten slices with over_the_top, the swing measured on only the first three;
        // ten straight shots, the swing measured on none of them.
        std::vector<OutcomeShot> plan;
        for (int i = 0; i < 20; ++i) plan.push_back({ i < 10, i < 10, i < 3 });
        stageOutcomeSession(lmOnly, QStringLiteral("2026-08-05_M_01"), t0 + 4 * day, QStringLiteral("DRIVER"), plan);

        WorkOnsController m;
        m.setSynchronous(true);
        m.setAthleteDir(lmOnly);
        check(m.waitForIdle() && m.tendenciesReady() && m.outcomeTooFewNote().isEmpty() && m.shotFilterLine().isEmpty(),
              "under All: no line, and no too-few-measured note");

        const QString tooFew = QStringLiteral("Too few of these shots had your swing measured to say what comes with them yet.");
        m.setShotFilter(QStringLiteral("direction:slice"));
        check(m.waitForIdle() && m.shotFilter() == slice
                  && lineIs(m, QStringLiteral("Your sliced shots: 10 over 1 session, against 10 others. Your swing was measured on 3 of them.")),
              "the line says on how many of the chosen shots the swing was measured");
        check(m.outcomeTooFewNote() == tooFew, "three measured shots are under the floor of eight: the note says so");
        {
            QFile f(WorkOnsController::themesPath(lmOnly, ClubScope(), shotFilterFromKey(QStringLiteral("direction:slice"))));
            const QJsonObject o = f.open(QIODevice::ReadOnly)
                                      ? QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("outcome")).toObject()
                                      : QJsonObject();
            check(o.value(QStringLiteral("chosen")).toInt() == 10 && o.value(QStringLiteral("measured")).toInt() == 3,
                  "the stored block counts the measured shots beside the chosen ones");
        }

        m.setShotFilter(QStringLiteral("direction:straight"));
        check(m.waitForIdle() && m.shotFilter() == QStringLiteral("strike:any|direction:straight|flight:any")
                  && lineIs(m, QStringLiteral("Your straight shots: 10 over 1 session, against 10 others. Your swing was not measured on any of them.")),
              "a choice the swing was measured on none of says so");
        check(m.outcomeTooFewNote() == tooFew, "and carries the note");

        // Read back from the stored file by a second controller: the measured count comes with it.
        {
            WorkOnsController again;
            again.setSynchronous(true);
            again.setShotFilter(QStringLiteral("direction:slice"));
            again.setAthleteDir(lmOnly);
            check(again.waitForIdle() && again.shotFilter() == slice
                      && again.shotFilterLine().endsWith(QStringLiteral("Your swing was measured on 3 of them."))
                      && again.outcomeTooFewNote() == tooFew,
                  "the measured count and the note come back from the stored file");
        }

        m.setShotFilter(QStringLiteral("all"));
        check(m.waitForIdle() && m.outcomeTooFewNote().isEmpty() && m.shotFilterLine().isEmpty(), "back to All: the note goes");
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
        // Two needs-work habits, as the work-on list holds them (rule v4, 14 Nov 2026: Your swing follows
        // Work-ons): the reverse spine angle, on every swing of both sessions, and the ball position — a
        // set-up the list works on, on every swing of the one session that measured it (the pull is an
        // outcome: never one). Every listable habit carries a drill. The focus starts earliest in the
        // swing, so the set-up (read at address, "Stance and ball-position sticks") is the focus, titled by
        // its drill and aiming for its ideal, and the reverse spine (at the top) is next on the list.
        const QVariantMap focus = c.focusItem();
        const QVariantList rightNow = focus.value(QStringLiteral("rightNow")).toList();
        const QVariantMap ballNow = rightNow.value(0).toMap();
        const QVariantList next = c.nextItems();
        const QVariantMap spineNext = next.value(0).toMap();
        check(rightNow.size() == 1 && next.size() == 1
              && ballNow.value(QStringLiteral("sessions")).toList() == QVariantList{ true }
              && ballNow.value(QStringLiteral("frequency")).toString() == QStringLiteral("every swing")
              && spineNext.value(QStringLiteral("sessions")).toList() == QVariantList{ true, true }
              && spineNext.value(QStringLiteral("frequency")).toString() == QStringLiteral("every swing"),
              "the set-up worked on is the focus (earliest in the swing), the fault on every swing of both sessions is next");
        check(!focus.value(QStringLiteral("practise")).toString().isEmpty()
              && focus.value(QStringLiteral("practiseLabel")).toString() == QStringLiteral("Stance and ball-position sticks")
              && focus.value(QStringLiteral("title")).toString() == QStringLiteral("Stance and ball-position sticks")
              && !focus.value(QStringLiteral("aimFor")).toList().value(0).toString().isEmpty(),
              "…a habit with its drill: titled by the drill, its ideal as what to aim for, the drill to practise");
        // The focus names its condition, and conditionLabel() names it as the list does.
        const QString focusId = focus.value(QStringLiteral("focusConditionId")).toString();
        check(focusId == QStringLiteral("ball_back")
              && c.conditionLabel(focusId) == item(c, "ball_back").value(QStringLiteral("name")).toString()
              && !c.conditionLabel(focusId).isEmpty() && c.conditionLabel(focusId) != focusId,
              "focusConditionId is the focus habit's condition, and conditionLabel() gives the name the list shows");
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
    check(areaGroups(c) == QStringList{ "lateral", "setup", "ballFlight",
                                        "posture", "rotation", "arms", "wrists", "planePath", "delivery" },
          "area order: one habit each, the occasional breaks the tie; then the unmeasured in address-to-finish order");
    check(!areaGroups(c).contains(QStringLiteral("golfer")),
          "golfer never yields an area on Home, on the shipped pack");
    {
        const QVariantMap posture = area(c, "posture"), lateral = area(c, "lateral"), setup = area(c, "setup");
        check(lateral.value(QStringLiteral("label")).toString() == QStringLiteral("Lateral & balance")
              && posture.value(QStringLiteral("label")).toString() == QStringLiteral("Posture"),
              "area labels are conditionGroupLabel's: 'Lateral & balance', 'Posture'");
        check(lateral.value(QStringLiteral("habits")).toInt() == 1 && lateral.value(QStringLiteral("measured")).toInt() == 2
              && lateral.value(QStringLiteral("topFaultId")).toString() == QStringLiteral("reverse_spine_p4")
              && lateral.value(QStringLiteral("topFault")).toString() == QStringLiteral("Reverse spine angle at P4"),
              "lateral: one habit, its top fault named by the pack label");
        check(lateral.value(QStringLiteral("occasional")).toInt() == 1,
              "…and sway on half the swings, the older half faded, is occasional");
        check(lateral.value(QStringLiteral("hasTrend")).toBool() && lateral.value(QStringLiteral("habitTrend")).toInt() == -1
              && setup.value(QStringLiteral("habitTrend")).toInt() == 1,
              "trend against the counts without s2: the ball position a new habit, sway cleared as one, the spine held");
        check(!posture.value(QStringLiteral("measurable")).toBool()
              && posture.value(QStringLiteral("items")).toList().isEmpty()
              && !posture.value(QStringLiteral("hasTrend")).toBool(),
              "an area nothing assessed is unmeasurable, with no items and no trend");

        const QVariantMap spineItem = areaItem(lateral, "reverse_spine_p4"), swayItem = areaItem(lateral, "sway"),
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
        const QVariantMap wrists = area(w, "wrists");
        const QVariantMap cup = areaItem(wrists, "cupped_at_top"), bow = areaItem(wrists, "bowed_at_top");
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

    checkConsistencyMarks(tmp.path());
    checkTendencies(tmp.path());
    checkClubScope(tmp.path());
    checkShotFilter(tmp.path());

    std::printf(g_fail ? "FAILED (%d)\n" : "OK\n", g_fail);
    return g_fail ? 1 : 0;
}
