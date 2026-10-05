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

// setup_capability_test — the closing page's data (session_wizard_refactor_design.md §4.14).
//
// INVARIANTS, NOT TOTALS. The manifest grows every week; a test pinning "Body rotation: 5 measured"
// would fail on every route promotion and teach nothing. What must hold whatever the manifest says:
//   · every metric lands in exactly one bucket of exactly one group;
//   · adding kit never moves a metric to a worse bucket;
//   · no launch monitor ⇒ every lm.* key is unavailable and its group names the launch monitor;
//   · no arm sensor ⇒ nothing in the wrist group is measured by an arm sensor;
//   · trunk rotation is estimated (or unavailable) from cameras alone and measured with trunk IMUs;
//   · an empty rig records nothing.
// The per-group table for each setup is printed (qInfo) so the real numbers can be read.

#include "setup_capability.h"

#include <QDebug>
#include <QHash>
#include <QSet>

#include <cstdio>
#include <vector>

using namespace pinpoint::analysis;

static int g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}
static void check(bool c, const QString &label) { check(c, qPrintable(label)); }

namespace {

struct NamedSetup {
    const char *name;
    SetupFacts  facts;
};

SetupFacts faceOnOnly()
{
    SetupFacts f;
    f.faceOnCamera = true;
    return f;
}

// The six setups the brief names, plus one auxiliary: two cameras and no trunk sensor, which the
// rotation invariant needs and none of the six provides.
std::vector<NamedSetup> setups()
{
    std::vector<NamedSetup> s;

    s.push_back({ "1 face-on only", faceOnOnly() });

    SetupFacts f2 = faceOnOnly();
    f2.hackMotion = true;
    s.push_back({ "2 face-on + wG3", f2 });

    SetupFacts f3 = faceOnOnly();
    f3.imuRoles = { SegmentRole::LeadForearm, SegmentRole::LeadHand };
    s.push_back({ "3 face-on + Witmotion forearm/hand", f3 });

    SetupFacts f4 = faceOnOnly();
    f4.dtlCamera = true;
    f4.imuRoles  = { SegmentRole::Pelvis, SegmentRole::Thorax };
    s.push_back({ "4 face-on + DTL + pelvis + thorax", f4 });

    SetupFacts f5 = f4;
    f5.hackMotion    = true;
    f5.launchMonitor = true;
    s.push_back({ "5 face-on + DTL + wG3 + pelvis + thorax + LM", f5 });

    s.push_back({ "6 nothing", SetupFacts{} });

    SetupFacts aux = faceOnOnly();
    aux.dtlCamera = true;
    s.push_back({ "aux face-on + DTL, no sensors", aux });
    return s;
}

enum Bucket { Unavailable = 0, Estimated = 1, Measured = 2, Planned = 3, Derived = 4, Missing = 5 };

// key → bucket, and key → group, for one summary.
struct Flat {
    QHash<QString, Bucket>  bucket;
    QHash<QString, QString> group;
    QHash<QString, int>     seen;     // how many lists the key appeared in, across all groups
};

Flat flatten(const std::vector<CapabilityGroup> &groups)
{
    Flat f;
    const auto add = [&f](const QStringList &keys, Bucket b, const QString &g) {
        for (const QString &k : keys) {
            f.bucket.insert(k, b);
            f.group.insert(k, g);
            ++f.seen[k];
        }
    };
    for (const CapabilityGroup &g : groups) {
        add(g.measuredKeys,    Measured,    g.group);
        add(g.estimatedKeys,   Estimated,   g.group);
        add(g.unavailableKeys, Unavailable, g.group);
        add(g.plannedKeys,     Planned,     g.group);
        add(g.derivedKeys,     Derived,     g.group);
    }
    return f;
}

void printTable(const char *name, const std::vector<CapabilityGroup> &groups)
{
    qInfo().noquote() << "";
    qInfo().noquote() << "=== setup" << name;
    qInfo().noquote() << "group | measured | estimated | unavailable | planned | derived | "
                         "estimatedBecause | unavailableBecause | upgrade | deviceGaps";
    for (const CapabilityGroup &g : groups)
        qInfo().noquote() << g.group << "|" << g.measured << "|" << g.estimated << "|"
                          << g.unavailable << "|" << g.planned << "|" << g.derived << "|"
                          << g.estimatedBecause << "|" << g.unavailableBecause << "|"
                          << g.upgrade << "|" << g.deviceGaps.join(QStringLiteral(", "));
}

const CapabilityGroup *findGroup(const std::vector<CapabilityGroup> &groups, const QString &name)
{
    for (const CapabilityGroup &g : groups)
        if (g.group == name) return &g;
    return nullptr;
}

const MetricRoute *routeById(const MetricDescriptor &d, const QString &id)
{
    for (const MetricRoute &r : d.routes)
        if (r.id == id) return &r;
    return nullptr;
}

bool needsArmSensor(const MetricRequirement &r)
{
    if (r.hackMotion) return true;
    for (SegmentRole role : r.imuRoles)
        if (role == SegmentRole::LeadForearm || role == SegmentRole::LeadHand
            || role == SegmentRole::LeadUpperArm)
            return true;
    return false;
}

bool hasArmSensor(const SetupFacts &f)
{
    if (f.hackMotion) return true;
    for (SegmentRole r : f.imuRoles)
        if (r == SegmentRole::LeadForearm || r == SegmentRole::LeadHand
            || r == SegmentRole::LeadUpperArm)
            return true;
    return false;
}

// ── The tests ──────────────────────────────────────────────────────────────────────────────────

void test_context_mirrors_the_setup(const MetricCatalogue &)
{
    std::printf("\n-- contextForSetup states its assumptions --\n");
    SetupFacts f;
    f.faceOnCamera = true;
    f.hackMotion   = true;
    const ShotContext c = contextForSetup(f);
    check(c.hasFaceOn && !c.hasDtl, "cameras copied through");
    check(c.hasClubTrack && c.hasBallTrack, "club and ball tracks expected with the face-on camera");
    check(c.hasRole(SegmentRole::LeadForearm) && c.hasRole(SegmentRole::LeadHand),
          "a wG3 supplies LeadForearm + LeadHand");
    check(c.hasHackMotion, "a wG3 opens the HackMotion axis");
    check(c.tier == ReconstructionTier::Mono3DPlusImu, "IMU roles ⇒ Mono3DPlusImu, as the analysis sets it");
    check(c.sessionType == -1, "sessionType is never set");

    const ShotContext none = contextForSetup(SetupFacts{});
    check(!none.hasClubTrack && !none.hasBallTrack, "no face-on camera ⇒ no club or ball track");
    check(none.tier == ReconstructionTier::Angles2D, "no IMU ⇒ Angles2D");

    SetupFacts two;
    two.faceOnCamera = two.dtlCamera = true;
    check(contextForSetup(two).tier == ReconstructionTier::Angles2D,
          "a second camera does not raise the tier (nothing in the pipeline assigns Stereo3D)");
}

void test_every_metric_in_exactly_one_bucket(const MetricCatalogue &cat, const char *name,
                                             const std::vector<CapabilityGroup> &groups)
{
    const Flat f = flatten(groups);
    int wrong = 0;
    for (const MetricDescriptor *d : cat.all()) {
        if (f.seen.value(d->key) != 1 || f.group.value(d->key) != d->group) {
            ++wrong;
            std::printf("     %s: %s seen %d times, group '%s'\n", name, qPrintable(d->key),
                        f.seen.value(d->key), qPrintable(f.group.value(d->key)));
        }
        const Bucket b = f.bucket.value(d->key, Missing);
        if (d->planned() && b != Planned) ++wrong;
        if (!d->planned() && b == Planned) ++wrong;
    }
    check(wrong == 0, QStringLiteral("%1: every metric in exactly one bucket of its own group").arg(name));

    bool countsMatch = true, reasonsMatch = true;
    for (const CapabilityGroup &g : groups) {
        countsMatch &= g.measured == g.measuredKeys.size() && g.estimated == g.estimatedKeys.size()
                    && g.unavailable == g.unavailableKeys.size()
                    && g.planned == g.plannedKeys.size() && g.derived == g.derivedKeys.size();
        reasonsMatch &= (g.estimated > 0) == !g.estimatedBecause.isEmpty();
        reasonsMatch &= (g.unavailable > 0) == !g.unavailableBecause.isEmpty();
        reasonsMatch &= g.deviceGaps.size() == g.deviceGapIds.size();
        for (const QString &id : g.deviceGapIds)
            reasonsMatch &= id != captureDeviceId(CaptureDevice::ClubTrack)
                         && id != captureDeviceId(CaptureDevice::BallTrack);
    }
    check(countsMatch, QStringLiteral("%1: counts equal their key lists").arg(name));
    check(reasonsMatch, QStringLiteral("%1: a reason exactly when its bucket is non-empty; "
                                       "no camera product named as a gap").arg(name));
}

void test_more_kit_is_never_worse(const char *from, const std::vector<CapabilityGroup> &a,
                                  const char *to, const std::vector<CapabilityGroup> &b)
{
    const Flat fa = flatten(a), fb = flatten(b);
    int worse = 0;
    for (auto it = fa.bucket.constBegin(); it != fa.bucket.constEnd(); ++it) {
        if (it.value() > Measured) continue;                    // planned / derived: not ranked
        const Bucket after = fb.bucket.value(it.key(), Missing);
        if (after > Measured || after < it.value()) {
            ++worse;
            std::printf("     %s: %d -> %d\n", qPrintable(it.key()), int(it.value()), int(after));
        }
    }
    check(worse == 0, QStringLiteral("%1 -> %2: no metric moves to a worse bucket").arg(from, to));
}

void test_launch_monitor_gap(const char *name, const SetupFacts &facts,
                             const std::vector<CapabilityGroup> &groups)
{
    const Flat f = flatten(groups);
    int lmKeys = 0, wrong = 0;
    for (auto it = f.bucket.constBegin(); it != f.bucket.constEnd(); ++it) {
        if (!it.key().startsWith(QStringLiteral("lm."))) continue;
        ++lmKeys;
        if (facts.launchMonitor) {
            if (it.value() == Unavailable) ++wrong;
            continue;
        }
        if (it.value() != Unavailable) { ++wrong; continue; }
        const CapabilityGroup *g = findGroup(groups, f.group.value(it.key()));
        if (!g || !g->deviceGapIds.contains(captureDeviceId(CaptureDevice::LaunchMonitor))) ++wrong;
    }
    check(lmKeys > 0, QStringLiteral("%1: the catalogue has lm.* keys").arg(name));
    check(wrong == 0, facts.launchMonitor
                          ? QStringLiteral("%1: with a launch monitor no lm.* key is unavailable").arg(name)
                          : QStringLiteral("%1: without one every lm.* key is unavailable and its "
                                           "group names the launch monitor").arg(name));
}

// THE BRIEF SAID "the wrist group has no measured metric" without an arm sensor. That is not what
// the manifest says: `trailWristFlexExt` sits in the same group and is read off the face-on image.
// The invariant that does hold is the one that matters to the page — nothing in the group is
// MEASURED BY AN ARM SENSOR when none is worn — and the camera-measured key is named in the output.
void test_no_arm_sensor_no_arm_measurement(const MetricCatalogue &cat, const char *name,
                                           const SetupFacts &facts,
                                           const std::vector<CapabilityGroup> &groups)
{
    if (hasArmSensor(facts)) return;
    const MetricDescriptor *wrist = cat.descriptor(QStringLiteral("leadWristFlexExt"));
    if (!wrist) { check(false, "leadWristFlexExt exists"); return; }
    const CapabilityGroup *g = findGroup(groups, wrist->group);
    if (!g) { check(false, "the wrist group exists"); return; }

    const ShotContext ctx = contextForSetup(facts);
    int armMeasured = 0;
    for (const QString &key : g->measuredKeys) {
        const MetricDescriptor *d = cat.descriptor(key);
        const MetricRoute *r = d ? routeById(*d, cat.resolve(key, ctx).routeId) : nullptr;
        if (!r || needsArmSensor(r->requirement)) ++armMeasured;
        else std::printf("     %s: %s measured without an arm sensor, by route '%s'\n", name,
                         qPrintable(key), qPrintable(r->id));
    }
    check(armMeasured == 0,
          QStringLiteral("%1: no arm sensor ⇒ nothing in '%2' is measured by one").arg(name, g->group));
    check(!g->measuredKeys.contains(QStringLiteral("leadWristFlexExt"))
              && !g->estimatedKeys.contains(QStringLiteral("leadWristFlexExt")),
          QStringLiteral("%1: lead-wrist bow/cup is unavailable").arg(name));
}

void test_rotation(const char *name, const std::vector<CapabilityGroup> &groups, bool expectMeasured)
{
    // pelvisRotationSigned is deliberately absent: its pelvis-IMU rung is planned, so it stays
    // estimated even with the sensor (reported).
    const Flat f = flatten(groups);
    bool ok = true;
    for (const char *k : { "pelvisRotation", "thoraxRotation", "xFactor" }) {
        const Bucket b = f.bucket.value(QString::fromLatin1(k), Missing);
        ok &= expectMeasured ? b == Measured : (b == Estimated || b == Unavailable);
        if (b == Missing) std::printf("     %s missing from the summary\n", k);
    }
    check(ok, expectMeasured
                  ? QStringLiteral("%1: pelvis/thorax rotation measured with trunk IMUs").arg(name)
                  : QStringLiteral("%1: pelvis/thorax rotation estimated or unavailable without "
                                   "them").arg(name));
}

} // namespace

int main()
{
    std::printf("=== Setup capability: the closing page's data ===\n");

    const MetricCatalogue cat = makeMetricCatalogue();
    test_context_mirrors_the_setup(cat);

    const std::vector<NamedSetup> all = setups();
    std::vector<std::vector<CapabilityGroup>> summary;
    for (const NamedSetup &s : all) {
        summary.push_back(summariseSetup(cat, s.facts));
        printTable(s.name, summary.back());
    }

    std::printf("\n-- per setup --\n");
    for (size_t i = 0; i < all.size(); ++i) {
        test_every_metric_in_exactly_one_bucket(cat, all[i].name, summary[i]);
        test_launch_monitor_gap(all[i].name, all[i].facts, summary[i]);
        test_no_arm_sensor_no_arm_measurement(cat, all[i].name, all[i].facts, summary[i]);
    }

    std::printf("\n-- monotonicity --\n");
    const auto mono = [&](size_t a, size_t b) {
        test_more_kit_is_never_worse(all[a].name, summary[a], all[b].name, summary[b]);
    };
    mono(0, 1);   // 1 → 2: add a wG3
    mono(0, 2);   // 1 → 3: add a Witmotion pair
    mono(0, 3);   // 1 → 4: add DTL + trunk
    mono(3, 4);   // 4 → 5: add wG3 + launch monitor
    mono(0, 6);   // 1 → aux: add DTL
    mono(6, 3);   // aux → 4: add trunk
    mono(5, 0);   // 6 → 1: add a camera to nothing

    std::printf("\n-- body rotation --\n");
    test_rotation(all[0].name, summary[0], false);
    test_rotation(all[6].name, summary[6], false);
    test_rotation(all[3].name, summary[3], true);
    test_rotation(all[4].name, summary[4], true);

    std::printf("\n-- instruments stay distinguishable --\n");
    {
        const Flat wg3 = flatten(summary[1]), wit = flatten(summary[2]);
        check(wg3.bucket.value(QStringLiteral("hm.leadWristFlexExt"), Missing) == Measured,
              "a wG3 measures hm.leadWristFlexExt");
        check(wit.bucket.value(QStringLiteral("hm.leadWristFlexExt"), Missing) == Unavailable,
              "a Witmotion pair does not open the hm.* rungs");
        check(wit.bucket.value(QStringLiteral("leadWristFlexExt"), Missing) == Measured,
              "a Witmotion pair measures leadWristFlexExt");
    }

    std::printf("\n-- an empty rig --\n");
    {
        int recorded = 0;
        for (const CapabilityGroup &g : summary[5])
            recorded += g.measured + g.estimated;
        check(recorded == 0, "setup 6 measures and estimates nothing");
        int derived = 0;
        for (const CapabilityGroup &g : summary[5])
            derived += g.derived;
        check(derived > 0, "capture-free metrics are set aside, not counted as measured");
    }

    std::printf(g_fail ? "\nFAILED (%d)\n" : "\nOK\n", g_fail);
    return g_fail ? 1 : 0;
}
