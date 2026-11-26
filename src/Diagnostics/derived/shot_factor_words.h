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

// ONE SHOT'S FACTOR IN WORDS — pure, header-only (refactor 4-b: the carousel's shot summary). What a factor of one
// shot reads as, and which kind of news it is, so a view only maps the kind to its colour (R-QML-4: no domain rule in
// QML). The kinds are the tendency flow's (shot_factors.h; Mark, 10 Nov 2026):
//   "fault"  any value that is a fault (Thin, Fat, Push, Slice, Sky…)
//   "watch"  else a value to watch (Low)
//   "good"   else every value on its factor's good path (Solid; Straight, Fade, Draw; Mid, High)
//   ""       not read: the text is "-" (DEC-96: never a blank, never a zero). Also, in plain ink, a read value that
//            is in none of the three: an id shotFactorValueTable does not hold. Today none can arise — shotFactors
//            builds a factor only from the table's ids (a direction's non-misses are Fade and Draw, both good) — so
//            a new pack label shows plainly, with its word, until the table gives it a kind.
// The text is the golfer's word for each value (shotFactorValueLabel: the pack's golferShort, else the table's), a
// combined miss shown once and never with its halves (a push slice is "Push slice", not "Push slice + Slice + Push"),
// several values joined by " + " ("Toe + Weak strike").

#include "shot_factors.h"

#include <QString>
#include <QStringList>

namespace pinpoint::analysis {

struct ShotFactorWords {
    QString text = QStringLiteral("-");
    QString tone;   // "fault" | "watch" | "good" | "" (not read)
};

inline ShotFactorWords shotFactorWords(const ShotFactor &f, const QString &factor, const ShotOutcomePack *pack)
{
    ShotFactorWords out;
    if (!f.read || f.values.isEmpty())
        return out;
    QStringList shown;
    for (const QString &v : f.values) {
        bool half = false;
        for (const QString &u : f.values)
            if (u != v && tendencyMissHalves(u).contains(v)) half = true;
        if (!half && !shown.contains(v)) shown << v;
    }
    QStringList words;
    bool fault = false, watch = false, good = true;
    const QSet<QString> goodValues = shotFactorGoodValues(factor);
    for (const QString &v : shown) {
        const ShotFactorValueInfo *info = shotFactorValueInfo(v);
        words << (pack ? shotFactorValueLabel(*pack, v) : info ? info->label : v);
        if (f.faults.contains(v)) fault = true;
        else if (shotFactorValueWatch(v)) watch = true;
        if (!goodValues.contains(v)) good = false;
    }
    out.text = words.join(QStringLiteral(" + "));
    out.tone = fault ? QStringLiteral("fault") : watch ? QStringLiteral("watch") : good ? QStringLiteral("good") : QString();
    return out;
}

} // namespace pinpoint::analysis
