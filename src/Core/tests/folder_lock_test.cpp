// Standalone tests for pinpoint::FolderLock (src/Core/folder_lock.h), the advisory lock on one session or
// athlete folder (R-PERS-6; refactor step 2 §4.9, D11).
//
// What it must do: two lock objects on one folder — the second answers Locked (naming the holder) until
// the first lets go, then it gets the lock; a lock written by ANOTHER machine (whose process cannot be
// asked about) is freed only by age, so a young one is Locked and one older than the stale time is taken
// over; a folder that is not there fails and is not made; the lock file is `.pinpoint-lock`.
//
//   cmake --build build/tests --target folder_lock_test -j 6
//   ctest --test-dir build/tests -R '^folder_lock_test$' --output-on-failure

#include "../folder_lock.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <cstdio>

using pinpoint::FolderLock;

static int  g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}

// A lock file as another machine's PinPoint leaves it (QLockFile's format: pid, application, host), aged
// `ageMs`. Its process cannot be looked up from here, so only its age can free it.
static bool writeForeignLock(const QString &path, qint64 ageMs)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    f.write("4242\nPinPointStudio\nanother-studio-pc.invalid\n");
    f.flush();   // before the time is set: a write at close() would make it "now" again
    const QDateTime when = QDateTime::currentDateTimeUtc().addMSecs(-ageMs);
    const bool timed = f.setFileTime(when, QFileDevice::FileModificationTime);
    f.close();
    return timed;
}

int main()
{
    QTemporaryDir tmp;
    if (!tmp.isValid()) {
        std::printf("  [FAIL] could not make a temporary directory\n");
        return 1;
    }

    check(FolderLock::fileName() == QStringLiteral(".pinpoint-lock"), "the lock file is .pinpoint-lock");
    check(FolderLock::kWaitMs == 5000 && FolderLock::kStaleMs == 60000, "waits 5 s; stale after 60 s");

    std::printf("Two lock objects on one folder\n");
    {
        const QString dir = tmp.filePath(QStringLiteral("session_a"));
        QDir().mkpath(dir);
        FolderLock a(dir);
        FolderLock b(dir);

        check(a.lock(0) == FolderLock::Result::Acquired && a.isHeld(), "the first takes it");
        check(QFileInfo::exists(QDir(dir).filePath(QStringLiteral(".pinpoint-lock"))), "the lock file is in the folder");
        check(a.lock(0) == FolderLock::Result::Acquired, "taking it again while held is still Acquired");

        QElapsedTimer t;
        t.start();
        const FolderLock::Result held = b.lock(300);
        const qint64 waited = t.elapsed();
        check(held == FolderLock::Result::Locked && !b.isHeld(), "held → the second answers Locked");
        check(waited >= 250, "it waited (about) the time it was given before answering");
        check(!b.holder().isEmpty() && b.holder().contains(QString::number(QCoreApplication::applicationPid())),
              "the holder is named, with its pid");
        check(b.errorString().contains(dir), "the error names the folder");
        std::printf("    %s\n", qPrintable(b.errorString()));

        a.unlock();
        check(!a.isHeld() && !QFileInfo::exists(a.lockPath()), "let go: the lock file is gone");
        check(b.lock(0) == FolderLock::Result::Acquired && b.isHeld(), "then the second takes it");
        check(b.holder().isEmpty() && b.errorString().isEmpty(), "and the last refusal is forgotten");
    }

    std::printf("Let go when destroyed\n");
    {
        const QString dir = tmp.filePath(QStringLiteral("session_b"));
        QDir().mkpath(dir);
        {
            FolderLock a(dir);
            check(a.lock(0) == FolderLock::Result::Acquired, "taken in a scope");
        }
        FolderLock b(dir);
        check(b.lock(0) == FolderLock::Result::Acquired, "free once the holder is destroyed");
    }

    std::printf("Stale by age (a lock left by another machine)\n");
    {
        const QString dir = tmp.filePath(QStringLiteral("athlete_c"));
        QDir().mkpath(dir);
        const QString lockFile = QDir(dir).filePath(FolderLock::fileName());

        check(writeForeignLock(lockFile, 5 * 1000), "another machine's lock, 5 s old");
        FolderLock young(dir);
        check(young.lock(200) == FolderLock::Result::Locked, "younger than the stale time → Locked");
        check(young.holder().contains(QStringLiteral("another-studio-pc.invalid")), "the other machine is named");

        check(writeForeignLock(lockFile, 2 * 60 * 1000), "the same lock, 2 minutes old");
        FolderLock old(dir);
        check(old.lock(0) == FolderLock::Result::Acquired && old.isHeld(), "older than the stale time → taken over");
        check(QFileInfo::exists(lockFile), "the lock file is ours now");
        old.unlock();

        // A shorter stale time (as a test or a tool may give) frees a younger lock.
        check(writeForeignLock(lockFile, 3 * 1000), "another machine's lock, 3 s old");
        FolderLock quick(dir, 1000);
        check(quick.lock(0) == FolderLock::Result::Acquired, "stale after 1 s → taken over");
    }

    std::printf("A folder that is not there\n");
    {
        const QString dir = tmp.filePath(QStringLiteral("trashed_session"));
        FolderLock a(dir);
        check(a.lock(0) == FolderLock::Result::Failed, "no such folder → Failed");
        check(!QFileInfo::exists(dir), "the folder was not made");
        check(a.errorString().contains(dir), "the error names the folder");
    }

    std::printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
