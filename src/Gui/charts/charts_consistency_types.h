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

// THE CONSISTENCY TAB'S TYPED RESULTS (refactor 5.5b, lint A6): what ChartsConsistencyPresenter hands QML, as Q_GADGET
// values. Each property is named as the map key it replaces (charts_consistency_presenter.h, THE OUTPUTS), so QML reads a
// gadget as it read the map. A gadget always has every property: where a key used to be absent, the property holds its
// default ("" , 0, false, an empty list), and every reader in QML already treats those as "none".
//
// The two outputs whose rows are READING ROWs (`cell` and `shot`, charts_relations_presenter.h) are still maps: the row is
// the Relations presenter's shape, and it is typed with that presenter (5.5c).
//
// NO VARIABILITY WORD IS SPELLED HERE (docs/design/system/variability.md): the words are the vocabulary singletons'. A
// cell says WHICH vocabulary names it and at WHICH grade (`vocabulary`, `vocabularyGrade`); QML only looks the word up.

#include "../variability/variability_facade.h"     // VariabilityResultView: a cell's grade

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

namespace pinpoint::charts {

// A run of a Sentence (charts_words.h): its words, and whether they are a figure.
struct ConsistencyTabRun
{
    Q_GADGET
    QML_VALUE_TYPE(consistencyTabRun)
    Q_PROPERTY(QString text MEMBER text FINAL)
    Q_PROPERTY(bool    fig  MEMBER fig  FINAL)
public:
    QString text;
    bool    fig = false;
    friend bool operator==(const ConsistencyTabRun &, const ConsistencyTabRun &) = default;
};

// A Sentence as words::Sentence::toVariant() spelled it: { text, runs }. No runs: no sentence (`text` "").
struct ConsistencyTabSentence
{
    Q_GADGET
    QML_VALUE_TYPE(consistencyTabSentence)
    Q_PROPERTY(QString                                     text MEMBER text FINAL)
    Q_PROPERTY(QList<pinpoint::charts::ConsistencyTabRun>  runs MEMBER runs FINAL)
public:
    QString                  text;
    QList<ConsistencyTabRun> runs;
    friend bool operator==(const ConsistencyTabSentence &, const ConsistencyTabSentence &) = default;
};

// A half's grade in the headline, backswing first.
struct ConsistencyTabHeadlineGrade
{
    Q_GADGET
    QML_VALUE_TYPE(consistencyTabHeadlineGrade)
    Q_PROPERTY(int  grade              MEMBER grade              FINAL)
    Q_PROPERTY(bool measurementLimited MEMBER measurementLimited FINAL)
public:
    int  grade = 0;
    bool measurementLimited = false;
    friend bool operator==(const ConsistencyTabHeadlineGrade &, const ConsistencyTabHeadlineGrade &) = default;
};

// The table in one sentence (charts_consistency_presenter.h, `head.headline`): a Sentence and its kind ("halves", "most",
// "grade0", "ungraded", "empty"; "" while a session is still being read). `grades` fill a "halves" template's %1 and %2,
// in that order; `fallback` is the line for a surface that will not say a half's word. "empty" has no grades and no
// fallback.
struct ConsistencyTabHeadline
{
    Q_GADGET
    QML_VALUE_TYPE(consistencyTabHeadline)
    Q_PROPERTY(QString                                               text     MEMBER text     FINAL)
    Q_PROPERTY(QList<pinpoint::charts::ConsistencyTabRun>            runs     MEMBER runs     FINAL)
    Q_PROPERTY(QString                                               kind     MEMBER kind     FINAL)
    Q_PROPERTY(QList<pinpoint::charts::ConsistencyTabHeadlineGrade>  grades   MEMBER grades   FINAL)
    Q_PROPERTY(QString                                               fallback MEMBER fallback FINAL)
public:
    QString                            text;
    QList<ConsistencyTabRun>           runs;
    QString                            kind;
    QList<ConsistencyTabHeadlineGrade> grades;
    QString                            fallback;
    friend bool operator==(const ConsistencyTabHeadline &, const ConsistencyTabHeadline &) = default;
};

// `head`: the kicker, the headline, the ball line (no runs with no carry) and the notes under the table.
struct ConsistencyTabHead
{
    Q_GADGET
    QML_VALUE_TYPE(consistencyTabHead)
    Q_PROPERTY(QString                                         kicker   MEMBER kicker   FINAL)
    Q_PROPERTY(pinpoint::charts::ConsistencyTabHeadline        headline MEMBER headline FINAL)
    Q_PROPERTY(pinpoint::charts::ConsistencyTabSentence        ball     MEMBER ball     FINAL)
    Q_PROPERTY(QList<pinpoint::charts::ConsistencyTabSentence> notes    MEMBER notes    FINAL)
public:
    QString                       kicker;
    ConsistencyTabHeadline        headline;
    ConsistencyTabSentence        ball;
    QList<ConsistencyTabSentence> notes;
    friend bool operator==(const ConsistencyTabHead &, const ConsistencyTabHead &) = default;
};

// A column of the table: its name, and its short form ("P3-P4", "3-4"). `short` is a C++ keyword, so its member is not.
struct ConsistencyTabColumn
{
    Q_GADGET
    QML_VALUE_TYPE(consistencyTabColumn)
    Q_PROPERTY(QString text  MEMBER text      FINAL)
    Q_PROPERTY(QString short MEMBER shortText FINAL)
public:
    QString text;
    QString shortText;
    friend bool operator==(const ConsistencyTabColumn &, const ConsistencyTabColumn &) = default;
};

// A cell of the table (PpChMatrix's rectangle). `text` is "-" for a cell with no graded reading and "" otherwise; a
// graded cell's word is the vocabulary's: `vocabulary` "consistency" is ConsistencyVocabulary.shortWord(vocabularyGrade)
// (the table's form, grade 0 "Too few", R33), "measurement" is MeasurementVocabulary.word(vocabularyGrade) (a
// measurement-limited cell, "Checking"), "" is `text` itself. A column that does not apply to the area has "" in both
// texts and nothing else set.
struct ConsistencyTabCell
{
    Q_GADGET
    QML_VALUE_TYPE(consistencyTabCell)
    Q_PROPERTY(QString                                     text            MEMBER text            FINAL)
    Q_PROPERTY(QString                                     short           MEMBER shortText       FINAL)
    Q_PROPERTY(bool                                        strong          MEMBER strong          FINAL)
    Q_PROPERTY(bool                                        heavy           MEMBER heavy           FINAL)
    Q_PROPERTY(bool                                        on              MEMBER on              FINAL)
    Q_PROPERTY(QString                                     mark            MEMBER mark            FINAL)
    Q_PROPERTY(bool                                        tap             MEMBER tap             FINAL)
    Q_PROPERTY(int                                         heat            MEMBER heat            FINAL)
    Q_PROPERTY(bool                                        soft            MEMBER soft            FINAL)
    Q_PROPERTY(pinpoint::variability::VariabilityResultView result         MEMBER result          FINAL)
    Q_PROPERTY(QString                                     vocabulary      MEMBER vocabulary      FINAL)
    Q_PROPERTY(int                                         vocabularyGrade MEMBER vocabularyGrade FINAL)
public:
    QString text;
    QString shortText;
    bool    strong = false;
    bool    heavy = false;
    bool    on = false;
    QString mark;
    bool    tap = false;
    int     heat = 0;
    bool    soft = false;
    pinpoint::variability::VariabilityResultView result;
    QString vocabulary;
    int     vocabularyGrade = 0;
    friend bool operator==(const ConsistencyTabCell &, const ConsistencyTabCell &) = default;
};

// A row of the table: an area, one cell per column.
struct ConsistencyTabRow
{
    Q_GADGET
    QML_VALUE_TYPE(consistencyTabRow)
    Q_PROPERTY(QString                                     id    MEMBER id    FINAL)
    Q_PROPERTY(QString                                     name  MEMBER name  FINAL)
    Q_PROPERTY(QList<pinpoint::charts::ConsistencyTabCell> cells MEMBER cells FINAL)
public:
    QString                   id;
    QString                   name;
    QList<ConsistencyTabCell> cells;
    friend bool operator==(const ConsistencyTabRow &, const ConsistencyTabRow &) = default;
};

// `matrix`: the table.
struct ConsistencyTabMatrix
{
    Q_GADGET
    QML_VALUE_TYPE(consistencyTabMatrix)
    Q_PROPERTY(QList<pinpoint::charts::ConsistencyTabColumn> columns MEMBER columns FINAL)
    Q_PROPERTY(QList<pinpoint::charts::ConsistencyTabRow>    rows    MEMBER rows    FINAL)
public:
    QList<ConsistencyTabColumn> columns;
    QList<ConsistencyTabRow>    rows;
    friend bool operator==(const ConsistencyTabMatrix &, const ConsistencyTabMatrix &) = default;
};

// One of `shots`, in strike order: a shot a press can pick. `flags` "short", "offline", either or neither.
struct ConsistencyTabShot
{
    Q_GADGET
    QML_VALUE_TYPE(consistencyTabShot)
    Q_PROPERTY(int         ordinal  MEMBER ordinal  FINAL)
    Q_PROPERTY(int         shotId   MEMBER shotId   FINAL)
    Q_PROPERTY(QString     swingDir MEMBER swingDir FINAL)
    Q_PROPERTY(QString     club     MEMBER club     FINAL)
    Q_PROPERTY(QStringList flags    MEMBER flags    FINAL)
    Q_PROPERTY(bool        guarded  MEMBER guarded  FINAL)
public:
    int         ordinal = 0;
    int         shotId = -1;
    QString     swingDir;
    QString     club;
    QStringList flags;
    bool        guarded = false;
    friend bool operator==(const ConsistencyTabShot &, const ConsistencyTabShot &) = default;
};

// `empty`: what the table cannot say. `kind` "" when it can ("noSession", "noShots", "noCamera", "tooFew" otherwise).
struct ConsistencyTabEmpty
{
    Q_GADGET
    QML_VALUE_TYPE(consistencyTabEmpty)
    Q_PROPERTY(QString                                  kind     MEMBER kind     FINAL)
    Q_PROPERTY(pinpoint::charts::ConsistencyTabSentence sentence MEMBER sentence FINAL)
public:
    QString                kind;
    ConsistencyTabSentence sentence;
    friend bool operator==(const ConsistencyTabEmpty &, const ConsistencyTabEmpty &) = default;
};

// What the latest job spent, in ms: all of it, and the rule alone (0 once it has run for the snapshot).
struct ConsistencyTabTimings
{
    Q_GADGET
    QML_VALUE_TYPE(consistencyTabTimings)
    Q_PROPERTY(double jobMs         MEMBER jobMs         FINAL)
    Q_PROPERTY(double consistencyMs MEMBER consistencyMs FINAL)
public:
    double jobMs = 0.0;
    double consistencyMs = 0.0;
    friend bool operator==(const ConsistencyTabTimings &, const ConsistencyTabTimings &) = default;
};

// `status`. `settling` ("Reading this session…") is set from before the first job, for the view to show when it has no
// table yet; everything else is the latest job's.
struct ConsistencyTabStatus
{
    Q_GADGET
    QML_VALUE_TYPE(consistencyTabStatus)
    Q_PROPERTY(bool                                     ready         MEMBER ready         FINAL)
    Q_PROPERTY(int                                      shotCount     MEMBER shotCount     FINAL)
    Q_PROPERTY(int                                      pickedOrdinal MEMBER pickedOrdinal FINAL)
    Q_PROPERTY(bool                                     loading       MEMBER loading       FINAL)
    Q_PROPERTY(pinpoint::charts::ConsistencyTabSentence settling      MEMBER settling      FINAL)
    Q_PROPERTY(pinpoint::charts::ConsistencyTabTimings  timings       MEMBER timings       FINAL)
public:
    bool                   ready = false;
    int                    shotCount = 0;
    int                    pickedOrdinal = 0;
    bool                   loading = false;
    ConsistencyTabSentence settling;
    ConsistencyTabTimings  timings;
    friend bool operator==(const ConsistencyTabStatus &, const ConsistencyTabStatus &) = default;
};

} // namespace pinpoint::charts
