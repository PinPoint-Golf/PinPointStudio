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
//   N  a focus, a run, a screen answer, a declared miss, a shot intent and a correction, made in the panel's
//      model, live in session_input.json and the shots' reviews, not in diagnostics.json; deleting
//      diagnostics.json and reopening the session loses none of them
//   L  a LEGACY session (its input only in diagnostics.json's older keys) reads identically; a regrade
//      (setRebuildCache, as regrade_ledger runs it) keeps the older keys byte for byte; an edit lands in the
//      new home and leaves the older keys as they were; a cleared shot answer stays cleared though the older
//      copy still holds one
//   O  an old envelope (written by the build before step 2.5, golfer keys inline) reads identically
//   R  a newer envelope is read-only: read, never written, and the golfer is told
//   F  the files layer itself: session_input.json round-trips, keeps unknown keys, refuses a newer schema
//
//   cmake --build build/tests --target golfer_input_test -j 6
//   ctest --test-dir build/tests -R '^golfer_input_test$' --output-on-failure

#include "../../Gui/diagnostics/session_diagnostics_model.h"
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

static std::unique_ptr<SessionDiagnosticsModel> freshModel()
{
    auto m = std::make_unique<SessionDiagnosticsModel>();
    m->setSynchronous(true);
    m->setCadence(QStringLiteral("everyShot"));
    m->setBaselineAuthority(SessionDiagnosticsModel::BaselineAuthority::Never);
    return m;
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

// The older shape: what the build before step 2.5 wrote (golferInputJson), inline in the envelope.
static QJsonObject legacyKeys()
{
    return QJsonObject{
        { QStringLiteral("intent"), QJsonObject{ { QStringLiteral("focusConditionId"), QStringLiteral("over_the_top") },
                                                 { QStringLiteral("focusFromShot"), 1 },
                                                 { QStringLiteral("declaredMiss"), QStringLiteral("slice") } } },
        { QStringLiteral("screens"), QJsonObject{ { QStringLiteral("early_extension"), true } } },
        { QStringLiteral("shotIntent"), QJsonObject{ { QStringLiteral("1"), QStringLiteral("draw") },
                                                     { QStringLiteral("3"), QStringLiteral("fade") } } },
        { QStringLiteral("intentFrom"), QJsonObject{ { QStringLiteral("fromShot"), 2 }, { QStringLiteral("shape"), QStringLiteral("fade") } } },
        { QStringLiteral("corrections"),
          QJsonObject{ { QStringLiteral("2"), QJsonArray{ QJsonObject{ { QStringLiteral("family"), QStringLiteral("direction") },
                                                                       { QStringLiteral("from"), QStringLiteral("push") },
                                                                       { QStringLiteral("to"), QStringLiteral("pull") } } } } } },
    };
}

// A legacy session: four swings and a diagnostics.json the build before 2.5 would have written — the cache
// as this build writes it, plus the older keys inline.
static QString makeLegacySession(const QTemporaryDir &tmp, const char *name)
{
    const QString dir = makeSession(tmp, "athlete_legacy", name);
    for (int i = 1; i <= 4; ++i) stageShot(dir, i);
    {
        auto m = freshModel();
        m->activateSession(dir);      // a fresh cache, no golfer keys
    }
    QJsonObject env = jsonOf(SessionFiles::diagnosticsFile(dir));
    const QJsonObject legacy = legacyKeys();
    for (auto it = legacy.constBegin(); it != legacy.constEnd(); ++it) env.insert(it.key(), it.value());
    writeJson(SessionFiles::diagnosticsFile(dir), env);
    return dir;
}

// ── N: the new homes, and the cache deleted ────────────────────────────────────────────────────────

static void newHomesSuite(const QTemporaryDir &tmp)
{
    std::printf("\nN: the golfer's input lives in its own homes, and survives the cache being deleted\n");
    const QString dir = makeSession(tmp, "athlete_new", "2026-11-15_M_01");
    bool staged = true;
    for (int i = 1; i <= 4; ++i) staged = stageShot(dir, i) && staged;
    check(staged, "N0 four swings staged");

    QString held;
    {
        auto m = freshModel();
        m->activateSession(dir);
        check(m->shotCount() == 4, "N1 the four swings are graded");
        m->declareFocusFrom(QStringLiteral("over_the_top"), 1);
        m->declareMiss(QStringLiteral("slice"));
        m->recordScreenResult(QStringLiteral("early_extension"), true);
        m->setIntentFrom(3, QStringLiteral("fade"));
        m->setShotIntent(1, QStringLiteral("draw"));
        m->correctOutcome(2, QStringLiteral("direction"), QStringLiteral("pull"));
        held = describe(m->golferInputHeld());
        check(m->golferInputHeld().corrections.contains(2), "N2 the correction is held (the pack knows the label)");
    }

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
        auto m = freshModel();
        m->activateSession(dir);
        check(m->shotCount() == 4, "N9 the cache is rebuilt from the swings");
        const QString now = describe(m->golferInputHeld());
        if (now != held) std::printf("    held:  %s\n    again: %s\n", qPrintable(held), qPrintable(now));
        check(now == held, "N10 …and everything the golfer said is still there");
        check(m->focusConditionId() == QStringLiteral("over_the_top") && m->declaredMiss() == QStringLiteral("slice"),
              "N11 the panel reads the focus and the miss as before");
    }

    // A review edit is not a new recording: the edited shot is not regraded at the next activation.
    {
        auto m = freshModel();
        m->activateSession(dir);
        m->setShotIntent(4, QStringLiteral("draw"));
        const QJsonObject gradedBefore = jsonOf(SessionFiles::diagnosticsFile(dir)).value(QStringLiteral("session")).toObject()
                                             .value(QStringLiteral("gradedFrom")).toObject().value(QStringLiteral("4")).toObject();
        const pinpoint::SwingStore::DocInfo doc = pinpoint::SwingStore::info(QDir(dir).filePath(QStringLiteral("swing_0004")));
        check(qint64(gradedBefore.value(QStringLiteral("size")).toDouble()) == doc.size,
              "N12 the edited shot's row follows its document: it is not stale for the edit");
    }
}

// ── L: a legacy session ─────────────────────────────────────────────────────────────────────────────

static void legacySuite(const QTemporaryDir &tmp)
{
    std::printf("\nL: a session whose input is only in diagnostics.json's older keys\n");
    const QString dir = makeLegacySession(tmp, "2026-11-10_M_01");
    const QJsonObject legacy = legacyKeys();
    const QString path = SessionFiles::diagnosticsFile(dir);

    QString first;
    {
        auto m = freshModel();
        m->activateSession(dir);
        const pinpoint::GolferInput g = m->golferInputHeld();
        first = describe(g);
        check(g.focus.conditionId == QStringLiteral("over_the_top") && g.focus.fromShot == 1 && g.declaredMiss == QStringLiteral("slice")
                  && g.screens.value(QStringLiteral("early_extension")) && g.intentRun.shape == QStringLiteral("fade")
                  && g.intentRun.fromShot == 2 && g.shotIntent.value(1) == QStringLiteral("draw")
                  && g.shotIntent.value(3) == QStringLiteral("fade") && g.corrections.value(2).size() == 1,
              "L1 every field reads from the older keys, as the build before step 2.5 read them");
        check(!QFile::exists(SessionFiles::sessionInputFile(dir)), "L2 a read writes no new home");
    }
    check(SessionDiagnosticsFiles::readDiagnostics(dir).envelope.legacyGolferInput == legacy,
          "L3 …and the activation's own write carried the older keys over unchanged");

    // The regrade, as regrade_ledger runs it.
    {
        QString backup;
        check(SessionDiagnosticsFiles::backupDiagnostics(dir, QStringLiteral("t"), &backup).ok() && QFileInfo::exists(path),
              "L4 a backup is a copy: the live file stays");
        auto m = freshModel();
        m->setRebuildCache(true);
        m->activateSession(dir);
        check(m->shotCount() == 4 && m->persistCount() > 0, "L5 the regrade grades every swing again and writes");
        check(SessionDiagnosticsFiles::readDiagnostics(dir).envelope.legacyGolferInput == legacy,
              "L6 the regraded file keeps every older key, byte for byte");
        check(describe(m->golferInputHeld()) == first, "L7 …and reads the same input");
    }

    // An edit lands in the new home; the older keys stay as they were.
    {
        auto m = freshModel();
        m->activateSession(dir);
        m->declareMiss(QStringLiteral("hook"));
        const QJsonObject in = jsonOf(SessionFiles::sessionInputFile(dir));
        check(in.value(QStringLiteral("declaredMiss")).toString() == QStringLiteral("hook"), "L8 a new miss lands in session_input.json");
        check(in.value(QStringLiteral("declaredFocus")).toObject().value(QStringLiteral("conditionId")).toString() == QStringLiteral("over_the_top")
                  && in.value(QStringLiteral("screenAnswers")).toObject().value(QStringLiteral("early_extension")).toBool()
                  && in.value(QStringLiteral("intentRuns")).toArray().first().toObject().value(QStringLiteral("fromShot")).toInt() == 2,
              "L9 …with the other three fields as they were read, so the new home is whole");
        check(SessionDiagnosticsFiles::readDiagnostics(dir).envelope.legacyGolferInput == legacy,
              "L10 diagnostics.json's older keys are untouched by the edit");

        m->setShotIntent(1, QString());                     // the older copy says draw (no run covers it): cleared
        m->correctOutcome(2, QStringLiteral("direction"), QStringLiteral("push"));   // back to the detected label
        const pinpoint::ShotReview r3 = reviewOf(dir, 1), r2 = reviewOf(dir, 2);
        if (!(r3.hasShotIntent && r3.shotIntent.isEmpty()))
            std::printf("    r3 ok=%d has=%d intent=%s err=%s\n", r3.ok, r3.hasShotIntent, qPrintable(r3.shotIntent), qPrintable(r3.error));
        check(r3.hasShotIntent && r3.shotIntent.isEmpty(), "L11 a cleared intent is held explicitly on the review");
        check(r2.hasLabelCorrections && r2.labelCorrections.isEmpty(), "L12 a withdrawn correction is held explicitly on the review");
        check(SessionDiagnosticsFiles::readDiagnostics(dir).envelope.legacyGolferInput == legacy,
              "L13 …and the older keys still stand");
    }
    {
        auto m = freshModel();
        m->activateSession(dir);
        const pinpoint::GolferInput g = m->golferInputHeld();
        check(g.declaredMiss == QStringLiteral("hook") && !g.shotIntent.contains(1) && !g.corrections.contains(2)
                  && g.shotIntent.value(3) == QStringLiteral("fade"),
              "L14 reopened: the new homes win, the cleared answers stay cleared, the rest still reads from the older keys");
    }
    check(QFile::remove(path), "L15 diagnostics.json deleted");
    {
        auto m = freshModel();
        m->activateSession(dir);
        const pinpoint::GolferInput g = m->golferInputHeld();
        check(g.declaredMiss == QStringLiteral("hook") && g.focus.conditionId == QStringLiteral("over_the_top"),
              "L16 the session-level input survives the cache going: the edit wrote all of it");
    }
}

// ── C: the older keys through the two writes the suites above do not reach (review round 1, item 3) ──────

static void carrySuite(const QTemporaryDir &tmp)
{
    std::printf("\nC: the older keys survive the cache going mid-activation, and a catch-up\n");
    const QJsonObject legacy = legacyKeys();
    {
        const QString dir = makeLegacySession(tmp, "2026-11-09_M_01");
        auto m = freshModel();
        m->activateSession(dir);
        check(QFile::remove(SessionFiles::diagnosticsFile(dir)), "C1 diagnostics.json deleted after the activation");
        m->declareMiss(QStringLiteral("hook"));                     // an edit, and so a write of the cache
        const pinpoint::DiagnosticsRead r = SessionDiagnosticsFiles::readDiagnostics(dir);
        check(r.state == pinpoint::DiagnosticsRead::State::Read && r.envelope.legacyGolferInput == legacy,
              "C2 the rewritten file carries the older keys the activation read, unchanged");
        check(jsonOf(SessionFiles::sessionInputFile(dir)).value(QStringLiteral("declaredMiss")).toString() == QStringLiteral("hook"),
              "C3 …and the edit is in the new home");
    }
    {
        const QString dir = makeLegacySession(tmp, "2026-11-09_M_02");
        QFile::remove(SessionFiles::clubBaselineFile(dir));
        auto c = freshModel();
        c->setBaselineAuthority(SessionDiagnosticsModel::BaselineAuthority::Always);
        c->activateSession(dir);
        check(c->persistCount() > 0 && SessionDiagnosticsFiles::readDiagnostics(dir).envelope.legacyGolferInput == legacy,
              "C4 a catch-up activation of a legacy session writes, and leaves the older keys as they were");
        check(QFile::exists(SessionFiles::clubBaselineFile(dir)), "C5 …and freezes its club baseline");
    }
}

// ── B: the club-baseline freeze reads its predecessors off the GUI thread (J-44) ─────────────────────

static QJsonObject baselineWithoutStamp(const QString &dir)
{
    QJsonObject o = jsonOf(SessionFiles::clubBaselineFile(dir));
    o.remove(QStringLiteral("frozenAt"));
    return o;
}

static void freezeSuite(const QTemporaryDir &tmp)
{
    std::printf("\nB: a freeze off the GUI thread gives the synchronous freeze's baseline, in the same order\n");
    // An athlete with two earlier sessions (one with a correction on a review) and the session to freeze.
    const QString older = makeSession(tmp, "athlete_freeze", "2026-11-01_M_01");
    const QString old2  = makeSession(tmp, "athlete_freeze", "2026-11-02_M_01");
    for (int i = 1; i <= 3; ++i) { stageShot(older, i); stageShot(old2, i); }
    {
        auto m = freshModel();
        m->activateSession(older);
        m->correctOutcome(2, QStringLiteral("direction"), QStringLiteral("pull"));
        m->activateSession(old2);
    }
    const QString a = makeSession(tmp, "athlete_freeze", "2026-11-03_M_01");
    const QString b = makeSession(tmp, "athlete_freeze", "2026-11-03_M_02");
    for (int i = 1; i <= 3; ++i) { stageShot(a, i); stageShot(b, i); }

    auto sync = freshModel();
    sync->setBaselineAuthority(SessionDiagnosticsModel::BaselineAuthority::Always);
    sync->activateSession(a);
    check(sync->baselineFrozen(), "B1 the synchronous model freezes the baseline in its activation");

    auto async = std::make_unique<SessionDiagnosticsModel>();
    async->setCadence(QStringLiteral("everyShot"));
    async->setBaselineAuthority(SessionDiagnosticsModel::BaselineAuthority::Always);
    async->activateSession(b);
    check(async->busy() && !async->baselineFrozen() && async->shotCount() == 0,
          "B2 off the GUI thread: the activation returns busy, nothing graded before the baseline is frozen");
    check(async->waitForIdle(60000), "B3 …and settles");
    check(async->baselineFrozen() && async->shotCount() == 3, "B4 the baseline is frozen, then every swing graded");
    check(!baselineWithoutStamp(b).isEmpty() && baselineWithoutStamp(b) == baselineWithoutStamp(a),
          "B5 the same baseline as the synchronous freeze (the predecessors' corrections included)");

    // A command while the freeze is still reading lands after it, never before.
    const QString c = makeSession(tmp, "athlete_freeze", "2026-11-03_M_03");
    for (int i = 1; i <= 3; ++i) stageShot(c, i);
    auto early = std::make_unique<SessionDiagnosticsModel>();
    early->setCadence(QStringLiteral("everyShot"));
    early->setBaselineAuthority(SessionDiagnosticsModel::BaselineAuthority::Always);
    early->activateSession(c);
    early->declareMiss(QStringLiteral("slice"));                    // joins the freeze first
    check(early->baselineFrozen() && QFile::exists(SessionFiles::clubBaselineFile(c)),
          "B6 a golfer command during the freeze waits for it: the baseline is frozen first");
    early->waitForIdle(60000);
    check(early->shotCount() == 3 && early->declaredMiss() == QStringLiteral("slice")
              && baselineWithoutStamp(c) == baselineWithoutStamp(a),
          "B7 …and the session then grades as it would have");
    // Another session while a freeze reads: the first is let go, nothing of it half done.
    const QString d = makeSession(tmp, "athlete_freeze", "2026-11-03_M_04");
    for (int i = 1; i <= 3; ++i) stageShot(d, i);
    auto moved = std::make_unique<SessionDiagnosticsModel>();
    moved->setBaselineAuthority(SessionDiagnosticsModel::BaselineAuthority::Always);
    // Each busyChanged with what busy() said at it.
    std::vector<bool> heardBusy;
    SessionDiagnosticsModel *mp = moved.get();
    QObject::connect(mp, &SessionDiagnosticsModel::busyChanged, mp, [&heardBusy, mp] { heardBusy.push_back(mp->busy()); });
    moved->activateSession(d);
    check(moved->busy() && heardBusy == std::vector<bool>{ true }, "B8 a freeze in flight is announced (busyChanged, busy)");
    const size_t beforeAbandon = heardBusy.size();
    moved->activateSession(a);                                      // a is frozen: no read needed
    // The FIRST signal after the re-activation is the abandon's, and says not busy — before anything of the
    // new session is queued (whose own signals say busy).
    check(heardBusy.size() > beforeAbandon && !heardBusy[beforeAbandon],
          "B9 letting the freeze go is announced too (busyChanged, not busy), so a listener waiting on busy() hears it");
    check(moved->waitForIdle(60000) && !moved->busy() && moved->sessionDir() == a,
          "B10 re-pointed during a freeze: the model settles on the new session");
}

// ── O: an old envelope reads identically ────────────────────────────────────────────────────────────

static void oldEnvelopeSuite(const QTemporaryDir &tmp)
{
    std::printf("\nO: an envelope written before step 2.5 reads identically\n");
    // Two copies of one legacy session: one read as it stands, one whose older keys are first copied to the new
    // homes by hand (as M3 will). Both must give the model the same input.
    const QString a = makeLegacySession(tmp, "2026-11-11_M_01");
    const QString b = makeLegacySession(tmp, "2026-11-11_M_02");
    pinpoint::SessionInput in;
    in.declaredFocus = pinpoint::DeclaredFocus{ QStringLiteral("over_the_top"), 1 };
    in.declaredMiss  = QStringLiteral("slice");
    in.screenAnswers = QMap<QString, bool>{ { QStringLiteral("early_extension"), true } };
    in.intentRuns    = QVector<pinpoint::IntentRun>{ pinpoint::IntentRun{ 2, QStringLiteral("fade") } };
    check(SessionDiagnosticsFiles::writeSessionInput(b, in).ok(), "O1 the moved copy's session_input.json");
    QJsonObject env = jsonOf(SessionFiles::diagnosticsFile(b));
    for (const QString &k : SessionDiagnosticsFiles::legacyGolferKeys())
        if (k != QLatin1String("shotIntent") && k != QLatin1String("corrections")) env.remove(k);
    writeJson(SessionFiles::diagnosticsFile(b), env);
    auto ma = freshModel();
    ma->activateSession(a);
    auto mb = freshModel();
    mb->activateSession(b);
    check(describe(ma->golferInputHeld()) == describe(mb->golferInputHeld()),
          "O2 the older shape and the new home give the model the same input");
    check(ma->stage() == mb->stage() && ma->patternCount() == mb->patternCount(), "O3 …and the same reduction");
}

// ── R: a newer envelope is read-only ────────────────────────────────────────────────────────────────

static void newerSuite(const QTemporaryDir &tmp)
{
    std::printf("\nR: a diagnostics.json a newer build saved is read, never written\n");
    const QString dir = makeLegacySession(tmp, "2026-11-12_M_01");
    QJsonObject env = jsonOf(SessionFiles::diagnosticsFile(dir));
    env.insert(QStringLiteral("schemaVersion"), SessionDiagnosticsFiles::kEnvelopeVersion + 1);
    env.insert(QStringLiteral("fromTheFuture"), true);
    writeJson(SessionFiles::diagnosticsFile(dir), env);
    const QByteArray before = bytesOf(SessionFiles::diagnosticsFile(dir));
    check(SessionDiagnosticsFiles::readDiagnostics(dir).state == pinpoint::DiagnosticsRead::State::Newer,
          "R1 the reader says it is a newer build's");

    auto m = freshModel();
    QStringList blocked;
    QObject::connect(m.get(), &SessionDiagnosticsModel::ledgerWritesBlocked, m.get(),
                     [&blocked](const QString &, const QString &why) { blocked << why; });
    m->activateSession(dir);
    m->declareMiss(QStringLiteral("hook"));
    QCoreApplication::processEvents();
    check(m->shotCount() == 4 && m->declaredMiss() == QStringLiteral("hook"), "R2 it is read, and the panel still works in memory");
    check(bytesOf(SessionFiles::diagnosticsFile(dir)) == before, "R3 diagnostics.json is not written, byte for byte");
    check(!QFile::exists(SessionFiles::sessionInputFile(dir)), "R4 …nor is anything else in the session");
    check(blocked.size() == 1 && blocked.first().contains(QStringLiteral("newer")), "R5 the golfer is told, once");
    check(!SessionDiagnosticsFiles::writeDiagnostics(dir, QJsonObject(), QJsonObject(), QJsonObject()).ok()
              && bytesOf(SessionFiles::diagnosticsFile(dir)) == before,
          "R6 the one writer refuses it too (ReadOnly)");
}

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
    legacySuite(tmp);
    oldEnvelopeSuite(tmp);
    newerSuite(tmp);
    filesSuite(tmp);
    carrySuite(tmp);
    freezeSuite(tmp);
    std::printf("\n%s: %d failure(s)\n", g_fail ? "FAIL" : "PASS", g_fail);
    return g_fail ? 1 : 0;
}
