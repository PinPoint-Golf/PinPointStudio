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

// Characterization of TimelineLabels — the pure, font-free maths behind the Transit
// timeline. stationLayout() is exercised indirectly (it composes distribute()); the
// font-measuring path needs a QGuiApplication and is left to the visual/manual check.

#include "timeline_labels.h"

#include <QVariantList>
#include <QVariantMap>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <utility>

static int g_fail = 0;

static void checkTrue(const char *label, bool ok)
{
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++g_fail;
}
static void checkNear(const char *label, double got, double want, double tol = 1e-6)
{
    const bool ok = std::fabs(got - want) <= tol;
    std::printf("  [%s] %-32s got %10.4f  want %10.4f\n", ok ? "PASS" : "FAIL", label, got, want);
    if (!ok) ++g_fail;
}
static void checkEqI(const char *label, long long got, long long want)
{
    const bool ok = got == want;
    std::printf("  [%s] %-32s got %8lld  want %8lld\n", ok ? "PASS" : "FAIL", label, got, want);
    if (!ok) ++g_fail;
}
static void checkStr(const char *label, const QString &got, const char *want)
{
    const bool ok = got == QString::fromLatin1(want);
    std::printf("  [%s] %-32s got %-16s want %-16s\n", ok ? "PASS" : "FAIL", label,
                got.toLocal8Bit().constData(), want);
    if (!ok) ++g_fail;
}

static QVariantList vl(std::initializer_list<double> xs)
{
    QVariantList l;
    for (double x : xs) l.append(x);
    return l;
}
static QVariantList phases(std::initializer_list<std::pair<int, qint64>> ps)
{
    QVariantList l;
    for (const auto &p : ps) {
        QVariantMap m;
        m.insert(QStringLiteral("phase"), p.first);
        m.insert(QStringLiteral("t_us"), QVariant::fromValue<qint64>(p.second));
        l.append(m);
    }
    return l;
}

int main()
{
    TimelineLabels t;
    std::printf("=== TimelineLabels ===\n\n");

    // ── distribute: no overlap → passthrough ──────────────────────────────────
    std::printf("-- distribute: no overlap (passthrough) --\n");
    {
        const QVariantList r = t.distribute(vl({10, 50, 90}), vl({8, 8, 8}), 4, 0, 100);
        checkNear("c0", r.at(0).toDouble(), 10);
        checkNear("c1", r.at(1).toDouble(), 50);
        checkNear("c2", r.at(2).toDouble(), 90);
    }

    // ── distribute: overlapping centres pushed to >= gap clearance ─────────────
    std::printf("-- distribute: overlap pushed apart --\n");
    {
        // sizes 10, gap 2 → minimum centre spacing = 5 + 2 + 5 = 12.
        const QVariantList r = t.distribute(vl({50, 52, 54}), vl({10, 10, 10}), 2, 0, 200);
        checkNear("c0", r.at(0).toDouble(), 50);
        checkNear("c1", r.at(1).toDouble(), 62);
        checkNear("c2", r.at(2).toDouble(), 74);
        const double g1 = r.at(1).toDouble() - r.at(0).toDouble();
        const double g2 = r.at(2).toDouble() - r.at(1).toDouble();
        checkTrue("gap0>=12", g1 >= 12 - 1e-9);
        checkTrue("gap1>=12", g2 >= 12 - 1e-9);
        checkTrue("monotonic", r.at(0).toDouble() <= r.at(1).toDouble()
                                && r.at(1).toDouble() <= r.at(2).toDouble());
    }

    // ── distribute: clamp to upper bound ───────────────────────────────────────
    std::printf("-- distribute: clamp to hi --\n");
    {
        const QVariantList r = t.distribute(vl({195, 198}), vl({10, 10}), 2, 0, 200);
        checkNear("c1 == hi-h", r.at(1).toDouble(), 195);   // 200 - 5
        checkNear("c0 == c1-12", r.at(0).toDouble(), 183);
    }

    // ── distribute: clamp to lower bound ───────────────────────────────────────
    std::printf("-- distribute: clamp to lo --\n");
    {
        const QVariantList r = t.distribute(vl({2, 5}), vl({10, 10}), 2, 0, 200);
        checkNear("c0 == lo+h", r.at(0).toDouble(), 5);     // 0 + 5
        checkNear("c1 == c0+12", r.at(1).toDouble(), 17);
    }

    // ── activeStation ──────────────────────────────────────────────────────────
    std::printf("-- activeStation --\n");
    {
        const QVariantList p = phases({{0, 0}, {1, 180000}, {2, 620000}, {5, 950000}});
        checkEqI("before first -> -1", t.activeStation(p, -1), -1);
        checkEqI("at 0 -> 0", t.activeStation(p, 0), 0);
        checkEqI("mid -> 1", t.activeStation(p, 500000), 1);
        checkEqI("on station -> that", t.activeStation(p, 620000), 2);
        checkEqI("after last -> last", t.activeStation(p, 2000000), 3);
        checkEqI("empty -> -1", t.activeStation(QVariantList(), 100), -1);
    }

    // ── valueAtNearest ─────────────────────────────────────────────────────────
    std::printf("-- valueAtNearest --\n");
    {
        const QVariantList tt = vl({0, 100, 200, 300});
        const QVariantList vv = vl({10, 20, 30, 40});
        checkNear("before -> first", t.valueAtNearest(tt, vv, -50), 10);
        checkNear("after  -> last",  t.valueAtNearest(tt, vv, 400), 40);
        checkNear("140 -> 20 (100 nearer)", t.valueAtNearest(tt, vv, 140), 20);
        checkNear("160 -> 30 (200 nearer)", t.valueAtNearest(tt, vv, 160), 30);
        checkNear("150 tie -> lower (20)",  t.valueAtNearest(tt, vv, 150), 20);
        checkNear("empty -> 0", t.valueAtNearest(QVariantList(), QVariantList(), 100), 0);
    }

    // ── snap ───────────────────────────────────────────────────────────────────
    std::printf("-- snap --\n");
    {
        const QVariantList p = phases({{0, 0}, {1, 180000}, {2, 620000}, {5, 950000}});
        checkEqI("within tol -> station", t.snap(p, 185000, 20000), 180000);
        checkEqI("outside tol -> unchanged", t.snap(p, 300000, 20000), 300000);
        checkEqI("near last -> last", t.snap(p, 960000, 20000), 950000);
    }

    // ── phase names ─────────────────────────────────────────────────────────────
    std::printf("-- phase names --\n");
    {
        checkStr("full 0", t.phaseFullName(0), "Address");
        checkStr("full 5", t.phaseFullName(5), "Impact");
        checkStr("full 11", t.phaseFullName(11), "Follow-through");
        checkStr("full oob", t.phaseFullName(12), "P12");
        checkStr("tag 5", t.phaseShortTag(5), "IMP");
        checkStr("tag 8", t.phaseShortTag(8), "MBK");
        checkStr("tag oob", t.phaseShortTag(99), "P99");
    }

    std::printf("\n=== %s (%d assert failures) ===\n",
                g_fail ? "FAILURES PRESENT" : "ALL ASSERTS PASS", g_fail);
    return g_fail ? 1 : 0;
}
