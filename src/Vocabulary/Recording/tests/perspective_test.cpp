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

// perspective_test — the one perspective type (src/Vocabulary/Recording/perspective.h; rig_design.md §4.7).
//
// The oracle is the swing file's schema reference (docs/reference/swing_json_schema.md, `setup.perspective`):
// 0 None · 1 DownTheLine · 2 FaceOn · 3 Other · 4 Impact, the codes every stored swing and the settings' camera map
// hold. The expectations below are written from that table, never from running the code. Checked:
//   A  each of the five codes reads as its placement, and each placement writes its code back;
//   B  a code the table does not hold reads as None (this function's rule; keeping an unknown code as its number
//      is the Rig's and the rig block's, not this function's);
//   C  the enum is reachable through Qt's meta-object by name: five keys, each name with its code, so QML can
//      write Perspective.FaceOn;
//   D  recording::Perspective is the same type, so no C++ user changes.
// Runtime checks, so a wrong code turns ctest red rather than stopping the build.

#include "../perspective.h"

#include <QCoreApplication>
#include <QMetaEnum>
#include <QString>

#include <climits>
#include <cstdio>
#include <type_traits>

using namespace pinpoint;
namespace rec = pinpoint::recording;

static int g_fail = 0;
static int g_run  = 0;

static void check(const QString &label, bool ok)
{
    ++g_run;
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", qPrintable(label));
    if (!ok) ++g_fail;
}

// The schema reference's table, as written there.
struct Row { int code; const char *name; rec::Perspective placement; };
static const Row kTable[] = {
    { 0, "None",        rec::Perspective::None },
    { 1, "DownTheLine", rec::Perspective::DownTheLine },
    { 2, "FaceOn",      rec::Perspective::FaceOn },
    { 3, "Other",       rec::Perspective::Other },
    { 4, "Impact",      rec::Perspective::Impact },
};

static void testCodes()
{
    std::printf("\n[A] the five stored codes\n");
    for (const Row &r : kTable) {
        check(QStringLiteral("A code %1 reads as %2").arg(r.code).arg(QLatin1String(r.name)),
              rec::perspectiveFromCode(r.code) == r.placement);
        check(QStringLiteral("A %1 writes code %2").arg(QLatin1String(r.name)).arg(r.code),
              rec::perspectiveCode(r.placement) == r.code);
    }
}

static void testUnknownCodes()
{
    std::printf("\n[B] a code the table does not hold\n");
    for (int code : { -1, 5, 6, 9, 100, INT_MIN, INT_MAX })
        check(QStringLiteral("B code %1 reads as None").arg(code),
              rec::perspectiveFromCode(code) == rec::Perspective::None);
}

static void testMetaObject()
{
    std::printf("\n[C] the meta-object, by name\n");
    const QMetaEnum me = QMetaEnum::fromType<rec::Perspective>();
    check(QStringLiteral("C the enum has a meta-enum"), me.isValid());
    check(QStringLiteral("C it is named Perspective (got \"%1\")").arg(QLatin1String(me.name())),
          QByteArray(me.name()) == "Perspective");
    check(QStringLiteral("C it is a scoped enum"), me.isScoped());
    check(QStringLiteral("C it has five keys (got %1)").arg(me.keyCount()), me.keyCount() == 5);
    for (const Row &r : kTable) {
        bool ok = false;
        const int v = me.keyToValue(r.name, &ok);
        check(QStringLiteral("C key %1 is code %2 (got %3)").arg(QLatin1String(r.name)).arg(r.code).arg(v),
              ok && v == r.code);
        check(QStringLiteral("C code %1 is key %2").arg(r.code).arg(QLatin1String(r.name)),
              QByteArray(me.valueToKey(r.code)) == r.name);
    }
    const QMetaObject &mo = rec::camera_perspective::staticMetaObject;
    check(QStringLiteral("C the namespace's meta-object holds the enum"), mo.indexOfEnumerator("Perspective") >= 0);
    check(QStringLiteral("C and only the enum (enumerators %1, methods %2, properties %3)")
              .arg(mo.enumeratorCount()).arg(mo.methodCount()).arg(mo.propertyCount()),
          mo.enumeratorCount() == 1 && mo.methodCount() == 0 && mo.propertyCount() == 0);
}

static void testSameType()
{
    std::printf("\n[D] one type\n");
    constexpr bool same = std::is_same_v<rec::Perspective, rec::camera_perspective::Perspective>;
    check(QStringLiteral("D recording::Perspective is the header's enum"), same);
    constexpr bool intBacked = std::is_same_v<std::underlying_type_t<rec::Perspective>, int>;
    check(QStringLiteral("D its codes are ints"), intBacked);
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    std::printf("perspective_test — the one perspective type\n");
    testCodes();
    testUnknownCodes();
    testMetaObject();
    testSameType();
    std::printf("\n%d checks, %d failed\n", g_run, g_fail);
    return g_fail == 0 ? 0 : 1;
}
