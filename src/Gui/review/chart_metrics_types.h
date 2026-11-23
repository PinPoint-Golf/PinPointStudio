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

// THE CHART METRICS' TYPED RESULTS (refactor 5.8a, lint A6): what ChartMetrics hands QML, as Q_GADGET values. Each
// property is named as the old map's key, so the QML reads them as it read the maps; the shapes are the ones
// chart_metrics.h documents beside each method. Every map these replace carried every key on every return, so a
// gadget (every property present, at its default) reads the same; the one exception is a segment's `swing`, which
// was present only on the swing segment and is false on the others — its one reader asks `=== true`.
//
// ChartMetrics keeps its map-shaped methods for its C++ callers (the Charts traces presenter, chart_metrics_test):
// the typed methods QML calls convert their typed lists to those, call them, and read the map back into the gadget,
// so a QML reading and a C++ reading of the same series are one computation.
//
// moc reaches this header through chart_metrics.cpp's #include of its moc file.

#include <QList>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QtGlobal>

namespace pinpoint::chartui {

// segments(): one window chip. [0] the whole recording (phaseA = phaseB = −1), [1] the swing (`swing` true) when the
// swing has one, then each adjacent P-position pair.
struct Segment
{
    Q_GADGET
    Q_PROPERTY(qint64 startUs MEMBER startUs FINAL)
    Q_PROPERTY(qint64 endUs   MEMBER endUs   FINAL)
    Q_PROPERTY(int    phaseA  MEMBER phaseA  FINAL)
    Q_PROPERTY(int    phaseB  MEMBER phaseB  FINAL)
    Q_PROPERTY(bool   swing   MEMBER swing   FINAL)
public:
    qint64 startUs = 0;
    qint64 endUs   = 0;
    int    phaseA  = -1;
    int    phaseB  = -1;
    bool   swing   = false;
    friend bool operator==(const Segment &, const Segment &) = default;
};

// dataWindow(): the P-positions that enclose the drawn data inside a window.
struct DataWindow
{
    Q_GADGET
    Q_PROPERTY(qint64 startUs MEMBER startUs FINAL)
    Q_PROPERTY(qint64 endUs   MEMBER endUs   FINAL)
    Q_PROPERTY(int    phaseA  MEMBER phaseA  FINAL)
    Q_PROPERTY(int    phaseB  MEMBER phaseB  FINAL)
    Q_PROPERTY(bool   trimmed MEMBER trimmed FINAL)
public:
    qint64 startUs = 0;
    qint64 endUs   = 0;
    int    phaseA  = -1;
    int    phaseB  = -1;
    bool   trimmed = false;
    friend bool operator==(const DataWindow &, const DataWindow &) = default;
};

// summaryMasked(): the per-window statistics (chart_metrics.h says what each one is, and when it may be shown).
struct Summary
{
    Q_GADGET
    Q_PROPERTY(double start      MEMBER start      FINAL)
    Q_PROPERTY(double end        MEMBER end        FINAL)
    Q_PROPERTY(double min        MEMBER min        FINAL)
    Q_PROPERTY(double max        MEMBER max        FINAL)
    Q_PROPERTY(double peak       MEMBER peak       FINAL)
    Q_PROPERTY(double range      MEMBER range      FINAL)
    Q_PROPERTY(double delta      MEMBER delta      FINAL)
    Q_PROPERTY(double rate       MEMBER rate       FINAL)
    Q_PROPERTY(qint64 tPeakUs    MEMBER tPeakUs    FINAL)
    Q_PROPERTY(bool   partial    MEMBER partial    FINAL)
    Q_PROPERTY(double peakSigma  MEMBER peakSigma  FINAL)
    Q_PROPERTY(double rateSigma  MEMBER rateSigma  FINAL)
    Q_PROPERTY(bool   edgeOk     MEMBER edgeOk     FINAL)
    Q_PROPERTY(bool   extremumOk MEMBER extremumOk FINAL)
    Q_PROPERTY(bool   rateOk     MEMBER rateOk     FINAL)
    Q_PROPERTY(qint64 tRateUs    MEMBER tRateUs    FINAL)
public:
    double start = 0, end = 0, min = 0, max = 0, peak = 0, range = 0, delta = 0, rate = 0;
    qint64 tPeakUs = 0;
    bool   partial = false;
    double peakSigma = 0, rateSigma = 0;
    bool   edgeOk = false, extremumOk = false, rateOk = false;
    qint64 tRateUs = 0;
    friend bool operator==(const Summary &, const Summary &) = default;
};

// fittedCurveAt(): a fitted curve's reading at an instant.
struct Reading
{
    Q_GADGET
    Q_PROPERTY(bool   ok    MEMBER ok    FINAL)
    Q_PROPERTY(double value MEMBER value FINAL)
public:
    bool   ok    = false;
    double value = 0;
    friend bool operator==(const Reading &, const Reading &) = default;
};

// windowedMean(): the drawn line and its σ, both as long as the curve.
struct WindowedMean
{
    Q_GADGET
    Q_PROPERTY(QList<double> mean  MEMBER mean  FINAL)
    Q_PROPERTY(QList<double> sigma MEMBER sigma FINAL)
public:
    QList<double> mean;
    QList<double> sigma;
    friend bool operator==(const WindowedMean &, const WindowedMean &) = default;
};

// domainFor(): where a metric's geometry means something (Phase enum values).
struct Domain
{
    Q_GADGET
    Q_PROPERTY(int  firstPhase    MEMBER firstPhase    FINAL)
    Q_PROPERTY(int  lastPhase     MEMBER lastPhase     FINAL)
    Q_PROPERTY(bool firstNarrowed MEMBER firstNarrowed FINAL)
    Q_PROPERTY(bool lastNarrowed  MEMBER lastNarrowed  FINAL)
    Q_PROPERTY(bool narrowed      MEMBER narrowed      FINAL)
public:
    int  firstPhase = 0;
    int  lastPhase  = 0;
    bool firstNarrowed = false;
    bool lastNarrowed  = false;
    bool narrowed      = false;
    friend bool operator==(const Domain &, const Domain &) = default;
};

// cardSpecFor()'s readAt entries: a reading at a phase, with the label the spec gives it ("" = the phase's own).
struct CardReading
{
    Q_GADGET
    Q_PROPERTY(int     phase MEMBER phase FINAL)
    Q_PROPERTY(QString label MEMBER label FINAL)
public:
    int     phase = 0;
    QString label;
    friend bool operator==(const CardReading &, const CardReading &) = default;
};

// cardSpecFor(): what the summary card shows for a metric (MetricDescriptor::card).
struct CardSpec
{
    Q_GADGET
    Q_PROPERTY(QList<pinpoint::chartui::CardReading> readAt MEMBER readAt FINAL)
    Q_PROPERTY(bool    peak         MEMBER peak         FINAL)
    Q_PROPERTY(bool    peakSpan     MEMBER peakSpan     FINAL)
    Q_PROPERTY(int     peakFrom     MEMBER peakFrom     FINAL)
    Q_PROPERTY(int     peakTo       MEMBER peakTo       FINAL)
    Q_PROPERTY(bool    delta        MEMBER delta        FINAL)
    Q_PROPERTY(bool    deltaSpan    MEMBER deltaSpan    FINAL)
    Q_PROPERTY(int     deltaFrom    MEMBER deltaFrom    FINAL)
    Q_PROPERTY(int     deltaTo      MEMBER deltaTo      FINAL)
    Q_PROPERTY(QString deltaLabel   MEMBER deltaLabel   FINAL)
    Q_PROPERTY(bool    rate         MEMBER rate         FINAL)
    Q_PROPERTY(bool    windowedMean MEMBER windowedMean FINAL)
    Q_PROPERTY(QString mergeInto    MEMBER mergeInto    FINAL)
public:
    QList<CardReading> readAt;
    bool    peak = false, peakSpan = false;
    int     peakFrom = 0, peakTo = 0;
    bool    delta = false, deltaSpan = false;
    int     deltaFrom = 0, deltaTo = 0;
    QString deltaLabel;
    bool    rate = false, windowedMean = false;
    QString mergeInto;
    friend bool operator==(const CardSpec &, const CardSpec &) = default;
};

// sigmaInfo(): what backs the σ chip, and the gross-error caveat.
struct SigmaInfo
{
    Q_GADGET
    Q_PROPERTY(QString tip     MEMBER tip     FINAL)
    Q_PROPERTY(bool    warn    MEMBER warn    FINAL)
    Q_PROPERTY(QString warnTip MEMBER warnTip FINAL)
public:
    QString tip;
    bool    warn = false;
    QString warnTip;
    friend bool operator==(const SigmaInfo &, const SigmaInfo &) = default;
};

// seriesGroups(): one preset — a catalogue group, a cross-cutting preset, or "Other" — and its keys.
struct SeriesGroup
{
    Q_GADGET
    Q_PROPERTY(QString     group MEMBER group FINAL)
    Q_PROPERTY(QStringList keys  MEMBER keys  FINAL)
public:
    QString     group;
    QStringList keys;
    friend bool operator==(const SeriesGroup &, const SeriesGroup &) = default;
};

} // namespace pinpoint::chartui
