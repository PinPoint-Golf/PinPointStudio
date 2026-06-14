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

#include <QJsonObject>
#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

namespace pinpoint {

class SwingCoverageModel;
class SwingSeriesModel;

// One resolved data lane parsed from a swing.json, in the canonical
// window-relative µs domain (stream t_us are already relative; analysis t_us are
// offset by clock.t0_us on load). Heavy arrays live here in C++; QML binds only to
// the two models and the lightweight QVariant* summaries SwingDataSource exposes.
struct SwingSeries {
    enum Kind { Imu, Pose, Club, Metric };

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

    qint64 nominalPeriodUs = 0;  // robust average period ((last-first)/(n-1))
    qint64 medianDeltaUs   = 0;  // median inter-sample delta (row-grid dedup basis)
    qint64 gapToleranceUs  = 40000;  // max(40ms, 1.8 * P90(delta)) — BLE-batch-aware

    bool hasConf() const { return !conf.isEmpty(); }
    bool isImu()   const { return kind == Imu; }
};

// Read-only façade over a single swing.json for the data viewer. Parses the doc,
// resolves the active region against what the file actually contains, and feeds the
// coverage + series models.
class SwingDataSource : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString swingDir   READ swingDir   WRITE setSwingDir   NOTIFY swingDirChanged)
    // Session type + the global imu/placement map — used to resolve an IMU stream's
    // anatomical body role when the swing.json predates the device.role field. Bound
    // from QML (root.sessionType, appSettings.imuPlacement). NOTIFY viewChanged: they
    // only feed resolution, never read back.
    Q_PROPERTY(int         sessionType  READ sessionType  WRITE setSessionType  NOTIFY viewChanged)
    Q_PROPERTY(QVariantMap imuPlacement READ imuPlacement WRITE setImuPlacement NOTIFY viewChanged)
    Q_PROPERTY(QString region     READ region     WRITE setRegion     NOTIFY viewChanged)
    Q_PROPERTY(QString fillMode   READ fillMode   WRITE setFillMode   NOTIFY viewChanged)  // "off" | "nearest"
    Q_PROPERTY(qint64  windowStartUs READ windowStartUs WRITE setWindowStartUs NOTIFY viewChanged)
    Q_PROPERTY(qint64  windowEndUs   READ windowEndUs   WRITE setWindowEndUs   NOTIFY viewChanged)

    Q_PROPERTY(bool    loaded     READ loaded     NOTIFY loadedChanged)
    Q_PROPERTY(qint64  spanUs     READ spanUs     NOTIFY loadedChanged)   // window.end_us
    Q_PROPERTY(QVariantList regionOptions   READ regionOptions   CONSTANT)         // ["Axial",...]
    // Content-aware region defaulting: bestRegion is the region that best surfaces
    // THIS swing's data (most IMU lanes, then most lanes). regionLaneCounts maps each
    // region → its resolved lane count, for dimming regions that would be empty.
    Q_PROPERTY(QString      bestRegion       READ bestRegion       NOTIFY loadedChanged)
    Q_PROPERTY(QVariantMap  regionLaneCounts READ regionLaneCounts NOTIFY loadedChanged)
    // Phase-bounded segments derived from the segmenter's phase events, for windowing
    // the detail table vertically: [{label,startUs,endUs}], "Full" first.
    Q_PROPERTY(QVariantList segments        READ segments        NOTIFY loadedChanged)
    Q_PROPERTY(QVariantList resolvedSources READ resolvedSources NOTIFY viewChanged) // [{label,colorKey,kind,ref,removable}]
    Q_PROPERTY(QVariantList phases          READ phases          NOTIFY loadedChanged) // [{label,t_us,kind}]
    Q_PROPERTY(QVariantList metadata        READ metadata        NOTIFY loadedChanged) // grouped provenance

    Q_PROPERTY(QObject *coverage READ coverage CONSTANT)
    Q_PROPERTY(QObject *table    READ table    CONSTANT)

public:
    explicit SwingDataSource(QObject *parent = nullptr);

    QString swingDir() const { return m_swingDir; }
    void    setSwingDir(const QString &dir);
    int         sessionType() const { return m_sessionType; }
    void        setSessionType(int v);
    QVariantMap imuPlacement() const { return m_imuPlacement; }
    void        setImuPlacement(const QVariantMap &m);
    QString region() const { return m_region; }
    void    setRegion(const QString &r);
    QString fillMode() const { return m_fillMode; }
    void    setFillMode(const QString &m);
    qint64  windowStartUs() const { return m_windowStartUs; }
    void    setWindowStartUs(qint64 v);
    qint64  windowEndUs() const { return m_windowEndUs; }
    void    setWindowEndUs(qint64 v);

    bool    loaded() const { return m_loaded; }
    qint64  spanUs() const { return m_spanUs; }
    QVariantList regionOptions() const;
    QString      bestRegion() const { return m_bestRegion; }
    QVariantMap  regionLaneCounts() const { return m_regionLaneCounts; }
    QVariantList segments() const { return m_segments; }
    QVariantList resolvedSources() const;
    QVariantList phases() const { return m_phases; }
    QVariantList metadata() const { return m_metadata; }

    QObject *coverage() const;
    QObject *table() const;

    // ── region/source editing from QML ──────────────────────────────────────
    Q_INVOKABLE void removeSource(int index);   // drop one resolved lane → region "Custom"
    Q_INVOKABLE void addSource(const QString &kind, const QString &ref, const QString &part);
    // Window the detail table to a phase segment in a single rebuild.
    Q_INVOKABLE void setWindow(qint64 startUs, qint64 endUs);

signals:
    void swingDirChanged();
    void loadedChanged();
    void viewChanged();

private:
    void reload();    // parse swingDir/swing.json → m_doc + m_all series store
    void rebuild();   // re-resolve region + reconfigure coverage/series models

    // A resolved part spec (identity), independent of whether it is present. For IMU:
    // role >= 1 matches by anatomical role; ref == "*" means all IMUs (generic
    // fallback); otherwise ref matches a concrete alias (Custom region).
    struct PartSpec { QString kind; QString ref; QString part; int role = -1; };
    QVector<PartSpec> specsForRegion(const QString &region) const;
    QVector<SwingSeries> resolve(const QVector<PartSpec> &specs) const;
    const SwingSeries *findSeries(const QString &kind, const QString &ref,
                                  const QString &part) const;

    QString m_swingDir;
    int         m_sessionType = -1;
    QVariantMap m_imuPlacement;
    bool        m_anyImuRoleKnown = false;   // any IMU resolved to a non-Unknown role
    QString m_region   = QStringLiteral("Axial");
    QString m_fillMode = QStringLiteral("off");
    qint64  m_windowStartUs = 0;
    qint64  m_windowEndUs   = 0;
    qint64  m_spanUs        = 0;
    bool    m_loaded   = false;

    QJsonObject m_doc;
    QVector<SwingSeries> m_all;        // everything parseable in the file
    QVector<SwingSeries> m_resolved;   // the active region's resolved lanes (ordered)
    QVector<PartSpec>    m_customSpecs;// the user's explicit set when region == Custom
    QVariantList m_phases;
    QVariantList m_metadata;
    QVariantList m_segments;          // phase-bounded windows ([0]=Full)
    QString      m_bestRegion = QStringLiteral("Axial");
    QVariantMap  m_regionLaneCounts;  // region name → resolved lane count

    SwingCoverageModel *m_coverage = nullptr;
    SwingSeriesModel   *m_table    = nullptr;
};

} // namespace pinpoint
