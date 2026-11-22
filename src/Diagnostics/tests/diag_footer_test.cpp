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

// diag_footer_test — A FINISHED SESSION'S DRIVER FOOTER IS ITS FINAL DIAGNOSIS'S (refactor 5.4x;
// build/refactor/step-5/NOTES-5.4.md, "5.4x").
//
// The adapter explains the session's patterns (explain(), the driver footer) only when the pattern set's MEMBERSHIP
// changes, live (§A5: no re-rank under the golfer's eyes). explain() also reads each condition's P(Pattern) and the
// assessed set, so a footer kept from earlier in the session could name a root the final evidence does not: after End
// the panel's footer differed from a reload of the same session (5.4a's round trip found it). A finished session's footer
// is definitive (§B7), so at Closing it is explained again from every new diagnosis.
//
//   END   a live session of six shots (five rich, one sparse), the panel's adapter bound throughout; after End its
//         footer is the one an adapter made fresh from the same diagnosis gives, and the one a reload gives
//   COUNT the ended session's shots trashed one by one (EVT-03): at every step the panel's footer is a fresh adapter's,
//         at least one step changing the counts but not the pattern set
//   LIVE  live, the footer still moves only with the pattern set's membership (§A5 unchanged)
//
//   cmake --build build/tests --target diag_footer_test -j 6
//   ctest --test-dir build/tests -R '^diag_footer_test$' --output-on-failure

#include "../../DomainModel/athlete.h"
#include "../../DomainModel/session.h"
#include "../../DomainModel/session_diagnostics.h"
#include "../../DomainModel/shot.h"
#include "../../Core/work_pool.h"
#include "../../Export/firing_reader_store.h"
#include "../../Export/recording_loader.h"
#include "../../Export/rule_data_install.h"
#include "../../Export/session_store.h"
#include "../../Export/swing_store_repository.h"
#include "../../Gui/diagnostics/session_diagnostics_adapter.h"
#include "../../Gui/diagnostics/session_diagnostics_model.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <QTimeZone>

#include <cstdio>
#include <functional>
#include <memory>

#ifndef PP_LIVE_SWINGS_DIR
#  define PP_LIVE_SWINGS_DIR "."
#endif

using namespace pinpoint;

static int g_fail = 0;
static int g_run  = 0;
static void check(const QString &label, bool ok)
{
    ++g_run;
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", qPrintable(label));
    if (!ok) ++g_fail;
}

static bool pump(const std::function<bool()> &done, int ms = 30000)
{
    QElapsedTimer t;
    t.start();
    while (!done()) {
        if (t.elapsed() > ms) return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QThread::msleep(1);
    }
    return true;
}
static void spin() { pump([] { return false; }, 30); }

static const AthleteId kAthlete = AthleteId::fromString(u"3e8f1a52-7c4d-4b19-9a06-2d5c8e1f7b43");
static const QString   kName    = QStringLiteral("Footer Golfer");
static const char     *kLayout[] = { "rich_7iron", "rich_7iron", "rich_7iron", "rich_7iron", "rich_7iron", "sparse_noclub" };
static constexpr int   kShots   = 6;

static QString fixture(const char *name)
{
    return QDir(QLatin1String(PP_LIVE_SWINGS_DIR)).filePath(QLatin1String(name) + QLatin1String("/swing.json"));
}

// One scratch library; the owner on synchronous doors with the real reader, persisting into it.
struct Rig {
    QTemporaryDir                                  dir;
    SessionStore                                   store;
    std::unique_ptr<SwingStoreRecordingRepository> recordings;
    std::shared_ptr<RecordingReader>               reader;
    std::unique_ptr<WorkReceiver>                  receiver;
    std::unique_ptr<SessionDiagnosticsAdapters>    adapters;
    std::unique_ptr<Athlete>                       athlete;   // destroyed first

    Rig()
    {
        store.setLockWaitForTest(200);
        store.setOsTrashForTest(false);
        recordings = std::make_unique<SwingStoreRecordingRepository>(store);
        reader     = std::make_shared<SwingStoreRecordingReader>(store);
        receiver   = std::make_unique<WorkReceiver>([this](const WorkAddress &a) {
            return diagnosticsWorkTarget(athlete ? athlete->loadedSession(SessionId::fromString(a.owner)) : nullptr, a);
        });
        adapters = std::make_unique<SessionDiagnosticsAdapters>();
        const QString folder = dir.path() + QStringLiteral("/Footer-Golfer");
        QDir().mkpath(folder);
        athlete = std::make_unique<Athlete>(nullptr, &store, kAthlete);
        AthleteRecord r;
        r.name = kName;
        athlete->setStored(r, folder, SessionOwner{ kAthlete, kName, dir.path() }, Athlete::Source::Library, false);
        athlete->setShotDoors({ recordings.get(), reader });
        DiagnosticsDoors d;
        d.receiver        = receiver.get();
        d.ruleData        = RuleData::installed();
        d.reader          = std::make_shared<SwingStoreFiringReader>(store, *recordings);
        d.persist         = true;
        d.synchronous     = true;
        d.writeDebounceMs = 0;
        d.attach          = &SessionDiagnostics::attach;
        athlete->setDiagnosticsDoors(d);
    }
    ~Rig()
    {
        if (athlete)
            for (Session *s : athlete->loadedSessions())
                for (SessionDiagnosticsModel *m : s->findChildren<SessionDiagnosticsModel *>(Qt::FindDirectChildrenOnly))
                    while (m->views() > 0) m->dropView();
        athlete.reset();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        adapters.reset();
    }
    static void settle(SessionDiagnostics *d) { if (d) pump([d] { return d->idle() && !d->writePending(); }, 20000); }
    // A shot: the fixture's document in the swing folder the Session allocated, its mtime pinned, then EVT-01.
    Shot *add(Session *s, const char *name, int n)
    {
        Shot *shot = s ? s->addShot() : nullptr;
        if (!shot) return nullptr;
        const QString doc = QDir(shot->folder()).filePath(QStringLiteral("swing.json"));
        if (!QFile::copy(fixture(name), doc)) return nullptr;
        QFile f(doc);
        f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        if (f.open(QIODevice::ReadWrite)) {
            f.setFileTime(QDateTime::fromMSecsSinceEpoch(1767225600000LL + qint64(n) * 1000, QTimeZone::UTC),
                          QFileDevice::FileModificationTime);
            f.close();
        }
        s->shotDocumentWritten(shot->ref());
        settle(s->diagnostics());
        return shot;
    }
    // An adapter of `s`, bound and pointed as a panel binds it.
    SessionDiagnosticsModel *view(Session *s, const QString &surface)
    {
        SessionDiagnosticsModel *m = s ? adapters->of(s, surface) : nullptr;
        if (!m) return nullptr;
        if (m->views() == 0) m->addView();
        m->setSynchronous(true);
        m->setCadence(QStringLiteral("everyShot"));
        m->activateSession(s->entry() ? s->entry()->folder : QString());
        spin();
        return m;
    }
};

static QByteArray footerOf(const SessionDiagnosticsModel *m)
{
    return m ? QJsonDocument::fromVariant(m->driver()).toJson(QJsonDocument::Compact) : QByteArray();
}
static QString footerWords(const SessionDiagnosticsModel *m)
{
    if (!m) return QStringLiteral("-");
    const QVariantMap d = m->driver();
    return d.value(QStringLiteral("rootId")).toString() + QStringLiteral(" · ") + d.value(QStringLiteral("whyText")).toString()
         + d.value(QStringLiteral("finalText")).toString() + d.value(QStringLiteral("waitingText")).toString();
}
static QStringList patternsOf(const SessionDiagnosticsModel *m)
{
    QStringList out;
    if (m)
        for (const analysis::ConditionLedger &l : m->ledgerRows())
            if (l.tier == analysis::Tier::Pattern) out << l.id;
    out.sort();
    return out;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    std::printf("diag_footer_test\n");
    RuleData &rules = installProcessRuleData();
    check(QStringLiteral("the shipped pack is read"), rules.snapshot() && rules.snapshot()->pack);
    if (!rules.snapshot() || !rules.snapshot()->pack) return 1;

    Rig rig;
    Session *s = rig.athlete->startSession(QStringLiteral("swing"), QStringLiteral("date-name-type"));
    SessionDiagnosticsModel *panel = rig.view(s, QStringLiteral("panel"));
    if (!s || !panel) { check(QStringLiteral("a live session and its panel's adapter"), false); return 1; }
    const QString folder = s->entry() ? s->entry()->folder : QString();

    // ── LIVE: the footer moves only with the pattern set's membership (§A5) ──
    std::printf("\nLIVE — the footer follows the pattern set's membership while the session is recorded\n");
    QStringList lastSet;
    QString     lastRoot;
    int liveSteps = 0, liveHeld = 0;
    for (int i = 0; i < kShots; ++i) {
        check(QStringLiteral("shot %1 added").arg(i + 1), rig.add(s, kLayout[i], i + 1) != nullptr);
        spin();
        const QStringList set = patternsOf(panel);
        const QString root = panel->driver().value(QStringLiteral("rootId")).toString();
        std::printf("    shot %d: %lld patterns · %s\n", i + 1, (long long)set.size(), qPrintable(footerWords(panel)));
        if (i > 0 && set == lastSet && !set.isEmpty()) {
            ++liveSteps;
            liveHeld += root == lastRoot || lastRoot.isEmpty() ? 1 : 0;   // (a footer still waiting names no root yet)
        }
        lastSet = set;
        lastRoot = root;
    }
    check(QStringLiteral("LIVEa live, a shot that keeps the pattern set keeps the footer's root (%1 of %2)").arg(liveHeld).arg(liveSteps),
          liveHeld == liveSteps);

    // ── END ──
    std::printf("\nEND — after End the panel's footer is the final diagnosis's\n");
    const QStringList setBeforeEnd = patternsOf(panel);
    const QByteArray  liveFooter   = footerOf(panel);
    check(QStringLiteral("ENDa the session ends (EVT-09)"), s->end().ok());
    Rig::settle(s->diagnostics());
    spin();
    check(QStringLiteral("ENDa2 End keeps the pattern set (%1 patterns), so only the definitive rule can move the footer")
              .arg(setBeforeEnd.size()),
          !setBeforeEnd.isEmpty() && patternsOf(panel) == setBeforeEnd);
    SessionDiagnosticsModel *fresh = rig.view(s, QStringLiteral("footer-fresh"));
    std::printf("    panel: %s\n    fresh: %s\n", qPrintable(footerWords(panel)), qPrintable(footerWords(fresh)));
    check(QStringLiteral("ENDb the panel shows Closing"), panel->stage() == QLatin1String("closing"));
    check(QStringLiteral("ENDc its footer is the one an adapter made from the same diagnosis gives"),
          fresh && footerOf(panel) == footerOf(fresh));
    const QByteArray endFooter = footerOf(panel);
    if (fresh) fresh->dropView();
    panel->dropView();
    const SessionId id = s->id();
    if (s->diagnostics()) s->diagnostics()->flush(/*final=*/true);
    check(QStringLiteral("ENDd released"), rig.athlete->releaseSession(id));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    Session *again = rig.athlete->session(id);
    Rig::settle(again ? again->diagnostics() : nullptr);
    SessionDiagnosticsModel *reloaded = rig.view(again, QStringLiteral("panel"));
    std::printf("    reloaded: %s\n", qPrintable(footerWords(reloaded)));
    check(QStringLiteral("ENDe …and the one a reload of the session gives"), reloaded && footerOf(reloaded) == endFooter);
    check(QStringLiteral("ENDf …and this fixture's final footer is not the live one (the case the fix is for)"),
          endFooter != liveFooter);
    if (!again || !reloaded) return 1;

    // ── COUNT ──
    std::printf("\nCOUNT — the ended session's counts change; its footer follows each diagnosis\n");
    const QList<Shot *> shots = again->shots();
    int steps = 0, equal = 0, sameSet = 0, sameSetMoved = 0;
    for (int i = int(shots.size()) - 1; i >= 3; --i) {
        const QStringList before = patternsOf(reloaded);
        const QByteArray  footerBefore = footerOf(reloaded);
        const int swing = shots.at(i)->swingNumber();
        again->trashShot(shots.at(i)->ref());   // EVT-03 (the Shot goes with it)
        Rig::settle(again->diagnostics());
        spin();
        SessionDiagnosticsModel *truth = rig.view(again, QStringLiteral("footer-truth-%1").arg(i));
        const bool set = patternsOf(reloaded) == before;
        const bool eq  = truth && footerOf(reloaded) == footerOf(truth);
        ++steps;
        equal   += eq;
        sameSet += set;
        sameSetMoved += set && truth && footerOf(truth) != footerBefore;
        std::printf("    swing %d trashed: patterns %s · panel %s · fresh %s\n", swing,
                    set ? "unchanged" : "changed", qPrintable(footerWords(reloaded)), qPrintable(footerWords(truth)));
        if (truth) truth->dropView();
    }
    check(QStringLiteral("COUNTa at every step the panel's footer is a fresh adapter's (%1 of %2)").arg(equal).arg(steps),
          steps > 0 && equal == steps);
    check(QStringLiteral("COUNTb …at least one step kept the pattern set and still moved the footer (%1 of %2 kept it)")
              .arg(sameSetMoved).arg(sameSet),
          sameSetMoved >= 1);
    reloaded->dropView();

    std::printf("\n%d checks, %d failed\n", g_run, g_fail);
    return g_fail == 0 ? 0 : 1;
}
