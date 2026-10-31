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

// THE WHOLE-SHOT GATES (trust stage 6, docs/design/trust_stage6_plan.md): the two data-integrity
// verdicts that withhold every judgement on a swing — frames missing from a camera lane, between
// frames or off either end of the capture window (capture_integrity_check.h) — and sensor
// orientation that its own raw readings do not reproduce (imu_refusion_check.h).
//
// THE DECISION LIVES IN THE MANIFEST, NOT IN THE ANALYSIS. Both verdicts are reached outside the
// pipeline — at the capture join (ShotProcessor) and again on every re-analysis (swing_reanalyzer)
// — and persist as the document's top-level `captureIntegrity` / `imuIntegrity` blocks. So their
// records are DERIVED from those blocks, here, each time the analysis is written
// (SwingDocWriter::writeSwingJson) and each time the live detail map is built (ShotProcessor): a
// rebuild, never an accumulation, so a re-write cannot duplicate them and a re-analysis that
// withdraws a block withdraws its record. SwingAnalysis::gates never holds them.
//
// The same rule as the blocks themselves: a block that checked nothing (camerasChecked / sourcesChecked
// 0) or passed leaves no record. Times are ms from impact, + after.

#include "gate_record.h"

#include <QJsonObject>
#include <QString>
#include <QVariantMap>

#include <vector>

namespace pinpoint::analysis {

// The records the two integrity blocks speak for. Either object may be empty (no block).
inline std::vector<GateRecord> manifestGates(const QJsonObject &captureIntegrity, const QJsonObject &imuIntegrity)
{
    GateLog log;

    // A failing block fails for holes, for a clip with no frame before impact (issue #53), or both;
    // each has its own record. A block written before coverage was judged failed for holes only. A
    // late start or an early end alone does NOT fail the block: its record downgrades, never withholds.
    const bool checked       = captureIntegrity.value(QStringLiteral("camerasChecked")).toInt() > 0;
    const bool captureFailed = checked && !captureIntegrity.value(QStringLiteral("ok")).toBool(true);
    const bool afterImpact = captureIntegrity.value(QStringLiteral("startsAfterImpact")).toBool();
    const bool lateStart   = captureIntegrity.value(QStringLiteral("lateStart")).toBool();
    const bool earlyEnd    = captureIntegrity.value(QStringLiteral("earlyEnd")).toBool();
    const bool clipped     = afterImpact || lateStart || earlyEnd;

    // Frames missing between camera timestamps: a hole longer than `holePeriods` frame periods.
    if (captureFailed && (captureIntegrity.value(QStringLiteral("holes")).toInt() > 0 || !afterImpact)) {
        QVariantMap ev{
            { QStringLiteral("holes"),       captureIntegrity.value(QStringLiteral("holes")).toInt() },
            { QStringLiteral("framesLost"),  captureIntegrity.value(QStringLiteral("framesLost")).toInt() },
            { QStringLiteral("worstHoleMs"), captureIntegrity.value(QStringLiteral("worstHoleMs")).toDouble() },
            { QStringLiteral("preImpact"),   captureIntegrity.value(QStringLiteral("preImpact")).toBool() },
            { QStringLiteral("postImpact"),  captureIntegrity.value(QStringLiteral("postImpact")).toBool() },
            { QStringLiteral("holePeriods"), captureIntegrity.value(QStringLiteral("holePeriods")).toDouble(3.0) } };
        // Absent on a block written before stage 6 or with impact unknown: the sentence drops it.
        if (captureIntegrity.contains(QStringLiteral("firstHoleMs")))
            ev.insert(QStringLiteral("firstHoleMs"), captureIntegrity.value(QStringLiteral("firstHoleMs")).toDouble());
        // The code the evidence sentence picks its clause by: where the frames went missing.
        const bool pre = ev.value(QStringLiteral("preImpact")).toBool();
        const bool post = ev.value(QStringLiteral("postImpact")).toBool();
        ev.insert(QStringLiteral("code"), pre && post ? QStringLiteral("both")
                                        : pre         ? QStringLiteral("pre")
                                                      : QStringLiteral("post"));
        log.fire(PP_GATE("shot.captureHoles"), QStringLiteral("shot"), GateEffect::Withheld, ev);
    }

    // The video does not cover the capture window: it starts late or stops early (Downgraded — a ⚠,
    // the shot still assessed), or has no frame at or before impact (Withheld). Amounts are the worst
    // camera's.
    if (checked && clipped) {
        QVariantMap ev{ { QStringLiteral("edgeToleranceMs"),
                          captureIntegrity.value(QStringLiteral("edgeToleranceMs")).toDouble() } };
        if (lateStart)
            ev.insert(QStringLiteral("missingStartMs"), captureIntegrity.value(QStringLiteral("missingStartMs")).toDouble());
        if (earlyEnd)
            ev.insert(QStringLiteral("missingEndMs"), captureIntegrity.value(QStringLiteral("missingEndMs")).toDouble());
        if (captureIntegrity.contains(QStringLiteral("framesBeforeImpact")))
            ev.insert(QStringLiteral("framesBeforeImpact"), captureIntegrity.value(QStringLiteral("framesBeforeImpact")).toInt());
        ev.insert(QStringLiteral("code"), afterImpact            ? QStringLiteral("afterImpact")
                                        : lateStart && earlyEnd  ? QStringLiteral("both")
                                        : lateStart              ? QStringLiteral("lateStart")
                                                                 : QStringLiteral("earlyEnd"));
        log.fire(PP_GATE("shot.captureShort"), QStringLiteral("shot"),
                 afterImpact ? GateEffect::Withheld : GateEffect::Downgraded, ev);
    }

    // Sensor re-fusion parity: re-running the filter from the stored raw readings did not
    // reproduce the stored orientation.
    if (imuIntegrity.value(QStringLiteral("sourcesChecked")).toInt() > 0
        && !imuIntegrity.value(QStringLiteral("refusionOk")).toBool(true)) {
        const QVariantMap ev{
            { QStringLiteral("worstDeg"),   imuIntegrity.value(QStringLiteral("worstMaxDeg")).toDouble() },
            { QStringLiteral("maxDeg"),     imuIntegrity.value(QStringLiteral("thresholdDeg")).toDouble(0.5) },
            { QStringLiteral("sensors"),    imuIntegrity.value(QStringLiteral("sourcesChecked")).toInt() } };
        log.fire(PP_GATE("shot.refusionParity"), QStringLiteral("shot"), GateEffect::Withheld, ev);
    }

    return log.take();
}

// The same, read straight off a manifest (the document root).
inline std::vector<GateRecord> manifestGatesOf(const QJsonObject &manifest)
{
    return manifestGates(manifest.value(QStringLiteral("captureIntegrity")).toObject(),
                         manifest.value(QStringLiteral("imuIntegrity")).toObject());
}

} // namespace pinpoint::analysis
