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

#include "session_series_types.h"

#include <QHash>
#include <QJsonObject>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QThreadPool>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

namespace pinpoint::analysis { class ICharacteristicPackProvider; }

// THE CHARTS PANEL'S SESSION — every shot of the current session as the series, phases,
// kinematic sequence and diagnostics the Charts panel reads (docs/design/
// charts_panel_traces_relations.md §6.1), published as one immutable SessionSnapshot
// (session_series_types.h).
//
// It is a LOADER AND A CACHE, deliberately not a place where any session arithmetic lives. The
// envelope, the warp, the group fit, the correlations and the kinds are the session maths'
// (session_traces.h, session_relations.h), which reads the snapshot and nothing else. What this
// file decides is which bytes leave a swing document, on which clock, and when they are read
// again.
//
// INSTANTIATED BY THE PANEL, not registered in main.cpp — SessionDiagnosticsModel's decision for
// the same reason: the Charts panel is one stage tab among several, and a golfer who never opens
// it should not be paying for a worker thread and fifty swings' worth of curves.
//
// ── The decisions worth knowing before reading the .cpp ─────────────────────────────────
//
// 1. THE FEEDING API IS SessionDiagnosticsModel's, NAME FOR NAME. activateSession(dir),
//    ingestShot(shotId, swingDir), regradeShot(swingDir) and closeSession() mean here what they
//    mean there, so the panel wires this model to the same three events the diagnostics panel
//    already answers (PpSessionDiagnosticsPanel.qml): its `sessionDir` binding changing, the
//    shot processor's shotProcessed, and the carousel's re-analysis (reanalysisController
//    .reanalysed). One addition, refreshDiagnostics(), because this model READS the ledger the
//    diagnostics model WRITES, and only that model knows when it has written it.
//
// 2. THREADING. Every read happens on a private QThreadPool of ONE thread, so jobs run in the
//    order they were asked for and a later job always builds on an earlier one's cache. A job
//    ends in a complete SessionSnapshot, handed back by a QUEUED invocation: the published
//    pointer, every property and every signal change only on the thread that owns this object.
//    A delivery stamped with an older activation is dropped. setSynchronous(true) runs the same
//    job inline, for tests — the only concession the design makes to being tested.
//
// 3. NEVER THE POSE BLOB, AND NEVER THE DOCUMENT. A job reads a swing through
//    SwingStore::loadAnalysis — the root chunk and /analysis/metrics, never a per-frame track —
//    copies out the metrics, phases, kinematic sequence, club and impact into a ShotSeries, and
//    drops what it read before the next swing is read: nothing parsed outlives extractShot().
//    Measured on the share: 7–9 ms a swing, where the whole document (SwingStore::load) is
//    150–220 ms (7.4 s for the 50-shot 7 Oct session). A legacy swing.json has no chunks and
//    still costs the full parse.
//
// 4. ONE CLOCK: THE REPLAY'S. Every time copied out goes through the same window-relative rule
//    disk_replay_source.cpp's relUs() applies (subtract clock.t0_us only from an absolute time),
//    and the kinematic sequence through the same retimeKinematicSequence(), so a t_us read off a
//    ShotSeries can be handed to shotReplay.seekToUs unchanged.
//
// 5. CACHED BY WHAT WAS READ. A swing is read again only when its document's size or mtime has
//    moved (SwingStore::info, stamped BEFORE the read, as the diagnostics model stamps its
//    rows) or when regradeShot() says it was re-analysed. Re-activating a session, re-ingesting
//    an unchanged shot or refreshing the ledger costs a stat per swing. The cache holds the two
//    most recently activated sessions, so flipping between the live session and a reviewed one
//    never re-reads either. And it is PROCESS-WIDE, not this object's: PpModeStage rebuilds its
//    panels — and so this model — on every Capture↔Replay flip, twice per auto-replayed shot, and
//    a cache that died with the instance would re-read the whole session each time (7 s for 50
//    shots). Published shots are immutable, so instances share them safely. It is an
//    OPTIMISATION ONLY: each instance keeps its own entries for its session and builds its
//    snapshot from those, so another instance evicting the shared copies costs this one nothing.
//
// 6. A PUBLISHED SHOT NEVER CHANGES. A re-analysis, a new ordinal (a back-fill landing between
//    two shots) or a new ledger row replaces that shot's shared_ptr; every other shot keeps its
//    pointer, so a reader can tell what moved by comparing pointers. `revision` bumps whenever
//    the snapshot's content does.
//
// 7. STRIKE ORDER IS THE FOLDER NUMBER. The carousel reloads a session from its swing_NNNN
//    folders in name order (ShotListModel::loadSessionDir via SwingDocReader::findSwingDirs) and
//    the diagnostics ledger numbers its shots by the same folder (folderShotId), so ordinal is
//    the rank of the folder number and shotId IS the folder number — the join key to the ledger.
//    A caller's shot id is used only for a folder that carries no number.
//
// 8. A SHOT WITHOUT ANALYSIS IS COUNTED, NOT HIDDEN. No document, an unreadable one, or one whose
//    analysis produced no metrics is left out of the snapshot and listed in `skipped` with the
//    reason, so the panel can say "15 shots, 2 not analysed" rather than quietly drawing 13.
//
// 9. THE CONDITION LINK IS THE PACK'S. Each condition in the snapshot carries the reading it grades
//    (ConditionInfo.metricKey/phase) and each shot the reading that drove each of its fired
//    conditions (ShotDiagnostics::drivenBy), both through condition_reading.h — the one walk of
//    detectedBy → signal → measure → key and reducer — so a fired mark is placed by the catalogue,
//    never by matching values.
//
// Unit-tested standalone in src/Gui/tests/charts_series_model_test.cpp.
namespace pinpoint::charts {

class SessionSeriesModel : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    // The session the snapshot describes (cleaned, absolute). Empty when none is loaded.
    Q_PROPERTY(QString sessionDir READ sessionDir NOTIFY sessionChanged)
    // A read is in flight. The previous snapshot stays published meanwhile — except across a
    // change of session, where the old session's shots are withdrawn at once (a stale session
    // drawn under a new name is worse than an empty one for a moment).
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    // The shots in the snapshot, and the shots left out of it (decision 8).
    Q_PROPERTY(int shotCount READ shotCount NOTIFY snapshotChanged)
    Q_PROPERTY(int skippedCount READ skippedCount NOTIFY snapshotChanged)
    // [{ swingDir, shotId, reason }], strike order. The reason is a sentence for the panel.
    Q_PROPERTY(QVariantList skipped READ skipped NOTIFY snapshotChanged)
    // SessionSnapshot::revision, for QML bindings that need to re-ask a C++ question.
    Q_PROPERTY(int revision READ revision NOTIFY snapshotChanged)
    Q_PROPERTY(bool closed READ closed NOTIFY sessionChanged)
    // ADDED 15 Oct (build/wrist-tab/CONTRACT.md I3): the wrist corridors EVERY shot's wrist profile
    // (ShotSeries::wrist) is graded against — -1 Auto (each shot resolves its own style), 0 Neutral,
    // 1 Bowed, 2 Cupped. Session-wide: setting it re-grades every shot from the series already held
    // (no document is read) and publishes a new snapshot. The Wrist tab's "Bands" choice.
    Q_PROPERTY(int wristArchetype READ wristArchetype WRITE setWristArchetype NOTIFY wristArchetypeChanged)

public:
    explicit SessionSeriesModel(QObject *parent = nullptr);
    ~SessionSeriesModel() override;

    QString sessionDir() const { return m_sessionDir; }
    bool    busy() const { return m_pending > 0; }
    int     shotCount() const { return m_snapshot ? m_snapshot->count() : 0; }
    int     skippedCount() const { return int(m_skipped.size()); }
    QVariantList skipped() const;
    int     revision() const { return m_snapshot ? int(m_snapshot->revision & 0x7fffffff) : 0; }
    bool    closed() const { return m_closed; }
    int     wristArchetype() const { return m_wristArchetype; }
    void    setWristArchetype(int a);

    // What the session maths reads. Never null; an empty session is an empty snapshot.
    std::shared_ptr<const SessionSnapshot> snapshot() const { return m_snapshot; }

    // ── Feeding (decision 1) ────────────────────────────────────────────────────────────

    // Point the model at a session directory and read every swing_* in it (from the cache where
    // the document has not moved). Covers opening a finished session, turning the panel on half
    // way through a live one and resuming after a crash, exactly as the diagnostics model's does.
    // An empty dir clears. Re-activating the loaded session re-reads only what changed on disk.
    Q_INVOKABLE void activateSession(const QString &sessionDir);

    // A shot landed. Idempotent: an unchanged document already held costs a stat. A swing
    // outside the loaded session is ignored (it belongs to whichever session activates it), and
    // so is any shot after closeSession(). Wire straight to ShotProcessor::shotProcessed.
    Q_INVOKABLE void ingestShot(int shotId, const QString &swingDir);

    // The swing was re-analysed: read it again whatever its stamp says, and replace its shot.
    // Accepted after closeSession() — it is the same shot measured again. A swing the model has
    // not seen is ingested; one outside the loaded session is ignored.
    Q_INVOKABLE void regradeShot(const QString &swingDir);

    // Re-read the session's diagnostics ledger (diagnostics.json) if it has changed, and re-join
    // its rows to the shots. Call it when SessionDiagnosticsModel has written the file — its
    // busyChanged to false precedes the write by a few statements on the same thread, so the read
    // is POSTED to run after the caller's handler returns, never inside it.
    Q_INVOKABLE void refreshDiagnostics();

    // End of session: later ingestShot() calls are refused, as the diagnostics model refuses
    // them. Nothing is written. activateSession() re-opens.
    Q_INVOKABLE void closeSession();

    // ── Test and measurement seams ──────────────────────────────────────────────────────

    // Run every job inline on the calling thread. The code path is otherwise identical.
    Q_INVOKABLE void setSynchronous(bool on);
    Q_INVOKABLE bool synchronous() const { return m_synchronous; }
    // Spin the caller's event loop until nothing is in flight. False on timeout; a no-op when
    // synchronous.
    Q_INVOKABLE bool waitForIdle(int msTimeout = 60000);

    // How a swing document is read. SwingStore::loadAnalysis by default; a test injects a counting or
    // partial loader. MUST be thread-safe: it runs on the worker. Set before activateSession().
    using DocLoader = std::function<QJsonObject(const QString &swingDir, QString *error)>;
    void setDocLoader(DocLoader loader);

    // What the worker has done since construction: documents read (cache misses), cache hits,
    // and the wall time spent loading documents and extracting shots. Read when idle.
    struct LoadStats {
        int    docReads   = 0;
        int    cacheHits  = 0;
        qint64 loadNs     = 0;
        qint64 extractNs  = 0;
    };
    LoadStats loadStats() const;

    // ── The pure halves, public so a test can hold them to the document directly ──────────

    // One swing document → one ShotSeries (ordinal, shotId and diagnostics left for the join).
    // Null when the document carries no analysis metrics; *whyNot then says why, in the
    // sentence `skipped` carries.
    static std::shared_ptr<ShotSeries> extractShot(const QJsonObject &root, const QString &swingDir,
                                                   QString *whyNot = nullptr);

    // The window-relative µs of an analysis time — disk_replay_source.cpp's relUs(), the one
    // rule every time here goes through (decision 4).
    static qint64 relUs(const QJsonValue &v, qint64 t0);

    // Empty the process-wide shot cache (decision 5). For tests, which rewrite one session's
    // documents between cases; the app never needs it.
    static void clearSharedCache();

    // The resident heap one shot holds, estimated from its containers' capacities. For sizing
    // (§6.1 "sized for 50+ shots") and the app-log line; not an allocator's figure.
    static qint64 approxBytes(const ShotSeries &shot);

    // The ledger's shot id for a swing folder (swing_0007 → 7), −1 when the name carries none.
    // The same rule SessionDiagnosticsModel's folderShotId() applies.
    static int folderShotId(const QString &swingDir);

signals:
    void sessionChanged();
    void busyChanged();
    // The snapshot was replaced. Every property above that reads it is re-read on this.
    void snapshotChanged();
    void wristArchetypeChanged();

private:
    // ── Worker half. Touched only by jobs, which run one at a time (decision 2). ──────────
    struct Skip {
        QString swingDir;
        int     shotId = -1;
        QString reason;
    };
    struct CacheEntry {
        QString sessionDir;                       // for eviction (decision 5)
        qint64  size    = -1;
        qint64  mtimeMs = -1;
        std::shared_ptr<const ShotSeries> shot;   // as last published; null when skipped
        std::shared_ptr<ShotSeries> fresh;        // read, not yet published (completed in place)
        QString skipReason;
    };
    enum class JobKind { Activate, Ingest, Regrade, RefreshLedger, Rearchetype };
    struct Job {
        JobKind kind = JobKind::Activate;
        int     generation = 0;
        QString dir;          // the session (Activate) or the swing (Ingest/Regrade)
        int     callerShotId = -1;
        int     wristArchetype = -1;   // stamped by queue() from the owner's choice
    };
    struct Result {
        int  generation = 0;
        bool changed    = false;
        std::shared_ptr<const SessionSnapshot> snapshot;
        std::vector<Skip> skipped;
    };

    Result runJob(const Job &job);
    void   ensureShot(const QString &swingDir, bool force);
    void   keep(const QString &swingDir, const CacheEntry &e);
    bool   readLedgerIfChanged();
    Result buildSnapshot(int generation, bool forcePublish);
    void   evictOtherSessions();

    // THE SHOT CACHE IS THE PROCESS'S, NOT THE INSTANCE'S (decision 5): every instance's jobs
    // read and fill one table, under its own mutex, taken after m_workerMutex.
    struct SharedCache;
    static SharedCache &shared();

    // Worker state, under m_workerMutex (held for the whole of a job).
    mutable QMutex m_workerMutex;
    // THIS INSTANCE'S shots, as it last read or published them — the source of truth for its
    // snapshot. The shared cache only saves reads; another instance may evict from it at any time.
    QHash<QString, CacheEntry> m_own;            // key: cleaned absolute swing dir; current session only
    QHash<QString, int>        m_callerIds;      // swing dir → caller's id, for unnumbered folders
    QString     m_wSessionDir;
    QStringList m_wDirs;                         // the session's swing dirs
    qint64      m_ledgerSize    = -1;
    qint64      m_ledgerMtimeMs = -1;
    QHash<int, ShotDiagnostics> m_ledger;        // shotId → the shot's ledger row, joined
    QHash<QString, QString>     m_ledgerTiers;   // conditionId → session tier (SessionSnapshot::tiers)
    int         m_wArchetype = -1;               // the archetype the running job grades against
    std::shared_ptr<const SessionSnapshot> m_wLast;
    std::vector<Skip> m_wLastSkipped;
    LoadStats   m_stats;
    DocLoader   m_loader;
    std::unique_ptr<pinpoint::analysis::ICharacteristicPackProvider> m_pack;   // lazily, on the worker

    // ── Owner half ──────────────────────────────────────────────────────────────────────
    void queue(const Job &job);
    void apply(const Result &r, bool counted);

    QString m_sessionDir;
    bool    m_closed = false;
    int     m_wristArchetype = -1;
    int     m_generation = 0;
    std::shared_ptr<const SessionSnapshot> m_snapshot;
    std::vector<Skip> m_skipped;

    QThreadPool m_pool;          // maxThreadCount 1 (decision 2)
    int  m_pending = 0;
    bool m_synchronous = false;
    bool m_refreshPosted = false;

    // Shared by both halves: the worker stamps a built snapshot, the owner an interim one
    // (activateSession). Atomic rather than under the worker mutex, so the GUI thread never
    // waits on a job to number a snapshot.
    std::atomic<quint64> m_revision{ 0 };
};

} // namespace pinpoint::charts
