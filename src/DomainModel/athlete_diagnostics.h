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

// AthleteDiagnostics — THE ATHLETE LEVEL'S DERIVED OWNER, one per Athlete, its child (refactor step 3.6;
// build/refactor/step-3/BRIEF.md §4.6, §6 row 3.6; docs/developer/information_model.md CLS-ATHLETE-DIAG,
// OBJ-ATHLETE-DIAG, R-DER-3, §7.1's athlete row; DEC-46, DEC-53, DEC-60, DEC-92, DEC-138, DEC-172, DEC-192).
//
// WHAT IT READS: THE SESSION SUMMARIES ONLY (R-DER-3, DEC-92): each session's work_ons.json (FILE-SESS-05, written
// by its SessionDiagnostics: DEC-55), through the session repository the Athlete was made with, by the SessionIndex's
// ids (DEC-53). Never a ledger (diagnostics.json), a shot, a review or a swing document; it never re-runs a session's
// reduction. A summary of an older rule is not this build's answer (read strictly); one without DEC-192's growth
// gives its work-ons but no club windows.
//
// WHAT IT COMPUTES (each a pure Coaching rule over the summaries):
//   • the athlete work-ons (GL-70; DEC-138): aggregateWorkOns over the sessions' ranked lists, and every condition
//     any session assessed (aggregateConditions) — work_ons.h's arithmetic, the same Home's list is built from;
//   • the distances ("Your distances", FILE-ATH-02's clubDistances; D7: DEC-172): the club windows each summary
//     carries, pooled newest first up to the caps (club_distances.h clubDistancesFromPools);
//   • a session's CLUB BASELINE before the Session freezes it (DEC-46): clubBaselineFor() — the predecessors'
//     summaries (strictly earlier days, the newest ten that hold a session), clubBaselineFromSummaries. Not cached
//     (D7): read fresh at each freeze, so a summary written by anyone is the one used. NOT COMPUTABLE (nothing is
//     frozen, so nothing wrong is frozen for good) while an earlier session in that window holds swings but no
//     current summary with its windows — the old model's live rule ("all current"), applied to every freeze.
//   • THE TENDENCIES (GL-64, FILE-ATH-02's rule): reduceTendencyShots over the summaries' compact tendency facts — each
//     shot as its session judged it by the Tendencies rule (DEC-192 as Mark amended it, 16 Nov "b": compact per-shot
//     facts, never the judged rows) — with the distances from the summaries' club windows;
//   • THE THEMES ("Your swing", "what you do well", the focus; FILE-ATH-03's rule, every club): reduceSwingThemes over
//     the summaries' compact theme facts (each assessed row: fired, in band; the shot's table cells), by session name,
//     the replicates through the caller's parallel-for.
// NOT HERE: the consistency grades (their grader and window rule are in src/Gui/variability, which the domain ring may
// not include — they stay WorkOnsController's, now reading nothing a summary does not also carry: 3.6 NOTES round 2);
// the themes and work-ons under a club scope or a shot filter (WorkOnsController's until the catch-up's cut-over, 3.7);
// intent consistency (no code computes it: DEC-58's intent summaries were never built). The praise rule is applied where
// the grades are (swing_themes.h applyPraiseRule, WorkOnsController::publishSummary).
//
// LAZY (DEC-60): an event marks it stale and it recomputes when next read. Heard: the Athlete's sessions started,
// resumed or trashed (EVT-17, 23, 20) and its record edited (EVT-14's interim) — the index again; and, for every
// Session the Athlete holds (Athlete::sessionHeld), its end (EVT-09), a shot trashed (EVT-03), its club baseline
// rewritten (EVT-16) and its diagnostics writing the summary (SessionDiagnostics::written) — that session's summary
// again. Only what moved is read again; changed() says a reader should read again.
//
// PERSISTS (FILE-ATH-02, FILE-ATH-03 via the athlete's repository): NOTHING YET — persist() answers Unchanged. Their
// writer in the app is still WorkOnsController (its tendencies and themes caches) until the catch-up's cut-over (3.7);
// writing them here too would give each file two writers. The catch-up asks it anyway (CLS-ATHLETE-DIAG).
//
// Ownership (information_model.md §10): owner AthleteDiagnostics; level athlete; one writer (of its caches, from 3.7
// on); a view reaches it through its Athlete (AthleteDiagnostics::of), until step 4 through WorkOnsController.
// Domain model ring (src/DomainModel): names no store type; GUI thread, like its Athlete.

#include "athlete.h"
#include "session_repository.h"
#include "rule_data.h"
#include "../Coaching/club_distances.h"
#include "../Coaching/session_results.h"
#include "../Coaching/swing_themes.h"
#include "../Coaching/tendencies.h"
#include "../Coaching/work_ons.h"
#include "../Identity/identity.h"

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

#include <atomic>
#include <memory>
#include <optional>
#include <vector>

namespace pinpoint {

class Session;

// One session's summary as the athlete level holds it.
struct AthleteSessionSummary {
    SessionId               id;
    analysis::SessionPlace  place;       // its folder's name, its day and place in the day (SessionIdentity's)
    SessionStatus           status = SessionStatus::Ended;
    bool                    read = false;   // a summary of this build's rule was read (Strict); else `results` is empty
    analysis::SessionResults results;
};

// The club baseline as of one session (DEC-46, DEC-172), as the athlete level computes it for the Session to freeze.
struct ClubBaselineComputation {
    bool        computable = false;
    QString     why;                       // when not computable
    QDate       day;                       // the session's day
    QHash<QString, analysis::ClubBaselineEntry> clubs;   // keys canonical; a club with no qualifying shot: no entry
    QStringList read;                      // the earlier sessions it read (folder names), newest first
};

class AthleteDiagnostics : public QObject
{
    Q_OBJECT
public:
    // The Athlete's AthleteDiagnostics: its child, made on the first ask (one per Athlete). Null for a null athlete.
    static AthleteDiagnostics *of(Athlete *athlete);

    Athlete *athlete() const { return m_athlete; }
    bool     stale() const { return m_indexStale || !m_dirty.isEmpty(); }
    // An event's chain ended here (DEC-60): `session` null marks the whole index stale.
    void     markStale(const SessionId &session = {});

    // ── Reads: each recomputes first if stale (DEC-60) ─────────────────────────────────────────────────
    const std::vector<AthleteSessionSummary> &summaries();     // by place, oldest first (sessionPlaceBefore)
    QHash<QString, analysis::SessionWorkOns>  records();       // every summary read, by its sessionId (folder name)
    const std::vector<analysis::WorkOnSummary>     &workOns();      // the athlete work-ons (DEC-138)
    const std::vector<analysis::WorkOnSummary>     &conditions();   // every condition any session assessed
    const std::vector<analysis::ClubDistanceItem>  &distances();    // Your distances (D7)
    // From the summaries' compact per-shot facts (DEC-192 as Mark amended it, 16 Nov "b"). A summary without them (an
    // older file) adds nothing.
    std::vector<analysis::TendencySessionShots> tendencySessions();
    const analysis::TendencySet                &tendencies();     // its clubDistances: the summaries' club windows (D7)
    std::vector<analysis::ThemeSessionInput>    themeInputs();    // by session (folder) name
    // Every club; `parallel` runs the replicates (empty: serially); not cached (a caller with a cancel recomputes).
    analysis::SwingThemes themes(const analysis::ThemeParallelFor &parallel = {}, const std::atomic<bool> *cancel = nullptr);
    // Not cached (D7): read fresh from the predecessors' summaries each time.
    ClubBaselineComputation clubBaselineFor(const SessionId &session);

    // The athlete level's own caches (FILE-ATH-02, 03): nothing to write until 3.7 (WorkOnsController writes them) — Unchanged.
    WriteResult persist();

    struct Counts {
        quint64 recomputes = 0;     // the derived results computed again
        quint64 summaryReads = 0;   // work_ons.json files read (the only file it reads)
        quint64 staleMarks = 0;
    };
    const Counts &counts() const { return m_counts; }

signals:
    void changed();   // it went stale: a reader should read again

private:
    explicit AthleteDiagnostics(Athlete *athlete);
    void hearSession(Session *session);
    void ensure();
    AthleteSessionSummary readSummary(const SessionIndexEntry &e);
    SessionRepository *repository() const;
    std::shared_ptr<const RuleDataSnapshot> rules() const;   // the Athlete's doors' RuleData, else the process's

    Athlete *m_athlete = nullptr;
    bool     m_indexStale = true;
    QSet<SessionId> m_dirty;
    QSet<SessionId> m_heard;
    std::vector<AthleteSessionSummary>      m_summaries;
    std::vector<analysis::WorkOnSummary>    m_workOns, m_conditions;
    std::vector<analysis::ClubDistanceItem> m_distances;
    analysis::TendencySet                   m_tendencies;
    bool                                    m_tendenciesCurrent = false;
    Counts   m_counts;
};

} // namespace pinpoint
