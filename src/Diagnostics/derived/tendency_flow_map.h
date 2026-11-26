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
#pragma once

// THE TENDENCY FLOW, IN WORDS AND PLACES — one map for every surface that draws it: Home's Tendencies tab
// (WorkOnsController::tendencyFlow) and the report's Your tendencies page (report_tendencies.h). The rows of `group`
// ("" for every club) pooled and laid out by layoutTendencyFlow (tendency_flow.h) in a plot `plotHeight` high with `gap`
// between nodes, every word final. The shape is WorkOnsController::tendencyFlow's (work_ons_controller.h).

#include "tendencies.h"
#include "tendency_flow.h"

#include <QVariantList>
#include <QVariantMap>

#include <cmath>

namespace pinpoint::analysis {

inline QVariantMap tendencyFlowMap(const TendencySet &set, const QString &group, const ShotOutcomePack &pack, double plotHeight,
                                   double gap)
{
    std::vector<TendencyFlowCombo> combos;
    for (const TendencyItem &it : set.items)
        if (group.isEmpty() || it.clubGroup == group)
            combos.insert(combos.end(), it.flow.begin(), it.flow.end());

    FlowOptions opt;
    opt.plotHeight = std::max(1.0, plotHeight);
    opt.gap        = std::max(0.0, gap);
    const FlowLayout l = layoutTendencyFlow(combos, opt);
    const auto pct = [&l](int n) { return l.total > 0 ? int(std::lround(100.0 * double(n) / double(l.total))) : 0; };
    const auto shots = [](int n) { return QStringLiteral("%1 %2").arg(n).arg(n == 1 ? QStringLiteral("shot") : QStringLiteral("shots")); };
    const auto labelOf = [&pack](const FlowNode &n) { return n.other ? QStringLiteral("Other") : shotFactorValueLabel(pack, n.value); };

    // The good path in words, from the good values themselves (§3.6), one family at a time, in the columns' order:
    // "solid"; "mid or high"; "on line" (more than Straight is good).
    const auto goodWords = [&pack, &opt](int col) {
        const QSet<QString> good = opt.goodIn(col);
        QStringList ids(good.begin(), good.end());
        if (opt.columns[size_t(col)] == shotFactorDirection() && ids.size() > 1) return QStringLiteral("on line");
        std::sort(ids.begin(), ids.end(), [](const QString &a, const QString &b) { return shotFactorValueRank(a) < shotFactorValueRank(b); });
        QStringList words;
        for (const QString &id : ids) words << shotFactorValueLabel(pack, id).toLower();
        return words.join(QStringLiteral(" or "));
    };
    const auto titleOf = [](const QString &f) {
        return f == shotFactorStrike() ? QStringLiteral("Strike") : f == shotFactorFlight() ? QStringLiteral("Flight") : QStringLiteral("Direction");
    };
    // A family on the path ("struck well"), off it ("flight off"), and in the headline ("flew mid or high").
    const auto wellWords = [](const QString &f) {
        return f == shotFactorStrike() ? QStringLiteral("struck well") : f == shotFactorFlight() ? QStringLiteral("flew well") : QStringLiteral("on line");
    };
    const auto offWords = [](const QString &f) {
        return f == shotFactorStrike() ? QStringLiteral("mis-struck") : f == shotFactorFlight() ? QStringLiteral("flight off") : QStringLiteral("off line");
    };
    const auto headWords = [&goodWords, &opt](int col) {
        const QString &f = opt.columns[size_t(col)];
        const QString w = goodWords(col);
        return f == shotFactorStrike() ? QStringLiteral("were ") + w : f == shotFactorFlight() ? QStringLiteral("flew ") + w : w;
    };
    // Each group is named by the family just before it in precedence and the one it failed (§3.6): "Struck well, off line",
    // "On line, flight off", "Mis-struck". The ideal by its good values in the columns' order: "Solid, on line, mid or high".
    std::array<QString, kFlowGroups> groupNames;
    {
        QStringList ideal;
        for (int c = 0; c < 3; ++c) ideal << goodWords(c);
        groupNames[size_t(FlowGroup::Ideal)] = outcomeCapitalised(ideal.join(QStringLiteral(", ")));
        QString previous;
        for (const QString &f : opt.precedence) {
            const QString words = previous.isEmpty() ? offWords(f) : previous + QStringLiteral(", ") + offWords(f);
            groupNames[size_t(tendency_flow_detail::groupFor(f))] = outcomeCapitalised(words);
            previous = wellWords(f);
        }
    }
    const std::array<int, kFlowGroups> rank = tendency_flow_detail::groupRanks(opt);
    QStringList titles, readOrder;
    for (int c = 0; c < 3; ++c) { titles << titleOf(opt.columns[size_t(c)]); readOrder << titleOf(opt.columns[size_t(c)]).toLower(); }

    const QString clubWords = group.isEmpty() ? QStringLiteral("All clubs") : tendencyClubLabel(group);
    const QString noun = group.isEmpty() ? QStringLiteral("shots")
                       : group == QLatin1String("driver") ? QStringLiteral("drives")
                                                          : tendencyClubShotWord(group) + QStringLiteral(" shots");

    // The legend in stacking order, top down: the ideal, then flight off, off line, mis-struck.
    std::array<int, kFlowGroups> byRank{};
    for (int g = 0; g < kFlowGroups; ++g) byRank[size_t(rank[size_t(g)])] = g;
    QVariantList groups;
    for (int g : byRank)
        groups << QVariantMap{ { QStringLiteral("group"), g }, { QStringLiteral("name"), groupNames[size_t(g)] },
                               { QStringLiteral("count"), l.groups[size_t(g)] }, { QStringLiteral("percent"), pct(l.groups[size_t(g)]) },
                               { QStringLiteral("line"), QStringLiteral("%1 · %2%").arg(l.groups[size_t(g)]).arg(pct(l.groups[size_t(g)])) } };
    // What a node's label says while a node or ribbon is examined: how many of the examined shots pass through it, and
    // what share of them ("2 · 67%"); "" for none, and the node dims.
    const auto examinedSubs = [](const std::vector<int> &at, int of) {
        QVariantList out;
        for (int n : at)
            out << (n > 0 && of > 0 ? QStringLiteral("%1 · %2%").arg(n).arg(int(std::lround(100.0 * double(n) / double(of)))) : QString());
        return out;
    };
    QVariantList nodes;
    for (const FlowNode &n : l.nodes) {
        QVariantList byGroup;
        for (int c : n.byGroup) byGroup << c;
        QString label = labelOf(n);
        nodes << QVariantMap{ { QStringLiteral("column"), n.column }, { QStringLiteral("value"), n.value }, { QStringLiteral("label"), label },
                              { QStringLiteral("good"), n.good }, { QStringLiteral("other"), n.other }, { QStringLiteral("count"), n.count },
                              // its tone in the rows' scheme: good, a value to watch (Low), a fault, or a fold of several
                              { QStringLiteral("kind"), n.good ? QStringLiteral("good") : n.other ? QStringLiteral("other")
                                                      : shotFactorValueWatch(n.value) ? QStringLiteral("watch") : QStringLiteral("fault") },
                              { QStringLiteral("percent"), pct(n.count) },
                              { QStringLiteral("sub"), QStringLiteral("%1 · %2%").arg(n.count).arg(pct(n.count)) },
                              { QStringLiteral("y"), n.y }, { QStringLiteral("h"), n.h }, { QStringLiteral("byGroup"), byGroup },
                              { QStringLiteral("withSub"), examinedSubs(n.with, n.count) } };
    }
    QVariantList links;
    for (const FlowLink &k : l.links) {
        QVariantList through;
        for (int t : k.through) through << t;
        links << QVariantMap{ { QStringLiteral("from"), k.fromNode }, { QStringLiteral("to"), k.toNode }, { QStringLiteral("group"), int(k.group) },
                              { QStringLiteral("count"), k.count }, { QStringLiteral("percent"), pct(k.count) },
                              { QStringLiteral("y0"), k.y0 }, { QStringLiteral("y1"), k.y1 }, { QStringLiteral("h"), k.h },
                              { QStringLiteral("through"), through },
                              { QStringLiteral("bySub"), examinedSubs(k.byNode, k.count) },
                              { QStringLiteral("line"), QStringLiteral("%1 → %2: %3 (%4%)")
                                    .arg(labelOf(l.nodes[size_t(k.fromNode)]), labelOf(l.nodes[size_t(k.toNode)]), shots(k.count))
                                    .arg(pct(k.count)) } };
    }

    return QVariantMap{
        { QStringLiteral("enough"),   l.enough() },
        { QStringLiteral("total"),    l.total },
        { QStringLiteral("excluded"), l.excluded },
        { QStringLiteral("idealPct"), pct(l.groups[0]) },
        { QStringLiteral("micro"),    QStringLiteral("Tendencies · %1 · %2").arg(clubWords, shots(l.total)) },
        { QStringLiteral("headline"), QStringLiteral("%1% of your %2 %3, %4 and %5")
                                          .arg(pct(l.groups[0])).arg(noun, headWords(0), headWords(1), headWords(2)) },
        { QStringLiteral("subtitle"), QStringLiteral("Read left to right: %1, then %2, then %3. Green is the path you want. Every other "
                                                     "colour shows where a shot first left it.").arg(readOrder[0], readOrder[1], readOrder[2]) },
        { QStringLiteral("emptyLine"), QStringLiteral("Hit at least %1 shots with %2, %3 and %4 all read, with %5, to see how your "
                                                      "shots flow.")
                                           .arg(kTendencyFlowMinShots).arg(readOrder[0], readOrder[1], readOrder[2],
                                                group.isEmpty() ? QStringLiteral("any club") : QStringLiteral("these clubs")) },
        { QStringLiteral("excludedLine"), l.excluded == 0 ? QString()
                                          : QStringLiteral("%1 not shown: %2, %3 or %4 wasn't read.")
                                                .arg(shots(l.excluded), readOrder[0], readOrder[1], readOrder[2]) },
        { QStringLiteral("columns"), QVariant(titles) },
        { QStringLiteral("groups"), groups },
        { QStringLiteral("nodes"),  nodes },
        { QStringLiteral("links"),  links },
    };
}

} // namespace pinpoint::analysis
