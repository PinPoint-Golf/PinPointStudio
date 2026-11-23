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

// THE POSTURE PRESENTER'S TYPED RESULTS (refactor 5.5c, lint A6): what ChartsPosturePresenter hands QML — choices,
// thisShot, session, standfirst — as Q_GADGET values. Each property is named as the old map's key, so the QML reads them
// as it read the maps; the shapes are the ones charts_posture_presenter.h documents (OUTPUT SHAPES). The picture strip's
// own gadgets are posture_picture.h's (postureui::Picture), which the Club delivery targets compile without this presenter.
//
// THE DIFFERENCES A GADGET MAKES, and how each keeps the page as it was:
//   · a missing number was null: it is a QVariant holding null (never an invalid QVariant, which reads `undefined`);
//   · an object that was null (a cell's faultHi / faultLo, an address row's history, a grade's result) is a QVariant
//     holding the gadget or null;
//   · a per-shot list of numbers with gaps (a box cell's and an address row's `values`) is a list of doubles, NaN for an
//     unmeasured shot — the one reader (PpChBoxCell.valueOf) takes null and NaN alike;
//   · This shot's rows had `boxes: null` and no `grades` / `rowGrades`: they are empty lists (the profile reads them on
//     Session only, and `r.boxes ? r.boxes : []` reads an empty list as it read null);
//   · a field the map left out (an unavailable thisShot or session) is present at its default; every QML reader tests
//     `available` first.
// A consistency result is the `variabilityResult` value type QML has always been handed (VariabilityResultView).
//
// The types are in their own namespace: session_posture.h owns the core's Posture* structs.
// moc reaches this header through charts_posture_presenter.cpp's #include of its moc file.

#include "posture_picture.h"                            // postureui::Picture, postureui::nullValue
#include "../variability/variability_facade.h"          // VariabilityResultView: a consistency grade

#include <QList>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QtGlobal>

namespace pinpoint::charts::postureui {

// ── words ──────────────────────────────────────────────────────────────────────────────────────

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
    Q_PROPERTY(QString    text   MEMBER text   FINAL)
    Q_PROPERTY(QList<pinpoint::charts::postureui::Run> runs MEMBER runs FINAL)
    Q_PROPERTY(QString    clause MEMBER clause FINAL)
public:
    QString    text;
    QList<Run> runs;
    QString    clause;
    friend bool operator==(const Sentence &, const Sentence &) = default;
};

// ── choices ────────────────────────────────────────────────────────────────────────────────────

struct ScopeChoice
{
    Q_GADGET
    Q_PROPERTY(QString id      MEMBER id      FINAL)
    Q_PROPERTY(QString label   MEMBER label   FINAL)
    Q_PROPERTY(bool    current MEMBER current FINAL)
public:
    QString id, label;
    bool    current = false;
    friend bool operator==(const ScopeChoice &, const ScopeChoice &) = default;
};

struct AgainstOption
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
    friend bool operator==(const AgainstOption &, const AgainstOption &) = default;
};

struct PatternChoice
{
    Q_GADGET
    Q_PROPERTY(int     id      MEMBER id      FINAL)
    Q_PROPERTY(QString label   MEMBER label   FINAL)
    Q_PROPERTY(bool    current MEMBER current FINAL)
public:
    int     id = -1;
    QString label;
    bool    current = false;
    friend bool operator==(const PatternChoice &, const PatternChoice &) = default;
};

// The ★ reference swing, as the Against choice and the ★ link read it.
struct Reference
{
    Q_GADGET
    Q_PROPERTY(bool    set        MEMBER set        FINAL)
    Q_PROPERTY(QString swingDir   MEMBER swingDir   FINAL)
    Q_PROPERTY(QString label      MEMBER label      FINAL)
    Q_PROPERTY(bool    isThisShot MEMBER isThisShot FINAL)
    Q_PROPERTY(QString linkText   MEMBER linkText   FINAL)
    Q_PROPERTY(bool    canSet     MEMBER canSet     FINAL)
public:
    bool    set = false;
    QString swingDir, label;
    bool    isThisShot = false;
    QString linkText;
    bool    canSet = false;
    friend bool operator==(const Reference &, const Reference &) = default;
};

struct Choices
{
    Q_GADGET
    Q_PROPERTY(QString     scope           MEMBER scope           FINAL)
    Q_PROPERTY(QList<pinpoint::charts::postureui::ScopeChoice> scopes MEMBER scopes FINAL)
    Q_PROPERTY(QString     against         MEMBER against         FINAL)
    Q_PROPERTY(QList<pinpoint::charts::postureui::AgainstOption> againstOptions MEMBER againstOptions FINAL)
    Q_PROPERTY(int         pattern         MEMBER pattern         FINAL)
    Q_PROPERTY(QList<pinpoint::charts::postureui::PatternChoice> patterns MEMBER patterns FINAL)
    Q_PROPERTY(QVariant    patternResolved MEMBER patternResolved FINAL)   // 0 | 1 | null
    Q_PROPERTY(QString     patternAfter    MEMBER patternAfter    FINAL)
    Q_PROPERTY(pinpoint::charts::postureui::Reference reference MEMBER reference FINAL)
    Q_PROPERTY(QString     summary         MEMBER summary         FINAL)
    Q_PROPERTY(QStringList onShowKeys      MEMBER onShowKeys      FINAL)
public:
    QString              scope;
    QList<ScopeChoice>   scopes;
    QString              against;
    QList<AgainstOption> againstOptions;
    int                  pattern = -1;        // −1 Auto, as PpChSection.autoChoice read a missing one
    QList<PatternChoice> patterns;
    QVariant             patternResolved = nullValue();
    QString              patternAfter;
    Reference            reference;
    QString              summary;
    QStringList          onShowKeys;
    friend bool operator==(const Choices &, const Choices &) = default;
};

// ── the profile ────────────────────────────────────────────────────────────────────────────────

// POS
struct Position
{
    Q_GADGET
    Q_PROPERTY(int     index     MEMBER index     FINAL)
    Q_PROPERTY(QString tag       MEMBER tag       FINAL)
    Q_PROPERTY(int     phase     MEMBER phase     FINAL)
    Q_PROPERTY(QString name      MEMBER name      FINAL)
    Q_PROPERTY(bool    available MEMBER available FINAL)
    Q_PROPERTY(qint64  tUs       MEMBER tUs       FINAL)
    Q_PROPERTY(qint64  seekUs    MEMBER seekUs    FINAL)
public:
    int     index = 0;
    QString tag;
    int     phase = -1;
    QString name;
    bool    available = false;
    qint64  tUs = -1, seekUs = -1;
    friend bool operator==(const Position &, const Position &) = default;
};

// FAULT
struct Fault
{
    Q_GADGET
    Q_PROPERTY(QVariant edge        MEMBER edge        FINAL)
    Q_PROPERTY(QString  edgeText    MEMBER edgeText    FINAL)
    Q_PROPERTY(QString  conditionId MEMBER conditionId FINAL)
    Q_PROPERTY(QString  label       MEMBER label       FINAL)
public:
    QVariant edge = nullValue();
    QString  edgeText, conditionId, label;
    friend bool operator==(const Fault &, const Fault &) = default;
};

// CELL
struct Cell
{
    Q_GADGET
    Q_PROPERTY(bool     alt           MEMBER alt           FINAL)
    Q_PROPERTY(bool     altAvailable  MEMBER altAvailable  FINAL)
    Q_PROPERTY(QVariant altValue      MEMBER altValue      FINAL)
    Q_PROPERTY(QString  altLabel      MEMBER altLabel      FINAL)
    Q_PROPERTY(QString  altFigure     MEMBER altFigure     FINAL)
    Q_PROPERTY(QString  altText       MEMBER altText       FINAL)
    Q_PROPERTY(QString  altWarn       MEMBER altWarn       FINAL)
    Q_PROPERTY(QString  altWarnShort  MEMBER altWarnShort  FINAL)
    Q_PROPERTY(int      index         MEMBER index         FINAL)
    Q_PROPERTY(QString  tag           MEMBER tag           FINAL)
    Q_PROPERTY(bool     available     MEMBER available     FINAL)
    Q_PROPERTY(QVariant value         MEMBER value         FINAL)
    Q_PROPERTY(QString  text          MEMBER text          FINAL)
    Q_PROPERTY(QString  valueText     MEMBER valueText     FINAL)
    Q_PROPERTY(bool     warned        MEMBER warned        FINAL)
    Q_PROPERTY(QString  warn          MEMBER warn          FINAL)
    Q_PROPERTY(QString  warnReason    MEMBER warnReason    FINAL)
    Q_PROPERTY(QString  warnShort     MEMBER warnShort     FINAL)
    Q_PROPERTY(QString  gateWords     MEMBER gateWords     FINAL)
    Q_PROPERTY(bool     counted       MEMBER counted       FINAL)
    Q_PROPERTY(bool     banded        MEMBER banded        FINAL)
    Q_PROPERTY(QVariant bandLo        MEMBER bandLo        FINAL)
    Q_PROPERTY(QVariant bandHi        MEMBER bandHi        FINAL)
    Q_PROPERTY(QVariant bandMu        MEMBER bandMu        FINAL)
    Q_PROPERTY(QVariant watchLo       MEMBER watchLo       FINAL)
    Q_PROPERTY(QVariant watchHi       MEMBER watchHi       FINAL)
    Q_PROPERTY(QString  bandContext   MEMBER bandContext   FINAL)
    Q_PROPERTY(bool     bandInherited MEMBER bandInherited FINAL)
    Q_PROPERTY(QString  bandSource    MEMBER bandSource    FINAL)
    Q_PROPERTY(QString  wash          MEMBER wash          FINAL)
    Q_PROPERTY(QVariant z             MEMBER z             FINAL)
    Q_PROPERTY(bool     outside       MEMBER outside       FINAL)
    Q_PROPERTY(bool     ring          MEMBER ring          FINAL)
    Q_PROPERTY(QString  ringWord      MEMBER ringWord      FINAL)
    Q_PROPERTY(bool     exit          MEMBER exit          FINAL)
    Q_PROPERTY(bool     beyondFault   MEMBER beyondFault   FINAL)
    Q_PROPERTY(QVariant faultHi       MEMBER faultHi       FINAL)   // Fault | null
    Q_PROPERTY(QVariant faultLo       MEMBER faultLo       FINAL)   // Fault | null
    Q_PROPERTY(int      firedShots    MEMBER firedShots    FINAL)
    Q_PROPERTY(QString  note          MEMBER note          FINAL)
    Q_PROPERTY(QString  mark          MEMBER mark          FINAL)
    Q_PROPERTY(QVariant ghost         MEMBER ghost         FINAL)
    Q_PROPERTY(QString  ghostText     MEMBER ghostText     FINAL)
    Q_PROPERTY(qint64   seekUs        MEMBER seekUs        FINAL)
    Q_PROPERTY(QVariant picked        MEMBER picked        FINAL)
    Q_PROPERTY(QString  pickedText    MEMBER pickedText    FINAL)
public:
    bool     alt = false, altAvailable = false;
    QVariant altValue = nullValue();
    QString  altLabel, altFigure, altText, altWarn, altWarnShort;
    int      index = 0;
    QString  tag;
    bool     available = false;
    QVariant value = nullValue();
    QString  text, valueText;
    bool     warned = false;
    QString  warn, warnReason, warnShort, gateWords;
    bool     counted = false, banded = false;
    QVariant bandLo = nullValue(), bandHi = nullValue(), bandMu = nullValue(), watchLo = nullValue(), watchHi = nullValue();
    QString  bandContext;
    bool     bandInherited = false;
    QString  bandSource, wash;
    QVariant z = nullValue();
    bool     outside = false, ring = false;
    QString  ringWord;
    bool     exit = false, beyondFault = false;
    QVariant faultHi = nullValue(), faultLo = nullValue();
    int      firedShots = 0;
    QString  note, mark;
    QVariant ghost = nullValue();
    QString  ghostText;
    qint64   seekUs = -1;
    QVariant picked = nullValue();
    QString  pickedText;
    friend bool operator==(const Cell &, const Cell &) = default;
};

// BOX (box_map.h's shape): its outliers are ordinals.
struct BoxTexts
{
    Q_GADGET
    Q_PROPERTY(QString lo  MEMBER lo  FINAL)
    Q_PROPERTY(QString q1  MEMBER q1  FINAL)
    Q_PROPERTY(QString med MEMBER med FINAL)
    Q_PROPERTY(QString q3  MEMBER q3  FINAL)
    Q_PROPERTY(QString hi  MEMBER hi  FINAL)
public:
    QString lo, q1, med, q3, hi;
    friend bool operator==(const BoxTexts &, const BoxTexts &) = default;
};

struct Box
{
    Q_GADGET
    Q_PROPERTY(int        n        MEMBER n        FINAL)
    Q_PROPERTY(QVariant   min      MEMBER min      FINAL)
    Q_PROPERTY(QVariant   max      MEMBER max      FINAL)
    Q_PROPERTY(QVariant   q1       MEMBER q1       FINAL)
    Q_PROPERTY(QVariant   med      MEMBER med      FINAL)
    Q_PROPERTY(QVariant   q3       MEMBER q3       FINAL)
    Q_PROPERTY(QVariant   lo       MEMBER lo       FINAL)
    Q_PROPERTY(QVariant   hi       MEMBER hi       FINAL)
    Q_PROPERTY(QList<int> outliers MEMBER outliers FINAL)
    Q_PROPERTY(pinpoint::charts::postureui::BoxTexts texts MEMBER texts FINAL)
public:
    int        n = 0;
    QVariant   min = nullValue(), max = nullValue(), q1 = nullValue(), med = nullValue(), q3 = nullValue();
    QVariant   lo = nullValue(), hi = nullValue();
    QList<int> outliers;
    BoxTexts   texts;                         // empty with no shot (PpChBoxCell draws only n > 0)
    friend bool operator==(const Box &, const Box &) = default;
};

// BOXCELL (Session)
struct BoxCell
{
    Q_GADGET
    Q_PROPERTY(pinpoint::charts::postureui::Box box MEMBER box FINAL)
    Q_PROPERTY(int           n          MEMBER n          FINAL)
    Q_PROPERTY(QVariant      median     MEMBER median     FINAL)
    Q_PROPERTY(QString       medianText MEMBER medianText FINAL)
    Q_PROPERTY(QVariant      wide       MEMBER wide       FINAL)
    Q_PROPERTY(QString       wideText   MEMBER wideText   FINAL)
    Q_PROPERTY(QVariant      picked     MEMBER picked     FINAL)
    Q_PROPERTY(QList<double> values     MEMBER values     FINAL)   // per shot, index = ordinal − 1; NaN unmeasured
    Q_PROPERTY(bool          graded     MEMBER graded     FINAL)
public:
    Box           box;
    int           n = 0;
    QVariant      median = nullValue();
    QString       medianText;
    QVariant      wide = nullValue();
    QString       wideText;
    QVariant      picked = nullValue();
    QList<double> values;
    bool          graded = false;
    friend bool operator==(const BoxCell &, const BoxCell &) = default;
};

// GRADES: a club's graded position on a row.
struct GradeCell
{
    Q_GADGET
    Q_PROPERTY(int     index MEMBER index FINAL)
    Q_PROPERTY(QString tag   MEMBER tag   FINAL)
    Q_PROPERTY(pinpoint::variability::VariabilityResultView result MEMBER result FINAL)
public:
    int     index = 0;
    QString tag;
    pinpoint::variability::VariabilityResultView result;
    friend bool operator==(const GradeCell &, const GradeCell &) = default;
};

struct Grade
{
    Q_GADGET
    Q_PROPERTY(QString  club   MEMBER club   FINAL)
    Q_PROPERTY(bool     named  MEMBER named  FINAL)
    Q_PROPERTY(bool     tooFew MEMBER tooFew FINAL)
    Q_PROPERTY(QVariant result MEMBER result FINAL)   // variabilityResult (tooFew) | null
    Q_PROPERTY(QList<pinpoint::charts::postureui::GradeCell> cells MEMBER cells FINAL)
public:
    QString          club;
    bool             named = false, tooFew = false;
    QVariant         result = nullValue();
    QList<GradeCell> cells;
    friend bool operator==(const Grade &, const Grade &) = default;
};

// ROWGRADES (R20): the row's one grade per club.
struct RowGrade
{
    Q_GADGET
    Q_PROPERTY(QString club  MEMBER club  FINAL)
    Q_PROPERTY(bool    named MEMBER named FINAL)
    Q_PROPERTY(pinpoint::variability::VariabilityResultView result MEMBER result FINAL)
public:
    QString club;
    bool    named = false;
    pinpoint::variability::VariabilityResultView result;
    friend bool operator==(const RowGrade &, const RowGrade &) = default;
};

// ROW
struct Row
{
    Q_GADGET
    Q_PROPERTY(QString key        MEMBER key        FINAL)
    Q_PROPERTY(QString label      MEMBER label      FINAL)
    Q_PROPERTY(QString metricKey  MEMBER metricKey  FINAL)
    Q_PROPERTY(QString hueKey     MEMBER hueKey     FINAL)
    Q_PROPERTY(QString camera     MEMBER camera     FINAL)
    Q_PROPERTY(QString unit       MEMBER unit       FINAL)
    Q_PROPERTY(int     decimals   MEMBER decimals   FINAL)
    Q_PROPERTY(QString poleWord   MEMBER poleWord   FINAL)
    Q_PROPERTY(QString poleText   MEMBER poleText   FINAL)
    Q_PROPERTY(QString plusWord   MEMBER plusWord   FINAL)
    Q_PROPERTY(QString minusWord  MEMBER minusWord  FINAL)
    Q_PROPERTY(bool    present    MEMBER present    FINAL)
    Q_PROPERTY(bool    style      MEMBER style      FINAL)
    Q_PROPERTY(QString styleWord  MEMBER styleWord  FINAL)
    Q_PROPERTY(QString styleText  MEMBER styleText  FINAL)
    Q_PROPERTY(int     outCount   MEMBER outCount   FINAL)
    Q_PROPERTY(bool    hasAlt     MEMBER hasAlt     FINAL)
    Q_PROPERTY(double  yLo        MEMBER yLo        FINAL)
    Q_PROPERTY(double  yHi        MEMBER yHi        FINAL)
    Q_PROPERTY(QList<pinpoint::charts::postureui::Cell> cells MEMBER cells FINAL)
    Q_PROPERTY(QList<pinpoint::charts::postureui::BoxCell> boxes MEMBER boxes FINAL)            // Session only
    Q_PROPERTY(QList<pinpoint::charts::postureui::Grade> grades MEMBER grades FINAL)            // Session only
    Q_PROPERTY(QList<pinpoint::charts::postureui::RowGrade> rowGrades MEMBER rowGrades FINAL)   // Session only
public:
    QString key, label, metricKey, hueKey, camera, unit;
    int     decimals = 0;
    QString poleWord, poleText, plusWord, minusWord;
    bool    present = false, style = false;
    QString styleWord, styleText;
    int     outCount = 0;
    bool    hasAlt = false;
    double  yLo = 0, yHi = 0;
    QList<Cell>     cells;
    QList<BoxCell>  boxes;
    QList<Grade>    grades;
    QList<RowGrade> rowGrades;
    friend bool operator==(const Row &, const Row &) = default;
};

// This shot's ("ghostWord", "ghostRefusal": "" on Session) and the session's.
struct Profile
{
    Q_GADGET
    Q_PROPERTY(QList<pinpoint::charts::postureui::Position> positions MEMBER positions FINAL)
    Q_PROPERTY(QList<pinpoint::charts::postureui::Row> rows MEMBER rows FINAL)
    Q_PROPERTY(QString ghostWord    MEMBER ghostWord    FINAL)
    Q_PROPERTY(QString ghostRefusal MEMBER ghostRefusal FINAL)
    Q_PROPERTY(QString sub          MEMBER sub          FINAL)
    Q_PROPERTY(QString caption      MEMBER caption      FINAL)
public:
    QList<Position> positions;
    QList<Row>      rows;
    QString         ghostWord, ghostRefusal, sub, caption;
    friend bool operator==(const Profile &, const Profile &) = default;
};

// ── the pattern ────────────────────────────────────────────────────────────────────────────────

struct Trail
{
    Q_GADGET
    Q_PROPERTY(bool     known       MEMBER known       FINAL)
    Q_PROPERTY(bool     kept        MEMBER kept        FINAL)
    Q_PROPERTY(QString  word        MEMBER word        FINAL)
    Q_PROPERTY(QString  words       MEMBER words       FINAL)
    Q_PROPERTY(QVariant changeAtTop MEMBER changeAtTop FINAL)
    Q_PROPERTY(QString  changeText  MEMBER changeText  FINAL)
public:
    bool     known = false, kept = false;
    QString  word, words;
    QVariant changeAtTop = nullValue();
    QString  changeText;
    friend bool operator==(const Trail &, const Trail &) = default;
};

// MATCH
struct Match
{
    Q_GADGET
    Q_PROPERTY(bool        ok             MEMBER ok             FINAL)
    Q_PROPERTY(QVariant    nearest        MEMBER nearest        FINAL)   // 0 | 1 | null
    Q_PROPERTY(QString     nearestName    MEMBER nearestName    FINAL)
    Q_PROPERTY(QString     fit            MEMBER fit            FINAL)
    Q_PROPERTY(QString     fitWords       MEMBER fitWords       FINAL)
    Q_PROPERTY(QString     basis          MEMBER basis          FINAL)
    Q_PROPERTY(QString     basisWords     MEMBER basisWords     FINAL)
    Q_PROPERTY(QVariant    dLevel         MEMBER dLevel         FINAL)
    Q_PROPERTY(QVariant    dSquatPush     MEMBER dSquatPush     FINAL)
    Q_PROPERTY(QString     dLevelText     MEMBER dLevelText     FINAL)
    Q_PROPERTY(QString     dSquatPushText MEMBER dSquatPushText FINAL)
    Q_PROPERTY(QVariant    gap            MEMBER gap            FINAL)
    Q_PROPERTY(int         cells          MEMBER cells          FINAL)
    Q_PROPERTY(QStringList rows           MEMBER rows           FINAL)
public:
    bool        ok = false;
    QVariant    nearest = nullValue();
    QString     nearestName, fit, fitWords, basis, basisWords;
    QVariant    dLevel = nullValue(), dSquatPush = nullValue();
    QString     dLevelText, dSquatPushText;
    QVariant    gap = nullValue();
    int         cells = 0;
    QStringList rows;
    friend bool operator==(const Match &, const Match &) = default;
};

// PATTERN: the MATCH fields of the match the line speaks for, then its own (no gadget inheritance: a reflected map
// reads only a class's own properties).
struct Pattern
{
    Q_GADGET
    Q_PROPERTY(bool        ok             MEMBER ok             FINAL)
    Q_PROPERTY(QVariant    nearest        MEMBER nearest        FINAL)
    Q_PROPERTY(QString     nearestName    MEMBER nearestName    FINAL)
    Q_PROPERTY(QString     fit            MEMBER fit            FINAL)
    Q_PROPERTY(QString     fitWords       MEMBER fitWords       FINAL)
    Q_PROPERTY(QString     basis          MEMBER basis          FINAL)
    Q_PROPERTY(QString     basisWords     MEMBER basisWords     FINAL)
    Q_PROPERTY(QVariant    dLevel         MEMBER dLevel         FINAL)
    Q_PROPERTY(QVariant    dSquatPush     MEMBER dSquatPush     FINAL)
    Q_PROPERTY(QString     dLevelText     MEMBER dLevelText     FINAL)
    Q_PROPERTY(QString     dSquatPushText MEMBER dSquatPushText FINAL)
    Q_PROPERTY(QVariant    gap            MEMBER gap            FINAL)
    Q_PROPERTY(int         cells          MEMBER cells          FINAL)
    Q_PROPERTY(QStringList rows           MEMBER rows           FINAL)
    Q_PROPERTY(int         mode           MEMBER mode           FINAL)
    Q_PROPERTY(int         pattern        MEMBER pattern        FINAL)
    Q_PROPERTY(QString     name           MEMBER name           FINAL)
    Q_PROPERTY(QString     source         MEMBER source         FINAL)
    Q_PROPERTY(QString     sourceWords    MEMBER sourceWords    FINAL)
    Q_PROPERTY(QString     line           MEMBER line           FINAL)
    Q_PROPERTY(pinpoint::charts::postureui::Match own   MEMBER own   FINAL)
    Q_PROPERTY(pinpoint::charts::postureui::Trail trail MEMBER trail FINAL)
public:
    bool        ok = false;
    QVariant    nearest = nullValue();
    QString     nearestName, fit, fitWords, basis, basisWords;
    QVariant    dLevel = nullValue(), dSquatPush = nullValue();
    QString     dLevelText, dSquatPushText;
    QVariant    gap = nullValue();
    int         cells = 0;
    QStringList rows;
    int         mode = -1, pattern = 0;
    QString     name, source, sourceWords, line;
    Match       own;
    Trail       trail;
    friend bool operator==(const Pattern &, const Pattern &) = default;
};

// ── exits, departures, what moved ──────────────────────────────────────────────────────────────

// EXIT
struct Exit
{
    Q_GADGET
    Q_PROPERTY(QString     row          MEMBER row          FINAL)
    Q_PROPERTY(QString     rowLabel     MEMBER rowLabel     FINAL)
    Q_PROPERTY(int         pos          MEMBER pos          FINAL)
    Q_PROPERTY(QString     tag          MEMBER tag          FINAL)
    Q_PROPERTY(QVariant    value        MEMBER value        FINAL)
    Q_PROPERTY(QString     valueText    MEMBER valueText    FINAL)
    Q_PROPERTY(QVariant    z            MEMBER z            FINAL)
    Q_PROPERTY(QString     zText        MEMBER zText        FINAL)
    Q_PROPERTY(QString     departure    MEMBER departure    FINAL)
    Q_PROPERTY(QString     name         MEMBER name         FINAL)
    Q_PROPERTY(QStringList conditionIds MEMBER conditionIds FINAL)
    Q_PROPERTY(bool        named        MEMBER named        FINAL)
    Q_PROPERTY(QString     words        MEMBER words        FINAL)
    Q_PROPERTY(qint64      seekUs       MEMBER seekUs       FINAL)
public:
    QString     row, rowLabel;
    int         pos = 0;
    QString     tag;
    QVariant    value = nullValue();
    QString     valueText;
    QVariant    z = nullValue();
    QString     zText, departure, name;
    QStringList conditionIds;
    bool        named = false;
    QString     words;
    qint64      seekUs = -1;
    friend bool operator==(const Exit &, const Exit &) = default;
};

// DEPARTURE (This shot)
struct Departure
{
    Q_GADGET
    Q_PROPERTY(QString     id            MEMBER id            FINAL)
    Q_PROPERTY(QString     name          MEMBER name          FINAL)
    Q_PROPERTY(QStringList conditionIds  MEMBER conditionIds  FINAL)
    Q_PROPERTY(QStringList positions     MEMBER positions     FINAL)
    Q_PROPERTY(QString     positionsText MEMBER positionsText FINAL)
    Q_PROPERTY(QString     firstTag      MEMBER firstTag      FINAL)
    Q_PROPERTY(qint64      seekUs        MEMBER seekUs        FINAL)
public:
    QString     id, name;
    QStringList conditionIds, positions;
    QString     positionsText, firstTag;
    qint64      seekUs = -1;
    friend bool operator==(const Departure &, const Departure &) = default;
};

// MOVED
struct Moved
{
    Q_GADGET
    Q_PROPERTY(int     pos         MEMBER pos         FINAL)
    Q_PROPERTY(QString tag         MEMBER tag         FINAL)
    Q_PROPERTY(QString when        MEMBER when        FINAL)
    Q_PROPERTY(QString line        MEMBER line        FINAL)
    Q_PROPERTY(QString head        MEMBER head        FINAL)
    Q_PROPERTY(QString bend        MEMBER bend        FINAL)
    Q_PROPERTY(QString attribution MEMBER attribution FINAL)
    Q_PROPERTY(bool    hipsIn      MEMBER hipsIn      FINAL)
    Q_PROPERTY(bool    hipsWarned  MEMBER hipsWarned  FINAL)
    Q_PROPERTY(bool    bendWarned  MEMBER bendWarned  FINAL)
    Q_PROPERTY(qint64  seekUs      MEMBER seekUs      FINAL)
public:
    int     pos = 0;
    QString tag, when, line, head, bend, attribution;
    bool    hipsIn = false, hipsWarned = false, bendWarned = false;
    qint64  seekUs = -1;
    friend bool operator==(const Moved &, const Moved &) = default;
};

struct WhatMoved
{
    Q_GADGET
    Q_PROPERTY(pinpoint::charts::postureui::Moved top    MEMBER top    FINAL)
    Q_PROPERTY(pinpoint::charts::postureui::Moved impact MEMBER impact FINAL)
public:
    Moved top, impact;
    friend bool operator==(const WhatMoved &, const WhatMoved &) = default;
};

// FINDING (Findings and Working well)
struct Finding
{
    Q_GADGET
    Q_PROPERTY(QString     id            MEMBER id            FINAL)
    Q_PROPERTY(QString     name          MEMBER name          FINAL)
    Q_PROPERTY(QString     kind          MEMBER kind          FINAL)
    Q_PROPERTY(QString     departure     MEMBER departure     FINAL)
    Q_PROPERTY(QString     departureName MEMBER departureName FINAL)
    Q_PROPERTY(QString     severity      MEMBER severity      FINAL)
    Q_PROPERTY(QString     severityWord  MEMBER severityWord  FINAL)
    Q_PROPERTY(QString     tier          MEMBER tier          FINAL)
    Q_PROPERTY(QStringList positions     MEMBER positions     FINAL)
    Q_PROPERTY(QString     positionsText MEMBER positionsText FINAL)
    Q_PROPERTY(QString     tag           MEMBER tag           FINAL)
    Q_PROPERTY(qint64      seekUs        MEMBER seekUs        FINAL)
    Q_PROPERTY(QString     seekText      MEMBER seekText      FINAL)
    Q_PROPERTY(QVariant    value         MEMBER value         FINAL)
    Q_PROPERTY(QString     valueText     MEMBER valueText     FINAL)
    Q_PROPERTY(QVariant    z             MEMBER z             FINAL)
    Q_PROPERTY(QString     explanation   MEMBER explanation   FINAL)
    Q_PROPERTY(QString     protect       MEMBER protect       FINAL)
public:
    QString     id, name, kind, departure, departureName, severity, severityWord, tier;
    QStringList positions;
    QString     positionsText, tag;
    qint64      seekUs = -1;
    QString     seekText;
    QVariant    value = nullValue();
    QString     valueText;
    QVariant    z = nullValue();
    QString     explanation, protect;
    friend bool operator==(const Finding &, const Finding &) = default;
};

// ── at address, and the trust fold ─────────────────────────────────────────────────────────────

// ADDRESS (= ABSOLUTE)
struct AddressRow
{
    Q_GADGET
    Q_PROPERTY(QString       measureId       MEMBER measureId       FINAL)
    Q_PROPERTY(QString       label           MEMBER label           FINAL)
    Q_PROPERTY(QString       measureLabel    MEMBER measureLabel    FINAL)
    Q_PROPERTY(QString       unit            MEMBER unit            FINAL)
    Q_PROPERTY(QString       camera          MEMBER camera          FINAL)
    Q_PROPERTY(QString       cameraKey       MEMBER cameraKey       FINAL)
    Q_PROPERTY(QString       metricKey       MEMBER metricKey       FINAL)
    Q_PROPERTY(QString       hueKey          MEMBER hueKey          FINAL)
    Q_PROPERTY(QVariant      value           MEMBER value           FINAL)
    Q_PROPERTY(QString       text            MEMBER text            FINAL)
    Q_PROPERTY(QString       valueWord       MEMBER valueWord       FINAL)
    Q_PROPERTY(bool          banded          MEMBER banded          FINAL)
    Q_PROPERTY(QVariant      bandLo          MEMBER bandLo          FINAL)
    Q_PROPERTY(QVariant      bandHi          MEMBER bandHi          FINAL)
    Q_PROPERTY(QVariant      bandMu          MEMBER bandMu          FINAL)
    Q_PROPERTY(QString       bandText        MEMBER bandText        FINAL)
    Q_PROPERTY(QString       bandSource      MEMBER bandSource      FINAL)
    Q_PROPERTY(QString       bandContext     MEMBER bandContext     FINAL)
    Q_PROPERTY(bool          bandInherited   MEMBER bandInherited   FINAL)
    Q_PROPERTY(pinpoint::charts::postureui::Box box MEMBER box FINAL)
    Q_PROPERTY(int           boxN            MEMBER boxN            FINAL)
    Q_PROPERTY(QString       boxText         MEMBER boxText         FINAL)
    Q_PROPERTY(QList<double> values          MEMBER values          FINAL)   // per shot, index = ordinal − 1; NaN none
    Q_PROPERTY(QVariant      dot             MEMBER dot             FINAL)
    Q_PROPERTY(QVariant      history         MEMBER history         FINAL)   // Box | null
    Q_PROPERTY(int           historyN        MEMBER historyN        FINAL)
    Q_PROPERTY(int           historySessions MEMBER historySessions FINAL)
    Q_PROPERTY(bool          hasHistory      MEMBER hasHistory      FINAL)
    Q_PROPERTY(QString       historyText     MEMBER historyText     FINAL)
    Q_PROPERTY(QString       historyWhy      MEMBER historyWhy      FINAL)
    Q_PROPERTY(bool          warned          MEMBER warned          FINAL)
    Q_PROPERTY(QString       warn            MEMBER warn            FINAL)
    Q_PROPERTY(QString       warnShort       MEMBER warnShort       FINAL)
    Q_PROPERTY(double        xLo             MEMBER xLo             FINAL)
    Q_PROPERTY(double        xHi             MEMBER xHi             FINAL)
    Q_PROPERTY(QString       trust           MEMBER trust           FINAL)
public:
    QString       measureId, label, measureLabel, unit, camera, cameraKey, metricKey, hueKey;
    QVariant      value = nullValue();
    QString       text, valueWord;
    bool          banded = false;
    QVariant      bandLo = nullValue(), bandHi = nullValue(), bandMu = nullValue();
    QString       bandText, bandSource, bandContext;
    bool          bandInherited = false;
    Box           box;
    int           boxN = 0;
    QString       boxText;
    QList<double> values;
    QVariant      dot = nullValue();
    QVariant      history = nullValue();
    int           historyN = 0, historySessions = 0;
    bool          hasHistory = false;
    QString       historyText, historyWhy;
    bool          warned = false;
    QString       warn, warnShort;
    double        xLo = 0, xHi = 1;
    QString       trust;
    friend bool operator==(const AddressRow &, const AddressRow &) = default;
};

// ADDRESSBLOCK
struct AddressBlock
{
    Q_GADGET
    Q_PROPERTY(bool        available MEMBER available FINAL)
    Q_PROPERTY(QString     title     MEMBER title     FINAL)
    Q_PROPERTY(QString     sub       MEMBER sub       FINAL)
    Q_PROPERTY(QList<pinpoint::charts::postureui::AddressRow> rows MEMBER rows FINAL)
    Q_PROPERTY(QString     legend    MEMBER legend    FINAL)
    Q_PROPERTY(QStringList lines     MEMBER lines     FINAL)
public:
    bool              available = false;
    QString           title, sub;
    QList<AddressRow> rows;
    QString           legend;
    QStringList       lines;
    friend bool operator==(const AddressBlock &, const AddressBlock &) = default;
};

// TRUST
struct Trust
{
    Q_GADGET
    Q_PROPERTY(QStringList lines       MEMBER lines       FINAL)
    Q_PROPERTY(QStringList cameras     MEMBER cameras     FINAL)
    Q_PROPERTY(QStringList absent      MEMBER absent      FINAL)
    Q_PROPERTY(QStringList warnings    MEMBER warnings    FINAL)
    Q_PROPERTY(QString     bandSources MEMBER bandSources FINAL)
    Q_PROPERTY(QString     trunkDatum  MEMBER trunkDatum  FINAL)
    Q_PROPERTY(QString     rigRule     MEMBER rigRule     FINAL)
public:
    QStringList lines, cameras, absent, warnings;
    QString     bandSources, trunkDatum, rigRule;
    friend bool operator==(const Trust &, const Trust &) = default;
};

// ── This shot ──────────────────────────────────────────────────────────────────────────────────

struct ThisShot
{
    Q_GADGET
    Q_PROPERTY(bool    available        MEMBER available        FINAL)
    Q_PROPERTY(QString empty            MEMBER empty            FINAL)
    Q_PROPERTY(int     ordinal          MEMBER ordinal          FINAL)
    Q_PROPERTY(int     shotId           MEMBER shotId           FINAL)
    Q_PROPERTY(QString swingDir         MEMBER swingDir         FINAL)
    Q_PROPERTY(bool    inSession        MEMBER inSession        FINAL)
    Q_PROPERTY(QString kicker           MEMBER kicker           FINAL)
    Q_PROPERTY(bool    graded           MEMBER graded           FINAL)
    Q_PROPERTY(pinpoint::charts::postureui::Sentence head MEMBER head FINAL)
    Q_PROPERTY(QString headText         MEMBER headText         FINAL)
    Q_PROPERTY(pinpoint::charts::postureui::Pattern pattern MEMBER pattern FINAL)
    Q_PROPERTY(QString patternLine      MEMBER patternLine      FINAL)
    Q_PROPERTY(pinpoint::charts::postureui::Profile profile MEMBER profile FINAL)
    Q_PROPERTY(QList<pinpoint::charts::postureui::Exit> exits MEMBER exits FINAL)
    Q_PROPERTY(QList<pinpoint::charts::postureui::Departure> departures MEMBER departures FINAL)
    Q_PROPERTY(pinpoint::charts::postureui::WhatMoved whatMoved MEMBER whatMoved FINAL)
    Q_PROPERTY(QList<pinpoint::charts::postureui::Finding> findings MEMBER findings FINAL)
    Q_PROPERTY(QList<pinpoint::charts::postureui::Finding> workingWell MEMBER workingWell FINAL)
    Q_PROPERTY(QString findingsEmpty    MEMBER findingsEmpty    FINAL)
    Q_PROPERTY(QString workingWellEmpty MEMBER workingWellEmpty FINAL)
    Q_PROPERTY(pinpoint::charts::postureui::AddressBlock address MEMBER address FINAL)
    Q_PROPERTY(QList<pinpoint::charts::postureui::AddressRow> absolutes MEMBER absolutes FINAL)   // = address.rows
    Q_PROPERTY(pinpoint::charts::postureui::Trust trust MEMBER trust FINAL)
    Q_PROPERTY(pinpoint::charts::postureui::Picture picture MEMBER picture FINAL)
    Q_PROPERTY(QList<pinpoint::charts::postureui::Sentence> standfirsts MEMBER standfirsts FINAL)
public:
    bool              available = false;
    QString           empty;
    int               ordinal = 0, shotId = -1;
    QString           swingDir;
    bool              inSession = false;
    QString           kicker;
    bool              graded = false;
    Sentence          head;
    QString           headText;
    Pattern           pattern;
    QString           patternLine;
    Profile           profile;
    QList<Exit>       exits;
    QList<Departure>  departures;
    WhatMoved         whatMoved;
    QList<Finding>    findings, workingWell;
    QString           findingsEmpty, workingWellEmpty;
    AddressBlock      address;
    QList<AddressRow> absolutes;
    Trust             trust;
    Picture           picture;
    QList<Sentence>   standfirsts;
    friend bool operator==(const ThisShot &, const ThisShot &) = default;
};

// ── Session ────────────────────────────────────────────────────────────────────────────────────

// The pattern by shot: one mark per shot.
struct StripMark
{
    Q_GADGET
    Q_PROPERTY(int      ordinal     MEMBER ordinal     FINAL)
    Q_PROPERTY(int      shotId      MEMBER shotId      FINAL)
    Q_PROPERTY(QString  swingDir    MEMBER swingDir    FINAL)
    Q_PROPERTY(bool     ok          MEMBER ok          FINAL)
    Q_PROPERTY(QVariant nearest     MEMBER nearest     FINAL)   // 0 | 1 | null
    Q_PROPERTY(QString  nearestName MEMBER nearestName FINAL)
    Q_PROPERTY(bool     between     MEMBER between     FINAL)
    Q_PROPERTY(bool     differs     MEMBER differs     FINAL)
    Q_PROPERTY(bool     picked      MEMBER picked      FINAL)
    Q_PROPERTY(QVariant dLevel      MEMBER dLevel      FINAL)
    Q_PROPERTY(QVariant dSquatPush  MEMBER dSquatPush  FINAL)
    Q_PROPERTY(QString  word        MEMBER word        FINAL)
    Q_PROPERTY(QString  label       MEMBER label       FINAL)
public:
    int      ordinal = 0, shotId = -1;
    QString  swingDir;
    bool     ok = false;
    QVariant nearest = nullValue();
    QString  nearestName;
    bool     between = false, differs = false, picked = false;
    QVariant dLevel = nullValue(), dSquatPush = nullValue();
    QString  word, label;
    friend bool operator==(const StripMark &, const StripMark &) = default;
};

struct Strip
{
    Q_GADGET
    Q_PROPERTY(QList<pinpoint::charts::postureui::StripMark> marks MEMBER marks FINAL)
    Q_PROPERTY(int     onPattern MEMBER onPattern FINAL)
    Q_PROPERTY(QString line      MEMBER line      FINAL)
    Q_PROPERTY(QString legend    MEMBER legend    FINAL)
public:
    QList<StripMark> marks;
    int              onPattern = 0;
    QString          line, legend;
    friend bool operator==(const Strip &, const Strip &) = default;
};

struct DepartureMark
{
    Q_GADGET
    Q_PROPERTY(int     ordinal  MEMBER ordinal  FINAL)
    Q_PROPERTY(QString swingDir MEMBER swingDir FINAL)
public:
    int     ordinal = 0;
    QString swingDir;
    friend bool operator==(const DepartureMark &, const DepartureMark &) = default;
};

// A departure counted across the session's shots.
struct SessionDeparture
{
    Q_GADGET
    Q_PROPERTY(QString     id           MEMBER id           FINAL)
    Q_PROPERTY(QString     name         MEMBER name         FINAL)
    Q_PROPERTY(QStringList conditionIds MEMBER conditionIds FINAL)
    Q_PROPERTY(int         shots        MEMBER shots        FINAL)
    Q_PROPERTY(QString     countText    MEMBER countText    FINAL)
    Q_PROPERTY(QList<int>  ordinals     MEMBER ordinals     FINAL)
    Q_PROPERTY(QList<pinpoint::charts::postureui::DepartureMark> marks MEMBER marks FINAL)
    Q_PROPERTY(QString     firstTag     MEMBER firstTag     FINAL)
    Q_PROPERTY(QString     line         MEMBER line         FINAL)
    Q_PROPERTY(QString     detail       MEMBER detail       FINAL)
public:
    QString              id, name;
    QStringList          conditionIds;
    int                  shots = 0;
    QString              countText;
    QList<int>           ordinals;
    QList<DepartureMark> marks;
    QString              firstTag, line, detail;
    friend bool operator==(const SessionDeparture &, const SessionDeparture &) = default;
};

// consistency.lines: per club, its most and least consistent cell (each a variabilityResult, or null).
struct ConsistencyLine
{
    Q_GADGET
    Q_PROPERTY(QString  club      MEMBER club      FINAL)
    Q_PROPERTY(bool     named     MEMBER named     FINAL)
    Q_PROPERTY(bool     tooFew    MEMBER tooFew    FINAL)
    Q_PROPERTY(QVariant result    MEMBER result    FINAL)
    Q_PROPERTY(QString  mostText  MEMBER mostText  FINAL)
    Q_PROPERTY(QVariant most      MEMBER most      FINAL)
    Q_PROPERTY(QString  leastText MEMBER leastText FINAL)
    Q_PROPERTY(QVariant least     MEMBER least     FINAL)
public:
    QString  club;
    bool     named = false, tooFew = false;
    QVariant result = nullValue();
    QString  mostText;
    QVariant most = nullValue();
    QString  leastText;
    QVariant least = nullValue();
    friend bool operator==(const ConsistencyLine &, const ConsistencyLine &) = default;
};

struct Consistency
{
    Q_GADGET
    Q_PROPERTY(QList<pinpoint::charts::postureui::ConsistencyLine> lines MEMBER lines FINAL)
public:
    QList<ConsistencyLine> lines;
    friend bool operator==(const Consistency &, const Consistency &) = default;
};

struct Session
{
    Q_GADGET
    Q_PROPERTY(bool    available       MEMBER available       FINAL)
    Q_PROPERTY(int     n               MEMBER n               FINAL)
    Q_PROPERTY(int     shots           MEMBER shots           FINAL)
    Q_PROPERTY(bool    small           MEMBER isSmall         FINAL)   // the member is not `small`: a Windows header defines it
    Q_PROPERTY(QString kicker          MEMBER kicker          FINAL)
    Q_PROPERTY(bool    fromSession     MEMBER fromSession     FINAL)
    Q_PROPERTY(pinpoint::charts::postureui::Sentence head MEMBER head FINAL)
    Q_PROPERTY(QString headText        MEMBER headText        FINAL)
    Q_PROPERTY(pinpoint::charts::postureui::Pattern pattern MEMBER pattern FINAL)
    Q_PROPERTY(QString patternLine     MEMBER patternLine     FINAL)
    Q_PROPERTY(pinpoint::charts::postureui::Profile profile MEMBER profile FINAL)
    Q_PROPERTY(pinpoint::charts::postureui::Strip strip MEMBER strip FINAL)
    Q_PROPERTY(QList<pinpoint::charts::postureui::SessionDeparture> departures MEMBER departures FINAL)
    Q_PROPERTY(QString departuresEmpty MEMBER departuresEmpty FINAL)
    Q_PROPERTY(pinpoint::charts::postureui::Consistency consistency MEMBER consistency FINAL)
    Q_PROPERTY(pinpoint::charts::postureui::WhatMoved whatMoved MEMBER whatMoved FINAL)
    Q_PROPERTY(pinpoint::charts::postureui::AddressBlock address MEMBER address FINAL)
    Q_PROPERTY(QList<pinpoint::charts::postureui::AddressRow> absolutes MEMBER absolutes FINAL)   // = address.rows
    Q_PROPERTY(pinpoint::charts::postureui::Trust trust MEMBER trust FINAL)
public:
    bool                    available = false;
    int                     n = 0, shots = 0;
    bool                    isSmall = false;      // shots < 10
    QString                 kicker;
    bool                    fromSession = false;
    Sentence                head;
    QString                 headText;
    Pattern                 pattern;
    QString                 patternLine;
    Profile                 profile;
    Strip                   strip;
    QList<SessionDeparture> departures;
    QString                 departuresEmpty;
    Consistency             consistency;
    WhatMoved               whatMoved;
    AddressBlock            address;
    QList<AddressRow>       absolutes;
    Trust                   trust;
    friend bool operator==(const Session &, const Session &) = default;
};

} // namespace pinpoint::charts::postureui
