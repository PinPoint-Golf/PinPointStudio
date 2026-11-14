// Standalone tests for atomicWrite() (src/Diagnostics/engine/pack_io.h), the one helper every
// session and athlete cache (diagnostics.json, club_baseline.json, the fault profile, work_ons.json,
// the swing themes, tendencies.json) and every user pack/norm/drill/screen/gate-word/reference set
// is written through.
//
// What it must do: replace an existing file whole, create a missing one (and its directory), and on
// ANY failure return false with whyNot set and leave the original byte-identical, with no temporary
// left behind. It must not use a fixed `<path>.tmp` name — two writers of one file (the app and
// tools/diagnostics/regrade_ledger) would share it.
//
// The disk-full branch is not exercised: there is no portable way to fill a disk in a unit test.
//
//   cmake --build build/tests --target pack_io_test -j 6
//   ctest --test-dir build/tests -R '^pack_io_test$' --output-on-failure

#include "../engine/pack_io.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <cstdio>

using namespace pinpoint::analysis;

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

        QString why = QStringLiteral("untouched");
        check(atomicWrite(path, newBytes, &why), "atomicWrite over an existing file returns true");
        check(readAll(path) == newBytes, "the file now holds exactly the new bytes");
        check(entries(dir) == QStringList{QStringLiteral("diagnostics.json")},
              "no temporary is left beside it");

        // Shorter content must not leave a tail of the longer file behind.
        check(atomicWrite(path, oldBytes, &why) && readAll(path) == oldBytes,
              "a shorter rewrite replaces the file whole (no stale tail)");
    }

    std::printf("Creating a new file in a missing directory\n");
    {
        const QString path = tmp.filePath(QStringLiteral("made/on/demand/tendencies.json"));
        check(atomicWrite(path, newBytes), "atomicWrite creates the directory and the file");
        check(readAll(path) == newBytes, "the new file holds the bytes");
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
        QString why;
        check(atomicWrite(path, newBytes, &why), "a <path>.tmp in the way does not stop the save");
        check(readAll(path) == newBytes, "the file holds the new bytes");
        check(QFileInfo(path + QStringLiteral(".tmp")).isDir(), "the <path>.tmp squatter is untouched");
    }

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

        QString why;
        const bool ok = atomicWrite(path, newBytes, &why);
        check(!ok, "atomicWrite into an unwritable directory returns false");
        check(!why.isEmpty() && why.contains(path), "whyNot is set and names the file");
        std::printf("    whyNot: %s\n", qPrintable(why));
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

        QString why;
        check(!atomicWrite(path, newBytes, &why), "atomicWrite over a read-only file returns false");
        check(!why.isEmpty(), "whyNot is set");
        QFile::setPermissions(path, filePerms);
        check(readAll(path) == oldBytes, "the original is byte-identical");
        check(entries(dir) == QStringList{QStringLiteral("user.json")}, "no temporary is left behind");
    }

    std::printf("An empty path\n");
    {
        QString why;
        check(!atomicWrite(QString(), newBytes, &why), "an empty path returns false");
        check(why == QStringLiteral("No writable application data location."),
              "whyNot says there is no writable application data location");
        check(!atomicWrite(QString(), newBytes), "a null whyNot is allowed");
    }

    std::printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
