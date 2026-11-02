// Which swing a launch monitor reading is attributed to, and what stops one being
// attributed to the wrong swing.
//
// Two halves:
//   • ShotPairing — the attribution rule itself. Pure, so it is driven directly.
//   • GcQuadMonitor — the watermark: noticing a genuinely new row in a file that is
//     rewritten in place, and refusing to claim what was already on disk at startup.
//
// The monitor half writes real files into a QTemporaryDir, per the repo convention
// that a test needing a file makes its own. pollNow() is called directly rather than
// waiting on the QTimer, so no event loop and no sleeping.

#include "gcquad_monitor.h"
#include "shot_pairing.h"

#include <gtest/gtest.h>

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>

using namespace pinpoint::lm;

// GcQuadMonitor owns a QTimer, and a QTimer with no event dispatcher warns on every
// start(). Nothing here waits on the timer — pollNow() is always called directly —
// but the application object keeps the output clean and the object legitimate.
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

namespace {

LaunchMonitorReading readingWithId(const QString &id, double clubSpeedMph = 90.0)
{
    LaunchMonitorReading r;
    r.deviceShotId  = id;
    r.clubheadSpeed = clubSpeedMph;
    return r;
}

QByteArray csvFor(const QString &shotId, double clubSpeedMps = 38.978607)
{
    return QByteArray("Shot ID, Club, Club head Speed (m/s), Total Spin (rpm)\r\n")
         + shotId.toUtf8() + ", Irn, "
         + QByteArray::number(clubSpeedMps, 'f', 6) + ", 7614\r\n";
}

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    const bool ok = f.write(bytes) == bytes.size();
    f.close();
    return ok;
}

} // namespace

// ════════════════════════════════════════════════════════════════ ShotPairing ══

TEST(ShotPairing, DiscardsAReadingWhenNothingIsArmed)
{
    // A ball struck while PinPoint was not capturing. There is no swing for it.
    ShotPairing p;
    const auto offer = p.offerReading(readingWithId(QStringLiteral("1")));
    EXPECT_EQ(offer.disposition, ShotPairing::Disposition::Discarded);
    EXPECT_TRUE(offer.swingDir.isEmpty());
}

TEST(ShotPairing, ParksAReadingThatArrivesBeforeTheSwingIsOnDisk)
{
    // The normal case: analysis runs 12-37 s, so the CSV lands long before there is
    // anywhere to write it.
    ShotPairing p;
    p.noteShotDetected();

    const auto offer = p.offerReading(readingWithId(QStringLiteral("1")));
    EXPECT_EQ(offer.disposition, ShotPairing::Disposition::Parked);
    EXPECT_TRUE(p.hasParked());
    EXPECT_FALSE(p.armed());                       // claimed, so no longer waiting

    const auto flushed = p.noteSwingDir(QStringLiteral("/lib/a/s1/swing_0001"));
    ASSERT_TRUE(flushed.has_value());
    EXPECT_EQ(flushed->deviceShotId.toStdString(), "1");
    EXPECT_FALSE(p.hasParked());
}

TEST(ShotPairing, ClaimsOutrightWhenTheReadingArrivesDuringReplay)
{
    // The other stated arrival window: swing.json already written, first replay
    // running. There is nothing to park — write it now.
    ShotPairing p;
    p.noteShotDetected();
    EXPECT_FALSE(p.noteSwingDir(QStringLiteral("/lib/a/s1/swing_0002")).has_value());

    const auto offer = p.offerReading(readingWithId(QStringLiteral("2")));
    EXPECT_EQ(offer.disposition, ShotPairing::Disposition::Claimed);
    EXPECT_EQ(offer.swingDir.toStdString(), "/lib/a/s1/swing_0002");
    EXPECT_FALSE(p.hasParked());
}

TEST(ShotPairing, ANewShotDisplacesASwingStillWaiting)
{
    // THE RULE. If a second ball is struck before the first one's row appeared, the
    // first swing gets nothing — because any subsequent write describes the second.
    ShotPairing p;
    p.noteShotDetected();          // swing A
    p.noteShotDetected();          // swing B — A is abandoned here

    const auto offer = p.offerReading(readingWithId(QStringLiteral("77")));
    ASSERT_EQ(offer.disposition, ShotPairing::Disposition::Parked);

    const auto flushed = p.noteSwingDir(QStringLiteral("/lib/a/s1/swing_B"));
    ASSERT_TRUE(flushed.has_value());
    EXPECT_EQ(flushed->deviceShotId.toStdString(), "77");   // it went to B
}

TEST(ShotPairing, OnlyTheFirstReadingAfterAShotIsClaimed)
{
    // Arming is consumed. A second write with no shot in between is not ours.
    ShotPairing p;
    p.noteShotDetected();
    EXPECT_EQ(p.offerReading(readingWithId(QStringLiteral("1"))).disposition,
              ShotPairing::Disposition::Parked);
    EXPECT_EQ(p.offerReading(readingWithId(QStringLiteral("2"))).disposition,
              ShotPairing::Disposition::Discarded);
}

TEST(ShotPairing, WaitsIndefinitelyWithNoTimeout)
{
    // There is deliberately no clock in this class. However long the monitor takes,
    // the swing is still armed until the next shot displaces it.
    ShotPairing p;
    p.noteShotDetected();
    EXPECT_TRUE(p.armed());
    for (int i = 0; i < 10'000; ++i)
        EXPECT_TRUE(p.armed());
    EXPECT_EQ(p.offerReading(readingWithId(QStringLiteral("9"))).disposition,
              ShotPairing::Disposition::Parked);
}

TEST(ShotPairing, DropsAParkedReadingWhenTheShotFails)
{
    // Analysis or export failed, so no swingDir will ever arrive. The reading has
    // nowhere to go and must not attach itself to the next swing.
    ShotPairing p;
    p.noteShotDetected();
    ASSERT_EQ(p.offerReading(readingWithId(QStringLiteral("3"))).disposition,
              ShotPairing::Disposition::Parked);

    p.noteShotFailed();
    EXPECT_FALSE(p.hasParked());
    EXPECT_FALSE(p.armed());
    EXPECT_FALSE(p.noteSwingDir(QStringLiteral("/lib/a/s1/swing_next")).has_value());
}

TEST(ShotPairing, AParkedReadingDoesNotLeakIntoTheNextSwing)
{
    // Same hazard by the other route: the shot never reported failure, it just never
    // produced a swingDir before the next ball was struck.
    ShotPairing p;
    p.noteShotDetected();
    ASSERT_EQ(p.offerReading(readingWithId(QStringLiteral("4"))).disposition,
              ShotPairing::Disposition::Parked);

    p.noteShotDetected();                                  // next swing
    EXPECT_FALSE(p.hasParked());
    EXPECT_FALSE(p.noteSwingDir(QStringLiteral("/lib/a/s1/swing_next")).has_value());
}

// ════════════════════════════════════════ ShotPairing, with a stated contact time ══
// Foresight Premier stamps each shot, and drops a shot hit before it is ready; the contact time
// is what keeps the next reading from going to the wrong swing (shot_pairing.h).

namespace {
LaunchMonitorReading timedReading(const QString &id, qint64 contactMs)
{
    LaunchMonitorReading r = readingWithId(id);
    r.contactAtMs = contactMs;
    return r;
}
constexpr qint64 kT0 = 1'800'000'000'000;   // an arbitrary wall-clock instant, ms
} // namespace

TEST(ShotPairingTimed, ClaimsAReadingWhoseContactMatchesOurImpact)
{
    ShotPairing p;
    p.noteShotDetected(kT0);
    const auto offer = p.offerReading(timedReading(QStringLiteral("a"), kT0 + 200));
    EXPECT_EQ(offer.disposition, ShotPairing::Disposition::Parked);
    EXPECT_EQ(offer.timeCheck, ShotPairing::TimeCheck::Matched);
    ASSERT_TRUE(offer.contactDeltaMs);
    EXPECT_EQ(*offer.contactDeltaMs, 200);
}

TEST(ShotPairingTimed, AReadingFromBeforeOurImpactIsAnEarlierShotAndWeStayArmed)
{
    // A shot hit while we were busy, which we never saw, reported late. Not ours.
    ShotPairing p;
    p.noteShotDetected(kT0);
    const auto stale = p.offerReading(timedReading(QStringLiteral("old"), kT0 - 6000));
    EXPECT_EQ(stale.disposition, ShotPairing::Disposition::Discarded);
    EXPECT_EQ(stale.timeCheck, ShotPairing::TimeCheck::BeforeImpact);
    EXPECT_TRUE(p.armed()) << "our own reading may still be coming";

    const auto ours = p.offerReading(timedReading(QStringLiteral("ours"), kT0 + 100));
    EXPECT_EQ(ours.disposition, ShotPairing::Disposition::Parked);
}

TEST(ShotPairingTimed, AReadingFromAfterTheWindowMeansOursWasDropped)
{
    ShotPairing p;
    p.noteShotDetected(kT0);
    const auto late = p.offerReading(timedReading(QStringLiteral("next"), kT0 + 6000));
    EXPECT_EQ(late.disposition, ShotPairing::Disposition::Discarded);
    EXPECT_EQ(late.timeCheck, ShotPairing::TimeCheck::AfterWindow);
    EXPECT_FALSE(p.armed()) << "the device dropped our shot; nothing later can be ours";
}

TEST(ShotPairingTimed, TheDroppedThenMissedShotNoLongerPairsWrongly)
{
    // design §4.3: Premier drops shot A (hit too soon); we then miss shot B, which Premier records.
    // Order alone hands B's reading to A's swing, silently. The contact time refuses it.
    ShotPairing p;
    p.noteShotDetected(kT0);                    // swing A armed; Premier never reports A
    const auto b = p.offerReading(timedReading(QStringLiteral("B"), kT0 + 7000));
    EXPECT_EQ(b.disposition, ShotPairing::Disposition::Discarded);
    EXPECT_FALSE(p.noteSwingDir(QStringLiteral("/swings/A")).has_value());
}

TEST(ShotPairingTimed, WithoutBothTimesOrderAloneDecides)
{
    ShotPairing p;
    p.noteShotDetected();                       // no impact time
    EXPECT_EQ(p.offerReading(timedReading(QStringLiteral("x"), kT0 + 99'000)).disposition,
              ShotPairing::Disposition::Parked);

    ShotPairing q;
    q.noteShotDetected(kT0);                    // a reading with no contact time (FSX2020)
    const auto o = q.offerReading(readingWithId(QStringLiteral("y")));
    EXPECT_EQ(o.disposition, ShotPairing::Disposition::Parked);
    EXPECT_EQ(o.timeCheck, ShotPairing::TimeCheck::None);
}

TEST(ShotPairingTimed, AFullerReadingReplacesTheParkedOneInPlace)
{
    ShotPairing p;
    p.noteShotDetected(kT0);
    p.offerReading(timedReading(QStringLiteral("s1"), kT0 + 100));
    LaunchMonitorReading whole = timedReading(QStringLiteral("s1"), kT0 + 100);
    whole.faceAngle = -2.0;
    EXPECT_TRUE(p.replaceParked(whole));
    EXPECT_FALSE(p.replaceParked(timedReading(QStringLiteral("other"), kT0)));
    const auto flushed = p.noteSwingDir(QStringLiteral("/swings/1"));
    ASSERT_TRUE(flushed);
    ASSERT_TRUE(flushed->faceAngle);
    EXPECT_DOUBLE_EQ(*flushed->faceAngle, -2.0);
}

// ═════════════════════════════════════════════════════════ GcQuadMonitor ══════

TEST(GcQuadMonitor, DoesNotClaimAFileThatWasAlreadyThereAtStartup)
{
    // FSX2020 leaves its last shot on disk between runs. Claiming it would hand the
    // first swing of the day yesterday's numbers.
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    ASSERT_TRUE(writeFile(dir.filePath(QStringLiteral("LastShot.csv")), csvFor(QStringLiteral("100"))));

    GcQuadMonitor m;
    QSignalSpy spy(&m, &LaunchMonitorBase::readingAvailable);
    m.setSourcePath(dir.path());
    m.start();

    EXPECT_EQ(spy.count(), 0);
    // …but the path is proven good, which is what the settings panel is asking.
    EXPECT_EQ(m.state(), State::Ready);

    m.pollNow();
    EXPECT_EQ(spy.count(), 0);
}

TEST(GcQuadMonitor, EmitsWhenANewShotIdAppears)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("LastShot.csv"));
    ASSERT_TRUE(writeFile(path, csvFor(QStringLiteral("100"))));

    GcQuadMonitor m;
    QSignalSpy spy(&m, &LaunchMonitorBase::readingAvailable);
    m.setSourcePath(dir.path());
    m.start();
    ASSERT_EQ(spy.count(), 0);

    ASSERT_TRUE(writeFile(path, csvFor(QStringLiteral("101"), 40.0)));
    m.pollNow();

    ASSERT_EQ(spy.count(), 1);
    const auto r = spy.at(0).at(0).value<LaunchMonitorReading>();
    EXPECT_EQ(r.deviceShotId.toStdString(), "101");
    EXPECT_FALSE(r.sourcePath.isEmpty());
    EXPECT_GT(r.readAtMs, 0);
}

TEST(GcQuadMonitor, IgnoresAByteIdenticalRewrite)
{
    // A `touch`, or FSX2020 rewriting a row it has already reported. The mtime moves and
    // the shot does not, so nothing is emitted.
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("LastShot.csv"));
    ASSERT_TRUE(writeFile(path, csvFor(QStringLiteral("100"))));

    GcQuadMonitor m;
    QSignalSpy spy(&m, &LaunchMonitorBase::readingAvailable);
    m.setSourcePath(dir.path());
    m.start();

    ASSERT_TRUE(writeFile(path, csvFor(QStringLiteral("100"))));   // same bytes
    m.pollNow();
    EXPECT_EQ(spy.count(), 0);
}

TEST(GcQuadMonitor, ANewShotReusingAnOldIdIsStillANewShot)
{
    // FSX2020's Shot ID is a PER-SESSION counter and it restarts. Close it, reopen it,
    // and the first shot of the new session can carry an id we have already seen. Keying
    // "is this new" on the id drops that shot silently — the worst way to lose one — so
    // the CONTENT is the identity and the id is provenance only.
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("LastShot.csv"));
    ASSERT_TRUE(writeFile(path, csvFor(QStringLiteral("1"), 38.0)));

    GcQuadMonitor m;
    QSignalSpy spy(&m, &LaunchMonitorBase::readingAvailable);
    m.setSourcePath(dir.path());
    m.start();
    ASSERT_EQ(spy.count(), 0);              // baseline, not claimed

    // Same id, different swing — a new FSX2020 session.
    ASSERT_TRUE(writeFile(path, csvFor(QStringLiteral("1"), 44.5)));
    m.pollNow();
    ASSERT_EQ(spy.count(), 1);
    const auto r = spy.at(0).at(0).value<LaunchMonitorReading>();
    EXPECT_EQ(r.deviceShotId.toStdString(), "1");
    EXPECT_NEAR(*r.clubheadSpeed, 44.5 / 0.44704, 0.01);
}

TEST(GcQuadMonitor, ATornReadDoesNotConsumeTheWatermark)
{
    // Caught mid-rewrite: the file is unparseable this instant. Because it is
    // rewritten IN PLACE, advancing past it would lose the shot permanently.
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("LastShot.csv"));
    ASSERT_TRUE(writeFile(path, csvFor(QStringLiteral("100"))));

    GcQuadMonitor m;
    QSignalSpy spy(&m, &LaunchMonitorBase::readingAvailable);
    m.setSourcePath(dir.path());
    m.start();

    // Header written, row not yet.
    ASSERT_TRUE(writeFile(path, QByteArray("Shot ID, Club head Speed (m/s)\r\n")));
    m.pollNow();
    EXPECT_EQ(spy.count(), 0);

    // FSX2020 finishes the write; the next tick must still see this as new.
    ASSERT_TRUE(writeFile(path, csvFor(QStringLiteral("101"))));
    m.pollNow();
    ASSERT_EQ(spy.count(), 1);
    EXPECT_EQ(spy.at(0).at(0).value<LaunchMonitorReading>().deviceShotId.toStdString(), "101");
}

TEST(GcQuadMonitor, FindsTheFileWhateverItsCase)
{
    // FSX2020 writes "LastShot.CSV"; our default spelling is "LastShot.csv". On a
    // case-sensitive filesystem a literal match finds nothing.
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    ASSERT_TRUE(writeFile(dir.filePath(QStringLiteral("LASTSHOT.CSV")), csvFor(QStringLiteral("5"))));

    GcQuadMonitor m;
    m.setSourcePath(dir.path());
    m.start();
    EXPECT_EQ(m.state(), State::Ready);
}

TEST(GcQuadMonitor, WaitsWhenTheFolderIsEmptyAndErrorsWhenItIsMissing)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    GcQuadMonitor m;
    m.setSourcePath(dir.path());
    m.start();
    EXPECT_EQ(m.state(), State::Waiting);
    EXPECT_TRUE(m.errorText().isEmpty());

    m.setSourcePath(dir.filePath(QStringLiteral("no-such-folder")));
    m.start();
    EXPECT_EQ(m.state(), State::Error);
    EXPECT_FALSE(m.errorText().isEmpty());
}

TEST(GcQuadMonitor, ChangingTheFolderRebaselines)
{
    // Pointing at a new folder must not claim whatever is sitting in it.
    QTemporaryDir a, b;
    ASSERT_TRUE(a.isValid() && b.isValid());
    ASSERT_TRUE(writeFile(a.filePath(QStringLiteral("LastShot.csv")), csvFor(QStringLiteral("1"))));
    ASSERT_TRUE(writeFile(b.filePath(QStringLiteral("LastShot.csv")), csvFor(QStringLiteral("2"))));

    GcQuadMonitor m;
    QSignalSpy spy(&m, &LaunchMonitorBase::readingAvailable);
    m.setSourcePath(a.path());
    m.start();
    ASSERT_EQ(spy.count(), 0);

    m.setSourcePath(b.path());     // start() is re-run internally while running
    m.pollNow();
    EXPECT_EQ(spy.count(), 0);
}

TEST(GcQuadMonitor, StopsEmittingOnceStopped)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("LastShot.csv"));
    ASSERT_TRUE(writeFile(path, csvFor(QStringLiteral("100"))));

    GcQuadMonitor m;
    QSignalSpy spy(&m, &LaunchMonitorBase::readingAvailable);
    m.setSourcePath(dir.path());
    m.start();
    m.stop();

    ASSERT_TRUE(writeFile(path, csvFor(QStringLiteral("101"))));
    m.pollNow();
    EXPECT_EQ(spy.count(), 0);
    EXPECT_EQ(m.state(), State::Disabled);
}
