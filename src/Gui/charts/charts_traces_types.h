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

// THE TRACES PRESENTER'S TYPED RESULTS (refactor 5.5c, lint A6): what ChartsTracesPresenter hands QML — choices,
// thisShot, reference, session, now, and what shotAt, hoverAt and lastTimings return — as Q_GADGET values. Each property
// is named as the old map's key, so the QML reads them as it read the maps; the shapes are the ones
// charts_traces_presenter.h documents (OUTPUT SHAPES).
//
// THE DIFFERENCES A GADGET MAKES, and how each keeps the page as it was:
//   · an object the map left out, and whose presence a reader tests (thisShot.window, a row's or a shot's chest
//     `estimate`, a panel's `alt`, a session row's `picked`, a reference row's `typicality`, the sequence's `checks`),
//     is a QVariant holding the gadget or null, so `x && x.y` reads as it did;
//   · a number the map left out and a reader tests for null (the session sequence's lo and hi) is a QVariant holding
//     the number or null;
//   · a field the map left out and every reader guards (an unavailable thisShot or session, a refused reference) is
//     present at its default; the defaults are chosen so a guard reads the same: hoverAt's shotMs is NaN, so an
//     invalid hover draws no crosshair;
//   · a list that was absent is an empty list: every reader takes `|| []` or a length.
//
// The rules the QML used to apply now live here, decided by the presenter: SessionTick.landmark (the session's impact
// tick is drawn strong and kept when crowded, by its phase — never by its tag's words) and Peak.trustSubject (the
// reading an unplaced peak's ring opens, never made in QML from a "ks." prefix).
//
// The types are in their own namespace: the session maths own ShotSequence, SessionSequence and their kin in
// pinpoint::charts, and Delivery's own words are in deliveryui. moc reaches this header through
// charts_traces_presenter.cpp's #include of its moc file. No QML registration: the gadgets reach QML as values.

#include "../trust/trust_types.h"                   // thisShot.trust: the readings a gate touched
#include "../variability/variability_facade.h"      // clubGrades[].result

#include <QList>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QtGlobal>

#include <limits>

namespace pinpoint::charts::tracesui {

inline QVariant nullValue() { return QVariant::fromValue(nullptr); }
inline constexpr double kNaNValue = std::numeric_limits<double>::quiet_NaN();

// ── words and axes ─────────────────────────────────────────────────────────────────────────────

// A run of a sentence: words, or a figure set in the figure face.
struct Run
{
    Q_GADGET
    Q_PROPERTY(QString text MEMBER text FINAL)
    Q_PROPERTY(bool    fig  MEMBER fig  FINAL)
public:
    QString text;
    bool    fig = false;
    friend bool operator==(const Run &, const Run &) = default;
};

// words::Sentence as QML takes it: its text, its runs and its first clause.
struct Sentence
{
    Q_GADGET
    Q_PROPERTY(QString text MEMBER text FINAL)
    Q_PROPERTY(QList<pinpoint::charts::tracesui::Run> runs MEMBER runs FINAL)
    Q_PROPERTY(QString clause MEMBER clause FINAL)
public:
    QString    text;
    QList<Run> runs;
    QString    clause;
    friend bool operator==(const Sentence &, const Sentence &) = default;
};

// TICK: a value and its figure text.
struct Tick
{
    Q_GADGET
    Q_PROPERTY(double  v    MEMBER v    FINAL)
    Q_PROPERTY(QString text MEMBER text FINAL)
public:
    double  v = 0;
    QString text;
    friend bool operator==(const Tick &, const Tick &) = default;
};

// BRIDGE: a dashed straight line across a run of samples that are not measurements.
struct Bridge
{
    Q_GADGET
    Q_PROPERTY(double t0 MEMBER t0 FINAL)
    Q_PROPERTY(double v0 MEMBER v0 FINAL)
    Q_PROPERTY(double t1 MEMBER t1 FINAL)
    Q_PROPERTY(double v1 MEMBER v1 FINAL)
public:
    double t0 = 0, v0 = 0, t1 = 0, v1 = 0;
    friend bool operator==(const Bridge &, const Bridge &) = default;
};

// An inclusive index range of a curve and how it is drawn: "measured" | "bridged" | "outside".
struct IndexRun
{
    Q_GADGET
    Q_PROPERTY(int     from  MEMBER from  FINAL)
    Q_PROPERTY(int     to    MEMBER to    FINAL)
    Q_PROPERTY(QString style MEMBER style FINAL)
public:
    int     from = 0, to = 0;
    QString style;
    friend bool operator==(const IndexRun &, const IndexRun &) = default;
};

// A stretch of time (a coverage hairline's measured run).
struct TimeRun
{
    Q_GADGET
    Q_PROPERTY(double t0 MEMBER t0 FINAL)
    Q_PROPERTY(double t1 MEMBER t1 FINAL)
public:
    double t0 = 0, t1 = 0;
    friend bool operator==(const TimeRun &, const TimeRun &) = default;
};

// A label and its figure text (a Session row's figures).
struct LabelText
{
    Q_GADGET
    Q_PROPERTY(QString label MEMBER label FINAL)
    Q_PROPERTY(QString text  MEMBER text  FINAL)
public:
    QString label, text;
    friend bool operator==(const LabelText &, const LabelText &) = default;
};

// ── choices ────────────────────────────────────────────────────────────────────────────────────

struct PresetChoice
{
    Q_GADGET
    Q_PROPERTY(QString id    MEMBER id    FINAL)
    Q_PROPERTY(QString label MEMBER label FINAL)
    Q_PROPERTY(int     count MEMBER count FINAL)
public:
    QString id, label;
    int     count = 0;
    friend bool operator==(const PresetChoice &, const PresetChoice &) = default;
};

struct MetricChoice
{
    Q_GADGET
    Q_PROPERTY(QString key   MEMBER key   FINAL)
    Q_PROPERTY(QString label MEMBER label FINAL)
    Q_PROPERTY(QString unit  MEMBER unit  FINAL)
    Q_PROPERTY(bool    on    MEMBER on    FINAL)
    Q_PROPERTY(bool    curve MEMBER curve FINAL)
public:
    QString key, label, unit;
    bool    on = false, curve = false;
    friend bool operator==(const MetricChoice &, const MetricChoice &) = default;
};

// A window chip. `segment`: an adjacent P-position pair, which the view lists under its own opener (rule C5).
struct WindowChoice
{
    Q_GADGET
    Q_PROPERTY(QString key     MEMBER key     FINAL)
    Q_PROPERTY(QString label   MEMBER label   FINAL)
    Q_PROPERTY(bool    current MEMBER current FINAL)
    Q_PROPERTY(bool    segment MEMBER segment FINAL)
    Q_PROPERTY(qint64  startUs MEMBER startUs FINAL)
    Q_PROPERTY(qint64  endUs   MEMBER endUs   FINAL)
    Q_PROPERTY(int     phaseA  MEMBER phaseA  FINAL)
    Q_PROPERTY(int     phaseB  MEMBER phaseB  FINAL)
public:
    QString key, label;
    bool    current = false, segment = false;
    qint64  startUs = 0, endUs = 0;
    int     phaseA = -1, phaseB = -1;
    friend bool operator==(const WindowChoice &, const WindowChoice &) = default;
};

// A band or an alignment: { id, label, current }.
struct OptionChoice
{
    Q_GADGET
    Q_PROPERTY(QString id      MEMBER id      FINAL)
    Q_PROPERTY(QString label   MEMBER label   FINAL)
    Q_PROPERTY(bool    current MEMBER current FINAL)
public:
    QString id, label;
    bool    current = false;
    friend bool operator==(const OptionChoice &, const OptionChoice &) = default;
};

struct FacetChoice
{
    Q_GADGET
    Q_PROPERTY(QString id      MEMBER id      FINAL)
    Q_PROPERTY(QString label   MEMBER label   FINAL)
    Q_PROPERTY(QString kind    MEMBER kind    FINAL)   // "none" | "condition" | "half" | "shape" | "club"
    Q_PROPERTY(int     n       MEMBER n       FINAL)
    Q_PROPERTY(bool    current MEMBER current FINAL)
public:
    QString id, label, kind;
    int     n = 0;
    bool    current = false;
    friend bool operator==(const FacetChoice &, const FacetChoice &) = default;
};

struct AgainstChoice
{
    Q_GADGET
    Q_PROPERTY(QString id      MEMBER id      FINAL)
    Q_PROPERTY(QString label   MEMBER label   FINAL)
    Q_PROPERTY(bool    current MEMBER current FINAL)
    Q_PROPERTY(bool    enabled MEMBER enabled FINAL)
    Q_PROPERTY(QString why     MEMBER why     FINAL)
public:
    QString id, label;
    bool    current = false, enabled = false;
    QString why;
    friend bool operator==(const AgainstChoice &, const AgainstChoice &) = default;
};

struct Choices
{
    Q_GADGET
    Q_PROPERTY(QString register MEMBER registerName FINAL)   // the member is not `register`: a C++ keyword
    Q_PROPERTY(QString view        MEMBER view        FINAL)
    Q_PROPERTY(QList<pinpoint::charts::tracesui::PresetChoice> presets MEMBER presets FINAL)
    Q_PROPERTY(QString preset      MEMBER preset      FINAL)
    Q_PROPERTY(QString presetTitle MEMBER presetTitle FINAL)
    Q_PROPERTY(bool    fromAddress MEMBER fromAddress FINAL)
    Q_PROPERTY(bool    custom      MEMBER custom      FINAL)
    Q_PROPERTY(QList<pinpoint::charts::tracesui::MetricChoice> metrics MEMBER metrics FINAL)
    Q_PROPERTY(QList<pinpoint::charts::tracesui::WindowChoice> windows MEMBER windows FINAL)
    Q_PROPERTY(QString window      MEMBER window      FINAL)
    Q_PROPERTY(QList<pinpoint::charts::tracesui::OptionChoice> bands MEMBER bands FINAL)
    Q_PROPERTY(QList<pinpoint::charts::tracesui::OptionChoice> aligns MEMBER aligns FINAL)
    Q_PROPERTY(QList<pinpoint::charts::tracesui::FacetChoice> facets MEMBER facets FINAL)
    Q_PROPERTY(QList<pinpoint::charts::tracesui::AgainstChoice> against MEMBER against FINAL)
    Q_PROPERTY(bool    difference  MEMBER difference  FINAL)
public:
    QString              registerName, view;
    QList<PresetChoice>  presets;
    QString              preset, presetTitle;
    bool                 fromAddress = false, custom = false;
    QList<MetricChoice>  metrics;
    QList<WindowChoice>  windows;
    QString              window;
    QList<OptionChoice>  bands, aligns;
    QList<FacetChoice>   facets;
    QList<AgainstChoice> against;
    bool                 difference = false;
    friend bool operator==(const Choices &, const Choices &) = default;
};

// ── this shot ──────────────────────────────────────────────────────────────────────────────────

// TILE: one reading of a card (window: PARTIAL speaks for it).
struct Tile
{
    Q_GADGET
    Q_PROPERTY(QString label       MEMBER label       FINAL)
    Q_PROPERTY(QString text        MEMBER text        FINAL)
    Q_PROPERTY(bool    ok          MEMBER ok          FINAL)
    Q_PROPERTY(QString verdict     MEMBER verdict     FINAL)
    Q_PROPERTY(QString verdictWord MEMBER verdictWord FINAL)
    Q_PROPERTY(QString sub         MEMBER sub         FINAL)
    Q_PROPERTY(QString unit        MEMBER unit        FINAL)
    Q_PROPERTY(QString tip         MEMBER tip         FINAL)
    Q_PROPERTY(bool    window      MEMBER window      FINAL)
public:
    QString label, text;
    bool    ok = false;
    QString verdict, verdictWord, sub, unit, tip;
    bool    window = false;
    friend bool operator==(const Tile &, const Tile &) = default;
};

// A peak on a curve. trustSubject: the reading its unplaced ring opens ("ks.<segment>"; "" off the sequence).
struct Peak
{
    Q_GADGET
    Q_PROPERTY(double  shotMs         MEMBER shotMs         FINAL)
    Q_PROPERTY(double  value          MEMBER value          FINAL)
    Q_PROPERTY(QString text           MEMBER text           FINAL)
    Q_PROPERTY(double  sigmaMs        MEMBER sigmaMs        FINAL)
    Q_PROPERTY(QString valueSigmaText MEMBER valueSigmaText FINAL)
    Q_PROPERTY(bool    placed         MEMBER placed         FINAL)
    Q_PROPERTY(bool    probable       MEMBER probable       FINAL)
    Q_PROPERTY(bool    late           MEMBER late           FINAL)
    Q_PROPERTY(QString lateWord       MEMBER lateWord       FINAL)
    Q_PROPERTY(bool    rising         MEMBER rising         FINAL)
    Q_PROPERTY(QString risingWord     MEMBER risingWord     FINAL)
    Q_PROPERTY(QString trustSubject   MEMBER trustSubject   FINAL)
public:
    double  shotMs = kNaNValue, value = kNaNValue;
    QString text;
    double  sigmaMs = kNaNValue;
    QString valueSigmaText;
    bool    placed = false, probable = false, late = false;
    QString lateWord;
    bool    rising = false;
    QString risingWord, trustSubject;
    friend bool operator==(const Peak &, const Peak &) = default;
};

// A P dot, measured only, with its verdict and its word.
struct Dot
{
    Q_GADGET
    Q_PROPERTY(int     phase       MEMBER phase       FINAL)
    Q_PROPERTY(QString tag         MEMBER tag         FINAL)
    Q_PROPERTY(double  shotMs      MEMBER shotMs      FINAL)
    Q_PROPERTY(double  value       MEMBER value       FINAL)
    Q_PROPERTY(QString verdict     MEMBER verdict     FINAL)
    Q_PROPERTY(QString verdictWord MEMBER verdictWord FINAL)
public:
    int     phase = -1;
    QString tag;
    double  shotMs = kNaNValue, value = kNaNValue;
    QString verdict, verdictWord;
    friend bool operator==(const Dot &, const Dot &) = default;
};

// A figure beside a curve, or one of This shot's key figures (lateWord: the key figures' late word).
struct Figure
{
    Q_GADGET
    Q_PROPERTY(QString label    MEMBER label    FINAL)
    Q_PROPERTY(QString text     MEMBER text     FINAL)
    Q_PROPERTY(bool    warn     MEMBER warn     FINAL)
    Q_PROPERTY(QString lateWord MEMBER lateWord FINAL)
public:
    QString label, text;
    bool    warn = false;
    QString lateWord;
    friend bool operator==(const Figure &, const Figure &) = default;
};

// A sequence curve's node (empty on any other curve).
struct RowSequence
{
    Q_GADGET
    Q_PROPERTY(QString segment   MEMBER segment   FINAL)
    Q_PROPERTY(QString word      MEMBER word      FINAL)
    Q_PROPERTY(bool    placed    MEMBER placed    FINAL)
    Q_PROPERTY(bool    probable  MEMBER probable  FINAL)
    Q_PROPERTY(int     rank      MEMBER rank      FINAL)
    Q_PROPERTY(QString rankText  MEMBER rankText  FINAL)
    Q_PROPERTY(bool    late      MEMBER late      FINAL)
    Q_PROPERTY(QString lateWord  MEMBER lateWord  FINAL)
    Q_PROPERTY(bool    rising    MEMBER rising    FINAL)
    Q_PROPERTY(QString peakText  MEMBER peakText  FINAL)
    Q_PROPERTY(QString speedText MEMBER speedText FINAL)
    Q_PROPERTY(QString pctText   MEMBER pctText   FINAL)
public:
    QString segment, word;
    bool    placed = false, probable = false;
    int     rank = 0;
    QString rankText;
    bool    late = false;
    QString lateWord;
    bool    rising = false;
    QString peakText, speedText, pctText;
    friend bool operator==(const RowSequence &, const RowSequence &) = default;
};

// The chest's fit across the camera's gap: t, v, and its ± 1 sd (lo, hi; empty where none is drawn).
struct Estimate
{
    Q_GADGET
    Q_PROPERTY(QList<double> t  MEMBER t  FINAL)
    Q_PROPERTY(QList<double> v  MEMBER v  FINAL)
    Q_PROPERTY(QList<double> lo MEMBER lo FINAL)
    Q_PROPERTY(QList<double> hi MEMBER hi FINAL)
public:
    QList<double> t, v, lo, hi;
    friend bool operator==(const Estimate &, const Estimate &) = default;
};

// A stretch the route could not see, or a gate's mask: a bar along the plot's foot with the gate's short words.
struct Blind
{
    Q_GADGET
    Q_PROPERTY(double  t0      MEMBER t0      FINAL)
    Q_PROPERTY(double  t1      MEMBER t1      FINAL)
    Q_PROPERTY(QString gate    MEMBER gate    FINAL)
    Q_PROPERTY(QString label   MEMBER label   FINAL)
    Q_PROPERTY(QString subject MEMBER subject FINAL)
public:
    double  t0 = 0, t1 = 0;
    QString gate, label, subject;
    friend bool operator==(const Blind &, const Blind &) = default;
};

// ROW and CARD, one type: a card is a row with no curve (its curve fields empty); a stub is a refused reading's card.
struct Row
{
    Q_GADGET
    // the card
    Q_PROPERTY(QString key         MEMBER key         FINAL)
    Q_PROPERTY(QString label       MEMBER label       FINAL)
    Q_PROPERTY(QString unit        MEMBER unit        FINAL)
    Q_PROPERTY(QList<pinpoint::charts::tracesui::Tile> tiles MEMBER tiles FINAL)
    Q_PROPERTY(bool    partial     MEMBER partial     FINAL)
    Q_PROPERTY(QString partialWord MEMBER partialWord FINAL)
    Q_PROPERTY(QString sigmaText   MEMBER sigmaText   FINAL)
    Q_PROPERTY(QString sigmaTip    MEMBER sigmaTip    FINAL)
    Q_PROPERTY(bool    grossWarn   MEMBER grossWarn   FINAL)
    Q_PROPERTY(QString grossWord   MEMBER grossWord   FINAL)
    Q_PROPERTY(QString grossTip    MEMBER grossTip    FINAL)
    Q_PROPERTY(bool    stub        MEMBER stub        FINAL)
    // the curve
    Q_PROPERTY(int     dec         MEMBER dec         FINAL)
    Q_PROPERTY(QString segment     MEMBER segment     FINAL)
    Q_PROPERTY(QList<double> t     MEMBER t           FINAL)
    Q_PROPERTY(QList<double> mean  MEMBER mean        FINAL)
    Q_PROPERTY(QList<double> raw   MEMBER raw         FINAL)
    Q_PROPERTY(QList<pinpoint::charts::tracesui::IndexRun> runs MEMBER runs FINAL)
    Q_PROPERTY(QList<pinpoint::charts::tracesui::Bridge> bridges MEMBER bridges FINAL)
    Q_PROPERTY(double  yLo         MEMBER yLo         FINAL)
    Q_PROPERTY(double  yHi         MEMBER yHi         FINAL)
    Q_PROPERTY(QList<pinpoint::charts::tracesui::Tick> yTicks MEMBER yTicks FINAL)
    Q_PROPERTY(QList<pinpoint::charts::tracesui::Peak> peaks MEMBER peaks FINAL)
    Q_PROPERTY(QList<pinpoint::charts::tracesui::Dot> dots MEMBER dots FINAL)
    Q_PROPERTY(QList<pinpoint::charts::tracesui::Figure> figures MEMBER figures FINAL)
    Q_PROPERTY(bool    windowEmpty MEMBER windowEmpty FINAL)
    Q_PROPERTY(pinpoint::charts::tracesui::RowSequence sequence MEMBER sequence FINAL)
    Q_PROPERTY(QVariant estimate   MEMBER estimate    FINAL)   // an Estimate, or null
    Q_PROPERTY(QList<pinpoint::charts::tracesui::Blind> blind MEMBER blind FINAL)
    Q_PROPERTY(QString trustSubject MEMBER trustSubject FINAL)
public:
    QString       key, label, unit;
    QList<Tile>   tiles;
    bool          partial = false;
    QString       partialWord, sigmaText, sigmaTip;
    bool          grossWarn = false;
    QString       grossWord, grossTip;
    bool          stub = false;
    int           dec = 0;
    QString       segment;
    QList<double> t, mean, raw;
    QList<IndexRun> runs;
    QList<Bridge> bridges;
    double        yLo = kNaNValue, yHi = kNaNValue;
    QList<Tick>   yTicks;
    QList<Peak>   peaks;
    QList<Dot>    dots;
    QList<Figure> figures;
    bool          windowEmpty = false;
    RowSequence   sequence;
    QVariant      estimate = nullValue();
    QList<Blind>  blind;
    QString       trustSubject;
    friend bool operator==(const Row &, const Row &) = default;
};

// One shared value axis for the overlay option (F6).
struct Overlay
{
    Q_GADGET
    Q_PROPERTY(double  yLo      MEMBER yLo      FINAL)
    Q_PROPERTY(double  yHi      MEMBER yHi      FINAL)
    Q_PROPERTY(QList<pinpoint::charts::tracesui::Tick> yTicks MEMBER yTicks FINAL)
    Q_PROPERTY(bool    sameUnit MEMBER sameUnit FINAL)
public:
    double      yLo = kNaNValue, yHi = kNaNValue;
    QList<Tick> yTicks;
    bool        sameUnit = false;
    friend bool operator==(const Overlay &, const Overlay &) = default;
};

struct DotKey
{
    Q_GADGET
    Q_PROPERTY(QString verdict MEMBER verdict FINAL)
    Q_PROPERTY(QString word    MEMBER word    FINAL)
public:
    QString verdict, word;
    friend bool operator==(const DotKey &, const DotKey &) = default;
};

// This shot's window and its header.
struct ShotWindow
{
    Q_GADGET
    Q_PROPERTY(QString key         MEMBER key         FINAL)
    Q_PROPERTY(QString label       MEMBER label       FINAL)
    Q_PROPERTY(QString header      MEMBER header      FINAL)
    Q_PROPERTY(QString spanText    MEMBER spanText    FINAL)
    Q_PROPERTY(QString metricsText MEMBER metricsText FINAL)
    Q_PROPERTY(QString tagsText    MEMBER tagsText    FINAL)
    Q_PROPERTY(qint64  startUs     MEMBER startUs     FINAL)
    Q_PROPERTY(qint64  endUs       MEMBER endUs       FINAL)
    Q_PROPERTY(double  t0Ms        MEMBER t0Ms        FINAL)
    Q_PROPERTY(double  t1Ms        MEMBER t1Ms        FINAL)
    Q_PROPERTY(int     phaseA      MEMBER phaseA      FINAL)
    Q_PROPERTY(int     phaseB      MEMBER phaseB      FINAL)
    Q_PROPERTY(bool    hasImpact   MEMBER hasImpact   FINAL)
public:
    QString key, label, header, spanText, metricsText, tagsText;
    qint64  startUs = 0, endUs = 0;
    double  t0Ms = 0, t1Ms = 0;
    int     phaseA = -1, phaseB = -1;
    bool    hasImpact = false;
    friend bool operator==(const ShotWindow &, const ShotWindow &) = default;
};

// A P tag inside the window, on the shot's clock (landmark: "top" | "impact" | "").
struct ShotTick
{
    Q_GADGET
    Q_PROPERTY(int     phase    MEMBER phase    FINAL)
    Q_PROPERTY(QString tag      MEMBER tag      FINAL)
    Q_PROPERTY(QString name     MEMBER name     FINAL)
    Q_PROPERTY(double  shotMs   MEMBER shotMs   FINAL)
    Q_PROPERTY(QString landmark MEMBER landmark FINAL)
public:
    int     phase = -1;
    QString tag, name;
    double  shotMs = 0;
    QString landmark;
    friend bool operator==(const ShotTick &, const ShotTick &) = default;
};

// A P tag on the whole recording's clock (the brush).
struct RecordingTick
{
    Q_GADGET
    Q_PROPERTY(int     phase    MEMBER phase    FINAL)
    Q_PROPERTY(QString tag      MEMBER tag      FINAL)
    Q_PROPERTY(qint64  us       MEMBER us       FINAL)
    Q_PROPERTY(QString landmark MEMBER landmark FINAL)
public:
    int     phase = -1;
    QString tag;
    qint64  us = 0;
    QString landmark;
    friend bool operator==(const RecordingTick &, const RecordingTick &) = default;
};

// A drawn metric over the whole recording (the brush's sparkline).
struct OverviewLine
{
    Q_GADGET
    Q_PROPERTY(QString key MEMBER key FINAL)
    Q_PROPERTY(QList<double> t MEMBER t FINAL)
    Q_PROPERTY(QList<double> v MEMBER v FINAL)
    Q_PROPERTY(double vLo MEMBER vLo FINAL)
    Q_PROPERTY(double vHi MEMBER vHi FINAL)
public:
    QString       key;
    QList<double> t, v;
    double        vLo = kNaNValue, vHi = kNaNValue;
    friend bool operator==(const OverviewLine &, const OverviewLine &) = default;
};

// One arrow of the sequence chain.
struct SeqLink
{
    Q_GADGET
    Q_PROPERTY(QString segment  MEMBER segment  FINAL)
    Q_PROPERTY(QString word     MEMBER word     FINAL)
    Q_PROPERTY(bool    late     MEMBER late     FINAL)
    Q_PROPERTY(QString lateWord MEMBER lateWord FINAL)
    Q_PROPERTY(bool    probable MEMBER probable FINAL)
    Q_PROPERTY(double  gapMs    MEMBER gapMs    FINAL)
    Q_PROPERTY(QString gapText  MEMBER gapText  FINAL)
public:
    QString segment, word;
    bool    late = false;
    QString lateWord;
    bool    probable = false;
    double  gapMs = kNaNValue;
    QString gapText;
    friend bool operator==(const SeqLink &, const SeqLink &) = default;
};

// One row of the per-segment peak table (ChartMetrics::sequenceTable's, with the timed segments' ranks and marks).
struct SeqTableRow
{
    Q_GADGET
    Q_PROPERTY(QString segment    MEMBER segment    FINAL)
    Q_PROPERTY(QString label      MEMBER label      FINAL)
    Q_PROPERTY(QString shouldRank MEMBER shouldRank FINAL)
    Q_PROPERTY(QString actualRank MEMBER actualRank FINAL)
    Q_PROPERTY(bool    placed     MEMBER placed     FINAL)
    Q_PROPERTY(bool    outOfTurn  MEMBER outOfTurn  FINAL)
    Q_PROPERTY(QString peakText   MEMBER peakText   FINAL)
    Q_PROPERTY(QString speedText  MEMBER speedText  FINAL)
    Q_PROPERTY(QString pctText    MEMBER pctText    FINAL)
    Q_PROPERTY(bool    late       MEMBER late       FINAL)
    Q_PROPERTY(QString lateWord   MEMBER lateWord   FINAL)
    Q_PROPERTY(bool    rising     MEMBER rising     FINAL)
    Q_PROPERTY(bool    probable   MEMBER probable   FINAL)
    Q_PROPERTY(QString warn       MEMBER warn       FINAL)
    Q_PROPERTY(QString warnShort  MEMBER warnShort  FINAL)
public:
    QString segment, label, shouldRank, actualRank;
    bool    placed = false, outOfTurn = false;
    QString peakText, speedText, pctText;
    bool    late = false;
    QString lateWord;
    bool    rising = false, probable = false;
    QString warn, warnShort;
    friend bool operator==(const SeqTableRow &, const SeqTableRow &) = default;
};

// One sequence check (Coaching/sequence_checks.h sequenceChecksToVariant's entry).
struct SeqCheck
{
    Q_GADGET
    Q_PROPERTY(QString facet        MEMBER facet        FINAL)
    Q_PROPERTY(QString signalId     MEMBER signalId     FINAL)
    Q_PROPERTY(QString label        MEMBER label        FINAL)
    Q_PROPERTY(QString question     MEMBER question     FINAL)
    Q_PROPERTY(QString state        MEMBER state        FINAL)
    Q_PROPERTY(QString mark         MEMBER mark         FINAL)
    Q_PROPERTY(double  pFire        MEMBER pFire        FINAL)
    Q_PROPERTY(bool    quantified   MEMBER quantified   FINAL)
    Q_PROPERTY(QString reason       MEMBER reason       FINAL)
    Q_PROPERTY(QString gate         MEMBER gate         FINAL)
    Q_PROPERTY(QString trustSubject MEMBER trustSubject FINAL)
    Q_PROPERTY(QString gateWords    MEMBER gateWords    FINAL)
    Q_PROPERTY(QString detail       MEMBER detail       FINAL)
public:
    QString facet, signalId, label, question, state, mark;
    double  pFire = 0;
    bool    quantified = false;
    QString reason, gate, trustSubject, gateWords, detail;
    friend bool operator==(const SeqCheck &, const SeqCheck &) = default;
};

// The shot's sequence checks, once recorded (sequence.checks is null when nothing was recorded).
struct SeqChecks
{
    Q_GADGET
    Q_PROPERTY(int     notAssessable  MEMBER notAssessable  FINAL)
    Q_PROPERTY(QString gapNote        MEMBER gapNote        FINAL)
    Q_PROPERTY(double  expectedPassed MEMBER expectedPassed FINAL)
    Q_PROPERTY(double  sumPFire       MEMBER sumPFire       FINAL)
    Q_PROPERTY(int     failedLikely   MEMBER failedLikely   FINAL)
    Q_PROPERTY(QList<pinpoint::charts::tracesui::SeqCheck> checks MEMBER checks FINAL)
public:
    int             notAssessable = 0;
    QString         gapNote;
    double          expectedPassed = 0, sumPFire = 0;
    int             failedLikely = 0;
    QList<SeqCheck> checks;
    friend bool operator==(const SeqChecks &, const SeqChecks &) = default;
};

// SEQ: this shot's kinematic sequence (empty when the preset carries no sequence curve or the shot no sequence).
struct SequenceBlock
{
    Q_GADGET
    Q_PROPERTY(QString verdictText    MEMBER verdictText    FINAL)
    Q_PROPERTY(QString verdictState   MEMBER verdictState   FINAL)
    Q_PROPERTY(bool    anyLate        MEMBER anyLate        FINAL)
    Q_PROPERTY(bool    anyProbable    MEMBER anyProbable    FINAL)
    Q_PROPERTY(pinpoint::charts::tracesui::Sentence chain MEMBER chain FINAL)
    Q_PROPERTY(QList<pinpoint::charts::tracesui::SeqLink> links MEMBER links FINAL)
    Q_PROPERTY(pinpoint::charts::tracesui::Sentence note MEMBER note FINAL)
    Q_PROPERTY(QString whyText        MEMBER whyText        FINAL)
    Q_PROPERTY(QString unrecordedText MEMBER unrecordedText FINAL)
    Q_PROPERTY(QList<pinpoint::charts::tracesui::SeqTableRow> table MEMBER table FINAL)
    Q_PROPERTY(QVariant checks        MEMBER checks         FINAL)   // a SeqChecks, or null
public:
    QString            verdictText, verdictState;
    bool               anyLate = false, anyProbable = false;
    Sentence           chain;
    QList<SeqLink>     links;
    Sentence           note;
    QString            whyText, unrecordedText;
    QList<SeqTableRow> table;
    QVariant           checks = nullValue();
    friend bool operator==(const SequenceBlock &, const SequenceBlock &) = default;
};

struct ThisShot
{
    Q_GADGET
    Q_PROPERTY(int      sessionCount MEMBER sessionCount FINAL)
    Q_PROPERTY(bool     available    MEMBER available    FINAL)
    Q_PROPERTY(pinpoint::charts::tracesui::Sentence empty MEMBER empty FINAL)
    Q_PROPERTY(int      ordinal      MEMBER ordinal      FINAL)
    Q_PROPERTY(int      shotId       MEMBER shotId       FINAL)
    Q_PROPERTY(QString  swingDir     MEMBER swingDir     FINAL)
    Q_PROPERTY(bool     inSession    MEMBER inSession    FINAL)
    Q_PROPERTY(QString  kicker       MEMBER kicker       FINAL)
    Q_PROPERTY(QList<pinpoint::charts::tracesui::Row> rows MEMBER rows FINAL)
    Q_PROPERTY(QList<pinpoint::charts::tracesui::Row> cards MEMBER cards FINAL)
    Q_PROPERTY(pinpoint::charts::tracesui::Overlay overlay MEMBER overlay FINAL)
    Q_PROPERTY(pinpoint::charts::tracesui::Sentence noTraces MEMBER noTraces FINAL)
    Q_PROPERTY(QList<pinpoint::charts::tracesui::DotKey> dotKey MEMBER dotKey FINAL)
    Q_PROPERTY(QVariant window       MEMBER window       FINAL)   // a ShotWindow; null with no shot
    Q_PROPERTY(QList<pinpoint::charts::tracesui::ShotTick> ticks MEMBER ticks FINAL)
    Q_PROPERTY(QList<pinpoint::charts::tracesui::Tick> msTicks MEMBER msTicks FINAL)
    Q_PROPERTY(QList<pinpoint::charts::tracesui::RecordingTick> recordingTicks MEMBER recordingTicks FINAL)
    Q_PROPERTY(QList<pinpoint::charts::tracesui::OverviewLine> overview MEMBER overview FINAL)
    Q_PROPERTY(pinpoint::charts::tracesui::SequenceBlock sequence MEMBER sequence FINAL)
    Q_PROPERTY(QList<pinpoint::trust::TrustReading> trust MEMBER trust FINAL)
    Q_PROPERTY(pinpoint::charts::tracesui::Sentence head MEMBER head FINAL)
    Q_PROPERTY(pinpoint::charts::tracesui::Sentence note MEMBER note FINAL)
    Q_PROPERTY(pinpoint::charts::tracesui::Sentence standfirst MEMBER standfirst FINAL)
    Q_PROPERTY(QList<pinpoint::charts::tracesui::Figure> figures MEMBER figures FINAL)
public:
    int                  sessionCount = 0;
    bool                 available = false;
    Sentence             empty;
    int                  ordinal = 0, shotId = -1;
    QString              swingDir;
    bool                 inSession = false;
    QString              kicker;
    QList<Row>           rows, cards;
    Overlay              overlay;
    Sentence             noTraces;
    QList<DotKey>        dotKey;
    QVariant             window = nullValue();
    QList<ShotTick>      ticks;
    QList<Tick>          msTicks;
    QList<RecordingTick> recordingTicks;
    QList<OverviewLine>  overview;
    SequenceBlock        sequence;
    QList<pinpoint::trust::TrustReading> trust;
    Sentence             head, note, standfirst;
    QList<Figure>        figures;
    friend bool operator==(const ThisShot &, const ThisShot &) = default;
};

// ── against ────────────────────────────────────────────────────────────────────────────────────

// The largest gap from the reference (found: there is one).
struct Gap
{
    Q_GADGET
    Q_PROPERTY(bool    found  MEMBER found  FINAL)
    Q_PROPERTY(double  shotMs MEMBER shotMs FINAL)
    Q_PROPERTY(double  diff   MEMBER diff   FINAL)
    Q_PROPERTY(QString text   MEMBER text   FINAL)
    Q_PROPERTY(QString when   MEMBER when   FINAL)
public:
    bool    found = false;
    double  shotMs = kNaNValue, diff = kNaNValue;
    QString text, when;
    friend bool operator==(const Gap &, const Gap &) = default;
};

struct TypicalityCorridor
{
    Q_GADGET
    Q_PROPERTY(double loT MEMBER loT FINAL)
    Q_PROPERTY(double hiT MEMBER hiT FINAL)
public:
    double loT = kNaNValue, hiT = kNaNValue;
    friend bool operator==(const TypicalityCorridor &, const TypicalityCorridor &) = default;
};

// The TypicalityTrace mark's per-instance values (variability_facade.h typicalityTracePayload).
struct TypicalityValues
{
    Q_GADGET
    Q_PROPERTY(QList<double> mean  MEMBER mean  FINAL)
    Q_PROPERTY(QList<double> sd    MEMBER sd    FINAL)
    Q_PROPERTY(QList<double> swing MEMBER swing FINAL)
    Q_PROPERTY(QVariant corridor   MEMBER corridor FINAL)   // a TypicalityCorridor, or null
    Q_PROPERTY(QString band        MEMBER band  FINAL)
public:
    QList<double> mean, sd, swing;
    QVariant      corridor = nullValue();
    QString       band;
    friend bool operator==(const TypicalityValues &, const TypicalityValues &) = default;
};

struct Typicality
{
    Q_GADGET
    Q_PROPERTY(pinpoint::charts::tracesui::TypicalityValues values MEMBER values FINAL)
    Q_PROPERTY(QString basisText MEMBER basisText FINAL)
public:
    TypicalityValues values;
    QString          basisText;
    friend bool operator==(const Typicality &, const Typicality &) = default;
};

// The reference on THIS shot's clock, for one drawn curve.
struct RefRow
{
    Q_GADGET
    Q_PROPERTY(QString key MEMBER key FINAL)
    Q_PROPERTY(QList<double> t    MEMBER t    FINAL)
    Q_PROPERTY(QList<double> v    MEMBER v    FINAL)
    Q_PROPERTY(QList<double> diff MEMBER diff FINAL)
    Q_PROPERTY(pinpoint::charts::tracesui::Gap gap MEMBER gap FINAL)
    Q_PROPERTY(QList<pinpoint::charts::tracesui::Bridge> bridges MEMBER bridges FINAL)
    Q_PROPERTY(double diffLo MEMBER diffLo FINAL)
    Q_PROPERTY(double diffHi MEMBER diffHi FINAL)
    Q_PROPERTY(QVariant typicality MEMBER typicality FINAL)   // a Typicality, or null
public:
    QString       key;
    QList<double> t, v, diff;
    Gap           gap;
    QList<Bridge> bridges;
    double        diffLo = kNaNValue, diffHi = kNaNValue;
    QVariant      typicality = nullValue();
    friend bool operator==(const RefRow &, const RefRow &) = default;
};

struct Reference
{
    Q_GADGET
    Q_PROPERTY(QString  mode       MEMBER mode       FINAL)
    Q_PROPERTY(bool     difference MEMBER difference FINAL)
    Q_PROPERTY(bool     active     MEMBER active     FINAL)
    Q_PROPERTY(bool     refused    MEMBER refused    FINAL)
    Q_PROPERTY(pinpoint::charts::tracesui::Sentence why MEMBER why FINAL)
    Q_PROPERTY(QString  label      MEMBER label      FINAL)
    Q_PROPERTY(int      n          MEMBER n          FINAL)
    Q_PROPERTY(QList<int> group    MEMBER group      FINAL)
    Q_PROPERTY(int      ordinal    MEMBER ordinal    FINAL)
    Q_PROPERTY(int      shotId     MEMBER shotId     FINAL)
    Q_PROPERTY(QString  swingDir   MEMBER swingDir   FINAL)
    Q_PROPERTY(QString  refLabel   MEMBER refLabel   FINAL)
    Q_PROPERTY(QString  diffLabel  MEMBER diffLabel  FINAL)
    Q_PROPERTY(QList<pinpoint::charts::tracesui::RefRow> rows MEMBER rows FINAL)
    Q_PROPERTY(pinpoint::charts::tracesui::Sentence head MEMBER head FINAL)
public:
    QString       mode;
    bool          difference = false, active = false, refused = false;
    Sentence      why;
    QString       label;
    int           n = 0;
    QList<int>    group;
    int           ordinal = 0, shotId = -1;
    QString       swingDir, refLabel, diffLabel;
    QList<RefRow> rows;
    Sentence      head;
    friend bool operator==(const Reference &, const Reference &) = default;
};

// ── session ────────────────────────────────────────────────────────────────────────────────────

// A canonical P tag with the middle half of when it happened (landmark: "top" | "impact" | "", the session's rule
// for the strong impact tick).
struct SessionTick
{
    Q_GADGET
    Q_PROPERTY(int     phase    MEMBER phase    FINAL)
    Q_PROPERTY(QString tag      MEMBER tag      FINAL)
    Q_PROPERTY(double  canonMs  MEMBER canonMs  FINAL)
    Q_PROPERTY(double  q1Ms     MEMBER q1Ms     FINAL)
    Q_PROPERTY(double  q3Ms     MEMBER q3Ms     FINAL)
    Q_PROPERTY(int     n        MEMBER n        FINAL)
    Q_PROPERTY(QString when     MEMBER when     FINAL)
    Q_PROPERTY(QString landmark MEMBER landmark FINAL)
public:
    int     phase = -1;
    QString tag;
    double  canonMs = 0, q1Ms = 0, q3Ms = 0;
    int     n = 0;
    QString when, landmark;
    friend bool operator==(const SessionTick &, const SessionTick &) = default;
};

// A facet's group. `short` is the property: the member is not, a C++ keyword.
struct GroupInfo
{
    Q_GADGET
    Q_PROPERTY(QString key   MEMBER key        FINAL)
    Q_PROPERTY(QString label MEMBER label      FINAL)
    Q_PROPERTY(QString short MEMBER shortLabel FINAL)
    Q_PROPERTY(int     n     MEMBER n          FINAL)
    Q_PROPERTY(QList<int> ordinals MEMBER ordinals FINAL)
    Q_PROPERTY(bool    own   MEMBER own        FINAL)
public:
    QString    key, label, shortLabel;
    int        n = 0;
    QList<int> ordinals;
    bool       own = false;
    friend bool operator==(const GroupInfo &, const GroupInfo &) = default;
};

// The group fit (band "fit": ± 1 sd band, D2); refused with why for a group under three.
struct Fit
{
    Q_GADGET
    Q_PROPERTY(bool    refused MEMBER refused FINAL)
    Q_PROPERTY(QString label   MEMBER label   FINAL)
    Q_PROPERTY(QString why     MEMBER why     FINAL)
    Q_PROPERTY(QList<double> v      MEMBER v      FINAL)
    Q_PROPERTY(QList<double> lo     MEMBER lo     FINAL)
    Q_PROPERTY(QList<double> hi     MEMBER hi     FINAL)
    Q_PROPERTY(QList<double> nShots MEMBER nShots FINAL)
public:
    bool          refused = false;
    QString       label, why;
    QList<double> v, lo, hi, nShots;
    friend bool operator==(const Fit &, const Fit &) = default;
};

// The other group's median or fit, drawn dashed over this one's.
struct Alt
{
    Q_GADGET
    Q_PROPERTY(QList<double> v MEMBER v FINAL)
    Q_PROPERTY(QString label  MEMBER label  FINAL)
    Q_PROPERTY(bool    dashed MEMBER dashed FINAL)
public:
    QList<double> v;
    QString       label;
    bool          dashed = false;
    friend bool operator==(const Alt &, const Alt &) = default;
};

// Another shot on the canonical grid (band "every").
struct Other
{
    Q_GADGET
    Q_PROPERTY(int     ordinal  MEMBER ordinal  FINAL)
    Q_PROPERTY(int     shotId   MEMBER shotId   FINAL)
    Q_PROPERTY(QString swingDir MEMBER swingDir FINAL)
    Q_PROPERTY(QList<double> v  MEMBER v        FINAL)
    Q_PROPERTY(QList<pinpoint::charts::tracesui::Bridge> bridges MEMBER bridges FINAL)
    Q_PROPERTY(QVariant estimate MEMBER estimate FINAL)   // an Estimate (t, v), or null
public:
    int           ordinal = 0, shotId = -1;
    QString       swingDir;
    QList<double> v;
    QList<Bridge> bridges;
    QVariant      estimate = nullValue();
    friend bool operator==(const Other &, const Other &) = default;
};

// One facet group's panel of a Session row.
struct Panel
{
    Q_GADGET
    Q_PROPERTY(QString group MEMBER group FINAL)
    Q_PROPERTY(QString label MEMBER label FINAL)
    Q_PROPERTY(int     n     MEMBER n     FINAL)
    Q_PROPERTY(QList<double> t MEMBER t FINAL)
    Q_PROPERTY(pinpoint::charts::tracesui::Fit fit MEMBER fit FINAL)
    Q_PROPERTY(QVariant alt  MEMBER alt  FINAL)   // an Alt, or null
    Q_PROPERTY(QList<double> q1     MEMBER q1     FINAL)
    Q_PROPERTY(QList<double> med    MEMBER med    FINAL)
    Q_PROPERTY(QList<double> q3     MEMBER q3     FINAL)
    Q_PROPERTY(QList<double> medEst MEMBER medEst FINAL)
    Q_PROPERTY(QList<pinpoint::charts::tracesui::Other> others MEMBER others FINAL)
public:
    QString       group, label;
    int           n = 0;
    QList<double> t;
    Fit           fit;
    QVariant      alt = nullValue();
    QList<double> q1, med, q3, medEst;
    QList<Other>  others;
    friend bool operator==(const Panel &, const Panel &) = default;
};

// This shot on the canonical axis, full strength.
struct Picked
{
    Q_GADGET
    Q_PROPERTY(int     ordinal  MEMBER ordinal  FINAL)
    Q_PROPERTY(int     shotId   MEMBER shotId   FINAL)
    Q_PROPERTY(QString swingDir MEMBER swingDir FINAL)
    Q_PROPERTY(QList<double> t  MEMBER t        FINAL)
    Q_PROPERTY(QList<double> v  MEMBER v        FINAL)
    Q_PROPERTY(QList<pinpoint::charts::tracesui::IndexRun> runs MEMBER runs FINAL)
    Q_PROPERTY(QList<pinpoint::charts::tracesui::Bridge> bridges MEMBER bridges FINAL)
    Q_PROPERTY(QVariant estimate MEMBER estimate FINAL)   // an Estimate, or null
public:
    int             ordinal = 0, shotId = -1;
    QString         swingDir;
    QList<double>   t, v;
    QList<IndexRun> runs;
    QList<Bridge>   bridges;
    QVariant        estimate = nullValue();
    friend bool operator==(const Picked &, const Picked &) = default;
};

// One shot's coverage hairline (band "fit").
struct Coverage
{
    Q_GADGET
    Q_PROPERTY(int     ordinal  MEMBER ordinal  FINAL)
    Q_PROPERTY(int     shotId   MEMBER shotId   FINAL)
    Q_PROPERTY(QString swingDir MEMBER swingDir FINAL)
    Q_PROPERTY(bool    picked   MEMBER picked   FINAL)
    Q_PROPERTY(QList<pinpoint::charts::tracesui::TimeRun> runs MEMBER runs FINAL)
public:
    int            ordinal = 0, shotId = -1;
    QString        swingDir;
    bool           picked = false;
    QList<TimeRun> runs;
    friend bool operator==(const Coverage &, const Coverage &) = default;
};

// Where the curves part.
struct Part
{
    Q_GADGET
    Q_PROPERTY(bool    found   MEMBER found   FINAL)
    Q_PROPERTY(double  canonMs MEMBER canonMs FINAL)
    Q_PROPERTY(double  q1      MEMBER q1      FINAL)
    Q_PROPERTY(double  q3      MEMBER q3      FINAL)
    Q_PROPERTY(double  med     MEMBER med     FINAL)
    Q_PROPERTY(QString when    MEMBER when    FINAL)
public:
    bool    found = false;
    double  canonMs = kNaNValue, q1 = kNaNValue, q3 = kNaNValue, med = kNaNValue;
    QString when;
    friend bool operator==(const Part &, const Part &) = default;
};

// One club's Overlay grade of a Session row (R2b).
struct ClubGrade
{
    Q_GADGET
    Q_PROPERTY(QString club     MEMBER club     FINAL)
    Q_PROPERTY(int     readings MEMBER readings FINAL)
    Q_PROPERTY(pinpoint::variability::VariabilityResultView result MEMBER result FINAL)
public:
    QString club;
    int     readings = 0;
    pinpoint::variability::VariabilityResultView result;
    friend bool operator==(const ClubGrade &, const ClubGrade &) = default;
};

struct SessionRow
{
    Q_GADGET
    Q_PROPERTY(QString key   MEMBER key   FINAL)
    Q_PROPERTY(QString label MEMBER label FINAL)
    Q_PROPERTY(QString unit  MEMBER unit  FINAL)
    Q_PROPERTY(int     dec   MEMBER dec   FINAL)
    Q_PROPERTY(double  yLo   MEMBER yLo   FINAL)
    Q_PROPERTY(double  yHi   MEMBER yHi   FINAL)
    Q_PROPERTY(QList<pinpoint::charts::tracesui::Tick> yTicks MEMBER yTicks FINAL)
    Q_PROPERTY(QList<pinpoint::charts::tracesui::Panel> panels MEMBER panels FINAL)
    Q_PROPERTY(QVariant picked MEMBER picked FINAL)   // a Picked, or null
    Q_PROPERTY(QList<pinpoint::charts::tracesui::Coverage> coverage MEMBER coverage FINAL)
    Q_PROPERTY(pinpoint::charts::tracesui::Part part MEMBER part FINAL)
    Q_PROPERTY(QList<pinpoint::charts::tracesui::LabelText> figures MEMBER figures FINAL)
    Q_PROPERTY(QList<pinpoint::charts::tracesui::ClubGrade> clubGrades MEMBER clubGrades FINAL)
public:
    QString          key, label, unit;
    int              dec = 0;
    double           yLo = kNaNValue, yHi = kNaNValue;
    QList<Tick>      yTicks;
    QList<Panel>     panels;
    QVariant         picked = nullValue();
    QList<Coverage>  coverage;
    Part             part;
    QList<LabelText> figures;
    QList<ClubGrade> clubGrades;
    friend bool operator==(const SessionRow &, const SessionRow &) = default;
};

// What the trust fold says about the chest's probable peak (kind: "chestProbable" | "chestEarlier").
struct TraceTrustLine
{
    Q_GADGET
    Q_PROPERTY(QString kind MEMBER kind FINAL)
    Q_PROPERTY(QString text MEMBER text FINAL)
public:
    QString kind, text;
    friend bool operator==(const TraceTrustLine &, const TraceTrustLine &) = default;
};

struct SeqPTick
{
    Q_GADGET
    Q_PROPERTY(QString tag MEMBER tag FINAL)
    Q_PROPERTY(double  ms  MEMBER ms  FINAL)
public:
    QString tag;
    double  ms = 0;
    friend bool operator==(const SeqPTick &, const SeqPTick &) = default;
};

// One shot's rank of one segment (the order barcode).
struct SeqCell
{
    Q_GADGET
    Q_PROPERTY(int     ordinal  MEMBER ordinal  FINAL)
    Q_PROPERTY(int     shotId   MEMBER shotId   FINAL)
    Q_PROPERTY(QString swingDir MEMBER swingDir FINAL)
    Q_PROPERTY(int     rank     MEMBER rank     FINAL)
    Q_PROPERTY(QString text     MEMBER text     FINAL)
    Q_PROPERTY(bool    late     MEMBER late     FINAL)
    Q_PROPERTY(bool    probable MEMBER probable FINAL)
    Q_PROPERTY(bool    picked   MEMBER picked   FINAL)
public:
    int     ordinal = 0, shotId = -1;
    QString swingDir;
    int     rank = 0;
    QString text;
    bool    late = false, probable = false, picked = false;
    friend bool operator==(const SeqCell &, const SeqCell &) = default;
};

// One shot's peak time of one segment (the dot plot).
struct SeqPoint
{
    Q_GADGET
    Q_PROPERTY(int     ordinal  MEMBER ordinal  FINAL)
    Q_PROPERTY(int     shotId   MEMBER shotId   FINAL)
    Q_PROPERTY(QString swingDir MEMBER swingDir FINAL)
    Q_PROPERTY(double  ms       MEMBER ms       FINAL)
    Q_PROPERTY(bool    late     MEMBER late     FINAL)
    Q_PROPERTY(bool    probable MEMBER probable FINAL)
    Q_PROPERTY(double  sigmaMs  MEMBER sigmaMs  FINAL)
    Q_PROPERTY(bool    picked   MEMBER picked   FINAL)
public:
    int     ordinal = 0, shotId = -1;
    QString swingDir;
    double  ms = 0;
    bool    late = false, probable = false;
    double  sigmaMs = kNaNValue;
    bool    picked = false;
    friend bool operator==(const SeqPoint &, const SeqPoint &) = default;
};

struct SeqSegment
{
    Q_GADGET
    Q_PROPERTY(QString segment   MEMBER segment   FINAL)
    Q_PROPERTY(QString word      MEMBER word      FINAL)
    Q_PROPERTY(bool    never     MEMBER never     FINAL)
    Q_PROPERTY(QList<pinpoint::charts::tracesui::SeqCell> cells MEMBER cells FINAL)
    Q_PROPERTY(QList<pinpoint::charts::tracesui::SeqPoint> points MEMBER points FINAL)
    Q_PROPERTY(double  median    MEMBER median    FINAL)
    Q_PROPERTY(double  q1        MEMBER q1        FINAL)
    Q_PROPERTY(double  q3        MEMBER q3        FINAL)
    Q_PROPERTY(int     placed    MEMBER placed    FINAL)
    Q_PROPERTY(int     probable  MEMBER probable  FINAL)
    Q_PROPERTY(QString warn      MEMBER warn      FINAL)
    Q_PROPERTY(QString warnShort MEMBER warnShort FINAL)
    Q_PROPERTY(QString figText   MEMBER figText   FINAL)
public:
    QString         segment, word;
    bool            never = false;
    QList<SeqCell>  cells;
    QList<SeqPoint> points;
    double          median = kNaNValue, q1 = kNaNValue, q3 = kNaNValue;
    int             placed = 0, probable = 0;
    QString         warn, warnShort, figText;
    friend bool operator==(const SeqSegment &, const SeqSegment &) = default;
};

// The common order's grade: VariabilityResultView::toMap()'s fields, its basisText naming the kept segments.
struct OrderResult
{
    Q_GADGET
    Q_PROPERTY(int      grade              MEMBER grade              FINAL)
    Q_PROPERTY(bool     graded             MEMBER graded             FINAL)
    Q_PROPERTY(QString  basis              MEMBER basis              FINAL)
    Q_PROPERTY(QString  ungradedReason     MEMBER ungradedReason     FINAL)
    Q_PROPERTY(bool     measurementLimited MEMBER measurementLimited FINAL)
    Q_PROPERTY(int      n                  MEMBER n                  FINAL)
    Q_PROPERTY(bool     hasRho             MEMBER hasRho             FINAL)
    Q_PROPERTY(QVariant rho                MEMBER rho                FINAL)   // null when there is none
    Q_PROPERTY(QString  basisText          MEMBER basisText          FINAL)
    Q_PROPERTY(QString  domain             MEMBER domain             FINAL)
public:
    int      grade = 0;
    bool     graded = false;
    QString  basis, ungradedReason;
    bool     measurementLimited = false;
    int      n = 0;
    bool     hasRho = false;
    QVariant rho = nullValue();
    QString  basisText, domain;
    friend bool operator==(const OrderResult &, const OrderResult &) = default;
};

struct OrderGrade
{
    Q_GADGET
    Q_PROPERTY(QString     club     MEMBER club     FINAL)
    Q_PROPERTY(int         readings MEMBER readings FINAL)
    Q_PROPERTY(int         k        MEMBER k        FINAL)
    Q_PROPERTY(QStringList segments MEMBER segments FINAL)
    Q_PROPERTY(pinpoint::charts::tracesui::OrderResult result MEMBER result FINAL)
public:
    QString     club;
    int         readings = 0, k = 0;
    QStringList segments;
    OrderResult result;
    friend bool operator==(const OrderGrade &, const OrderGrade &) = default;
};

// The sequence across the session (present: drawn; lo and hi are null until it is).
struct SessionSequenceBlock
{
    Q_GADGET
    Q_PROPERTY(bool     present MEMBER present FINAL)
    Q_PROPERTY(QVariant lo      MEMBER lo      FINAL)
    Q_PROPERTY(QVariant hi      MEMBER hi      FINAL)
    Q_PROPERTY(QList<pinpoint::charts::tracesui::Tick> ticks MEMBER ticks FINAL)
    Q_PROPERTY(QList<pinpoint::charts::tracesui::SeqPTick> pTicks MEMBER pTicks FINAL)
    Q_PROPERTY(QList<pinpoint::charts::tracesui::SeqSegment> segments MEMBER segments FINAL)
    Q_PROPERTY(pinpoint::charts::tracesui::Sentence stand MEMBER stand FINAL)
    Q_PROPERTY(QList<pinpoint::charts::tracesui::OrderGrade> orderGrades MEMBER orderGrades FINAL)
public:
    bool              present = false;
    QVariant          lo = nullValue(), hi = nullValue();
    QList<Tick>       ticks;
    QList<SeqPTick>   pTicks;
    QList<SeqSegment> segments;
    Sentence          stand;
    QList<OrderGrade> orderGrades;
    friend bool operator==(const SessionSequenceBlock &, const SessionSequenceBlock &) = default;
};

// A data-backed question, answered from the data.
struct Question
{
    Q_GADGET
    Q_PROPERTY(QString id MEMBER id FINAL)
    Q_PROPERTY(QString text MEMBER text FINAL)
    Q_PROPERTY(pinpoint::charts::tracesui::Sentence answer MEMBER answer FINAL)
    Q_PROPERTY(QList<int> lit MEMBER lit FINAL)
    Q_PROPERTY(QString setFacet MEMBER setFacet FINAL)
    Q_PROPERTY(QString setBand  MEMBER setBand  FINAL)
public:
    QString    id, text;
    Sentence   answer;
    QList<int> lit;
    QString    setFacet, setBand;
    friend bool operator==(const Question &, const Question &) = default;
};

struct Session
{
    Q_GADGET
    Q_PROPERTY(int      n           MEMBER n           FINAL)
    Q_PROPERTY(int      aligned     MEMBER aligned     FINAL)
    Q_PROPERTY(bool     available   MEMBER available   FINAL)
    Q_PROPERTY(pinpoint::charts::tracesui::Sentence why MEMBER why FINAL)
    // the tab row's count: the session's shots when Session would draw, else 0 (the tab then shows none)
    Q_PROPERTY(int      tabCount    MEMBER tabCount    FINAL)
    Q_PROPERTY(QString  kicker      MEMBER kicker      FINAL)
    Q_PROPERTY(QString  axisCaption MEMBER axisCaption FINAL)
    Q_PROPERTY(double   t0          MEMBER t0          FINAL)
    Q_PROPERTY(double   t1          MEMBER t1          FINAL)
    Q_PROPERTY(pinpoint::charts::tracesui::Sentence alignNote MEMBER alignNote FINAL)
    Q_PROPERTY(pinpoint::charts::tracesui::Sentence clubNote MEMBER clubNote FINAL)
    Q_PROPERTY(QList<pinpoint::charts::tracesui::SessionTick> ticks MEMBER ticks FINAL)
    Q_PROPERTY(QList<pinpoint::charts::tracesui::Tick> msTicks MEMBER msTicks FINAL)
    Q_PROPERTY(QString  band        MEMBER band        FINAL)
    Q_PROPERTY(QString  facet       MEMBER facet       FINAL)
    Q_PROPERTY(QList<pinpoint::charts::tracesui::GroupInfo> groups MEMBER groups FINAL)
    Q_PROPERTY(QList<pinpoint::charts::tracesui::SessionRow> rows MEMBER rows FINAL)
    Q_PROPERTY(pinpoint::charts::tracesui::Overlay overlay MEMBER overlay FINAL)
    Q_PROPERTY(QString  alignedAt   MEMBER alignedAt   FINAL)
    Q_PROPERTY(QList<pinpoint::charts::tracesui::TraceTrustLine> trustLines MEMBER trustLines FINAL)
    Q_PROPERTY(pinpoint::charts::tracesui::Sentence head MEMBER head FINAL)
    Q_PROPERTY(pinpoint::charts::tracesui::Sentence standfirst MEMBER standfirst FINAL)
    Q_PROPERTY(pinpoint::charts::tracesui::Sentence caveat MEMBER caveat FINAL)
    Q_PROPERTY(pinpoint::charts::tracesui::SessionSequenceBlock sequence MEMBER sequence FINAL)
    Q_PROPERTY(QList<pinpoint::charts::tracesui::Question> questions MEMBER questions FINAL)
public:
    int                   n = 0, aligned = 0;
    bool                  available = false;
    Sentence              why;
    int                   tabCount = 0;
    QString               kicker, axisCaption;
    double                t0 = 0, t1 = 0;
    Sentence              alignNote, clubNote;
    QList<SessionTick>    ticks;
    QList<Tick>           msTicks;
    QString               band, facet;
    QList<GroupInfo>      groups;
    QList<SessionRow>     rows;
    Overlay               overlay;
    QString               alignedAt;
    QList<TraceTrustLine> trustLines;
    Sentence              head, standfirst, caveat;
    SessionSequenceBlock  sequence;
    QList<Question>       questions;
    friend bool operator==(const Session &, const Session &) = default;
};

// ── the playhead and the pointer ───────────────────────────────────────────────────────────────

// A row's readings at the playhead ("-" where not measured).
struct NowRow
{
    Q_GADGET
    Q_PROPERTY(QString key         MEMBER key         FINAL)
    Q_PROPERTY(double  value       MEMBER value       FINAL)
    Q_PROPERTY(QString text        MEMBER text        FINAL)
    Q_PROPERTY(bool    measured    MEMBER measured    FINAL)
    Q_PROPERTY(QString addrText    MEMBER addrText    FINAL)
    Q_PROPERTY(double  typical     MEMBER typical     FINAL)
    Q_PROPERTY(QString typicalText MEMBER typicalText FINAL)
    Q_PROPERTY(QString refText     MEMBER refText     FINAL)
    Q_PROPERTY(QString gapText     MEMBER gapText     FINAL)
public:
    QString key;
    double  value = kNaNValue;
    QString text;
    bool    measured = false;
    QString addrText;
    double  typical = kNaNValue;
    QString typicalText, refText, gapText;
    friend bool operator==(const NowRow &, const NowRow &) = default;
};

// The readings at the playhead. `sig` is what was printed, the throttle's test (never a defaulted ==: NaN members
// compare unequal).
struct Now
{
    Q_GADGET
    Q_PROPERTY(bool    valid   MEMBER valid   FINAL)
    Q_PROPERTY(qint64  us      MEMBER us      FINAL)
    Q_PROPERTY(double  shotMs  MEMBER shotMs  FINAL)
    Q_PROPERTY(double  canonMs MEMBER canonMs FINAL)
    Q_PROPERTY(QString when    MEMBER when    FINAL)
    Q_PROPERTY(QList<pinpoint::charts::tracesui::NowRow> rows MEMBER rows FINAL)
    Q_PROPERTY(QString sig     MEMBER sig     FINAL)
public:
    bool          valid = false;
    qint64        us = -1;
    double        shotMs = kNaNValue, canonMs = kNaNValue;
    QString       when;
    QList<NowRow> rows;
    QString       sig;
};

// One row of the hover readout.
struct HoverRow
{
    Q_GADGET
    Q_PROPERTY(QString key       MEMBER key       FINAL)
    Q_PROPERTY(QString label     MEMBER label     FINAL)
    Q_PROPERTY(QString text      MEMBER text      FINAL)
    Q_PROPERTY(QString raw       MEMBER raw       FINAL)
    Q_PROPERTY(bool    measured  MEMBER measured  FINAL)
    Q_PROPERTY(bool    grossWarn MEMBER grossWarn FINAL)
    Q_PROPERTY(QString trust     MEMBER trust     FINAL)
public:
    QString key, label, text, raw;
    bool    measured = false, grossWarn = false;
    QString trust;
    friend bool operator==(const HoverRow &, const HoverRow &) = default;
};

// hoverAt's readout. shotMs is NaN when not valid, so the figure draws no crosshair.
struct Hover
{
    Q_GADGET
    Q_PROPERTY(bool    valid  MEMBER valid  FINAL)
    Q_PROPERTY(double  shotMs MEMBER shotMs FINAL)
    Q_PROPERTY(QString when   MEMBER when   FINAL)
    Q_PROPERTY(QList<pinpoint::charts::tracesui::HoverRow> rows MEMBER rows FINAL)
public:
    bool            valid = false;
    double          shotMs = kNaNValue;
    QString         when;
    QList<HoverRow> rows;
    friend bool operator==(const Hover &, const Hover &) = default;
};

// shotAt's answer: the shot whose aligned curve passes nearest.
struct ShotHit
{
    Q_GADGET
    Q_PROPERTY(bool    found    MEMBER found    FINAL)
    Q_PROPERTY(int     ordinal  MEMBER ordinal  FINAL)
    Q_PROPERTY(int     shotId   MEMBER shotId   FINAL)
    Q_PROPERTY(QString swingDir MEMBER swingDir FINAL)
    Q_PROPERTY(double  value    MEMBER value    FINAL)
public:
    bool    found = false;
    int     ordinal = 0, shotId = -1;
    QString swingDir;
    double  value = kNaNValue;
    friend bool operator==(const ShotHit &, const ShotHit &) = default;
};

// The last job's cost (a diagnostic).
struct Timings
{
    Q_GADGET
    Q_PROPERTY(double  jobMs       MEMBER jobMs       FINAL)
    Q_PROPERTY(double  thisShotMs  MEMBER thisShotMs  FINAL)
    Q_PROPERTY(double  referenceMs MEMBER referenceMs FINAL)
    Q_PROPERTY(double  sessionMs   MEMBER sessionMs   FINAL)
    Q_PROPERTY(bool    cacheHit    MEMBER cacheHit    FINAL)
    Q_PROPERTY(quint64 revision    MEMBER revision    FINAL)
public:
    double  jobMs = 0, thisShotMs = 0, referenceMs = 0, sessionMs = 0;
    bool    cacheHit = false;
    quint64 revision = 0;
    friend bool operator==(const Timings &, const Timings &) = default;
};

} // namespace pinpoint::charts::tracesui
