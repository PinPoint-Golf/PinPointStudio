// session_archiver_test — SessionArchiver (src/Export/session_archiver.h): a session moved to an
// archive location and back.
//
// Guarded:
//   · every archived file is byte-identical to its source; the library keeps only a stub
//     swing.ppsw and the thumbnail per swing, and the stub still lists (summary, metrics, review)
//     while the heavy tracks are gone;
//   · a review made while archived survives the restore;
//   · restore brings back every file byte for byte and clears the marker;
//   · the refusals: archiving twice, an archive location inside the session;
//   · raw frames are dropped only when asked;
//   · cancelling part-way leaves the library exactly as it was.

#include "../session_archiver.h"
#include "../swing_doc.h"
#include "../swing_store.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QTemporaryDir>

#include <cstdio>

using namespace pinpoint;

static int g_fail = 0;
static void check(bool ok, const char *what)
{
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++g_fail;
}

static QByteArray noise(int n, quint32 seed)
{
    QRandomGenerator g(seed);
    QByteArray b(n, Qt::Uninitialized);
    for (int i = 0; i < n; ++i) b[i] = char(g.bounded(256));
    return b;
}

static bool put(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}

static QByteArray get(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

// A swing as the app writes one: a production document with a video stream, a thumbnail, an
// analysis carrying metrics AND a pose track, a review — plus its media.
static void makeSwing(const QString &dir, int index, bool withRaw)
{
    QDir().mkpath(dir);
    QJsonArray tUs;
    for (int i = 0; i < 50; ++i) tUs.append(qint64(i) * 6667);
    const QJsonObject manifest{
        { "schema", "pinpoint.swing/2" },
        { "clock", QJsonObject{ { "t0_us", 0 }, { "wallclock", "2026-09-01T10:00:00.000Z" } } },
        { "swing", QJsonObject{ { "index", index }, { "id", QStringLiteral("swing_%1").arg(index, 4, 10, QLatin1Char('0')) } } },
        { "streams", QJsonArray{ QJsonObject{ { "kind", "video" }, { "alias", "Face-On" },
                                              { "file", "Face-On.mp4" }, { "frames", QJsonObject{ { "t_us", tUs } } } } } },
        { "thumbnail", QJsonObject{ { "file", "thumb.jpg" } } } };
    SwingDocWriter::writeSwingJson(dir, manifest, nullptr);

    QJsonObject doc = SwingStore::load(dir);
    QJsonArray frames;
    for (int i = 0; i < 50; ++i) {
        QJsonArray kp;
        for (int j = 0; j < 399; ++j) kp.append(0.001 * (i + j));
        frames.append(QJsonObject{ { "t_us", qint64(i) * 6667 }, { "kp", kp } });
    }
    doc.insert("analysis", QJsonObject{
        { "schema", "pinpoint.analysis/3" },
        { "score", QJsonObject{ { "overall", 71 } } },
        { "metrics", QJsonArray{ QJsonObject{ { "key", "tempoRatio" }, { "label", "Tempo" }, { "unit", ":1" },
                                              { "phaseSamples", QJsonArray{ QJsonObject{ { "phase", 7 }, { "value", 3.1 } } } } } } },
        { "phases", QJsonArray{ QJsonObject{ { "phase", 7 }, { "t_us", 200000 } } } },
        { "pose2d", QJsonObject{ { "frames", frames } } } });
    SwingStore::save(dir, doc);
    SwingDocWriter::updateReview(dir, 4, QStringLiteral("good one"), QStringLiteral("7 IRON"));   // rebuilds the summary

    put(dir + "/Face-On.mp4", noise(300 * 1024, 11 + index));
    put(dir + "/thumb.jpg",   noise(8 * 1024, 21 + index));
    put(dir + "/truth.json",  "{\"P7\":1}");
    put(dir + "/swing_phasegrid.json", "{}");   // a cache: neither archived nor kept
    if (withRaw) put(dir + "/Face-On.raw", noise(1024 * 1024, 31 + index));
}

static QStringList filesUnder(const QString &root)
{
    QStringList out;
    QDirIterator it(root, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    const QDir base(root);
    while (it.hasNext()) out << base.relativeFilePath(it.next());
    out.sort();
    return out;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir tmp;
    const QString library = tmp.filePath("library");
    const QString store   = tmp.filePath("archive");
    const QString session = library + "/Test-Athlete/2026-09-01_Test-Athlete_Wrist_01";
    for (int i = 1; i <= 2; ++i)
        makeSwing(session + QStringLiteral("/swing_%1").arg(i, 4, 10, QLatin1Char('0')), i, /*withRaw=*/true);
    put(session + "/diagnostics.json", "{\"ledger\":{}}");

    // Snapshot of every file worth keeping, by content, before anything happens.
    QHash<QString, QByteArray> before;
    for (const QString &rel : filesUnder(session))
        if (!rel.endsWith("swing_phasegrid.json")) before.insert(rel, get(session + "/" + rel));
    const QString swing1 = session + "/swing_0001";
    const SwingSummary sumBefore = SwingDocReader::readSwingSummary(swing1);

    std::printf("=== refusals ===\n");
    {
        const auto r = SessionArchiver::archive(session, session + "/inside");
        check(!r.ok && !SessionArchiver::isArchived(session), "an archive location inside the session is refused");
        const auto c = SessionArchiver::archive(session, store, {}, [](int done, int) { return done < 3; });
        check(!c.ok, "cancelled part-way");
        check(!SessionArchiver::isArchived(session), "…no marker");
        bool untouched = true;
        for (auto it = before.begin(); it != before.end(); ++it)
            untouched &= get(session + "/" + it.key()) == it.value();
        check(untouched, "…and every library file exactly as it was");
    }

    std::printf("\n=== archive ===\n");
    const auto r = SessionArchiver::archive(session, store);
    check(r.ok, "archived");
    if (!r.ok) std::printf("    %s\n", r.error.toUtf8().constData());
    const QString dest = SessionArchiver::archivePathFor(session, store);
    check(r.archivePath == dest && dest.endsWith("/Test-Athlete/2026-09-01_Test-Athlete_Wrist_01"),
          "copied to <archive>/<athlete>/<session>");
    bool identical = true;
    for (auto it = before.begin(); it != before.end(); ++it)
        identical &= get(dest + "/" + it.key()) == it.value();
    check(identical, "every file in the archive is byte-identical to its source, raw included");
    check(!QFile::exists(dest + "/swing_0001/swing_phasegrid.json"), "caches are not archived");
    check(SessionArchiver::isArchived(session), "the session is marked archived");
    check(filesUnder(swing1) == QStringList({ "swing.ppsw", "thumb.jpg" }),
          "each swing keeps only its stub and thumbnail");
    check(r.bytesFreed > 2 * 1024 * 1024, "the media left the library");

    const QJsonObject stub = SwingStore::load(swing1);
    const QJsonObject an = stub.value("analysis").toObject();
    check(!an.contains("pose2d") && an.contains("metrics") && an.contains("phases"),
          "the stub drops the pose track and keeps metrics and phases");
    check(stub.value("archive").toObject().value("location").toString() == dest + "/swing_0001",
          "…and names where the rest went");
    check(!stub.value("streams").toArray().at(0).toObject().contains("frames"),
          "…streams keep their identity, not their samples");
    const SwingSummary sumStub = SwingDocReader::readSwingSummary(swing1);
    check(sumStub.ok && sumStub.fromSidecar, "the stub lists from its summary block");
    check(sumStub.metrics == sumBefore.metrics && sumStub.score == sumBefore.score
              && sumStub.rating == 4 && sumStub.club == QStringLiteral("7 IRON"),
          "…with the same chips, score, stars and club as before");
    check(!SessionArchiver::archive(session, store).ok, "archiving it twice is refused");

    std::printf("\n=== a review made while archived ===\n");
    check(SwingDocWriter::updateReview(swing1, 2, QStringLiteral("rated later"), QStringLiteral("PW")),
          "rate the stub");

    std::printf("\n=== restore ===\n");
    const auto back = SessionArchiver::restore(session);
    check(back.ok, "restored");
    if (!back.ok) std::printf("    %s\n", back.error.toUtf8().constData());
    check(!SessionArchiver::isArchived(session), "the marker is gone");
    bool restored = true;
    for (auto it = before.begin(); it != before.end(); ++it) {
        if (it.key() == QLatin1String("swing_0001/swing.ppsw")) continue;   // re-reviewed, below
        restored &= get(session + "/" + it.key()) == it.value();
    }
    check(restored, "every file is back byte for byte");
    const PersistedShot full = SwingDocReader::readSwingJson(swing1);
    check(full.ok && full.analysisDetail.contains("pose2d"), "the full document is back, pose track and all");
    check(full.rating == 2 && full.note == QStringLiteral("rated later") && full.club == QStringLiteral("PW"),
          "…carrying the review made while it was archived");
    check(QFile::exists(dest + "/swing_0001/Face-On.mp4"), "the archive copy is kept");

    std::printf("\n=== raw frames dropped only when asked ===\n");
    {
        const QString s2 = library + "/Test-Athlete/2026-09-02_Test-Athlete_Wrist_01";
        makeSwing(s2 + "/swing_0001", 1, /*withRaw=*/true);
        SessionArchiver::Options opt;
        opt.keepRaw = false;
        const auto a = SessionArchiver::archive(s2, store, opt);
        check(a.ok, "archived without raw");
        const QString d2 = SessionArchiver::archivePathFor(s2, store);
        check(!QFile::exists(d2 + "/swing_0001/Face-On.raw") && QFile::exists(d2 + "/swing_0001/Face-On.mp4"),
              "the raw is not in the archive, the video is");
        check(!QFile::exists(s2 + "/swing_0001/Face-On.raw"), "…and not in the library either");
    }

    std::printf("\n%s (%d failed)\n", g_fail ? "FAILED" : "ALL PASSED", g_fail);
    return g_fail ? 1 : 0;
}
