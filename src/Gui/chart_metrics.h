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

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

// ChartMetrics — the C++ home for the derivation maths that the "no JavaScript logic
// in QML" rule keeps out of PpMetricChart, exactly the TimelineLabels shape. Stateless:
// every method is const and depends only on its arguments, so one shared instance can be
// declared declaratively (ChartMetrics { id: metrics }) and reused by every chart.
//
// Phase names/tags are NOT duplicated here — the chart composes segment labels in QML from
// phaseA/phaseB via TimelineLabels.phaseShortTag, and the crosshair value-at-cursor reuses
// TimelineLabels.valueAtNearest. This class owns only the segment vocabulary, the
// per-window summary statistics, and the metric-key → short-name map.
class ChartMetrics : public QObject
{
    Q_OBJECT
    QML_ELEMENT

public:
    explicit ChartMetrics(QObject *parent = nullptr) : QObject(parent) {}

    // Segment list for a swing. [0] = Full ({startUs:0, endUs:spanUs, phaseA:-1, phaseB:-1});
    // then one entry per adjacent phase pair, ordered by time:
    //   { startUs, endUs, phaseA:int, phaseB:int }.
    // The label is composed in QML from phaseA/phaseB via TimelineLabels.phaseShortTag (no
    // tag strings duplicated here). Mirrors swing_data_source.cpp segment logic so the
    // segment vocabulary is identical. `phases` is analysisDetail.phases ([{phase,t_us,…}]).
    Q_INVOKABLE QVariantList segments(const QVariantList &phases, qint64 spanUs) const;

    // Per-metric summary over [startUs, endUs], window edges linearly interpolated:
    //   { start, end, min, max, peak, range, delta, rate, tPeakUs }
    // peak = the extremum of larger magnitude; delta = end-start; range = max-min;
    // rate = max |Δvalue/Δt| between consecutive in-window samples, in deg per 100 ms;
    // tPeakUs = the time at which peak occurs. `tUs`/`value` are the parallel arrays from
    // analysisDetail.series[i] (tUs ascending).
    Q_INVOKABLE QVariantMap summary(const QVariantList &tUs, const QVariantList &value,
                                    qint64 startUs, qint64 endUs) const;

    // Compact display name for a metric key (e.g. "leadWristFlexExt" → "Bow/cup"), or ""
    // when the key has no short form — the caller then falls back to series.label. Single
    // source of truth for metric short names, mirroring TimelineLabels::phaseShortTag.
    Q_INVOKABLE QString shortLabel(const QString &key) const;

    // "Nice" Y-axis tick values across [lo, hi] at a 1/2/5×10ⁿ step chosen so there are
    // about `maxTicks` of them. Returns the tick values (doubles) the chart labels + grids.
    Q_INVOKABLE QVariantList niceTicks(double lo, double hi, int maxTicks) const;

    // X-axis tick offsets in milliseconds relative to impact, for the domain
    // [domStartUs, domEndUs]. Each returned int `ms` marks a gridline at impactUs+ms*1000
    // that falls inside the domain; the step widens with the span. The chart labels them
    // "(+)ms" and positions each via its own xForT(impactUs + ms*1000).
    Q_INVOKABLE QVariantList timeTicksMs(qint64 domStartUs, qint64 domEndUs,
                                         qint64 impactUs) const;

    // Phase enum of the station nearest `us` (or -1 when `phases` is empty). Used to label
    // a free-dragged ("Custom") window with the phases bracketing its edges.
    Q_INVOKABLE int nearestPhase(const QVariantList &phases, qint64 us) const;

    // Band ("good"/"attention"/"warn") of the phaseSample nearest `us` (default "good").
    // Used to tint a summary card's @end value by the swing's state at the window edge.
    Q_INVOKABLE QString bandAtNearest(const QVariantList &phaseSamples, qint64 us) const;
};
