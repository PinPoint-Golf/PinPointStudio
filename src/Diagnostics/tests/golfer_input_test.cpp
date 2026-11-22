/*
 * Copyright (C) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

// golfer_input_test — THE GOLFER'S INPUT IS NOT IN THE CACHE (refactor step 2.5; audit H-04's missing test;
// build/refactor/step-2/BRIEF.md §4.8, the lead's call J-33; docs/developer/information_model.md FILE-SESS-02,
// FILE-SESS-03, FILE-SHOT-02).
//
//   N  a focus, a run, a screen answer, a declared miss, a shot intent and a correction, made through the
//      panel's adapter (refactor 5.4a: the old engine's own model until then), live in session_input.json and the shots' reviews, not in diagnostics.json; deleting
//      diagnostics.json and reopening the session loses none of them
//   (L, O, R, C and B: since refactor 5.4a the owner's — session_diagnostics_test LEG, LEGw, UNR-d; see below.)
//   F  the files layer itself: session_input.json round-trips, keeps unknown keys, refuses a newer schema
//
//   cmake --build build/tests --target golfer_input_test -j 6
//   ctest --test-dir build/tests -R '^golfer_input_test$' --output-on-failure

#include "../../Gui/diagnostics/session_diagnostics_model.h"
#include "owner_rig.h"   // the owners the suite drives (refactor 5.4a)
#include "../../Export/rule_data_install.h"
#include "../../Export/session_diagnostics_files.h"
#include "../../Export/session_files.h"
#include "../../Export/session_store.h"
#include "../../Export/swing_store.h"
#include "../../Export/swing_store_repository.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTimeZone>

#include <cstdio>
#include <memory>
#include <vector>

using pinpoint::SessionDiagnosticsFiles;
using pinpoint::SessionFiles;

static int g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}

#ifndef PP_LIVE_SWINGS_DIR
#  define PP_LIVE_SWINGS_DIR "."
#endif

static constexpr qint64 kStageEpochMs = 1767225600000LL;   // 2026-01-01T00:00:00Z

static QString makeSession(const QTemporaryDir &tmp, const char *athlete, const char *session)
{
    const QString dir = QDir(tmp.path()).filePath(QLatin1String(athlete) + QLatin1Char('/') + QLatin1String(session));
    QDir().mkpath(dir);
    return dir;
}

// A corpus fixture as <session>/swing_000N/swing.json, its mtime pinned (session_diagnostics_model_test's rule).
static bool stageShot(const QString &sessionDir, int shotId, const char *fixture = "rich_7iron")
{
    const QString dst = QDir(sessionDir).filePath(QStringLiteral("swing_%1").arg(shotId, 4, 10, QLatin1Char('0')));
    if (!QDir().mkpath(dst)) return false;
    const QString src = QDir(QLatin1String(PP_LIVE_SWINGS_DIR)).filePath(QLatin1String(fixture) + QLatin1String("/swing.json"));
    const QString file = QDir(dst).filePath(QStringLiteral("swing.json"));
    if (!QFile::copy(src, file)) return false;
    QFile f(file);
    if (!f.open(QIODevice::ReadWrite)) return false;
    return f.setFileTime(QDateTime::fromMSecsSinceEpoch(kStageEpochMs + qint64(shotId) * 1000, QTimeZone::UTC),
                         QFileDevice::FileModificationTime);
}

static QByteArray bytesOf(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

static QJsonObject jsonOf(const QString &path)
{
    return QJsonDocument::fromJson(bytesOf(path)).object();
}

static bool writeJson(const QString &path, const QJsonObject &o)
{
    QFile f(path);
    return f.open(QIODevice::WriteOnly | QIODevice::Truncate)
        && f.write(QJsonDocument(o).toJson(QJsonDocument::Compact)) > 0;
}

// A shot's review, read back through the swing door as any reader would.
static pinpoint::ShotReview reviewOf(const QString &sessionDir, int shotId)
{
    pinpoint::SessionStore                  sessions;
    pinpoint::SwingStoreRecordingRepository recordings(sessions);
    const pinpoint::ShotRef shot =
        recordings.shotOfFolder(QDir(sessionDir).filePath(QStringLiteral("swing_%1").arg(shotId, 4, 10, QLatin1Char('0'))));
    return recordings.readReview(shot);
}

static bool hasNoLegacyKeys(const QJsonObject &envelope)
{
    for (const QString &k : SessionDiagnosticsFiles::legacyGolferKeys())
        if (envelope.contains(k)) return false;
    return true;
}

// The model's whole golfer state, as one comparable string.
static QString describe(const pinpoint::GolferInput &g)
{
    QStringList parts;
    parts << QStringLiteral("focus=%1@%2").arg(g.focus.conditionId).arg(g.focus.fromShot)
          << QStringLiteral("miss=%1").arg(g.declaredMiss)
          << QStringLiteral("run=%1@%2").arg(g.intentRun.shape).arg(g.intentRun.fromShot);
    QStringList screens;
    for (auto it = g.screens.constBegin(); it != g.screens.constEnd(); ++it)
        screens << it.key() + QLatin1Char('=') + (it.value() ? QLatin1Char('1') : QLatin1Char('0'));
    screens.sort();
    parts << QStringLiteral("screens=") + screens.join(QLatin1Char(','));
    QStringList intents;
    for (auto it = g.shotIntent.constBegin(); it != g.shotIntent.constEnd(); ++it)
        intents << QStringLiteral("%1:%2").arg(it.key()).arg(it.value());
    intents.sort();
    parts << QStringLiteral("intent=") + intents.join(QLatin1Char(','));
    QStringList corr;
    for (auto it = g.corrections.constBegin(); it != g.corrections.constEnd(); ++it)
        for (const auto &c : it.value()) corr << QStringLiteral("%1:%2:%3>%4").arg(it.key()).arg(c.family, c.from, c.to);
    corr.sort();
    parts << QStringLiteral("corr=") + corr.join(QLatin1Char(','));
    return parts.join(QLatin1Char(' '));
}

// ── N: the new homes, and the cache deleted ────────────────────────────────────────────────────────

static void newHomesSuite(const QTemporaryDir &tmp)
{
    std::printf("\nN: the golfer's input lives in its own homes, and survives the cache being deleted\n");
    const QString dir = makeSession(tmp, "athlete_new", "2026-11-15_M_01");
    bool staged = true;
    for (int i = 1; i <= 4; ++i) staged = stageShot(dir, i) && staged;
    check(staged, "N0 four swings staged");

    // (5.4a) The session's adapter, as the panel binds it; each edit forwards to the Session's or the Shot's command.
    owner_rig::ViewRig &rig = owner_rig::rigOfSession(dir);
    check(rig.ensureSessionFile(dir), "N0b its session file, as M2 left every session");
    QString held;
    {
        SessionDiagnosticsModel *m = rig.open(dir);
        check(m && m->shotCount() == 4, "N1 the four swings are graded");
        if (!m) return;
        m->declareFocusFrom(QStringLiteral("over_the_top"), 1);
        m->declareMiss(QStringLiteral("slice"));
        m->recordScreenResult(QStringLiteral("early_extension"), true);
        m->setIntentFrom(3, QStringLiteral("fade"));
        m->setShotIntent(1, QStringLiteral("draw"));
        m->correctOutcome(2, QStringLiteral("direction"), QStringLiteral("pull"));
        owner_rig::ViewRig::settle(rig.loadedAt(dir) ? rig.loadedAt(dir)->diagnostics() : nullptr);
        const pinpoint::GolferInput g = SessionDiagnosticsFiles::golferInput(dir, QJsonObject());
        held = describe(g);
        check(g.corrections.contains(2), "N2 the correction is saved (the pack knows the label)");
    }
    rig.release(dir);

    const QJsonObject in = jsonOf(SessionFiles::sessionInputFile(dir));
    check(in.value(QStringLiteral("schema")).toString() == QLatin1String(SessionDiagnosticsFiles::kSessionInputSchema),
          "N3 session_input.json is written, with its schema");
    check(in.value(QStringLiteral("declaredFocus")).toObject().value(QStringLiteral("conditionId")).toString() == QStringLiteral("over_the_top")
              && in.value(QStringLiteral("declaredFocus")).toObject().value(QStringLiteral("fromShot")).toInt() == 1
              && in.value(QStringLiteral("declaredMiss")).toString() == QStringLiteral("slice")
              && in.value(QStringLiteral("screenAnswers")).toObject().value(QStringLiteral("early_extension")).toBool()
              && in.value(QStringLiteral("intentRuns")).toArray().size() == 1
              && in.value(QStringLiteral("intentRuns")).toArray().first().toObject().value(QStringLiteral("shape")).toString() == QStringLiteral("fade"),
          "N4 …holding the focus, the miss, the screen answer and the run");
    const pinpoint::ShotReview r1 = reviewOf(dir, 1), r2 = reviewOf(dir, 2);
    check(r1.ok && r1.hasShotIntent && r1.shotIntent == QStringLiteral("draw"), "N5 shot 1's intent is on its review");
    check(r2.ok && r2.hasLabelCorrections && r2.labelCorrections.size() == 1
              && r2.labelCorrections.first().family == QStringLiteral("direction"),
          "N6 shot 2's correction is on its review");
    check(hasNoLegacyKeys(jsonOf(SessionFiles::diagnosticsFile(dir))), "N7 diagnostics.json holds none of it");

    check(QFile::remove(SessionFiles::diagnosticsFile(dir)), "N8 diagnostics.json deleted");
    {
        SessionDiagnosticsModel *m = rig.open(dir);
        check(m && m->shotCount() == 4, "N9 the cache is rebuilt from the swings");
        if (!m) return;
        const QString now = describe(SessionDiagnosticsFiles::golferInput(dir, QJsonObject()));
        if (now != held) std::printf("    held:  %s\n    again: %s\n", qPrintable(held), qPrintable(now));
        check(now == held, "N10 …and everything the golfer said is still there");
        check(m->focusConditionId() == QStringLiteral("over_the_top") && m->declaredMiss() == QStringLiteral("slice"),
              "N11 the panel reads the focus and the miss as before");
        pinpoint::Session *s = rig.loadedAt(dir);
        const auto in = s && s->diagnostics() ? s->diagnostics()->diagnosisInput() : nullptr;
        check(in && in->intents.value(1) == QStringLiteral("draw") && in->corrections.contains(2)
                  && in->focusConditionId == QStringLiteral("over_the_top"),
              "N11b …and the owner reduces with the intent, the correction and the focus");
    }

    // A review edit is not a new recording: the edited shot's row follows its document (the owner fires it again on
    // EVT-07, under the rewritten document's stamp).
    {
        SessionDiagnosticsModel *m = rig.open(dir);
        if (!m) return;
        m->setShotIntent(4, QStringLiteral("draw"));
        if (pinpoint::Session *s = rig.loadedAt(dir); s && s->diagnostics()) {
            owner_rig::ViewRig::settle(s->diagnostics());
            s->diagnostics()->flush(/*final=*/true);
        }
        const QJsonObject gradedBefore = jsonOf(SessionFiles::diagnosticsFile(dir)).value(QStringLiteral("session")).toObject()
                                             .value(QStringLiteral("gradedFrom")).toObject().value(QStringLiteral("4")).toObject();
        const pinpoint::SwingStore::DocInfo doc = pinpoint::SwingStore::info(QDir(dir).filePath(QStringLiteral("swing_0004")));
        std::printf("      gradedFrom 4: %lld, the document: %lld\n", qint64(gradedBefore.value(QStringLiteral("size")).toDouble()),
                    qint64(doc.size));
        check(qint64(gradedBefore.value(QStringLiteral("size")).toDouble()) == doc.size,
              "N12 the edited shot's row follows its document: it is not stale for the edit");
    }
}

// (Refactor 5.4a: L, C, B, O and R checked the golfer's input through the old engine's own reads and writes. Their
// owner-side twins: L and O → session_diagnostics_test LEG (the older keys read as the new homes are, by the one rule,
// golfer_input_rules.h) and LEGw (the owner's write carries them as they stood); R → UNR-d (a newer build's file read
// and never written; the notice is main.cpp's, diagAttached). C (a catch-up's write over a panel's) and B (the old
// engine's freeze pool) held of the old engine alone: the session has one writer, its owner, and one freeze rule.)

// ── F: the files layer ──────────────────────────────────────────────────────────────────────────────

static void filesSuite(const QTemporaryDir &tmp)
{
    std::printf("\nF: session_input.json through its one reader and writer\n");
    const QString dir = makeSession(tmp, "athlete_files", "2026-11-13_M_01");
    check(SessionDiagnosticsFiles::readSessionInput(dir).state == pinpoint::SessionInputRead::State::Missing,
          "F1 no file: every field absent (the older keys are the fallback)");
    QJsonObject o{ { QStringLiteral("schema"), QLatin1String(SessionDiagnosticsFiles::kSessionInputSchema) },
                   { QStringLiteral("declaredMiss"), QStringLiteral("slice") },
                   { QStringLiteral("futureKey"), 42 } };
    writeJson(SessionFiles::sessionInputFile(dir), o);
    const pinpoint::SessionInputRead r = SessionDiagnosticsFiles::readSessionInput(dir);
    check(r.state == pinpoint::SessionInputRead::State::Read && r.input.declaredMiss == QStringLiteral("slice")
              && !r.input.declaredFocus && !r.input.screenAnswers && !r.input.intentRuns,
          "F2 a file holds only the fields it names");
    pinpoint::SessionInput in = r.input;
    in.declaredFocus = pinpoint::DeclaredFocus{ QStringLiteral("x"), 2 };
    check(SessionDiagnosticsFiles::writeSessionInput(dir, in).ok()
              && jsonOf(SessionFiles::sessionInputFile(dir)).value(QStringLiteral("futureKey")).toInt() == 42,
          "F3 a rewrite keeps a key this build does not know");
    check(SessionDiagnosticsFiles::readSessionInput(dir).input == in, "F4 …and round-trips");
    o.insert(QStringLiteral("schema"), QStringLiteral("pinpoint.session-input/2"));
    writeJson(SessionFiles::sessionInputFile(dir), o);
    const QByteArray newer = bytesOf(SessionFiles::sessionInputFile(dir));
    check(SessionDiagnosticsFiles::readSessionInput(dir).readOnly()
              && SessionDiagnosticsFiles::writeSessionInput(dir, in).status == pinpoint::WriteResult::Status::ReadOnly
              && bytesOf(SessionFiles::sessionInputFile(dir)) == newer,
          "F5 a newer session_input.json is read-only");
    QString aside;
    writeJson(SessionFiles::diagnosticsFile(dir), QJsonObject{ { QStringLiteral("ledger"), QJsonObject() } });
    check(SessionDiagnosticsFiles::backupDiagnostics(dir, QStringLiteral("t"), &aside).ok()
              && !SessionDiagnosticsFiles::backupDiagnostics(dir, QStringLiteral("t")).ok(),
          "F6 a backup is never written over");
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    pinpoint::installProcessRuleData();   // this process's packs and norms (refactor step 3.2)
    std::printf("golfer_input_test\n");
    if (qEnvironmentVariableIsEmpty("PINPOINT_CORE_PACK")) {
        std::printf("PINPOINT_CORE_PACK is unset — run this through ctest, which sets the shipped content.\n");
        return 1;
    }
    QTemporaryDir tmp;
    if (!tmp.isValid()) return 1;
    newHomesSuite(tmp);
    filesSuite(tmp);
    owner_rig::rigs().clear();
    std::printf("\n%s: %d failure(s)\n", g_fail ? "FAIL" : "PASS", g_fail);
    return g_fail ? 1 : 0;
}
