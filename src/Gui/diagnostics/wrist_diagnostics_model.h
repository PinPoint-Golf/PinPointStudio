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

#include "../Analysis/reference_bands.h"
#include "../Analysis/wrist_assessment_contract.h"
#include "../Analysis/wrist_assessment_result.h"
#include "../review/timeline_labels.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <memory>

// QML-facing adapter for the Wrist Motion diagnostics view (the read-only Tier-1 surface). It is fed
// the focused swing's real analysis — `analysisDetail` (= shotReplay.analysisDetail) — and the replay
// `playheadUs`, and presents the result as plain QVariant models the QML binds to (no logic in QML).
// Positions use the segmentation phase vocabulary (TimelineLabels), and the selected checkpoint is
// derived from the playhead — the panel has no scrubber of its own.

class WristDiagnosticsModel : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QVariantMap  analysisDetail   READ analysisDetail   WRITE setAnalysisDetail NOTIFY resultChanged)
    Q_PROPERTY(qint64       playheadUs       READ playheadUs       WRITE setPlayheadUs     NOTIFY selectedPositionChanged)
    Q_PROPERTY(bool         hasData          READ hasData          NOTIFY resultChanged)
    Q_PROPERTY(int          score            READ score            NOTIFY resultChanged)
    Q_PROPERTY(QString      scoreBand        READ scoreBand        NOTIFY resultChanged)
    Q_PROPERTY(QVariantList positions        READ positions        NOTIFY resultChanged)
    Q_PROPERTY(QVariantList strips           READ strips           NOTIFY resultChanged)
    Q_PROPERTY(QVariantList gridRows         READ gridRows         NOTIFY resultChanged)
    Q_PROPERTY(QVariantList scoreBreakdown   READ scoreBreakdown   NOTIFY resultChanged)
    Q_PROPERTY(QString      scoreBreakdownText READ scoreBreakdownText NOTIFY resultChanged)
    Q_PROPERTY(QVariantList findings         READ findings         NOTIFY resultChanged)
    Q_PROPERTY(QVariantList strengths        READ strengths        NOTIFY resultChanged)
    Q_PROPERTY(int          archetype        READ archetype        WRITE setArchetype  NOTIFY resultChanged)  // mode: -1 Auto
    Q_PROPERTY(QStringList  archetypes       READ archetypes       CONSTANT)
    Q_PROPERTY(int          effectiveArchetype     READ effectiveArchetype     NOTIFY resultChanged)
    Q_PROPERTY(QString      effectiveArchetypeName READ effectiveArchetypeName NOTIFY resultChanged)
    Q_PROPERTY(QString      compareTo        READ compareTo        WRITE setCompareTo  NOTIFY resultChanged)
    Q_PROPERTY(QVariantMap  previousAnalysisDetail  READ previousAnalysisDetail  WRITE setPreviousAnalysisDetail  NOTIFY resultChanged)
    Q_PROPERTY(QVariantMap  referenceAnalysisDetail READ referenceAnalysisDetail WRITE setReferenceAnalysisDetail NOTIFY resultChanged)
    Q_PROPERTY(int          selectedPosition READ selectedPosition NOTIFY selectedPositionChanged)

public:
    explicit WristDiagnosticsModel(QObject *parent = nullptr);

    QVariantMap analysisDetail() const { return m_analysisDetail; }
    void        setAnalysisDetail(const QVariantMap &detail);

    qint64 playheadUs() const { return m_playheadUs; }
    void   setPlayheadUs(qint64 us);

    bool         hasData() const;
    int          score() const { return m_result.score.total; }
    QString      scoreBand() const;
    QVariantList positions() const;          // [{ phase, name, tag, note, available }]
    QVariantList strips() const;
    QVariantList gridRows() const;
    QVariantList scoreBreakdown() const;
    QString      scoreBreakdownText() const;
    QVariantList findings() const;           // faults / watch, severity-ordered (incl. low-confidence)
    QVariantList strengths() const;          // "Working well" (severity == Good)

    int          archetype() const { return m_archetype; }   // mode: -1 Auto / 0 neutral / 1 bowed / 2 cupped
    void         setArchetype(int a);        // re-bands the swing against the chosen model
    QStringList  archetypes() const { return { QStringLiteral("Auto"), QStringLiteral("Neutral"),
                                               QStringLiteral("Bowed"), QStringLiteral("Cupped") }; }
    int          effectiveArchetype() const { return m_result.archetype; }   // the RESOLVED model (0/1/2)
    QString      effectiveArchetypeName() const;
    QString      compareTo() const { return m_compareTo; }
    void         setCompareTo(const QString &mode);   // "address" (no ghost) | "previous" | "reference"
    QVariantMap  previousAnalysisDetail() const { return m_previousAnalysisDetail; }
    void         setPreviousAnalysisDetail(const QVariantMap &detail);
    QVariantMap  referenceAnalysisDetail() const { return m_referenceAnalysisDetail; }
    void         setReferenceAnalysisDetail(const QVariantMap &detail);

    int          selectedPosition() const { return m_selectedPosition; }

    // Dev/test affordance — load a synthetic swing (not used in the live flow).
    // Names: "mockup" (default), "clean", "cast", "flip", "openface", "holdingoff".
    Q_INVOKABLE void loadDemo(const QString &name = QStringLiteral("mockup"));

signals:
    void resultChanged();
    void selectedPositionChanged();

private:
    void assess(const pinpoint::analysis::IWristAngleSource &source);
    void reassess();                         // re-run on the current live swing (e.g. archetype change)
    void recomputeSelected();
    void recomputeGhost();                   // parse + assess the compare-to swing
    QVariantMap findingMap(const pinpoint::analysis::PpWristFinding &f) const;

    std::unique_ptr<pinpoint::analysis::IReferenceBandProvider> m_provider;
    pinpoint::analysis::PpWristAssessmentResult                 m_result;
    pinpoint::analysis::PpWristAssessmentResult                 m_prevResult;  // compare-to ghost
    pinpoint::analysis::PpSwingPositionTimeline                 m_timeline;   // for playhead → checkpoint
    QVariantMap    m_analysisDetail;
    QVariantMap    m_previousAnalysisDetail;
    QVariantMap    m_referenceAnalysisDetail;
    QString        m_compareTo        = QStringLiteral("address");
    bool           m_hasGhost         = false;
    int            m_archetype        = -1;   // Auto by default
    qint64         m_playheadUs       = 0;
    int            m_selectedPosition = 0;
    TimelineLabels m_labels;                 // phase-name source of truth
};
