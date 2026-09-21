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

#include "dtl_shaft_json.h"

#include "dtl_shaft_config.h"   // DtlShaftConfig, dtlConfigHash

namespace pinpoint::analysis {

QJsonObject dtlShaftConfigJson(const DtlShaftConfig& cfg)
{
    return QJsonObject{
        { "enabled",              cfg.enabled },
        { "truthOnly",            cfg.truthOnly },
        { "rhoSolveMin",          cfg.rhoSolveMin },
        { "scheduleEnabled",      cfg.schedule.enabled },
        { "minBandFrames",        cfg.minBandFrames },
        { "corridorEnabled",      cfg.corridor.enabled },
        { "corridorW0Deg",        cfg.corridor.w0Deg },
        { "corridorWCorr",        cfg.corridor.wCorr },
        { "corridorRhoFMax",      cfg.corridor.rhoFMax },
        { "halfWHalf",            cfg.half.wHalf },
        { "lenWLen",              cfg.len.wLen },
        { "lenSlack",             cfg.len.slack },
        { "lenHolePx",            cfg.len.holePx },
        { "armVetoDeg",           cfg.arm.vetoDeg },
        { "armLatPx",             cfg.arm.latPx },
        { "armWArm",              cfg.arm.wArm },
        { "armMinJointPx",        cfg.arm.minJointPx },
        { "revWRev",              cfg.rev.wRev },
        { "revTol",               cfg.rev.tol },
        { "ballWBall",            cfg.ball.wBall },
        { "ballSigmaDeg",         cfg.ball.sigmaDeg },
        { "ballGateDeg",          cfg.ball.gateDeg },
        { "ballWGate",            cfg.ball.wGate },
        { "ballStillDeg",         cfg.ball.stillDeg },
        { "ballStillMaxUs",       double(cfg.ball.stillMaxUs) },
        { "ballShadowMatMin",     cfg.ball.shadowMatMin },
        { "ballShadowDrop",       cfg.ball.shadowDrop },
        { "ballShadowLaunchRise", cfg.ball.shadowLaunchRise },
        { "ballShadowToCentreR",  cfg.ball.shadowToCentreR },
        { "omegaBaseDegPerFrame", cfg.omegaBaseDegPerFrame },
        { "kSmooth",              cfg.kSmooth },
        { "grid",                 cfg.grid },
        { "wE2",                  cfg.wE2 },
        { "wBand",                cfg.wBand },
        { "bandTol",              cfg.bandTol },
        { "evRay",                cfg.evRay },
        { "supRay",               cfg.supRay },
        { "revRatio",             cfg.revRatio },
        { "minLenFrac",           cfg.minLenFrac },
        { "snapRhoMin",           cfg.snapRhoMin },
        { "evAbsFloor",           cfg.evAbsFloor },
        { "evAbsFloorDif",        cfg.evAbsFloorDif },
        { "contrastKsz",          cfg.contrastKsz },
        { "plateMaxFrames",       cfg.plateMaxFrames },
        { "quarantineP95Mult",    cfg.quarantine.p95Mult },
        { "quarantineAbsPx",      cfg.quarantine.absPx },
        { "quarantinePostImpactUs", double(cfg.quarantine.postImpactUs) },
        { "quarantinePostAbsPx",  cfg.quarantine.postAbsPx },
        { "revArmDeg",            cfg.rev.armDeg },
        { "revArmMinPx",          cfg.rev.armMinPx },
        { "lineConfRay",          cfg.lineConfRay } };
}

QJsonObject dtlShaftTrackToJson(const DtlShaftTrack2D& track, int64_t t0Us,
                                const DtlShaftConfig& cfg,
                                const QString& streamAlias, const QString& streamFile)
{
    return dtlShaftTrackToJson(track, t0Us, dtlShaftConfigJson(cfg), dtlConfigHash(cfg),
                               streamAlias, streamFile);
}

} // namespace pinpoint::analysis
