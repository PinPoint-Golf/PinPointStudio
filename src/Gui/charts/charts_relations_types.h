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

// THE RELATIONS PRESENTER'S TYPED RESULTS (refactor 5.5c, lint A6): what ChartsRelationsPresenter hands QML — status,
// families, thisShot, openRow, sequence, relations, kinds, replaySession, playhead and ticks() — as Q_GADGET values, and
// the READING ROW the Consistency tab shares (ConsistencyReadingRows). Each property is named as the old map's key, so
// the QML reads them as it read the maps; the shapes are the ones charts_relations_presenter.h documents (THE OUTPUTS).
// A key that is a C++ keyword (`short`) keeps its name in QML and has another member name (`shortText`).
//
// THE DIFFERENCES A GADGET MAKES, and how each keeps the page as it was:
//   · a missing number was null: a single number is a QVariant holding null (never an invalid QVariant, which reads
//     `undefined`); a per-shot or per-sample list of numbers is a list of doubles, NaN where it had null. Every reader
//     of those lists takes null and NaN alike (PpChBoxCell.valueOf, PpChPlot's and PpTypeset's fin());
//   · an object that was null or absent (a trace, a line, a typicality badge, a facet, a partner, withinClub, a kind
//     curve's window) is a QVariant holding the gadget or null;
//   · a field the map left out is present at its default: "", 0, false, an empty list or an empty Sentence (which
//     PpChRoles.has() takes as no sentence). A default that would read differently is set to what the absent key read
//     as (status.pickedImpactUs −1, so `>= 0` stays false before the first result).
//
// THE READING ROW IS ONE TYPE for every list that holds one: This shot's apart, groups and familyRows, and the
// Consistency tab's opened cell and picked shot. The Consistency fields (`graded`, `soft`, `softWord`) are at their
// defaults on a Charts row, which every reader takes as "not set". Its `variability` is a QVariant holding a
// RelationsVariability or null, tagged by `kind` ("typicality" | "consistency").
//
// The rules the QML used to apply that now live here: RelationsKindPhase.impact (the kinds' curve marks impact by its
// P-position, never by its tag's spelling) and RelationsThisShot.apartLabel (the Apart fold's "2 readings", singular and
// plural). Sentence.clause is the sentence's first clause (words::firstClauseOf).
//
// No QML registration, as charts_delivery_types.h: moc reaches this header through charts_relations_presenter.cpp's
// #include of its moc file.

#include "../variability/variability_facade.h"     // VariabilityResultView: a consistency row's grade

#include <QList>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QtGlobal>

namespace pinpoint::charts {

inline QVariant relationsNull() { return QVariant::fromValue(nullptr); }

// ── words ──────────────────────────────────────────────────────────────────────────────────────

// A run of a sentence: words, or a figure set in the figure face.
struct RelationsWordRun
{
    Q_GADGET
    Q_PROPERTY(QString text MEMBER text FINAL)
    Q_PROPERTY(bool    fig  MEMBER fig  FINAL)
public:
    QString text;
    bool    fig = false;
    friend bool operator==(const RelationsWordRun &, const RelationsWordRun &) = default;
};

// words::Sentence as QML takes it: its text, its runs and its first clause. No runs: no sentence.
struct RelationsSentence
{
    Q_GADGET
    Q_PROPERTY(QString                                   text   MEMBER text   FINAL)
    Q_PROPERTY(QList<pinpoint::charts::RelationsWordRun> runs   MEMBER runs   FINAL)
    Q_PROPERTY(QString                                   clause MEMBER clause FINAL)
public:
    QString                 text;
    QList<RelationsWordRun> runs;
    QString                 clause;
    friend bool operator==(const RelationsSentence &, const RelationsSentence &) = default;
};

// ── scales and marks ───────────────────────────────────────────────────────────────────────────

// { lo, hi }: a scale's ends, null where a reading has none.
struct RelationsRange
{
    Q_GADGET
    Q_PROPERTY(QVariant lo MEMBER lo FINAL)
    Q_PROPERTY(QVariant hi MEMBER hi FINAL)
public:
    QVariant lo = relationsNull(), hi = relationsNull();
    friend bool operator==(const RelationsRange &, const RelationsRange &) = default;
};

// { t0, t1 }: a stretch of a clock, in ms.
struct RelationsWindow
{
    Q_GADGET
    Q_PROPERTY(double t0 MEMBER t0 FINAL)
    Q_PROPERTY(double t1 MEMBER t1 FINAL)
public:
    double t0 = 0, t1 = 0;
    friend bool operator==(const RelationsWindow &, const RelationsWindow &) = default;
};

// A fitted or drift line, end to end.
struct RelationsLine
{
    Q_GADGET
    Q_PROPERTY(double x0 MEMBER x0 FINAL)
    Q_PROPERTY(double y0 MEMBER y0 FINAL)
    Q_PROPERTY(double x1 MEMBER x1 FINAL)
    Q_PROPERTY(double y1 MEMBER y1 FINAL)
public:
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    friend bool operator==(const RelationsLine &, const RelationsLine &) = default;
};

// The numbers a box plot lays on itself while it is hovered, as text: a box's own `texts` (lo … hi), and a reading
// row's `boxText` (the same, with the corridor's edges and fault lines where one is drawn). "" where there is none,
// which PpBoxPlot takes as no label.
struct RelationsBoxTexts
{
    Q_GADGET
    Q_PROPERTY(QString lo       MEMBER lo       FINAL)
    Q_PROPERTY(QString q1       MEMBER q1       FINAL)
    Q_PROPERTY(QString med      MEMBER med      FINAL)
    Q_PROPERTY(QString q3       MEMBER q3       FINAL)
    Q_PROPERTY(QString hi       MEMBER hi       FINAL)
    Q_PROPERTY(QString signalLo MEMBER signalLo FINAL)
    Q_PROPERTY(QString signalHi MEMBER signalHi FINAL)
    Q_PROPERTY(QString faultLo  MEMBER faultLo  FINAL)
    Q_PROPERTY(QString faultHi  MEMBER faultHi  FINAL)
public:
    QString lo, q1, med, q3, hi, signalLo, signalHi, faultLo, faultHi;
    friend bool operator==(const RelationsBoxTexts &, const RelationsBoxTexts &) = default;
};

// A box (box_map.h's shape): its numbers null when n = 0; outliers by ordinal.
struct RelationsBox
{
    Q_GADGET
    Q_PROPERTY(int                                  n        MEMBER n        FINAL)
    Q_PROPERTY(QVariant                             min      MEMBER min      FINAL)
    Q_PROPERTY(QVariant                             max      MEMBER max      FINAL)
    Q_PROPERTY(QVariant                             q1       MEMBER q1       FINAL)
    Q_PROPERTY(QVariant                             med      MEMBER med      FINAL)
    Q_PROPERTY(QVariant                             q3       MEMBER q3       FINAL)
    Q_PROPERTY(QVariant                             lo       MEMBER lo       FINAL)
    Q_PROPERTY(QVariant                             hi       MEMBER hi       FINAL)
    Q_PROPERTY(QList<int>                           outliers MEMBER outliers FINAL)
    Q_PROPERTY(pinpoint::charts::RelationsBoxTexts  texts    MEMBER texts    FINAL)
public:
    int        n = 0;
    QVariant   min = relationsNull(), max = relationsNull(), q1 = relationsNull(), med = relationsNull(), q3 = relationsNull();
    QVariant   lo = relationsNull(), hi = relationsNull();
    QList<int> outliers;
    RelationsBoxTexts texts;
    friend bool operator==(const RelationsBox &, const RelationsBox &) = default;
};

// One of ticks(): a value on an axis and its text.
struct RelationsTick
{
    Q_GADGET
    Q_PROPERTY(double  v    MEMBER v    FINAL)
    Q_PROPERTY(QString text MEMBER text FINAL)
public:
    double  v = 0;
    QString text;
    friend bool operator==(const RelationsTick &, const RelationsTick &) = default;
};

// A shot of the session, strike order (status.shots): an ordinal → a carousel request.
struct RelationsShot
{
    Q_GADGET
    Q_PROPERTY(int     ordinal  MEMBER ordinal  FINAL)
    Q_PROPERTY(int     shotId   MEMBER shotId   FINAL)
    Q_PROPERTY(QString swingDir MEMBER swingDir FINAL)
    Q_PROPERTY(QString club     MEMBER club     FINAL)
public:
    int     ordinal = 0;
    int     shotId = -1;
    QString swingDir;
    QString club;
    friend bool operator==(const RelationsShot &, const RelationsShot &) = default;
};

// A SHOT MARK in a group (a kind's members): the shot, and whether it is the picked one.
struct RelationsMember
{
    Q_GADGET
    Q_PROPERTY(int     ordinal  MEMBER ordinal  FINAL)
    Q_PROPERTY(int     shotId   MEMBER shotId   FINAL)
    Q_PROPERTY(QString swingDir MEMBER swingDir FINAL)
    Q_PROPERTY(bool    picked   MEMBER picked   FINAL)
public:
    int     ordinal = 0;
    int     shotId = -1;
    QString swingDir;
    bool    picked = false;
    friend bool operator==(const RelationsMember &, const RelationsMember &) = default;
};

// ── the reading row ────────────────────────────────────────────────────────────────────────────

// The norm's bands a condition grades the reading against, as Session diagnostics draws them. `known` false: none,
// and nothing else is read.
struct RelationsCorridor
{
    Q_GADGET
    Q_PROPERTY(bool   known    MEMBER known    FINAL)
    Q_PROPERTY(bool   lowOpen  MEMBER lowOpen  FINAL)
    Q_PROPERTY(bool   highOpen MEMBER highOpen FINAL)
    Q_PROPERTY(double signalLo MEMBER signalLo FINAL)
    Q_PROPERTY(double signalHi MEMBER signalHi FINAL)
    Q_PROPERTY(double faultLo  MEMBER faultLo  FINAL)
    Q_PROPERTY(double faultHi  MEMBER faultHi  FINAL)
public:
    bool   known = false, lowOpen = false, highOpen = false;
    double signalLo = 0, signalHi = 0, faultLo = 0, faultHi = 0;
    friend bool operator==(const RelationsCorridor &, const RelationsCorridor &) = default;
};

// A condition that fired on this shot at the row's reading, and how it read it.
struct RelationsFiredCondition
{
    Q_GADGET
    Q_PROPERTY(QString id      MEMBER id      FINAL)
    Q_PROPERTY(QString name    MEMBER name    FINAL)
    Q_PROPERTY(int     reducer MEMBER reducer FINAL)
    Q_PROPERTY(QString instant MEMBER instant FINAL)
    Q_PROPERTY(QString text    MEMBER text    FINAL)
public:
    QString id, name;
    int     reducer = -1;
    QString instant, text;
    friend bool operator==(const RelationsFiredCondition &, const RelationsFiredCondition &) = default;
};

// PpVariabilityMark's Typicality `values` (its component page, "Consumer supplies"): { z, ghosts: [{ swingId, z }],
// corridor: { loZ, hiZ } | null, band }, as variability_facade.h's typicalityPayload spells them.
struct RelationsTypicalityGhost
{
    Q_GADGET
    Q_PROPERTY(QString swingId MEMBER swingId FINAL)
    Q_PROPERTY(double  z       MEMBER z       FINAL)
public:
    QString swingId;
    double  z = 0;
    friend bool operator==(const RelationsTypicalityGhost &, const RelationsTypicalityGhost &) = default;
};

struct RelationsTypicalityCorridor
{
    Q_GADGET
    Q_PROPERTY(double loZ MEMBER loZ FINAL)
    Q_PROPERTY(double hiZ MEMBER hiZ FINAL)
public:
    double loZ = 0, hiZ = 0;
    friend bool operator==(const RelationsTypicalityCorridor &, const RelationsTypicalityCorridor &) = default;
};

struct RelationsTypicalityValues
{
    Q_GADGET
    Q_PROPERTY(double                                           z        MEMBER z        FINAL)
    Q_PROPERTY(QList<pinpoint::charts::RelationsTypicalityGhost> ghosts  MEMBER ghosts   FINAL)
    Q_PROPERTY(QVariant                                         corridor MEMBER corridor FINAL)   // RelationsTypicalityCorridor | null
    Q_PROPERTY(QString                                          band     MEMBER band     FINAL)
public:
    double                          z = 0;
    QList<RelationsTypicalityGhost> ghosts;
    QVariant                        corridor = relationsNull();
    QString                         band;
    friend bool operator==(const RelationsTypicalityValues &, const RelationsTypicalityValues &) = default;
};

// A row's (or a key figure's, or Replay's "now") variability badge: `kind` "typicality" (this shot against the usual,
// R8: `values`, `basisText`, `showCorridor`) or "consistency" (an opened Consistency cell's reading: `result`, its
// grade). The other kind's fields stay at their defaults.
struct RelationsVariability
{
    Q_GADGET
    Q_PROPERTY(QString                                      kind         MEMBER kind         FINAL)
    Q_PROPERTY(pinpoint::charts::RelationsTypicalityValues  values       MEMBER values       FINAL)
    Q_PROPERTY(QString                                      basisText    MEMBER basisText    FINAL)
    Q_PROPERTY(bool                                         showCorridor MEMBER showCorridor FINAL)
    Q_PROPERTY(pinpoint::variability::VariabilityResultView result       MEMBER result       FINAL)
public:
    QString                   kind;
    RelationsTypicalityValues values;
    QString                   basisText;
    bool                      showCorridor = false;
    pinpoint::variability::VariabilityResultView result;
    friend bool operator==(const RelationsVariability &, const RelationsVariability &) = default;
};

// THE READING ROW (charts_relations_presenter.h, CONVENTIONS): one reading on this shot against its session. `values`
// and `firedShots` are one per shot in strike order, NaN where unmeasured. `variability` is a RelationsVariability or
// null. `graded`, `soft` and `softWord` are the Consistency tab's opened cell's; a Charts row leaves them at false/"".
struct RelationsReadingRow
{
    Q_GADGET
    Q_PROPERTY(bool                                           caution         MEMBER caution         FINAL)
    Q_PROPERTY(QString                                        cautionText     MEMBER cautionText     FINAL)
    Q_PROPERTY(QString                                        trustSubject    MEMBER trustSubject    FINAL)
    Q_PROPERTY(QString                                        gateWords       MEMBER gateWords       FINAL)
    Q_PROPERTY(QString                                        readingId       MEMBER readingId       FINAL)
    Q_PROPERTY(QString                                        key             MEMBER key             FINAL)
    Q_PROPERTY(int                                            phase           MEMBER phase           FINAL)
    Q_PROPERTY(QString                                        tag             MEMBER tag             FINAL)
    Q_PROPERTY(QString                                        phaseName       MEMBER phaseName       FINAL)
    Q_PROPERTY(QString                                        label           MEMBER label           FINAL)
    Q_PROPERTY(QString                                        short           MEMBER shortText       FINAL)
    Q_PROPERTY(QString                                        unit            MEMBER unit            FINAL)
    Q_PROPERTY(QString                                        unitText        MEMBER unitText        FINAL)
    Q_PROPERTY(QString                                        group           MEMBER group           FINAL)
    Q_PROPERTY(bool                                           multi           MEMBER multi           FINAL)
    Q_PROPERTY(bool                                           measured        MEMBER measured        FINAL)
    Q_PROPERTY(QVariant                                       value           MEMBER value           FINAL)
    Q_PROPERTY(QString                                        valueText       MEMBER valueText       FINAL)
    Q_PROPERTY(QString                                        valueFigure     MEMBER valueFigure     FINAL)
    Q_PROPERTY(QString                                        medianText      MEMBER medianText      FINAL)
    Q_PROPERTY(QString                                        place           MEMBER place           FINAL)
    Q_PROPERTY(int                                            rankHi          MEMBER rankHi          FINAL)
    Q_PROPERTY(int                                            rankLo          MEMBER rankLo          FINAL)
    Q_PROPERTY(double                                         rz              MEMBER rz              FINAL)
    Q_PROPERTY(int                                            side            MEMBER side            FINAL)
    Q_PROPERTY(bool                                           apart           MEMBER apart           FINAL)
    Q_PROPERTY(pinpoint::charts::RelationsBox                 box             MEMBER box             FINAL)
    Q_PROPERTY(QList<double>                                  values          MEMBER values          FINAL)
    Q_PROPERTY(QList<bool>                                    firedShots      MEMBER firedShots      FINAL)
    Q_PROPERTY(bool                                           fired           MEMBER fired           FINAL)
    Q_PROPERTY(QString                                        firedWord       MEMBER firedWord       FINAL)
    Q_PROPERTY(QString                                        verdict         MEMBER verdict         FINAL)
    Q_PROPERTY(QStringList                                    verdicts        MEMBER verdicts        FINAL)
    Q_PROPERTY(QStringList                                    conditions      MEMBER conditions      FINAL)
    Q_PROPERTY(QList<pinpoint::charts::RelationsFiredCondition> firedConditions MEMBER firedConditions FINAL)
    Q_PROPERTY(pinpoint::charts::RelationsCorridor            corridor        MEMBER corridor        FINAL)
    Q_PROPERTY(pinpoint::charts::RelationsBoxTexts            boxText         MEMBER boxText         FINAL)
    Q_PROPERTY(bool                                           hasCurve        MEMBER hasCurve        FINAL)
    Q_PROPERTY(QString                                        opens           MEMBER opens           FINAL)
    Q_PROPERTY(QVariant                                       variability     MEMBER variability     FINAL)   // RelationsVariability | null
    Q_PROPERTY(bool                                           graded          MEMBER graded          FINAL)
    Q_PROPERTY(bool                                           soft            MEMBER soft            FINAL)
    Q_PROPERTY(QString                                        softWord        MEMBER softWord        FINAL)
public:
    bool        caution = false;
    QString     cautionText, trustSubject, gateWords;
    QString     readingId, key;
    int         phase = 0;
    QString     tag, phaseName, label, shortText, unit, unitText, group;
    bool        multi = false;
    bool        measured = false;
    QVariant    value = relationsNull();
    QString     valueText, valueFigure, medianText, place;
    int         rankHi = 0, rankLo = 0;
    double      rz = 0;
    int         side = 0;
    bool        apart = false;
    RelationsBox  box;
    QList<double> values;
    QList<bool>   firedShots;
    bool        fired = false;
    QString     firedWord, verdict;
    QStringList verdicts, conditions;
    QList<RelationsFiredCondition> firedConditions;
    RelationsCorridor corridor;
    RelationsBoxTexts boxText;
    bool        hasCurve = false;
    QString     opens;
    QVariant    variability = relationsNull();
    bool        graded = false;
    bool        soft = false;
    QString     softWord;
    friend bool operator==(const RelationsReadingRow &, const RelationsReadingRow &) = default;
};

// A family's rows under its name (thisShot.groups; the Consistency tab's picked shot, by ball, club and swing).
struct RelationsRowGroup
{
    Q_GADGET
    Q_PROPERTY(QString                                       name MEMBER name FINAL)
    Q_PROPERTY(QList<pinpoint::charts::RelationsReadingRow>  rows MEMBER rows FINAL)
public:
    QString                    name;
    QList<RelationsReadingRow> rows;
    friend bool operator==(const RelationsRowGroup &, const RelationsRowGroup &) = default;
};

// ── a RUN, a TRACE, a SCATTER ──────────────────────────────────────────────────────────────────

// One shot of a run: `value` null where it was not measured.
struct RelationsRunPoint
{
    Q_GADGET
    Q_PROPERTY(int      ordinal  MEMBER ordinal  FINAL)
    Q_PROPERTY(int      shotId   MEMBER shotId   FINAL)
    Q_PROPERTY(QString  swingDir MEMBER swingDir FINAL)
    Q_PROPERTY(QVariant value    MEMBER value    FINAL)
    Q_PROPERTY(bool     measured MEMBER measured FINAL)
    Q_PROPERTY(bool     fired    MEMBER fired    FINAL)
    Q_PROPERTY(bool     picked   MEMBER picked   FINAL)
public:
    int      ordinal = 0;
    int      shotId = -1;
    QString  swingDir;
    QVariant value = relationsNull();
    bool     measured = false, fired = false, picked = false;
    friend bool operator==(const RelationsRunPoint &, const RelationsRunPoint &) = default;
};

// A run's drift against shot order; `line` (RelationsLine, on the ordinal axis) only when it clears chance, else null.
struct RelationsDrift
{
    Q_GADGET
    Q_PROPERTY(bool     defined   MEMBER defined   FINAL)
    Q_PROPERTY(QVariant r         MEMBER r         FINAL)
    Q_PROPERTY(QVariant lo        MEMBER lo        FINAL)
    Q_PROPERTY(QVariant hi        MEMBER hi        FINAL)
    Q_PROPERTY(bool     clear     MEMBER clear     FINAL)
    Q_PROPERTY(QString  rText     MEMBER rText     FINAL)
    Q_PROPERTY(QString  ciText    MEMBER ciText    FINAL)
    Q_PROPERTY(QVariant slope     MEMBER slope     FINAL)
    Q_PROPERTY(QVariant intercept MEMBER intercept FINAL)
    Q_PROPERTY(QVariant line      MEMBER line      FINAL)
public:
    bool     defined = false;
    QVariant r = relationsNull(), lo = relationsNull(), hi = relationsNull();
    bool     clear = false;
    QString  rText, ciText;
    QVariant slope = relationsNull(), intercept = relationsNull();
    QVariant line = relationsNull();
    friend bool operator==(const RelationsDrift &, const RelationsDrift &) = default;
};

// A RUN (PpChRunBox): one reading through the session.
struct RelationsReadingRun
{
    Q_GADGET
    Q_PROPERTY(QString                                    readingId MEMBER readingId FINAL)
    Q_PROPERTY(QString                                    key       MEMBER key       FINAL)
    Q_PROPERTY(QString                                    label     MEMBER label     FINAL)
    Q_PROPERTY(QString                                    tag       MEMBER tag       FINAL)
    Q_PROPERTY(QString                                    unit      MEMBER unit      FINAL)
    Q_PROPERTY(QString                                    unitText  MEMBER unitText  FINAL)
    Q_PROPERTY(QString                                    caption   MEMBER caption   FINAL)
    Q_PROPERTY(QList<pinpoint::charts::RelationsRunPoint> points    MEMBER points    FINAL)
    Q_PROPERTY(pinpoint::charts::RelationsBox             box       MEMBER box       FINAL)
    Q_PROPERTY(pinpoint::charts::RelationsRange           yRange    MEMBER yRange    FINAL)
    Q_PROPERTY(pinpoint::charts::RelationsDrift           drift     MEMBER drift     FINAL)
public:
    QString readingId, key, label, tag, unit, unitText, caption;
    QList<RelationsRunPoint> points;
    RelationsBox   box;
    RelationsRange yRange;
    RelationsDrift drift;
    friend bool operator==(const RelationsReadingRun &, const RelationsReadingRun &) = default;
};

// A trace's curve on this shot's clock: `v` NaN where bridged, `raw` NaN where no sample.
struct RelationsCurve
{
    Q_GADGET
    Q_PROPERTY(QList<double> t   MEMBER t   FINAL)
    Q_PROPERTY(QList<double> v   MEMBER v   FINAL)
    Q_PROPERTY(QList<double> raw MEMBER raw FINAL)
    Q_PROPERTY(QList<bool>   ok  MEMBER ok  FINAL)
public:
    QList<double> t, v, raw;
    QList<bool>   ok;
    friend bool operator==(const RelationsCurve &, const RelationsCurve &) = default;
};

// A P-position on a trace.
struct RelationsTracePhase
{
    Q_GADGET
    Q_PROPERTY(int     phase  MEMBER phase  FINAL)
    Q_PROPERTY(QString tag    MEMBER tag    FINAL)
    Q_PROPERTY(double  shotMs MEMBER shotMs FINAL)
    Q_PROPERTY(bool    impact MEMBER impact FINAL)
public:
    int     phase = 0;
    QString tag;
    double  shotMs = 0;
    bool    impact = false;
    friend bool operator==(const RelationsTracePhase &, const RelationsTracePhase &) = default;
};

// A box of every shot at one P-position the key is read at; `firedShots` empty except at the reading's own instant.
struct RelationsTraceInstant
{
    Q_GADGET
    Q_PROPERTY(int                            phase       MEMBER phase       FINAL)
    Q_PROPERTY(QString                        tag         MEMBER tag         FINAL)
    Q_PROPERTY(double                         shotMs      MEMBER shotMs      FINAL)
    Q_PROPERTY(pinpoint::charts::RelationsBox box         MEMBER box         FINAL)
    Q_PROPERTY(QList<double>                  values      MEMBER values      FINAL)
    Q_PROPERTY(QList<bool>                    firedShots  MEMBER firedShots  FINAL)
    Q_PROPERTY(QVariant                       pickedValue MEMBER pickedValue FINAL)
    Q_PROPERTY(QString                        pickedText  MEMBER pickedText  FINAL)
    Q_PROPERTY(bool                           own         MEMBER own         FINAL)
public:
    int           phase = 0;
    QString       tag;
    double        shotMs = 0;
    RelationsBox  box;
    QList<double> values;
    QList<bool>   firedShots;
    QVariant      pickedValue = relationsNull();
    QString       pickedText;
    bool          own = false;
    friend bool operator==(const RelationsTraceInstant &, const RelationsTraceInstant &) = default;
};

// A TRACE (PpChReadingTrace): one reading's curve on the picked shot's clock.
struct RelationsTrace
{
    Q_GADGET
    Q_PROPERTY(QString                                        readingId MEMBER readingId FINAL)
    Q_PROPERTY(QString                                        key       MEMBER key       FINAL)
    Q_PROPERTY(QString                                        label     MEMBER label     FINAL)
    Q_PROPERTY(QString                                        tag       MEMBER tag       FINAL)
    Q_PROPERTY(QString                                        unit      MEMBER unit      FINAL)
    Q_PROPERTY(QString                                        unitText  MEMBER unitText  FINAL)
    Q_PROPERTY(QString                                        caption   MEMBER caption   FINAL)
    Q_PROPERTY(pinpoint::charts::RelationsWindow              window    MEMBER window    FINAL)
    Q_PROPERTY(pinpoint::charts::RelationsRange               yRange    MEMBER yRange    FINAL)
    Q_PROPERTY(pinpoint::charts::RelationsCurve               curve     MEMBER curve     FINAL)
    Q_PROPERTY(QList<pinpoint::charts::RelationsTracePhase>   phases    MEMBER phases    FINAL)
    Q_PROPERTY(QList<pinpoint::charts::RelationsTraceInstant> instants  MEMBER instants  FINAL)
public:
    QString readingId, key, label, tag, unit, unitText, caption;
    RelationsWindow window;
    RelationsRange  yRange;
    RelationsCurve  curve;
    QList<RelationsTracePhase>   phases;
    QList<RelationsTraceInstant> instants;
    friend bool operator==(const RelationsTrace &, const RelationsTrace &) = default;
};

struct RelationsScatterPoint
{
    Q_GADGET
    Q_PROPERTY(int     ordinal  MEMBER ordinal  FINAL)
    Q_PROPERTY(int     shotId   MEMBER shotId   FINAL)
    Q_PROPERTY(QString swingDir MEMBER swingDir FINAL)
    Q_PROPERTY(double  x        MEMBER x        FINAL)
    Q_PROPERTY(double  y        MEMBER y        FINAL)
    Q_PROPERTY(bool    picked   MEMBER picked   FINAL)
    Q_PROPERTY(bool    fired    MEMBER fired    FINAL)
public:
    int     ordinal = 0;
    int     shotId = -1;
    QString swingDir;
    double  x = 0, y = 0;
    bool    picked = false, fired = false;
    friend bool operator==(const RelationsScatterPoint &, const RelationsScatterPoint &) = default;
};

// A SCATTER (PpChScatter). `line` is a RelationsLine, or null with too few shots for one.
struct RelationsScatter
{
    Q_GADGET
    Q_PROPERTY(QString                                        xId     MEMBER xId     FINAL)
    Q_PROPERTY(QString                                        yId     MEMBER yId     FINAL)
    Q_PROPERTY(QString                                        xKey    MEMBER xKey    FINAL)
    Q_PROPERTY(QString                                        yKey    MEMBER yKey    FINAL)
    Q_PROPERTY(QString                                        xLabel  MEMBER xLabel  FINAL)
    Q_PROPERTY(QString                                        yLabel  MEMBER yLabel  FINAL)
    Q_PROPERTY(QString                                        xUnit   MEMBER xUnit   FINAL)
    Q_PROPERTY(QString                                        yUnit   MEMBER yUnit   FINAL)
    Q_PROPERTY(pinpoint::charts::RelationsRange               xRange  MEMBER xRange  FINAL)
    Q_PROPERTY(pinpoint::charts::RelationsRange               yRange  MEMBER yRange  FINAL)
    Q_PROPERTY(pinpoint::charts::RelationsBox                 xBox    MEMBER xBox    FINAL)
    Q_PROPERTY(pinpoint::charts::RelationsBox                 yBox    MEMBER yBox    FINAL)
    Q_PROPERTY(QList<pinpoint::charts::RelationsScatterPoint> points  MEMBER points  FINAL)
    Q_PROPERTY(bool                                           defined MEMBER defined FINAL)
    Q_PROPERTY(QVariant                                       r       MEMBER r       FINAL)
    Q_PROPERTY(QVariant                                       lo      MEMBER lo      FINAL)
    Q_PROPERTY(QVariant                                       hi      MEMBER hi      FINAL)
    Q_PROPERTY(int                                            n       MEMBER n       FINAL)
    Q_PROPERTY(bool                                           clear   MEMBER clear   FINAL)
    Q_PROPERTY(QString                                        rText   MEMBER rText   FINAL)
    Q_PROPERTY(QString                                        ciText  MEMBER ciText  FINAL)
    Q_PROPERTY(QVariant                                       line    MEMBER line    FINAL)
    Q_PROPERTY(QString                                        caption MEMBER caption FINAL)
public:
    QString xId, yId, xKey, yKey, xLabel, yLabel, xUnit, yUnit;
    RelationsRange xRange, yRange;
    RelationsBox   xBox, yBox;
    QList<RelationsScatterPoint> points;
    bool     defined = false;
    QVariant r = relationsNull(), lo = relationsNull(), hi = relationsNull();
    int      n = 0;
    bool     clear = false;
    QString  rText, ciText;
    QVariant line = relationsNull();
    QString  caption;
    friend bool operator==(const RelationsScatter &, const RelationsScatter &) = default;
};

// ── the ledger ─────────────────────────────────────────────────────────────────────────────────

// A ledger row's sparkline: each reading's values per shot (NaN where missing) and its session min and max.
struct RelationsPair
{
    Q_GADGET
    Q_PROPERTY(QList<double>                   xs     MEMBER xs     FINAL)
    Q_PROPERTY(QList<double>                   ys     MEMBER ys     FINAL)
    Q_PROPERTY(pinpoint::charts::RelationsRange xRange MEMBER xRange FINAL)
    Q_PROPERTY(pinpoint::charts::RelationsRange yRange MEMBER yRange FINAL)
public:
    QList<double>  xs, ys;
    RelationsRange xRange, yRange;
    friend bool operator==(const RelationsPair &, const RelationsPair &) = default;
};

// A LEDGER ROW (relations.ledger, and movesWith().rows): a reading against the Up one.
struct RelationsLedgerRow
{
    Q_GADGET
    Q_PROPERTY(int                             rank      MEMBER rank      FINAL)
    Q_PROPERTY(QString                         readingId MEMBER readingId FINAL)
    Q_PROPERTY(QString                         key       MEMBER key       FINAL)
    Q_PROPERTY(QString                         label     MEMBER label     FINAL)
    Q_PROPERTY(QString                         short     MEMBER shortText FINAL)
    Q_PROPERTY(QString                         tag       MEMBER tag       FINAL)
    Q_PROPERTY(bool                            same      MEMBER same      FINAL)
    Q_PROPERTY(QString                         sameMark  MEMBER sameMark  FINAL)
    Q_PROPERTY(bool                            finding   MEMBER finding   FINAL)
    Q_PROPERTY(QVariant                        r         MEMBER r         FINAL)
    Q_PROPERTY(QString                         rText     MEMBER rText     FINAL)
    Q_PROPERTY(QVariant                        lo        MEMBER lo        FINAL)
    Q_PROPERTY(QVariant                        hi        MEMBER hi        FINAL)
    Q_PROPERTY(QString                         loText    MEMBER loText    FINAL)
    Q_PROPERTY(QString                         hiText    MEMBER hiText    FINAL)
    Q_PROPERTY(QString                         ciText    MEMBER ciText    FINAL)
    Q_PROPERTY(int                             n         MEMBER n         FINAL)
    Q_PROPERTY(bool                            clear     MEMBER clear     FINAL)
    Q_PROPERTY(QVariant                        slope     MEMBER slope     FINAL)
    Q_PROPERTY(QString                         slopeText MEMBER slopeText FINAL)
    Q_PROPERTY(pinpoint::charts::RelationsPair pair      MEMBER pair      FINAL)
public:
    int      rank = 0;
    QString  readingId, key, label, shortText, tag;
    bool     same = false;
    QString  sameMark;
    bool     finding = false;
    QVariant r = relationsNull();
    QString  rText;
    QVariant lo = relationsNull(), hi = relationsNull();
    QString  loText, hiText, ciText;
    int      n = 0;
    bool     clear = false;
    QVariant slope = relationsNull();
    QString  slopeText;
    RelationsPair pair;
    friend bool operator==(const RelationsLedgerRow &, const RelationsLedgerRow &) = default;
};

// A reading in brief (relations.up / across, a matrix's readings, movesWith().up).
struct RelationsBrief
{
    Q_GADGET
    Q_PROPERTY(QString readingId MEMBER readingId FINAL)
    Q_PROPERTY(QString key       MEMBER key       FINAL)
    Q_PROPERTY(QString label     MEMBER label     FINAL)
    Q_PROPERTY(QString short     MEMBER shortText FINAL)
    Q_PROPERTY(QString tag       MEMBER tag       FINAL)
    Q_PROPERTY(QString unit      MEMBER unit      FINAL)
    Q_PROPERTY(QString group     MEMBER group     FINAL)
    Q_PROPERTY(QString text      MEMBER text      FINAL)
public:
    QString readingId, key, label, shortText, tag, unit, group, text;
    friend bool operator==(const RelationsBrief &, const RelationsBrief &) = default;
};

// The chance line, said out loud. movesWith() has no `sentence` (it stays empty).
struct RelationsChance
{
    Q_GADGET
    Q_PROPERTY(int                                 n             MEMBER n             FINAL)
    Q_PROPERTY(int                                 tests         MEMBER tests         FINAL)
    Q_PROPERTY(int                                 clearCount    MEMBER clearCount    FINAL)
    Q_PROPERTY(int                                 chanceCount   MEMBER chanceCount   FINAL)
    Q_PROPERTY(double                              rCritical     MEMBER rCritical     FINAL)
    Q_PROPERTY(QString                             rCriticalText MEMBER rCriticalText FINAL)
    Q_PROPERTY(pinpoint::charts::RelationsSentence line          MEMBER line          FINAL)
    Q_PROPERTY(pinpoint::charts::RelationsSentence sentence      MEMBER sentence      FINAL)
public:
    int     n = 0, tests = 0, clearCount = 0, chanceCount = 0;
    double  rCritical = 0;
    QString rCriticalText;
    RelationsSentence line, sentence;
    friend bool operator==(const RelationsChance &, const RelationsChance &) = default;
};

// THE WRIST TAB'S QUESTION (ChartsRelationsPresenter::movesWith): what moves with one reading, whatever the Relations
// tab's Up is. Its keys are movesWith()'s; `relationsUpId` and `sub` are the two the Wrist presenter sets on it (""
// as movesWith() returns it).
struct RelationsMovesWith
{
    Q_GADGET
    Q_PROPERTY(bool                                       available     MEMBER available     FINAL)
    Q_PROPERTY(pinpoint::charts::RelationsSentence        refusal       MEMBER refusal       FINAL)
    Q_PROPERTY(QString                                    upId          MEMBER upId          FINAL)
    Q_PROPERTY(pinpoint::charts::RelationsBrief           up            MEMBER up            FINAL)
    Q_PROPERTY(pinpoint::charts::RelationsSentence        head          MEMBER head          FINAL)
    Q_PROPERTY(pinpoint::charts::RelationsChance          chance        MEMBER chance        FINAL)
    Q_PROPERTY(QList<pinpoint::charts::RelationsLedgerRow> rows         MEMBER rows          FINAL)
    Q_PROPERTY(pinpoint::charts::RelationsSentence        note          MEMBER note          FINAL)
    Q_PROPERTY(QString                                    relationsUpId MEMBER relationsUpId FINAL)
    Q_PROPERTY(QString                                    sub           MEMBER sub           FINAL)
public:
    bool              available = false;
    RelationsSentence refusal;
    QString           upId;
    RelationsBrief    up;
    RelationsSentence head;
    RelationsChance   chance;
    QList<RelationsLedgerRow> rows;
    RelationsSentence note;
    QString           relationsUpId, sub;
    friend bool operator==(const RelationsMovesWith &, const RelationsMovesWith &) = default;
};

// ── status and families ────────────────────────────────────────────────────────────────────────

// What a job spent, in ms: on the worker, on the rule alone (0 once it has run for the snapshot), and on the GUI thread.
struct RelationsTimings
{
    Q_GADGET
    Q_PROPERTY(double jobMs         MEMBER jobMs         FINAL)
    Q_PROPERTY(double consistencyMs MEMBER consistencyMs FINAL)
    Q_PROPERTY(double applyMs       MEMBER applyMs       FINAL)
public:
    double jobMs = 0, consistencyMs = 0, applyMs = 0;
    friend bool operator==(const RelationsTimings &, const RelationsTimings &) = default;
};

// `status`. Before the first result every field is at its default; pickedImpactUs is −1 then, as "none".
struct RelationsStatus
{
    Q_GADGET
    Q_PROPERTY(bool                                  ready            MEMBER ready            FINAL)
    Q_PROPERTY(int                                   shotCount        MEMBER shotCount        FINAL)
    Q_PROPERTY(int                                   pickedOrdinal    MEMBER pickedOrdinal    FINAL)
    Q_PROPERTY(int                                   pickedShotId     MEMBER pickedShotId     FINAL)
    Q_PROPERTY(QString                               pickedSwingDir   MEMBER pickedSwingDir   FINAL)
    Q_PROPERTY(bool                                  pickedInSession  MEMBER pickedInSession  FINAL)
    Q_PROPERTY(bool                                  pickedFromDetail MEMBER pickedFromDetail FINAL)
    Q_PROPERTY(qint64                                pickedImpactUs   MEMBER pickedImpactUs   FINAL)
    Q_PROPERTY(QString                               club             MEMBER club             FINAL)
    Q_PROPERTY(bool                                  clubsMixed       MEMBER clubsMixed       FINAL)
    Q_PROPERTY(QString                               clubsSentence    MEMBER clubsSentence    FINAL)
    Q_PROPERTY(QString                               kicker           MEMBER kicker           FINAL)
    Q_PROPERTY(QString                               family           MEMBER family           FINAL)
    Q_PROPERTY(int                                   readingCount     MEMBER readingCount     FINAL)
    Q_PROPERTY(QList<pinpoint::charts::RelationsShot> shots           MEMBER shots            FINAL)
    Q_PROPERTY(QString                               shotLabel        MEMBER shotLabel        FINAL)
    Q_PROPERTY(QString                               countLabel       MEMBER countLabel       FINAL)
    Q_PROPERTY(QString                               countLabelShort  MEMBER countLabelShort  FINAL)
    Q_PROPERTY(bool                                  refused          MEMBER refused          FINAL)
    Q_PROPERTY(pinpoint::charts::RelationsSentence   refusal          MEMBER refusal          FINAL)
    Q_PROPERTY(pinpoint::charts::RelationsTimings    timings          MEMBER timings          FINAL)
public:
    bool    ready = false;
    int     shotCount = 0, pickedOrdinal = 0, pickedShotId = -1;
    QString pickedSwingDir;
    bool    pickedInSession = false, pickedFromDetail = false;
    qint64  pickedImpactUs = -1;
    QString club;
    bool    clubsMixed = false;
    QString clubsSentence, kicker, family;
    int     readingCount = 0;
    QList<RelationsShot> shots;
    QString shotLabel, countLabel, countLabelShort;
    bool    refused = false;
    RelationsSentence refusal;
    RelationsTimings  timings;
    friend bool operator==(const RelationsStatus &, const RelationsStatus &) = default;
};

struct RelationsFamily
{
    Q_GADGET
    Q_PROPERTY(QString name     MEMBER name     FINAL)
    Q_PROPERTY(int     count    MEMBER count    FINAL)
    Q_PROPERTY(QString note     MEMBER note     FINAL)
    Q_PROPERTY(bool    preset   MEMBER preset   FINAL)
    Q_PROPERTY(bool    current  MEMBER current  FINAL)
    Q_PROPERTY(bool    sequence MEMBER sequence FINAL)
public:
    QString name;
    int     count = 0;
    QString note;
    bool    preset = false, current = false, sequence = false;
    friend bool operator==(const RelationsFamily &, const RelationsFamily &) = default;
};

// ── This shot ──────────────────────────────────────────────────────────────────────────────────

// A key figure: `typicality` a RelationsVariability ("typicality") or null.
struct RelationsKeyFigure
{
    Q_GADGET
    Q_PROPERTY(QString                             readingId    MEMBER readingId    FINAL)
    Q_PROPERTY(QString                             key          MEMBER key          FINAL)
    Q_PROPERTY(QString                             label        MEMBER label        FINAL)
    Q_PROPERTY(QString                             figure       MEMBER figure       FINAL)
    Q_PROPERTY(QString                             unit         MEMBER unit         FINAL)
    Q_PROPERTY(QString                             foot         MEMBER foot         FINAL)
    Q_PROPERTY(QString                             median       MEMBER median       FINAL)
    Q_PROPERTY(QString                             place        MEMBER place        FINAL)
    Q_PROPERTY(bool                                warn         MEMBER warn         FINAL)
    Q_PROPERTY(QString                             warnWord     MEMBER warnWord     FINAL)
    Q_PROPERTY(QString                             trustSubject MEMBER trustSubject FINAL)
    Q_PROPERTY(pinpoint::charts::RelationsSentence sentence     MEMBER sentence     FINAL)
    Q_PROPERTY(QVariant                            typicality   MEMBER typicality   FINAL)
public:
    QString  readingId, key, label, figure, unit, foot, median, place;
    bool     warn = false;
    QString  warnWord, trustSubject;
    RelationsSentence sentence;
    QVariant typicality = relationsNull();
    friend bool operator==(const RelationsKeyFigure &, const RelationsKeyFigure &) = default;
};

struct RelationsKeyFigures
{
    Q_GADGET
    Q_PROPERTY(QString                                    family   MEMBER family   FINAL)
    Q_PROPERTY(bool                                       sequence MEMBER sequence FINAL)
    Q_PROPERTY(QList<pinpoint::charts::RelationsKeyFigure> items   MEMBER items    FINAL)
public:
    QString family;
    bool    sequence = false;
    QList<RelationsKeyFigure> items;
    friend bool operator==(const RelationsKeyFigures &, const RelationsKeyFigures &) = default;
};

// The family's lead reading on this shot: `trace` a RelationsTrace or null ("" readingId: no lead).
struct RelationsLead
{
    Q_GADGET
    Q_PROPERTY(QString  readingId MEMBER readingId FINAL)
    Q_PROPERTY(QVariant trace     MEMBER trace     FINAL)
public:
    QString  readingId;
    QVariant trace = relationsNull();
    friend bool operator==(const RelationsLead &, const RelationsLead &) = default;
};

// A fired condition whose link lands on no reading.
struct RelationsFiredElsewhere
{
    Q_GADGET
    Q_PROPERTY(QString condition MEMBER condition FINAL)
    Q_PROPERTY(QString name      MEMBER name      FINAL)
    Q_PROPERTY(QString metricKey MEMBER metricKey FINAL)
    Q_PROPERTY(int     phase     MEMBER phase     FINAL)
    Q_PROPERTY(int     reducer   MEMBER reducer   FINAL)
    Q_PROPERTY(QString instant   MEMBER instant   FINAL)
public:
    QString condition, name, metricKey;
    int     phase = -1, reducer = -1;
    QString instant;
    friend bool operator==(const RelationsFiredElsewhere &, const RelationsFiredElsewhere &) = default;
};

// A question (Analyse's asks): `lit` the ordinals it lights, `go` the tab it opens ("" none).
struct RelationsAsk
{
    Q_GADGET
    Q_PROPERTY(QString    id     MEMBER id     FINAL)
    Q_PROPERTY(QString    text   MEMBER text   FINAL)
    Q_PROPERTY(QString    answer MEMBER answer FINAL)
    Q_PROPERTY(QList<int> lit    MEMBER lit    FINAL)
    Q_PROPERTY(QString    go     MEMBER go     FINAL)
    Q_PROPERTY(QString    upId   MEMBER upId   FINAL)
public:
    QString    id, text, answer;
    QList<int> lit;
    QString    go, upId;
    friend bool operator==(const RelationsAsk &, const RelationsAsk &) = default;
};

// One of the Readings tab's verdict filters.
struct RelationsVerdictFilter
{
    Q_GADGET
    Q_PROPERTY(QString verdict   MEMBER verdict   FINAL)
    Q_PROPERTY(QString word      MEMBER word      FINAL)
    Q_PROPERTY(bool    warm      MEMBER warm      FINAL)
    Q_PROPERTY(QString none      MEMBER none      FINAL)
    Q_PROPERTY(QString noneNamed MEMBER noneNamed FINAL)
public:
    QString verdict, word;
    bool    warm = false;
    QString none, noneNamed;
    friend bool operator==(const RelationsVerdictFilter &, const RelationsVerdictFilter &) = default;
};

// `thisShot`.
struct RelationsThisShot
{
    Q_GADGET
    Q_PROPERTY(bool                                            available      MEMBER available      FINAL)
    Q_PROPERTY(int                                             total          MEMBER total          FINAL)
    Q_PROPERTY(QList<pinpoint::charts::RelationsVerdictFilter> verdictFilters MEMBER verdictFilters FINAL)
    Q_PROPERTY(QString                                         noName         MEMBER noName         FINAL)
    Q_PROPERTY(pinpoint::charts::RelationsSentence             head           MEMBER head           FINAL)
    Q_PROPERTY(QString                                         kicker         MEMBER kicker         FINAL)
    Q_PROPERTY(pinpoint::charts::RelationsKeyFigures           keyFigures     MEMBER keyFigures     FINAL)
    Q_PROPERTY(pinpoint::charts::RelationsLead                 lead           MEMBER lead           FINAL)
    Q_PROPERTY(int                                             apartCount     MEMBER apartCount     FINAL)
    Q_PROPERTY(QString                                         apartLabel     MEMBER apartLabel     FINAL)
    Q_PROPERTY(QList<pinpoint::charts::RelationsReadingRow>    apart          MEMBER apart          FINAL)
    Q_PROPERTY(pinpoint::charts::RelationsSentence             standfirst     MEMBER standfirst     FINAL)
    Q_PROPERTY(QList<pinpoint::charts::RelationsRowGroup>      groups         MEMBER groups         FINAL)
    Q_PROPERTY(QString                                         notes          MEMBER notes          FINAL)
    Q_PROPERTY(QList<pinpoint::charts::RelationsFiredElsewhere> firedElsewhere MEMBER firedElsewhere FINAL)
    Q_PROPERTY(QList<pinpoint::charts::RelationsReadingRow>    familyRows     MEMBER familyRows     FINAL)
    Q_PROPERTY(QList<pinpoint::charts::RelationsAsk>           asks           MEMBER asks           FINAL)
public:
    bool    available = false;
    int     total = 0;
    QList<RelationsVerdictFilter> verdictFilters;
    QString noName;
    RelationsSentence   head;
    QString             kicker;
    RelationsKeyFigures keyFigures;
    RelationsLead       lead;
    int     apartCount = 0;
    QString apartLabel;          // the Apart fold's state: "2 readings", "" with none (refactor 5.5c: QML made it)
    QList<RelationsReadingRow> apart;
    RelationsSentence   standfirst;
    QList<RelationsRowGroup> groups;
    QString notes;
    QList<RelationsFiredElsewhere> firedElsewhere;
    QList<RelationsReadingRow> familyRows;
    QList<RelationsAsk> asks;
    friend bool operator==(const RelationsThisShot &, const RelationsThisShot &) = default;
};

// `openRow`: the open row's curve (`trace` a RelationsTrace or null) or its run. "" readingId: nothing open.
struct RelationsOpenRow
{
    Q_GADGET
    Q_PROPERTY(QString                               readingId MEMBER readingId FINAL)
    Q_PROPERTY(QString                               opens     MEMBER opens     FINAL)
    Q_PROPERTY(QVariant                              trace     MEMBER trace     FINAL)
    Q_PROPERTY(pinpoint::charts::RelationsReadingRun run       MEMBER run       FINAL)
public:
    QString  readingId, opens;
    QVariant trace = relationsNull();
    RelationsReadingRun run;
    friend bool operator==(const RelationsOpenRow &, const RelationsOpenRow &) = default;
};

// ── the sequence (SeqTiming) ───────────────────────────────────────────────────────────────────

struct RelationsChainLink
{
    Q_GADGET
    Q_PROPERTY(QString segment  MEMBER segment  FINAL)
    Q_PROPERTY(QString label    MEMBER label    FINAL)
    Q_PROPERTY(bool    probable MEMBER probable FINAL)
    Q_PROPERTY(double  shotMs   MEMBER shotMs   FINAL)
    Q_PROPERTY(QString msText   MEMBER msText   FINAL)
    Q_PROPERTY(bool    late     MEMBER late     FINAL)
    Q_PROPERTY(QString lateWord MEMBER lateWord FINAL)
public:
    QString segment, label;
    bool    probable = false;
    double  shotMs = 0;
    QString msText;
    bool    late = false;
    QString lateWord;
    friend bool operator==(const RelationsChainLink &, const RelationsChainLink &) = default;
};

struct RelationsSequenceTick
{
    Q_GADGET
    Q_PROPERTY(double  ms   MEMBER ms   FINAL)
    Q_PROPERTY(QString text MEMBER text FINAL)
public:
    double  ms = 0;
    QString text;
    friend bool operator==(const RelationsSequenceTick &, const RelationsSequenceTick &) = default;
};

// A segment's row: `shotMs` null when this shot's peak is not timed; `values` NaN where a shot's is not.
struct RelationsSequenceRow
{
    Q_GADGET
    Q_PROPERTY(QString                        segment       MEMBER segment       FINAL)
    Q_PROPERTY(QString                        label         MEMBER label         FINAL)
    Q_PROPERTY(QString                        valueText     MEMBER valueText     FINAL)
    Q_PROPERTY(QVariant                       shotMs        MEMBER shotMs        FINAL)
    Q_PROPERTY(int                            placed        MEMBER placed        FINAL)
    Q_PROPERTY(QString                        placedText    MEMBER placedText    FINAL)
    Q_PROPERTY(pinpoint::charts::RelationsBox box           MEMBER box           FINAL)
    Q_PROPERTY(QList<double>                  values        MEMBER values        FINAL)
    Q_PROPERTY(QList<bool>                    lateShots     MEMBER lateShots     FINAL)
    Q_PROPERTY(bool                           hasBox        MEMBER hasBox        FINAL)
    Q_PROPERTY(QString                        notPlacedText MEMBER notPlacedText FINAL)
public:
    QString       segment, label, valueText;
    QVariant      shotMs = relationsNull();
    int           placed = 0;
    QString       placedText;
    RelationsBox  box;
    QList<double> values;
    QList<bool>   lateShots;
    bool          hasBox = false;
    QString       notPlacedText;
    friend bool operator==(const RelationsSequenceRow &, const RelationsSequenceRow &) = default;
};

struct RelationsSequence
{
    Q_GADGET
    Q_PROPERTY(bool                                           available     MEMBER available     FINAL)
    Q_PROPERTY(QList<pinpoint::charts::RelationsChainLink>    chain         MEMBER chain         FINAL)
    Q_PROPERTY(pinpoint::charts::RelationsSentence            chainSentence MEMBER chainSentence FINAL)
    Q_PROPERTY(QString                                        unplaced      MEMBER unplaced      FINAL)
    Q_PROPERTY(int                                            orderSame     MEMBER orderSame     FINAL)
    Q_PROPERTY(int                                            orderCount    MEMBER orderCount    FINAL)
    Q_PROPERTY(pinpoint::charts::RelationsSentence            verdict       MEMBER verdict       FINAL)
    Q_PROPERTY(pinpoint::charts::RelationsRange               axis          MEMBER axis          FINAL)
    Q_PROPERTY(QList<pinpoint::charts::RelationsSequenceTick> ticks         MEMBER ticks         FINAL)
    Q_PROPERTY(QList<pinpoint::charts::RelationsSequenceRow>  rows          MEMBER rows          FINAL)
    Q_PROPERTY(pinpoint::charts::RelationsSentence            note          MEMBER note          FINAL)
public:
    bool available = false;
    QList<RelationsChainLink> chain;
    RelationsSentence chainSentence;
    QString unplaced;
    int     orderSame = 0, orderCount = 0;
    RelationsSentence verdict;
    RelationsRange axis;
    QList<RelationsSequenceTick> ticks;
    QList<RelationsSequenceRow>  rows;
    RelationsSentence note;
    friend bool operator==(const RelationsSequence &, const RelationsSequence &) = default;
};

// ── Relations ──────────────────────────────────────────────────────────────────────────────────

struct RelationsCatalogueReading
{
    Q_GADGET
    Q_PROPERTY(QString readingId MEMBER readingId FINAL)
    Q_PROPERTY(QString short     MEMBER shortText FINAL)
    Q_PROPERTY(QString tag       MEMBER tag       FINAL)
    Q_PROPERTY(QString text      MEMBER text      FINAL)
    Q_PROPERTY(QString label     MEMBER label     FINAL)
public:
    QString readingId, shortText, tag, text, label;
    friend bool operator==(const RelationsCatalogueReading &, const RelationsCatalogueReading &) = default;
};

struct RelationsCatalogueGroup
{
    Q_GADGET
    Q_PROPERTY(QString                                             name     MEMBER name     FINAL)
    Q_PROPERTY(QList<pinpoint::charts::RelationsCatalogueReading>  readings MEMBER readings FINAL)
public:
    QString name;
    QList<RelationsCatalogueReading> readings;
    friend bool operator==(const RelationsCatalogueGroup &, const RelationsCatalogueGroup &) = default;
};

// A facet the pickers offer.
struct RelationsFacetChoice
{
    Q_GADGET
    Q_PROPERTY(QString id    MEMBER id    FINAL)
    Q_PROPERTY(QString kind  MEMBER kind  FINAL)
    Q_PROPERTY(QString name  MEMBER name  FINAL)
    Q_PROPERTY(QString label MEMBER label FINAL)
    Q_PROPERTY(int     n     MEMBER n     FINAL)
public:
    QString id, kind, name, label;
    int     n = 0;
    friend bool operator==(const RelationsFacetChoice &, const RelationsFacetChoice &) = default;
};

struct RelationsFacetPanel
{
    Q_GADGET
    Q_PROPERTY(QString                            title    MEMBER title    FINAL)
    Q_PROPERTY(int                                n        MEMBER n        FINAL)
    Q_PROPERTY(QList<int>                         ordinals MEMBER ordinals FINAL)
    Q_PROPERTY(bool                               fired    MEMBER fired    FINAL)
    Q_PROPERTY(pinpoint::charts::RelationsScatter scatter  MEMBER scatter  FINAL)
public:
    QString    title;
    int        n = 0;
    QList<int> ordinals;
    bool       fired = false;
    RelationsScatter scatter;
    friend bool operator==(const RelationsFacetPanel &, const RelationsFacetPanel &) = default;
};

// The chosen facet (relations.facet holds one, or null with none chosen or none that splits).
struct RelationsFacet
{
    Q_GADGET
    Q_PROPERTY(QString                                       id       MEMBER id       FINAL)
    Q_PROPERTY(QString                                       kind     MEMBER kind     FINAL)
    Q_PROPERTY(QString                                       name     MEMBER name     FINAL)
    Q_PROPERTY(pinpoint::charts::RelationsSentence           sentence MEMBER sentence FINAL)
    Q_PROPERTY(QList<pinpoint::charts::RelationsFacetPanel>  panels   MEMBER panels   FINAL)
public:
    QString id, kind, name;
    RelationsSentence sentence;
    QList<RelationsFacetPanel> panels;
    friend bool operator==(const RelationsFacet &, const RelationsFacet &) = default;
};

struct RelationsMatrixCell
{
    Q_GADGET
    Q_PROPERTY(bool     defined MEMBER defined FINAL)
    Q_PROPERTY(QVariant r       MEMBER r       FINAL)
    Q_PROPERTY(QString  rText   MEMBER rText   FINAL)
    Q_PROPERTY(bool     clear   MEMBER clear   FINAL)
    Q_PROPERTY(QString  xId     MEMBER xId     FINAL)
    Q_PROPERTY(QString  yId     MEMBER yId     FINAL)
    Q_PROPERTY(bool     on      MEMBER on      FINAL)
public:
    bool     defined = false;
    QVariant r = relationsNull();
    QString  rText;
    bool     clear = false;
    QString  xId, yId;
    bool     on = false;
    friend bool operator==(const RelationsMatrixCell &, const RelationsMatrixCell &) = default;
};

// The family matrix, the lower triangle: row i of `cells` has i cells (PpChMatrix reads cells[i][j]).
struct RelationsMatrix
{
    Q_GADGET
    Q_PROPERTY(QString                                            group    MEMBER group    FINAL)
    Q_PROPERTY(QList<pinpoint::charts::RelationsBrief>            readings MEMBER readings FINAL)
    Q_PROPERTY(QList<QList<pinpoint::charts::RelationsMatrixCell>> cells   MEMBER cells    FINAL)
public:
    QString group;
    QList<RelationsBrief> readings;
    QList<QList<RelationsMatrixCell>> cells;
    friend bool operator==(const RelationsMatrix &, const RelationsMatrix &) = default;
};

// A drift that clears chance.
struct RelationsDriftItem
{
    Q_GADGET
    Q_PROPERTY(QString                                readingId MEMBER readingId FINAL)
    Q_PROPERTY(QString                                label     MEMBER label     FINAL)
    Q_PROPERTY(QString                                tag       MEMBER tag       FINAL)
    Q_PROPERTY(double                                 r         MEMBER r         FINAL)
    Q_PROPERTY(QString                                rText     MEMBER rText     FINAL)
    Q_PROPERTY(double                                 lo        MEMBER lo        FINAL)
    Q_PROPERTY(double                                 hi        MEMBER hi        FINAL)
    Q_PROPERTY(QString                                ciText    MEMBER ciText    FINAL)
    Q_PROPERTY(bool                                   clear     MEMBER clear     FINAL)
    Q_PROPERTY(QString                                direction MEMBER direction FINAL)
    Q_PROPERTY(pinpoint::charts::RelationsSentence    sentence  MEMBER sentence  FINAL)
    Q_PROPERTY(pinpoint::charts::RelationsReadingRun  run       MEMBER run       FINAL)
public:
    QString readingId, label, tag;
    double  r = 0;
    QString rText;
    double  lo = 0, hi = 0;
    QString ciText;
    bool    clear = false;
    QString direction;
    RelationsSentence   sentence;
    RelationsReadingRun run;
    friend bool operator==(const RelationsDriftItem &, const RelationsDriftItem &) = default;
};

struct RelationsDrifts
{
    Q_GADGET
    Q_PROPERTY(int                                         clearCount MEMBER clearCount FINAL)
    Q_PROPERTY(int                                         tests      MEMBER tests      FINAL)
    Q_PROPERTY(QList<pinpoint::charts::RelationsDriftItem> items      MEMBER items      FINAL)
    Q_PROPERTY(pinpoint::charts::RelationsSentence         sentence   MEMBER sentence   FINAL)
public:
    int clearCount = 0, tests = 0;
    QList<RelationsDriftItem> items;
    RelationsSentence sentence;
    friend bool operator==(const RelationsDrifts &, const RelationsDrifts &) = default;
};

// `relations`. `facet` is a RelationsFacet or null.
struct RelationsRelations
{
    Q_GADGET
    Q_PROPERTY(bool                                              available  MEMBER available  FINAL)
    Q_PROPERTY(pinpoint::charts::RelationsSentence               refusal    MEMBER refusal    FINAL)
    Q_PROPERTY(QString                                           upId       MEMBER upId       FINAL)
    Q_PROPERTY(QString                                           acrossId   MEMBER acrossId   FINAL)
    Q_PROPERTY(pinpoint::charts::RelationsBrief                  up         MEMBER up         FINAL)
    Q_PROPERTY(pinpoint::charts::RelationsBrief                  across     MEMBER across     FINAL)
    Q_PROPERTY(QList<double>                                     upValues   MEMBER upValues   FINAL)
    Q_PROPERTY(QList<pinpoint::charts::RelationsCatalogueGroup>  catalogue  MEMBER catalogue  FINAL)
    Q_PROPERTY(pinpoint::charts::RelationsSentence               head       MEMBER head       FINAL)
    Q_PROPERTY(pinpoint::charts::RelationsSentence               short      MEMBER shortText  FINAL)
    Q_PROPERTY(pinpoint::charts::RelationsSentence               standfirst MEMBER standfirst FINAL)
    Q_PROPERTY(pinpoint::charts::RelationsChance                 chance     MEMBER chance     FINAL)
    Q_PROPERTY(pinpoint::charts::RelationsScatter                scatter    MEMBER scatter    FINAL)
    Q_PROPERTY(pinpoint::charts::RelationsReadingRun             upRun      MEMBER upRun      FINAL)
    Q_PROPERTY(QList<pinpoint::charts::RelationsLedgerRow>       ledger     MEMBER ledger     FINAL)
    Q_PROPERTY(QString                                           restText   MEMBER restText   FINAL)
    Q_PROPERTY(pinpoint::charts::RelationsSentence               ledgerNote MEMBER ledgerNote FINAL)
    Q_PROPERTY(QList<pinpoint::charts::RelationsFacetChoice>     facets     MEMBER facets     FINAL)
    Q_PROPERTY(QVariant                                          facet      MEMBER facet      FINAL)
    Q_PROPERTY(pinpoint::charts::RelationsMatrix                 matrix     MEMBER matrix     FINAL)
    Q_PROPERTY(pinpoint::charts::RelationsDrifts                 drifts     MEMBER drifts     FINAL)
    Q_PROPERTY(QList<pinpoint::charts::RelationsAsk>             asks       MEMBER asks       FINAL)
public:
    bool              available = false;
    RelationsSentence refusal;
    QString           upId, acrossId;
    RelationsBrief    up, across;
    QList<double>     upValues;
    QList<RelationsCatalogueGroup> catalogue;
    RelationsSentence head, shortText, standfirst;
    RelationsChance   chance;
    RelationsScatter  scatter;
    RelationsReadingRun upRun;
    QList<RelationsLedgerRow> ledger;
    QString           restText;
    RelationsSentence ledgerNote;
    QList<RelationsFacetChoice> facets;
    QVariant          facet = relationsNull();
    RelationsMatrix   matrix;
    RelationsDrifts   drifts;
    QList<RelationsAsk> asks;
    friend bool operator==(const RelationsRelations &, const RelationsRelations &) = default;
};

// ── Kinds ──────────────────────────────────────────────────────────────────────────────────────

struct RelationsClubAgreement
{
    Q_GADGET
    Q_PROPERTY(int agree   MEMBER agree   FINAL)
    Q_PROPERTY(int grouped MEMBER grouped FINAL)
public:
    int agree = 0, grouped = 0;
    friend bool operator==(const RelationsClubAgreement &, const RelationsClubAgreement &) = default;
};

// A kind (or a club, or the neutral "not measured" set, `neutral` true with its `text`).
struct RelationsKindGroup
{
    Q_GADGET
    Q_PROPERTY(QString                                   name       MEMBER name       FINAL)
    Q_PROPERTY(int                                       n          MEMBER n          FINAL)
    Q_PROPERTY(QList<pinpoint::charts::RelationsMember>  members    MEMBER members    FINAL)
    Q_PROPERTY(int                                       medoid     MEMBER medoid     FINAL)
    Q_PROPERTY(QString                                   medoidText MEMBER medoidText FINAL)
    Q_PROPERTY(bool                                      neutral    MEMBER neutral    FINAL)
    Q_PROPERTY(QString                                   text       MEMBER text       FINAL)
public:
    QString name;
    int     n = 0;
    QList<RelationsMember> members;
    int     medoid = 0;
    QString medoidText;
    bool    neutral = false;
    QString text;
    friend bool operator==(const RelationsKindGroup &, const RelationsKindGroup &) = default;
};

struct RelationsSeparationRow
{
    Q_GADGET
    Q_PROPERTY(QString                          readingId MEMBER readingId FINAL)
    Q_PROPERTY(QString                          key       MEMBER key       FINAL)
    Q_PROPERTY(QString                          label     MEMBER label     FINAL)
    Q_PROPERTY(QString                          tag       MEMBER tag       FINAL)
    Q_PROPERTY(bool                             same      MEMBER same      FINAL)
    Q_PROPERTY(QString                          sameMark  MEMBER sameMark  FINAL)
    Q_PROPERTY(double                           d         MEMBER d         FINAL)
    Q_PROPERTY(QString                          dText     MEMBER dText     FINAL)
    Q_PROPERTY(bool                             clear     MEMBER clear     FINAL)
    Q_PROPERTY(QString                          medAText  MEMBER medAText  FINAL)
    Q_PROPERTY(QString                          medBText  MEMBER medBText  FINAL)
    Q_PROPERTY(QString                          unitText  MEMBER unitText  FINAL)
    Q_PROPERTY(pinpoint::charts::RelationsRange scale     MEMBER scale     FINAL)
    Q_PROPERTY(pinpoint::charts::RelationsBox   boxA      MEMBER boxA      FINAL)
    Q_PROPERTY(pinpoint::charts::RelationsBox   boxB      MEMBER boxB      FINAL)
    Q_PROPERTY(QList<double>                    valuesA   MEMBER valuesA   FINAL)
    Q_PROPERTY(QList<double>                    valuesB   MEMBER valuesB   FINAL)
public:
    QString readingId, key, label, tag;
    bool    same = false;
    QString sameMark;
    double  d = 0;
    QString dText;
    bool    clear = false;
    QString medAText, medBText, unitText;
    RelationsRange scale;
    RelationsBox   boxA, boxB;
    QList<double>  valuesA, valuesB;
    friend bool operator==(const RelationsSeparationRow &, const RelationsSeparationRow &) = default;
};

struct RelationsSeparation
{
    Q_GADGET
    Q_PROPERTY(double                                          dCritical     MEMBER dCritical     FINAL)
    Q_PROPERTY(QString                                         dCriticalText MEMBER dCriticalText FINAL)
    Q_PROPERTY(pinpoint::charts::RelationsSentence             note          MEMBER note          FINAL)
    Q_PROPERTY(QList<pinpoint::charts::RelationsSeparationRow> rows          MEMBER rows          FINAL)
public:
    double  dCritical = 0;
    QString dCriticalText;
    RelationsSentence note;
    QList<RelationsSeparationRow> rows;
    friend bool operator==(const RelationsSeparation &, const RelationsSeparation &) = default;
};

// A canonical P-position on a kind's curve. `impact`: it is impact, which the curve marks heavier (refactor 5.5c: the
// QML asked its tag for "P7").
struct RelationsKindPhase
{
    Q_GADGET
    Q_PROPERTY(int     phase  MEMBER phase  FINAL)
    Q_PROPERTY(QString tag    MEMBER tag    FINAL)
    Q_PROPERTY(double  tMs    MEMBER tMs    FINAL)
    Q_PROPERTY(bool    impact MEMBER impact FINAL)
public:
    int     phase = 0;
    QString tag;
    double  tMs = 0;
    bool    impact = false;
    friend bool operator==(const RelationsKindPhase &, const RelationsKindPhase &) = default;
};

// One shot's measured stretches under a kind's curve: `runs` [[t0, t1]].
struct RelationsCover
{
    Q_GADGET
    Q_PROPERTY(int                  ordinal MEMBER ordinal FINAL)
    Q_PROPERTY(bool                 picked  MEMBER picked  FINAL)
    Q_PROPERTY(double               frac    MEMBER frac    FINAL)
    Q_PROPERTY(QList<QList<double>> runs    MEMBER runs    FINAL)
public:
    int     ordinal = 0;
    bool    picked = false;
    double  frac = 0;
    QList<QList<double>> runs;
    friend bool operator==(const RelationsCover &, const RelationsCover &) = default;
};

// A kind's fitted curve: `v`, `sd`, `lo`, `hi` NaN where blank.
struct RelationsKindFit
{
    Q_GADGET
    Q_PROPERTY(QString                                 name         MEMBER name         FINAL)
    Q_PROPERTY(int                                     n            MEMBER n            FINAL)
    Q_PROPERTY(int                                     contributing MEMBER contributing FINAL)
    Q_PROPERTY(QString                                 label        MEMBER label        FINAL)
    Q_PROPERTY(bool                                    refused      MEMBER refused      FINAL)
    Q_PROPERTY(QString                                 refusal      MEMBER refusal      FINAL)
    Q_PROPERTY(QList<double>                           t            MEMBER t            FINAL)
    Q_PROPERTY(QList<double>                           v            MEMBER v            FINAL)
    Q_PROPERTY(QList<double>                           sd           MEMBER sd           FINAL)
    Q_PROPERTY(QList<double>                           lo           MEMBER lo           FINAL)
    Q_PROPERTY(QList<double>                           hi           MEMBER hi           FINAL)
    Q_PROPERTY(int                                     drawn        MEMBER drawn        FINAL)
    Q_PROPERTY(QList<int>                              nShots       MEMBER nShots       FINAL)
    Q_PROPERTY(QList<pinpoint::charts::RelationsCover> cover        MEMBER cover        FINAL)
public:
    QString name;
    int     n = 0, contributing = 0;
    QString label;
    bool    refused = false;
    QString refusal;
    QList<double> t, v, sd, lo, hi;
    int     drawn = 0;
    QList<int> nShots;
    QList<RelationsCover> cover;
    friend bool operator==(const RelationsKindFit &, const RelationsKindFit &) = default;
};

// Each kind's curve of one reading. `window` a RelationsWindow, or null when no fit has a value.
struct RelationsKindCurve
{
    Q_GADGET
    Q_PROPERTY(QString                                     readingId MEMBER readingId FINAL)
    Q_PROPERTY(QString                                     key       MEMBER key       FINAL)
    Q_PROPERTY(QString                                     label     MEMBER label     FINAL)
    Q_PROPERTY(QString                                     unit      MEMBER unit      FINAL)
    Q_PROPERTY(QString                                     unitText  MEMBER unitText  FINAL)
    Q_PROPERTY(QVariant                                    window    MEMBER window    FINAL)
    Q_PROPERTY(pinpoint::charts::RelationsRange            yRange    MEMBER yRange    FINAL)
    Q_PROPERTY(QList<pinpoint::charts::RelationsKindPhase> phases    MEMBER phases    FINAL)
    Q_PROPERTY(QList<pinpoint::charts::RelationsKindFit>   fits      MEMBER fits      FINAL)
public:
    QString  readingId, key, label, unit, unitText;
    QVariant window = relationsNull();
    RelationsRange yRange;
    QList<RelationsKindPhase> phases;
    QList<RelationsKindFit>   fits;
    friend bool operator==(const RelationsKindCurve &, const RelationsKindCurve &) = default;
};

struct RelationsShapeGroup
{
    Q_GADGET
    Q_PROPERTY(QString    name     MEMBER name     FINAL)
    Q_PROPERTY(QString    label    MEMBER label    FINAL)
    Q_PROPERTY(int        medoid   MEMBER medoid   FINAL)
    Q_PROPERTY(int        n        MEMBER n        FINAL)
    Q_PROPERTY(QList<int> ordinals MEMBER ordinals FINAL)
public:
    QString    name, label;
    int        medoid = 0, n = 0;
    QList<int> ordinals;
    friend bool operator==(const RelationsShapeGroup &, const RelationsShapeGroup &) = default;
};

struct RelationsShapes
{
    Q_GADGET
    Q_PROPERTY(bool                                        found    MEMBER found    FINAL)
    Q_PROPERTY(pinpoint::charts::RelationsSentence         sentence MEMBER sentence FINAL)
    Q_PROPERTY(QList<pinpoint::charts::RelationsShapeGroup> groups  MEMBER groups   FINAL)
public:
    bool found = false;
    RelationsSentence sentence;
    QList<RelationsShapeGroup> groups;
    friend bool operator==(const RelationsShapes &, const RelationsShapes &) = default;
};

// `kinds`. `withinClub` is a RelationsWithinClub (below: the kinds within the largest club, when the kinds are the
// clubs and it has six shots) or null.
struct RelationsKinds
{
    Q_GADGET
    Q_PROPERTY(bool                                           available      MEMBER available      FINAL)
    Q_PROPERTY(pinpoint::charts::RelationsSentence            refusal        MEMBER refusal        FINAL)
    Q_PROPERTY(QString                                        kicker         MEMBER kicker         FINAL)
    Q_PROPERTY(QString                                        method         MEMBER method         FINAL)
    Q_PROPERTY(bool                                           structure      MEMBER structure      FINAL)
    Q_PROPERTY(QVariant                                       silhouette     MEMBER silhouette     FINAL)
    Q_PROPERTY(QString                                        silhouetteText MEMBER silhouetteText FINAL)
    Q_PROPERTY(int                                            readingsUsed   MEMBER readingsUsed   FINAL)
    Q_PROPERTY(int                                            minSize        MEMBER minSize        FINAL)
    Q_PROPERTY(QStringList                                    names          MEMBER names          FINAL)
    Q_PROPERTY(pinpoint::charts::RelationsClubAgreement       clubAgreement  MEMBER clubAgreement  FINAL)
    Q_PROPERTY(QString                                        clubsNote      MEMBER clubsNote      FINAL)
    Q_PROPERTY(pinpoint::charts::RelationsSentence            head           MEMBER head           FINAL)
    Q_PROPERTY(pinpoint::charts::RelationsSentence            standfirst     MEMBER standfirst     FINAL)
    Q_PROPERTY(pinpoint::charts::RelationsSentence            splitLine      MEMBER splitLine      FINAL)
    Q_PROPERTY(QString                                        pickedKind     MEMBER pickedKind     FINAL)
    Q_PROPERTY(QString                                        pickedSentence MEMBER pickedSentence FINAL)
    Q_PROPERTY(QList<pinpoint::charts::RelationsKindGroup>    groups         MEMBER groups         FINAL)
    Q_PROPERTY(int                                            unplacedCount  MEMBER unplacedCount  FINAL)
    Q_PROPERTY(pinpoint::charts::RelationsSeparation          separation     MEMBER separation     FINAL)
    Q_PROPERTY(QList<pinpoint::charts::RelationsKindCurve>    curves         MEMBER curves         FINAL)
    Q_PROPERTY(pinpoint::charts::RelationsSentence            alignNote      MEMBER alignNote      FINAL)
    Q_PROPERTY(pinpoint::charts::RelationsShapes              shapes         MEMBER shapes         FINAL)
    Q_PROPERTY(QString                                        fitNote        MEMBER fitNote        FINAL)
    Q_PROPERTY(QString                                        methodNote     MEMBER methodNote     FINAL)
    Q_PROPERTY(QVariant                                       withinClub     MEMBER withinClub     FINAL)
public:
    bool              available = false;
    RelationsSentence refusal;
    QString           kicker, method;
    bool              structure = false;
    QVariant          silhouette = relationsNull();
    QString           silhouetteText;
    int               readingsUsed = 0, minSize = 0;
    QStringList       names;
    RelationsClubAgreement clubAgreement;
    QString           clubsNote;
    RelationsSentence head, standfirst, splitLine;
    QString           pickedKind, pickedSentence;
    QList<RelationsKindGroup> groups;
    int               unplacedCount = 0;
    RelationsSeparation separation;
    QList<RelationsKindCurve> curves;
    RelationsSentence alignNote;
    RelationsShapes   shapes;
    QString           fitNote, methodNote;
    QVariant          withinClub = relationsNull();
    friend bool operator==(const RelationsKinds &, const RelationsKinds &) = default;
};

struct RelationsWithinClub
{
    Q_GADGET
    Q_PROPERTY(QString                             club     MEMBER club     FINAL)
    Q_PROPERTY(int                                 n        MEMBER n        FINAL)
    Q_PROPERTY(pinpoint::charts::RelationsSentence sentence MEMBER sentence FINAL)
    Q_PROPERTY(pinpoint::charts::RelationsKinds    kinds    MEMBER kinds    FINAL)
public:
    QString           club;
    int               n = 0;
    RelationsSentence sentence;
    RelationsKinds    kinds;
    friend bool operator==(const RelationsWithinClub &, const RelationsWithinClub &) = default;
};

// ── Replay Session ─────────────────────────────────────────────────────────────────────────────

struct RelationsReplayPhase
{
    Q_GADGET
    Q_PROPERTY(int     phase   MEMBER phase   FINAL)
    Q_PROPERTY(QString tag     MEMBER tag     FINAL)
    Q_PROPERTY(qint64  seekUs  MEMBER seekUs  FINAL)
    Q_PROPERTY(bool    current MEMBER current FINAL)
public:
    int     phase = 0;
    QString tag;
    qint64  seekUs = -1;
    bool    current = false;
    friend bool operator==(const RelationsReplayPhase &, const RelationsReplayPhase &) = default;
};

// This shot now: `typicality` a RelationsVariability ("typicality") or null.
struct RelationsReplayNow
{
    Q_GADGET
    Q_PROPERTY(QString                             valueText    MEMBER valueText    FINAL)
    Q_PROPERTY(QString                             trustSubject MEMBER trustSubject FINAL)
    Q_PROPERTY(QVariant                            typicality   MEMBER typicality   FINAL)
    Q_PROPERTY(QString                             medianText   MEMBER medianText   FINAL)
    Q_PROPERTY(QString                             place        MEMBER place        FINAL)
    Q_PROPERTY(pinpoint::charts::RelationsSentence sentence     MEMBER sentence     FINAL)
public:
    QString  valueText, trustSubject;
    QVariant typicality = relationsNull();
    QString  medianText, place;
    RelationsSentence sentence;
    friend bool operator==(const RelationsReplayNow &, const RelationsReplayNow &) = default;
};

// The strongest partner of the reading that is not the same quantity.
struct RelationsPartner
{
    Q_GADGET
    Q_PROPERTY(QString                            readingId MEMBER readingId FINAL)
    Q_PROPERTY(QString                            label     MEMBER label     FINAL)
    Q_PROPERTY(QString                            tag       MEMBER tag       FINAL)
    Q_PROPERTY(QString                            rText     MEMBER rText     FINAL)
    Q_PROPERTY(pinpoint::charts::RelationsScatter scatter   MEMBER scatter   FINAL)
public:
    QString readingId, label, tag, rText;
    RelationsScatter scatter;
    friend bool operator==(const RelationsPartner &, const RelationsPartner &) = default;
};

// One of Replay's three partners, said as a sentence.
struct RelationsPartnerRow
{
    Q_GADGET
    Q_PROPERTY(QString                             readingId MEMBER readingId FINAL)
    Q_PROPERTY(QString                             label     MEMBER label     FINAL)
    Q_PROPERTY(QString                             tag       MEMBER tag       FINAL)
    Q_PROPERTY(bool                                same      MEMBER same      FINAL)
    Q_PROPERTY(QString                             sameMark  MEMBER sameMark  FINAL)
    Q_PROPERTY(double                              r         MEMBER r         FINAL)
    Q_PROPERTY(QString                             rText     MEMBER rText     FINAL)
    Q_PROPERTY(double                              lo        MEMBER lo        FINAL)
    Q_PROPERTY(double                              hi        MEMBER hi        FINAL)
    Q_PROPERTY(QString                             ciText    MEMBER ciText    FINAL)
    Q_PROPERTY(int                                 n         MEMBER n         FINAL)
    Q_PROPERTY(bool                                clear     MEMBER clear     FINAL)
    Q_PROPERTY(pinpoint::charts::RelationsSentence sentence  MEMBER sentence  FINAL)
public:
    QString readingId, label, tag;
    bool    same = false;
    QString sameMark;
    double  r = 0;
    QString rText;
    double  lo = 0, hi = 0;
    QString ciText;
    int     n = 0;
    bool    clear = false;
    RelationsSentence sentence;
    friend bool operator==(const RelationsPartnerRow &, const RelationsPartnerRow &) = default;
};

// `replaySession`: "" readingId when there is none. `partner` a RelationsPartner or null.
struct RelationsReplaySession
{
    Q_GADGET
    Q_PROPERTY(QString                                      readingId MEMBER readingId FINAL)
    Q_PROPERTY(QString                                      key       MEMBER key       FINAL)
    Q_PROPERTY(QString                                      label     MEMBER label     FINAL)
    Q_PROPERTY(QString                                      tag       MEMBER tag       FINAL)
    Q_PROPERTY(QString                                      phaseName MEMBER phaseName FINAL)
    Q_PROPERTY(pinpoint::charts::RelationsSentence          head      MEMBER head      FINAL)
    Q_PROPERTY(QString                                      follows   MEMBER follows   FINAL)
    Q_PROPERTY(QList<pinpoint::charts::RelationsReplayPhase> phases   MEMBER phases    FINAL)
    Q_PROPERTY(pinpoint::charts::RelationsReadingRun        run       MEMBER run       FINAL)
    Q_PROPERTY(pinpoint::charts::RelationsReplayNow         now       MEMBER now       FINAL)
    Q_PROPERTY(QVariant                                     partner   MEMBER partner   FINAL)
    Q_PROPERTY(QList<pinpoint::charts::RelationsPartnerRow> movesWith MEMBER movesWith FINAL)
    Q_PROPERTY(pinpoint::charts::RelationsSentence          note      MEMBER note      FINAL)
public:
    QString readingId, key, label, tag, phaseName;
    RelationsSentence head;
    QString follows;
    QList<RelationsReplayPhase> phases;
    RelationsReadingRun run;
    RelationsReplayNow  now;
    QVariant partner = relationsNull();
    QList<RelationsPartnerRow> movesWith;
    RelationsSentence note;
    friend bool operator==(const RelationsReplaySession &, const RelationsReplaySession &) = default;
};

// `playhead`: the open row's and the lead trace's value at the playhead, as text.
struct RelationsPlayhead
{
    Q_GADGET
    Q_PROPERTY(QString openValueText MEMBER openValueText FINAL)
    Q_PROPERTY(QString leadValueText MEMBER leadValueText FINAL)
    Q_PROPERTY(int     replayIndex   MEMBER replayIndex   FINAL)
public:
    QString openValueText, leadValueText;
    int     replayIndex = -1;
    friend bool operator==(const RelationsPlayhead &, const RelationsPlayhead &) = default;
};

} // namespace pinpoint::charts
