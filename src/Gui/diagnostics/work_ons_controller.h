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

#include "../../Coaching/swing_themes.h"
#include "../../Coaching/work_ons.h"

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QThreadPool>
#include <QVariantList>
#include <QVariantMap>

#include <atomic>
#include <memory>
#include <vector>

class SessionDiagnosticsModel;
namespace pinpoint::analysis { class ICharacteristicPackProvider; struct CharacteristicPack; }

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
    Q_PROPERTY(int detailsRevision READ detailsRevision NOTIFY itemsChanged)
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
    //    topFault (label, "" when none), topFaultId, workOnsActive, workOnsNew, items [item]}
    // Each item, habits first, then occasional, then the rest, by share within each — every one (Home
    // lists them all, each opening in place):
    //   {conditionId, label, golfer, band ("habit" | "occasional" | "clean" | "unclear"), share (0..1,
    //    recency-weighted), fired, assessable (unweighted swings), measureId, hasTypical, typical,
    //    corridorLo, corridorHi, corridorShape, unit, grade, workOn ("active" | "easing" | "new" | ""),
    //    sessionsListed}
    // `grade` is the typical value graded against its stored corridor under `gradePolicy`, in the
    // panel's tokens ("ideal" | "good" | "watch" | "action", for Theme.corridorTone/corridorWord);
    // "" when there is no typical value or no corridor to read it against.
    Q_PROPERTY(QVariantList areaItems READ areaItems NOTIFY areasChanged)

    // ── Your swing ──
    // Every string final (swingSummaryView); HmSwingSummary.qml, HmFocus.qml and (Home's Matchups
    // tab) HmGoesTogether.qml position and paint. A fault item, wherever it appears, is
    // {text, share (0..1), frequency, trend (+1 growing, −1 easing, 0), sessions [bool, one per
    // judged session], conditionId (what its row opens with faultDetail())}.
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
    // session assessed, not only the listed work-ons: a listed one's is its `items` entry exactly;
    // any other's has the same keys, its "Latest" from the newest session that assessed it.
    // Empty when no session assessed it. One map, not a map of them all: a QVariantMap property
    // read is a deep copy, and every fault row on Home asks.
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

    // Rescan the athlete's sessions and re-derive whatever is stale. Cheap when nothing is.
    Q_INVOKABLE void refresh();
    // A condition's name as the items show it: the pack label, else the id ("" for "").
    Q_INVOKABLE QString conditionLabel(const QString &id) const;

    // Test seam, as SessionDiagnosticsModel's: scan, derive and reduce the themes inline.
    void setSynchronous(bool on) { m_synchronous = on; }
    // Waits for the scan, the derivation AND the swing summary.
    bool waitForIdle(int msTimeout = 60000);

    static QString recordPath(const QString &sessionDir);
    static QString themesPath(const QString &athleteDir);

signals:
    void athleteDirChanged();
    void gradePolicyChanged();
    void pausedChanged();
    void itemsChanged();
    void updatingChanged();
    void summaryChanged();
    void areasChanged();

private:
    struct Scanned {
        QString sessionDir;
        QString fingerprint;
        bool    fresh = false;
        qint64  mtimeMs = 0;
        pinpoint::analysis::SessionWorkOns record;   // valid when fresh
    };
    static std::vector<Scanned> scan(const QString &athleteDir, const QString &contentStamp);
    void applyScan(int generation, std::vector<Scanned> rows);

    void deriveNext();
    void onModelSettled();
    void finishDerive();
    void publish();
    void publishAreas(const pinpoint::analysis::CharacteristicPack &pack,
                      const std::vector<pinpoint::analysis::SessionWorkOns> &sessions);
    SessionDiagnosticsModel *model();

    // ── Your swing ──
    struct ThemesJob {
        int     generation = 0;
        QString athleteDir;
        QString fingerprint;
        QString contentStamp;
        QStringList sessionDirs;    // name order
        QHash<QString, pinpoint::analysis::ThemeMeasureInfo>   measures;
        QHash<QString, pinpoint::analysis::ThemeConditionInfo> conditions;
    };
    struct ThemesOutcome {
        bool    fromDisk = false;   // the stored summary was current; nothing was reduced
        bool    computed = false;   // reduced here (and not cancelled)
        pinpoint::analysis::SwingThemes themes;
        QString fingerprint;        // the job's, carried back
        QString contentStamp;
        QString athleteDir;
        qint64  wallMs = 0;
    };
    QString themesFingerprint() const;
    void maybeThemes();
    void cancelThemes();
    ThemesOutcome runThemes(const ThemesJob &job, const std::atomic<bool> *cancel);
    void applyThemes(int generation, ThemesOutcome out);
    void publishSummary();
    void clearSummary();

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
};
