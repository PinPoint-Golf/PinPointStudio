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

#include "setup_capability.h"

#include <QHash>

#include <algorithm>

namespace pinpoint::analysis {

namespace {

void addRole(std::vector<SegmentRole> &roles, SegmentRole r)
{
    if (r == SegmentRole::Unknown)
        return;
    if (std::find(roles.begin(), roles.end(), r) == roles.end())
        roles.push_back(r);
}

// A requirement that asks for nothing at all — the tempo metrics' "phases" rung, timed off whatever
// segmented the swing. Read field by field rather than through captureDevicesFor(), because that
// folds roles into families and a role outside every family would read as "needs nothing".
bool requirementIsEmpty(const MetricRequirement &r)
{
    return !r.faceOnCamera && !r.dtlCamera && r.imuRoles.empty() && !r.clubTrack && !r.ballTrack
        && !r.launchMonitor && !r.hackMotion && r.minTier == ReconstructionTier::Angles2D;
}

// Every live route needs no capture of its own. Such a metric resolves the same on every setup,
// including one with nothing mounted, so counting it would report "measured" on an empty rig.
bool isCaptureFree(const MetricDescriptor &d)
{
    bool anyLive = false;
    for (const MetricRoute &r : d.routes) {
        if (r.planned) continue;
        anyLive = true;
        if (!requirementIsEmpty(r.requirement)) return false;
    }
    return anyLive;
}

// The part of `req` this context lacks, as a requirement — so captureDevicesFor() can name the kit.
// Mirrors missingForRequirement() field for field (metric_provider.h); the only difference is the
// camera products. A club or ball track is expected exactly when the face-on camera is present
// (contextForSetup), so its gap IS the camera's, and is reported as that.
MetricRequirement missingPart(const MetricRequirement &req, const ShotContext &ctx)
{
    MetricRequirement m;
    m.faceOnCamera  = (req.faceOnCamera && !ctx.hasFaceOn)
                   || (req.clubTrack && !ctx.hasClubTrack)
                   || (req.ballTrack && !ctx.hasBallTrack);
    m.dtlCamera     = req.dtlCamera && !ctx.hasDtl;
    m.launchMonitor = req.launchMonitor && !ctx.hasLaunchMonitor;
    m.hackMotion    = req.hackMotion && !ctx.hasHackMotion;
    for (SegmentRole r : req.imuRoles)
        if (!ctx.hasRole(r))
            m.imuRoles.push_back(r);
    return m;
}

const MetricRoute *routeById(const MetricDescriptor &d, const QString &id)
{
    for (const MetricRoute &r : d.routes)
        if (r.id == id) return &r;
    return nullptr;
}

// The commonest value; a tie goes to the one seen first (manifest order). Empty strings ignored.
QString commonest(const QStringList &values)
{
    QHash<QString, int> count;
    for (const QString &v : values)
        if (!v.isEmpty()) ++count[v];
    QString best;
    int bestN = 0;
    for (const QString &v : values) {
        const int n = count.value(v);
        if (n > bestN) { best = v; bestN = n; }
    }
    return best;
}

struct GroupWork {
    CapabilityGroup out;
    QStringList     estimatedReasons, unavailableReasons, upgrades;
    std::vector<CaptureDevice> gaps;
};

} // namespace

ShotContext contextForSetup(const SetupFacts &facts)
{
    ShotContext c;

    c.hasFaceOn = facts.faceOnCamera;
    c.hasDtl    = facts.dtlCamera;

    // CAMERA PRODUCTS. A ShotContext built from a recording sets these from the analysis
    // (`club.valid`, `ball.valid` — ShotReplayController::shotContext). Before a swing nothing is
    // known, and both trackers run on the face-on stream, so the honest prior is "expected when the
    // face-on camera is present". A swing whose track fails is the review's business, not setup's.
    c.hasClubTrack = facts.faceOnCamera;
    c.hasBallTrack = facts.faceOnCamera;

    c.hasLaunchMonitor = facts.launchMonitor;
    c.hasHackMotion    = facts.hackMotion;

    // ROLES. The caller has already filtered to assigned + enabled + calibrated. A wG3 fills the
    // lead forearm and hand itself: a HackMotion swing carries bindings for both roles
    // (shot_processor.cpp binds its #lowerArm / #palm units like any IMU), so the bare wrist keys
    // resolve on it as well as the hm.* ones.
    for (SegmentRole r : facts.imuRoles)
        addRole(c.imuRoles, r);
    if (facts.hackMotion) {
        addRole(c.imuRoles, SegmentRole::LeadForearm);
        addRole(c.imuRoles, SegmentRole::LeadHand);
    }

    // TIER. A recorded swing's tier is set by the analysis, and only ever to one of two values:
    // Mono3DPlusImu when the job carried IMU streams, Angles2D otherwise (ResemblanceStage,
    // wrist_analyzer.cpp). Nothing assigns Stereo3D — a second camera does NOT raise it — so a
    // planned setup mirrors that rule and no more. It gates nothing today either way: every rung in
    // the manifest leaves minTier at Angles2D (depth is stated as the dtlCamera device instead).
    c.tier = c.imuRoles.empty() ? ReconstructionTier::Angles2D : ReconstructionTier::Mono3DPlusImu;

    // SESSION TYPE stays "none". No descriptor and no provider may read it (metric_providers.h,
    // metric_catalogue_manifest.cpp), and a setup is not a session type — the flow is one flow
    // (design §4.12).
    c.sessionType = -1;

    // BAND stays at its defaults: archetype, club and shape steer normative resolution, not
    // availability, and none of them is known before a swing.
    return c;
}

std::vector<CapabilityGroup> summariseSetup(const MetricCatalogue &catalogue,
                                            const SetupFacts &facts)
{
    const ShotContext ctx = contextForSetup(facts);

    std::vector<GroupWork> work;
    QHash<QString, int>    index;

    for (const MetricDescriptor *d : catalogue.all()) {
        auto it = index.constFind(d->group);
        if (it == index.constEnd()) {
            it = index.insert(d->group, int(work.size()));
            work.push_back({});
            work.back().out.group = d->group;
        }
        GroupWork &g = work[size_t(*it)];

        if (d->planned()) {
            ++g.out.planned;
            g.out.plannedKeys << d->key;
            continue;
        }
        if (isCaptureFree(*d)) {
            ++g.out.derived;
            g.out.derivedKeys << d->key;
            continue;
        }

        const MetricAvailability a = catalogue.resolve(d->key, ctx);
        const MetricRoute *winner = a.routeId.isEmpty() ? nullptr : routeById(*d, a.routeId);

        switch (a.state) {
        case MetricAvailability::Measured:
            ++g.out.measured;
            g.out.measuredKeys << d->key;
            continue;                                   // nothing to explain, nothing to buy
        case MetricAvailability::Bridged:
            ++g.out.estimated;
            g.out.estimatedKeys << d->key;
            // The METHOD, not the reason string: a.reason appends the upgrade sentence, which has
            // its own field.
            g.estimatedReasons << (winner ? winner->summary : a.reason);
            break;
        case MetricAvailability::Unavailable:
            ++g.out.unavailable;
            g.out.unavailableKeys << d->key;
            g.unavailableReasons << a.reason;
            break;
        }
        g.upgrades << a.upgrade;

        // The kit that would do better: every LIVE rung above the winner, or every live rung when
        // nothing fired. Planned rungs are skipped for the resolver's own reason — a golfer must
        // not be sent to buy kit for a pipeline we have not written.
        for (const MetricRoute &r : d->routes) {
            if (winner && &r == winner) break;
            if (r.planned) continue;
            for (CaptureDevice dev : captureDevicesFor(missingPart(r.requirement, ctx)))
                if (std::find(g.gaps.begin(), g.gaps.end(), dev) == g.gaps.end())
                    g.gaps.push_back(dev);
        }
    }

    std::vector<CapabilityGroup> out;
    out.reserve(work.size());
    for (GroupWork &g : work) {
        g.out.estimatedBecause   = commonest(g.estimatedReasons);
        g.out.unavailableBecause = commonest(g.unavailableReasons);
        g.out.upgrade            = commonest(g.upgrades);
        for (CaptureDevice dev : allCaptureDevices()) {                 // canonical order
            if (std::find(g.gaps.begin(), g.gaps.end(), dev) == g.gaps.end()) continue;
            g.out.deviceGaps   << captureDeviceLabel(dev);
            g.out.deviceGapIds << captureDeviceId(dev);
        }
        out.push_back(std::move(g.out));
    }
    return out;
}

} // namespace pinpoint::analysis
