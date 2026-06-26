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

#include <QVariantMap>
#include <cstdint>

#include "analysis_tuning.h"
#include "../IMU/orientation_refuser.h"

// SwingLab tuning for the orientation re-fusion filter (validation §5.3.1). Single source for the
// filter.* dotted-key list, shared by the offline analyzer (wrist_analyzer → ImuVisionFuser, the
// metric path) and the swinglab_run --refuse-orientation parity tool. Empty map ⇒ fixed-beta from
// betaDefault (the live behaviour). Defaults live in pp_tuned_constants.h.
//
//   filter.refuse        — bool: feed the re-fused quaternion into the fusion (the C3 metric path)
//   filter.adaptive      — bool: phase-adaptive schedule (continuous gate + saturation + impact blank)
//   filter.beta / .betaStatic / .betaDynamic
//   filter.accelErrGateG / .gyroGateDps / .accelSatG
//   filter.impactBlankPreMs / .impactBlankPostMs

namespace pinpoint::analysis {

// True if the analyzer should re-fuse orientation and feed it into the wrist metric (distinct from
// filter.adaptive, which only chooses the schedule once re-fusion is on).
inline bool tuningWantsRefusion(const QVariantMap &ov)
{
    return ov.value(QStringLiteral("filter.refuse")).toBool();
}

// Build a RefuseConfig from filter.* keys. betaStatic starts at betaDefault (the live gain or a CLI
// --refuse-beta value) and is overridden by filter.beta/.betaStatic. impactUs arms the impact-blanking
// window, but only when filter.adaptive is set.
inline pinpoint::RefuseConfig refuseConfigFromTuning(const QVariantMap &ov, int64_t impactUs,
                                                     float betaDefault = pinpoint::tuned::filter::kBeta)
{
    namespace tn = pinpoint::analysis::tuning;
    pinpoint::RefuseConfig cfg;
    cfg.warmStart  = true;
    cfg.betaStatic = betaDefault;
    tn::apply(ov, "filter.adaptive",          cfg.adaptive);
    tn::apply(ov, "filter.beta",              cfg.betaStatic);
    tn::apply(ov, "filter.betaStatic",        cfg.betaStatic);
    tn::apply(ov, "filter.betaDynamic",       cfg.betaDynamic);
    tn::apply(ov, "filter.accelErrGateG",     cfg.accelErrGateG);
    tn::apply(ov, "filter.gyroGateDps",       cfg.gyroGateDps);
    tn::apply(ov, "filter.accelSatG",         cfg.accelSatG);
    tn::apply(ov, "filter.impactBlankPreMs",  cfg.impactBlankPreMs);
    tn::apply(ov, "filter.impactBlankPostMs", cfg.impactBlankPostMs);
    if (cfg.adaptive)
        cfg.impactUs = impactUs;
    return cfg;
}

} // namespace pinpoint::analysis
