// ForesightPremierMonitor, driven through the file versions Premier really wrote.
//
// Each fixture case (tests/data/premier/<case>/) is a sequence of steps captured on the capture
// PC on 2026-11-02: NN_snapshot.json / NN_queue.json is the next version of that file, and
// NN_snapshot.gone means Premier deleted it. A step is copied into a QTemporaryDir under the name
// Premier uses, its mtime moved on, and pollNow() called — so the monitor sees exactly the
// sequence of writes Premier made, in order, with no event loop and no sleeping.
//
// What the sequences hold (docs/design/foresight_premier_connector.md §2.6):
//   s1 — gap wedge with full stickers ×5, a club change, 5-wood with one sticker ×5, exit to menu
//   s2 — 4-iron with none ×2, a 2.1 mph knock, rapid fire with one shot Premier dropped, a
//        mishit, the knock deleted, six sign shots, exit to menu, upload drains the queue
//   s3 — one shot, then Premier quit mid-session (club data lost), relaunch promotes it

#include "premier_monitor.h"

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimeZone>

using namespace pinpoint::lm;

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

namespace {

const QString kUser = QStringLiteral("00000000-0000-0000-0000-000000000000");

struct Step {
    QString kind;       // "snapshot" | "queue"
    QString source;     // fixture path, empty for a deletion
};

QList<Step> stepsOf(const char *caseName)
{
    const QDir d(QStringLiteral(PP_PREMIER_FIXTURES "/") + QString::fromLatin1(caseName));
    QList<Step> out;
    for (const QString &n : d.entryList(QDir::Files, QDir::Name)) {
        const QString kind = n.section(QLatin1Char('_'), 1).section(QLatin1Char('.'), 0, 0);
        out.append({ kind, n.endsWith(QLatin1String(".gone")) ? QString() : d.filePath(n) });
    }
    return out;
}

// Premier's folder, rebuilt one write at a time.
struct Folder {
    QTemporaryDir dir;
    QDateTime     clock = QDateTime::fromSecsSinceEpoch(1'800'000'000, QTimeZone::UTC);

    QString pathFor(const QString &kind) const
    {
        const QString base = kind == QLatin1String("queue") ? QStringLiteral("SessionUploadQueue")
                                                            : QStringLiteral("SessionLiveSnapshot");
        return dir.filePath(base + QLatin1Char('.') + kUser + QStringLiteral(".json"));
    }

    void write(const QString &kind, const QByteArray &bytes)
    {
        const QString p = pathFor(kind);
        QFile f(p);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        ASSERT_EQ(f.write(bytes), bytes.size());
        clock = clock.addSecs(1);
        // Moved on explicitly: two writes inside one mtime tick must still read as two.
        ASSERT_TRUE(f.setFileTime(clock, QFileDevice::FileModificationTime));
    }

    // ShotReplay/history.json, where Premier keeps its carry (issue #148).
    void writeHistory(const QByteArray &bytes)
    {
        ASSERT_TRUE(QDir(dir.path()).mkpath(QStringLiteral("ShotReplay")));
        QFile f(dir.filePath(ForesightPremierMonitor::historyPath()));
        ASSERT_TRUE(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        ASSERT_EQ(f.write(bytes), bytes.size());
        clock = clock.addSecs(1);
        ASSERT_TRUE(f.setFileTime(clock, QFileDevice::FileModificationTime));
    }
    void writeHistory() { writeHistory(historyFixture()); }
    static QByteArray historyFixture()
    {
        QFile f(QStringLiteral(PP_PREMIER_FIXTURES "/history_20261102.json"));
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    }

    void apply(const Step &s)
    {
        if (s.source.isEmpty()) {
            QFile::remove(pathFor(s.kind));
            return;
        }
        QFile src(s.source);
        ASSERT_TRUE(src.open(QIODevice::ReadOnly)) << s.source.toStdString();
        write(s.kind, src.readAll());
    }
};

struct Rig {
    Folder folder;
    ForesightPremierMonitor m;
    QSignalSpy available{ &m, &LaunchMonitorBase::readingAvailable };
    QSignalSpy updated{ &m, &ForesightPremierMonitor::readingUpdated };

    LaunchMonitorReading availableAt(int i) const { return available.at(i).at(0).value<LaunchMonitorReading>(); }
    LaunchMonitorReading updatedAt(int i)   const { return updated.at(i).at(0).value<LaunchMonitorReading>(); }

    // Applies steps [from, to) before start() (the state already on disk), then starts.
    void startAfter(const QList<Step> &steps, int upTo)
    {
        for (int i = 0; i < upTo; ++i)
            folder.apply(steps[i]);
        m.setSourcePath(folder.dir.path());
        m.start();
    }
    void play(const QList<Step> &steps, int from, int to = -1)
    {
        if (to < 0) to = steps.size();
        for (int i = from; i < to; ++i) {
            folder.apply(steps[i]);
            m.pollNow();
        }
    }
};

const DroppedField *dropFor(const LaunchMonitorReading &r, const char *key)
{
    for (const DroppedField &d : r.dropped)
        if (d.key == QLatin1String(key))
            return &d;
    return nullptr;
}

} // namespace

// ══════════════════════════════════════════════════════ the whole sequences ═══

TEST(PremierMonitor, FullStickersThenOneSticker_EveryShotOnceAndItsClubDataAShotLater)
{
    const auto steps = stepsOf("s1_full_then_one_sticker");
    ASSERT_EQ(steps.size(), 13);
    Rig r;
    r.startAfter(steps, 1);            // the stuck queue is all that is on disk
    EXPECT_EQ(r.m.state(), State::Waiting);
    EXPECT_FALSE(r.m.rangeSessionOpen());

    // The first shot: emitted at once, ball only, nothing claimed missing.
    r.play(steps, 1, 2);
    ASSERT_EQ(r.available.size(), 1);
    EXPECT_EQ(r.updated.size(), 0);
    const LaunchMonitorReading first = r.availableAt(0);
    EXPECT_EQ(first.deviceKind, QStringLiteral("premier"));
    EXPECT_TRUE(first.ballSpeed);
    EXPECT_FALSE(first.clubheadSpeed);
    EXPECT_TRUE(first.dropped.empty());
    EXPECT_TRUE(first.contactAtMs);
    EXPECT_EQ(r.m.state(), State::Ready);
    EXPECT_TRUE(r.m.rangeSessionOpen());

    // The second shot's write carries the first shot's club data.
    r.play(steps, 2, 3);
    ASSERT_EQ(r.available.size(), 2);
    ASSERT_EQ(r.updated.size(), 1);
    const LaunchMonitorReading firstWhole = r.updatedAt(0);
    EXPECT_EQ(firstWhole.deviceShotId, first.deviceShotId);
    EXPECT_EQ(firstWhole.readAtMs, first.readAtMs) << "the update keeps when the shot was first read";
    EXPECT_TRUE(firstWhole.clubheadSpeed);
    EXPECT_TRUE(firstWhole.faceAngle);
    EXPECT_TRUE(firstWhole.dropped.empty());

    // The rest of the session, and the exit to the menu: the queue flush finalises the last shot.
    r.play(steps, 3);
    EXPECT_EQ(r.available.size(), 10);
    EXPECT_EQ(r.updated.size(), 10);
    EXPECT_FALSE(r.m.rangeSessionOpen());
    EXPECT_EQ(r.m.state(), State::Waiting);

    // The 5-woods carried one sticker: speed, attack and path, the rest "not measured".
    const LaunchMonitorReading last = r.updatedAt(9);
    EXPECT_EQ(last.deviceClub, QStringLiteral("5w"));
    EXPECT_TRUE(last.clubheadSpeed);
    EXPECT_TRUE(last.clubPath);
    EXPECT_FALSE(last.faceAngle);
    ASSERT_TRUE(dropFor(last, "lm.faceAngle"));
    EXPECT_EQ(dropFor(last, "lm.faceAngle")->code, QStringLiteral("noData"));

    // Every shot id emitted once, and every update names a shot already emitted.
    QSet<QString> ids;
    for (int i = 0; i < r.available.size(); ++i)
        ids.insert(r.availableAt(i).deviceShotId);
    EXPECT_EQ(ids.size(), 10);
    for (int i = 0; i < r.updated.size(); ++i)
        EXPECT_TRUE(ids.contains(r.updatedAt(i).deviceShotId));
}

TEST(PremierMonitor, NoStickersGhostDroppedShotAndDeletion)
{
    const auto steps = stepsOf("s2_no_stickers_ghost_delete");
    ASSERT_EQ(steps.size(), 19);
    Rig r;
    r.startAfter(steps, 1);
    r.play(steps, 1);

    // 15 shots reached the snapshot: 2 × 4-iron, the 2.1 mph knock, four of the five rapid-fire
    // wedges (Premier dropped one — it never reached disk, so nothing here can see it), the
    // mishit, one more, and six sign shots. The knock is not a shot.
    EXPECT_EQ(r.available.size(), 14);
    for (int i = 0; i < r.available.size(); ++i)
        EXPECT_GE(*r.availableAt(i).ballSpeed, ForesightPremierMonitor::kGhostBallSpeedMph);
    // Every one finalised: 2 with no club data a shot later, 12 with it.
    ASSERT_EQ(r.updated.size(), 14);

    // The 4-irons had no stickers: final a shot later with every club field "not measured".
    const LaunchMonitorReading iron = r.updatedAt(0);
    EXPECT_EQ(iron.deviceClub, QStringLiteral("4i"));
    EXPECT_FALSE(iron.clubheadSpeed);
    ASSERT_TRUE(dropFor(iron, "lm.clubheadSpeed"));
    EXPECT_EQ(dropFor(iron, "lm.clubheadSpeed")->code, QStringLiteral("noData"));

    // The last sign shot (heel) got its club data from the session-end flush.
    const LaunchMonitorReading heel = r.updatedAt(13);
    EXPECT_TRUE(heel.deviceShotId.startsWith(QLatin1String("ad509163")));
    ASSERT_TRUE(heel.strikeLocation);
    EXPECT_NEAR(*heel.strikeLocation, -6.5, 1e-4);

    // And the queue draining afterwards is nothing at all.
    EXPECT_EQ(r.m.state(), State::Waiting);
}

TEST(PremierMonitor, QuitMidSessionLeavesTheLastShotBallOnly)
{
    const auto steps = stepsOf("s3_quit_mid_session");
    ASSERT_EQ(steps.size(), 6);
    Rig r;
    r.startAfter(steps, 1);
    r.play(steps, 1);
    // The shot, once. Premier's quit flush rewrote the snapshot without its club data, and the
    // relaunch queued it that way — "lost" and "no stickers" look the same, so neither is claimed.
    EXPECT_EQ(r.available.size(), 1);
    EXPECT_EQ(r.updated.size(), 0);
}

// ════════════════════════════════════════════════════ what is already there ═══

TEST(PremierMonitor, WhatIsOnDiskAtStartIsNeverClaimed)
{
    // PinPoint starts with Premier mid-session: eight shots already in the snapshot.
    const auto steps = stepsOf("s2_no_stickers_ghost_delete");
    Rig r;
    r.startAfter(steps, 9);
    EXPECT_EQ(r.available.size(), 0);
    EXPECT_EQ(r.m.state(), State::Ready);
    r.m.pollNow();
    EXPECT_EQ(r.available.size(), 0) << "an unchanged file is not a new shot";

    // From here: one more shot, six sign shots, the flush. The shot primed as newest gets its club
    // data with the next write, but it was never ours, so no update names it.
    r.play(steps, 9);
    EXPECT_EQ(r.available.size(), 7);
    EXPECT_EQ(r.updated.size(), 7);
}

TEST(PremierMonitor, AnUnchangedFileIsNotReadAgain)
{
    const auto steps = stepsOf("s1_full_then_one_sticker");
    Rig r;
    r.startAfter(steps, 1);
    r.play(steps, 1, 3);
    const int a = r.available.size(), u = r.updated.size();
    for (int i = 0; i < 5; ++i)
        r.m.pollNow();
    EXPECT_EQ(r.available.size(), a);
    EXPECT_EQ(r.updated.size(), u);
}

TEST(PremierMonitor, TheQueueIsNotOpenedWhileNothingWaitsOnIt)
{
    // Premier opens its upload queue exclusively, and a reader that opens it every tick breaks
    // Premier's upload (§2.6.1). With no shot pending, a queue in a shape we cannot read must go
    // unnoticed — because it was never opened.
    const auto steps = stepsOf("s1_full_then_one_sticker");
    Rig r;
    r.startAfter(steps, 1);
    r.folder.write(QStringLiteral("queue"), "{\"NotWhatWeKnow\": true}");
    r.m.pollNow();
    EXPECT_NE(r.m.state(), State::Error);

    // Once a shot waits on it, it is read on change — and then the shape matters.
    r.play(steps, 1, 2);
    r.folder.write(QStringLiteral("queue"), "{\"NotWhatWeKnow\": false}");
    r.m.pollNow();
    EXPECT_EQ(r.m.state(), State::Error);
}

// ═════════════════════════════════════════════════════════ what goes wrong ═══

TEST(PremierMonitor, ATornWriteIsRetriedAndTheShotStillArrives)
{
    const auto steps = stepsOf("s1_full_then_one_sticker");
    Rig r;
    r.startAfter(steps, 1);
    QFile src(steps[1].source);
    ASSERT_TRUE(src.open(QIODevice::ReadOnly));
    const QByteArray whole = src.readAll();

    r.folder.write(QStringLiteral("snapshot"), whole.left(whole.size() / 2));
    r.m.pollNow();
    EXPECT_EQ(r.available.size(), 0);
    EXPECT_NE(r.m.state(), State::Error) << "half a file is not a format change";

    r.folder.write(QStringLiteral("snapshot"), whole);
    r.m.pollNow();
    EXPECT_EQ(r.available.size(), 1);
}

TEST(PremierMonitor, AChangedFormatIsAnErrorAndRecovers)
{
    const auto steps = stepsOf("s1_full_then_one_sticker");
    Rig r;
    r.startAfter(steps, 1);
    r.folder.write(QStringLiteral("snapshot"), "{\"Session\": {\"Shots\": []}}");
    r.m.pollNow();
    EXPECT_EQ(r.m.state(), State::Error);
    EXPECT_TRUE(r.m.errorText().contains(QLatin1String("format")));
    EXPECT_EQ(r.available.size(), 0);

    r.play(steps, 1, 2);
    EXPECT_EQ(r.m.state(), State::Ready);
    EXPECT_EQ(r.available.size(), 1);
}

TEST(PremierMonitor, AMissingFolderIsAnError)
{
    ForesightPremierMonitor m;
    m.setSourcePath(QStringLiteral("/nonexistent/Foresight Premier"));
    m.start();
    EXPECT_EQ(m.state(), State::Error);

    ForesightPremierMonitor none;
    none.start();
    EXPECT_EQ(none.state(), State::Disabled);
}

// ═══════════════════════════════════════════ carry from history.json (issue #148) ═══

TEST(PremierCarry, EveryFinalReadingCarriesPremiersCarry)
{
    const auto steps = stepsOf("s1_full_then_one_sticker");
    Rig r;
    r.folder.writeHistory();
    r.startAfter(steps, 1);
    r.play(steps, 1);

    ASSERT_EQ(r.available.size(), 10);
    ASSERT_EQ(r.updated.size(), 10) << "the carry rides in the final reading, not one more update";
    for (int i = 0; i < r.available.size(); ++i)
        EXPECT_FALSE(r.availableAt(i).carryDistance) << "a first, ball-only reading is too early for it";
    for (int i = 0; i < r.updated.size(); ++i) {
        const LaunchMonitorReading u = r.updatedAt(i);
        ASSERT_TRUE(u.carryDistance) << u.deviceShotId.toStdString();
        EXPECT_TRUE(u.clubheadSpeed) << "and the club data still with it";
        if (u.deviceShotId.startsWith(QLatin1String("ce9eade0")))
            EXPECT_NEAR(*u.carryDistance, 169.1 / 0.9144, 1e-6);
    }
}

TEST(PremierCarry, ARecordWrittenLaterGoesOutAsOneMoreUpdate)
{
    const auto steps = stepsOf("s1_full_then_one_sticker");
    Rig r;
    r.startAfter(steps, 1);
    r.play(steps, 1, 3);                       // the first shot is final, with no history.json
    ASSERT_EQ(r.updated.size(), 1);
    EXPECT_FALSE(r.updatedAt(0).carryDistance);

    r.folder.writeHistory();
    r.m.pollNow();
    ASSERT_EQ(r.updated.size(), 2);
    const LaunchMonitorReading u = r.updatedAt(1);
    EXPECT_EQ(u.deviceShotId, r.updatedAt(0).deviceShotId);
    EXPECT_TRUE(u.carryDistance);
    EXPECT_TRUE(u.clubheadSpeed) << "the whole final reading, plus its carry";
    EXPECT_EQ(u.readAtMs, r.updatedAt(0).readAtMs);

    r.m.pollNow();
    EXPECT_EQ(r.updated.size(), 2) << "found once, sent once";
}

TEST(PremierCarry, OneTimedRetryThenOnlyTheSweep)
{
    qint64 now = 1'000'000;
    const auto steps = stepsOf("s1_full_then_one_sticker");
    Rig r;
    r.m.setClockForTesting([&] { return now; });
    // A history.json that does not hold this shot yet (it holds nothing).
    r.folder.writeHistory("{\"version\":1,\"maxHistorySize\":100,\"records\":[]}");
    r.startAfter(steps, 1);
    r.play(steps, 1, 3);
    ASSERT_EQ(r.updated.size(), 1);
    EXPECT_FALSE(r.updatedAt(0).carryDistance);

    now += ForesightPremierMonitor::kCarryRetryMs;
    r.m.pollNow();                              // the retry: still not there
    EXPECT_EQ(r.updated.size(), 1);

    // The record lands. The sweep, not the poll, is what finds it here: nothing polls between.
    // It fills the final first shot AND the second, still waiting for its club data.
    r.folder.writeHistory();
    r.m.sweepCarry();
    ASSERT_EQ(r.updated.size(), 3);
    EXPECT_EQ(r.updatedAt(1).deviceShotId, r.updatedAt(0).deviceShotId);
    EXPECT_TRUE(r.updatedAt(1).carryDistance);
    EXPECT_TRUE(r.updatedAt(1).clubheadSpeed);
    EXPECT_TRUE(r.updatedAt(2).carryDistance);
    EXPECT_FALSE(r.updatedAt(2).clubheadSpeed);
}

TEST(PremierCarry, TheSweepGivesAShotStillWaitingForItsClubDataItsCarry)
{
    const auto steps = stepsOf("s1_full_then_one_sticker");
    Rig r;
    r.startAfter(steps, 1);
    r.play(steps, 1, 2);                        // one shot, pending its club data
    ASSERT_EQ(r.available.size(), 1);
    ASSERT_EQ(r.updated.size(), 0);

    r.folder.writeHistory();
    r.m.sweepCarry();                           // PinPoint's End Session, Premier still open
    ASSERT_EQ(r.updated.size(), 1);
    EXPECT_TRUE(r.updatedAt(0).carryDistance);
    EXPECT_FALSE(r.updatedAt(0).clubheadSpeed) << "its club data is still to come";
    EXPECT_TRUE(r.updatedAt(0).dropped.empty()) << "pending, so nothing claimed missing";

    // Its club data comes with the next shot: the final reading keeps the carry.
    r.play(steps, 2, 3);
    ASSERT_EQ(r.updated.size(), 2);
    EXPECT_TRUE(r.updatedAt(1).clubheadSpeed);
    EXPECT_TRUE(r.updatedAt(1).carryDistance);
    EXPECT_EQ(*r.updatedAt(1).carryDistance, *r.updatedAt(0).carryDistance);
}

TEST(PremierCarry, ABallOnlyShotAtTheQueueStillGetsItsCarry)
{
    const auto steps = stepsOf("s3_quit_mid_session");
    Rig r;
    r.folder.writeHistory();
    r.startAfter(steps, 1);
    r.play(steps, 1);
    EXPECT_EQ(r.available.size(), 1);
    ASSERT_EQ(r.updated.size(), 1) << "ball-only for good, but the carry is Premier's to give";
    EXPECT_TRUE(r.updatedAt(0).carryDistance);
    EXPECT_FALSE(r.updatedAt(0).clubheadSpeed);
}

TEST(PremierCarry, NoHistoryFileChangesNothing)
{
    const auto steps = stepsOf("s1_full_then_one_sticker");
    Rig r;
    r.startAfter(steps, 1);
    r.play(steps, 1);
    r.m.sweepCarry();
    EXPECT_EQ(r.updated.size(), 10);
    EXPECT_EQ(r.m.state(), State::Waiting);
}
