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

// review_views_shot_test — THE REPLAY AND THE MARKUP READ THROUGH THE SHOT (refactor 5.7c, 5.7d;
// build/refactor/step-4/LOCAL-COPIES.md rows 13 and 14). Over 5.4's owner rig (owner_rig.h): an Athlete on a temporary
// library holds a real Session and its Shots, and `app` is the door, as in the app.
//   REPLAY-FOLLOW   the replay follows the replayed folder's Shot (the composition root's edge)
//   REPLAY-EVT02    a real re-analysis (Shot::applyAnalysis) during a replay: the slice is read again through the Shot's
//                   RecordingSource and follows (the new impact), with no reload: no activeChanged, analysisDetailChanged once
//   REPLAY-DROP     a read still in flight when the replay stops is dropped
//   REPLAY-SOURCE   DiskReplaySource::applyAnalysis: the impact, the slice and the span follow; streams and playhead stay
//   MARKUP-SEEN     a truth written by another path (the Shot's own command) is what the markup tool's rows show, with no
//                   save of its own; its save is the Shot's (read back by Shot::markupTruth)
//   MARKUP-UNHELD   a swing no Session holds is not read, and its save is refused (N1 gone)
//
//   cmake --build build/tests --target review_views_shot_test -j 3
//   ctest --test-dir build/tests -R '^review_views_shot_test$' --output-on-failure

#include "../../Diagnostics/tests/owner_rig.h"

#include "../shot/shot_replay_controller.h"
#include "../shot/disk_replay_source.h"
#include "../markup/markup_controller.h"
#include "../markup/markup_truth.h"
#include "../../Analysis/swing_analysis.h"
#include "../../Export/rule_data_install.h"
#include "../../Export/swing_doc.h"

#include <QCoreApplication>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <cstdio>

using namespace pinpoint;

static int g_fail = 0;
static void check(const QString &what, bool ok)
{
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", qPrintable(what));
    if (!ok) ++g_fail;
}

static QString fixture(const char *name) { return QStringLiteral(PP_LIVE_SWINGS_DIR "/%1/swing.json").arg(QLatin1String(name)); }

// The replay checks only that a stream's file exists before it queues it on a player.
static bool touchStreamFiles(const QString &dir)
{
    QFile doc(QDir(dir).filePath(QStringLiteral("swing.json")));
    if (!doc.open(QIODevice::ReadOnly)) return false;
    const QJsonObject root = QJsonDocument::fromJson(doc.readAll()).object();
    for (const QJsonValue &v : root.value(QStringLiteral("streams")).toArray()) {
        const QString f = v.toObject().value(QStringLiteral("file")).toString();
        if (f.isEmpty() || v.toObject().value(QStringLiteral("kind")).toString() != QLatin1String("video")) continue;
        QFile out(QDir(dir).filePath(f));
        if (!out.exists() && !out.open(QIODevice::WriteOnly)) return false;
    }
    return true;
}

static qint64 phaseUs(const QVariantMap &detail, analysis::Phase phase)
{
    for (const QVariant &v : detail.value(QStringLiteral("phases")).toList())
        if (v.toMap().value(QStringLiteral("phase")).toInt() == int(phase))
            return v.toMap().value(QStringLiteral("t_us")).toLongLong();
    return -1;
}

// A source of the test's own: no players (a media backend's own events would tear a real one down while the test waits
// on the lane). Its slice is the one projection, as DiskReplaySource's is.
class FakeSource final : public ReplaySource
{
public:
    QString     loaded;
    QVariantMap detail;
    int         applied = 0;
    bool load(const QString &dir, double, bool) override
    {
        loaded = dir;
        QFile f(QDir(dir).filePath(QStringLiteral("swing.json")));
        if (!f.open(QIODevice::ReadOnly)) return false;
        detail = analysisDetailForDocument(QJsonDocument::fromJson(f.readAll()).object());
        return true;
    }
    void unload() override { loaded.clear(); detail.clear(); }
    bool applyAnalysis(const QJsonObject &an, qint64 originUs) override
    {
        if (loaded.isEmpty()) return false;
        detail = analysisDetailFor(an, originUs);
        ++applied;
        return true;
    }
    int                       streamCount() const override { return loaded.isEmpty() ? 0 : 1; }
    QVector<ReplayStreamInfo> streams() const override { return {}; }
    QVariantMap               analysisDetail() const override { return detail; }
    qint64 startUs() const override { return 0; }
    qint64 endUs() const override { return 1; }
    qint64 impactUs() const override { return phaseUs(detail, analysis::Phase::Impact); }
    qint64 positionUs() const override { return 0; }
    bool   playing() const override { return false; }
    double speed() const override { return 0.25; }
    void setVideoSink(int, QVideoSink *) override {}
    void togglePlay() override {}
    void seekToFraction(double) override {}
    void seekToUs(qint64) override {}
    void stepFrame(int) override {}
    void setSpeed(double) override {}
    void beginScrub() override {}
    void endScrub() override {}
    bool impactLoopPlaying() const override { return true; }
    void toggleImpactLoop() override {}
    qint64 impactPositionUs() const override { return -1; }
    qint64 impactLoopStartUs() const override { return -1; }
    qint64 impactLoopEndUs() const override { return -1; }
    void seekImpactToUs(qint64) override {}
    void stepImpactFrame(int) override {}
};

// A re-analysis whose Address, Impact and Finish are at these window times (µs).
static WriteResult reanalyse(Shot *shot, qint64 addressUs, qint64 impactUs, qint64 finishUs)
{
    analysis::SwingAnalysis fresh;
    fresh.timeBase      = analysis::TimeBase::Window;
    fresh.score.overall = 41;
    for (const auto &[phase, t] : { std::pair{ analysis::Phase::Address, addressUs }, std::pair{ analysis::Phase::Impact, impactUs },
                                    std::pair{ analysis::Phase::Finish, finishUs } }) {
        analysis::PhaseEvent e;
        e.phase = phase;
        e.t_us  = t;
        fresh.phases.push_back(e);
    }
    AnalysisOutcome outcome;
    outcome.analysis = &fresh;
    return shot->applyAnalysis(outcome);
}

static void testReplay(owner_rig::ViewRig &rig, Session *s)
{
    std::printf("\nREPLAY — the slice follows the Shot\n");
    Shot *shot = rig.addLive(s, fixture("rich_7iron"));
    Shot *other = rig.addLive(s, fixture("rich_7iron"));
    check(QStringLiteral("two shots in a live session"), shot && other);
    if (!shot || !other) return;

    auto *fake = new FakeSource;
    ShotReplayController replay(std::unique_ptr<ReplaySource>(fake), nullptr);
    replay.setShotResolver([s](const QString &dir) { return s->shotAtFolder(dir); });
    QSignalSpy active(&replay, &ShotReplayController::activeChanged);
    QSignalSpy detail(&replay, &ShotReplayController::analysisDetailChanged);

    check(QStringLiteral("REPLAY-FOLLOW  the replay starts"), replay.start(shot->swingNumber(), shot->folder(), 0.25, false));
    check(QStringLiteral("REPLAY-FOLLOW  …and follows the replayed folder's Shot"), replay.followedShot() == shot);
    const qint64 before = phaseUs(replay.analysisDetail(), analysis::Phase::Impact);
    const qint64 impact = 1234567;   // window µs, not the fixture's
    check(QStringLiteral("REPLAY-FOLLOW  the slice at load is the document's (impact %1 µs)").arg(before),
          !replay.analysisDetail().value(QStringLiteral("phases")).toList().isEmpty() && before != impact);

    active.clear();
    detail.clear();
    const WriteResult w = reanalyse(shot, impact - 400000, impact, impact + 600000);
    check(QStringLiteral("REPLAY-EVT02  the Shot re-analysed (EVT-02)"), w.ok() && w.changed());
    check(QStringLiteral("REPLAY-EVT02  the read through the Shot settles"), replay.waitForAnalysisRead());
    check(QStringLiteral("REPLAY-EVT02  the impact follows (%1 µs)").arg(phaseUs(replay.analysisDetail(), analysis::Phase::Impact)),
          phaseUs(replay.analysisDetail(), analysis::Phase::Impact) == impact && replay.impactUs() == impact);
    check(QStringLiteral("REPLAY-EVT02  …in place: no reload, no activeChanged, analysisDetailChanged once"),
          fake->applied == 1 && active.isEmpty() && detail.count() == 1 && replay.active() && fake->loaded == shot->folder());

    std::printf("\nREPLAY-DROP — another shot's event, and a read the replay no longer wants\n");
    detail.clear();
    (void)reanalyse(other, 1000000, 1500000, 2000000);
    replay.waitForAnalysisRead();
    check(QStringLiteral("REPLAY-DROP  another shot's re-analysis is not read"), fake->applied == 1 && detail.isEmpty());
    emit shot->reanalysed(shot->ref());   // a read asked…
    replay.stop();                         // …and the replay stopped before it came back
    check(QStringLiteral("REPLAY-DROP  …it comes back"), replay.waitForAnalysisRead());
    check(QStringLiteral("REPLAY-DROP  a read in flight at a stop is dropped, and the Shot let go"),
          fake->applied == 1 && !replay.followedShot());
}

static void testReplaySource()
{
    std::printf("\nREPLAY-SOURCE — DiskReplaySource takes a slice in place\n");
    QTemporaryDir tmp;
    const QString dir = tmp.path();
    const bool made = QFile::copy(fixture("rich_7iron"), QDir(dir).filePath(QStringLiteral("swing.json"))) && touchStreamFiles(dir);
    check(QStringLiteral("REPLAY-SOURCE  a swing folder"), made);
    if (!made) return;
    QFile doc(QDir(dir).filePath(QStringLiteral("swing.json")));
    (void)doc.open(QIODevice::ReadOnly);
    const QJsonObject root = QJsonDocument::fromJson(doc.readAll()).object();

    DiskReplaySource src;
    // No event loop runs between the load and the checks: the empty media file's own events never arrive.
    check(QStringLiteral("REPLAY-SOURCE  loads (trimmed)"), src.load(dir, 0.25, true));
    const qint64 startUs = src.startUs(), endUs = src.endUs(), posUs = src.positionUs();
    const int streams = src.streamCount();
    const qint64 impact = src.impactUs() + 98765;
    QJsonObject an = root.value(QStringLiteral("analysis")).toObject();
    QJsonArray phases;
    for (const QJsonValue &v : an.value(QStringLiteral("phases")).toArray()) {
        QJsonObject p = v.toObject();
        if (p.value(QStringLiteral("phase")).toInt() == int(analysis::Phase::Impact))
            p[QStringLiteral("t_us")] = double(impact + analysisReadOriginUs(root));
        phases.append(p);
    }
    an[QStringLiteral("phases")] = phases;
    QSignalSpy span(&src, &ReplaySource::spanChanged);
    check(QStringLiteral("REPLAY-SOURCE  applyAnalysis takes it"), src.applyAnalysis(an, analysisReadOriginUs(root)));
    check(QStringLiteral("REPLAY-SOURCE  the impact and the slice follow (%1 µs)").arg(src.impactUs()),
          src.impactUs() == impact && phaseUs(src.analysisDetail(), analysis::Phase::Impact) == impact && span.count() == 1);
    check(QStringLiteral("REPLAY-SOURCE  the span, the streams and the playhead stay"),
          src.startUs() == startUs && src.endUs() == endUs && src.positionUs() == posUs && src.streamCount() == streams);
    src.unload();
    check(QStringLiteral("REPLAY-SOURCE  nothing loaded: nothing taken"), !src.applyAnalysis(an, 0));
}

static void testMarkup(owner_rig::ViewRig &rig, Session *s)
{
    std::printf("\nMARKUP — the truth is the Shot's\n");
    Shot *shot = rig.addLive(s, fixture("rich_7iron"));
    check(QStringLiteral("a shot"), shot && touchStreamFiles(shot->folder()));
    if (!shot) return;

    MarkupController mc;
    mc.setShotCommands(rig.app.get());
    mc.loadSwings({ shot->folder() });
    check(QStringLiteral("MARKUP-SEEN  the swing is open, unlabelled"), mc.hasSwing() && mc.labelledCount() == 0);

    // Another path: the Shot's own command, not the markup tool's save.
    const markup::VideoStreamInfo fo = markup::readVideoStream(shot->folder(), markup::MarkupView::FaceOn);
    markup::TruthDoc doc;
    doc.events.insert(markup::eventNames().first(), 10);
    const WriteResult w = shot->setMarkupTruth(TruthView::FaceOn, markup::toJson(doc, fo, markup::MarkupView::FaceOn));
    const QVariantMap row = mc.swings().value(0).toMap();
    check(QStringLiteral("MARKUP-SEEN  a truth written by another path is seen, with no save of the tool's (%1 event)")
              .arg(row.value(QStringLiteral("eventCount")).toInt()),
          w.ok() && mc.labelledCount() == 1 && row.value(QStringLiteral("labelled")).toBool()
              && row.value(QStringLiteral("eventCount")).toInt() == 1);

    mc.revert();   // the edit buffer, read again from the Shot
    mc.setEvent(markup::eventNames().last());
    const bool saved = mc.save();
    const markup::TruthSummary back = markup::summarizeJson(shot->markupTruth(TruthView::FaceOn));
    check(QStringLiteral("MARKUP-SEEN  the tool's save is the Shot's (%1 events read back by Shot::markupTruth)").arg(back.eventCount),
          saved && back.exists && back.eventCount == 2 && mc.labelledCount() == 1);

    std::printf("\nMARKUP-UNHELD — a swing no Session holds\n");
    QTemporaryDir tmp;
    const QString loose = QDir(tmp.path()).filePath(QStringLiteral("swing_0001"));
    QDir().mkpath(loose);
    const bool made = QFile::copy(fixture("rich_7iron"), QDir(loose).filePath(QStringLiteral("swing.json"))) && touchStreamFiles(loose)
                   && QFile::copy(QDir(shot->folder()).filePath(markup::truthFileName(markup::MarkupView::FaceOn)),
                                  QDir(loose).filePath(markup::truthFileName(markup::MarkupView::FaceOn)));
    mc.loadSwings({ loose });
    check(QStringLiteral("MARKUP-UNHELD  its truth file is not read (no Shot)"), made && mc.hasSwing() && mc.labelledCount() == 0);
    mc.setEvent(markup::eventNames().first());
    check(QStringLiteral("MARKUP-UNHELD  its save is refused"), !mc.save());
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    std::printf("review_views_shot_test\n");
    RuleData &rules = installProcessRuleData();
    if (!rules.snapshot() || !rules.snapshot()->pack) { check(QStringLiteral("the shipped pack is read"), false); return 1; }

    testReplaySource();   // first: no event loop has run yet

    QTemporaryDir tmp;
    {
        owner_rig::ViewRig rig(QDir(tmp.path()).filePath(QStringLiteral("lib/Review-Golfer")));
        Session *s = rig.startLive();
        check(QStringLiteral("a live session"), s != nullptr);
        if (s) {
            testReplay(rig, s);
            testMarkup(rig, s);
        }
    }
    std::printf("\n%s (%d failed)\n", g_fail ? "FAIL" : "PASS", g_fail);
    return g_fail ? 1 : 0;
}
