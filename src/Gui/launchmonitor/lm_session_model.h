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

#include <QAbstractListModel>
#include <QPointer>
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
namespace pinpoint::analysis {
struct LmFieldStats;
class ICharacteristicPackProvider;
class INormProvider;
using LmShotValues = QHash<QString, double>;
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
// its EFFECTIVE club (resolveClubs in the .cpp: the golfer's override, else a specific evidence relabel, else the
// recorded club), whether the golfer LEFT IT OUT, and its raw readings. Every surface that quotes a launch monitor
// figure for the session reads these — the board itself and the session report's cover, numbers table, swing grid,
// reference swing and appendix — so a shot the golfer left out or re-labelled is treated one way everywhere.
// LmSessionModel::boardShots() publishes them; LmSessionModel's own rows ARE these (it derives from this struct).
struct LmBoardShot {
    int          id = -1;          // the shot list's shot id
    int          ordinal = 0;      // capture order
    QString      swingDir;         // where its document is ("" for a shot never written)
    QString      recorded;         // the club it was recorded under (ShotListModel::ClubRole)
    QString      club;             // the EFFECTIVE club — the one every figure groups by
    QString      source;           // "recorded" | "evidence" | "user"
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

} // namespace pinpoint::lm

// The launch monitor session board's model (PpLaunchMonitorPanel).
//
// Every number is lmSessionStats()' — this class only decides WHICH SHOTS it is asked
// about, and turns the answer into rows. Instantiated by the panel rather than living
// in main.cpp as a context property, because the panel is off by default: a golfer with
// no monitor should not be paying for a model that recomputes on every shot.
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
// statistic, it would be a wrong one printed confidently. AnalysisDetailRole's `series`
// carries the raw doubles the writer put there, and it is populated by the same code
// path for a live shot (updateLaunchMonitor → refreshShot) and a reloaded one
// (readSwingJson), so there is one source here and not two. It is read as the C++ map
// (ShotListModel::analysisDetailMap), NEVER through data(AnalysisDetailRole): the role is the
// row's JavaScript object for QML, and going through it built every shot's whole detail in the
// QML heap and copied it back on each rebuild — a 2–2.5 minute freeze on entering Analyse with a
// 32-shot session open (#173).
//
// EXCEPT THAT A CHEAP-LOADED ROW HAS NO analysisDetail. ShotListModel::loadSessionDir — End
// Session, app start, an athlete change, entering the Wrist screen — seeds rows from the
// summary and leaves it empty, and the board then said "no readings" over a session full of
// them (#52). Such a row, if a device measured it, is read from its document with
// SwingDocReader::readLaunchMonitorDetail: the same entries in the same shape, so it is still
// one source, reached by a cheaper read. Cached per swing folder — see m_diskDetail.
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
class LmSessionModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT

    // The session's shots. Any ShotListModel — the panel passes whichever list the
    // screen it sits on is showing, so a review screen boards its own session.
    Q_PROPERTY(ShotListModel *shotModel READ shotModel WRITE setShotModel NOTIFY shotModelChanged)
    // The swing the rest of the stage is showing (SessionMode.focusedShotId). -1 for
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
    Q_PROPERTY(QVariantList bandCounts READ bandCounts NOTIFY headerChanged)

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
    enum Roles {
        BandRole = Qt::UserRole + 1,   // "Club"
        CountRole,                     // tiles in this band
        TilesRole,                     // QVariantList of tile maps, in fieldDefs() order
    };

    explicit LmSessionModel(QObject *parent = nullptr);
    ~LmSessionModel() override;

    int                    rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant               data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    ShotListModel *shotModel() const { return m_shots; }
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

    QString scopeText()  const { return m_scopeText; }
    QString valueLabel() const { return m_valueLabel; }
    QString      emptyText()  const;
    QString      shotEmptyText() const;
    int          shotCount()  const { return m_shotCount; }
    QString      corridorScope() const { return m_corridorScope; }
    QVariantList bandCounts() const;
    QVariantMap  graphics()   const { return m_graphics; }
    // Every shot of the list as the last rebuild counted it, oldest first (capture order): the effective club, left
    // out or not, the raw readings. The C++ face of graphics.shots, for a reader that quotes the board's figures
    // without re-deciding which shots count (the session report). Empty with no shot list.
    const std::vector<pinpoint::lm::LmBoardShot> &boardShots() const { return m_boardShots; }

    // ── The panel's two inline actions and the mismatch note's Undo (lm_panel_fsx.md) ──
    //
    // All three are PERSISTED, through the shot list's write-through to each shot's review
    // block (ShotListModel::setLmExcluded / setLmClubOverride → SwingDocWriter::
    // updateShotAnnotations), so a reload of the session comes back exactly as the golfer left
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
    // then. For rows arriving and leaving: a reload (loadSessionDir) is a reset and then one insert
    // per row, and the arithmetic is cheap but the panel's redraw is not — ~80 ms a rebuild in a
    // debug build once the rows carry readings, so a 19-shot session took a second to reload at
    // End Session (#52). An edit to a row still rebuilds at once.
    void scheduleRebuild();
    void connectShotModel();
    // The graphics-mode projection of the SAME statistics rebuild() banded for the
    // tiles. Takes them as an argument rather than recomputing, which is what keeps the
    // two modes incapable of disagreeing about one shot.
    //
    // `opticalLowPoint` is the FOCUSED shot's camera-estimated low point, or empty. It
    // arrives separately from the readings because it is not one — see opticalLowPointFor()
    // in the .cpp for why an estimate must never enter the `lm.` map.
    //
    // `grades` is gradesFor()'s answer, HANDED IN for the same reason: the schematics and
    // the tiles must colour one reading the same way, and two calls to the resolver is two
    // chances for them not to.
    void buildGraphics(const std::vector<pinpoint::analysis::LmFieldStats> &stats,
                       const std::vector<pinpoint::analysis::LmShotValues> &scoped,
                       const QHash<QString, QString> &grades,
                       const std::optional<double> &opticalLowPoint);

    // The focused shot's grade for every reading that has both a value and a corridor,
    // keyed by metric key. Computed ONCE per rebuild and handed to both projections, for
    // exactly the reason buildGraphics() takes its statistics rather than recomputing
    // them: a tile and the schematic beside it must not be able to grade one reading two
    // different ways. A key absent from the map has no corridor, no reading, or a value
    // the norm calls implausible — three facts the panel draws identically, on purpose.
    QHash<QString, QString> gradesFor(const std::vector<pinpoint::analysis::LmFieldStats> &stats,
                                      const QString &contextId);

    // ONE CLUB'S SPREAD OF ONE CLUB-DELIVERY READING, graded (R4b): `values` are the club's included
    // shots' readings (not left out, carrying a reading), in strike order. The scale is the reading's
    // tolerance corridor in the club's own context (lmToleranceHalfWidth), else the grader's own rungs —
    // the CV for a Ratio reading (club speed); an Interval reading with no corridor stays ungraded. As
    // the `variabilityResult` fields in a plain map (graded, grade, measurementLimited, basisText, basis,
    // ungradedReason, n), so it survives a JSON trip (the report). Empty for a blank club: a mixed bag is
    // never graded. Live (not reviewing) the grade passes through the §8.9 hysteresis.
    QVariantMap clubDeliveryGrade(const QString &club, const QString &key, std::vector<double> values);

    struct Band {
        QString      name;
        QVariantList tiles;
    };

    QPointer<ShotListModel> m_shots;
    // The launch monitor slice of each cheap-loaded row (readLaunchMonitorDetail), keyed by swing
    // folder. A reload inserts its rows one at a time and every insert rebuilds, so without this a
    // session's documents would be read once per row. Holds exactly the rows the last rebuild used,
    // so a row that left takes its entry with it; emptied on a reset (a reload re-reads the disk)
    // and per row when its readings change underneath it.
    QHash<QString, QVariantMap> m_diskDetail;
    // Set while undoMismatch() writes several shots: each write emits dataChanged, and one
    // rebuild at the end is the same answer as eight in a row.
    bool                    m_batching = false;
    bool                    m_rebuildPending = false;   // scheduleRebuild() has one queued
    int                     m_focusedShotId = -1;
    bool                    m_connected = false;
    bool                    m_reviewing = false;
    bool                    m_leftHanded = false;
    QString                 m_deviceName;

    QVector<Band> m_bands;
    QVariantMap   m_graphics;
    std::vector<pinpoint::lm::LmBoardShot> m_boardShots;   // boardShots(): the rows rebuild() resolved, oldest first
    QString       m_scopeText;
    QString       m_valueLabel;
    QString       m_corridorScope;
    int           m_shotCount = 0;
    bool          m_anyReadings = false;
    bool          m_focusFound = false;        // the focused shot is in the list
    bool          m_headlineHasReadings = false;   // the shot the board headlines carries a reading

    // The two registries a corridor comes from. Assembled once and read-only thereafter,
    // (nothing mutates them after construction): the norm provider is the process-wide cached one, so a
    // board opening costs no file read, and the pack is this object's own.
    std::unique_ptr<pinpoint::analysis::ICharacteristicPackProvider> m_pack;
    std::shared_ptr<const pinpoint::analysis::INormProvider>         m_norms;
    QString                                                          m_policyName;

    // The grade each (club, reading) badge showed last, for the live hysteresis (§8.9): keyed
    // "club|key", lower case. Cleared when reviewing and when the shot list changes.
    QHash<QString, int> m_shownGrades;
};
