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

// measure_dump — every pack measure's reading, per shot, as the session diagnostics panel grades it
// (posture_metrics_design.md §9, V1–V5). The core lives here so measure_dump_test can drive the same
// code path the binary does.
//
// THE SAME CONSTRUCTION AS THE APP. SessionDiagnosticsModel::detectShot() builds, per swing:
//
//   LiveMeasureSource(swingDir, pack)  + setWithSigma(tuned default)
//   NormMeasureSource(values, sharedNormProvider(), values.contextId(), gradePolicyByName(policy),
//                     &pack, Cohort{})                       (inside detectForSwing)
//
// and this does exactly that, with ONE difference that moves no number: the phase grid is read
// WITHOUT writing the swing_phasegrid.json sidecar the app's reader writes on a cache miss, because
// this tool must never write into a session directory. The grid is built by the same functions
// (loadPhaseGrid on a fresh sidecar, else buildPhaseGrid over SwingStore::load) with the same identity
// fields, so the readings are bit-identical — measure_dump_test pins that against the app's reader.
//
// Namespace `measure_dump`, deliberately outside pinpoint::analysis ([[odr-collision-in-pinpoint-analysis]]).

#include "Diagnostics/engine/characteristic.h"
#include "Diagnostics/measures/live_measure_source.h"
#include "Diagnostics/measures/measure_sample.h"
#include "Diagnostics/norms/norm.h"
#include "Diagnostics/norms/norm_provider.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QTextStream>

#include <limits>
#include <memory>
#include <vector>

namespace measure_dump {

inline constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

struct Options {
    QStringList measures;                          // explicit ids; empty => the whole pack
    QString     prefix;                            // with no explicit ids: only ids starting with this
    QString     policy = QStringLiteral("standard");   // the model's default grade policy
};

// One CSV row. -1 on a tri-state int means "not stated" and prints empty; NaN prints empty.
struct Row {
    QString sessionPath, session, swing, club, contextId, devices;
    int     faceOn = -1, dtl = -1, captureWithheld = -1;
    QString measure;        // the measure id, or "<compositeId>/<partName>" for a composite's part
    QString partMeasure;    // part rows: the alternative that answered (or the last tried, unread)
    QString kind;           // composed | provided | composite | part
    QString status;         // the measure's pack status (live, held, …); parts: the part measure's
    double  value = kNaN;
    QString unit;
    int     hasCorridor = -1;
    double  z = kNaN;       // (value − μ) / σ_side against the resolved norm; empty with no norm
    QString grade;
    int     implausible = -1;
    QString withheld;       // the producer's "drawn but not judged" text
    QString refusal;        // token: a MissingKind name, noCorridor, implausible, withheld, CompositeCoverage, …
    QString refusalText;    // the sentence the panel would show
    QString gate;           // the gate record behind an absent / withheld measure, when one speaks
    double  atStart = kNaN, atEnd = kNaN, measSigma = kNaN;
    double  weight = kNaN;  // part rows: authored weight
    int     sign = 0;       // part rows: ±1 coherent loss sign; 0 = none (distance / not a part)
    double  share = kNaN;   // part rows: share of the composite
    double  weightRead = kNaN, weightTotal = kNaN;   // composite rows: coverage
};

QStringList csvColumns();
QString     csvNumber(double v);          // "" for NaN / ±inf, never "0" or "nan"
QStringList rowFields(const Row &r);      // in csvColumns() order, unquoted
QString     csvLine(const Row &r);        // quoted where needed, no newline

// MissingKind as a stable token ("MetricNotProduced").
QString missingKindToken(pinpoint::analysis::MissingKind k);

// The measures to dump, in pack order (explicit ids in the order given). Every alternative of every
// part of a selected composite is added as a plain measure too, so a ladder's losing rung is still
// dumped (V1 needs m_spineBendChangeImpact AND m_trunkRiseImpact on a two-camera swing). Session-scope
// measures are skipped unless named explicitly — they never read off one swing.
QStringList selectMeasures(const pinpoint::analysis::CharacteristicPack &pack, const Options &opt);

// The phase grid exactly as readPhaseGrid() builds it, but never writing the sidecar. `rootOut`
// receives the full document when it had to be parsed (empty when the sidecar answered).
pinpoint::analysis::SwingPhaseGrid readGridReadOnly(const QString &swingDir, QJsonObject *rootOut = nullptr);

// Every row for one swing directory. Read-only.
std::vector<Row> readSwing(const QString &sessionDir, const QString &swingDir,
                           const pinpoint::analysis::CharacteristicPack &pack,
                           const std::shared_ptr<const pinpoint::analysis::INormProvider> &norms,
                           const QStringList &measureIds,
                           const pinpoint::analysis::GradePolicy &policy);

// The swing_* directories of a session, as the model lists them (name order). A directory that is
// itself a swing (holds a swing document) is returned alone.
QStringList swingDirsOf(const QString &dir);

} // namespace measure_dump
