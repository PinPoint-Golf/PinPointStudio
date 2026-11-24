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

// SwingPaths — the library's names: sanitise, the composed session and swing folder names, the one
// swing_* listing, and the trash.
//
// ⚠ EVERY EXPECTATION HERE COMES FROM A STATED CONTRACT OR FROM THE FILESYSTEM, NEVER FROM RUNNING THE
// CODE. Athlete names that are legal in the UI and illegal on the volume; a delete that must be
// recoverable on a volume with no trash.
//
// The session allocator's sections (N, A, R, B, E, K) moved with the allocator to
// session_repository_test (refactor step 2.2): where a session goes, where a swing goes inside it, and
// what happens to a session that captured nothing are SessionRepository's now.

#include "swing_paths.h"
#include "temp_library.h"

#include <QCoreApplication>
#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QStringList>

#include <cstdio>

using namespace pinpoint;
using namespace pinpoint::test;

static int g_fail = 0;
static int g_run  = 0;
static int g_skip = 0;

static void check(const QString &label, bool ok)
{
    ++g_run;
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", qPrintable(label));
    if (!ok) ++g_fail;
}

static void checkEq(const QString &label, const QString &got, const QString &want)
{
    check(QStringLiteral("%1 (got \"%2\", want \"%3\")").arg(label, got, want), got == want);
}

static void checkEqInt(const QString &label, int got, int want)
{
    check(QStringLiteral("%1 (got %2, want %3)").arg(label).arg(got).arg(want), got == want);
}

static void skip(const QString &label, const QString &why)
{
    ++g_skip;
    std::printf("  [SKIP] %s — %s\n", qPrintable(label), qPrintable(why));
}

static QString today()
{
    return QDate::currentDate().toString(Qt::ISODate);
}

// The suite's standard athlete: name as typed, and the token it sanitises to.
static const QString kName  = QStringLiteral("Mark Liversedge");
static const QString kToken = QStringLiteral("Mark-Liversedge");
static const QString kType  = QStringLiteral("Swing");

// The composed session base for the default pattern, per the documented token
// order "date-name-type". Computed here from the contract, not read back.
static QString baseDefault()
{
    return today() + QLatin1Char('_') + kToken + QLatin1Char('_') + kType;
}

// ---------------------------------------------------------------------------
// S — sanitise(): a name legal in the UI must become a name legal on the volume
// ---------------------------------------------------------------------------

static void testSanitise()
{
    std::printf("\nS — sanitise() produces a filesystem-safe token\n");

    checkEq("S1 spaces become separators", SwingPaths::sanitise(kName), kToken);
    checkEq("S2 path-hostile characters are replaced",
            SwingPaths::sanitise(QStringLiteral("a/b\\c:d*e?f")), QStringLiteral("a-b-c-d-e-f"));
    checkEq("S3 runs of separators collapse",
            SwingPaths::sanitise(QStringLiteral("a   b")), QStringLiteral("a-b"));
    checkEq("S4 leading and trailing separators are stripped",
            SwingPaths::sanitise(QStringLiteral("  .-name-.  ")), QStringLiteral("name"));
    checkEq("S5 an empty name falls back", SwingPaths::sanitise(QString()),
            QStringLiteral("unknown"));
    checkEq("S6 an all-hostile name falls back", SwingPaths::sanitise(QStringLiteral("///")),
            QStringLiteral("unknown"));
    checkEq("S7 a lone dot cannot become a relative path",
            SwingPaths::sanitise(QStringLiteral(".")), QStringLiteral("unknown"));
    checkEq("S8 a double dot cannot become a parent traversal",
            SwingPaths::sanitise(QStringLiteral("..")), QStringLiteral("unknown"));

    // Idempotence: a token that has already been sanitised is already safe, so
    // running it through again must not change it. Anything that fails this is
    // producing output it would not accept as input.
    const QStringList corpus = {
        kName, QStringLiteral("Bjørn Ørsted"), QStringLiteral("O'Brien-Smith"),
        QStringLiteral("  spaced  out  "), QStringLiteral("...dots..."),
        QStringLiteral("汉字の名前"), QStringLiteral("a"), QStringLiteral("-"),
        QString(64, QLatin1Char('x')), QString(200, QLatin1Char('y')),
    };
    bool idempotent = true;
    for (const QString &raw : corpus) {
        const QString once = SwingPaths::sanitise(raw);
        if (SwingPaths::sanitise(once) != once) {
            idempotent = false;
            std::printf("        not idempotent: \"%s\" -> \"%s\" -> \"%s\"\n",
                        qPrintable(raw), qPrintable(once),
                        qPrintable(SwingPaths::sanitise(once)));
        }
    }
    check(QStringLiteral("S9 sanitise is idempotent over the corpus"), idempotent);

    // ⭐ The truncation happens AFTER the ends are stripped, so a long name can
    // be cut back to a trailing separator. Windows silently drops a trailing
    // dot from a directory name, after which the folder the app computes and
    // the folder that exists are different strings — every later lookup misses.
    {
        // 63 legal characters then a dot: the 64-char truncation keeps the dot.
        const QString raw = QString(63, QLatin1Char('a')) + QStringLiteral(".tail");
        const QString out = SwingPaths::sanitise(raw);
        check(QStringLiteral("S10 a truncated token never ends in '.' or '-' (got \"%1\")")
                  .arg(out),
              !out.endsWith(QLatin1Char('.')) && !out.endsWith(QLatin1Char('-')));
    }
    {
        const QString raw = QString(63, QLatin1Char('b')) + QStringLiteral("-tail");
        const QString out = SwingPaths::sanitise(raw);
        check(QStringLiteral("S11 a truncated token never ends in '-' (got \"%1\")").arg(out),
              !out.endsWith(QLatin1Char('-')));
    }

    // ⭐ Windows reserved device names. "Con" is a perfectly ordinary surname
    // and an impossible directory: every mkpath under it fails, on that
    // platform only, for a library that works everywhere else.
    {
        const QStringList reserved = { QStringLiteral("CON"), QStringLiteral("PRN"),
                                       QStringLiteral("AUX"), QStringLiteral("NUL"),
                                       QStringLiteral("COM1"), QStringLiteral("LPT1") };
        bool safe = true;
        for (const QString &r : reserved) {
            const QString out = SwingPaths::sanitise(r);
            if (out.compare(r, Qt::CaseInsensitive) == 0) {
                safe = false;
                std::printf("        reserved device name survives: \"%s\"\n", qPrintable(out));
            }
        }
        check(QStringLiteral("S12 a reserved Windows device name is not returned verbatim"), safe);
    }
}

// ---------------------------------------------------------------------------
// T — trashPath(): a delete is recoverable, or it does not happen
// ---------------------------------------------------------------------------

static void testTrashPath()
{
    std::printf("\nT — deletes are recoverable\n");

    check(QStringLiteral("T1 an empty path is refused"), !SwingPaths::trashPath(QString()));
    check(QStringLiteral("T2 a path that does not exist is refused"),
          !SwingPaths::trashPath(QStringLiteral("/no/such/path/anywhere-12345")));

    // ⚠ NEVER THE OS TRASH: every trash here passes useOsTrash = false (step 3.4a, review round 3), so a run
    // never puts a folder in the trash of the machine that runs it. The library-local trash is the very route a
    // share without an OS trash takes, so it is the one tested: <root>/.pinpoint-trash/<athlete>/<stamp>_[<session>_]<name>.
    {
        TempLibrary lib;
        if (!lib.valid()) { skip(QStringLiteral("T3"), QStringLiteral("no temp dir")); return; }
        const QString session = lib.makeSession(kToken, baseDefault() + QStringLiteral("_01"), 2);
        const QString swing   = session + QStringLiteral("/swing_0001");
        QString where;
        const bool ok = SwingPaths::trashPath(swing, &where, /*useOsTrash=*/false);
        check(QStringLiteral("T3 a swing folder can be trashed (to %1)").arg(where), ok);
        if (ok) {
            check(QStringLiteral("T4 it leaves its original location"), !TempLibrary::exists(swing));
            checkEqInt(QStringLiteral("T5 and its session keeps the others"),
                       TempLibrary::countSwings(session), 1);
            const QString trash = lib.root() + QStringLiteral("/.pinpoint-trash/") + kToken + QLatin1Char('/');
            check(QStringLiteral("T5b it is in the library's own trash, named for its session"),
                  where.startsWith(trash) && TempLibrary::exists(where)
                      && where.endsWith(QLatin1Char('_') + QFileInfo(session).fileName() + QStringLiteral("_swing_0001")));
        }
    }

    // The share case: no OS trash on the volume, so the folder must move into
    // the library-local .pinpoint-trash rather than being lost or left in place.
    {
        TempLibrary lib;
        if (!lib.valid()) { skip(QStringLiteral("T6"), QStringLiteral("no temp dir")); return; }
        const QString session = lib.makeSession(kToken, baseDefault() + QStringLiteral("_02"), 1);
        QString where;
        const bool ok = SwingPaths::trashPath(session, &where, /*useOsTrash=*/false);
        check(QStringLiteral("T6 the session lands under the library's own .pinpoint-trash (to %1)").arg(where),
              ok && where.startsWith(lib.root() + QStringLiteral("/.pinpoint-trash/") + kToken + QLatin1Char('/'))
                  && where.endsWith(QLatin1Char('_') + QFileInfo(session).fileName()));
        check(QStringLiteral("T7 and the folder is recoverable there, gone from the library"),
              ok && TempLibrary::exists(where) && !TempLibrary::exists(session));
    }
}

// ---------------------------------------------------------------------------
// M — the names (refactor step 2.1): SwingPaths is the one composer of a
// session or swing folder's name and the one reader of a swing folder's number
// ---------------------------------------------------------------------------

static void testNames()
{
    std::printf("\nM — the names\n");

    const QDate day(2026, 6, 5);
    // The documented token orders, spelled out here from the header, not read back.
    checkEq(QStringLiteral("M1 date-name-type base"),
            SwingPaths::sessionFolderBase(QStringLiteral("date-name-type"), day, kToken, kType),
            QStringLiteral("2026-06-05_Mark-Liversedge_Swing"));
    checkEq(QStringLiteral("M2 date-type-name base"),
            SwingPaths::sessionFolderBase(QStringLiteral("date-type-name"), day, kToken, kType),
            QStringLiteral("2026-06-05_Swing_Mark-Liversedge"));
    checkEq(QStringLiteral("M3 name-date-type base"),
            SwingPaths::sessionFolderBase(QStringLiteral("name-date-type"), day, kToken, kType),
            QStringLiteral("Mark-Liversedge_2026-06-05_Swing"));
    checkEq(QStringLiteral("M4 date-only base"),
            SwingPaths::sessionFolderBase(QStringLiteral("date-only"), day, kToken, kType),
            QStringLiteral("2026-06-05"));
    checkEq(QStringLiteral("M5 an unknown pattern key is the default"),
            SwingPaths::sessionFolderBase(QStringLiteral("no-such-pattern"), day, kToken, kType),
            QStringLiteral("2026-06-05_Mark-Liversedge_Swing"));
    checkEq(QStringLiteral("M6 no session type says \"session\""),
            SwingPaths::sessionFolderBase(QStringLiteral("date-name-type"), day, kToken, QString()),
            QStringLiteral("2026-06-05_Mark-Liversedge_session"));
    checkEq(QStringLiteral("M7 the counter has at least two digits"),
            SwingPaths::sessionFolderName(QStringLiteral("b"), 3), QStringLiteral("b_03"));
    checkEq(QStringLiteral("M8 …and more when it needs them"),
            SwingPaths::sessionFolderName(QStringLiteral("b"), 123), QStringLiteral("b_123"));

    checkEq(QStringLiteral("M9 swing 7's folder"), SwingPaths::swingFolderName(7), QStringLiteral("swing_0007"));
    checkEq(QStringLiteral("M10 swing 12345's folder"), SwingPaths::swingFolderName(12345), QStringLiteral("swing_12345"));
    check(QStringLiteral("M11 no folder for swing 0 or below"),
          SwingPaths::swingFolderName(0).isEmpty() && SwingPaths::swingFolderName(-1).isEmpty());
    checkEqInt(QStringLiteral("M12 the number of swing_0007"), SwingPaths::swingNumberOf(QStringLiteral("swing_0007")), 7);
    checkEqInt(QStringLiteral("M13 a name and its number round-trip"),
               SwingPaths::swingNumberOf(SwingPaths::swingFolderName(42)), 42);
    check(QStringLiteral("M14 names that are not a swing folder have no number"),
          SwingPaths::swingNumberOf(QStringLiteral("swing_")) == 0
              && SwingPaths::swingNumberOf(QStringLiteral("swing_abc")) == 0
              && SwingPaths::swingNumberOf(QStringLiteral("swing_0000")) == 0
              && SwingPaths::swingNumberOf(QStringLiteral("2026-06-05_Mark_Swing_07")) == 0
              && SwingPaths::swingNumberOf(QString()) == 0);

    TempLibrary lib;
    if (!lib.valid()) { skip(QStringLiteral("M15–M17"), QStringLiteral("no temp dir")); return; }
    const QString session = lib.root() + QStringLiteral("/") + kToken + QStringLiteral("/s_01");
    QDir().mkpath(session + QStringLiteral("/swing_0002"));
    QDir().mkpath(session + QStringLiteral("/swing_0001"));
    QDir().mkpath(session + QStringLiteral("/notes"));
    QFile f(session + QStringLiteral("/swing_0003"));   // a FILE of that name is not a swing folder
    check(QStringLiteral("M15 (setup) a file named swing_0003"), f.open(QIODevice::WriteOnly));
    f.close();
    const QStringList got = SwingPaths::swingFolders(session);
    checkEq(QStringLiteral("M15 the session's swing folders, by name, and nothing else"),
            got.join(QLatin1Char(',')), QStringLiteral("swing_0001,swing_0002"));
    check(QStringLiteral("M16 a missing session folder has none"),
          SwingPaths::swingFolders(session + QStringLiteral("/gone")).isEmpty());
    checkEqInt(QStringLiteral("M17 the next counter is one past the highest _NN of that base"),
               SwingPaths::nextSessionCounter(lib.root() + QStringLiteral("/") + kToken, QStringLiteral("s")), 2);
}

// visitLibrarySwings (refactor step 3.9): the Diagnostic Model's library scans walk through it.
static void testLibraryWalk()
{
    TempLibrary lib;
    if (!lib.valid()) { skip(QStringLiteral("W1–W4"), QStringLiteral("no temp dir")); return; }
    const QString root = lib.root();
    // Two athletes, made out of order; sessions with swings out of order, a non-swing folder, a FILE named
    // like a swing, an empty session, and the library's trash at the top (a dot-named folder: hidden from this listing
    // on macOS and Linux, listed on Windows; #242).
    for (const char *d : { "zed/s_02/swing_0001", "ann/s_02/swing_0003", "ann/s_02/swing_0001", "ann/s_02/notes",
                           "ann/s_01/swing_0002", "ann/s_03", ".pinpoint-trash/x/swing_0001" })
        QDir().mkpath(root + QLatin1Char('/') + QLatin1String(d));
    QFile f(root + QStringLiteral("/ann/s_01/swing_0004"));
    check(QStringLiteral("W1 (setup) a file named swing_0004"), f.open(QIODevice::WriteOnly));
    f.close();

    QStringList seen;
    SwingPaths::visitLibrarySwings(root, [&](const QString &session, const QString &swingDir) {
        seen << session + QLatin1Char(':') + QDir(root).relativeFilePath(swingDir);
        return true;
    });
    checkEq(QStringLiteral("W2 athlete, then session, then swing, each by name; only swing folders"),
            seen.join(QLatin1Char(',')),
            QStringLiteral("s_01:ann/s_01/swing_0002,s_02:ann/s_02/swing_0001,s_02:ann/s_02/swing_0003,"
                           "s_02:zed/s_02/swing_0001"));

    QStringList first;
    SwingPaths::visitLibrarySwings(root, [&](const QString &, const QString &swingDir) {
        first << QDir(root).relativeFilePath(swingDir);
        return first.size() < 2;
    });
    checkEq(QStringLiteral("W3 the walk stops when the visitor says so"), first.join(QLatin1Char(',')),
            QStringLiteral("ann/s_01/swing_0002,ann/s_02/swing_0001"));

    int none = 0;
    SwingPaths::visitLibrarySwings(root + QStringLiteral("/gone"), [&](const QString &, const QString &) {
        ++none;
        return true;
    });
    checkEqInt(QStringLiteral("W4 a missing library has no swings"), none, 0);

    // #242: THE TOP LEVEL AS WINDOWS LISTS IT. W2 passed here only because macOS hides a dot-named folder from a QDir
    // listing; Windows lists it, and the walk visited the trash as an athlete. QDir::Hidden shows this platform what
    // Windows sees, so the skip rule is proved here too, not only on the cabin.
    const QDir::Filters windows = QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden;
    check(QStringLiteral("W5 (setup) listed as Windows lists it, the top level holds the trash"),
          QDir(root).entryList(windows, QDir::Name).contains(SwingPaths::trashFolderName()));
    checkEq(QStringLiteral("W6 the library's top level, listed as Windows lists it: its athletes, never the trash"),
            SwingPaths::libraryFolders(root, windows).join(QLatin1Char(',')), QStringLiteral("ann,zed"));
    checkEq(QStringLiteral("W7 the default listing: the same athletes"),
            SwingPaths::libraryFolders(root).join(QLatin1Char(',')), QStringLiteral("ann,zed"));
    check(QStringLiteral("W8 the trash is known by its one name, the folder trashPath makes"),
          SwingPaths::trashFolderName() == QStringLiteral(".pinpoint-trash")
              && SwingPaths::isTrashFolder(SwingPaths::trashFolderName())
              && !SwingPaths::isTrashFolder(QStringLiteral("ann")) && !SwingPaths::isTrashFolder(QStringLiteral("pinpoint-trash")));
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);   // QStandardPaths / QTemporaryDir
    std::printf("swing_paths_test — session and swing folder layout\n");
    testSanitise();
    testTrashPath();
    testNames();
    testLibraryWalk();
    std::printf("\n%d checks, %d failed, %d skipped\n", g_run, g_fail, g_skip);
    return g_fail == 0 ? 0 : 1;
}
