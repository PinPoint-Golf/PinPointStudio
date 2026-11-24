/*
 * Copyright (c) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#pragma once

// Included rather than forward-declared: the QML type registration takes the address of
// a ShotListModel* property, and a pointer meta-type has to point at a complete type.
#include "shot_list_model.h"

#include <QList>
#include <QObject>
#include <QPointer>

#include <functional>
#include <QQmlEngine>
#include <QString>
#include <QVariantList>
#include <QHash>
#include <QVariantMap>

#include <memory>
#include <optional>
#include <vector>

// Forward-declared rather than included: buildGraphics() only takes references, and
// pulling lm_session_reductions.h in here would drag the whole launch monitor field
// catalogue into every translation unit that touches the model.
//
// The two registries are forward-declared for the same reason and held behind smart
// pointers, which is why this class now declares a destructor it does not need: a
// unique_ptr to an incomplete type has to be destroyed where the type is complete. This is
// the one QML façade that resolves a corridor (through metric_corridor.h).
// LmShotValues has one home, lm_session_reductions.h; this header names no LmShotValues, so it does not repeat the
// alias (namespace_collision_test: one definition per name, PR-39).
namespace pinpoint::analysis {
struct LmFieldStats;
class ICharacteristicPackProvider;
class INormProvider;
}

// ── The variability marks on this panel (docs/design/system/variability.md; decisions.md §2, R4, R4b, R8) ──
namespace pinpoint::lm {

// R4b: the readings that are CLUB DELIVERY — movement, so a consistency grade and a typicality are allowed, per
// club: every launch monitor reading that is not an outcome (isOutcomeMetric, the one rule every surface uses: ball
// flight and smash factor). Club speed, path, face, face to path, attack, dynamic and spin loft, lie, closure, low
// point and strike location and height.
bool lmIsClubDelivery(const QString &key);

// ── ONE SHOT AS THE BOARD COUNTS IT (audit F-03) ──
// The board's own facts about a shot, after the golfer's two annotations and the club evidence are applied:
// its EFFECTIVE club (the one rule, club_vocabulary.h effectiveClub: the golfer's override, else the recorded club; the
// club evidence only flags, refactor 6.2, except on a legacy "DRIVER" stub: J-106), whether the golfer LEFT IT OUT, and
// its raw readings. Every surface that quotes a launch monitor
// figure for the session reads these — the board itself and the session report's cover, numbers table, swing grid,
// reference swing and appendix — so a shot the golfer left out or re-labelled is treated one way everywhere.
// LmSessionModel::boardShots() publishes them; LmSessionModel's own rows ARE these (it derives from this struct).
struct LmBoardShot {
    int          id = -1;          // the shot list's shot id
    int          ordinal = 0;      // capture order
    QString      swingDir;         // where its document is ("" for a shot never written)
    QString      recorded;         // the club it was recorded under (ShotListModel::RecordedClubRole: the uncorrected club)
    QString      club;             // the EFFECTIVE club — the one every figure groups by
    QString      source;           // "recorded" | "user" | "evidence" (a legacy "DRIVER" stub the evidence relabelled:
                                   // J-106, interim; the evidence only flags every other shot since refactor 6.2)
    bool         excluded = false; // "Leave out" on the board (review.excluded)
    QHash<QString, double> values; // the raw readings, keyed "lm.carryDistance" (LmShotValues)

    // THE ONE RULE for whether a shot is in the figures: not left out, and the monitor read it. A left-out shot is
    // still listed (and drawn hollow) — it is in no mean, median, SD, grade, corridor, count, tile or ellipse.
    bool inFigures() const { return !excluded && !values.isEmpty(); }
    // A reading, or NaN when the shot does not carry it.
    double reading(const QString &key) const;
    // Re-labelled: its effective club is not the one it was recorded under.
    bool relabelled() const;
};

// Two club names are one club when they match ignoring case and surrounding space ("7 IRON" / "7 iron").
bool lmSameClub(const QString &a, const QString &b);
// Bag order, driver → wedges (lmClubBagRank), ties by name: the order the board and the report list clubs in.
bool lmClubBagLess(const QString &a, const QString &b);

// ── THE BOARD'S TYPED BANDS (refactor 6.1b, lint A6): what the panel's reading index and ledger read ──
// One tile: one reading of the scope, formatted here (lmValueText, lmMeanText, lmSdText) so the panel and any export
// cannot disagree about what a reading looks like. Named as the old tile map's keys, so the QML reads them as before.
struct LmTile
{
    Q_GADGET
    Q_PROPERTY(QString key       MEMBER key       FINAL)
    // "" | "ideal" | "good" | "watch" | "action": the focused shot against its corridor (gradesFor in the .cpp). The panel
    // paints only the last two; "" is three different facts (no corridor, no reading, an implausible value) drawn alike.
    Q_PROPERTY(QString grade     MEMBER grade     FINAL)
    Q_PROPERTY(QString label     MEMBER label     FINAL)
    Q_PROPERTY(QString abbrev    MEMBER abbrev    FINAL)
    Q_PROPERTY(QString unit      MEMBER unit      FINAL)
    Q_PROPERTY(QString group     MEMBER group     FINAL)
    Q_PROPERTY(QString latest    MEMBER latest    FINAL)
    Q_PROPERTY(QString mean      MEMBER mean      FINAL)
    Q_PROPERTY(QString sd        MEMBER sd        FINAL)
    Q_PROPERTY(int     n         MEMBER n         FINAL)
    Q_PROPERTY(bool    hasLatest MEMBER hasLatest FINAL)
    Q_PROPERTY(bool    hasSpread MEMBER hasSpread FINAL)
    Q_PROPERTY(double  z         MEMBER z         FINAL)
    // The strip's tick position (lmTickPercent), decided here for the same reason as the numbers.
    Q_PROPERTY(double  tickPct   MEMBER tickPct   FINAL)
public:
    QString key, grade, label, abbrev, unit, group, latest, mean, sd;
    int     n = 0;
    bool    hasLatest = false, hasSpread = false;
    double  z = 0.0, tickPct = 50.0;
    friend bool operator==(const LmTile &, const LmTile &) = default;
};

// One band of the board: a gutter rule, a name, a count, then its tiles in fieldDefs() order. A band no shot produced
// a reading for is not published at all.
struct LmBand
{
    Q_GADGET
    Q_PROPERTY(QString band  MEMBER band  FINAL)
    Q_PROPERTY(int     count READ count   FINAL)
    Q_PROPERTY(QList<pinpoint::lm::LmTile> tiles MEMBER tiles FINAL)
public:
    QString       band;
    QList<LmTile> tiles;
    int count() const { return int(tiles.size()); }
    friend bool operator==(const LmBand &, const LmBand &) = default;
};

// ── THE ONE LAUNCH MONITOR REDUCTION (refactor 6.1b; R-INS-4; 5.6b's C11) ──
// The board's statistics, scope, corridors, ellipses, club evidence and graphics, as ONE pure function of the session's
// rows and what it is asked. The board (LmSessionModel, the presenter of the session on screen) and the session report's
// launch monitor figures (session_report_model.cpp launchMonitorPart) both call it, each with its own asks, so the report
// can count no shot the board left out and needs no board of its own. No QObject, no file: the rows carry the Shot's
// measurements (Shot::measurements, read through the shot list: refactor 6.1a).

// The IMPACT card's low point candidates for one shot that are NOT launch monitor readings (the device's own low point,
// `lm.lowPointAhead`, is one, in `values`): what PinPoint derives from the monitor's attack angle (`lowPointFromAttack`,
// with its σ) and our camera's estimate. Kept out of `values` for the reason opticalLowPointFor gives; the one precedence
// rule orders all three (buildGraphics; refactor 6.4, D-4).
struct LmLowPointOffer {
    std::optional<double> fromAttack;        // lowPointFromAttack at impact
    std::optional<double> fromAttackSigma;   // its 1σ, as the writer stored it
    std::optional<double> optical;           // our own (camera) lowPointAhead at impact
};

// One row of the session as the board reads it, in the shot list's order (newest first).
struct LmBoardInput {
    int                    id = -1;          // the shot list's row id (the panel's actions and its focus use it)
    int                    ordinal = 0;      // capture order
    QString                swingDir;         // "" for a shot never written
    QString                recorded;         // the club it was recorded under (trimmed)
    QString                clubOverride;     // the golfer's choice on the board (review.clubOverride), trimmed, or ""
    bool                   legacyClubStub = false; // a stored "DRIVER" stub (ShotListModel::LegacyClubStubRole; J-106)
    bool                   excluded = false; // "Leave out" (review.excluded)
    QString                kind;             // the device (lm_device.h key), "" when none measured it
    QHash<QString, double> values;           // the launch monitor's readings, keyed "lm.carryDistance"
    LmLowPointOffer        lowPoint;         // the low point candidates that are not readings (never in `values`)
    QVariantMap            gates;            // the gate behind each launch monitor reading the shot lacks, by key
};

// What the board is asked: the presenter's inputs, or the report's.
struct LmBoardAsk {
    bool    hasList = true;        // false: no shot list at all (the board has nothing to board)
    int     focusedShotId = -1;    // the shot the stage shows, or −1 (the newest in scope is headlined)
    bool    reviewing = false;     // a loaded session's rows rather than the live one's
    bool    connected = false;     // a monitor is configured (live only)
    bool    leftHanded = false;
    QString deviceName;            // the live connector's short name
};

// The corridors' sources: the pack and norm set every grade on the board is read against, and the grade policy.
struct LmBoardRules {
    const pinpoint::analysis::ICharacteristicPackProvider *pack = nullptr;
    const pinpoint::analysis::INormProvider               *norms = nullptr;
    QString                                                policyName = QStringLiteral("standard");
};

// The reduction's answer.
struct LmBoard {
    QList<LmBand>             bands;          // the tiles board, in display order
    QVariantMap               graphics;       // everything graphics mode draws (LmSessionModel::graphics)
    std::vector<LmBoardShot>  shots;          // every shot as counted, oldest first (LmSessionModel::boardShots)
    QString                   scopeText;
    QString                   valueLabel;
    QString                   corridorScope;
    int                       shotCount = 0;
    bool                      anyReadings = false;
    bool                      focusFound = false;
    bool                      headlineHasReadings = false;
};

// The rows of a shot list, read once: identity, the recorded club, the golfer's two annotations, the device, and from
// each row's Shot the readings, our own low point and the gate records (ShotListModel::measurementsOf). `detail`, when
// given, reads a row's in-memory document instead (the tests' seam, LmSessionModel::setDetailReader).
std::vector<LmBoardInput> lmBoardInputsOf(const ShotListModel &shots,
                                          const std::function<QVariantMap(const QString &swingDir)> &detail = {});

// THE reduction. `shownGrades` is the live board's hysteresis memory (§8.9), "club|key" → the grade shown last; null, or
// a reviewed session, keeps none (it is cleared when reviewing).
LmBoard lmBoardOf(const std::vector<LmBoardInput> &rows, const LmBoardAsk &ask, const LmBoardRules &rules,
                  QHash<QString, int> *shownGrades = nullptr);

// The same, against the pack and norm set published now (RuleData's current pack, the shared norm set): a reader with
// no board of its own and no edit to follow (the session report, once per make).
LmBoard lmBoardOfNow(const std::vector<LmBoardInput> &rows, const LmBoardAsk &ask, const QString &policyName);

// The one line shown INSTEAD of the board, or empty when it renders (LmSessionModel::emptyText), and the line the This
// shot view shows when the headline shot has no reading (shotEmptyText). From the board and the two live gates, so a
// monitor connecting needs no new reduction.
QString lmBoardEmptyText(const LmBoard &b, bool reviewing, bool connected);
QString lmBoardShotEmptyText(const LmBoard &b, bool reviewing, bool connected);

} // namespace pinpoint::lm

// The launch monitor session board's presenter (PpLaunchMonitorPanel).
//
// Every number is lmBoardOf()'s, the one launch monitor reduction above (refactor 6.1b): this class holds what the
// board is asked (the focus, the two live gates, the device, the policy, the hand), follows its shot list and the
// rule data, and publishes the answer. NOT MADE BY A VIEW (lint A2; information model C10): the session on screen's
// board is SessionReviewController::lmBoard, beside the shot list it boards (activeShots), and the panel holds it while
// it is up (holdLmBoard / releaseLmBoard). With no panel holding it the board boards no list, so a golfer with no
// monitor and the panel off (the default) pays for no rebuild on every shot.
//
// ONE ROW PER BAND, not per field, which is where this departs from the brief's shape.
// The board is banded — a gutter rule, a name, a count, then that band's tiles — and a
// flat field-per-row model cannot express that without either a proxy per band or
// DelegateModel groups populated from JS. A band row carrying its own tiles is the same
// data in the shape the thing being drawn actually has, and it keeps ONE projection of
// the statistics rather than two that can disagree.
//
// WHERE THE NUMBERS COME FROM, and why not the obvious place. ShotListModel's
// MetricsRole looks like the right input — it is a map keyed by exactly these metric
// keys — but its values are DISPLAY STRINGS, and SwingDocReader rounds every degree
// metric to a whole degree on the way in. Fifteen of the twenty-five reading fields are
// in degrees, so a standard deviation computed from that map would not be a suppressed
// statistic, it would be a wrong one printed confidently. The row's launch monitor slice (`series`)
// carries the raw doubles the writer put there, and it is populated by the same code
// path for a live shot and a reloaded one, so there is one source here and not two. Since refactor 6.1a every row a
// device measured is read from its SHOT'S MEASUREMENTS (Shot::measurements, through the carousel: the one source-tagged
// set, R-INS-1/2), filtered to the launch monitor's readings — never a read of the document of the board's own, and
// never the whole detail through QML (H-07: the 2–2.5 minute freeze of #173). The Shot keeps its set.
//
// AND THE ONE JUDGEMENT IT DOES MAKE: whether the focused shot's reading sits outside its
// corridor. Every tile and every annotation carries a `grade` — "ideal" | "good" | "watch"
// | "action", or empty — resolved through the norm set at the scope's own club context.
// The panel paints only the last two, so a reading inside its corridor still looks exactly
// as it did: no green, no reassurance, and no colour on a metric nobody has authored a
// norm for. That is the point of resolving it here rather than compiling a table — 7 of
// the 25 reading measures carry a norm today, the rest say nothing rather than guessing,
// and authoring one in the model browser lights its tile with no change to this file.
//
// IT DOES NOT LIVE IN lm_session_reductions.h AND MUST NOT MOVE THERE. That header is a
// READOUT — the golfer against their own other shots, no norm and no grade — and it stays
// one. The join to the norm set is a projection for a surface to paint, which is this
// class's job, and it is why the dispersion strip and the corridor cannot be confused:
// the strip is geometry the reductions computed, the corridor is colour resolved here.
class LmSessionModel : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("the launch monitor board belongs to the session on screen: sessionReviewController.lmBoard")

    // The session's shots. Any ShotListModel — the panel passes whichever list the
    // screen it sits on is showing, so a review screen boards its own session.
    Q_PROPERTY(ShotListModel *shotModel READ shotModel WRITE setShotModel NOTIFY shotModelChanged)
    // The swing the rest of the stage is showing (app.state.selection, as the shot model's row id). -1 for
    // "nothing focused", which falls back to the newest shot in scope.
    Q_PROPERTY(int focusedShotId READ focusedShotId WRITE setFocusedShotId NOTIFY focusedShotIdChanged)

    // §5's gating facts, passed in rather than read here: the panel already has
    // `launchMonitor` and `appSettings` in scope, and a model that reached for the
    // singletons itself would be untestable and would couple the board to main.cpp.
    Q_PROPERTY(bool connected READ connected WRITE setConnected NOTIFY stateTextChanged)
    // Whether the shots being boarded are a LOADED session's rather than the live one's.
    // It changes no reading and no statistic — only which empty line is honest, and
    // whether the live device's name may head the scope. `connected` is a fact about
    // capturing the NEXT shot; it cannot unwrite readings already on disk, so it may not
    // hide them.
    Q_PROPERTY(bool reviewing READ reviewing WRITE setReviewing NOTIFY stateTextChanged)
    // "GC Quad" — the short device name for the header. Empty is fine; the scope line
    // then starts with the club.
    Q_PROPERTY(QString deviceName READ deviceName WRITE setDeviceName NOTIFY headerChanged)
    // The pack-wide grade policy, by NAME ("standard" | "strict" | "lenient"), passed in
    // for the same reason as the gates above: the panel has `appSettings` in scope. It is
    // the same property ChartsRelationsPresenter exposes and it must be bound to the same setting —
    // a corridor graded against the default while the golfer has chosen Strict would put a
    // different colour on this panel than on the other surfaces for one reading.
    Q_PROPERTY(QString gradePolicy READ gradePolicy WRITE setGradePolicy NOTIFY gradePolicyChanged)

    // The athlete's handedness, passed in for the same reason as `connected`: the panel already has athleteController in scope and a model that
    // reached for the singleton itself would be untestable. It changes only the two
    // INFERRED reads' wording and the strike diagram's mirroring — never a reading.
    Q_PROPERTY(bool leftHanded READ leftHanded WRITE setLeftHanded NOTIFY graphicsChanged)

    // "GC Quad · 7 iron · 18 shots". States the scope in words, because a mean whose
    // scope you cannot see is a number you cannot use.
    Q_PROPERTY(QString scopeText READ scopeText NOTIFY headerChanged)
    // "latest" when the focused shot IS the newest in scope, "this shot" when it is
    // not. The header must never claim something the number isn't.
    Q_PROPERTY(QString valueLabel READ valueLabel NOTIFY headerChanged)
    // The one line the panel shows INSTEAD of the board, or empty when the board
    // renders. Empty is the only "we are fine" signal — see §5.
    Q_PROPERTY(QString emptyText READ emptyText NOTIFY stateTextChanged)
    // The board is up (emptyText is empty) but the shot it headlines carries no reading: the
    // line the This shot view shows in place of the readings, said about THE SHOT (the one
    // picked, or the latest when none is), never the session — the session has readings, or
    // emptyText would have said so. Empty when the headline shot has readings, and whenever
    // emptyText is not. The Session view gates on graphics.sessionHas instead.
    Q_PROPERTY(QString shotEmptyText READ shotEmptyText NOTIFY stateTextChanged)
    // Shots in scope (the club's, or the session's when the club is unknown).
    Q_PROPERTY(int shotCount READ shotCount NOTIFY headerChanged)
    // WHICH CORRIDORS THESE ARE, in words — "7 iron", "Driver" — or empty when the scope
    // graded nothing. Empty is the honest answer in two different situations that look the
    // same on screen (no reading has a norm; the club is unknown so the scope is the whole
    // session), and in both of them the panel must not print a legend for colour it is not
    // showing. It names the context ASKED FOR rather than the one a norm was found at: the
    // tree's whole point is that a wedge row can answer a lob wedge, and a legend reading
    // "Wedge" over a board scoped to a lob wedge would misdescribe the scope, not the norm.
    Q_PROPERTY(QString corridorScope READ corridorScope NOTIFY headerChanged)
    // Tiles per band, in row order. The panel sizes itself to fit the whole board on
    // screen, and it cannot do that arithmetic without knowing the shape up front —
    // a Repeater only reveals a band's tile count once the band is already laid out.
    Q_PROPERTY(QList<int> bandCounts READ bandCounts NOTIFY headerChanged)
    // THE BANDS THEMSELVES, typed (refactor 6.1b): one per band of readings, in display order — its name, its count and
    // its tiles (LmTile above). Published with the graphics, at the end of every rebuild.
    Q_PROPERTY(QList<pinpoint::lm::LmBand> bands READ bands NOTIFY graphicsChanged)

    // EVERYTHING GRAPHICS MODE DRAWS, in one map. See graphics() in the .cpp for the
    // shape and for why it is one property rather than a second model: the two modes
    // are two projections of ONE set of statistics, computed once in rebuild(), and a
    // separate model would be a second chance for the board and the schematics to
    // disagree about the same shot.
    //
    // The FSX panel's session half reads four more keys (docs/design/lm_panel_fsx.md, "Model
    // contract" — the names are binding): `shots` (every shot, its effective club, whether it
    // is left out, its raw readings and its club evidence), `clubs` (per effective club: the
    // statistics and the two enclosing ellipses, dispersion and strike), `mismatch` (the note) and `device` (whose
    // club markers the Impact face carries). See sessionGraphics() in the .cpp.
    //
    // THE VARIABILITY MARKS (the technical layer; docs/design/system/variability.md). Two more keys:
    //   clubs[].stats[key].variability   a club-delivery reading's per-club grade (R4b; lmIsClubDelivery),
    //                                    the `variabilityResult` fields as a map — clubDeliveryGrade().
    //                                    Ball flight and smash factor never carry one (R4).
    //   typicality[key]                  the headline shot against the session's OTHER shots of its club,
    //                                    not left out, with that reading (R8: at least N_MIN of them):
    //                                    { values, basisText }, the Typicality badge's. Absent: no result.
    //                                    Every reading, ball included: typicality is not a grade.
    Q_PROPERTY(QVariantMap graphics READ graphics NOTIFY graphicsChanged)

public:
    explicit LmSessionModel(QObject *parent = nullptr);
    ~LmSessionModel() override;

    ShotListModel *shotModel() const { return m_shots; }
    // How a row's measurements are read: its Shot's set by default (ShotListModel::measurementsOf; refactor 6.1a). The
    // tests hand in their in-memory documents (the old slice's shape: `series`, `gates`), parsed as a Shot's would be.
    void setDetailReader(std::function<QVariantMap(const QString &swingDir)> reader) { m_detailReader = std::move(reader); }
    void           setShotModel(ShotListModel *m);
    int            focusedShotId() const { return m_focusedShotId; }
    void           setFocusedShotId(int id);
    bool           connected() const { return m_connected; }
    void           setConnected(bool on);
    bool           reviewing() const { return m_reviewing; }
    void           setReviewing(bool on);
    QString        deviceName() const { return m_deviceName; }
    void           setDeviceName(const QString &name);
    bool           leftHanded() const { return m_leftHanded; }
    void           setLeftHanded(bool on);
    QString        gradePolicy() const { return m_policyName; }
    void           setGradePolicy(const QString &name);

    QString scopeText()  const { return m_board.scopeText; }
    QString valueLabel() const { return m_board.valueLabel; }
    QString      emptyText()  const;
    QString      shotEmptyText() const;
    int          shotCount()  const { return m_board.shotCount; }
    QString      corridorScope() const { return m_board.corridorScope; }
    QList<int>   bandCounts() const;
    QList<pinpoint::lm::LmBand> bands() const { return m_board.bands; }
    QVariantMap  graphics()   const { return m_board.graphics; }
    // Every shot of the list as the last rebuild counted it, oldest first (capture order): the effective club, left
    // out or not, the raw readings. The C++ face of graphics.shots (lmBoardOf's `shots`). Empty with no shot list.
    const std::vector<pinpoint::lm::LmBoardShot> &boardShots() const { return m_board.shots; }

    // ── The panel's two inline actions and the mismatch note's Undo (lm_panel_fsx.md) ──
    //
    // All three are PERSISTED, through the shot list's write-through to each shot's review
    // block (ShotListModel::setLmExcluded / setLmClubOverride → RecordingRepository::
    // editReview), so a reload of the session comes back exactly as the golfer left
    // it. They change no reading: they change which shots the board is computed over, and
    // which club it computes them as. The rebuild that follows is the ordinary one, driven by
    // the shot list's dataChanged — there is no second path to keep in step.
    //
    // "Leave out": the shot stays on every figure (hollow), and is left out of every mean, SD,
    // grade, corridor, scope count, tile and ellipse.
    Q_INVOKABLE void setShotExcluded(int shotId, bool excluded);
    // "Club": "" restores the default (the evidence's club, else the recorded one). Choosing
    // the RECORDED club explicitly declines the evidence for that shot — it is stored, so the
    // evidence cannot re-apply on the next load.
    Q_INVOKABLE void setShotClub(int shotId, const QString &club);
    // The note's Undo: every shot the note names as evidence-relabelled is set back to its
    // recorded club, explicitly — i.e. declined, and remembered as declined.
    Q_INVOKABLE void undoMismatch();

signals:
    void shotModelChanged();
    void focusedShotIdChanged();
    void headerChanged();
    void stateTextChanged();
    void graphicsChanged();
    void gradePolicyChanged();

private:
    // Recompute everything. 25 fields across tens of shots is arithmetic no profiler
    // will ever find, so there are no running sums to keep correct across an edit —
    // a club change or a re-read simply rebuilds.
    void rebuild();
    // ONE rebuild on the next turn of the event loop, however many times it is asked for before
    // then. For rows arriving and leaving: a session shown is one reset; shots landing are one insert
    // per row, and the arithmetic is cheap but the panel's redraw is not — ~80 ms a rebuild in a
    // debug build once the rows carry readings, so a 19-shot session took a second to reload at
    // End Session (#52). An edit to a row still rebuilds at once.
    void scheduleRebuild();
    void connectShotModel();

    QPointer<ShotListModel> m_shots;
    // The tests' documents (setDetailReader); default: each row's Shot's measurements (ShotListModel::measurementsOf;
    // refactor 6.1a). The board keeps no slice of its own: the Shot keeps its set, so a rebuild reads no document.
    std::function<QVariantMap(const QString &swingDir)> m_detailReader;
    // Set while undoMismatch() writes several shots: each write emits dataChanged, and one
    // rebuild at the end is the same answer as eight in a row.
    bool                    m_batching = false;
    bool                    m_rebuildPending = false;   // scheduleRebuild() has one queued
    int                     m_focusedShotId = -1;
    bool                    m_connected = false;
    bool                    m_reviewing = false;
    bool                    m_leftHanded = false;
    QString                 m_deviceName;

    // The last answer of the one reduction (lmBoardOf): the bands, the graphics, the shots as counted, the header.
    pinpoint::lm::LmBoard m_board;

    // The two registries a corridor comes from. The pack is a view of RuleData's current one: a saved pack
    // edit (EVT-13) publishes another, and the board re-takes it and grades again at once (followPackSave,
    // refactor 5.7e); m_packOf is the provider it was made over, held, the key. The norm provider is the
    // process-wide cached snapshot, so a board opening costs no file read; a saved corridor edit moves
    // sharedNormProviderGeneration(), and the next grade pass (rebuild(), on the GUI thread) re-takes the
    // set before it grades (followNormSave). Until then the board shows the grades it last drew.
    std::unique_ptr<pinpoint::analysis::ICharacteristicPackProvider> m_pack;
    std::shared_ptr<const pinpoint::analysis::ICharacteristicPackProvider> m_packOf;
    void                                                             followPackSave();
    std::shared_ptr<const pinpoint::analysis::INormProvider>         m_norms;
    quint64                                                          m_normsGeneration = 0;
    void                                                             followNormSave();
    QString                                                          m_policyName;

    // The grade each (club, reading) badge showed last, for the live hysteresis (§8.9): keyed
    // "club|key", lower case. Cleared when reviewing and when the shot list changes.
    QHash<QString, int> m_shownGrades;
};
