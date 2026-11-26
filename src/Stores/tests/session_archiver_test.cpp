// session_archiver_test — the archive (src/Stores/session_archiver.h) behind SessionRepository::archive /
// restore / trash (step 2.6): a session moved to an archive location and back, by its SessionId.
//
// Guarded:
//   · every archived file is byte-identical to its source; the library keeps only a stub
//     swing.ppsw and the thumbnail per swing, and the stub still lists (summary, metrics, review)
//     while the heavy tracks are gone;
//   · a review made while archived survives the restore;
//   · restore brings back every file byte for byte and clears the marker;
//   · the refusals: archiving twice, an archive location inside the session;
//   · raw frames are dropped only when asked;
//   · cancelling part-way leaves the library exactly as it was;
//   · (step 2.6) the session file is copied with the archive, the library's kept on restore and put
//     back from the archive when the library has lost it; archived.json's schema is written and checked
//     on read (a newer marker is read-only, a broken one refused); a session its file says is in
//     progress is neither archived nor trashed; the session's lock is held around each write and never
//     across the run, and a lock another writer holds stops the run (Locked) with the library unchanged;
//     a proven copy replaces its file in one rename — the file is there the moment before — and a failed
//     rename leaves the file as it was; the index's `archived`; the session trash;
//   · (step 2.8b) the swing door's writes take the session's lock — an edit from another thread while the
//     restore puts a document back is refused Locked, the archiver's own writes join its hold (J-36, J-47);
//     the archive re-checks the marker and the session's status before each swing and stops when a restore
//     elsewhere removed the marker or the session was resumed; the restore keeps the marker while any swing is
//     still a stub (J-52).

#include "../session_archiver.h"
#include "../session_files.h"
#include "../swing_doc.h"
#include "swing_doc_test_writes.h"   // the writes, through RecordingRepository (step 1.4)
#include "../swing_store.h"
#include "../../Core/folder_lock.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QSet>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QUuid>

#include <cstdio>
#include <filesystem>
#include <system_error>
#include <thread>

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
        // Whose swing (every captured swing records it): what names a session without a session
        // file (D3) when the archiver translates its folders to ShotRefs (step 2.4).
        { "athlete", QJsonObject{ { "name", "Mark" }, { "uuid", "3f2c8a4e-1b7d-4c9a-9e21-6a0d5c1b2a10" } } },
        { "clock", QJsonObject{ { "t0_us", 0 }, { "wallclock", "2026-09-01T10:00:00.000Z" } } },
        { "swing", QJsonObject{ { "index", index }, { "id", QStringLiteral("swing_%1").arg(index, 4, 10, QLatin1Char('0')) } } },
        { "streams", QJsonArray{ QJsonObject{ { "kind", "video" }, { "alias", "Face-On" },
                                              { "file", "Face-On.mp4" }, { "frames", QJsonObject{ { "t_us", tUs } } } } } },
        { "thumbnail", QJsonObject{ { "file", "thumb.jpg" } } } };
    pinpoint::testdoc::writeSwingJson(dir, manifest, nullptr);

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
    pinpoint::testdoc::updateReview(dir, 4, QStringLiteral("good one"), QStringLiteral("7 IRON"));   // rebuilds the summary

    put(dir + "/Face-On.mp4", noise(300 * 1024, 11 + index));
    put(dir + "/thumb.jpg",   noise(8 * 1024, 21 + index));
    put(dir + "/truth.json",  "{\"P7\":1}");
    put(dir + "/swing_phasegrid.json", "{}");   // a cache: neither archived nor kept
    if (withRaw) put(dir + "/Face-On.raw", noise(1024 * 1024, 31 + index));
}

static const char *const kAthleteUuid = "3f2c8a4e-1b7d-4c9a-9e21-6a0d5c1b2a10";

// Through the repository, by SessionId, in a run of its own — as the Archiving tab does.
static SessionArchiveResult archiveIt(const QString &session, const QString &root, bool keepRaw = true,
                                      const SessionArchiveProgress &progress = {})
{
    SessionArchiveRun run;
    return run.sessions().archive(run.openSession(session), SessionArchiveOptions{ root, keepRaw }, progress);
}
static SessionArchiveResult restoreIt(const QString &session, const SessionArchiveProgress &progress = {})
{
    SessionArchiveRun run;
    return run.sessions().restore(run.openSession(session), progress);
}

// The archiver's own rename over a file (std::filesystem::rename), for a test's replace hook to finish with.
static bool renameOver(const QString &from, const QString &to, QString *why)
{
    std::error_code ec;
    std::filesystem::rename(std::filesystem::path(QFile::encodeName(from).toStdString()),
                            std::filesystem::path(QFile::encodeName(to).toStdString()), ec);
    if (ec && why) *why = QString::fromStdString(ec.message());
    return !ec;
}

// A session file as SessionStore writes one (FILE-SESS-01).
static QByteArray sessionFile(const QString &status)
{
    QJsonObject o{ { "schema", "pinpoint.session/1" },
                   { "sessionId", QUuid::createUuid().toString(QUuid::WithoutBraces) },
                   { "athleteId", kAthleteUuid }, { "type", "wrist" }, { "status", status },
                   { "startedAt", "2026-09-03T10:00:00.000Z" }, { "origin", QJsonObject{ { "kind", "captured" } } } };
    if (status == QLatin1String("ended")) o.insert("endedAt", "2026-09-03T11:00:00.000Z");
    return QJsonDocument(o).toJson(QJsonDocument::Indented);
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
        const auto r = archiveIt(session, session + "/inside");
        check(!r.ok && !SessionArchiver::isArchived(session), "an archive location inside the session is refused");
        const auto c = archiveIt(session, store, true, [](int done, int) { return done < 3; });
        check(!c.ok, "cancelled part-way");
        check(!SessionArchiver::isArchived(session), "…no marker");
        bool untouched = true;
        for (auto it = before.begin(); it != before.end(); ++it)
            untouched &= get(session + "/" + it.key()) == it.value();
        check(untouched, "…and every library file exactly as it was");
    }

    std::printf("\n=== archive ===\n");
    const auto r = archiveIt(session, store);
    check(r.ok && r.status == WriteResult::Status::Written, "archived, through SessionRepository by its SessionId");
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
    check(!archiveIt(session, store).ok, "archiving it twice is refused");

    std::printf("\n=== a review made while archived ===\n");
    check(pinpoint::testdoc::updateReview(swing1, 2, QStringLiteral("rated later"), QStringLiteral("PW")),
          "rate the stub");
    // The launch monitor board's annotations are golfer input too (REVIEW-1 of step 1.4): leaving a
    // shot out, or naming its club, on an archived session's stub must survive the restore.
    check(pinpoint::testdoc::updateShotAnnotations(swing1, true, QStringLiteral("9 IRON")),
          "leave the stub out of the board and name its club");
    // swing_0002: a note and a rating on its stub; the golfer rates it again and leaves it out WHILE
    // the restore runs, after its document has landed (REVIEW-2 of step 1.4).
    const QString swing2 = session + "/swing_0002";
    check(pinpoint::testdoc::updateReview(swing2, 1, QStringLiteral("stub note"), QStringLiteral("7 IRON")),
          "rate and note the second stub");

    std::printf("\n=== restore ===\n");
    bool editedDuringRestore = false;
    const auto back = restoreIt(session, [&](int, int) {
        // Between files: once swing_0002's document is back (its stub's `archive` block is gone),
        // the golfer edits it through the same door the app uses.
        if (!editedDuringRestore && SwingStore::hasDocument(swing2)
            && !SwingStore::load(swing2).contains(QStringLiteral("archive"))) {
            editedDuringRestore = pinpoint::testdoc::updateReview(swing2, 5, QStringLiteral("stub note"),
                                                                  QStringLiteral("7 IRON"))
                                  && pinpoint::testdoc::updateShotAnnotations(swing2, true, QString());
        }
        return true;
    });
    check(back.ok, "restored");
    if (!back.ok) std::printf("    %s\n", back.error.toUtf8().constData());
    check(!SessionArchiver::isArchived(session), "the marker is gone");
    bool restored = true;
    for (auto it = before.begin(); it != before.end(); ++it) {
        if (it.key() == QLatin1String("swing_0001/swing.ppsw")
            || it.key() == QLatin1String("swing_0002/swing.ppsw")) continue;   // re-reviewed, below
        restored &= get(session + "/" + it.key()) == it.value();
    }
    check(restored, "every file is back byte for byte");
    const PersistedShot full = SwingDocReader::readSwingJson(swing1);
    check(full.ok && full.analysisDetail.contains("pose2d"), "the full document is back, pose track and all");
    check(full.rating == 2 && full.note == QStringLiteral("rated later") && full.club == QStringLiteral("PW"),
          "…carrying the review made while it was archived");
    check(full.excluded && full.clubOverride == QStringLiteral("9 IRON"),
          "…and the board's exclusion and club override made on the stub");
    check(editedDuringRestore, "swing_0002 was edited during the restore, after its document landed");
    const PersistedShot two = SwingDocReader::readSwingJson(swing2);
    check(two.ok && two.rating == 5 && two.excluded,
          "…and that edit survived the restore (rating 5, left out)");
    check(two.note == QStringLiteral("stub note") && two.club == QStringLiteral("7 IRON"),
          "…beside what was made on its stub while archived");

    std::printf("\n=== the three-way review merge ===\n");
    {
        const QJsonObject base{ { "rating", 3 }, { "note", "a" }, { "club", "7 IRON" } };
        // Changed on the stub only → taken; changed during the restore only → left alone.
        QJsonObject stub = base;  stub["rating"] = 4; stub["excluded"] = true;
        QJsonObject now  = base;  now["note"] = "b";  now["clubOverride"] = "PW";
        auto m = SessionArchiver::mergeReview(base, stub, now);
        check(m.any && m.edit.rating == 4 && m.edit.excluded == true && !m.edit.note && !m.edit.clubOverride
                  && !m.edit.club && m.conflicts.isEmpty(),
              "a stub-only change is taken; a change made during the restore is left alone");
        // Both changed one field differently → the later edit (now) stays, and it is reported.
        stub = base; stub["rating"] = 1;
        now  = base; now["rating"]  = 5;
        m = SessionArchiver::mergeReview(base, stub, now);
        check(!m.any && !m.edit.rating && m.conflicts == QStringList{ "rating" },
              "a conflict keeps the restored document's (later) edit and names the field");
        // Both made the same change → nothing to do, no conflict.
        now = stub;
        m = SessionArchiver::mergeReview(base, stub, now);
        check(!m.any && m.conflicts.isEmpty(), "the same change on both sides is no conflict");
        // An annotation cleared on the stub (absent) clears it, unless the restored one changed.
        QJsonObject baseX = base; baseX["excluded"] = true; baseX["clubOverride"] = "PW";
        m = SessionArchiver::mergeReview(baseX, base, baseX);
        check(m.any && m.edit.excluded == false && m.edit.clubOverride == QString(),
              "annotations cleared on the stub are cleared on restore");
        m = SessionArchiver::mergeReview(base, base, now);
        check(!m.any && m.conflicts.isEmpty(), "an untouched stub changes nothing");
    }
    check(QFile::exists(dest + "/swing_0001/Face-On.mp4"), "the archive copy is kept");

    std::printf("\n=== raw frames dropped only when asked ===\n");
    {
        const QString s2 = library + "/Test-Athlete/2026-09-02_Test-Athlete_Wrist_01";
        makeSwing(s2 + "/swing_0001", 1, /*withRaw=*/true);
        const auto a = archiveIt(s2, store, /*keepRaw=*/false);
        check(a.ok, "archived without raw");
        const QString d2 = SessionArchiver::archivePathFor(s2, store);
        check(!QFile::exists(d2 + "/swing_0001/Face-On.raw") && QFile::exists(d2 + "/swing_0001/Face-On.mp4"),
              "the raw is not in the archive, the video is");
        check(!QFile::exists(s2 + "/swing_0001/Face-On.raw"), "…and not in the library either");
    }

    std::printf("\n=== the session file goes with the archive (step 2.6) ===\n");
    const QString s3 = library + "/Test-Athlete/2026-09-03_Test-Athlete_Wrist_01";
    makeSwing(s3 + "/swing_0001", 1, /*withRaw=*/false);
    const QString s3file = SessionFiles::sessionFile(s3);
    put(s3file, sessionFile("ended"));
    const QByteArray s3fileBytes = get(s3file);
    const QString d3 = SessionArchiver::archivePathFor(s3, store);
    {
        const auto a = archiveIt(s3, store);
        check(a.ok, "a session with a session file is archived");
        check(get(d3 + "/session.json") == s3fileBytes, "…its session file is in the archive, byte for byte");
        check(get(s3file) == s3fileBytes, "…and stays in the library, untouched");

        // archived.json: written whole, with its schema, and read back with it checked.
        const QJsonObject m = QJsonDocument::fromJson(get(SessionArchiver::markerPath(s3))).object();
        check(m.value("schema").toString() == QLatin1String(SessionArchiver::kMarkerSchema)
                  && m.value("location").toString() == d3 && m.value("swings").toInt() == 1
                  && m.value("rawKept").toBool() && m.contains("archivedAt") && m.contains("files"),
              "archived.json carries its schema (pinpoint.archive/1), the location and the counts");
        check(SessionArchiver::readMarker(s3).state == SessionArchiver::Marker::State::Read
                  && SessionArchiver::readMarker(s3).location == d3, "…and reads back with its schema checked");

        // The index says it is archived.
        SessionStore st;
        bool listed = false;
        for (const SessionIndexEntry &e : st.enumerate(SessionOwner{ AthleteId::fromString(QString::fromLatin1(kAthleteUuid)), "Test-Athlete", library }))
            if (QDir::cleanPath(e.folder) == s3) listed = e.archived && e.hasSessionFile;
        check(listed, "the session index lists it as archived");

        // A change to the library's session file while archived (a newer write) is kept on restore.
        QJsonObject later = QJsonDocument::fromJson(s3fileBytes).object();
        later.insert("keptKey", "written while archived");
        put(s3file, QJsonDocument(later).toJson(QJsonDocument::Indented));
        const QByteArray laterBytes = get(s3file);
        const auto b = restoreIt(s3);
        check(b.ok, "restored");
        check(get(s3file) == laterBytes, "…the library's session file is kept, never overwritten by the archive's");
    }
    {
        // Archived again, over the archive's earlier copy, which every file replaces in place.
        const QByteArray now = get(s3file);
        const auto a = archiveIt(s3, store);
        check(a.ok && get(d3 + "/session.json") == now, "archived again: the archive's copy is replaced by the newer one");
        bool noPartial = true;
        for (const QString &f : filesUnder(d3)) noPartial &= !f.endsWith(".partial");
        check(noPartial, "…and no half-written copy is left in the archive");
        // Lost from the library while archived: the restore puts the archive's back.
        QFile::remove(s3file);
        const auto b = restoreIt(s3);
        check(b.ok && get(s3file) == now, "a session file missing from the library is restored from the archive");
    }

    std::printf("\n=== archived.json is checked on read ===\n");
    {
        const QString s5 = library + "/Test-Athlete/2026-09-05_Test-Athlete_Wrist_01";
        makeSwing(s5 + "/swing_0001", 1, false);
        check(archiveIt(s5, store).ok, "archived");
        const QByteArray marker = get(SessionArchiver::markerPath(s5));
        const QByteArray stub   = get(s5 + "/swing_0001/swing.ppsw");
        QJsonObject newer = QJsonDocument::fromJson(marker).object();
        newer.insert("schema", "pinpoint.archive/2");
        put(SessionArchiver::markerPath(s5), QJsonDocument(newer).toJson());
        const auto n = restoreIt(s5);
        check(!n.ok && n.status == WriteResult::Status::ReadOnly, "a newer build's marker: refused, read-only");
        check(get(s5 + "/swing_0001/swing.ppsw") == stub && SessionArchiver::isArchived(s5),
              "…the stub untouched, still marked archived");
        put(SessionArchiver::markerPath(s5), "{ not json");
        const auto g = restoreIt(s5);
        check(!g.ok && g.status == WriteResult::Status::Failed && g.error.contains("cannot be read"),
              "a marker that cannot be read: refused");
        put(SessionArchiver::markerPath(s5), "{\"schema\":\"someone.else/1\",\"location\":\"/x\"}");
        check(!restoreIt(s5).ok, "a file that is not an archive marker: refused");
        check(get(s5 + "/swing_0001/swing.ppsw") == stub, "…the stub untouched throughout");
        put(SessionArchiver::markerPath(s5), marker);
        check(restoreIt(s5).ok, "the marker put back: restored");
    }

    std::printf("\n=== a session in progress is neither archived nor trashed ===\n");
    {
        const QString s4 = library + "/Test-Athlete/2026-09-04_Test-Athlete_Wrist_01";
        makeSwing(s4 + "/swing_0001", 1, false);
        put(SessionFiles::sessionFile(s4), sessionFile("inProgress"));
        QHash<QString, QByteArray> snap;
        for (const QString &rel : filesUnder(s4)) snap.insert(rel, get(s4 + "/" + rel));
        const auto a = archiveIt(s4, store);
        check(!a.ok && a.status == WriteResult::Status::Failed && a.error.contains("in progress"),
              "archiving it is refused: its session file says in progress (here, the other computer, or a crash)");
        bool same = !SessionArchiver::isArchived(s4);
        for (auto it = snap.begin(); it != snap.end(); ++it) same &= get(s4 + "/" + it.key()) == it.value();
        check(same, "…and the library is exactly as it was");
        SessionStore st;
        st.setOsTrashForTest(false);   // never the OS trash (step 3.4a, review round 3)
        const SessionId id = st.openFolder(s4);
        const WriteResult t = st.trash(id);
        check(!id.isNull() && !t.ok() && QFileInfo(s4).isDir(), "trashing it is refused, and the folder stays");
    }

    std::printf("\n=== the session lock: per write, never across the run ===\n");
    const QString s6 = library + "/Test-Athlete/2026-09-06_Test-Athlete_Wrist_01";
    for (int i = 1; i <= 2; ++i)
        makeSwing(s6 + QStringLiteral("/swing_%1").arg(i, 4, 10, QLatin1Char('0')), i, false);
    {
        QHash<QString, QByteArray> snap;
        for (const QString &rel : filesUnder(s6)) snap.insert(rel, get(s6 + "/" + rel));
        FolderLock other(s6);
        check(other.lock(0) == FolderLock::Result::Acquired, "another writer holds the session's lock");
        SessionArchiveRun run;
        run.setLockWaitForTest(200);
        const auto a = run.sessions().archive(run.openSession(s6), SessionArchiveOptions{ store, true });
        check(!a.ok && a.status == WriteResult::Status::Locked, "archiving stops: Locked, the holder named");
        bool same = !SessionArchiver::isArchived(s6);
        for (auto it = snap.begin(); it != snap.end(); ++it)
            if (!it.key().startsWith(".")) same &= get(s6 + "/" + it.key()) == it.value();
        check(same, "…and no swing was thinned, no marker written");
        other.unlock();
    }
    {
        // Archive, holding the lock only for each write: between files (the progress callback) it is free.
        SessionArchiveRun run;
        bool freeBetween = true;
        const auto a = run.sessions().archive(run.openSession(s6), SessionArchiveOptions{ store, true }, [&](int, int) {
            FolderLock probe(s6);
            freeBetween &= probe.lock(0) == FolderLock::Result::Acquired;
            return true;
        });
        check(a.ok && freeBetween, "archived; between files the lock is free (never held across the run)");
    }
    {
        // A held lock stops a restore before it puts anything back.
        const QByteArray stub = get(s6 + "/swing_0001/swing.ppsw");
        FolderLock other(s6);
        other.lock(0);
        SessionArchiveRun run;
        run.setLockWaitForTest(200);
        const auto b = run.sessions().restore(run.openSession(s6));
        check(!b.ok && b.status == WriteResult::Status::Locked, "restoring stops: Locked");
        bool noPartial = true;
        for (const QString &f : filesUnder(s6)) noPartial &= !f.endsWith(".partial");
        check(get(s6 + "/swing_0001/swing.ppsw") == stub && SessionArchiver::isArchived(s6) && noPartial,
              "…the stubs untouched, still marked archived, no half copy left behind");
        other.unlock();
    }
    {
        // The restore holds the lock around each file it puts back, and the file is there the moment
        // before: one rename over it, never remove-then-rename.
        QSet<QString> existedBefore;
        for (const QString &rel : filesUnder(s6)) existedBefore.insert(QDir::cleanPath(s6 + "/" + rel));
        SessionArchiveRun run;
        int  replaced = 0;
        bool heldDuring = true, gap = false, freeBetween = true;
        run.setReplaceForTest([&](const QString &from, const QString &to, QString *why) {
            FolderLock probe(s6);
            heldDuring &= probe.lock(0) == FolderLock::Result::Locked;
            if (existedBefore.contains(QDir::cleanPath(to)) && !QFileInfo::exists(to)) gap = true;
            ++replaced;
            return renameOver(from, to, why);
        });
        const auto b = run.sessions().restore(run.openSession(s6), [&](int, int) {
            FolderLock probe(s6);
            freeBetween &= probe.lock(0) == FolderLock::Result::Acquired;
            return true;
        });
        check(b.ok && replaced == b.members && replaced > 0, "restored; every file put back by one rename");
        check(heldDuring, "…each with the session's lock held");
        check(freeBetween, "…and the lock free between files");
        check(!gap, "…and no file put back was missing the moment before (no remove-then-rename)");
    }
    {
        // A rename that fails leaves the file it would have replaced exactly as it was.
        check(archiveIt(s6, store).ok, "archived again");
        const QString doc  = s6 + "/swing_0001/swing.ppsw";
        const QByteArray stub = get(doc);
        SessionArchiveRun run;
        run.setReplaceForTest([&](const QString &from, const QString &to, QString *why) {
            if (QFileInfo(to).fileName() != QLatin1String("swing.ppsw"))
                return renameOver(from, to, why);   // not the document: put it in place for real
            if (why) *why = QStringLiteral("simulated");
            return false;
        });
        const auto b = run.sessions().restore(run.openSession(s6));
        bool noPartial = true;
        for (const QString &f : filesUnder(s6)) noPartial &= !f.endsWith(".partial");
        check(!b.ok && get(doc) == stub && SessionArchiver::isArchived(s6) && noPartial,
              "a failed rename: the stub is still there byte for byte, still archived, no half copy");
        check(restoreIt(s6).ok && !SessionArchiver::isArchived(s6), "…and the next restore completes");
    }

    std::printf("\n=== round 2: a swing refused part-way — the marker is written, swing 1 comes back ===\n");
    {
        const QString s9 = library + "/Test-Athlete/2026-09-09_Test-Athlete_Wrist_01";
        makeSwing(s9 + "/swing_0001", 1, false);
        makeSwing(s9 + "/swing_0002", 2, false);
        QJsonObject newer = SwingStore::load(s9 + "/swing_0002");
        newer.insert("schema", "pinpoint.swing/9");   // saved by a newer build: never thinned by this one
        check(SwingStore::save(s9 + "/swing_0002", newer), "swing 2 of 2 is a newer build's document");
        const QByteArray full1 = get(s9 + "/swing_0001/swing.ppsw");
        const QByteArray full2 = get(s9 + "/swing_0002/swing.ppsw");
        const auto a = archiveIt(s9, store);
        check(!a.ok && a.error.contains("1 of 2"), "archiving stops at swing 2, and says 1 of 2 were archived");
        if (a.ok || !a.error.contains("1 of 2")) std::printf("    %s\n", a.error.toUtf8().constData());
        check(SessionArchiver::readMarker(s9).state == SessionArchiver::Marker::State::Read
                  && QJsonDocument::fromJson(get(SessionArchiver::markerPath(s9))).object().value("swings").toInt() == 1,
              "…the marker is written (its count: 1) — the loop's own lock no longer blocks it");
        check(get(s9 + "/swing_0002/swing.ppsw") == full2, "…swing 2 untouched");
        const auto b = restoreIt(s9);
        check(b.ok, "restored");
        check(get(s9 + "/swing_0001/swing.ppsw") == full1 && SwingDocReader::readSwingJson(s9 + "/swing_0001").analysisDetail.contains("pose2d"),
              "…swing 1 back whole, pose track and all");
        check(get(s9 + "/swing_0002/swing.ppsw") == full2 && !SessionArchiver::isArchived(s9), "…swing 2 as it was, marker gone");
    }

    std::printf("\n=== round 2: stubs never without a marker; a stub never copied over a full document ===\n");
    {
        const QString s10 = library + "/Test-Athlete/2026-09-10_Test-Athlete_Wrist_01";
        makeSwing(s10 + "/swing_0001", 1, false);
        makeSwing(s10 + "/swing_0002", 2, false);
        const QByteArray full1 = get(s10 + "/swing_0001/swing.ppsw");
        const QByteArray full2 = get(s10 + "/swing_0002/swing.ppsw");
        const QString    d10   = SessionArchiver::archivePathFor(s10, store);
        bool markerBeforeThin = false;
        {
            // Another writer takes the session's lock just before swing 2 is thinned, and keeps it.
            FolderLock other(s10);
            SessionArchiveRun run;
            run.setLockWaitForTest(200);
            run.setBeforeThinForTest([&](int i, const QString &) {
                if (i == 0) markerBeforeThin = SessionArchiver::isArchived(s10);
                if (i == 1) other.lock(0);
            });
            const auto a = run.sessions().archive(run.openSession(s10), SessionArchiveOptions{ store, true });
            check(markerBeforeThin, "the marker is there before the first swing is thinned");
            check(!a.ok && a.status == WriteResult::Status::Locked, "a lock held at swing 2 stops the run: Locked");
            check(SessionArchiver::isArchived(s10) && SessionArchiver::readMarker(s10).location == d10,
                  "…and the session is still marked archived, naming the archive (no stub without a marker)");
            check(get(s10 + "/swing_0002/swing.ppsw") == full2 && get(s10 + "/swing_0001/swing.ppsw") != full1,
                  "…swing 1 a stub, swing 2 whole");
        }
        check(!archiveIt(s10, store).ok, "archiving it again is refused: it is marked archived");
        check(get(d10 + "/swing_0001/swing.ppsw") == full1, "…and the archive's full document of swing 1 is untouched");

        // The marker lost (or a run from before round 2): a stub with no marker.
        QFile::remove(SessionArchiver::markerPath(s10));
        const QByteArray stub1 = get(s10 + "/swing_0001/swing.ppsw");
        const QString store2 = tmp.filePath("archive-elsewhere");
        const auto e = archiveIt(s10, store2);
        check(!e.ok && e.error.contains("already archived") && get(s10 + "/swing_0001/swing.ppsw") == stub1
                  && !SessionArchiver::isArchived(s10) && !QFile::exists(store2 + "/Test-Athlete/2026-09-10_Test-Athlete_Wrist_01/swing_0001/swing.ppsw"),
              "to an archive without the stub's full copy: refused, the library unchanged, the stub copied nowhere");
        const auto again = archiveIt(s10, store);
        check(again.ok, "archived again into the archive holding the full copy");
        if (!again.ok) std::printf("    %s\n", again.error.toUtf8().constData());
        check(get(d10 + "/swing_0001/swing.ppsw") == full1 && get(d10 + "/swing_0002/swing.ppsw") == full2,
              "…every full document in the archive survived — the stub was never copied over it");
        const auto b = restoreIt(s10);
        check(b.ok && get(s10 + "/swing_0001/swing.ppsw") == full1 && get(s10 + "/swing_0002/swing.ppsw") == full2,
              "restored: both documents whole, byte for byte");
    }

    std::printf("\n=== step 2.8b: the swing door's writes take the session's lock; the archiver's own join it (J-36, J-47) ===\n");
    {
        const QString s11 = library + "/Test-Athlete/2026-09-11_Test-Athlete_Wrist_01";
        makeSwing(s11 + "/swing_0001", 1, false);
        makeSwing(s11 + "/swing_0002", 2, false);
        check(archiveIt(s11, store).ok, "archived (the stubs written through the door, inside the archiver's hold)");
        check(pinpoint::testdoc::updateReview(s11 + "/swing_0001", 2, QStringLiteral("while archived"), QStringLiteral("PW")),
              "a review made on the stub");
        // While the restore holds the lock to put a swing's document back, the app's own review edit, made on
        // ANOTHER thread through its own door, is shut out (Locked past its wait); the archiver's merge,
        // made inside the hold on its own thread, joins it.
        SessionArchiveRun run;
        WriteResult::Status other = WriteResult::Status::Written;
        bool tried = false;
        run.setReplaceForTest([&](const QString &from, const QString &to, QString *why) {
            if (!tried && QFileInfo(to).fileName() == QLatin1String("swing.ppsw")) {
                tried = true;
                const QString swingDir = QFileInfo(to).path();
                std::thread t([&] {
                    SessionStore st;
                    SwingStoreRecordingRepository door(st);
                    door.setLockWaitForTest(200);
                    ReviewEdit e;
                    e.rating = 5;
                    other = door.editReview(door.shotOfFolder(swingDir, AthleteId::fromString(QString::fromLatin1(kAthleteUuid))), e).status;
                });
                t.join();
            }
            return renameOver(from, to, why);
        });
        const auto b = run.sessions().restore(run.openSession(s11));
        check(tried && other == WriteResult::Status::Locked,
              "an edit from another thread while a document is put back: refused, Locked (H-10 closed for the app's edits)");
        check(b.ok && !SessionArchiver::isArchived(s11), "…and the restore, whose own review merge joins its hold, completes");
        check(SwingDocReader::readSwingJson(s11 + "/swing_0001").note == QLatin1String("while archived"),
              "…keeping the review made while archived (the merge was written)");
        // Outside any hold, the same edit is written — and a lock another writer holds refuses it.
        SessionStore st;
        SwingStoreRecordingRepository door(st);
        door.setLockWaitForTest(200);
        const ShotRef shot = door.shotOfFolder(s11 + "/swing_0002", AthleteId::fromString(QString::fromLatin1(kAthleteUuid)));
        ReviewEdit e;
        e.rating = 3;
        check(door.editReview(shot, e).ok(), "an edit with the session free: written");
        WriteResult::Status heldOut = WriteResult::Status::Written;
        {
            FolderLock otherWriter(s11);
            check(otherWriter.lock(0) == FolderLock::Result::Acquired, "a tool takes the session's lock");
            std::thread t([&] { ReviewEdit e2; e2.rating = 1; heldOut = door.editReview(shot, e2).status; });
            t.join();
        }
        check(heldOut == WriteResult::Status::Locked && SwingDocReader::readSwingJson(s11 + "/swing_0002").rating == 3,
              "…an edit meanwhile: Locked, the document as it was");
        check(!QFileInfo::exists(s11 + "/.pinpoint-lock"), "no lock file left behind");
    }

    std::printf("\n=== step 2.8b: the archive re-checks per swing; the restore removes the marker only over a whole library (J-52) ===\n");
    {
        // A restore run from another computer while this one thins: it puts swing 1 back and removes the marker.
        const QString s12 = library + "/Test-Athlete/2026-09-12_Test-Athlete_Wrist_01";
        makeSwing(s12 + "/swing_0001", 1, false);
        makeSwing(s12 + "/swing_0002", 2, false);
        const QByteArray full1 = get(s12 + "/swing_0001/swing.ppsw");
        const QByteArray full2 = get(s12 + "/swing_0002/swing.ppsw");
        const QString    d12   = SessionArchiver::archivePathFor(s12, store);
        SessionArchiveRun run;
        run.setBeforeThinForTest([&](int i, const QString &) {
            if (i != 1) return;
            put(s12 + "/swing_0001/swing.ppsw", get(d12 + "/swing_0001/swing.ppsw"));   // the other computer's restore
            QFile::remove(SessionArchiver::markerPath(s12));
        });
        const auto a = run.sessions().archive(run.openSession(s12), SessionArchiveOptions{ store, true });
        check(!a.ok && a.error.contains("restored"), "the marker gone before swing 2: archiving stops, and says why");
        check(!SessionArchiver::isArchived(s12), "…the marker is not written again over the restored session");
        check(get(s12 + "/swing_0001/swing.ppsw") == full1 && get(s12 + "/swing_0002/swing.ppsw") == full2,
              "…no stub left without a marker: swing 1 as restored, swing 2 never thinned");
    }
    {
        // The session resumed (InProgress) on another computer while it is thinned.
        const QString s13 = library + "/Test-Athlete/2026-09-13_Test-Athlete_Wrist_01";
        makeSwing(s13 + "/swing_0001", 1, false);
        makeSwing(s13 + "/swing_0002", 2, false);
        put(s13 + "/session.json", sessionFile(QStringLiteral("ended")));
        const QByteArray full2 = get(s13 + "/swing_0002/swing.ppsw");
        SessionArchiveRun run;
        run.setBeforeThinForTest([&](int i, const QString &) {
            if (i != 1) return;
            QJsonObject o = QJsonDocument::fromJson(get(s13 + "/session.json")).object();
            o.insert("status", "inProgress");
            o.remove("endedAt");
            put(s13 + "/session.json", QJsonDocument(o).toJson());
        });
        const auto a = run.sessions().archive(run.openSession(s13), SessionArchiveOptions{ store, true });
        check(!a.ok && a.error.contains("in progress"), "resumed before swing 2: archiving stops (in progress)");
        check(SessionArchiver::isArchived(s13) && get(s13 + "/swing_0002/swing.ppsw") == full2,
              "…swing 2 never thinned, and the marker kept over swing 1's stub");
    }
    {
        // Another run thins a swing again while this restore runs: the marker stays.
        const QString s14 = library + "/Test-Athlete/2026-09-14_Test-Athlete_Wrist_01";
        makeSwing(s14 + "/swing_0001", 1, false);
        makeSwing(s14 + "/swing_0002", 2, false);
        check(archiveIt(s14, store).ok, "archived");
        const QByteArray stub2 = get(s14 + "/swing_0002/swing.ppsw");
        SessionArchiveRun run;
        run.setReplaceForTest([&](const QString &from, const QString &to, QString *why) {
            const bool ok = renameOver(from, to, why);
            if (ok && QDir::cleanPath(to) == QDir::cleanPath(s14 + "/swing_0002/swing.ppsw"))
                put(to, stub2);   // another computer's archive thins it again
            return ok;
        });
        const auto b = run.sessions().restore(run.openSession(s14));
        check(!b.ok && b.error.contains("stub") && SessionArchiver::isArchived(s14),
              "a stub again at the end of the restore: the marker is kept, and the restore says so");
        const auto again = restoreIt(s14);
        check(again.ok && !SessionArchiver::isArchived(s14), "…and the next restore completes");
    }

    std::printf("\n=== the session trash ===\n");
    {
        const QString s8 = library + "/Test-Athlete/2026-09-08_Test-Athlete_Wrist_01";
        makeSwing(s8 + "/swing_0001", 1, false);
        SessionStore st;
        st.setLockWaitForTest(200);
        st.setOsTrashForTest(false);   // never the OS trash: into the library's own (step 3.4a, review round 3)
        const SessionId id = st.openFolder(s8, AthleteId::fromString(QString::fromLatin1(kAthleteUuid)));
        FolderLock other(s8);
        other.lock(0);
        const WriteResult l = st.trash(id);
        check(!l.ok() && l.status == WriteResult::Status::Locked && QFileInfo(s8).isDir(),
              "a session another writer holds locked is not trashed");
        other.unlock();
        QString where;
        const WriteResult t = st.trash(id, &where);
        check(t.ok() && !QFileInfo::exists(s8) && !where.isEmpty(), "an ended session goes to the trash, whole");
        check(where.startsWith(library + "/.pinpoint-trash/Test-Athlete/") && QFileInfo(where).isDir()
                  && QFileInfo(where + "/swing_0001").isDir(),
              "…into the library's own .pinpoint-trash, its swing with it");
        check(!st.open(id).has_value(), "…and the repository lets it go");
    }

    std::printf("\n%s (%d failed)\n", g_fail ? "FAILED" : "ALL PASSED", g_fail);
    return g_fail ? 1 : 0;
}
