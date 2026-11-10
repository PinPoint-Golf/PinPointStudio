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

// THE TENDENCY FLOW LAYOUT — the Sankey of docs/design/tendencies_flow_design.md §4: every shot flowing left to right,
// strike → direction → flight, its ribbon in the colour of the family where it first left the good path. Pure: no Qt
// Quick, no theme, no words. It counts, folds, orders and places; a renderer scales and paints (PpSankey on screen).
//
// THE GOOD PATH (Mark, 10 Nov 2026, deciding the doc's §11.1): each factor's good values are the ones that are neither
// a fault nor a value to watch (shotFactorGoodValues): Solid; Straight, Fade or Draw (shapes, never a miss); Mid or
// High (Low is amber). The doc's illustrative "High only, Straight only" is a FlowOptions choice the golden test makes.
//
// Groups, by the first family at which a shot left the good path (§3.6). Stacking order inside a node is the enum's
// (ideal on top); `links` come out in DRAW order, the ideal last so it sits on top (§4.4).
//
// Values are the factor's displayIds (the left-hander's own words); "other" is a folded node (§4.1). A shot with any
// family unread ("" in a combo) is not drawn and is counted in `excluded` (§3.2).

#include "shot_factors.h"

#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <set>
#include <tuple>
#include <vector>

namespace pinpoint::analysis {

inline constexpr int kTendencyFlowMinShots = 10;    // under it, the empty state (§3.3)

enum class FlowGroup { Ideal = 0, FlightOff = 1, OffLine = 2, MisStruck = 3 };
inline constexpr int kFlowGroups = 4;

// One combination of the three factors' values (each shot's value in that factor's bar) and how many shots had it.
struct TendencyFlowCombo {
    QString strike, direction, flight;   // displayIds; "" when that factor wasn't read
    int     shots = 0;
};

struct FlowOptions {
    double plotHeight = 360.0;           // logical units; renderers scale
    double gap        = 12.0;            // between nodes in a column
    double foldPct    = 0.03;            // a value under max(foldMin, foldPct × total) shots folds into "other"
    int    foldMin    = 2;
    std::array<QSet<QString>, 3> good{ shotFactorGoodValues(shotFactorStrike()), shotFactorGoodValues(shotFactorDirection()),
                                       shotFactorGoodValues(shotFactorFlight()) };
};

struct FlowNode {
    int     column = 0;                  // 0 strike, 1 direction, 2 flight
    QString value;                       // a displayId, or "other"
    bool    good = false, other = false;
    QStringList folded;                  // the values an "other" node holds
    int     count = 0;
    std::array<int, kFlowGroups> byGroup{};
    std::vector<int> with;               // with[j]: this node's shots that also pass through node j (with[self] = count)
    double  y = 0.0, h = 0.0;
};

struct FlowLink {
    int       fromNode = -1, toNode = -1;
    FlowGroup group = FlowGroup::Ideal;
    int       count = 0;
    double    y0 = 0.0, y1 = 0.0, h = 0.0;
    std::vector<int> through;            // every node the link's shots pass through, sorted: what a node's hover lights
    std::vector<int> byNode;             // byNode[j]: this link's shots that pass through node j (its two ends = count)
};

struct FlowLayout {
    std::vector<FlowNode> nodes;         // column by column, each top to bottom
    std::vector<FlowLink> links;         // in draw order: mis-struck, off line, flight off, then the ideal on top
    std::array<int, kFlowGroups> groups{};
    int    total = 0, excluded = 0, nMax = 0;
    double k = 0.0;                      // units per shot, the same in every column
    bool   enough() const { return total >= kTendencyFlowMinShots; }
};

namespace tendency_flow_detail {

inline FlowGroup groupOf(const std::array<QString, 3> &v, const FlowOptions &o)
{
    if (!o.good[0].contains(v[0])) return FlowGroup::MisStruck;
    if (!o.good[1].contains(v[1])) return FlowGroup::OffLine;
    if (!o.good[2].contains(v[2])) return FlowGroup::FlightOff;
    return FlowGroup::Ideal;
}

// A column's order, top to bottom (§4.2): the good values first, then (direction) the right side then the left, then
// the vocabulary's order; "other" last.
inline bool before(int column, const QString &a, const QString &b, const FlowOptions &o)
{
    const auto key = [column, &o](const QString &v) {
        const bool other = v == QLatin1String("other");
        const bool good  = o.good[size_t(column)].contains(v);
        const QString side = column == 1 ? shotFactorValueSide(v) : QString();
        const int sideRank = side.isEmpty() ? 0 : side == QLatin1String("right") ? 1 : 2;
        return std::array<int, 4>{ other ? 1 : 0, good ? 0 : 1, sideRank, shotFactorValueRank(v) };
    };
    const auto ka = key(a), kb = key(b);
    return ka != kb ? ka < kb : a < b;
}

} // namespace tendency_flow_detail

inline FlowLayout layoutTendencyFlow(const std::vector<TendencyFlowCombo> &input, const FlowOptions &o = {})
{
    using namespace tendency_flow_detail;
    FlowLayout out;

    // ── Aggregate: the drawn shots by full combination, the rest excluded ──
    std::map<std::array<QString, 3>, int> combos;   // ordered, so the result never depends on the input's order
    for (const TendencyFlowCombo &c : input) {
        if (c.shots <= 0) continue;
        if (c.strike.isEmpty() || c.direction.isEmpty() || c.flight.isEmpty()) { out.excluded += c.shots; continue; }
        combos[{ c.strike, c.direction, c.flight }] += c.shots;
        out.total += c.shots;
    }
    if (out.total == 0) return out;

    // ── Fold small values, column by column, before any link is made (§4.1) ──
    std::array<QHash<QString, QString>, 3> foldTo;
    std::array<QStringList, 3> foldedOf;
    const double threshold = std::max(double(o.foldMin), o.foldPct * double(out.total));
    for (int col = 0; col < 3; ++col) {
        QHash<QString, int> n;
        for (const auto &[v, shots] : combos) n[v[size_t(col)]] += shots;
        QStringList small;
        for (auto it = n.constBegin(); it != n.constEnd(); ++it)
            if (!o.good[size_t(col)].contains(it.key()) && double(it.value()) < threshold) small << it.key();
        if (small.size() < 2) continue;                         // never an "other" of one value
        std::sort(small.begin(), small.end(), [col, &o](const QString &a, const QString &b) { return before(col, a, b, o); });
        for (const QString &v : small) foldTo[size_t(col)].insert(v, QStringLiteral("other"));
        foldedOf[size_t(col)] = small;
    }
    std::map<std::array<QString, 3>, int> folded;
    for (const auto &[v, shots] : combos) {
        std::array<QString, 3> f = v;
        for (int col = 0; col < 3; ++col) f[size_t(col)] = foldTo[size_t(col)].value(v[size_t(col)], v[size_t(col)]);
        folded[f] += shots;
    }

    // ── Nodes, ordered, with their counts by group ──
    std::array<QStringList, 3> order;
    std::array<QHash<QString, int>, 3> nodeIndex;
    {
        std::array<QHash<QString, FlowNode>, 3> byValue;
        for (const auto &[v, shots] : folded) {
            const FlowGroup g = groupOf(v, o);
            out.groups[size_t(g)] += shots;
            for (int col = 0; col < 3; ++col) {
                FlowNode &n = byValue[size_t(col)][v[size_t(col)]];
                n.column = col;
                n.value  = v[size_t(col)];
                n.count += shots;
                n.byGroup[size_t(g)] += shots;
            }
        }
        for (int col = 0; col < 3; ++col) {
            QStringList values = byValue[size_t(col)].keys();
            std::sort(values.begin(), values.end(), [col, &o](const QString &a, const QString &b) { return before(col, a, b, o); });
            order[size_t(col)] = values;
            out.nMax = std::max(out.nMax, int(values.size()));
            for (const QString &v : values) {
                FlowNode n = byValue[size_t(col)].value(v);
                n.good  = o.good[size_t(col)].contains(v);
                n.other = v == QLatin1String("other");
                if (n.other) n.folded = foldedOf[size_t(col)];
                nodeIndex[size_t(col)].insert(v, int(out.nodes.size()));
                out.nodes.push_back(n);
            }
        }
    }

    // ── Which shots pass through which nodes: what examining a node or ribbon counts ──
    for (FlowNode &n : out.nodes) n.with.assign(out.nodes.size(), 0);
    for (const auto &[v, shots] : folded) {
        const std::array<int, 3> at{ nodeIndex[0].value(v[0]), nodeIndex[1].value(v[1]), nodeIndex[2].value(v[2]) };
        for (int a : at) for (int b : at) out.nodes[size_t(a)].with[size_t(b)] += shots;
    }

    // ── Geometry: one scale for every column, each stack centred (§4.3) ──
    out.k = std::max(0.0, (o.plotHeight - o.gap * double(out.nMax - 1)) / double(out.total));
    for (int col = 0; col < 3; ++col) {
        const int n = int(order[size_t(col)].size());
        const double stack = out.k * double(out.total) + o.gap * double(n - 1);
        double y = (o.plotHeight - stack) / 2.0;
        for (const QString &v : order[size_t(col)]) {
            FlowNode &node = out.nodes[size_t(nodeIndex[size_t(col)].value(v))];
            node.y = y;
            node.h = out.k * double(node.count);
            y += node.h + o.gap;
        }
    }

    // ── Links: strike → direction and direction → flight, by (from, to, group) (§4.4) ──
    struct Key { int from, to, group; bool operator<(const Key &b) const { return std::tie(from, to, group) < std::tie(b.from, b.to, b.group); } };
    std::map<Key, std::pair<int, std::set<int>>> agg;
    std::map<Key, std::vector<int>> byNode;
    for (const auto &[v, shots] : folded) {
        const int g = int(groupOf(v, o));
        const int n0 = nodeIndex[0].value(v[0]), n1 = nodeIndex[1].value(v[1]), n2 = nodeIndex[2].value(v[2]);
        for (const Key &key : { Key{ n0, n1, g }, Key{ n1, n2, g } }) {
            auto &[count, through] = agg[key];
            count += shots;
            through.insert({ n0, n1, n2 });
            std::vector<int> &at = byNode[key];
            at.resize(out.nodes.size(), 0);
            for (int n : { n0, n1, n2 }) at[size_t(n)] += shots;
        }
    }
    std::vector<FlowLink> links;
    for (const auto &[key, val] : agg) {
        FlowLink l;
        l.fromNode = key.from;
        l.toNode   = key.to;
        l.group    = FlowGroup(key.group);
        l.count    = val.first;
        l.h        = out.k * double(l.count);
        l.through.assign(val.second.begin(), val.second.end());
        l.byNode = byNode[key];
        links.push_back(l);
    }
    // Out of each source by (target's place, group); into each target by (source's place, group). Node indices run in
    // column order, top to bottom, so an index is a place.
    std::vector<int> byOut(links.size()), byIn(links.size());
    for (size_t i = 0; i < links.size(); ++i) byOut[i] = byIn[i] = int(i);
    std::sort(byOut.begin(), byOut.end(), [&links](int a, int b) {
        const FlowLink &x = links[size_t(a)], &y = links[size_t(b)];
        return std::tie(x.fromNode, x.toNode, x.group) < std::tie(y.fromNode, y.toNode, y.group);
    });
    std::sort(byIn.begin(), byIn.end(), [&links](int a, int b) {
        const FlowLink &x = links[size_t(a)], &y = links[size_t(b)];
        return std::tie(x.toNode, x.fromNode, x.group) < std::tie(y.toNode, y.fromNode, y.group);
    });
    std::vector<double> outCursor(out.nodes.size()), inCursor(out.nodes.size());
    for (size_t i = 0; i < out.nodes.size(); ++i) outCursor[i] = inCursor[i] = out.nodes[i].y;
    for (int i : byOut) { FlowLink &l = links[size_t(i)]; l.y0 = outCursor[size_t(l.fromNode)]; outCursor[size_t(l.fromNode)] += l.h; }
    for (int i : byIn)  { FlowLink &l = links[size_t(i)]; l.y1 = inCursor[size_t(l.toNode)];    inCursor[size_t(l.toNode)]    += l.h; }

    // Draw order: mis-struck, off line, flight off, the ideal last (on top); within a group, left to right, top down.
    std::stable_sort(links.begin(), links.end(), [](const FlowLink &a, const FlowLink &b) {
        const int ga = kFlowGroups - 1 - int(a.group), gb = kFlowGroups - 1 - int(b.group);
        return std::tie(ga, a.fromNode, a.toNode) < std::tie(gb, b.fromNode, b.toNode);
    });
    out.links = std::move(links);
    return out;
}

} // namespace pinpoint::analysis
