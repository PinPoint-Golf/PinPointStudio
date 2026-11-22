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

// session_series_test — THE SESSION'S OWN SERIES (refactor 5.5a; src/Gui/charts/session_series.h; DUTIES-5.5 §B, §D;
// re-pointed from charts_series_model_test's owner-facing cases). Over 5.4's owner rig (owner_rig.h): an Athlete on a
// temporary library holds real Sessions whose SessionDiagnostics judge each shot (synchronous doors), and the series is
// attached with every Session through the same hook, as main.cpp attaches it.
//   ATTACH     every Session gets one series, its child; two asks get the same one (VIEWS); nothing is read until asked
//   READ       the first ask reads each shot once: ordinal by swing number, shotId the swing number, the session folder
//   JOIN       each shot's findings are the owner's rows (diagnosisInput), the tiers its ledgers', the order its uncapped one
//   UNCHANGED  a re-join reads no document again (a stat each)
//   EVT01      a live shot appends; the revision moves
//   EVT04      a board club override reads that shot again and shows the board's club
//   EVT03      a trashed swing leaves the snapshot
//   EVT13      a saved pack edit republishes at once (no relaunch)
//   BOUND      a third series read empties the oldest one nobody listens to
//   OLDNEW     the old view-owned loader (FolderSeries, a test double since 5.5a) and the series agree, shot by shot
//   NOWAIT     releasing a Session while its series' read is in flight on the pool returns at once
//
//   cmake --build build/tests --target session_series_test -j 6
//   ctest --test-dir build/tests -R '^session_series_test$' --output-on-failure

#include "../../Diagnostics/tests/owner_rig.h"

#include "../charts/session_series.h"
#include "../charts/session_series_extract.h"
#include "folder_series.h"
#include "../../Export/rule_data_install.h"
#include "../../Export/series_store.h"

#include <QCoreApplication>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <mutex>

using namespace pinpoint;
using pinpoint::charts::SessionSeries;
using pinpoint::charts::SessionSnapshot;

static int g_fail = 0;
static void check(const QString &what, bool ok)
{
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", qPrintable(what));
    if (!ok) ++g_fail;
}

static QString fixture(const char *name) { return QStringLiteral(PP_LIVE_SWINGS_DIR "/%1/swing.json").arg(QLatin1String(name)); }

// The rig's Athlete, its doors' attach also attaching the series (as main.cpp does).
static void attachSeries(owner_rig::ViewRig &rig, const charts::SeriesDoors &doors)
{
    DiagnosticsDoors d = rig.athlete->diagnosticsDoors();
    d.attach = [doors](Session *s, const DiagnosticsDoors &dd) {
        SessionDiagnostics *o = SessionDiagnostics::attach(s, dd);
        SessionSeries::attach(s, doors);
        return o;
    };
    rig.athlete->setDiagnosticsDoors(d);
}

// A reader whose reads wait at a gate (NOWAIT).
struct GatedReader final : SeriesReader {
    std::shared_ptr<const SeriesReader> inner;
    std::shared_ptr<std::mutex> m = std::make_shared<std::mutex>();
    std::shared_ptr<std::condition_variable> cv = std::make_shared<std::condition_variable>();
    std::shared_ptr<bool> open = std::make_shared<bool>(false);
    std::shared_ptr<std::atomic<int>> entered = std::make_shared<std::atomic<int>>(0);
    DocumentStamp stamp(const ShotRef &s) const override { return inner->stamp(s); }
    SeriesRead analysis(const ShotRef &s) const override
    {
        entered->fetch_add(1);
        std::unique_lock<std::mutex> lk(*m);
        cv->wait(lk, [this] { return *open; });
        return inner->analysis(s);
    }
    void release() const { { std::lock_guard<std::mutex> lk(*m); *open = true; } cv->notify_all(); }
};

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    std::printf("session_series_test\n");
    RuleData &rules = installProcessRuleData();
    if (!rules.snapshot() || !rules.snapshot()->pack) { check(QStringLiteral("the shipped pack is read"), false); return 1; }

    QTemporaryDir tmp;
    owner_rig::ViewRig rig(QDir(tmp.path()).filePath(QStringLiteral("lib/Series-Golfer")));
    charts::SeriesDoors doors;
    doors.reader      = std::make_shared<SwingStoreSeriesReader>(rig.store);
    doors.ruleData    = &rules;
    doors.synchronous = true;
    attachSeries(rig, doors);

    std::printf("\nATTACH, VIEWS — one per Session, read only when asked\n");
    Session *s = rig.startLive();
    check(QStringLiteral("a live session"), s != nullptr);
    if (!s) return 1;
    SessionSeries *series = SessionSeries::of(s);
    check(QStringLiteral("ATTACH  the Session has a series, its child"), series && series->parent() == s);
    check(QStringLiteral("VIEWS   two asks get the same series"), series && SessionSeries::of(s) == series
                                                            && charts::ChartsSeries().of(s) == series);
    const char *layout[] = { "rich_7iron", "rich_7iron", "sparse_noclub", "rich_7iron", "lm_7iron" };
    for (const char *f : layout) rig.addLive(s, fixture(f));
    check(QStringLiteral("ATTACH  nothing read before a reader asks"), series && !series->active() && series->loadStats().docReads == 0);
    if (!series) return 1;

    std::printf("\nREAD — the first ask\n");
    std::shared_ptr<const SessionSnapshot> snap = series->snapshot();
    const int analysed = snap->count();
    std::printf("    %d shots, %d skipped, %d documents read\n", analysed, series->skippedCount(), series->loadStats().docReads);
    check(QStringLiteral("READ    every swing read once"), series->loadStats().docReads == int(std::size(layout)));
    check(QStringLiteral("READ    every swing is in the snapshot or counted as skipped"), analysed + series->skippedCount() == int(std::size(layout)));
    bool order = analysed > 0;
    for (int i = 0; i < analysed; ++i)
        order = order && snap->shots[size_t(i)]->ordinal == i + 1 && snap->shots[size_t(i)]->shotId == charts::folderShotId(snap->shots[size_t(i)]->swingDir);
    check(QStringLiteral("READ    ordinal by swing number, shotId the swing number"), order);
    check(QStringLiteral("READ    the session folder"), snap->sessionDir == QDir::cleanPath(s->entry()->folder));

    std::printf("\nJOIN — the owner's findings\n");
    SessionDiagnostics *owner = s->diagnostics();
    bool joined = owner && owner->diagnosisInput();
    if (joined) {
        const auto pack = rules.snapshot()->pack;
        for (const auto &shot : snap->shots) {
            const analysis::ShotRecord *row = nullptr;
            for (const analysis::ShotRecord &r : owner->diagnosisInput()->shots) if (r.shotId == shot->shotId) row = &r;
            joined = joined && row && charts::seriesSameDiagnostics(shot->diagnostics, charts::seriesDiagnosticsOf(*row, &pack->pack()));
        }
    }
    check(QStringLiteral("JOIN    each shot's findings are its owner row's"), joined);
    bool tiers = owner && owner->diagnosis() && !snap->tiers.isEmpty();
    if (tiers) for (const analysis::ConditionLedger &l : owner->diagnosis()->ledgers) tiers = tiers && snap->tiers.contains(l.id);
    check(QStringLiteral("JOIN    the tiers are the owner's ledgers'"), tiers);
    check(QStringLiteral("JOIN    the work-on order is the owner's uncapped one"), owner && owner->diagnosis() && snap->workOnOrder == owner->diagnosis()->workOnOrder);

    std::printf("\nUNCHANGED — a re-join reads nothing again\n");
    const int readsBefore = series->loadStats().docReads;
    owner->fireAll();
    owner_rig::ViewRig::settle(owner);
    (void)series->snapshot();
    check(QStringLiteral("UNCHANGED  a re-diagnosis costs no document read"), series->loadStats().docReads == readsBefore);

    std::printf("\nEVT01 — a live shot appends\n");
    const quint64 rev = series->snapshot()->revision;
    rig.addLive(s, fixture("rich_7iron"));
    snap = series->snapshot();
    check(QStringLiteral("EVT01   one more shot, read once"), snap->count() == analysed + 1 && series->loadStats().docReads == readsBefore + 1);
    check(QStringLiteral("EVT01   the revision moved"), snap->revision != rev);

    std::printf("\nEVT04 — the board's club\n");
    Shot *first = s->shot(ShotRef(s->id(), snap->shots.front()->shotId));
    const int readsBefore4 = series->loadStats().docReads;
    const WriteResult w = first ? first->correctClub(QStringLiteral("9 IRON")) : WriteResult{};
    snap = series->snapshot();
    check(QStringLiteral("EVT04   the override was written"), w.ok());
    check(QStringLiteral("EVT04   that shot was read again"), series->loadStats().docReads == readsBefore4 + 1);
    check(QStringLiteral("EVT04   and shows the board's club (%1)").arg(snap->shots.front()->club),
          snap->shots.front()->club.contains(QLatin1String("9"), Qt::CaseInsensitive));

    std::printf("\nEVT03 — a trashed swing leaves\n");
    const int before3 = snap->count();
    const WriteResult t = s->trashShot(ShotRef(s->id(), snap->shots.back()->shotId));
    snap = series->snapshot();
    check(QStringLiteral("EVT03   trashed, and one shot fewer"), t.ok() && snap->count() == before3 - 1);

    std::printf("\nEVT13 — a saved pack edit\n");
    const quint64 rev13 = snap->revision;
    emit rules.packChanged(QStringLiteral("test"));
    check(QStringLiteral("EVT13   the series republished at once"), series->snapshot()->revision != rev13);

    std::printf("\nOLDNEW — the old loader and the series agree\n");
    {
        if (owner) owner->flush(true);
        charts::FolderSeries old;
        old.setSynchronous(true);
        old.activateSession(s->entry()->folder);
        const auto a = old.snapshot(), b = series->snapshot();
        bool same = a && b && a->count() == b->count();
        for (int i = 0; same && i < b->count(); ++i) {
            const auto &x = a->shots[size_t(i)], &y = b->shots[size_t(i)];
            same = x->swingDir == y->swingDir && x->ordinal == y->ordinal && x->shotId == y->shotId && x->club == y->club
                && x->metricOrder == y->metricOrder && x->impactUs == y->impactUs
                && x->diagnostics.fired == y->diagnostics.fired && x->diagnostics.clean == y->diagnostics.clean;
            if (!same) std::printf("    shot %d differs: %s / %s\n", i + 1, qPrintable(x->club), qPrintable(y->club));
        }
        int tierDiff = 0;
        for (auto it = a->tiers.constBegin(); it != a->tiers.constEnd(); ++it) if (b->tiers.value(it.key()) != it.value()) ++tierDiff;
        std::printf("    tiers: old %lld, new %lld (D2: the owner's adds its session-scope conditions); %d differ\n",
                    (long long)a->tiers.size(), (long long)b->tiers.size(), tierDiff);
        check(QStringLiteral("OLDNEW  same shots, series, clubs and fired/clean sets"), same);
        check(QStringLiteral("OLDNEW  every tier the old one names, the series names the same"), tierDiff == 0);
    }

    std::printf("\nBOUND — the two most recently read keep their shots\n");
    {
        Session *s2 = rig.startLive();
        rig.addLive(s2, fixture("rich_7iron"));
        (void)SessionSeries::of(s2)->snapshot();
        Session *s3 = rig.startLive();
        rig.addLive(s3, fixture("rich_7iron"));
        (void)SessionSeries::of(s3)->snapshot();
        check(QStringLiteral("BOUND   the oldest (no view) is emptied"), !series->active());
        check(QStringLiteral("BOUND   and reads again when asked"), series->snapshot()->count() > 0 && series->active());
    }

    std::printf("\nNOWAIT — a release never waits for a read in flight\n");
    {
        QTemporaryDir tmp2;
        owner_rig::ViewRig rig2(QDir(tmp2.path()).filePath(QStringLiteral("lib/Gated-Golfer")));
        auto gated = std::make_shared<GatedReader>();
        gated->inner = std::make_shared<SwingStoreSeriesReader>(rig2.store);
        charts::SeriesDoors async;
        async.reader = gated;
        async.ruleData = &rules;
        attachSeries(rig2, async);
        Session *g = rig2.startLive();
        rig2.addLive(g, fixture("rich_7iron"));
        const QString folder = g->entry()->folder;
        (void)SessionSeries::of(g)->snapshot();
        owner_rig::pump([&] { return gated->entered->load() > 0; }, 5000);
        QElapsedTimer t2;
        t2.start();
        rig2.release(folder);
        const qint64 ms = t2.elapsed();
        gated->release();
        owner_rig::pump([] { return false; }, 200);
        check(QStringLiteral("NOWAIT  released in %1 ms with its read still waiting").arg(ms), ms < 1000);
    }

    std::printf("\n%s (%d failure%s)\n", g_fail == 0 ? "ALL PASS" : "FAILURES", g_fail, g_fail == 1 ? "" : "s");
    return g_fail == 0 ? 0 : 1;
}
