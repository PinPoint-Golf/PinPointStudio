/*
 * Copyright (c) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

// ShotListModel::shotSummary — the focused-shot metadata map that backs the
// carousel's PpShotActionBar. Asserts: a valid id resolves every identity
// field; an unknown / -1 id returns { valid:false } (present, false, bindable);
// a row that would be filtered out of a proxy still resolves (summary is
// model-scoped); and an IMU-only shot (no video, empty swingDir) reports the
// right flags without crashing.

#include "shot/shot_list_model.h"

#include <QVariantMap>
#include <QUrl>
#include <cstdio>

static int g_fail = 0;

static void checkBool(const char *label, bool got, bool want)
{
    const bool ok = got == want;
    std::printf("  [%s] %-40s got %-5s want %-5s\n",
                ok ? "PASS" : "FAIL", label, got ? "true" : "false", want ? "true" : "false");
    if (!ok) ++g_fail;
}

static void checkInt(const char *label, long long got, long long want)
{
    const bool ok = got == want;
    std::printf("  [%s] %-40s got %4lld  want %4lld\n", ok ? "PASS" : "FAIL", label, got, want);
    if (!ok) ++g_fail;
}

static void checkStr(const char *label, const QString &got, const QString &want)
{
    const bool ok = got == want;
    std::printf("  [%s] %-40s got \"%s\"  want \"%s\"\n",
                ok ? "PASS" : "FAIL", label, got.toUtf8().constData(), want.toUtf8().constData());
    if (!ok) ++g_fail;
}

int main()
{
    std::printf("=== ShotListModel::shotSummary ===\n\n");

    ShotListModel model;

    // Two video shots + one IMU-only shot. addShot prepends (newest first) and
    // assigns ascending ids (1,2,3) and ascending ordinals (1,2,3). We capture
    // the returned ids so the test never assumes row order.
    const int id1 = model.addShot(QStringLiteral("/swings/swing_0001"), QStringLiteral("13:42"),
                                  QStringLiteral("7-iron"), /*hasVideo*/ true,
                                  QUrl(QStringLiteral("file:///swings/swing_0001/thumb.jpg")),
                                  /*tracePoints*/ {}, /*score*/ 82, /*metrics*/ {});
    const int id2 = model.addShot(QStringLiteral("/swings/swing_0002"), QStringLiteral("13:45"),
                                  QStringLiteral("Driver"), /*hasVideo*/ true,
                                  QUrl(QStringLiteral("file:///swings/swing_0002/thumb.jpg")),
                                  /*tracePoints*/ {}, /*score*/ 44, /*metrics*/ {});
    // IMU-only shot: no video, no on-disk folder.
    const int id3 = model.addShot(/*swingDir*/ QString(), QStringLiteral("13:48"),
                                  QStringLiteral("PW"), /*hasVideo*/ false,
                                  QUrl(), {}, /*score*/ 18, {});
    model.setRating(id1, 4);

    std::printf("-- valid id (full field round-trip) --\n");
    {
        const QVariantMap s = model.shotSummary(id1);
        checkBool("valid",                s.value(QStringLiteral("valid")).toBool(), true);
        checkInt ("ordinal",              s.value(QStringLiteral("ordinal")).toInt(), 1);
        checkStr ("club",                 s.value(QStringLiteral("club")).toString(), QStringLiteral("7-iron"));
        checkStr ("timestampLabel",       s.value(QStringLiteral("timestampLabel")).toString(), QStringLiteral("13:42"));
        checkInt ("score",                s.value(QStringLiteral("score")).toInt(), 82);
        checkInt ("rating (after setRating)", s.value(QStringLiteral("rating")).toInt(), 4);
        checkBool("hasVideo",             s.value(QStringLiteral("hasVideo")).toBool(), true);
        checkStr ("swingDir",             s.value(QStringLiteral("swingDir")).toString(), QStringLiteral("/swings/swing_0001"));
    }

    std::printf("\n-- unknown / -1 id --\n");
    {
        const QVariantMap none = model.shotSummary(-1);
        checkBool("(-1) valid present & false", none.value(QStringLiteral("valid")).toBool(), false);
        checkBool("(-1) valid key present",     none.contains(QStringLiteral("valid")), true);
        const QVariantMap gone = model.shotSummary(99999);
        checkBool("(99999) valid present & false", gone.value(QStringLiteral("valid")).toBool(), false);
    }

    std::printf("\n-- model-scoped: a low-score shot a proxy would hide still resolves --\n");
    {
        // id2 (score 44) would be filtered out by a quality>=50 proxy band, yet
        // the focused-shot summary must still resolve from the source model.
        const QVariantMap s = model.shotSummary(id2);
        checkBool("valid",   s.value(QStringLiteral("valid")).toBool(), true);
        checkStr ("club",    s.value(QStringLiteral("club")).toString(), QStringLiteral("Driver"));
        checkInt ("score",   s.value(QStringLiteral("score")).toInt(), 44);
    }

    std::printf("\n-- IMU-only shot (no video, empty swingDir) --\n");
    {
        const QVariantMap s = model.shotSummary(id3);
        checkBool("valid",            s.value(QStringLiteral("valid")).toBool(), true);
        checkBool("hasVideo == false", s.value(QStringLiteral("hasVideo")).toBool(), false);
        checkStr ("swingDir empty",   s.value(QStringLiteral("swingDir")).toString(), QString());
        checkInt ("score",            s.value(QStringLiteral("score")).toInt(), 18);
    }

    std::printf("\n%s (%d failure%s)\n", g_fail == 0 ? "ALL PASS" : "FAILURES",
                g_fail, g_fail == 1 ? "" : "s");
    return g_fail == 0 ? 0 : 1;
}
