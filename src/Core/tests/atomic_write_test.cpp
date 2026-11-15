// Standalone tests for pinpoint::atomicWrite() and pinpoint::AtomicFile (src/Core/atomic_write.h), the
// one way a file is replaced (R-PERS-2): every session and athlete cache, every user pack and content
// set, the swing folder's sidecars, the report's files, the PDF, and the tools' outputs.
//
// Moved here from src/Diagnostics/tests/pack_io_test.cpp with the helper (refactor step 2.0); the cases
// it had are kept, and the streaming forms and a failed rename are added.
//
// What it must do: replace an existing file whole, create a missing one (and its directory, unless told
// the folder must exist), and on ANY failure return ok = false with an error naming the file, leave the
// original byte-identical, and leave no temporary behind. It must not use a fixed `<path>.tmp` name — two
// writers of one file (the app and tools/diagnostics/regrade_ledger) would share it.
//
// The disk-full branch is not exercised: there is no portable way to fill a disk in a unit test. The
// permission cases are skipped when run as root (root writes anywhere).
//
//   cmake --build build/tests --target atomic_write_test -j 6
//   ctest --test-dir build/tests -R '^atomic_write_test$' --output-on-failure

#include "../atomic_write.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <cstdio>

#ifndef Q_OS_WIN
#include <unistd.h>
#endif

using namespace pinpoint;

static int  g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}

static QByteArray readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

static bool writePlain(const QString &path, const QByteArray &bytes)
{
    QFile f(path);
    return f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(bytes) == bytes.size();
}

// Every entry in `dir` (hidden ones included), so a stray temporary cannot hide behind a dot.
static QStringList entries(const QString &dir)
{
    return QDir(dir).entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                               QDir::Name);
}

static bool permissionsBite()
{
#ifdef Q_OS_WIN
    return false;   // POSIX directory permissions; Windows has no equivalent of a read-only folder here
#else
    return ::geteuid() != 0;
#endif
}

int main()
{
    QTemporaryDir tmp;
    if (!tmp.isValid()) {
        std::printf("  [FAIL] could not make a temporary directory\n");
        return 1;
    }
    const QByteArray oldBytes = QByteArrayLiteral("{\"version\":1,\"old\":true}");
    const QByteArray newBytes = QByteArrayLiteral("{\"version\":2,\"new\":\"a longer replacement\"}");

    std::printf("Replacing an existing file\n");
    {
        const QString dir  = tmp.filePath(QStringLiteral("replace"));
        const QString path = dir + QStringLiteral("/diagnostics.json");
        QDir().mkpath(dir);
        check(writePlain(path, oldBytes), "seeded the original");

        const AtomicWriteResult r = atomicWrite(path, newBytes);
        check(r.ok && r.error.isEmpty(), "atomicWrite over an existing file returns ok, no error");
        check(readAll(path) == newBytes, "the file now holds exactly the new bytes");
        check(entries(dir) == QStringList{QStringLiteral("diagnostics.json")},
              "no temporary is left beside it");

        // Shorter content must not leave a tail of the longer file behind.
        check(atomicWrite(path, oldBytes).ok && readAll(path) == oldBytes,
              "a shorter rewrite replaces the file whole (no stale tail)");
    }

    std::printf("Creating a new file in a missing directory\n");
    {
        const QString path = tmp.filePath(QStringLiteral("made/on/demand/tendencies.json"));
        check(atomicWrite(path, newBytes).ok, "atomicWrite creates the directory and the file");
        check(readAll(path) == newBytes, "the new file holds the bytes");
    }

    std::printf("A folder that must already exist is not made\n");
    {
        // A swing folder trashed under a sidecar write must not come back holding the sidecar alone.
        const QString dir  = tmp.filePath(QStringLiteral("trashed_swing"));
        const QString path = dir + QStringLiteral("/swing_phasegrid.json");
        const AtomicWriteResult r = atomicWrite(path, newBytes, AtomicWriteOptions::intoExistingFolder());
        check(!r.ok, "intoExistingFolder: a missing folder fails the write");
        check(r.error.contains(path), "the error names the file");
        check(!QFileInfo::exists(dir), "the folder was not re-made");
    }

    std::printf("Not a fixed <path>.tmp\n");
    {
        // A directory squatting on the old fixed temporary name made the old remove-then-rename
        // write fail every time. A uniquely named temporary does not care, and the squatter is left
        // exactly as it was.
        const QString dir  = tmp.filePath(QStringLiteral("squat"));
        const QString path = dir + QStringLiteral("/work_ons.json");
        QDir().mkpath(path + QStringLiteral(".tmp"));
        check(writePlain(path, oldBytes), "seeded the original");
        check(atomicWrite(path, newBytes).ok, "a <path>.tmp in the way does not stop the save");
        check(readAll(path) == newBytes, "the file holds the new bytes");
        check(QFileInfo(path + QStringLiteral(".tmp")).isDir(), "the <path>.tmp squatter is untouched");
    }

    if (permissionsBite()) {
        std::printf("A failed write leaves the original alone\n");
        {
            // An unwritable directory: no temporary can be made beside the file. With the direct-write
            // fallback off, the save must fail rather than write the file in place.
            const QString dir  = tmp.filePath(QStringLiteral("locked"));
            const QString path = dir + QStringLiteral("/club_baseline.json");
            QDir().mkpath(dir);
            check(writePlain(path, oldBytes), "seeded the original");
            const QFile::Permissions dirPerms = QFile::permissions(dir);
            check(QFile::setPermissions(dir, QFile::ReadOwner | QFile::ExeOwner), "made the directory read-only");

            const AtomicWriteResult r = atomicWrite(path, newBytes);
            check(!r.ok, "atomicWrite into an unwritable directory returns not ok");
            check(!r.error.isEmpty() && r.error.contains(path), "the error is set and names the file");
            std::printf("    error: %s / detail: %s\n", qPrintable(r.error), qPrintable(r.detail));
            check(readAll(path) == oldBytes, "the original is byte-identical");
            check(entries(dir) == QStringList{QStringLiteral("club_baseline.json")},
                  "nothing else appeared in the directory");

            QFile::setPermissions(dir, dirPerms);
        }

        std::printf("A read-only original is refused, not replaced\n");
        {
            const QString dir  = tmp.filePath(QStringLiteral("readonly"));
            const QString path = dir + QStringLiteral("/user.json");
            QDir().mkpath(dir);
            check(writePlain(path, oldBytes), "seeded the original");
            const QFile::Permissions filePerms = QFile::permissions(path);
            QFile::setPermissions(path, QFile::ReadOwner);

            check(!atomicWrite(path, newBytes).ok, "atomicWrite over a read-only file returns not ok");
            QFile::setPermissions(path, filePerms);
            check(readAll(path) == oldBytes, "the original is byte-identical");
            check(entries(dir) == QStringList{QStringLiteral("user.json")}, "no temporary is left behind");
        }

        std::printf("A failed rename leaves the old file\n");
        {
            // The temporary is made and filled, then the folder turns read-only before the commit: the
            // rename over the original is refused. The original must be whole and the result must say
            // the file could not be replaced.
            const QString dir  = tmp.filePath(QStringLiteral("rename"));
            const QString path = dir + QStringLiteral("/archived.json");
            QDir().mkpath(dir);
            check(writePlain(path, oldBytes), "seeded the original");
            const QFile::Permissions dirPerms = QFile::permissions(dir);

            const AtomicWriteResult r = atomicWrite(path, [&](QIODevice &out, QString &) {
                const bool wrote = out.write(newBytes) == newBytes.size();
                QFile::setPermissions(dir, QFile::ReadOwner | QFile::ExeOwner);   // before the rename
                return wrote;
            });
            QFile::setPermissions(dir, dirPerms);
            check(!r.ok, "a refused rename returns not ok");
            check(r.error.contains(path), "the error names the file");
            std::printf("    error: %s / detail: %s\n", qPrintable(r.error), qPrintable(r.detail));
            check(readAll(path) == oldBytes, "the original is byte-identical");
            // The temporary could not be deleted while the folder was read-only; it is now.
            const QStringList left = entries(dir);
            for (const QString &e : left)
                if (e != QStringLiteral("archived.json"))
                    QFile::remove(QDir(dir).filePath(e));
            check(readAll(path) == oldBytes, "the original is still whole after the clean-up");
        }
    } else {
        std::printf("Permission cases skipped (root, or Windows)\n");
    }

    std::printf("The streaming form\n");
    {
        const QString dir  = tmp.filePath(QStringLiteral("stream"));
        const QString path = dir + QStringLiteral("/clip.raw");
        QDir().mkpath(dir);
        check(writePlain(path, oldBytes), "seeded the original");

        // Written in pieces, the way the archive copies a file and the exporter writes its raw frames.
        QByteArray expected;
        const AtomicWriteResult r = atomicWrite(path, [&](QIODevice &out, QString &) {
            for (int i = 0; i < 64; ++i) {
                const QByteArray chunk = QByteArray(4096, char('a' + i % 26));
                expected += chunk;
                if (out.write(chunk) != chunk.size()) return false;
            }
            return true;
        });
        check(r.ok, "a streamed write returns ok");
        check(readAll(path) == expected, "the file holds every piece, in order");
        check(entries(dir) == QStringList{QStringLiteral("clip.raw")}, "no temporary is left beside it");

        // A writer that gives up: nothing changes, and its own reason is the error.
        const AtomicWriteResult gaveUp = atomicWrite(path, [&](QIODevice &out, QString &whyNot) {
            out.write(QByteArrayLiteral("half a fi"));
            whyNot = QStringLiteral("the source could not be read");
            return false;
        });
        check(!gaveUp.ok, "a writer that returns false fails the write");
        check(gaveUp.error == QStringLiteral("the source could not be read"), "the writer's own reason is the error");
        check(readAll(path) == expected, "the original is byte-identical");
        check(entries(dir) == QStringList{QStringLiteral("clip.raw")}, "the temporary is gone");

        // A writer with no reason of its own: the result says the disk may be full.
        const AtomicWriteResult silent = atomicWrite(path, [](QIODevice &, QString &) { return false; });
        check(!silent.ok && silent.error.contains(QStringLiteral("the disk may be full")),
              "a writer with no reason: 'the disk may be full'");
    }

    std::printf("AtomicFile, held across calls\n");
    {
        const QString dir  = tmp.filePath(QStringLiteral("held"));
        const QString path = dir + QStringLiteral("/report.pdf");
        QDir().mkpath(dir);
        check(writePlain(path, oldBytes), "seeded the original");

        {
            AtomicFile f(path, AtomicWriteOptions::intoExistingFolder());
            check(f.open().ok && f.isOpen() && f.device(), "open() makes the temporary and a device");
            check(f.write(QByteArrayLiteral("page 1;")) && f.device()->write("page 2;") == 7, "written in pieces, by write() and the device");
            check(readAll(path) == oldBytes, "nothing appears at the path before commit()");
            check(f.commit().ok, "commit() returns ok");
            check(readAll(path) == QByteArrayLiteral("page 1;page 2;"), "the file holds both pieces");
        }
        {
            AtomicFile f(path, AtomicWriteOptions::intoExistingFolder());
            check(f.open().ok, "a second document opens");
            f.write(QByteArrayLiteral("abandoned"));
            f.cancel();
            check(readAll(path) == QByteArrayLiteral("page 1;page 2;"), "cancel() leaves the original");
        }
        {
            AtomicFile f(path, AtomicWriteOptions::intoExistingFolder());
            check(f.open().ok, "a third document opens");
            f.write(QByteArrayLiteral("dropped mid-write"));
        }   // destroyed without commit()
        check(readAll(path) == QByteArrayLiteral("page 1;page 2;"), "destroyed without commit(): the original stays");
        check(entries(dir) == QStringList{QStringLiteral("report.pdf")}, "no temporary is left behind");

        AtomicFile unopened(path);
        check(!unopened.commit().ok, "commit() without open() fails");
        check(!unopened.write(QByteArrayLiteral("x")), "write() without open() fails");
    }

    std::printf("Text mode\n");
    {
        const QString path = tmp.filePath(QStringLiteral("text/rows.csv"));
        AtomicWriteOptions o;
        o.text = true;
        check(atomicWrite(path, QByteArrayLiteral("a,b\n1,2\n"), o).ok, "a text-mode write returns ok");
#ifdef Q_OS_WIN
        check(readAll(path) == QByteArrayLiteral("a,b\r\n1,2\r\n"), "text mode writes CRLF on Windows");
#else
        check(readAll(path) == QByteArrayLiteral("a,b\n1,2\n"), "text mode writes LF here");
#endif
    }

    std::printf("An empty path\n");
    {
        const AtomicWriteResult r = atomicWrite(QString(), newBytes);
        check(!r.ok, "an empty path returns not ok");
        check(!r.error.isEmpty(), "the error is set");
    }

    std::printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
