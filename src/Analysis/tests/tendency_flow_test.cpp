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
// tendency_flow_test — the Sankey layout (src/Coaching/tendency_flow.h; docs/design/tendencies_flow_design.md §8):
// conservation, determinism, folding, the edge cases, geometry, the first-failure rule, and the doc's 80-shot golden set.

#include "../../Coaching/tendency_flow.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>

using namespace pinpoint::analysis;

static int g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}

static TendencyFlowCombo combo(const char *s, const char *d, const char *f, int n)
{
    return TendencyFlowCombo{ QString::fromLatin1(s), QString::fromLatin1(d), QString::fromLatin1(f), n };
}

// The doc's appendix: a driver, 80 shots, judged with its illustrative good path (Solid, Straight, High).
static std::vector<TendencyFlowCombo> golden()
{
    return { combo("solid", "straight", "high", 22), combo("solid", "straight", "mid", 6), combo("solid", "slice", "high", 8),
             combo("solid", "slice", "mid", 4), combo("solid", "push", "high", 3), combo("solid", "push", "mid", 2),
             combo("solid", "pull", "mid", 2), combo("solid", "push_slice", "high", 3), combo("solid", "pull_hook", "low", 1),
             combo("thin", "straight", "low", 4), combo("thin", "slice", "low", 5), combo("thin", "slice", "mid", 2),
             combo("thin", "pull", "low", 3), combo("thin", "pull_hook", "low", 2), combo("strike_heel", "slice", "mid", 3),
             combo("strike_heel", "push_slice", "mid", 3), combo("strike_heel", "straight", "mid", 1),
             combo("strike_toe", "hook", "mid", 2), combo("strike_toe", "pull_hook", "low", 1), combo("chunk", "straight", "low", 1),
             combo("chunk", "pull", "low", 1), combo("chunk", "pull_hook", "low", 1) };
}

static FlowOptions docGood()
{
    FlowOptions o;
    o.good = { QSet<QString>{ "solid" }, QSet<QString>{ "straight" }, QSet<QString>{ "high" } };
    return o;
}

static int nodeCount(const FlowLayout &l, int col, const char *value)
{
    for (const FlowNode &n : l.nodes) if (n.column == col && n.value == QLatin1String(value)) return n.count;
    return -1;
}

static bool conserved(const FlowLayout &l)
{
    std::array<int, 3> colSum{};
    for (const FlowNode &n : l.nodes) colSum[size_t(n.column)] += n.count;
    if (colSum[0] != l.total || colSum[1] != l.total || colSum[2] != l.total) return false;
    for (size_t i = 0; i < l.nodes.size(); ++i) {
        int in = 0, out = 0;
        for (const FlowLink &k : l.links) { if (k.toNode == int(i)) in += k.count; if (k.fromNode == int(i)) out += k.count; }
        const FlowNode &n = l.nodes[i];
        if (n.column == 1 && (in != n.count || out != n.count)) return false;
        if (n.column == 0 && out != n.count) return false;
        if (n.column == 2 && in != n.count) return false;
    }
    return true;
}

static bool geometryOk(const FlowLayout &l, double H)
{
    for (int col = 0; col < 3; ++col) {
        double last = -1e9;
        for (const FlowNode &n : l.nodes) {
            if (n.column != col) continue;
            if (n.y < -1e-9 || n.y + n.h > H + 1e-9 || n.y < last - 1e-9) return false;
            last = n.y + n.h;
        }
    }
    for (const FlowLink &k : l.links) {
        const FlowNode &a = l.nodes[size_t(k.fromNode)], &b = l.nodes[size_t(k.toNode)];
        if (k.y0 < a.y - 1e-9 || k.y0 + k.h > a.y + a.h + 1e-9 || k.y1 < b.y - 1e-9 || k.y1 + k.h > b.y + b.h + 1e-9) return false;
    }
    return true;
}

static QString signature(const FlowLayout &l)
{
    QStringList s;
    for (const FlowNode &n : l.nodes) s << QStringLiteral("%1:%2=%3@%4").arg(n.column).arg(n.value).arg(n.count).arg(n.y, 0, 'f', 6);
    for (const FlowLink &k : l.links)
        s << QStringLiteral("%1>%2/%3=%4@%5,%6").arg(k.fromNode).arg(k.toNode).arg(int(k.group)).arg(k.count).arg(k.y0, 0, 'f', 6).arg(k.y1, 0, 'f', 6);
    return s.join(QLatin1Char(' '));
}

int main()
{
    std::printf("tendency_flow_test\n");

    std::printf("\nthe golden set (doc appendix, 80 drives):\n");
    {
        const FlowLayout l = layoutTendencyFlow(golden(), docGood());
        check(l.total == 80 && l.excluded == 0, "80 shots, none excluded");
        check(l.groups[0] == 22 && l.groups[1] == 6 && l.groups[2] == 23 && l.groups[3] == 29,
              "groups: ideal 22, flight off 6, off line 23, mis-struck 29");
        check(std::lround(100.0 * l.groups[0] / l.total) == 28, "28% ideal");
        check(nodeCount(l, 0, "solid") == 51 && nodeCount(l, 0, "thin") == 16 && nodeCount(l, 0, "strike_heel") == 7
                  && nodeCount(l, 0, "strike_toe") == 3 && nodeCount(l, 0, "chunk") == 3,
              "strike: Solid 51, Thin 16, Heel 7, Toe 3, Fat 3");
        check(nodeCount(l, 2, "high") == 36 && nodeCount(l, 2, "mid") == 25 && nodeCount(l, 2, "low") == 19,
              "flight: High 36, Mid 25, Low 19");
        check(nodeCount(l, 1, "hook") == 2 && nodeCount(l, 1, "other") == -1 && nodeCount(l, 0, "other") == -1,
              "Hook (2) is the only small direction value, so nothing folds; Toe and Fat (3) are over the line");
        check(conserved(l), "conservation: every direction node's in = out = its count; each column sums to the total");
        check(geometryOk(l, 360.0), "geometry: no overlap in a column, every node and ribbon end inside the plot");
        check(l.nodes.front().value == QLatin1String("solid") && l.nodes.front().good, "Solid, the good strike, on top");
        QStringList dir;
        for (const FlowNode &n : l.nodes) if (n.column == 1) dir << n.value;
        check(dir == QStringList{ "straight", "push", "slice", "push_slice", "pull", "hook", "pull_hook" },
              "direction: Straight, then the right side (push, slice, push-slice), then the left");
        if (dir != QStringList{ "straight", "push", "slice", "push_slice", "pull", "hook", "pull_hook" })
            std::printf("    direction order: %s\n", qPrintable(dir.join(" ")));
        QStringList flight;
        for (const FlowNode &n : l.nodes) if (n.column == 2) flight << n.value;
        check(flight == QStringList{ "high", "low", "mid" } || flight == QStringList{ "high", "mid", "low" },
              "flight: the good value (High) first");
        bool idealLast = true, seenIdeal = false;
        for (const FlowLink &k : l.links) { if (k.group == FlowGroup::Ideal) seenIdeal = true; else if (seenIdeal) idealLast = false; }
        check(seenIdeal && idealLast, "the ideal ribbons are drawn last, on top");
    }

    std::printf("\nPPS's good path (Mark, 10 Nov 2026):\n");
    {
        const FlowLayout l = layoutTendencyFlow(golden());     // the default options: the value table's good values
        // Ideal now holds solid · straight · mid (6) as well as high (22); solid · pull_hook · low stays off line.
        check(l.groups[0] == 28 && l.groups[1] == 0 && l.groups[2] == 23 && l.groups[3] == 29,
              "Mid is good flight: ideal 28 (22 high + 6 mid), no flight-off shots here, off line 23, mis-struck 29");
        QStringList flight;
        for (const FlowNode &n : l.nodes) if (n.column == 2) flight << n.value;
        check(flight == QStringList{ "mid", "high", "low" }, "flight: the good values first (Mid, High in the table's order), then Low");
        if (flight != QStringList{ "mid", "high", "low" }) std::printf("    flight order: %s\n", qPrintable(flight.join(" ")));
        const FlowLayout fd = layoutTendencyFlow({ combo("solid", "fade", "mid", 5), combo("solid", "draw", "high", 5),
                                                  combo("solid", "straight", "low", 2) });
        check(fd.groups[0] == 10 && fd.groups[1] == 2, "Fade and Draw are on the good path; Low is flight off");
    }

    std::printf("\nthe first-failure rule:\n");
    {
        FlowOptions o = docGood();
        o.good[2] = QSet<QString>{ "mid" };
        const FlowLayout l = layoutTendencyFlow({ combo("solid", "straight", "mid", 3), combo("solid", "straight", "high", 2),
                                                 combo("solid", "slice", "low", 1), combo("thin", "straight", "mid", 4) }, o);
        check(l.groups[0] == 3 && l.groups[1] == 2 && l.groups[2] == 1 && l.groups[3] == 4,
              "with Mid the good flight: ideal 3, flight off 2 (high), off line 1, mis-struck 4 (a thin is mis-struck whatever follows)");
    }

    std::printf("\nfolding:\n");
    {
        std::vector<TendencyFlowCombo> in{ combo("solid", "straight", "mid", 40), combo("thin", "straight", "mid", 10),
                                           combo("top", "straight", "low", 1), combo("shank", "push", "low", 1),
                                           combo("solid", "hook", "mid", 1) };
        const FlowLayout l = layoutTendencyFlow(in);
        check(nodeCount(l, 0, "other") == 2 && nodeCount(l, 0, "top") == -1 && nodeCount(l, 0, "shank") == -1,
              "Top and Shank (1 each, under max(2, 3% of 53)) fold into one Other");
        check(l.nodes[size_t(std::find_if(l.nodes.begin(), l.nodes.end(), [](const FlowNode &n) { return n.column == 0 && n.other; })
                             - l.nodes.begin())].folded == QStringList{ "top", "shank" },
              "…which says what it holds, in the column's order");
        int lastStrike = -1;
        for (int i = 0; i < int(l.nodes.size()); ++i) if (l.nodes[size_t(i)].column == 0) lastStrike = i;
        check(l.nodes[size_t(lastStrike)].other, "…and sits last in its column");
        check(nodeCount(l, 1, "other") == 2 && nodeCount(l, 1, "hook") == -1 && nodeCount(l, 1, "push") == -1,
              "direction: Push and Hook (1 each) fold together too, column by column");
        const FlowLayout one = layoutTendencyFlow({ combo("solid", "straight", "mid", 40), combo("top", "straight", "mid", 1) });
        check(nodeCount(one, 0, "top") == 1 && nodeCount(one, 0, "other") == -1, "a single small value is never folded");
        const FlowLayout good = layoutTendencyFlow({ combo("thin", "straight", "mid", 40), combo("solid", "straight", "mid", 1),
                                                     combo("chunk", "straight", "mid", 1), combo("top", "straight", "mid", 1) });
        check(nodeCount(good, 0, "solid") == 1 && nodeCount(good, 0, "other") == 2, "the good value is never folded, however few");
        check(conserved(l) && conserved(good), "folding keeps every shot");
    }

    std::printf("\nedge cases:\n");
    {
        const FlowLayout empty = layoutTendencyFlow({});
        check(empty.total == 0 && empty.nodes.empty() && empty.links.empty() && !empty.enough(), "no shots: nothing, and not enough");
        const FlowLayout ideal = layoutTendencyFlow({ combo("solid", "straight", "mid", 12) });
        check(ideal.nodes.size() == 3 && ideal.links.size() == 2 && ideal.links[0].group == FlowGroup::Ideal
                  && ideal.links[1].group == FlowGroup::Ideal && ideal.enough(),
              "every shot ideal: one node a column, only ideal ribbons");
        const FlowLayout none = layoutTendencyFlow({ combo("thin", "slice", "low", 12) });
        check(std::none_of(none.links.begin(), none.links.end(), [](const FlowLink &k) { return k.group == FlowGroup::Ideal; }),
              "no shot ideal: no ideal ribbon");
        const FlowLayout single = layoutTendencyFlow({ combo("solid", "pull", "high", 1) });
        check(single.total == 1 && single.nodes.size() == 3 && conserved(single) && geometryOk(single, 360.0) && !single.enough(),
              "a single shot lays out (and is under the minimum)");
        const FlowLayout ex = layoutTendencyFlow({ combo("solid", "straight", "", 5), combo("", "straight", "mid", 2),
                                                   combo("solid", "straight", "mid", 10) });
        check(ex.total == 10 && ex.excluded == 7, "a shot with a family unread is excluded and counted");
    }

    std::printf("\ndeterminism and geometry:\n");
    {
        std::vector<TendencyFlowCombo> in = golden();
        const QString a = signature(layoutTendencyFlow(in, docGood()));
        std::mt19937 rng(7);
        bool same = true;
        for (int i = 0; i < 5; ++i) {
            std::shuffle(in.begin(), in.end(), rng);
            same = same && signature(layoutTendencyFlow(in, docGood())) == a;
        }
        check(same, "the same shots in any order give the same layout");
        FlowOptions o = docGood();
        o.plotHeight = 200.0;
        o.gap = 8.0;
        const FlowLayout l = layoutTendencyFlow(golden(), o);
        check(geometryOk(l, 200.0), "a smaller plot: still inside it, still no overlap");
        bool through = true;
        for (const FlowLink &k : l.links)
            through = through && std::find(k.through.begin(), k.through.end(), k.fromNode) != k.through.end()
                      && std::find(k.through.begin(), k.through.end(), k.toNode) != k.through.end() && k.through.size() >= 3;
        check(through, "each ribbon knows the nodes its shots pass through, its own two and at least one beyond");
    }

    std::printf("\n%s (%d)\n", g_fail ? "FAILED" : "PASSED", g_fail);
    return g_fail ? 1 : 0;
}
