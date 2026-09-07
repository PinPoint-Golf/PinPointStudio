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

// SwingPaths — the on-disk session/swing layout: where a session goes, where a
// swing goes inside it, and what happens to a session that captured nothing.
//
// ⚠ EVERY EXPECTATION HERE COMES FROM A STATED CONTRACT OR FROM THE FILESYSTEM,
// NEVER FROM RUNNING THE CODE. The header says what a session folder is, what
// "extend" means, and that a discard must be recoverable; the filesystem says
// what is actually on disk afterwards. A test that called allocateSwingDir()
// and wrote down the name it got back would pass for ever and find nothing.
//
// The cases the suite is built around are the ones the arguments cannot reach:
// a library that is not writable (the /mnt/swingdata share unmounted, 1 Sept
// 2026), a session folder that already holds an afternoon's work, folders that
// vanish underneath the app, and athlete names that are legal in the UI and
// illegal on the volume.

#include "swing_paths.h"
#include "temp_library.h"

#include <QCoreApplication>
#include <QDate>
#include <QDir>
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
static const QString kUuid  = QStringLiteral("uuid-0001");
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
// N — the composed session name honours the documented patterns
// ---------------------------------------------------------------------------

static void testNamingPatterns()
{
    std::printf("\nN — session naming patterns\n");

    struct Case { const char *pattern; QString want; };
    const Case cases[] = {
        { "date-name-type", today() + "_" + kToken + "_" + kType },
        { "date-type-name", today() + "_" + kType  + "_" + kToken },
        { "name-date-type", kToken  + "_" + today() + "_" + kType },
        { "date-only",      today() },
    };

    for (const Case &c : cases) {
        TempLibrary lib;
        if (!lib.valid()) { skip(QStringLiteral("N %1").arg(c.pattern), QStringLiteral("no temp dir")); continue; }
        SwingPaths paths;
        const QString dir = paths.beginSession(lib.root(), kName, kUuid,
                                               QString::fromLatin1(c.pattern), kType, false);
        checkEq(QStringLiteral("N %1 folder name").arg(c.pattern),
                QFileInfo(dir).fileName(), c.want + QStringLiteral("_01"));
    }

    // An unnamed session type still produces a usable folder rather than a
    // trailing separator (the header: sessionTypeLabel is "empty when none").
    {
        TempLibrary lib;
        if (lib.valid()) {
            SwingPaths paths;
            const QString dir = paths.beginSession(lib.root(), kName, kUuid,
                                                   QStringLiteral("date-name-type"),
                                                   QString(), false);
            const QString name = QFileInfo(dir).fileName();
            check(QStringLiteral("N5 an empty session type leaves no dangling separator (\"%1\")")
                      .arg(name),
                  !name.contains(QStringLiteral("__")) && !name.endsWith(QLatin1Char('_')));
        }
    }
}

// ---------------------------------------------------------------------------
// A — allocateSwingDir(): where a swing lands
// ---------------------------------------------------------------------------

static void testAllocateSwingDir()
{
    std::printf("\nA — swing folder allocation\n");

    {
        TempLibrary lib;
        if (!lib.valid()) { skip(QStringLiteral("A1"), QStringLiteral("no temp dir")); return; }
        SwingPaths paths;

        const SwingPaths::Allocation a1 =
            paths.allocateSwingDir(lib.root(), kName, kUuid, QStringLiteral("date-name-type"), kType);
        checkEq(QStringLiteral("A1 first swing is swing_0001"), a1.swingId,
                QStringLiteral("swing_0001"));
        check(QStringLiteral("A2 the folder is created on disk"), TempLibrary::exists(a1.swingDir));
        checkEqInt(QStringLiteral("A3 its index matches its name"), a1.swingIndex, 1);

        const SwingPaths::Allocation a2 =
            paths.allocateSwingDir(lib.root(), kName, kUuid, QStringLiteral("date-name-type"), kType);
        checkEq(QStringLiteral("A4 second swing is swing_0002"), a2.swingId,
                QStringLiteral("swing_0002"));
        checkEq(QStringLiteral("A5 both swings share one session folder"),
                QFileInfo(a1.swingDir).absolutePath(), QFileInfo(a2.swingDir).absolutePath());
        checkEq(QStringLiteral("A6 both report the same session id"), a1.sessionId, a2.sessionId);

        // The identity the rest of the app relies on: the reported index and the
        // folder name are two encodings of one number, and swing.json carries
        // both. They must never disagree.
        check(QStringLiteral("A7 swingIndex and swingId are the same number"),
              a2.swingId == QStringLiteral("swing_%1").arg(a2.swingIndex, 4, 10, QLatin1Char('0')));
    }

    // A swing deleted mid-session must not cause the next allocation to land on
    // a folder that still exists — the probe loop exists for exactly this.
    {
        TempLibrary lib;
        if (!lib.valid()) { skip(QStringLiteral("A8"), QStringLiteral("no temp dir")); return; }
        SwingPaths paths;
        const SwingPaths::Allocation a1 =
            paths.allocateSwingDir(lib.root(), kName, kUuid, QStringLiteral("date-name-type"), kType);
        const SwingPaths::Allocation a2 =
            paths.allocateSwingDir(lib.root(), kName, kUuid, QStringLiteral("date-name-type"), kType);
        const SwingPaths::Allocation a3 =
            paths.allocateSwingDir(lib.root(), kName, kUuid, QStringLiteral("date-name-type"), kType);
        Q_UNUSED(a2);

        // Trash the middle one, then allocate again.
        QDir(a2.swingDir).removeRecursively();
        const SwingPaths::Allocation a4 =
            paths.allocateSwingDir(lib.root(), kName, kUuid, QStringLiteral("date-name-type"), kType);
        check(QStringLiteral("A8 an allocation after a delete does not collide (%1)").arg(a4.swingId),
              a4.swingDir != a1.swingDir && a4.swingDir != a3.swingDir);
        check(QStringLiteral("A9 the surviving swings are untouched"),
              TempLibrary::exists(a1.swingDir) && TempLibrary::exists(a3.swingDir));
        check(QStringLiteral("A10 the new folder was actually created"),
              TempLibrary::exists(a4.swingDir));
    }

    // The unwritable library. The contract (swing_paths.h + the ShotProcessor
    // comment that made sessionFolderReady exist) is that failure is REPORTED,
    // not swallowed: an empty swingDir, no crash, and nothing half-created.
    {
        TempLibrary lib;
        if (!lib.valid()) { skip(QStringLiteral("A11"), QStringLiteral("no temp dir")); return; }
        if (!makeUnwritable(lib.root())) {
            skip(QStringLiteral("A11 unwritable library"),
                 QStringLiteral("the platform will not make a directory unwritable here"));
        } else {
            SwingPaths paths;
            const SwingPaths::Allocation a =
                paths.allocateSwingDir(lib.root(), kName, kUuid,
                                       QStringLiteral("date-name-type"), kType);
            check(QStringLiteral("A11 an unwritable library yields no swing folder"),
                  a.swingDir.isEmpty());
            makeWritable(lib.root());
        }
    }

    // Switching athlete must not put one golfer's swings in another's session.
    {
        TempLibrary lib;
        if (!lib.valid()) { skip(QStringLiteral("A12"), QStringLiteral("no temp dir")); return; }
        SwingPaths paths;
        const SwingPaths::Allocation a =
            paths.allocateSwingDir(lib.root(), kName, kUuid, QStringLiteral("date-name-type"), kType);
        const SwingPaths::Allocation b =
            paths.allocateSwingDir(lib.root(), QStringLiteral("Other Golfer"),
                                   QStringLiteral("uuid-0002"),
                                   QStringLiteral("date-name-type"), kType);
        check(QStringLiteral("A12 a different athlete gets a different session folder"),
              QFileInfo(a.swingDir).absolutePath() != QFileInfo(b.swingDir).absolutePath());
        check(QStringLiteral("A13 and a different athlete folder"),
              !b.swingDir.startsWith(lib.athleteDir(kToken) + QLatin1Char('/')));
    }
}

// ---------------------------------------------------------------------------
// B — beginSession() / findTodaySessionDir(): extend today, or start fresh
// ---------------------------------------------------------------------------

static void testBeginSession()
{
    std::printf("\nB — explicit session start\n");

    {
        TempLibrary lib;
        if (!lib.valid()) { skip(QStringLiteral("B1"), QStringLiteral("no temp dir")); return; }
        SwingPaths paths;

        check(QStringLiteral("B1 no session folder exists before the first start"),
              paths.findTodaySessionDir(lib.root(), kName, kUuid,
                                        QStringLiteral("date-name-type"), kType).isEmpty());

        const QString s1 = paths.beginSession(lib.root(), kName, kUuid,
                                              QStringLiteral("date-name-type"), kType, false);
        checkEq(QStringLiteral("B2 the first session is _01"), QFileInfo(s1).fileName(),
                baseDefault() + QStringLiteral("_01"));
        check(QStringLiteral("B3 it is created on disk"), TempLibrary::exists(s1));
        checkEq(QStringLiteral("B4 currentSessionDir reports it"), paths.currentSessionDir(), s1);

        paths.endSession(false);
        const QString s2 = paths.beginSession(lib.root(), kName, kUuid,
                                              QStringLiteral("date-name-type"), kType, false);
        checkEq(QStringLiteral("B5 a second new session is _02"), QFileInfo(s2).fileName(),
                baseDefault() + QStringLiteral("_02"));
    }

    // Extend: reuse today's most-recent folder and do NOT bump the counter.
    {
        TempLibrary lib;
        if (!lib.valid()) { skip(QStringLiteral("B6"), QStringLiteral("no temp dir")); return; }
        const QString existing = lib.makeSession(kToken, baseDefault() + QStringLiteral("_01"), 3);
        check(QStringLiteral("B6 fixture session exists with 3 swings"),
              TempLibrary::countSwings(existing) == 3);

        SwingPaths paths;
        checkEq(QStringLiteral("B7 findTodaySessionDir locates it"),
                paths.findTodaySessionDir(lib.root(), kName, kUuid,
                                          QStringLiteral("date-name-type"), kType),
                existing);

        const QString s = paths.beginSession(lib.root(), kName, kUuid,
                                             QStringLiteral("date-name-type"), kType, true);
        checkEq(QStringLiteral("B8 extend reuses the existing folder"), s, existing);
        checkEqInt(QStringLiteral("B9 extend adds no second session folder"),
                   TempLibrary::countSessions(lib.athleteDir(kToken)), 1);
    }

    // findTodaySessionDir must return the MOST RECENT, not the first.
    {
        TempLibrary lib;
        if (!lib.valid()) { skip(QStringLiteral("B10"), QStringLiteral("no temp dir")); return; }
        lib.makeSession(kToken, baseDefault() + QStringLiteral("_01"), 1);
        lib.makeSession(kToken, baseDefault() + QStringLiteral("_02"), 1);
        const QString third = lib.makeSession(kToken, baseDefault() + QStringLiteral("_03"), 1);
        SwingPaths paths;
        checkEq(QStringLiteral("B10 the most recent session is found"),
                paths.findTodaySessionDir(lib.root(), kName, kUuid,
                                          QStringLiteral("date-name-type"), kType),
                third);
        const QString s = paths.beginSession(lib.root(), kName, kUuid,
                                             QStringLiteral("date-name-type"), kType, false);
        checkEq(QStringLiteral("B11 a new session continues the counter"),
                QFileInfo(s).fileName(), baseDefault() + QStringLiteral("_04"));
    }

    // Extend with nothing to extend: fall back to a fresh folder rather than
    // failing (the header: "extendExisting reuses today's most-recent folder ...
    // otherwise a fresh _NN is allocated").
    {
        TempLibrary lib;
        if (!lib.valid()) { skip(QStringLiteral("B12"), QStringLiteral("no temp dir")); return; }
        SwingPaths paths;
        const QString s = paths.beginSession(lib.root(), kName, kUuid,
                                             QStringLiteral("date-name-type"), kType, true);
        check(QStringLiteral("B12 extend with no today folder still starts a session"),
              !s.isEmpty() && TempLibrary::exists(s));
    }

    // ⭐ The 1 September 2026 case. beginSession must ANSWER that it failed —
    // ShotProcessor::sessionFolderReady() is built on this return value, and a
    // session that starts on an unwritable library loses every swing silently.
    {
        TempLibrary lib;
        if (!lib.valid()) { skip(QStringLiteral("B13"), QStringLiteral("no temp dir")); return; }
        if (!makeUnwritable(lib.root())) {
            skip(QStringLiteral("B13 unwritable library"),
                 QStringLiteral("the platform will not make a directory unwritable here"));
        } else {
            SwingPaths paths;
            const QString s = paths.beginSession(lib.root(), kName, kUuid,
                                                 QStringLiteral("date-name-type"), kType, false);
            check(QStringLiteral("B13 an unwritable library reports a failed session start"),
                  s.isEmpty());
            check(QStringLiteral("B14 and leaves no session cached"),
                  paths.currentSessionDir().isEmpty());
            makeWritable(lib.root());
        }
    }

    // A session started explicitly must own every swing of that session — the
    // whole reason beginSession primes the cache.
    {
        TempLibrary lib;
        if (!lib.valid()) { skip(QStringLiteral("B15"), QStringLiteral("no temp dir")); return; }
        SwingPaths paths;
        const QString s = paths.beginSession(lib.root(), kName, kUuid,
                                             QStringLiteral("date-name-type"), kType, false);
        const SwingPaths::Allocation a =
            paths.allocateSwingDir(lib.root(), kName, kUuid, QStringLiteral("date-name-type"), kType);
        checkEq(QStringLiteral("B15 a swing lands in the session that was started"),
                QFileInfo(a.swingDir).absolutePath(), s);
        checkEqInt(QStringLiteral("B16 and no second session folder appears"),
                   TempLibrary::countSessions(lib.athleteDir(kToken)), 1);
    }
}

// ---------------------------------------------------------------------------
// E — endSession(): discard what this session created, and NOTHING else
// ---------------------------------------------------------------------------
//
// The contract, from swing_paths.h: "When discardIfNoNewSwings and the folder
// gained no swings since beginSession() ... move it to the OS trash". The
// purpose is to clean up a session folder the app itself just created and then
// left empty. It is not a licence to delete work that was already there — an
// extended session's folder holds an afternoon that predates this session, and
// endSession has no claim on any of it.

static void testEndSession()
{
    std::printf("\nE — ending a session discards only what it created\n");

    // The intended case: a fresh session that captured nothing.
    {
        TempLibrary lib;
        if (!lib.valid()) { skip(QStringLiteral("E1"), QStringLiteral("no temp dir")); return; }
        SwingPaths paths;
        const QString s = paths.beginSession(lib.root(), kName, kUuid,
                                             QStringLiteral("date-name-type"), kType, false);
        paths.endSession(true);
        check(QStringLiteral("E1 an empty new session is discarded"), !TempLibrary::exists(s));
        check(QStringLiteral("E2 and the cache is cleared"), paths.currentSessionDir().isEmpty());
    }

    // A fresh session that captured something is kept.
    {
        TempLibrary lib;
        if (!lib.valid()) { skip(QStringLiteral("E3"), QStringLiteral("no temp dir")); return; }
        SwingPaths paths;
        const QString s = paths.beginSession(lib.root(), kName, kUuid,
                                             QStringLiteral("date-name-type"), kType, false);
        paths.allocateSwingDir(lib.root(), kName, kUuid, QStringLiteral("date-name-type"), kType);
        paths.endSession(true);
        check(QStringLiteral("E3 a new session with a swing survives"), TempLibrary::exists(s));
        checkEqInt(QStringLiteral("E4 with its swing intact"), TempLibrary::countSwings(s), 1);
    }

    // ⭐⭐ THE ONE THAT LOSES DATA.
    // The golfer hit 20 balls this morning, comes back after lunch and chooses
    // "extend today's session", warms up, decides against hitting and stops.
    // The session gained nothing — and the folder it did not create holds all
    // 20 of this morning's swings.
    {
        TempLibrary lib;
        if (!lib.valid()) { skip(QStringLiteral("E5"), QStringLiteral("no temp dir")); return; }
        const QString existing = lib.makeSession(kToken, baseDefault() + QStringLiteral("_01"), 20);
        SwingPaths paths;
        const QString s = paths.beginSession(lib.root(), kName, kUuid,
                                             QStringLiteral("date-name-type"), kType, true);
        checkEq(QStringLiteral("E5 the extended session is this morning's folder"), s, existing);
        paths.endSession(true);
        check(QStringLiteral("E6 an extended session that captured nothing is NOT discarded"),
              TempLibrary::exists(existing));
        checkEqInt(QStringLiteral("E7 and this morning's 20 swings survive"),
                   TempLibrary::countSwings(existing), 20);
    }

    // ⭐ The same loss reached by a different route: an extended session where
    // the golfer captured one shot and deleted one of the morning's. The count
    // is unchanged, so a count-based rule reads it as "captured nothing" — and
    // takes the whole folder.
    {
        TempLibrary lib;
        if (!lib.valid()) { skip(QStringLiteral("E8"), QStringLiteral("no temp dir")); return; }
        const QString existing = lib.makeSession(kToken, baseDefault() + QStringLiteral("_01"), 5);
        SwingPaths paths;
        paths.beginSession(lib.root(), kName, kUuid, QStringLiteral("date-name-type"), kType, true);
        // One new swing captured this session ...
        const SwingPaths::Allocation a =
            paths.allocateSwingDir(lib.root(), kName, kUuid, QStringLiteral("date-name-type"), kType);
        check(QStringLiteral("E8 the new swing landed in the extended session"),
              !a.swingDir.isEmpty() && QFileInfo(a.swingDir).absolutePath() == existing);
        // ... and one of the morning's deleted from the carousel.
        QDir(existing + QStringLiteral("/swing_0001")).removeRecursively();

        paths.endSession(true);
        check(QStringLiteral("E9 a session that captured a swing is never discarded"),
              TempLibrary::exists(existing));
        check(QStringLiteral("E10 and the swing it captured is still there"),
              TempLibrary::exists(a.swingDir));
    }

    // discardIfNoNewSwings=false must never remove anything.
    {
        TempLibrary lib;
        if (!lib.valid()) { skip(QStringLiteral("E11"), QStringLiteral("no temp dir")); return; }
        SwingPaths paths;
        const QString s = paths.beginSession(lib.root(), kName, kUuid,
                                             QStringLiteral("date-name-type"), kType, false);
        paths.endSession(false);
        check(QStringLiteral("E11 endSession(false) keeps an empty session"),
              TempLibrary::exists(s));
    }

    // No session in progress: safe, and does not reach for anything.
    {
        TempLibrary lib;
        if (!lib.valid()) { skip(QStringLiteral("E12"), QStringLiteral("no temp dir")); return; }
        const QString orphan = lib.makeSession(kToken, baseDefault() + QStringLiteral("_01"), 2);
        SwingPaths paths;
        paths.endSession(true);          // never begun
        check(QStringLiteral("E12 endSession with no session in progress touches nothing"),
              TempLibrary::exists(orphan) && TempLibrary::countSwings(orphan) == 2);
    }

    // Ending twice must not reach into the next session (the cache is cleared,
    // so the second call has nothing to act on).
    {
        TempLibrary lib;
        if (!lib.valid()) { skip(QStringLiteral("E13"), QStringLiteral("no temp dir")); return; }
        SwingPaths paths;
        paths.beginSession(lib.root(), kName, kUuid, QStringLiteral("date-name-type"), kType, false);
        paths.allocateSwingDir(lib.root(), kName, kUuid, QStringLiteral("date-name-type"), kType);
        paths.endSession(true);
        const QString kept = lib.athleteDir(kToken) + QLatin1Char('/') + baseDefault()
                           + QStringLiteral("_01");
        paths.endSession(true);          // again, with nothing in progress
        check(QStringLiteral("E13 a second endSession does not remove the finished one"),
              TempLibrary::exists(kept));
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

    {
        TempLibrary lib;
        if (!lib.valid()) { skip(QStringLiteral("T3"), QStringLiteral("no temp dir")); return; }
        const QString session = lib.makeSession(kToken, baseDefault() + QStringLiteral("_01"), 2);
        const QString swing   = session + QStringLiteral("/swing_0001");
        QString where;
        const bool ok = SwingPaths::trashPath(swing, &where);
        check(QStringLiteral("T3 a swing folder can be trashed (to %1)").arg(where), ok);
        if (ok) {
            check(QStringLiteral("T4 it leaves its original location"), !TempLibrary::exists(swing));
            checkEqInt(QStringLiteral("T5 and its session keeps the others"),
                       TempLibrary::countSwings(session), 1);
        }
    }

    // The share case: no OS trash on the volume, so the folder must move into
    // the library-local .pinpoint-trash rather than being lost or left in place.
    // Reachable only where the platform has no trash for the temp volume, so
    // this reports SKIP rather than a false pass when the OS trash worked.
    {
        TempLibrary lib;
        if (!lib.valid()) { skip(QStringLiteral("T6"), QStringLiteral("no temp dir")); return; }
        const QString session = lib.makeSession(kToken, baseDefault() + QStringLiteral("_02"), 1);
        QString where;
        if (!SwingPaths::trashPath(session, &where)) {
            check(QStringLiteral("T6 a session that cannot be trashed stays put"),
                  TempLibrary::exists(session));
        } else if (where == QStringLiteral("the system trash")) {
            skip(QStringLiteral("T6 library-local trash fallback"),
                 QStringLiteral("this volume has an OS trash, so the fallback never runs"));
        } else {
            check(QStringLiteral("T6 the fallback lands under .pinpoint-trash"),
                  where.contains(QStringLiteral("/.pinpoint-trash/")));
            check(QStringLiteral("T7 and the folder is recoverable there"),
                  TempLibrary::exists(where));
        }
    }
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);   // QStandardPaths / QTemporaryDir
    std::printf("swing_paths_test — session and swing folder layout\n");
    testSanitise();
    testNamingPatterns();
    testAllocateSwingDir();
    testBeginSession();
    testEndSession();
    testTrashPath();
    std::printf("\n%d checks, %d failed, %d skipped\n", g_run, g_fail, g_skip);
    return g_fail == 0 ? 0 : 1;
}
