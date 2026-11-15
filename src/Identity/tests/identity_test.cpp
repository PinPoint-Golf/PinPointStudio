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

// identity_test — the identity types (src/Identity/identity.h; refactor step 2.1, brief §4.1):
// AthleteId, SessionId, ShotRef.
//
// The contract under test, from the header and information_model.md §5.2: each is a value type
// that names one thing; none can be made from a folder name or a path (P7); the text each one
// writes reads back to the same value, and nothing else reads as one; they are hashable, ordered and
// distinct types; a ShotRef is a session and a swing number >= 1. Expectations come from that
// contract (and from QUuid's own spelling), never from running the code and writing down what came out.

#include "identity.h"

#include <QCoreApplication>
#include <QHash>
#include <QSet>
#include <QString>
#include <QUuid>
#include <QVariant>

#include <cstdio>
#include <type_traits>

using namespace pinpoint;

// ── P — no constructor from a path, a folder name or another identity (compile time) ───────────
static_assert(!std::is_constructible_v<AthleteId, QString>, "AthleteId has no constructor from text");
static_assert(!std::is_constructible_v<SessionId, QString>, "SessionId has no constructor from text");
static_assert(!std::is_constructible_v<ShotRef, QString>, "ShotRef has no constructor from text");
static_assert(!std::is_constructible_v<AthleteId, const char *>, "nor from a C string");
static_assert(!std::is_constructible_v<SessionId, const char *>, "nor from a C string");
static_assert(!std::is_constructible_v<ShotRef, const char *>, "nor from a C string");
static_assert(!std::is_convertible_v<QUuid, AthleteId>, "a QUuid becomes an AthleteId only on purpose");
static_assert(!std::is_convertible_v<QUuid, SessionId>, "a QUuid becomes a SessionId only on purpose");
static_assert(!std::is_constructible_v<SessionId, AthleteId>, "an athlete's id is not a session's");
static_assert(!std::is_constructible_v<AthleteId, SessionId>, "a session's id is not an athlete's");
static_assert(!std::is_constructible_v<ShotRef, SessionId>, "a ShotRef needs its swing number");
static_assert(!std::is_constructible_v<ShotRef, AthleteId, int>, "a ShotRef's session is a SessionId");

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

// A fixed uuid, written as the athlete records and swing headers write one (lower case, no braces).
static const QString kU1 = QStringLiteral("3f2c8f2e-1d2b-4c6a-9b1e-7a5d4c3b2a10");
static const QString kU2 = QStringLiteral("9b0e6c1a-2f4d-4e8b-8a7c-1d2e3f4a5b6c");

// The paths and names a caller might be tempted to pass: none of them is an identity.
static const QStringList kNotIds = {
    QStringLiteral("2026-06-05_Mark-Liversedge_Swing_01"),
    QStringLiteral("/mnt/swingdata/Mark-Liversedge/2026-06-05_Mark-Liversedge_Swing_01"),
    QStringLiteral("/mnt/swingdata/Mark-Liversedge/2026-06-05_Mark-Liversedge_Swing_01/swing_0007"),
    QStringLiteral("swing_0007"),
    QStringLiteral("Mark-Liversedge"),
    QStringLiteral("Mark Liversedge"),
    QStringLiteral("7"),
    QString(),
};

static void testAthleteAndSession()
{
    std::printf("\nI — AthleteId and SessionId\n");

    check(QStringLiteral("I1 a default AthleteId is null"), AthleteId().isNull());
    check(QStringLiteral("I2 a default SessionId is null"), SessionId().isNull());
    check(QStringLiteral("I3 a null id writes no text"), AthleteId().toString().isEmpty() && SessionId().toString().isEmpty());

    const AthleteId a = AthleteId::fromString(kU1);
    check(QStringLiteral("I4 an athlete uuid reads as an AthleteId"), !a.isNull());
    checkEq(QStringLiteral("I5 AthleteId round trip: text -> id -> text"), a.toString(), kU1);
    check(QStringLiteral("I6 AthleteId round trip: id -> text -> id"), AthleteId::fromString(a.toString()) == a);
    check(QStringLiteral("I7 the uuid it holds is the one it was read from"), a.uuid() == QUuid::fromString(kU1));
    check(QStringLiteral("I8 braces and upper case read as the same athlete"),
          AthleteId::fromString(QStringLiteral("{") + kU1.toUpper() + QStringLiteral("}")) == a);
    checkEq(QStringLiteral("I9 …and write back in the one spelling"),
            AthleteId::fromString(QStringLiteral("{") + kU1.toUpper() + QStringLiteral("}")).toString(), kU1);

    const SessionId s = SessionId::fromString(kU2);
    checkEq(QStringLiteral("I10 SessionId round trip: text -> id -> text"), s.toString(), kU2);
    check(QStringLiteral("I11 SessionId round trip: id -> text -> id"), SessionId::fromString(s.toString()) == s);
    check(QStringLiteral("I12 SessionId(QUuid) holds that uuid"), SessionId(QUuid::fromString(kU2)) == s);

    bool noneAthlete = true, noneSession = true;
    for (const QString &t : kNotIds) {
        noneAthlete = noneAthlete && AthleteId::fromString(t).isNull();
        noneSession = noneSession && SessionId::fromString(t).isNull();
    }
    check(QStringLiteral("I13 no folder name, path, athlete name or number reads as an AthleteId"), noneAthlete);
    check(QStringLiteral("I14 no folder name, path, athlete name or number reads as a SessionId"), noneSession);

    // A uuid with anything before or after it is not a uuid (QUuid::fromString alone reads the prefix).
    const QStringList prefixed = {
        kU2 + QStringLiteral("/7"),                  // a ShotRef's own text
        kU2 + QStringLiteral("_Mark"),
        kU2 + QStringLiteral("/swing_0007"),
        QStringLiteral("{") + kU2,                   // an opening brace only
        kU2 + QStringLiteral("}"),                   // a closing brace only
        QStringLiteral(" ") + kU2,
        QStringLiteral("x") + kU2.mid(1) + QStringLiteral("x"),
    };
    bool prefAthlete = true, prefSession = true;
    for (const QString &t : prefixed) {
        const bool a0 = AthleteId::fromString(t).isNull(), s0 = SessionId::fromString(t).isNull();
        if (!a0 || !s0)
            std::printf("    read as an id: \"%s\"\n", qPrintable(t));
        prefAthlete = prefAthlete && a0;
        prefSession = prefSession && s0;
    }
    check(QStringLiteral("I14a a uuid with text before or after it is no AthleteId"), prefAthlete);
    check(QStringLiteral("I14b …and no SessionId (a ShotRef's text never reads as its session)"), prefSession);
    check(QStringLiteral("I14c exactly 36 characters, or 38 with both braces, read"),
          !SessionId::fromString(kU2).isNull()
              && SessionId::fromString(QStringLiteral("{") + kU2 + QStringLiteral("}")) == SessionId::fromString(kU2));

    const SessionId n1 = SessionId::create();
    const SessionId n2 = SessionId::create();
    check(QStringLiteral("I15 a new session's id is not null"), !n1.isNull());
    check(QStringLiteral("I16 two new sessions get two ids"), n1 != n2);
    check(QStringLiteral("I17 a new session's id is random (v4, DEC-41)"), n1.uuid().version() == QUuid::Random);

    const SessionId lo = SessionId::fromString(QStringLiteral("00000000-0000-4000-8000-000000000001"));
    const SessionId hi = SessionId::fromString(QStringLiteral("00000000-0000-4000-8000-000000000002"));
    check(QStringLiteral("I18 ordered: a strict order, consistent with ==, both ways"),
          lo < hi && !(hi < lo) && !(lo < lo) && lo != hi);

    QSet<AthleteId> athletes{ a, AthleteId::fromString(kU1), AthleteId::fromString(kU2) };
    check(QStringLiteral("I19 hashable: the same athlete read twice is one key"), athletes.size() == 2);
    QHash<SessionId, int> sessions;
    sessions.insert(s, 1);
    sessions.insert(SessionId::fromString(kU2), 2);
    check(QStringLiteral("I20 hashable: a SessionId keys a QHash"), sessions.size() == 1 && sessions.value(s) == 2);
    check(QStringLiteral("I21 equal ids hash equal"), qHash(a, 7) == qHash(AthleteId::fromString(kU1), 7));
}

static void testShotRef()
{
    std::printf("\nR — ShotRef\n");

    const SessionId s = SessionId::fromString(kU2);
    const ShotRef   r(s, 7);
    check(QStringLiteral("R1 a session and a swing number make a ShotRef"), !r.isNull());
    check(QStringLiteral("R2 its parts are the ones it was made of"), r.session() == s && r.swingNumber() == 7);
    checkEq(QStringLiteral("R3 its text is \"<session uuid>/<swing number>\""), r.toString(), kU2 + QStringLiteral("/7"));
    check(QStringLiteral("R4 round trip: ref -> text -> ref"), ShotRef::fromString(r.toString()) == r);
    checkEq(QStringLiteral("R5 round trip: text -> ref -> text"),
            ShotRef::fromString(kU2 + QStringLiteral("/1234")).toString(), kU2 + QStringLiteral("/1234"));

    check(QStringLiteral("R6 a default ShotRef is null and writes no text"), ShotRef().isNull() && ShotRef().toString().isEmpty());
    check(QStringLiteral("R7 swing number 0 is no shot (numbers start at 1)"), ShotRef(s, 0).isNull());
    check(QStringLiteral("R8 a negative swing number is no shot"), ShotRef(s, -3).isNull());
    check(QStringLiteral("R9 a null session is no shot"), ShotRef(SessionId(), 7).isNull());
    check(QStringLiteral("R10 every invalid ref equals the null ref"),
          ShotRef(s, 0) == ShotRef() && ShotRef(SessionId(), 7) == ShotRef());

    bool none = true;
    for (const QString &t : kNotIds)
        none = none && ShotRef::fromString(t).isNull();
    check(QStringLiteral("R11 no folder name or path reads as a ShotRef"), none);
    const QStringList notCanonical = {
        kU2 + QStringLiteral("/swing_0007"),                     // a folder name after the slash
        kU2 + QStringLiteral("/0007"),                           // leading zeros
        kU2 + QStringLiteral("/+7"),                             // a sign
        kU2 + QStringLiteral("/0"),
        kU2 + QStringLiteral("/7/"),
        QStringLiteral("{") + kU2 + QStringLiteral("}/7"),     // braces
        kU2.toUpper() + QStringLiteral("/7"),                    // upper case
        QStringLiteral("/") + kU2 + QStringLiteral("/7"),       // a path that ends like one
        kU2,
    };
    bool noneOther = true;
    for (const QString &t : notCanonical) {
        const bool isNull = ShotRef::fromString(t).isNull();
        if (!isNull)
            std::printf("    read as a ShotRef: \"%s\"\n", qPrintable(t));
        noneOther = noneOther && isNull;
    }
    check(QStringLiteral("R12 only the exact text toString() writes reads back (no other spelling)"), noneOther);

    const SessionId lo = SessionId::fromString(QStringLiteral("00000000-0000-4000-8000-000000000001"));
    const SessionId hi = SessionId::fromString(QStringLiteral("00000000-0000-4000-8000-000000000002"));
    check(QStringLiteral("R13 ordered by session, then swing number"),
          ShotRef(lo, 2) < ShotRef(lo, 10) && ShotRef(lo, 99) < ShotRef(hi, 1) && !(ShotRef(lo, 2) < ShotRef(lo, 2)));
    check(QStringLiteral("R14 equal only when both parts are"),
          ShotRef(lo, 2) == ShotRef(lo, 2) && ShotRef(lo, 2) != ShotRef(lo, 3) && ShotRef(lo, 2) != ShotRef(hi, 2));

    QSet<ShotRef> refs{ ShotRef(lo, 1), ShotRef(lo, 1), ShotRef(lo, 2), ShotRef(hi, 1) };
    check(QStringLiteral("R15 hashable: one key per shot"), refs.size() == 3);
}

static void testGadgets()
{
    std::printf("\nG — the gadgets (moc, QVariant: what a QML role carries)\n");

    const ShotRef r(SessionId::fromString(kU2), 7);
    const QVariant v = QVariant::fromValue(r);
    check(QStringLiteral("G1 a ShotRef round-trips through a QVariant"), v.value<ShotRef>() == r);
    check(QStringLiteral("G2 an AthleteId round-trips through a QVariant"),
          QVariant::fromValue(AthleteId::fromString(kU1)).value<AthleteId>() == AthleteId::fromString(kU1));
    check(QStringLiteral("G3 a SessionId round-trips through a QVariant"),
          QVariant::fromValue(SessionId::fromString(kU2)).value<SessionId>() == SessionId::fromString(kU2));
    check(QStringLiteral("G4 ShotRef is a gadget with a swingNumber property"),
          ShotRef::staticMetaObject.indexOfProperty("swingNumber") >= 0);
    check(QStringLiteral("G5 each type offers toString() to QML"),
          AthleteId::staticMetaObject.indexOfMethod("toString()") >= 0
              && SessionId::staticMetaObject.indexOfMethod("toString()") >= 0
              && ShotRef::staticMetaObject.indexOfMethod("toString()") >= 0);
    check(QStringLiteral("G6 three distinct meta types"),
          QMetaType::fromType<AthleteId>() != QMetaType::fromType<SessionId>()
              && QMetaType::fromType<SessionId>() != QMetaType::fromType<ShotRef>());
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    std::printf("identity_test — AthleteId, SessionId, ShotRef\n");
    testAthleteAndSession();
    testShotRef();
    testGadgets();
    std::printf("\n%d checks, %d failed\n", g_run, g_fail);
    return g_fail == 0 ? 0 : 1;
}
