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

// THE WRIST PRESENTER'S TYPED RESULTS (refactor 5.5c, lint A6): what ChartsWristPresenter hands QML — choices,
// thisShot, session, standfirst — as Q_GADGET values. Each property is named as the old map's key, so the QML reads
// them as it read the maps; the shapes are the ones charts_wrist_presenter.h documents (OUTPUT SHAPES). `movesWith`
// is not here: it is the Relations presenter's ledger, typed with that presenter.
//
// THE DIFFERENCES A GADGET MAKES, and how each keeps the page as it was:
//   · a missing number was null: it is a QVariant holding null (never an invalid QVariant, which reads `undefined`);
//   · a grade that was null (a club's one grade when it has too few swings, Most / Least) is a QVariant holding the
//     VariabilityResultView or null;
//   · a list that was null (ROW.ghost with no ghost, ROW.boxes on This shot) is an empty list: its readers index it
//     (`gh[i]`, a Repeater) and treat the undefined they get as no value;
//   · a per-shot or per-checkpoint list of numbers with gaps (BOXCELL.values, ROW.ghost) is a list of doubles, NaN
//     where there is none — every reader takes null and NaN alike (PpChBoxCell.valueOf, PpTypeset.fin);
//   · ROW is one gadget for the strip and the numbers-only row: a numbers-only row's yLo / yHi stay null, so
//     PpChWristProfile falls back as it did; the fields a row did not carry are at their defaults, and its readers
//     branch on `numbersOnly` first;
//   · a field the map left out (an unavailable thisShot or session, a box of no shots) is present at its default;
//     every QML reader tests `available` (or the box's n) first.
//
// The rules the QML used to apply now live here, decided by the presenter: SCORE.foldText (the folded section's
// state, "score 84") and PROFILE.missingLead (the words before the DOFs a swing did not measure, the ones
// `missingText` opens with).
//
// The types are in their own namespace: the presenter's worker owns wristview::Profile, Cell and Dof.
// moc reaches this header through charts_wrist_presenter.cpp's #include of its moc file.

#include "../variability/variability_facade.h"     // VariabilityResultView: a checkpoint's consistency grade

#include <QList>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QtGlobal>

namespace pinpoint::charts::wristui {

inline QVariant nullValue() { return QVariant::fromValue(nullptr); }

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

// words::Sentence as QML takes it: its text, its runs and its first clause (the Session fold head's state).
struct Sentence
{
    Q_GADGET
    Q_PROPERTY(QString text MEMBER text FINAL)
    Q_PROPERTY(QList<pinpoint::charts::wristui::Run> runs MEMBER runs FINAL)
    Q_PROPERTY(QString clause MEMBER clause FINAL)
public:
    QString    text;
    QList<Run> runs;
    QString    clause;
    friend bool operator==(const Sentence &, const Sentence &) = default;
};

// TRUST: the lines said once, in order.
struct Trust
{
    Q_GADGET
    Q_PROPERTY(QStringList lines MEMBER lines FINAL)
public:
    QStringList lines;
    friend bool operator==(const Trust &, const Trust &) = default;
};

// ── choices ────────────────────────────────────────────────────────────────────────────────────

// A scope or a Curves behind option: { id, label, current }.
struct Option
{
    Q_GADGET
    Q_PROPERTY(QString id      MEMBER id      FINAL)
    Q_PROPERTY(QString label   MEMBER label   FINAL)
    Q_PROPERTY(bool    current MEMBER current FINAL)
public:
    QString id, label;
    bool    current = false;
    friend bool operator==(const Option &, const Option &) = default;
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

// An archetype: its id is a number (−1 Auto, 0..2), as PpChSection.autoChoice reads it.
struct ArchetypeOption
{
    Q_GADGET
    Q_PROPERTY(int     id      MEMBER id      FINAL)
    Q_PROPERTY(QString label   MEMBER label   FINAL)
    Q_PROPERTY(bool    current MEMBER current FINAL)
public:
    int     id = -1;
    QString label;
    bool    current = false;
    friend bool operator==(const ArchetypeOption &, const ArchetypeOption &) = default;
};

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
    Q_PROPERTY(QString scope MEMBER scope FINAL)
    Q_PROPERTY(QList<pinpoint::charts::wristui::Option> scopes MEMBER scopes FINAL)
    Q_PROPERTY(QString against MEMBER against FINAL)
    Q_PROPERTY(QList<pinpoint::charts::wristui::AgainstOption> againstOptions MEMBER againstOptions FINAL)
    Q_PROPERTY(bool curvesBehind MEMBER curvesBehind FINAL)
    Q_PROPERTY(QList<pinpoint::charts::wristui::Option> curves MEMBER curves FINAL)
    Q_PROPERTY(int archetype MEMBER archetype FINAL)
    Q_PROPERTY(QList<pinpoint::charts::wristui::ArchetypeOption> archetypes MEMBER archetypes FINAL)
    Q_PROPERTY(QString archetypeAfter MEMBER archetypeAfter FINAL)
    Q_PROPERTY(pinpoint::charts::wristui::Reference reference MEMBER reference FINAL)
    Q_PROPERTY(QString summary MEMBER summary FINAL)
public:
    QString                scope;
    QList<Option>          scopes;
    QString                against;
    QList<AgainstOption>   againstOptions;
    bool                   curvesBehind = false;
    QList<Option>          curves;
    int                    archetype = -1;   // Auto until the first result: PpChSection.autoChoice's fallback
    QList<ArchetypeOption> archetypes;
    QString                archetypeAfter;
    Reference              reference;
    QString                summary;
    friend bool operator==(const Choices &, const Choices &) = default;
};

// ── the profile ────────────────────────────────────────────────────────────────────────────────

// POS: a checkpoint column.
struct Position
{
    Q_GADGET
    Q_PROPERTY(int     index     MEMBER index     FINAL)
    Q_PROPERTY(QString tag       MEMBER tag       FINAL)
    Q_PROPERTY(int     phase     MEMBER phase     FINAL)
    Q_PROPERTY(QString name      MEMBER name      FINAL)
    Q_PROPERTY(QString note      MEMBER note      FINAL)
    Q_PROPERTY(bool    available MEMBER available FINAL)
    Q_PROPERTY(qint64  tUs       MEMBER tUs       FINAL)
    Q_PROPERTY(qint64  seekUs    MEMBER seekUs    FINAL)
public:
    int     index = 0;
    QString tag;
    int     phase = -1;
    QString name, note;
    bool    available = false;
    qint64  tUs = -1, seekUs = -1;
    friend bool operator==(const Position &, const Position &) = default;
};

// POINT: a strip's value at a checkpoint. `trust`: why a cell not graded yet is not ("" else).
struct Point
{
    Q_GADGET
    Q_PROPERTY(bool     available MEMBER available FINAL)
    Q_PROPERTY(QVariant value     MEMBER value     FINAL)
    Q_PROPERTY(QString  valueText MEMBER valueText FINAL)
    Q_PROPERTY(QString  rag       MEMBER rag       FINAL)
    Q_PROPERTY(bool     banded    MEMBER banded    FINAL)
    Q_PROPERTY(QVariant bandLo    MEMBER bandLo    FINAL)
    Q_PROPERTY(QVariant bandHi    MEMBER bandHi    FINAL)
    Q_PROPERTY(bool     out       MEMBER out       FINAL)
    Q_PROPERTY(bool     fault     MEMBER fault     FINAL)
    Q_PROPERTY(bool     watch     MEMBER watch     FINAL)
    Q_PROPERTY(QVariant outBy     MEMBER outBy     FINAL)
    Q_PROPERTY(QString  outPole   MEMBER outPole   FINAL)
    Q_PROPERTY(QString  word      MEMBER word      FINAL)
    Q_PROPERTY(QString  wordShort MEMBER wordShort FINAL)
    Q_PROPERTY(QString  trust     MEMBER trust     FINAL)
public:
    bool     available = false;
    QVariant value = nullValue();
    QString  valueText;
    QString  rag;
    bool     banded = false;
    QVariant bandLo = nullValue(), bandHi = nullValue();
    bool     out = false, fault = false, watch = false;
    QVariant outBy = nullValue();
    QString  outPole, word, wordShort, trust;
    friend bool operator==(const Point &, const Point &) = default;
};

// A numbers-only row's cell (the grid-only lead elbow).
struct NumberCell
{
    Q_GADGET
    Q_PROPERTY(bool     available MEMBER available FINAL)
    Q_PROPERTY(QVariant value     MEMBER value     FINAL)
    Q_PROPERTY(QString  valueText MEMBER valueText FINAL)
    Q_PROPERTY(QString  trust     MEMBER trust     FINAL)
public:
    bool     available = false;
    QVariant value = nullValue();
    QString  valueText, trust;
    friend bool operator==(const NumberCell &, const NumberCell &) = default;
};

// The curve behind: x the checkpoint index (0..7), v the Δ (null where the curve is not read).
struct TracePoint
{
    Q_GADGET
    Q_PROPERTY(double   x MEMBER x FINAL)
    Q_PROPERTY(QVariant v MEMBER v FINAL)
public:
    double   x = 0;
    QVariant v = nullValue();
    friend bool operator==(const TracePoint &, const TracePoint &) = default;
};

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

// box_map.h's BOX, typed: { n, min, max, q1, med, q3, lo, hi, outliers: [ordinal], texts }.
struct Box
{
    Q_GADGET
    Q_PROPERTY(int      n        MEMBER n        FINAL)
    Q_PROPERTY(QVariant min      MEMBER min      FINAL)
    Q_PROPERTY(QVariant max      MEMBER max      FINAL)
    Q_PROPERTY(QVariant q1       MEMBER q1       FINAL)
    Q_PROPERTY(QVariant med      MEMBER med      FINAL)
    Q_PROPERTY(QVariant q3       MEMBER q3       FINAL)
    Q_PROPERTY(QVariant lo       MEMBER lo       FINAL)
    Q_PROPERTY(QVariant hi       MEMBER hi       FINAL)
    Q_PROPERTY(QList<int> outliers MEMBER outliers FINAL)
    Q_PROPERTY(pinpoint::charts::wristui::BoxTexts texts MEMBER texts FINAL)
public:
    int        n = 0;
    QVariant   min = nullValue(), max = nullValue(), q1 = nullValue(), med = nullValue(), q3 = nullValue();
    QVariant   lo = nullValue(), hi = nullValue();
    QList<int> outliers;
    BoxTexts   texts;
    friend bool operator==(const Box &, const Box &) = default;
};

// BOXCELL: the session's box at a checkpoint. `values` per shot, NaN where a shot has none.
struct BoxCell
{
    Q_GADGET
    Q_PROPERTY(pinpoint::charts::wristui::Box box MEMBER box FINAL)
    Q_PROPERTY(QList<double> values     MEMBER values     FINAL)
    Q_PROPERTY(int           n          MEMBER n          FINAL)
    Q_PROPERTY(QVariant      median     MEMBER median     FINAL)
    Q_PROPERTY(QString       medianText MEMBER medianText FINAL)
    Q_PROPERTY(QVariant      wide       MEMBER wide       FINAL)
    Q_PROPERTY(QString       wideText   MEMBER wideText   FINAL)
    Q_PROPERTY(bool          graded     MEMBER graded     FINAL)
    Q_PROPERTY(QVariant      picked     MEMBER picked     FINAL)
public:
    Box           box;
    QList<double> values;
    int           n = 0;
    QVariant      median = nullValue();
    QString       medianText;
    QVariant      wide = nullValue();
    QString       wideText;
    bool          graded = false;
    QVariant      picked = nullValue();
    friend bool operator==(const BoxCell &, const BoxCell &) = default;
};

// GRADE's cell: one graded checkpoint's consistency for a club.
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

// GRADE: a club's consistency along a row. `result` the one grade-0 result of a club too few to grade, else null.
struct Grade
{
    Q_GADGET
    Q_PROPERTY(QString  club   MEMBER club   FINAL)
    Q_PROPERTY(bool     named  MEMBER named  FINAL)
    Q_PROPERTY(bool     tooFew MEMBER tooFew FINAL)
    Q_PROPERTY(QVariant result MEMBER result FINAL)
    Q_PROPERTY(QList<pinpoint::charts::wristui::GradeCell> cells MEMBER cells FINAL)
public:
    QString          club;
    bool             named = false, tooFew = false;
    QVariant         result = nullValue();
    QList<GradeCell> cells;
    friend bool operator==(const Grade &, const Grade &) = default;
};

// ROW.rowGrades: a club's one grade for the row (the report's badge, Mark's ruling R20).
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

// ROW: a strip (numbersOnly false: points, ghost, trace, boxes, the scale) or the numbers-only row (cells).
struct Row
{
    Q_GADGET
    Q_PROPERTY(bool    numbersOnly    MEMBER numbersOnly    FINAL)
    Q_PROPERTY(QString dof            MEMBER dof            FINAL)
    Q_PROPERTY(QString key            MEMBER key            FINAL)
    Q_PROPERTY(QString name           MEMBER name           FINAL)
    Q_PROPERTY(QString sub            MEMBER sub            FINAL)
    Q_PROPERTY(QString source         MEMBER source         FINAL)
    Q_PROPERTY(double  confidence     MEMBER confidence     FINAL)
    Q_PROPERTY(QString pips           MEMBER pips           FINAL)
    Q_PROPERTY(QString sourceText     MEMBER sourceText     FINAL)
    Q_PROPERTY(QString short          MEMBER shortText      FINAL)
    Q_PROPERTY(QString poleTop        MEMBER poleTop        FINAL)
    Q_PROPERTY(QString poleBottom     MEMBER poleBottom     FINAL)
    Q_PROPERTY(QString poleTopText    MEMBER poleTopText    FINAL)
    Q_PROPERTY(QString poleBottomText MEMBER poleBottomText FINAL)
    Q_PROPERTY(QList<pinpoint::charts::wristui::Point> points MEMBER points FINAL)
    Q_PROPERTY(QList<double> ghost MEMBER ghost FINAL)
    Q_PROPERTY(QList<pinpoint::charts::wristui::TracePoint> trace MEMBER trace FINAL)
    Q_PROPERTY(QList<pinpoint::charts::wristui::BoxCell> boxes MEMBER boxes FINAL)
    Q_PROPERTY(QVariant yLo      MEMBER yLo      FINAL)
    Q_PROPERTY(QVariant yHi      MEMBER yHi      FINAL)
    Q_PROPERTY(int      outCount MEMBER outCount FINAL)
    Q_PROPERTY(QList<pinpoint::charts::wristui::Grade> grades MEMBER grades FINAL)
    Q_PROPERTY(QList<pinpoint::charts::wristui::RowGrade> rowGrades MEMBER rowGrades FINAL)
    Q_PROPERTY(QList<pinpoint::charts::wristui::NumberCell> cells MEMBER cells FINAL)
public:
    bool              numbersOnly = false;
    QString           dof, key, name, sub, source;
    double            confidence = 0;
    QString           pips, sourceText, shortText;
    QString           poleTop, poleBottom, poleTopText, poleBottomText;
    QList<Point>      points;
    QList<double>     ghost;          // empty: no ghost; NaN at a checkpoint the ghost has no value
    QList<TracePoint> trace;
    QList<BoxCell>    boxes;          // Session only
    QVariant          yLo = nullValue(), yHi = nullValue();   // null on a numbers-only row
    int               outCount = 0;
    QList<Grade>      grades;         // Session only
    QList<RowGrade>   rowGrades;      // Session only
    QList<NumberCell> cells;          // a numbers-only row only
    friend bool operator==(const Row &, const Row &) = default;
};

// A DOF the swing did not measure: its name, a gate's words and the reading they open ("" none).
struct MissingPart
{
    Q_GADGET
    Q_PROPERTY(QString name    MEMBER name    FINAL)
    Q_PROPERTY(QString words   MEMBER words   FINAL)
    Q_PROPERTY(QString subject MEMBER subject FINAL)
public:
    QString name, words, subject;
    friend bool operator==(const MissingPart &, const MissingPart &) = default;
};

struct Profile
{
    Q_GADGET
    Q_PROPERTY(QList<pinpoint::charts::wristui::Position> positions MEMBER positions FINAL)
    Q_PROPERTY(QList<pinpoint::charts::wristui::Row> rows MEMBER rows FINAL)
    Q_PROPERTY(QString     ghostWord    MEMBER ghostWord    FINAL)
    Q_PROPERTY(QString     ghostRefusal MEMBER ghostRefusal FINAL)
    Q_PROPERTY(QString     sub          MEMBER sub          FINAL)
    Q_PROPERTY(QString     caption      MEMBER caption      FINAL)
    Q_PROPERTY(QStringList missing      MEMBER missing      FINAL)
    Q_PROPERTY(QList<pinpoint::charts::wristui::MissingPart> missingParts MEMBER missingParts FINAL)
    Q_PROPERTY(QString     missingLead  MEMBER missingLead  FINAL)
    Q_PROPERTY(QString     missingTail  MEMBER missingTail  FINAL)
    Q_PROPERTY(QString     missingText  MEMBER missingText  FINAL)
public:
    QList<Position>    positions;
    QList<Row>         rows;
    QString            ghostWord, ghostRefusal, sub, caption;
    QStringList        missing;
    QList<MissingPart> missingParts;
    QString            missingLead;   // "Not measured on this swing: " when a DOF is missing, else ""
    QString            missingTail, missingText;
    friend bool operator==(const Profile &, const Profile &) = default;
};

// ── This shot ──────────────────────────────────────────────────────────────────────────────────

struct Contribution
{
    Q_GADGET
    Q_PROPERTY(QString id          MEMBER id          FINAL)
    Q_PROPERTY(QString label       MEMBER label       FINAL)
    Q_PROPERTY(int     penalty     MEMBER penalty     FINAL)
    Q_PROPERTY(QString penaltyText MEMBER penaltyText FINAL)
public:
    QString id, label;
    int     penalty = 0;
    QString penaltyText;
    friend bool operator==(const Contribution &, const Contribution &) = default;
};

struct Score
{
    Q_GADGET
    Q_PROPERTY(QVariant score     MEMBER score     FINAL)
    Q_PROPERTY(QString  scoreText MEMBER scoreText FINAL)
    Q_PROPERTY(QString  band      MEMBER band      FINAL)
    Q_PROPERTY(int      base      MEMBER base      FINAL)
    Q_PROPERTY(QString  sumText   MEMBER sumText   FINAL)
    Q_PROPERTY(QList<pinpoint::charts::wristui::Contribution> contributions MEMBER contributions FINAL)
    Q_PROPERTY(QString  line      MEMBER line      FINAL)
    Q_PROPERTY(QString  foldText  MEMBER foldText  FINAL)
public:
    QVariant            score = nullValue();
    QString             scoreText, band;
    int                 base = 100;
    QString             sumText;
    QList<Contribution> contributions;
    QString             line;
    QString             foldText;   // the folded section's state: "score 84" | "score -"
    friend bool operator==(const Score &, const Score &) = default;
};

struct StyleItem
{
    Q_GADGET
    Q_PROPERTY(QString  key     MEMBER key     FINAL)
    Q_PROPERTY(QString  name    MEMBER name    FINAL)
    Q_PROPERTY(QVariant pct     MEMBER pct     FINAL)
    Q_PROPERTY(QString  pctText MEMBER pctText FINAL)
    Q_PROPERTY(bool     graded  MEMBER graded  FINAL)
public:
    QString  key, name;
    QVariant pct = nullValue();
    QString  pctText;
    bool     graded = false;
    friend bool operator==(const StyleItem &, const StyleItem &) = default;
};

struct Style
{
    Q_GADGET
    Q_PROPERTY(bool    hasIndex      MEMBER hasIndex      FINAL)
    Q_PROPERTY(int     archetype     MEMBER archetype     FINAL)
    Q_PROPERTY(QString archetypeName MEMBER archetypeName FINAL)
    Q_PROPERTY(int     mode          MEMBER mode          FINAL)
    Q_PROPERTY(bool    byHand        MEMBER byHand        FINAL)
    Q_PROPERTY(QList<pinpoint::charts::wristui::StyleItem> items MEMBER items FINAL)
    Q_PROPERTY(pinpoint::charts::wristui::Sentence line MEMBER line FINAL)
public:
    bool             hasIndex = false;
    int              archetype = 0;
    QString          archetypeName;
    int              mode = -1;
    bool             byHand = false;
    QList<StyleItem> items;
    Sentence         line;
    friend bool operator==(const Style &, const Style &) = default;
};

// A finding's open row: { k ("Coaching" | "Protect" | "Linked to" | "Corroborated by"), v }.
struct More
{
    Q_GADGET
    Q_PROPERTY(QString k MEMBER k FINAL)
    Q_PROPERTY(QString v MEMBER v FINAL)
public:
    QString k, v;
    friend bool operator==(const More &, const More &) = default;
};

// FINDING (a fired wrist condition) or a strength.
struct Finding
{
    Q_GADGET
    Q_PROPERTY(QString     id             MEMBER id             FINAL)
    Q_PROPERTY(QString     name           MEMBER name           FINAL)
    Q_PROPERTY(QString     severity       MEMBER severity       FINAL)
    Q_PROPERTY(QString     severityWord   MEMBER severityWord   FINAL)
    Q_PROPERTY(QString     tier           MEMBER tier           FINAL)
    Q_PROPERTY(int         pointsLost     MEMBER pointsLost     FINAL)
    Q_PROPERTY(QString     pointsText     MEMBER pointsText     FINAL)
    Q_PROPERTY(QStringList positions      MEMBER positions      FINAL)
    Q_PROPERTY(QString     positionsText  MEMBER positionsText  FINAL)
    Q_PROPERTY(int         phase          MEMBER phase          FINAL)
    Q_PROPERTY(qint64      seekUs         MEMBER seekUs         FINAL)
    Q_PROPERTY(QVariant    value          MEMBER value          FINAL)
    Q_PROPERTY(QString     valueText      MEMBER valueText      FINAL)
    Q_PROPERTY(QVariant    outBy          MEMBER outBy          FINAL)
    Q_PROPERTY(int         magnitude      MEMBER magnitude      FINAL)
    Q_PROPERTY(QString     magnitudeText  MEMBER magnitudeText  FINAL)
    Q_PROPERTY(double      confidence     MEMBER confidence     FINAL)
    Q_PROPERTY(QString     pips           MEMBER pips           FINAL)
    Q_PROPERTY(bool        lowConfidence  MEMBER lowConfidence  FINAL)
    Q_PROPERTY(QString     explanation    MEMBER explanation    FINAL)
    Q_PROPERTY(QString     coaching       MEMBER coaching       FINAL)
    Q_PROPERTY(QString     protect        MEMBER protect        FINAL)
    Q_PROPERTY(QStringList linkedTo       MEMBER linkedTo       FINAL)
    Q_PROPERTY(QStringList corroboratedBy MEMBER corroboratedBy FINAL)
    Q_PROPERTY(QList<pinpoint::charts::wristui::More> more MEMBER more FINAL)
public:
    QString     id, name, severity, severityWord, tier;
    int         pointsLost = 0;
    QString     pointsText;
    QStringList positions;
    QString     positionsText;
    int         phase = -1;
    qint64      seekUs = -1;
    QVariant    value = nullValue();
    QString     valueText;
    QVariant    outBy = nullValue();
    int         magnitude = 0;
    QString     magnitudeText;
    double      confidence = 0;
    QString     pips;
    bool        lowConfidence = false;
    QString     explanation, coaching, protect;
    QStringList linkedTo, corroboratedBy;
    QList<More> more;
    friend bool operator==(const Finding &, const Finding &) = default;
};

struct ThisShot
{
    Q_GADGET
    Q_PROPERTY(bool    available MEMBER available FINAL)
    Q_PROPERTY(QString empty     MEMBER empty     FINAL)
    Q_PROPERTY(int     ordinal   MEMBER ordinal   FINAL)
    Q_PROPERTY(int     shotId    MEMBER shotId    FINAL)
    Q_PROPERTY(QString swingDir  MEMBER swingDir  FINAL)
    Q_PROPERTY(bool    inSession MEMBER inSession FINAL)
    Q_PROPERTY(QString kicker    MEMBER kicker    FINAL)
    Q_PROPERTY(int     outCells  MEMBER outCells  FINAL)
    Q_PROPERTY(bool    graded    MEMBER graded    FINAL)
    Q_PROPERTY(pinpoint::charts::wristui::Sentence head MEMBER head FINAL)
    Q_PROPERTY(QList<pinpoint::charts::wristui::Sentence> standfirsts MEMBER standfirsts FINAL)
    Q_PROPERTY(pinpoint::charts::wristui::Score score MEMBER score FINAL)
    Q_PROPERTY(pinpoint::charts::wristui::Style style MEMBER style FINAL)
    Q_PROPERTY(pinpoint::charts::wristui::Profile profile MEMBER profile FINAL)
    Q_PROPERTY(QList<pinpoint::charts::wristui::Finding> findings MEMBER findings FINAL)
    Q_PROPERTY(QList<pinpoint::charts::wristui::Finding> strengths MEMBER strengths FINAL)
    Q_PROPERTY(QString findingsEmpty  MEMBER findingsEmpty  FINAL)
    Q_PROPERTY(QString strengthsEmpty MEMBER strengthsEmpty FINAL)
    Q_PROPERTY(pinpoint::charts::wristui::Trust trust MEMBER trust FINAL)
public:
    bool            available = false;
    QString         empty;
    int             ordinal = 0, shotId = -1;
    QString         swingDir;
    bool            inSession = false;
    QString         kicker;
    int             outCells = 0;
    bool            graded = false;
    Sentence        head;
    QList<Sentence> standfirsts;
    Score           score;
    Style           style;
    Profile         profile;
    QList<Finding>  findings, strengths;
    QString         findingsEmpty, strengthsEmpty;
    Trust           trust;
    friend bool operator==(const ThisShot &, const ThisShot &) = default;
};

// ── Session ────────────────────────────────────────────────────────────────────────────────────

// RUN's point (PpChRunBox). No `label`: PpChRunBox labels the mark by its ordinal when a point has none.
struct RunPoint
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
    int      ordinal = 0, shotId = -1;
    QString  swingDir;
    QVariant value = nullValue();
    bool     measured = false, fired = false, picked = false;
    friend bool operator==(const RunPoint &, const RunPoint &) = default;
};

struct YRange
{
    Q_GADGET
    Q_PROPERTY(double lo MEMBER lo FINAL)
    Q_PROPERTY(double hi MEMBER hi FINAL)
public:
    double lo = 0, hi = 0;
    friend bool operator==(const YRange &, const YRange &) = default;
};

// The drift's fitted line, drawn only where it clears chance.
struct DriftLine
{
    Q_GADGET
    Q_PROPERTY(double x0 MEMBER x0 FINAL)
    Q_PROPERTY(double y0 MEMBER y0 FINAL)
    Q_PROPERTY(double x1 MEMBER x1 FINAL)
    Q_PROPERTY(double y1 MEMBER y1 FINAL)
public:
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    friend bool operator==(const DriftLine &, const DriftLine &) = default;
};

// `line`: a DriftLine, or null.
struct Drift
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
    QVariant r = nullValue(), lo = nullValue(), hi = nullValue();
    bool     clear = false;
    QString  rText, ciText;
    QVariant slope = nullValue(), intercept = nullValue();
    QVariant line = nullValue();
    friend bool operator==(const Drift &, const Drift &) = default;
};

// RUN: the wrist score, shot by shot (PpChRunBox). The report's payload adds `ticks` to its map.
struct ScoreRun
{
    Q_GADGET
    Q_PROPERTY(QString readingId MEMBER readingId FINAL)
    Q_PROPERTY(QString key       MEMBER key       FINAL)
    Q_PROPERTY(QString label     MEMBER label     FINAL)
    Q_PROPERTY(QString tag       MEMBER tag       FINAL)
    Q_PROPERTY(QString unit      MEMBER unit      FINAL)
    Q_PROPERTY(QString unitText  MEMBER unitText  FINAL)
    Q_PROPERTY(QString caption   MEMBER caption   FINAL)
    Q_PROPERTY(QList<pinpoint::charts::wristui::RunPoint> points MEMBER points FINAL)
    Q_PROPERTY(pinpoint::charts::wristui::Box box MEMBER box FINAL)
    Q_PROPERTY(pinpoint::charts::wristui::YRange yRange MEMBER yRange FINAL)
    Q_PROPERTY(pinpoint::charts::wristui::Drift drift MEMBER drift FINAL)
public:
    QString         readingId, key, label, tag, unit, unitText, caption;
    QList<RunPoint> points;
    Box             box;
    YRange          yRange;
    Drift           drift;
    friend bool operator==(const ScoreRun &, const ScoreRun &) = default;
};

struct StyleColumn
{
    Q_GADGET
    Q_PROPERTY(int     ordinal  MEMBER ordinal   FINAL)
    Q_PROPERTY(int     shotId   MEMBER shotId    FINAL)
    Q_PROPERTY(QString swingDir MEMBER swingDir  FINAL)
    Q_PROPERTY(QString label    MEMBER label     FINAL)
    Q_PROPERTY(QString short    MEMBER shortText FINAL)
    Q_PROPERTY(bool    picked   MEMBER picked    FINAL)
public:
    int     ordinal = 0, shotId = -1;
    QString swingDir, label, shortText;
    bool    picked = false;
    friend bool operator==(const StyleColumn &, const StyleColumn &) = default;
};

struct StyleCell
{
    Q_GADGET
    Q_PROPERTY(QString text   MEMBER text   FINAL)
    Q_PROPERTY(bool    graded MEMBER graded FINAL)
public:
    QString text;
    bool    graded = false;
    friend bool operator==(const StyleCell &, const StyleCell &) = default;
};

struct StyleRow
{
    Q_GADGET
    Q_PROPERTY(QString key  MEMBER key  FINAL)
    Q_PROPERTY(QString name MEMBER name FINAL)
    Q_PROPERTY(QList<pinpoint::charts::wristui::StyleCell> cells MEMBER cells FINAL)
public:
    QString          key, name;
    QList<StyleCell> cells;
    friend bool operator==(const StyleRow &, const StyleRow &) = default;
};

struct StyleCount
{
    Q_GADGET
    Q_PROPERTY(QString name MEMBER name FINAL)
    Q_PROPERTY(int     n    MEMBER n    FINAL)
public:
    QString name;
    int     n = 0;
    friend bool operator==(const StyleCount &, const StyleCount &) = default;
};

struct Styles
{
    Q_GADGET
    Q_PROPERTY(QList<pinpoint::charts::wristui::StyleColumn> columns MEMBER columns FINAL)
    Q_PROPERTY(QList<pinpoint::charts::wristui::StyleRow> rows MEMBER rows FINAL)
    Q_PROPERTY(QList<pinpoint::charts::wristui::StyleCount> counts MEMBER counts FINAL)
    Q_PROPERTY(bool    hasIndex MEMBER hasIndex FINAL)
    Q_PROPERTY(QString note     MEMBER note     FINAL)
public:
    QList<StyleColumn> columns;
    QList<StyleRow>    rows;
    QList<StyleCount>  counts;
    bool               hasIndex = false;
    QString            note;
    friend bool operator==(const Styles &, const Styles &) = default;
};

struct RecurringPosition
{
    Q_GADGET
    Q_PROPERTY(QString tag MEMBER tag FINAL)
    Q_PROPERTY(int     n   MEMBER n   FINAL)
public:
    QString tag;
    int     n = 0;
    friend bool operator==(const RecurringPosition &, const RecurringPosition &) = default;
};

struct RecurringMark
{
    Q_GADGET
    Q_PROPERTY(int     ordinal  MEMBER ordinal  FINAL)
    Q_PROPERTY(int     shotId   MEMBER shotId   FINAL)
    Q_PROPERTY(QString swingDir MEMBER swingDir FINAL)
    Q_PROPERTY(bool    fired    MEMBER fired    FINAL)
public:
    int     ordinal = 0, shotId = -1;
    QString swingDir;
    bool    fired = false;
    friend bool operator==(const RecurringMark &, const RecurringMark &) = default;
};

struct Recurring
{
    Q_GADGET
    Q_PROPERTY(QString id        MEMBER id        FINAL)
    Q_PROPERTY(QString name      MEMBER name      FINAL)
    Q_PROPERTY(QString severity  MEMBER severity  FINAL)
    Q_PROPERTY(QString tier      MEMBER tier      FINAL)
    Q_PROPERTY(int     count     MEMBER count     FINAL)
    Q_PROPERTY(QString countText MEMBER countText FINAL)
    Q_PROPERTY(QList<pinpoint::charts::wristui::RecurringPosition> positions MEMBER positions FINAL)
    Q_PROPERTY(QList<pinpoint::charts::wristui::RecurringMark> marks MEMBER marks FINAL)
public:
    QString                  id, name, severity, tier;
    int                      count = 0;
    QString                  countText;
    QList<RecurringPosition> positions;
    QList<RecurringMark>     marks;
    friend bool operator==(const Recurring &, const Recurring &) = default;
};

// consistency.lines: a club's most and least consistent checkpoint from P3 on. `result` (too few: the grade-0
// result), `most` and `least` each a VariabilityResultView, or null.
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
    Q_PROPERTY(QString sub MEMBER sub FINAL)
    Q_PROPERTY(QList<pinpoint::charts::wristui::ConsistencyLine> lines MEMBER lines FINAL)
public:
    QString                sub;
    QList<ConsistencyLine> lines;
    friend bool operator==(const Consistency &, const Consistency &) = default;
};

// `pending`: shots with wrist readings and none graded yet — the head and the reason (`pendingWhy`), and the wrist
// curves' metric keys (`keys`), nothing drawn from grades.
struct Session
{
    Q_GADGET
    Q_PROPERTY(int         n              MEMBER n              FINAL)
    Q_PROPERTY(int         ready          MEMBER ready          FINAL)
    Q_PROPERTY(bool        small          MEMBER isSmall        FINAL)   // `small` is a macro on Windows (rpcndr.h)
    Q_PROPERTY(bool        available      MEMBER available      FINAL)
    Q_PROPERTY(bool        pending        MEMBER pending        FINAL)
    Q_PROPERTY(QString     kicker         MEMBER kicker         FINAL)
    Q_PROPERTY(pinpoint::charts::wristui::Sentence head MEMBER head FINAL)
    Q_PROPERTY(pinpoint::charts::wristui::Sentence standfirst MEMBER standfirst FINAL)
    Q_PROPERTY(QString     pendingWhy     MEMBER pendingWhy     FINAL)
    Q_PROPERTY(QStringList keys           MEMBER keys           FINAL)
    Q_PROPERTY(QList<pinpoint::charts::wristui::Position> positions MEMBER positions FINAL)
    Q_PROPERTY(pinpoint::charts::wristui::Consistency consistency MEMBER consistency FINAL)
    Q_PROPERTY(QList<pinpoint::charts::wristui::Row> rows MEMBER rows FINAL)
    Q_PROPERTY(QString     caption        MEMBER caption        FINAL)
    Q_PROPERTY(QString     sub            MEMBER sub            FINAL)
    Q_PROPERTY(pinpoint::charts::wristui::Box scoreBox MEMBER scoreBox FINAL)
    Q_PROPERTY(pinpoint::charts::wristui::ScoreRun scoreRun MEMBER scoreRun FINAL)
    Q_PROPERTY(pinpoint::charts::wristui::Styles styles MEMBER styles FINAL)
    Q_PROPERTY(QList<pinpoint::charts::wristui::Recurring> recurring MEMBER recurring FINAL)
    Q_PROPERTY(QString     recurringEmpty MEMBER recurringEmpty FINAL)
    Q_PROPERTY(pinpoint::charts::wristui::Trust trust MEMBER trust FINAL)
public:
    int              n = 0, ready = 0;
    bool             isSmall = false, available = false, pending = false;
    QString          kicker;
    Sentence         head, standfirst;
    QString          pendingWhy;
    QStringList      keys;
    QList<Position>  positions;
    Consistency      consistency;
    QList<Row>       rows;
    QString          caption, sub;
    Box              scoreBox;
    ScoreRun         scoreRun;
    Styles           styles;
    QList<Recurring> recurring;
    QString          recurringEmpty;
    Trust            trust;
    friend bool operator==(const Session &, const Session &) = default;
};

} // namespace pinpoint::charts::wristui
