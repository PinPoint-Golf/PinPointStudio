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

// session_identity_test — SessionIdentity (src/DomainModel/session_identity.*; refactor step 2.1,
// brief §4.2 and D3).
//
// The contract: a session's local day and its place in that day ("n-of-day") come from its start
// time under every naming pattern, `name-date-type` included (F-06: under that pattern no folder
// starts with its date, and every `left(10)` parse ordered it nowhere); a session without a session
// file is read from its folder's name — its date, type and _NN — and gets D3's name-based id, the
// same every time, on every machine, and the same as Python's uuid.uuid5 gives (so a tool can
// reproduce it). The golden ids below were computed with Python's `uuid.uuid5`, not by this code.
//
// The names are composed by SwingPaths (the one composer) wherever a test needs "a name this app
// made", so the reader is checked against the writer, not against a copy of it.

#include "session_identity.h"
#include "swing_paths.h"

#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QTimeZone>
#include <QUuid>

#include <algorithm>
#include <cstdio>

using namespace pinpoint;

static int g_fail = 0;
static int g_run  = 0;

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

static const AthleteId kAthlete = AthleteId::fromString(QStringLiteral("3f2c8f2e-1d2b-4c6a-9b1e-7a5d4c3b2a10"));
static const QString   kLib     = QStringLiteral("/lib");
static const QString   kToken   = QStringLiteral("Mark-Liversedge");
static const QDate     kDay(2026, 6, 5);
// Every local day below is UTC's, so the suite reads the same in any machine's time zone.
static const QTimeZone kZone = QTimeZone::UTC;
static const QStringList kPatterns = { QStringLiteral("date-name-type"), QStringLiteral("date-type-name"),
                                       QStringLiteral("name-date-type"), QStringLiteral("date-only") };

static QDateTime utc(int h, int m, const QDate &d = kDay) { return QDateTime(d, QTime(h, m), QTimeZone::UTC); }

// A session folder name as the app composes it.
static QString composed(const QString &pattern, const QString &token, const QString &typeLabel, int nn,
                        const QDate &day = kDay)
{
    return SwingPaths::sessionFolderName(SwingPaths::sessionFolderBase(pattern, day, token, typeLabel), nn);
}

// ── D — D3: the id of a session older than step 2 ─────────────────────────────────────────────
static void testLegacyId()
{
    std::printf("\nD — D3: a legacy session's id\n");

    // Golden: uuid.uuid5(UUID('98a52a15-71c0-40f2-b377-f9a758154d28'), '<athlete>/<folder>') in Python.
    const QString f1 = QStringLiteral("2026-06-05_Mark-Liversedge_Swing_01");
    const QString f2 = QStringLiteral("2026-06-05_Mark-Liversedge_Swing_02");
    const QString f3 = QStringLiteral("Mark-Liversedge_2026-06-05_Swing_01");
    checkEq(QStringLiteral("D1 the id is Python's uuid5 of \"<athlete>/<folder>\""),
            SessionIdentity::legacySessionId(kAthlete, f1).toString(), QStringLiteral("3c852070-1e72-591c-97e1-85b5df9bc128"));
    checkEq(QStringLiteral("D2 …for a second folder"),
            SessionIdentity::legacySessionId(kAthlete, f2).toString(), QStringLiteral("3a107436-6ce0-563e-935a-a061f679d718"));
    checkEq(QStringLiteral("D3 …and under name-date-type"),
            SessionIdentity::legacySessionId(kAthlete, f3).toString(), QStringLiteral("e7f39841-1235-56e0-b427-62549ca02f5e"));
    check(QStringLiteral("D4 deterministic: asked twice, one id"),
          SessionIdentity::legacySessionId(kAthlete, f1) == SessionIdentity::legacySessionId(kAthlete, f1));
    check(QStringLiteral("D5 a name-based uuid (version 5)"),
          SessionIdentity::legacySessionId(kAthlete, f1).uuid().version() == QUuid::Sha1);
    const AthleteId other = AthleteId::fromString(QStringLiteral("9b0e6c1a-2f4d-4e8b-8a7c-1d2e3f4a5b6c"));
    check(QStringLiteral("D6 another athlete's folder of the same name is another session"),
          SessionIdentity::legacySessionId(other, f1) != SessionIdentity::legacySessionId(kAthlete, f1));
    check(QStringLiteral("D7 no athlete, no id"), SessionIdentity::legacySessionId(AthleteId(), f1).isNull());
    check(QStringLiteral("D8 no folder name, no id"), SessionIdentity::legacySessionId(kAthlete, QString()).isNull());
    // A folder made by hand on a Mac can come back from the file system decomposed (NFD).
    const QString nfc = QStringLiteral("2026-06-05_José_Swing_01");
    const QString nfd = QStringLiteral("2026-06-05_José_Swing_01");
    check(QStringLiteral("D9 a name spelled composed or decomposed is one session (NFC)"),
          SessionIdentity::legacySessionId(kAthlete, nfc) == SessionIdentity::legacySessionId(kAthlete, nfd));
    // Golden over the NFC string (reviewer, Python uuid.uuid5): pins NFC, not merely "both forms agree".
    checkEq(QStringLiteral("D9a the NFC spelling's id is uuid5 over NFC"),
            SessionIdentity::legacySessionId(kAthlete, nfc).toString(), QStringLiteral("0ead6d91-e4d2-5b0f-9458-02e26a8b6a7a"));
    checkEq(QStringLiteral("D9b the NFD spelling's id is the same golden"),
            SessionIdentity::legacySessionId(kAthlete, nfd).toString(), QStringLiteral("0ead6d91-e4d2-5b0f-9458-02e26a8b6a7a"));
    // The athlete's uuid is hashed lower case without braces, however it was spelled where it was read.
    const AthleteId shouty = AthleteId::fromString(QStringLiteral("{3F2C8F2E-1D2B-4C6A-9B1E-7A5D4C3B2A10}"));
    checkEq(QStringLiteral("D9c an upper-case braced athlete uuid gives D1's golden"),
            SessionIdentity::legacySessionId(shouty, f1).toString(), QStringLiteral("3c852070-1e72-591c-97e1-85b5df9bc128"));

    // The identity read from a folder's path carries that id, whatever the path's spelling.
    const SessionIdentity a = SessionIdentity::fromLegacyFolder(kLib + "/" + kToken + "/" + f1, kAthlete);
    const SessionIdentity b = SessionIdentity::fromLegacyFolder(kLib + "/" + kToken + "/" + f1 + "/", kAthlete);
    const SessionIdentity c = SessionIdentity::fromLegacyFolder("/elsewhere/archive/" + kToken + "/" + f1, kAthlete);
    check(QStringLiteral("D10 fromLegacyFolder gives D3's id"), a.id() == SessionIdentity::legacySessionId(kAthlete, f1));
    check(QStringLiteral("D11 a trailing '/' is the same session"), b.id() == a.id());
    check(QStringLiteral("D12 the archive mirror's copy is the same session (the id is the name's, not the path's)"),
          c.id() == a.id());
    check(QStringLiteral("D13 it is marked as read from its folder"), a.isLegacy() && !a.isNull());
    checkEq(QStringLiteral("D14 its folder is the path, cleaned"), b.folder(), kLib + "/" + kToken + "/" + f1);
    check(QStringLiteral("D15 no athlete id, no identity"),
          SessionIdentity::fromLegacyFolder(kLib + "/" + kToken + "/" + f1, AthleteId()).isNull());
}

// ── L — a legacy folder's name, under every pattern ───────────────────────────────────────────
static void testLegacyNames()
{
    std::printf("\nL — reading a legacy folder's name\n");

    // The composer and the reader agree, for every pattern, type and athlete token, including an
    // athlete token that holds '_' (sanitise keeps it) and one that is itself a type word.
    struct Type { QString label; QString type; };
    const QList<Type> types = { { QStringLiteral("Swing"), QStringLiteral("swing") },
                                { QStringLiteral("Wrist"), QStringLiteral("wrist") },
                                { QStringLiteral("GRF"), QStringLiteral("grf") },
                                { QStringLiteral("Coach"), QStringLiteral("coach") },
                                { QString(), QString() } };   // the launch monitor's untyped session
    const QStringList tokens = { kToken, QStringLiteral("Jo_Ann"), QStringLiteral("Coach"), QStringLiteral("Swing") };
    int agreed = 0, cases = 0;
    for (const QString &p : kPatterns)
        for (const Type &t : types)
            for (const QString &tok : tokens) {
                ++cases;
                const QString name = composed(p, tok, t.label, 7);
                const LegacyFolderName got = SessionIdentity::parseLegacyFolderName(name, tok);
                const QString wantType = p == QLatin1String("date-only") ? QString() : t.type;
                const bool ok = got.day == kDay && got.counter == 7
                                && SessionIdentity::typeFromToken(got.typeToken) == wantType && got.isSessionName();
                if (ok) ++agreed;
                else
                    std::printf("    \"%s\": day %s, nn %d, type \"%s\" (want \"%s\")\n", qPrintable(name),
                                qPrintable(got.day.toString(Qt::ISODate)), got.counter,
                                qPrintable(got.typeToken), qPrintable(wantType));
            }
    checkEqInt(QStringLiteral("L1 every composed name reads back: date, type, _NN (4 patterns × 5 types × 4 athletes)"),
               agreed, cases);

    // Without the athlete's token, the ordinary names still read.
    const LegacyFolderName dnt = SessionIdentity::parseLegacyFolderName(QStringLiteral("2026-06-05_Mark-Liversedge_Wrist_12"));
    check(QStringLiteral("L2 date-name-type without the athlete's token"),
          dnt.day == kDay && dnt.typeToken == QLatin1String("Wrist") && dnt.counter == 12);
    const LegacyFolderName dtn = SessionIdentity::parseLegacyFolderName(QStringLiteral("2026-06-05_GRF_Mark-Liversedge_02"));
    check(QStringLiteral("L3 date-type-name without the athlete's token"),
          dtn.day == kDay && dtn.typeToken == QLatin1String("GRF") && dtn.counter == 2);
    const LegacyFolderName ndt = SessionIdentity::parseLegacyFolderName(QStringLiteral("Mark-Liversedge_2026-06-05_Swing_03"));
    check(QStringLiteral("L4 name-date-type without the athlete's token: the date is found mid-name"),
          ndt.day == kDay && ndt.typeToken == QLatin1String("Swing") && ndt.counter == 3);
    const LegacyFolderName lm = SessionIdentity::parseLegacyFolderName(QStringLiteral("2026-06-05_Mark-Liversedge_session_04"));
    check(QStringLiteral("L5 the launch monitor's untyped session: token \"session\", no type"),
          lm.typeToken == QLatin1String("session") && SessionIdentity::typeFromToken(lm.typeToken).isEmpty());
    // The app's first pattern (165f10f1, 5–9 Jun 2026): "<date>_session-NN".
    const LegacyFolderName first = SessionIdentity::parseLegacyFolderName(QStringLiteral("2026-06-05_session-03"), kToken);
    check(QStringLiteral("L5a the first pattern <date>_session-NN is a session: its day, _NN 3, no type"),
          first.day == kDay && first.counter == 3 && first.isSessionName()
              && SessionIdentity::typeFromToken(first.typeToken).isEmpty());
    const SessionIdentity firstId = SessionIdentity::fromLegacyFolder(
        kLib + "/" + kToken + QStringLiteral("/2026-06-05_session-03"), kAthlete);
    check(QStringLiteral("L5b …and its identity has the name's day"), firstId.localDay() == kDay && !firstId.isNull());
    // The athlete folder's name in fromLegacyFolder is the hint that separates name from type.
    const SessionIdentity coach = SessionIdentity::fromLegacyFolder(
        kLib + QStringLiteral("/Coach/2026-06-05_Swing_Coach_01"), kAthlete);
    checkEq(QStringLiteral("L6 an athlete called Coach, date-type-name: the type is Swing"), coach.type(), QStringLiteral("swing"));

    // Not session names: nothing invented.
    const QStringList notSessions = { QStringLiteral("trunk-hw"), QStringLiteral("swing_0007"),
                                      QStringLiteral("2026-06-05"), QStringLiteral("2026-13-45_Mark_Swing_01"),
                                      QStringLiteral("tendencies.json"), QString() };
    bool none = true;
    for (const QString &n : notSessions)
        none = none && !SessionIdentity::parseLegacyFolderName(n).isSessionName();
    check(QStringLiteral("L7 a folder that is not a session name reads as none (no date and _NN)"), none);
    check(QStringLiteral("L8 an invalid date is no date"),
          !SessionIdentity::parseLegacyFolderName(QStringLiteral("2026-13-45_Mark_Swing_01")).day.isValid());
}

// ── N — n-of-day by start time, under every naming pattern ────────────────────────────────────
static void testDayOrder()
{
    std::printf("\nN — n-of-day by start time\n");

    for (const QString &p : kPatterns) {
        // Three sessions on 5 June whose _NN disagree with the order they were started in (an
        // extended or renamed session can do that), and one on 6 June. Order must follow the clock.
        QList<SessionIdentity> s = {
            SessionIdentity::fromLegacyFolder(kLib + "/" + kToken + "/" + composed(p, kToken, "Swing", 1), kAthlete, utc(15, 0), kZone),
            SessionIdentity::fromLegacyFolder(kLib + "/" + kToken + "/" + composed(p, kToken, "Swing", 2), kAthlete, utc(9, 30), kZone),
            SessionIdentity::fromLegacyFolder(kLib + "/" + kToken + "/" + composed(p, kToken, "Wrist", 1), kAthlete, utc(12, 0), kZone),
            SessionIdentity::fromLegacyFolder(kLib + "/" + kToken + "/" + composed(p, kToken, "Swing", 1, kDay.addDays(1)),
                                              kAthlete, utc(8, 0, kDay.addDays(1)), kZone),
        };
        SessionIdentity::assignDayOrder(s);
        check(QStringLiteral("N %1: the local day is the start time's").arg(p),
              s[0].localDay() == kDay && s[1].localDay() == kDay && s[2].localDay() == kDay
                  && s[3].localDay() == kDay.addDays(1));
        check(QStringLiteral("N %1: 9:30 is 1st, 12:00 2nd, 15:00 3rd (got %2, %3, %4)")
                  .arg(p).arg(s[1].nOfDay()).arg(s[2].nOfDay()).arg(s[0].nOfDay()),
              s[1].nOfDay() == 1 && s[2].nOfDay() == 2 && s[0].nOfDay() == 3);
        check(QStringLiteral("N %1: the next day starts again at 1st").arg(p), s[3].nOfDay() == 1);
    }

    // A session with a session file and legacy ones, one day: one order, by the clock.
    {
        QList<SessionIdentity> s = {
            SessionIdentity::fromSessionFile(SessionId::create(), kLib + "/x/new", QStringLiteral("swing"), utc(18, 0), kZone),
            SessionIdentity::fromLegacyFolder(kLib + "/" + kToken + "/" + composed("name-date-type", kToken, "Swing", 1),
                                              kAthlete, utc(10, 0), kZone),
        };
        SessionIdentity::assignDayOrder(s);
        check(QStringLiteral("N5 a session file's session and a legacy one order together by start time"),
              s[1].nOfDay() == 1 && s[0].nOfDay() == 2);
    }

    // The local day is the athlete's clock's, not UTC's: 23:30 UTC on 5 June is 6 June two hours east.
    {
        const QTimeZone east = QTimeZone::fromSecondsAheadOfUtc(2 * 3600);
        const QTimeZone west = QTimeZone::fromSecondsAheadOfUtc(-5 * 3600);
        const SessionIdentity e = SessionIdentity::fromSessionFile(SessionId::create(), kLib + "/e", "swing", utc(23, 30), east);
        const SessionIdentity w = SessionIdentity::fromSessionFile(SessionId::create(), kLib + "/w", "swing", utc(2, 0), west);
        check(QStringLiteral("N6 a late start is the next local day east of UTC"), e.localDay() == kDay.addDays(1));
        check(QStringLiteral("N7 an early start is the previous local day west of UTC"), w.localDay() == kDay.addDays(-1));
        check(QStringLiteral("N8 the start time is kept in UTC"),
              e.startedAtUtc().offsetFromUtc() == 0 && e.startedAtUtc() == utc(23, 30));
    }

    // A legacy folder with no start time: the day is its name's, the order its folder name's (today's).
    {
        QList<SessionIdentity> s = {
            SessionIdentity::fromLegacyFolder(kLib + "/" + kToken + "/" + composed("name-date-type", kToken, "Swing", 3), kAthlete),
            SessionIdentity::fromLegacyFolder(kLib + "/" + kToken + "/" + composed("name-date-type", kToken, "Swing", 1), kAthlete),
            SessionIdentity::fromLegacyFolder(kLib + "/" + kToken + "/" + composed("name-date-type", kToken, "Swing", 2), kAthlete,
                                              utc(7, 0), kZone),
        };
        SessionIdentity::assignDayOrder(s);
        check(QStringLiteral("N9 no start time: the day is the name's, even under name-date-type"),
              s[0].localDay() == kDay && s[1].localDay() == kDay);
        check(QStringLiteral("N10 a day where any start time is missing orders by folder name (got %1, %2, %3)")
                  .arg(s[1].nOfDay()).arg(s[2].nOfDay()).arg(s[0].nOfDay()),
              s[1].nOfDay() == 1 && s[2].nOfDay() == 2 && s[0].nOfDay() == 3);
    }

    // Two types on one untimed day (step 2.7a): the order is the folders' names, as the app has always
    // ordered a day — never the `_NN` alone, which counts per type ("…_Wrist_01" is not before "…_Swing_02").
    for (const QString &p : kPatterns) {
        QList<SessionIdentity> s = {
            SessionIdentity::fromLegacyFolder(kLib + "/" + kToken + "/" + composed(p, kToken, "Wrist", 1), kAthlete),
            SessionIdentity::fromLegacyFolder(kLib + "/" + kToken + "/" + composed(p, kToken, "Swing", 3), kAthlete),
            SessionIdentity::fromLegacyFolder(kLib + "/" + kToken + "/" + composed(p, kToken, "Swing", 2), kAthlete),
        };
        SessionIdentity::assignDayOrder(s);
        QList<int> byName = { 0, 1, 2 };
        std::sort(byName.begin(), byName.end(), [&](int a, int b) { return s[a].folderName() < s[b].folderName(); });
        check(QStringLiteral("N12 %1: an untimed day of two types orders by folder name (got %2, %3, %4)")
                  .arg(p).arg(s[byName[0]].nOfDay()).arg(s[byName[1]].nOfDay()).arg(s[byName[2]].nOfDay()),
              s[byName[0]].nOfDay() == 1 && s[byName[1]].nOfDay() == 2 && s[byName[2]].nOfDay() == 3);
    }

    // The upgrade day: a legacy session (no start time) and a session-file session made after it. The
    // day has an untimed session, so it orders by folder name — the new session's counter is past the
    // old one's, so it comes second (before step 2.7a it ranked first: its counter read as 0).
    {
        QList<SessionIdentity> s = {
            SessionIdentity::fromSessionFile(SessionId::create(),
                                             kLib + "/" + kToken + "/" + composed("date-name-type", kToken, "Swing", 2),
                                             QStringLiteral("swing"), utc(14, 0), kZone),
            SessionIdentity::fromLegacyFolder(kLib + "/" + kToken + "/" + composed("date-name-type", kToken, "Swing", 1),
                                              kAthlete),
        };
        SessionIdentity::assignDayOrder(s);
        check(QStringLiteral("N13 the upgrade day: the legacy morning 1st, the new session 2nd (got %1, %2)")
                  .arg(s[1].nOfDay()).arg(s[0].nOfDay()),
              s[1].nOfDay() == 1 && s[0].nOfDay() == 2);
    }

    // No day at all: no place.
    {
        QList<SessionIdentity> s = { SessionIdentity::fromLegacyFolder(kLib + "/" + kToken + "/trunk-hw", kAthlete) };
        SessionIdentity::assignDayOrder(s);
        check(QStringLiteral("N11 a folder with no day and no start keeps n-of-day 0"), s[0].nOfDay() == 0 && !s[0].localDay().isValid());
    }
}

// ── T — the label ──────────────────────────────────────────────────────────────────────────────
static void testLabel()
{
    std::printf("\nT — the label\n");

    QList<SessionIdentity> s = {
        SessionIdentity::fromSessionFile(SessionId::create(), kLib + "/a", QStringLiteral("swing"), utc(9, 0), kZone),
        SessionIdentity::fromSessionFile(SessionId::create(), kLib + "/b", QStringLiteral("grf"), utc(11, 0), kZone),
        SessionIdentity::fromLegacyFolder(kLib + "/" + kToken + "/" + composed("date-name-type", kToken, QString(), 1),
                                          kAthlete, utc(13, 0), kZone),
    };
    const QString before = s[0].label();
    SessionIdentity::assignDayOrder(s);
    checkEq(QStringLiteral("T1 type · day · place"), s[0].label(), QStringLiteral("Swing · 5 Jun · 1st"));
    checkEq(QStringLiteral("T2 GRF keeps its capitals"), s[1].label(), QStringLiteral("GRF · 5 Jun · 2nd"));
    checkEq(QStringLiteral("T3 an untyped session says Session"), s[2].label(), QStringLiteral("Session · 5 Jun · 3rd"));
    checkEq(QStringLiteral("T4 before its place is known, the place is left out"), before, QStringLiteral("Swing · 5 Jun"));

    const QList<QPair<int, QString>> ords = { { 1, "1st" }, { 2, "2nd" }, { 3, "3rd" }, { 4, "4th" }, { 10, "10th" },
                                              { 11, "11th" }, { 12, "12th" }, { 13, "13th" }, { 21, "21st" },
                                              { 22, "22nd" }, { 23, "23rd" }, { 101, "101st" }, { 111, "111th" },
                                              { 112, "112th" } };
    bool allOrd = true;
    for (const auto &[n, want] : ords) {
        if (SessionIdentity::ordinal(n) != want) {
            std::printf("    ordinal(%d) = \"%s\", want \"%s\"\n", n, qPrintable(SessionIdentity::ordinal(n)), qPrintable(want));
            allOrd = false;
        }
    }
    check(QStringLiteral("T5 English ordinals, 11th–13th included"), allOrd);
    check(QStringLiteral("T6 a null session id gives no identity"),
          SessionIdentity::fromSessionFile(SessionId(), kLib + "/a", "swing", utc(9, 0)).isNull());
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    std::printf("session_identity_test — SessionIdentity: legacy names, D3 ids, n-of-day\n");
    testLegacyId();
    testLegacyNames();
    testDayOrder();
    testLabel();
    std::printf("\n%d checks, %d failed\n", g_run, g_fail);
    return g_fail == 0 ? 0 : 1;
}
