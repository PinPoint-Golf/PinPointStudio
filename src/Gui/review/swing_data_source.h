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

#include "shot_data_door.h"
#include "swing_data_types.h"

#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVector>

#include <memory>

namespace pinpoint {

class SwingCoverageModel;
class SwingSeriesModel;
namespace charts { class ChartsLane; }

// One resolved data lane of the shot, in the canonical
// window-relative µs domain (stream t_us are already relative; analysis t_us are
// offset by clock.t0_us on load). Heavy arrays live here in C++; QML binds only to
// the two models and the lightweight QVariant* summaries SwingDataSource exposes.
struct SwingSeries {
    enum Kind { Imu, Pose, Club, Metric, Ball };

    Kind    kind     = Imu;
    QString ref;            // alias / serial / metric key — stable identity
    QString part;           // catalogue part id (pose keypoint-set / channel)
    QString label;          // tag label  (e.g. "Pose · Shoulders")
    QString header;         // short table-column header (e.g. "Shoulders")
    QString unit;           // value unit ("°", "deg/s", "")
    QString colorKey;       // teal|blue|purple|purple2|amber|coral
    bool    removable = true;
    bool    primary   = false;   // the primary IMU (raw+fused cols, Δt/State scope)
    int     imuRole   = 0;       // pinpoint::analysis::SegmentRole (0 = Unknown)

    QVector<qint64> t;          // ascending, window-relative µs
    QVector<double> value;      // primary scalar value per sample
    QVector<double> conf;       // per-sample confidence (empty => source has none)

    // IMU extras (kind == Imu)
    QVector<double> gyroZ;       // raw gyro z (deg/s)
    QVector<double> fusedRateZ;  // quaternion-derived body-z angular rate (deg/s)

    // Ball extras (kind == Ball): value holds x; these hold y and radius (all norm 0..1)
    QVector<double> ballY;
    QVector<double> ballR;

    qint64 nominalPeriodUs = 0;  // robust average period ((last-first)/(n-1))
    qint64 medianDeltaUs   = 0;  // median inter-sample delta (row-grid dedup basis)
    qint64 gapToleranceUs  = 40000;  // max(40ms, 1.8 * P90(delta)) — BLE-batch-aware

    bool hasConf() const { return !conf.isEmpty(); }
    bool isImu()   const { return kind == Imu; }
};

// THE DATA VIEWER'S PRESENTER OVER THE SHOT (refactor 5.8a; LOCAL-COPIES.md row 10). It shows one shot's data, read
// from the shot's owners through the review views' door (shot_data_door.h) — the analysis, the streams and their
// descriptions, the capture block, the schema and the clock from the Shot's Recording; the athlete's from the Athlete's
// record; the session's from its Session; the review from the Shot — never from the swing file. It follows the Shot's
// events (EVT-02 re-analysed, EVT-22 completed): the shot is read again on the charts' lane and the view follows, with
// no reload. What it holds (the parsed lanes, the Properties rows) is a DERIVED DISPLAY CACHE (Product Owner, 23 Nov
// 2026): filled only from the door, refreshed on the Shot's events, never written back, never a source for anything
// else. The table and coverage models are fed from it.
//
// A view reaches it through `app`: `session: app.session`, `swingDir:` the focused shot's folder (view state). A folder
// that Session does not hold shows nothing.
class SwingDataSource : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString swingDir   READ swingDir   WRITE setSwingDir   NOTIFY swingDirChanged)
    // The Session the view is given (app.session): its Shot at swingDir is the one shown.
    Q_PROPERTY(QObject *session   READ session    WRITE setSession    NOTIFY sessionChanged)
    // Session type + the global imu/placement map — used to resolve an IMU stream's
    // anatomical body role when the swing predates the device.role field. Bound
    // from QML (root.sessionType, appSettings.imuPlacement as serial, slot pairs). NOTIFY
    // viewChanged: they only feed resolution, never read back.
    Q_PROPERTY(int         sessionType  READ sessionType  WRITE setSessionType  NOTIFY viewChanged)
    Q_PROPERTY(QStringList imuPlacementPairs READ imuPlacementPairs WRITE setImuPlacementPairs NOTIFY viewChanged)
    Q_PROPERTY(QString region     READ region     WRITE setRegion     NOTIFY viewChanged)
    Q_PROPERTY(QString fillMode   READ fillMode   WRITE setFillMode   NOTIFY viewChanged)  // "off" | "nearest"
    Q_PROPERTY(qint64  windowStartUs READ windowStartUs WRITE setWindowStartUs NOTIFY viewChanged)
    Q_PROPERTY(qint64  windowEndUs   READ windowEndUs   WRITE setWindowEndUs   NOTIFY viewChanged)

    Q_PROPERTY(bool    loaded     READ loaded     NOTIFY loadedChanged)
    // A read of the shot is in flight (it keeps showing the last one until the new one lands).
    Q_PROPERTY(bool    busy       READ busy       NOTIFY busyChanged)
    // Moves each time a read of the shot lands (a probe's and a test's signal that the view has followed).
    Q_PROPERTY(int     revision   READ revision   NOTIFY loadedChanged)
    Q_PROPERTY(qint64  spanUs     READ spanUs     NOTIFY loadedChanged)   // window.end_us
    Q_PROPERTY(QStringList regionOptions   READ regionOptions   CONSTANT)         // ["Axial",...]
    // Content-aware region defaulting: bestRegion is the region that best surfaces
    // THIS swing's data (most IMU lanes, then most lanes). regionLaneCounts is each
    // region's resolved lane count, in regionOptions' order (Custom has none), for
    // dimming regions that would be empty.
    Q_PROPERTY(QString      bestRegion       READ bestRegion       NOTIFY loadedChanged)
    Q_PROPERTY(QList<int>   regionLaneCounts READ regionLaneCounts NOTIFY loadedChanged)
    // Phase-bounded segments derived from the segmenter's phase events, for windowing
    // the detail table vertically: "Full" first.
    Q_PROPERTY(QList<pinpoint::dataview::Segment>       segments        READ segments        NOTIFY loadedChanged)
    Q_PROPERTY(QList<pinpoint::dataview::SourceChip>    resolvedSources READ resolvedSources NOTIFY viewChanged)
    Q_PROPERTY(QList<pinpoint::dataview::Phase>         phases          READ phases          NOTIFY loadedChanged)
    Q_PROPERTY(QList<pinpoint::dataview::PropertyGroup> metadata        READ metadata        NOTIFY loadedChanged) // grouped provenance
    // The region's readings a gate kept out of the table (trust stage 6 session 2): a lane the region
    // asks for that the swing does not carry because a gate refused it, one per subject, in the gate's
    // words — { label ("Club track"), subject ("track.club"), words ("refused") }. Empty on a swing
    // analysed before records existed, and for a lane that is simply not in the file.
    Q_PROPERTY(QList<pinpoint::dataview::NotShown>      notShown        READ notShown        NOTIFY viewChanged)

    Q_PROPERTY(QObject *coverage READ coverage CONSTANT)
    Q_PROPERTY(QObject *table    READ table    CONSTANT)

public:
    explicit SwingDataSource(QObject *parent = nullptr);
    ~SwingDataSource() override;

    QString swingDir() const { return m_swingDir; }
    void    setSwingDir(const QString &dir);
    QObject *session() const { return m_session.data(); }
    void     setSession(QObject *session);
    int         sessionType() const { return m_sessionType; }
    void        setSessionType(int v);
    QStringList imuPlacementPairs() const { return m_imuPlacementPairs; }
    void        setImuPlacementPairs(const QStringList &pairs);
    QString region() const { return m_region; }
    void    setRegion(const QString &r);
    QString fillMode() const { return m_fillMode; }
    void    setFillMode(const QString &m);
    qint64  windowStartUs() const { return m_windowStartUs; }
    void    setWindowStartUs(qint64 v);
    qint64  windowEndUs() const { return m_windowEndUs; }
    void    setWindowEndUs(qint64 v);

    bool    loaded() const { return m_loaded; }
    bool    busy() const;
    int     revision() const { return m_revision; }
    qint64  spanUs() const { return m_spanUs; }
    QStringList  regionOptions() const;
    QString      bestRegion() const { return m_bestRegion; }
    QList<int>   regionLaneCounts() const { return m_regionLaneCounts; }
    QList<pinpoint::dataview::Segment>       segments() const { return m_segments; }
    QList<pinpoint::dataview::SourceChip>    resolvedSources() const;
    QList<pinpoint::dataview::Phase>         phases() const { return m_phases; }
    QList<pinpoint::dataview::PropertyGroup> metadata() const { return m_metadata; }
    QList<pinpoint::dataview::NotShown>      notShown() const { return m_notShown; }

    QObject *coverage() const;
    QObject *table() const;

    // ── region/source editing from QML ──────────────────────────────────────
    Q_INVOKABLE void removeSource(int index);   // drop one resolved lane → region "Custom"
    Q_INVOKABLE void addSource(const QString &kind, const QString &ref, const QString &part);
    // Window the detail table to a phase segment in a single rebuild.
    Q_INVOKABLE void setWindow(qint64 startUs, qint64 endUs);

    // Write the current table (as configured: region/window/fill mode) to a CSV file
    // in the user's home directory and return its absolute path, or "" on failure.
    // The name derives from the swing folder + active region, sanitised, and is
    // uniquified so an existing file is never overwritten.
    Q_INVOKABLE QString exportCsv() const;

    // Tests and probes: spin the GUI thread until no read of the shot is in flight. False on timeout.
    bool waitForIdle(int msTimeout = 60000);

signals:
    void swingDirChanged();
    void sessionChanged();
    void busyChanged();
    void loadedChanged();
    void viewChanged();

private:
    void reload();    // the shot's owners → m_all series store + the Properties rows (from the last read)
    void rebuild();   // re-resolve region + reconfigure coverage/series models
    void refollow();  // (session, swingDir) → the Shot through the door; read it
    void reread();    // the followed Shot's Recording, read on the lane; applied when it lands
    // Coalesce refollow() and reload(): swingDir/session/sessionType/imuPlacement are all set in the same
    // QML binding pass on a swing switch — without this each setter re-read the shot. Defer to the event
    // loop so they collapse to one.
    void scheduleRefollow();
    void scheduleReload();
    QString placementSlotOf(const QString &serial) const;

    // A resolved part spec (identity), independent of whether it is present. For IMU:
    // role >= 1 matches by anatomical role; ref == "*" means all IMUs (generic
    // fallback); otherwise ref matches a concrete alias (Custom region).
    struct PartSpec { QString kind; QString ref; QString part; int role = -1; };
    QVector<PartSpec> specsForRegion(const QString &region) const;
    QVector<SwingSeries> resolve(const QVector<PartSpec> &specs) const;
    const SwingSeries *findSeries(const QString &kind, const QString &ref,
                                  const QString &part) const;

    QString m_swingDir;
    QPointer<QObject> m_session;
    int         m_sessionType = -1;
    QStringList m_imuPlacementPairs;   // serial, slot, serial, slot, …
    bool        m_anyImuRoleKnown = false;   // any IMU resolved to a non-Unknown role
    QString m_region   = QStringLiteral("Axial");
    QString m_fillMode = QStringLiteral("off");
    qint64  m_windowStartUs = 0;
    qint64  m_windowEndUs   = 0;
    qint64  m_spanUs        = 0;
    bool    m_loaded   = false;
    int     m_revision = 0;
    bool    m_reloadPending   = false;   // a coalesced reload() is queued
    bool    m_refollowPending = false;   // a coalesced refollow() is queued

    // The followed shot: its door, the object its event connections hang on, and its last read (the cache's source).
    pinpoint::review::ShotDataHandle                    m_handle;
    QObject                                            *m_follower = nullptr;
    std::shared_ptr<const pinpoint::review::ShotDataRead> m_read;
    std::unique_ptr<pinpoint::charts::ChartsLane>       m_lane;

    QVector<SwingSeries> m_all;        // everything the shot carries
    QVector<SwingSeries> m_resolved;   // the active region's resolved lanes (ordered)
    QVector<PartSpec>    m_customSpecs;// the user's explicit set when region == Custom
    QList<pinpoint::dataview::Phase>         m_phases;
    QList<pinpoint::dataview::PropertyGroup> m_metadata;
    QVariantList m_gates;             // analysis.gates as stored ({} before records existed): C++ only (trust::gateFor)
    QList<pinpoint::dataview::NotShown>      m_notShown;   // see notShown
    QList<pinpoint::dataview::Segment>       m_segments;   // phase-bounded windows ([0]=Full)
    QString      m_bestRegion = QStringLiteral("Axial");
    QList<int>   m_regionLaneCounts;  // per region, in regionOptions' order (Custom excluded)

    SwingCoverageModel *m_coverage = nullptr;
    SwingSeriesModel   *m_table    = nullptr;
};

} // namespace pinpoint
