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

#include "../../Coaching/club_scope.h"
#include "../../Coaching/outcome_scope.h"
#include "../../Coaching/swing_themes.h"
#include "../../Coaching/tendencies.h"
#include "../../Coaching/work_ons.h"
#include "../variability/variability_facade.h"      // VariabilityResultView: the consistency marks

#include <QDate>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QThreadPool>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

class SessionDiagnosticsModel;
namespace pinpoint::analysis { class ICharacteristicPackProvider; struct CharacteristicPack; class INormProvider; }

// WORK ONS — the list of the faults a golfer keeps producing (each opened in place under its row on
// Home's Coaching and Your swing tabs, HmFaultDetail), and the thing that keeps each session's record
// of them current (docs/design/work_ons_design.md). The context property `workOns`.
//
// ONE MECHANISM FOR "END OF SESSION" AND FOR CATCH-UP, because they are the same problem. A
// session's record (<session>/work_ons.json) is STALE whenever what it was derived from has
// moved: there is no record yet (every session recorded before this existed, and any the app
// quit out of), a swing arrived, left or was re-analysed, the pack or norms changed, or the
// selection rule did. refresh() finds the stale sessions and re-derives them; ending a session
// is merely the moment one becomes stale. Nothing has to remember to "close" anything, and a
// library converted, re-analysed or copied in from another machine catches up by being looked at.
//
// The derivation is SessionDiagnosticsModel's — a private, panel-less instance. Its
// activateSession() already reconciles the ledger against the swings on disk (back-fills what
// was never reduced, regrades what went stale), so a record is always derived from a CURRENT
// ledger, and the list here cannot disagree with the panel a golfer opens on the same session.
//
// The list is aggregateWorkOns() over the records and nothing else. No running totals:
// trashing a session removes its contribution at the next refresh.
//
// THREADING. The folder scan (a stat per swing, on what may be a network share) runs on a
// private single-thread pool and is delivered back queued. Derivation is the model's own worker.
// Sessions are derived one at a time, and never while a session is live (`paused`) — detection
// reads whole swing documents and must not compete with capture.
//
// YOUR SWING — the same catch-up ends in the golfer's swing summary (swing_themes.h,
// docs/design/home_themes_design.md): the one thing to practise (the focus), what they do well,
// what is next on their list, and what rises and falls together. It is reduced from every session's ledger once the work-ons are current — a current
// work_ons.json means its diagnostics.json was reconciled — and kept in
// <athlete>/swing_themes.json, stamped with a THEMES FINGERPRINT over the rule, the schema, the
// content stamp and every session's (name, work-ons fingerprint). A matching file is read rather
// than recomputed; anything else is recomputed off the GUI thread:
//
//   - one job at a time on its own single-thread pool, its 500 bootstrap and parallel-analysis
//     replicates fanned out over a second pool (all cores but one, plus the job's own thread);
//   - cancelled when a session goes live, when the athlete changes, or when a newer job
//     supersedes it (a generation counter, as the scan has) — a cancelled result is neither
//     written nor shown, and the next refresh or the pause lifting computes it again;
//   - the summary on screen stays while a recompute runs, and is replaced when it lands.
//
// The WORDS are made at publish from the stored structure (swingSummaryView over the pack's
// golfer phrases), so editing a phrase needs no recompute.
//
// CONSISTENCY MARKS — Home's Consistency column and the badges beside its faults (Mark's rulings R5 and
// R10, docs/design/variability-mark/decisions.md; docs/design/system/variability.md). One golfer-layer
// grade per (condition, measure): how consistently the golfer repeats the measure behind that fault.
//
//   - THE WINDOW is the condition's last kNWindow (20) readings on that measure ACROSS sessions, newest
//     first by shot time, off each session's diagnostics.json ledger. Which rows are readings, and how a
//     window is graded, is the rule the report's work-on page shares (src/Gui/variability/
//     variability_ledger.h): rows of that condition whose driving measure is that measure, assessed
//     (not NotAssessable), with a finite value. A shot carrying a data
//     warning counts: the ledger has already made a broken recording's rows NotAssessable, and a warning that
//     withholds nothing (a refused club track) leaves rows the habit counts beside this column count too.
//   - Centred per club (R2a, pooledWithinClubInput) and graded by the scalar grader against the norm
//     corridor half-width, (σLo + σHi)/2 whatever the norm's shape (R12), resolved for each shot's
//     context the way the session panel resolves it (sharedNormProvider, the row's context): the median
//     over the window's shots. The measure's catalogue scale type, else Interval (variability_measures.h).
//     No propagated σ: a ledger row stores none.
//   - UNGRADED (no badge, "-"): no measure; a yes/no condition (every signal an order test or a
//     threshold, AreaConditionInfo::binary); a composite measure; an outcome measure (ball flight, smash
//     factor: R4, isOutcomeMeasure); a reading tested against a fixed number (a finite faultAt) or
//     carrying no band (CorridorShape::None); no norm σ. Under kNMin (8) readings after the club
//     centring: grade 0, "Not enough swings".
//   - Which conditions: every one faultDetail() answers for, each on the measure an area item names
//     (latestEvidence's driving measure, else the newest listing's: AreaItem::measureId's rule).
//
// CLUB SCOPE — which clubs Coaching, Your swing and Matchups read (club_scope.h): all of them, a group or
// one club. It is one more input, so the properties below keep their names and QML reads them as before. It is
// the first of the shot filter's four parts (SHOT FILTER, below), and the one Tendencies reads too.
//
//   - ALL is the pipeline above, untouched: the records, swing_themes.json and its fingerprint.
//   - Any other scope is the same job (maybeThemes / runThemes) with a filter on the shots. A session wholly
//     in scope keeps its stored record; one partly in scope is re-reduced from its in-scope shots, without the
//     session-level sequence verdict, which is the whole session's and is not in the ledger. It is stored in
//     swing_themes.<key>.json, with its records, and read back when nothing has moved.
//   - TWO LANDINGS, when the scope is changing. Layer 1 is quick and is all Coaching and Your swing read, so it
//     lands first; Matchups keeps its last answer until the replicates finish. A session arriving under
//     the same scope stays quiet and replaces everything at once, as it does under All.
//   - BUSY IS COMPARED, NOT FLAGGED: m_scope is what was asked for, m_summaryScope and m_togetherScope what the
//     screens show. They differ exactly while a screen is out of date.
//   - ONLY THESE THREE TABS ARE SCOPED. m_records, `items`, `lastSession`, Tendencies' work-on column and the
//     report list stay every club's, and a scoped landing emits no itemsChanged.
//
// SHOT FILTER — which shots of the club choice Coaching, Your swing and Matchups read by what the ball did
// (outcome_scope.h, shot_factors.h, docs/design/shot_outcomes_design.md §7.6). A shot is three things at once,
// its STRIKE, its DIRECTION and its FLIGHT, and each is "any" or one value ("strike:thin|direction:pull|
// flight:any"); a shot is shown when it matches every factor chosen (AND). It sits beside the club scope and
// goes through the same job, so the same properties carry it.
//
//   - WHICH SHOTS: the ones the Tendencies rule judges (judgeTendencyShot, the helper Tendencies reads through):
//     the session's corrections and intents applied, at least one factor read (shotFactors), and a club with a
//     tendency group. A factor not read on a shot fails any choice for it but Any; a shot shows every value it
//     carries (a toe and a weak strike; a push slice is a slice and a push too). A shot that isn't judged is in
//     no choice but All.
//   - THE OPTIONS (shotFilterOptions): per factor, Any first, then every value the club choice's judged shots
//     show, in shot_factors.h's table order. Each count is what that choice would leave with the OTHER two
//     factors' choices kept, off the tendency reduction's per-club factor tuples (TendencySet::clubOutcomes) over
//     the clubs the club choice holds; a value that leaves nothing under the other choices is still listed (count
//     0). Until the tendencies are in hand each list is Any alone, and the filter reads All.
//   - THE KEY is the combined one, the club's when the filter is All (so All × All is still "all", and every
//     "is anything scoped" test is one comparison), else club|filter. m_summaryScope and m_togetherScope hold it,
//     so busy is still compared, not flagged. The request is kept as asked (m_filterRequested), part by part: a
//     part naming a value the club choice has never shown reads back "any", and comes back when the club choice
//     does.
//   - THE CONTRAST: a filter alone would make a fault on every shot look like a cause. So the job also counts,
//     per fault, how often it fired on the chosen shots against the golfer's other judged shots in the club
//     choice, and each fault on Coaching (focus, Next on your list) and in faultDetail() carries the line, its
//     words the filter's (filterWords): "On your pulled, thin shots 8 of 11 · other shots 12 of 31", with
//     "— just as often on your other shots", "— less often than on your other shots" or "— too few other shots
//     to compare" after it unless it is 15 points more often. The words never re-rank or hide anything.
//     shotFilterLine says what the choice covers, from the same pass.
//   - MEASURED: most shots can come from launch-monitor-only sessions, where no swing fault was assessed, so an
//     empty Coaching page under a choice would read as "no faults" when it means "not measured". The same pass
//     counts the chosen shots with at least one fault row assessed (`measured`), and shotFilterLine says it in a
//     second sentence: "Your pulled, thin mid-iron shots: 11 over 5 sessions, against 31 others. Your swing was
//     measured on 3 of them." ("… on all of them." / "… was not measured on any of them."). Under the contrast's
//     own floor (TendencyOptions::minSessionShots, 8) outcomeTooFewNote says "Too few of these shots had your
//     swing measured to say what comes with them yet.", and Coaching shows it in place of the empty "What you
//     do well" and "Next on your list"; a listed item is never hidden.
//   - WHERE THE LINE SHOWS: Coaching's rows (focus, Next on your list) carry it, so a fault opened there does
//     not repeat it; only Your swing's opened items (HmAreaRow → HmFaultRow { showOutcomeContrast }) show
//     faultDetail()'s outcomeContrast.
//   - STORED in swing_themes.<club>.<filter>.json (each key through clubScopeFileKey: swing_themes.all.
//     strike_any_direction_pull_flight_any.json) with its records, its contrast counts and the line's counts,
//     and read back when nothing has moved. Its fingerprint adds the filter key, kOutcomeScopeRuleVersion and
//     the tendency fingerprint, so a correction or an intent, which only touch the ledger, count. All × All is
//     today's file and fingerprint, untouched.
//   - WHAT DOES NOT FOLLOW IT: the work-on list, `lastSession`, the report, the Tendencies tab and `tendencyItems`,
//     "On the ball" (which follows the club choice only) and the consistency marks (the club choice's shots).
//
// The job runs once the catch-up settles (where the swing summary is started), one at a time on its
// own single-thread, low-priority pool, never while a session is live, and is cancelled by the athlete
// changing or a session going live. It first stats every session's ledger: the job's fingerprint is the
// rule, the content stamp, the conditions asked for and every ledger's (name, size, mtime), so an
// unchanged library costs a stat per session and nothing else. Otherwise it reads the ledgers newest
// session first and stops once every window is full and no unread session could hold a newer reading.
// The grades travel back in memory; nothing is written. A landing that changes a grade publishes again
// (areaItems and faultDetail() carry `consistency`); one that changes none moves nothing.
class WorkOnsController : public QObject
{
    Q_OBJECT

    // <library>/<athlete>, or empty for none. Passed in rather than reached for — main.cpp binds
    // it to the current athlete — so a test or a probe can point it at any folder.
    Q_PROPERTY(QString athleteDir READ athleteDir WRITE setAthleteDir NOTIFY athleteDirChanged)
    // The SAME policy the session panel grades against (AppSettings::diagnosticsGradePolicy).
    Q_PROPERTY(QString gradePolicy READ gradePolicy WRITE setGradePolicy NOTIFY gradePolicyChanged)
    // A session is live. Holds derivation, not the list.
    Q_PROPERTY(bool paused READ paused WRITE setPaused NOTIFY pausedChanged)

    // Active and easing work-ons, most persistent first; every string final.
    Q_PROPERTY(QVariantList items READ items NOTIFY itemsChanged)
    // Bumped with every publish: a QML binding on faultDetail() reads it to follow the records.
    Q_PROPERTY(int detailsRevision READ detailsRevision NOTIFY detailsChanged)
    Q_PROPERTY(int clearedCount READ clearedCount NOTIFY itemsChanged)
    // Sessions with a current record / sessions found. They differ while catching up.
    Q_PROPERTY(int sessionCount READ sessionCount NOTIFY itemsChanged)
    Q_PROPERTY(int sessionsFound READ sessionsFound NOTIFY itemsChanged)
    Q_PROPERTY(bool updating READ updating NOTIFY updatingChanged)
    // The newest session with a current record: {label "Mon 26 Oct" (the year too when not this
    // one), shots}. Empty with none. The queue derives newest first, so during a catch-up this is
    // the library's newest session as soon as that one record is current.
    Q_PROPERTY(QVariantMap lastSession READ lastSession NOTIFY itemsChanged)

    // ── Your swing: the areas ──
    // One row per area of the swing (swing_areas.h, docs/design/ux_refactor_away_from_4_modes.md
    // §5), in display order, rated by HOW MANY OF ITS FAULTS ARE HABITS — no 0–100 score. Built in
    // publish() from the same records as `items`, so it moves with the list, not with the themes.
    // Empty before the first publish and while no session has a record. Each area:
    //   {group (token), label ("Lateral motion"), measurable, measured, habits, occasional, clean,
    //    unclear, habitTrend (habits now − without the newest substantive session), hasTrend,
    //    topFault (label, "" when none), topFaultId, workOnsActive, workOnsNew, items [item], consistency}
    // Each item, habits first, then occasional, then the rest, by share within each — every one (Home
    // lists them all, each opening in place):
    //   {conditionId, label, golfer, band ("habit" | "occasional" | "clean" | "unclear"), share (0..1,
    //    recency-weighted), fired, assessable (unweighted swings), measureId, hasTypical, typical,
    //    corridorLo, corridorHi, corridorShape, tail ("high" | "low" | ""), unit, grade, workOn
    //    ("active" | "easing" | "new" | ""), sessionsListed, consistency}
    // And `consistency` on the area and on each item, a `variabilityResult` (VariabilityResultView): the
    // consistency mark of the item's measure (CONSISTENCY MARKS, above). The area's is its biggest fault's
    // (topFaultId's item), ungraded with no biggest fault. Ungraded until the first marks job lands.
    // `grade` is the typical value graded against its stored corridor under `gradePolicy`, in the
    // panel's tokens ("ideal" | "good" | "watch" | "action", for Theme.corridorTone/corridorWord);
    // "" when there is no typical value or no corridor to read it against. Only the condition's own
    // `tail` grades: past the other edge is its mirror's reading (bowed_at_top / cupped_at_top).
    Q_PROPERTY(QVariantList areaItems READ areaItems NOTIFY areasChanged)

    // ── Your swing ──
    // Every string final (swingSummaryView); HmSwingSummary.qml, HmFocus.qml and (Home's Matchups
    // tab) HmGoesTogether.qml position and paint. A fault item, wherever it appears, is
    // {text, share (0..1), frequency, trend (+1 growing, −1 easing, 0), sessions [bool, one per
    // judged session], conditionId (what its row opens with faultDetail()), onTheBall, outcomeContrast (the
    // SHOT FILTER's line, "" under All)}. focusItem carries both for its first "right now" fault too.
    // "From N swings over M sessions" — every shot in the ledgers, every session holding one.
    Q_PROPERTY(QString summarySubtitle READ summarySubtitle NOTIFY summaryChanged)
    // "Your focus": {present, title, aimFor [string], rightNow [fault item], why, practiseLabel,
    // practise, reason, reasonShort, focusConditionId (the first rightNow fault's condition id, ""
    // when none)}. reasonShort is the reason's two clauses on one line: "covers 2 things on your
    // list · comes earliest in your swing". present is false when nothing needs work; empty before
    // a summary.
    Q_PROPERTY(QVariantMap focusItem READ focusItem NOTIFY summaryChanged)
    // "What you do well", at most three: {text, caption, share (0..1, the in-band share the caption
    // words), sessions [bool, one per judged session]}.
    Q_PROPERTY(QVariantList doWellItems READ doWellItems NOTIFY summaryChanged)
    // "Next on your list", at most four fault items: the needs-work faults the focus does not
    // cover, in the order they would come up.
    Q_PROPERTY(QVariantList nextItems READ nextItems NOTIFY summaryChanged)
    // "What goes together", at most four: {tier ("firm" | "probably" | "possibly"), first, second
    // ("" when the theme has one part), startStop (0 address … 5 finish, −1 unplaced), startWords,
    // trend}.
    Q_PROPERTY(QVariantList togetherItems READ togetherItems NOTIFY summaryChanged)
    // The "Not yet …" / "Nothing yet …" sentence when nothing goes together, else empty.
    Q_PROPERTY(QString togetherNote READ togetherNote NOTIFY summaryChanged)
    // A summary for THIS athlete is loaded (it may be stale while summaryUpdating).
    Q_PROPERTY(bool summaryReady READ summaryReady NOTIFY summaryChanged)
    Q_PROPERTY(bool summaryUpdating READ summaryUpdating NOTIFY summaryChanged)
    // Swings and sessions the summary was reduced from (after the sparse-swing drop).
    Q_PROPERTY(int summarySwings READ summarySwings NOTIFY summaryChanged)
    Q_PROPERTY(int summarySessions READ summarySessions NOTIFY summaryChanged)

    // ── Tendencies (docs/design/shot_outcomes_design.md §7) ──
    //
    // The same catch-up ends in one more reduction (tendencies.h), <athlete>/tendencies.json, fingerprinted
    // on the rule, the schema, the content stamp, the flight table (flight_norms.json's bytes, so a tuned
    // threshold re-reduces) and each session's (name, work-ons fingerprint, ledger size and mtime), so a
    // correction, which only touches the ledger, counts. On the themes pool, cancelled by the same events; the
    // file is read rather than reduced when nothing moved. Strings final.
    //
    // focusItem and every nextItems entry gain `onTheBall` (a line, "" when none); faultDetail(id) gains `onBall`:
    //   { conditionId, text, produced: [{ phrase, count }], costWords, tendencyKey, factor, value, tendencyText,
    //     onTheBall } or {}. It is keyed on (club, factor fault): tendencyKey is the row ("7 IRON|full"), factor
    //   and value its fault ("direction", "pull"); "" for a condition that feeds no earned fault.
    //
    // tendencyItems: [{ key, club, clubLine, clubGroup, groupLabel, distance ("147 ± 31 yd", "" for a part swing),
    //   length, handedness, sessions, shots, workOnText, strike: F, direction: F, flight: F }], one per club and
    //   swing length. Row workOnText is the status words of its factors' work-ons, distinct, the most active first
    //   ("Active", "Active · Easing"), "" when none. Each F:
    //   { factor, value, label, fault, building, share, frequency, sessions, shots, seenWith, seenWithId,
    //     strength, trend, trendWords, workOn ("active" | "easing" | ""), workOnText ("Active", ""),
    //     mix: { line, parts: [{ key, label, kind ("good" | "value" | "main" | "other"), count, percent }] } }.
    //   A factor's usual is its most common fault when that earns, else its most common value said plainly
    //   (fault false); a building factor states no verdict; one never read is label "-".
    // tendencyNote: "From N shots over M sessions", or why nothing yet.
    // tendencyDetail(key): { strike: D, direction: D, flight: D, together: T } or {} for no such row; each D is
    //   { chain: [{ text, words }], alsoSeen, earlyNote, playTo, practiseConditionId }, or {} for a factor with
    //   nothing to say; T is { pairs: [line], combos: [line], moreCombos } (TendencyTogether), golfer-ready.
    // clubDistance(club): { mean, sd, n, total, offlineSd, earned, line } for the current athlete, or {}.
    Q_PROPERTY(QVariantList tendencyItems READ tendencyItems NOTIFY tendenciesChanged)
    Q_PROPERTY(QString tendencyNote READ tendencyNote NOTIFY tendenciesChanged)
    Q_PROPERTY(bool tendenciesReady READ tendenciesReady NOTIFY tendenciesChanged)
    Q_PROPERTY(bool tendenciesUpdating READ tendenciesUpdating NOTIFY tendenciesChanged)

    // ── Club scope (CLUB SCOPE, above) ──
    // "all", "group:mid_irons" or "club:7 IRON". Reads back "all" while the key names none of clubScopes.
    Q_PROPERTY(QString clubScope READ clubScope WRITE setClubScope NOTIFY clubScopeChanged)
    // The groups and clubs this golfer has hit: [{ value, group, label, clubs: [{ value, label }] }].
    Q_PROPERTY(QVariantList clubScopes READ clubScopes NOTIFY clubScopeChanged)
    // Coaching and Your swing, and then Matchups, are still showing another scope.
    Q_PROPERTY(bool clubScopeRefreshing READ clubScopeRefreshing NOTIFY clubScopeStateChanged)
    Q_PROPERTY(bool matchupsRefreshing READ matchupsRefreshing NOTIFY clubScopeStateChanged)
    // A new scope is being reduced. False for one whose stored result is current, so that never dims a page.
    Q_PROPERTY(bool clubScopeWorking READ clubScopeWorking NOTIFY clubScopeStateChanged)

    // ── Shot filter (SHOT FILTER, above) ──
    // "all", or "strike:…|direction:…|flight:…", each part "any" or a value. Written as asked and kept so; reads
    // back what it resolves to, a part naming none of that factor's options being "any" (and all three any "all").
    Q_PROPERTY(QString shotFilter READ shotFilter WRITE setShotFilter NOTIFY clubScopeChanged)
    // { strike: [...], direction: [...], flight: [...] }, each [{ value, label, count }] starting with
    // { value "any", label "Any", count }, then each value the club choice's shots show, in the table's order.
    // count: the shots that choice leaves with the other two factors' choices kept (0 is listed).
    Q_PROPERTY(QVariantMap shotFilterOptions READ shotFilterOptions NOTIFY clubScopeChanged)
    // What the shown filter covers, and on how many of its shots the swing was measured: "Your pulled, thin
    // mid-iron shots: 11 over 5 sessions, against 31 others. Your swing was measured on 3 of them." ("… on all of
    // them." / "Your swing was not measured on any of them."). Empty under All.
    Q_PROPERTY(QString shotFilterLine READ shotFilterLine NOTIFY clubScopeStateChanged)
    // Under a filter, when fewer than TendencyOptions::minSessionShots (8) of the chosen shots had a swing fault
    // assessed: "Too few of these shots had your swing measured to say what comes with them yet." Coaching shows
    // it in place of its empty-card words. Empty otherwise, and under All.
    Q_PROPERTY(QString outcomeTooFewNote READ outcomeTooFewNote NOTIFY clubScopeStateChanged)

public:
    explicit WorkOnsController(QObject *parent = nullptr);
    ~WorkOnsController() override;

    QString athleteDir() const { return m_athleteDir; }
    void    setAthleteDir(const QString &dir);
    QString gradePolicy() const { return m_gradePolicy; }
    void    setGradePolicy(const QString &name);
    bool    paused() const { return m_paused; }
    void    setPaused(bool on);

    QVariantList items() const { return m_items; }
    int  detailsRevision() const { return m_detailsRevision; }
    // ONE condition's detail, as Home opens it in place (HmFaultDetail) — for EVERY condition any
    // session assessed, not only the listed work-ons: a listed one's is its `items` entry exactly, plus
    // `outcomeContrast` (SHOT FILTER, above; "" under All), which the list never carries;
    // any other's has the same keys, its "Latest" from the newest session that assessed it.
    // Empty when no session assessed it. One map, not a map of them all: a QVariantMap property
    // read is a deep copy, and every fault row on Home asks. `consistency` is the condition's
    // consistency mark (CONSISTENCY MARKS, above), the one its area item shows; it sits beside the
    // work-on's status (R10).
    Q_INVOKABLE QVariantMap faultDetail(const QString &conditionId) const { return m_details.value(conditionId); }
    int  clearedCount() const { return m_clearedCount; }
    int  sessionCount() const { return int(m_records.size()); }
    int  sessionsFound() const { return m_sessionsFound; }
    bool updating() const { return m_scanning || m_deriving || !m_queue.isEmpty(); }
    QVariantMap  lastSession() const { return m_lastSession; }
    QVariantList areaItems() const { return m_areaItems; }

    QString      summarySubtitle() const { return m_summarySubtitle; }
    QVariantMap  focusItem() const { return m_focusItem; }
    QVariantList doWellItems() const { return m_doWellItems; }
    QVariantList nextItems() const { return m_nextItems; }
    QVariantList togetherItems() const { return m_togetherItems; }
    QString      togetherNote() const { return m_togetherNote; }
    bool summaryReady() const { return m_summaryReady; }
    bool summaryUpdating() const { return m_themesRunning; }
    int  summarySwings() const { return m_summaryReady ? m_themes.swings : 0; }
    int  summarySessions() const { return m_summaryReady ? m_themes.sessions : 0; }

    QVariantList tendencyItems() const { return m_tendencyItems; }
    QString      tendencyNote() const { return m_tendencyNote; }
    bool tendenciesReady() const { return m_tendenciesReady; }
    bool tendenciesUpdating() const { return m_tendenciesRunning; }
    Q_INVOKABLE QVariantMap tendencyDetail(const QString &key) const;
    Q_INVOKABLE QVariantMap clubDistance(const QString &club) const;

    QString clubScope() const { return m_scope.key; }
    void    setClubScope(const QString &key);
    QVariantList clubScopes() const { return m_clubScopes; }
    // Against the combined key (SHOT FILTER), so a filter still being reduced counts as refreshing.
    bool clubScopeRefreshing() const { return m_summaryScope != scopeKey(); }
    bool matchupsRefreshing() const { return m_togetherScope != scopeKey(); }
    bool clubScopeWorking() const { return m_scopeWorking; }

    QString shotFilter() const { return m_filter.key(); }
    void    setShotFilter(const QString &key);
    QVariantMap shotFilterOptions() const { return m_filterOptions; }
    QString shotFilterLine() const;
    QString outcomeTooFewNote() const;

    // Rescan the athlete's sessions and re-derive whatever is stale. Cheap when nothing is.
    Q_INVOKABLE void refresh();
    // A condition's name as the items show it: the pack label, else the id ("" for "").
    Q_INVOKABLE QString conditionLabel(const QString &id) const;

    // Test seam, as SessionDiagnosticsModel's: scan, derive and reduce the themes inline.
    void setSynchronous(bool on) { m_synchronous = on; }
    // Waits for the scan, the derivation, the swing summary AND the consistency marks.
    bool waitForIdle(int msTimeout = 60000);
    // For the tests: marks jobs started, and ledgers they have read between them.
    int  consistencyJobs() const { return m_marksJobs; }
    int  consistencyLedgersRead() const { return m_marksLedgersRead; }

    static QString recordPath(const QString &sessionDir);
    // Tendencies as they stood at this session: its own folder and the ones before it in the athlete's. Reads the
    // ledgers and writes nothing; any thread. For the golfer report, so a report remade later still says the same.
    static pinpoint::analysis::TendencySet tendenciesAsOf(const QString &sessionDir, const pinpoint::analysis::ShotOutcomePack &pack,
                                                          const pinpoint::analysis::INormProvider &norms);
    static QString themesPath(const QString &athleteDir);
    static QString themesPath(const QString &athleteDir, const pinpoint::analysis::ClubScope &scope);
    // A club × shot filter choice's: swing_themes.<club>.<filter>.json; the club scope's own file under All.
    static QString themesPath(const QString &athleteDir, const pinpoint::analysis::ClubScope &scope,
                              const pinpoint::analysis::ShotFilter &filter);
    static QString tendenciesPath(const QString &athleteDir);
    // For the tests: tendency reductions actually run (a stored file or an unchanged library counts for none).
    int  tendencyJobs() const { return m_tendencyJobs; }

signals:
    void athleteDirChanged();
    void gradePolicyChanged();
    void pausedChanged();
    void itemsChanged();
    void detailsChanged();
    void clubScopeChanged();
    void clubScopeStateChanged();
    void updatingChanged();
    void summaryChanged();
    void areasChanged();
    void tendenciesChanged();
    // A new set of tendencies is in hand (a reduction or the stored file), and not the busy flag moving:
    // the cue for anything that pays to read the file again.
    void tendenciesLanded();

private:
    struct Scanned {
        QString sessionDir;
        QString fingerprint;
        bool    fresh = false;
        qint64  mtimeMs = 0;
        bool    hasBaseline = false;         // <session>/club_baseline.json exists
        QDate   day;                         // the folder's day: what the catch-up order goes by when one is missing
        pinpoint::analysis::SessionWorkOns record;   // valid when fresh
    };
    static std::vector<Scanned> scan(const QString &athleteDir, const QString &contentStamp);
    void applyScan(int generation, std::vector<Scanned> rows);

    void deriveNext();
    void onModelSettled();
    void finishDerive();
    void publish();
    const QHash<QString, pinpoint::analysis::SessionWorkOns> &shownRecords() const;
    void updateClubScope();
    // `clubMoved`: the club choice moved, not only the filter; the consistency marks read the club choice only.
    void onClubScopeMoved(bool clubMoved = true);
    void setScopeWorking(bool on);
    // The combined key (SHOT FILTER): the club's under All, else club|filter. "all" for All × All.
    static QString scopeKeyOf(const pinpoint::analysis::ClubScope &club, const pinpoint::analysis::ShotFilter &filter);
    QString scopeKey() const { return scopeKeyOf(m_scope, m_filter); }
    // What a request resolves to within the club choice: each part kept when the club choice's judged shots show
    // that value, else "any". All until the tendencies are in hand.
    pinpoint::analysis::ShotFilter resolveShotFilter(const pinpoint::analysis::ShotFilter &asked) const;
    // shotFilterOptions' shape, for the club choice and `chosen` (the counts keep the other factors' choices).
    QVariantMap shotFilterOptionsFor(const pinpoint::analysis::ShotFilter &chosen) const;
    // "mid-iron", "7 iron", "driver": the club choice's word before "shots" in the filter's line; "" for every club.
    QString clubScopeWords() const;
    bool onBallInScope(const QString &tendencyKey) const;
    void publishAreas(const pinpoint::analysis::CharacteristicPack &pack,
                      const std::vector<pinpoint::analysis::SessionWorkOns> &sessions);
    SessionDiagnosticsModel *model();

    // ── Shot filter ──
    // One filter choice as the screens show it: what the job counted for it, and the words to say it with.
    struct ShownOutcome {
        pinpoint::analysis::ShotFilter scope;       // All: nothing below is said
        QString clubWords;          // "mid-iron"; "" for every club (clubScopeWords)
        int     chosen = 0;         // judged shots in the choice
        int     sessions = 0;       // sessions holding one of them
        int     others = 0;         // the other judged shots in the club choice
        int     measured = 0;       // chosen shots with at least one fault row assessed (not NotAssessable)
        QHash<QString, pinpoint::analysis::OutcomeContrast> contrasts;   // fault condition id -> its counts
    };
    // The line under one fault: outcomeContrastLine over its counts, "" under All or with none.
    static QString outcomeContrastFor(const ShownOutcome &shown, const QString &conditionId);
    static QJsonObject outcomeToJson(const ShownOutcome &shown);
    // Fills `shown`'s counts from a stored file's block; false when it is not this choice's.
    static bool outcomeFromJson(const QJsonObject &o, ShownOutcome &shown);

    // ── Your swing ──
    struct ThemesJob {
        int     generation = 0;
        QString athleteDir;
        QString fingerprint;
        QString contentStamp;
        QStringList sessionDirs;    // name order
        QHash<QString, pinpoint::analysis::ThemeMeasureInfo>   measures;
        QHash<QString, pinpoint::analysis::ThemeConditionInfo> conditions;
        pinpoint::analysis::ClubScope scope;
        bool    announce = false;   // a screen is showing another scope: land Layer 1 early and say so
        QHash<QString, pinpoint::analysis::SessionWorkOns> records;     // scoped only: session dir -> its stored record
        std::shared_ptr<const pinpoint::analysis::CharacteristicPack> pack;   // scoped only
        // A shot filter only: what judges each shot (the tendency reduction's pack, and the context tree its
        // club groups come from), the fault conditions the contrast counts, and the words the screens say it with.
        pinpoint::analysis::ShotFilter outcome;
        QString clubWords;
        std::shared_ptr<const pinpoint::analysis::ShotOutcomePack> outcomePack;
        std::shared_ptr<const pinpoint::analysis::INormProvider>   norms;
        QSet<QString> faultIds;
    };
    struct ThemesOutcome {
        bool    fromDisk = false;   // the stored summary was current; nothing was reduced
        bool    computed = false;   // reduced here (and not cancelled)
        pinpoint::analysis::SwingThemes themes;
        QString fingerprint;        // the job's, carried back
        QString contentStamp;
        QString athleteDir;
        qint64  wallMs = 0;
        pinpoint::analysis::ClubScope scope;
        QHash<QString, pinpoint::analysis::SessionWorkOns> records;     // scoped only
        ShownOutcome outcome;       // the job's choice, with its counts once reduced or read
    };
    QString themesFingerprint() const;
    void maybeThemes();
    void cancelThemes();
    ThemesOutcome runThemes(const ThemesJob &job, const std::atomic<bool> *cancel);
    void applyThemes(int generation, ThemesOutcome out);
    void applyScopeSummary(int generation, const QString &scopeKey,
                           QHash<QString, pinpoint::analysis::SessionWorkOns> records,
                           pinpoint::analysis::SwingThemes layerOne, ShownOutcome outcome);
    void applyScopeWorking(int generation);
    void publishSummary();
    void clearSummary();

    // ── Consistency marks ──
    struct MarksRequest {
        QString conditionId;
        QString measureId;
        QString unit;
        bool    ungraded = false;       // decided from the pack: no measure, yes/no, composite, an outcome (R4)
        pinpoint::variability::ScaleType scaleType = pinpoint::variability::ScaleType::Interval;
    };
    struct MarksSession {
        QString dir;
        qint64  startMs = 0, endMs = 0;
    };
    struct MarksJob {
        int     generation = 0;
        QString seed;                   // the rule, the content stamp and the requests
        QString previous;               // the fingerprint of the grades in hand
        std::vector<MarksSession> sessions;     // newest first
        std::vector<MarksRequest> requests;
        std::shared_ptr<const pinpoint::analysis::INormProvider> norms;
        pinpoint::analysis::ClubScope scope;
    };
    struct MarksOutcome {
        QString fingerprint;
        bool    unchanged = false;      // the ledgers and requests are those of the grades in hand
        bool    computed  = false;      // graded here (and not cancelled)
        int     ledgersRead = 0;
        QHash<QString, pinpoint::variability::VariabilityResultView> marks;   // markKey -> mark
    };
    // A set of records in final words: the list, every condition's detail, and the marks those ask for.
    struct DescribedWorkOns {
        QVariantList items;
        QHash<QString, QVariantMap> details;
        QHash<QString, MarksRequest> markRequests;
        int cleared = 0;
    };
    // `outcome`, when given, is the choice the records are of: each detail (never an item) carries its
    // outcomeContrast line.
    DescribedWorkOns describeWorkOns(const QHash<QString, pinpoint::analysis::SessionWorkOns> &records,
                                     const ShownOutcome *outcome = nullptr);
    // What the three scoped tabs read: details and areas, from the records of the scope they show.
    // `known` is that scope's description when the caller already has it.
    void publishScreens(const DescribedWorkOns *known = nullptr);
    static QString markKey(const QString &conditionId, const QString &measureId);
    static MarksOutcome runMarks(const MarksJob &job, const std::atomic<bool> *cancel);
    void maybeMarks();
    void cancelMarks();
    void applyMarks(int generation, MarksOutcome out);
    QVariant markFor(const QString &conditionId, const QString &measureId) const;

    // ── Tendencies ──
    struct TendenciesJob {
        int     generation = 0;
        QString athleteDir;
        QString seed;                   // the rule, schema, content stamp and every session's work-ons fingerprint
        QString previous;               // the fingerprint of the tendencies in hand
        QString contentStamp;
        QStringList sessionDirs;        // name order
        pinpoint::analysis::ShotOutcomePack pack;
        std::shared_ptr<const pinpoint::analysis::INormProvider> norms;
    };
    struct TendenciesOutcome {
        QString fingerprint;
        QString contentStamp;
        QString athleteDir;
        bool    unchanged = false;      // the ledgers are those of the tendencies in hand
        bool    fromDisk = false;       // the stored file was current
        bool    computed = false;       // reduced here (and not cancelled)
        pinpoint::analysis::TendencySet set;
    };
    void maybeTendencies();
    void cancelTendencies();
    static TendenciesOutcome runTendencies(const TendenciesJob &job, const std::atomic<bool> *cancel);
    void applyTendencies(int generation, TendenciesOutcome out);
    void publishTendencies();
    void clearTendencies();
    QVariantMap onBallFor(const QString &conditionId) const;
    QString onTheBallFor(const QString &conditionId) const;
    const pinpoint::analysis::ShotOutcomePack &outcomePack() const;

    QString m_athleteDir;
    QString m_gradePolicy = QStringLiteral("standard");
    bool    m_paused = false;
    bool    m_synchronous = false;

    // session dir -> its current record. Rebuilt by every scan, added to by every derivation.
    QHash<QString, pinpoint::analysis::SessionWorkOns> m_records;
    QHash<QString, QString> m_fingerprints;   // session dir -> what the queue will stamp
    QHash<QString, qint64>  m_dirMtime;
    QStringList m_queue;                      // stale session dirs, newest first
    QString     m_derivingDir;
    bool        m_deriving = false;
    bool        m_scanning = false;
    bool        m_rescan   = false;
    int         m_generation = 0;
    int         m_sessionsFound = 0;

    QVariantList m_items;
    QHash<QString, QVariantMap> m_details;   // conditionId -> faultDetail()
    int          m_detailsRevision = 0;
    int          m_clearedCount = 0;
    QVariantMap  m_lastSession;
    QVariantList m_areaItems;          // publish()'s, stored: QML reads it many times per frame

    SessionDiagnosticsModel *m_model = nullptr;   // created on first need; owned (child)
    // Lazily made; mutable so conditionLabel() (const) can make it on first need.
    mutable std::unique_ptr<pinpoint::analysis::ICharacteristicPackProvider> m_packProv;
    QThreadPool m_pool;

    // ── Your swing ──
    pinpoint::analysis::SwingThemes m_themes;      // valid when m_summaryReady
    QString      m_themesFingerprint;              // what m_themes was reduced from
    QString      m_themesJobFingerprint;           // what the running job will produce
    bool         m_summaryReady  = false;
    bool         m_themesRunning = false;
    int          m_themesGeneration = 0;
    std::shared_ptr<std::atomic<bool>> m_themesCancel;   // the running job's
    QString      m_summarySubtitle;
    QVariantMap  m_focusItem;
    QVariantList m_doWellItems;
    QVariantList m_nextItems;
    QVariantList m_togetherItems;
    QString      m_togetherNote;
    QThreadPool  m_themesPool;          // one job at a time
    QThreadPool  m_replicatePool;       // that job's replicates

    // ── Club scope ──
    QString      m_scopeRequested = pinpoint::analysis::clubScopeAllKey();    // as asked; may name no option yet
    pinpoint::analysis::ClubScope m_scope;                                    // what it resolves to
    QString      m_summaryScope  = pinpoint::analysis::clubScopeAllKey();     // what Coaching and Your swing show
    QString      m_togetherScope = pinpoint::analysis::clubScopeAllKey();     // what Matchups shows
    std::vector<pinpoint::analysis::ClubScopeGroupOption> m_scopeOptions;
    QVariantList m_clubScopes;
    QHash<QString, pinpoint::analysis::SessionWorkOns> m_scopedRecords;       // valid while m_summaryScope is not all
    bool         m_scopeWorking = false;

    // ── Shot filter ──
    QString      m_filterRequested = pinpoint::analysis::shotFilterAllKey();   // as asked; a part may name no option yet
    pinpoint::analysis::ShotFilter m_filter;                                   // what it resolves to
    QVariantMap  m_filterOptions;
    ShownOutcome m_shownOutcome;        // the filter m_summaryScope is of, with its counts

    // ── Tendencies ──
    pinpoint::analysis::TendencySet m_tendencySet;      // valid when m_tendenciesReady
    QString      m_tendencyFingerprint;                 // what m_tendencySet was reduced from
    bool         m_tendenciesReady = false;
    bool         m_tendenciesRunning = false;
    int          m_tendenciesGeneration = 0;
    int          m_tendencyJobs = 0;
    std::shared_ptr<std::atomic<bool>> m_tendenciesCancel;   // the running job's
    QVariantList m_tendencyItems;
    QString      m_tendencyNote;
    mutable std::unique_ptr<pinpoint::analysis::ShotOutcomePack> m_outcomePack;   // marshalled once, on demand

    // ── Consistency marks ──
    QHash<QString, pinpoint::variability::VariabilityResultView> m_marks;   // markKey -> the mark shown
    QString      m_marksFingerprint;                     // what m_marks was graded from
    QHash<QString, MarksRequest> m_markRequests;         // markKey -> what publish() asks for
    bool         m_marksRunning = false;
    bool         m_marksAgain   = false;                 // asked while a job ran
    int          m_marksGeneration = 0;
    int          m_marksJobs = 0;
    int          m_marksLedgersRead = 0;
    std::shared_ptr<std::atomic<bool>> m_marksCancel;    // the running job's
    QThreadPool  m_marksPool;                            // one job at a time
};
