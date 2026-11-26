/*
 * Copyright (C) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#pragma once

// THE LAUNCH MONITOR BOARD'S ANNOTATIONS, READ BY EVERY REDUCTION (refactor step 3.6; Stage 0.5's follow-up, 04 step 3
// "Closes"; build/stage0-20261114/0.5/REPORT.md §4). On the board the golfer can LEAVE A SHOT'S READING OUT
// (review.excluded: the monitor misread it) or RE-LABEL ITS CLUB (review.clubOverride). Stage 0.5 made the session
// report's ball figures honour both (lm::LmBoardShot::inFigures, the board's own rule). This is the same two facts as
// every other reduction reads them: a shot AS THE GOLFER ANNOTATED IT.
//
// THE RULE, annotatedShot() v1 (kBoardAnnotationRuleVersion):
//   club       the board's club when set (any spelling the club vocabulary reads; readers canonicalise), else the
//              row's own — the effective club (effective_context.h: "correction = the LM board's override")
//   excluded   the shot reads AS IF THE MONITOR HAD NOT READ IT — exactly the shot the board leaves out of its
//              figures (a reading left out and a reading never made are one case there, inFigures()): every monitor
//              reading on the record is "not read" (NaN), and every row whose driving measure the monitor reads is
//              NotAssessable, so no tally, window, usual or grade counts it. The camera's readings and rows stay.
// A shot with neither is returned unchanged, so an unannotated session reads exactly as before.
//
// WHICH MEASURES THE MONITOR READS: those whose catalogue key ladder (measureKeyLadder) holds an "lm." key — read from
// the pack, never a hand list, so a new monitor measure is covered when the pack names it (launchMonitorMeasures).
//
// Who reads it (step 3.6): the session reduction (SessionDiagnostics' input: the work-on facts, carry today, the club
// windows the distances and baselines come from, each shot's club in the summary); Home's Tendencies and Your
// distances (WorkOnsController's ledger read) and its Consistency marks (the club a reading is centred under). Not yet:
// the shot's own derived data and findings (layers 3–5 fire from the readings as recorded), and the session report's
// pages other than Stage 0.5's (3.7, with the report's cut-over). Since refactor 3.7-d also the report's pages and, by
// Mark's J-45, the Charts tab (SessionSeriesModel): the swing series under the board's club, a left-out shot's
// monitor series and monitor-driven ledger rows not read.
//
// Rules ring (src/Diagnostics/derived): pure, no I/O.

#include "diagnostic_ledger.h"
#include "../engine/characteristic.h"
#include "../../Vocabulary/Club/club_vocabulary.h"   // effectiveClub: the one club rule (refactor 6.2)

#include <QHash>
#include <QJsonObject>
#include <QSet>
#include <QString>

#include <limits>
#include <vector>

namespace pinpoint::analysis {

// 1 (16 Nov 2026, refactor step 3.6): the club override and the left-out reading, as above.
inline constexpr int kBoardAnnotationRuleVersion = 1;

// One shot's two board facts, as its review holds them. Empty: nothing said.
struct BoardAnnotation {
    bool    excluded = false;   // review.excluded: the reading is left out of the figures
    QString clubOverride;       // review.clubOverride: the board's club ("" = the shot's own)
    bool any() const { return excluded || !clubOverride.trimmed().isEmpty(); }
    bool operator==(const BoardAnnotation &o) const { return excluded == o.excluded && clubOverride == o.clubOverride; }
};

// THE RULE'S PARTS, ONE PLACE (refactor 3.7-d, J-45): every shape a shot is read in — the ledger's ShotRecord
// (annotatedShot, below) and the Charts' ShotSeries (annotateForBoard, a static in session_series_model.cpp) — asks these.
// A reading key the launch monitor gives: an "lm." key (the catalogue's key ladder; the document's metric keys) — the
// measurement vocabulary's source test (measurement.h, refactor 6.1a: a launch monitor reading, told one way).
inline bool isLaunchMonitorKey(const QString &key) { return isLaunchMonitorReading(key); }
// The club a shot is read under: the board's when it set one, else the shot's own — the one club rule
// (club_vocabulary.h effectiveClub; refactor 6.2, R-CLUB-2). Since 6.2 a fired shot's club is already its effective one
// (the phase grid's), so this is the identity there; the Charts' shot (the uncorrected club) still takes it here.
inline QString annotatedClub(const QString &club, const BoardAnnotation &a)
{
    return a.clubOverride.trimmed().isEmpty() ? club : pinpoint::effectiveClub(club, a.clubOverride);
}
// A swing document's two board facts, from its review block (review.excluded, review.clubOverride: the keys
// RecordingRepository's review read gives as ShotReview::lmExcluded / lmClubOverride).
inline BoardAnnotation boardAnnotationOfDocument(const QJsonObject &root)
{
    const QJsonObject review = root.value(QStringLiteral("review")).toObject();
    return BoardAnnotation{ review.value(QStringLiteral("excluded")).toBool(false),
                            review.value(QStringLiteral("clubOverride")).toString() };
}

// The pack's measures the launch monitor reads: an "lm." key on the measure's ladder.
inline QSet<QString> launchMonitorMeasures(const CharacteristicPack &pack)
{
    QSet<QString> out;
    for (const Measure &m : pack.measures)
        for (const QString &key : measureKeyLadder(m))
            if (isLaunchMonitorKey(key)) {
                out.insert(m.id);
                break;
            }
    return out;
}

// The shot as the golfer annotated it (the rule above).
inline ShotRecord annotatedShot(ShotRecord shot, const BoardAnnotation &a, const QSet<QString> &lmMeasures)
{
    if (!a.any())
        return shot;
    shot.club = annotatedClub(shot.club, a);
    if (a.excluded) {
        constexpr double kNotRead = std::numeric_limits<double>::quiet_NaN();
        for (const ShotReadingField &f : shotReadingFields())
            if (lmMeasures.contains(QLatin1String(f.measureId)))
                shot.*(f.field) = kNotRead;
        for (ConditionRow &r : shot.rows)
            if (!r.drivingMeasureId.isEmpty() && lmMeasures.contains(r.drivingMeasureId))
                r.state = ShotState::NotAssessable;
    }
    return shot;
}

// Every shot of a session, each by its own annotation (by swing number = the record's shotId).
inline void annotateShots(std::vector<ShotRecord> &shots, const QHash<int, BoardAnnotation> &board,
                          const QSet<QString> &lmMeasures)
{
    if (board.isEmpty())
        return;
    for (ShotRecord &s : shots)
        if (const auto it = board.constFind(s.shotId); it != board.constEnd())
            s = annotatedShot(std::move(s), *it, lmMeasures);
}

} // namespace pinpoint::analysis
