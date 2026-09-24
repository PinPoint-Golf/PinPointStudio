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

// CT-I7, CT-I8, CT-I20 and CT-I35 on the host path, and 8.1's three shapes.
// Work package H5, the arbitration bridge.
//
// The host declares a microphone of its own, so "two nominators of the SAME
// basis from DIFFERENT peers" is reachable — which is the half of CT-I8 that a
// per-modality slot fails silently, and the reason this bridge replaces
// `ShotArbiter` rather than wrapping it.

#include "ppcp_host_engine.h"
#include "ppcp_import_ledger.h"
#include "ppcp_live_session.h"
#include "ppcp_shot_bridge.h"
#include "ppcp_source_declaration.h"
#include "ppcp_test_peer.h"

#include <gtest/gtest.h>

#include <set>
#include <string>

using namespace Ppcp;
using pptest::DevicePeer;
using pptest::idStr;

namespace {

constexpr const char *kSession = "sess:arb";
constexpr std::int64_t kMs = 1000000;

// A deterministic id source.  8.3e says ids SHOULD be UUIDs and the library has
// no random source (ground rule 8), so the embedding supplies them — and a test
// that wants to assert WHICH Candidate ended up where wants them predictable.
struct Ids {
    int n = 0;
    std::string prefix = "id";
    bool next(std::string *out) { *out = prefix + "-" + std::to_string(++n); return true; }
};

struct Fixture {
    DevicePeer                  dev;
    std::unique_ptr<PpcpEngine> host;
    PpcpSourceDeclaration       decl;
    PpcpLiveSession             live;
    PpcpShotBridge              bridge;
    Ids                         ids;
    std::vector<std::string>    shots;
    std::vector<std::size_t>    shotCandidateCounts;
    ppcp_sim_clock              hostClk{};
    ppcp_clock                  hostIface{};

    std::int64_t nowNs()
    {
        ppcp_instant i{};
        EXPECT_EQ(ppcp_clock_read(&hostIface, kHostTimebaseId, &i), PPCP_OK);
        return i.ns;
    }

    void build(ppcp_role hostRole = PPCP_ROLE_HOST)
    {
        ASSERT_EQ(ppcp_sim_clock_init(&hostClk, kHostTimebaseId, 1000000000), PPCP_OK);
        hostIface = ppcp_sim_clock_interface(&hostClk);
        dev.build();

        // The host owns a microphone.  Without one it has nothing to nominate
        // with, and CT-I8's "a host microphone and a device microphone" is
        // unreachable — which is how a per-modality arbiter passes every test
        // it is given.
        PpcpSourceDeclaration::Inventory inv;
        inv.hasMicrophone = true;
        inv.microphone.id = "mic-0";
        inv.microphone.label = "bay microphone";
        std::string derr;
        ASSERT_TRUE(decl.build("host-1", inv, &derr)) << derr;
        ASSERT_GT(decl.sourceCount(), 0u) << "the host must own a Source to nominate from";

        HostEngineConfig cfg;
        cfg.peerId = "host-1";
        cfg.listener = true;
        cfg.clock = hostIface;
        cfg.syncTimebase = kHostTimebaseId;
        std::string why;
        host = makeHostEngine(std::move(cfg), &why);
        ASSERT_NE(host, nullptr) << why;
        (void)hostRole;

        live.attach(host->peer(), &decl);
        bridge.attach(host->peer(), &decl, &live);
        bridge.setShotCallback([this](const ppcp_shot &s) {
            shots.push_back(idStr(s.id));
            shotCandidateCounts.push_back(s.candidate_count);
        });
    }

    void toHost() { pptest::pipe(dev.p, host->peer(), PPCP_CHANNEL_CONTROL,
                                 [this] { drainHost(); }); }
    void toDevice(const pptest::EventSink &sink = {})
    { pptest::pipe(host->peer(), dev.p, PPCP_CHANNEL_CONTROL, {}, sink); }

    void drainHost()
    {
        pptest::drainEvents(host->peer(), [this](const ppcp_event &e) {
            live.observe(e);
            bridge.observe(e);
        });
    }

    void declare()
    {
        ASSERT_EQ(ppcp_peer_declare(dev.p, &dev.desc), PPCP_OK);
        toHost();
        ASSERT_EQ(ppcp_peer_declare(host->peer(), decl.peer()), PPCP_OK);
        toDevice();
    }

    void openSession()
    {
        PpcpLiveSession::Config cfg;
        cfg.sessionId = kSession;
        std::string err;
        ASSERT_TRUE(live.open(cfg, &err)) << err;
        toDevice();
    }

    void startBridge()
    {
        PpcpShotBridge::Config cfg;
        cfg.peerId = "host-1";
        std::string err;
        ASSERT_TRUE(bridge.start(cfg, [this](std::string *o) { return ids.next(o); }, &err))
            << err;
    }

    // The host's own microphone Source, whatever the declaration called it.
    std::string hostMicSourceId() const
    {
        for (const ppcp_source &s : decl.sources())
            if (idStr(s.kind) == "microphone") return idStr(s.id);
        return {};
    }

    // A relation `tb:dev` -> `tb:host`, DECLARED rather than measured, so a
    // device Candidate can be converted into `timebase_ref`.  Declaring it here
    // is legitimate — 5.4 allows `method: declared` — and it keeps this test
    // about arbitration rather than about §6.3, which has its own suite.
    void declareRelation(std::int64_t offsetNs, double sigmaNs)
    {
        ppcp_timebase_relation r{};
        ppcp_instant at{};
        ASSERT_EQ(ppcp_instant_make_z(&at, "tb:dev", 0), PPCP_OK);
        ASSERT_EQ(ppcp_relation_make_affine(&r, "tb:dev", kHostTimebaseId, offsetNs, 0.0,
                                            sigmaNs, 0.0, PPCP_RELM_DECLARED, &at),
                  PPCP_OK);
        ppcp_relation_set *rs = ppcp_peer_relations(host->peer());
        ASSERT_NE(rs, nullptr);
        ASSERT_EQ(ppcp_relations_put(rs, &r), PPCP_OK);
    }

    // A Candidate from the DEVICE's microphone, on the device's clock.
    std::string deviceNominates(std::int64_t devNs, double confidence, const char *basis)
    {
        std::string cid;
        deviceNominatesInto(devNs, confidence, basis, &cid);
        return cid;
    }
    void deviceNominatesInto(std::int64_t devNs, double confidence, const char *basis,
                             std::string *outId)
    {
        ppcp_candidate c{};
        static int n = 0;
        const std::string cid = "dev-c-" + std::to_string(++n);
        if (outId) *outId = cid;
        ppcp_instant at{};
        ASSERT_EQ(ppcp_instant_make_z(&at, dev.tb.c_str(), devNs), PPCP_OK);
        ASSERT_EQ(ppcp_candidate_make(&c, cid.c_str(), dev.peerId.c_str(), "src-mic",
                                      basis, &at, confidence), PPCP_OK);
        ASSERT_EQ(ppcp_peer_nominate(dev.p, &c), PPCP_OK);
        toHost();
    }

    // ── MSG 8.5 helpers ────────────────────────────────────────────────────

    // 8.2i — the device mints on its own authority, referencing its Candidate.
    // `t0` is in `Session.timebase_ref`, which is `tb:host` here (5.13c).
    void deviceMints(const std::string &shotId, const std::string &candidateId,
                     std::int64_t t0HostNs)
    {
        ppcp_instant t0{};
        ASSERT_EQ(ppcp_instant_make_z(&t0, kHostTimebaseId, t0HostNs), PPCP_OK);
        ppcp_shot s{};
        ASSERT_EQ(ppcp_shot_make(&s, shotId.c_str(), kSession, &t0, PPCP_AUTHORITY_DEVICE,
                                 dev.peerId.c_str(), candidateId.c_str()), PPCP_OK);
        ASSERT_EQ(ppcp_peer_shot(dev.p, &s), PPCP_OK);
        toHost();
    }

    // What the DEVICE received on control since the last call — the wire, read
    // by a real engine, which is the only place a `shot_disposition` means
    // anything.
    struct Heard {
        std::vector<std::string> declinedShots, reasons, sessions;
        int commits = 0;
    };
    Heard hearFromHost()
    {
        Heard h;
        toDevice([&h](const ppcp_event &e) {
            if (!e.msg) return;
            if (e.msg->type == PPCP_MT_SHOT_DISPOSITION) {
                const ppcp_body_shot_disposition &b = e.msg->body.shot_disposition;
                EXPECT_TRUE(ppcp_shot_disposition_is_declined(&b));
                h.declinedShots.push_back(idStr(b.shot_id));
                h.reasons.push_back(b.has_reason ? idStr(b.reason) : std::string());
                h.sessions.push_back(e.msg->env.has_session_id ? idStr(e.msg->env.session_id)
                                                               : std::string());
            }
            if (e.msg->type == PPCP_MT_CAPTURE_COMMITTED) ++h.commits;
        });
        return h;
    }

    // A shot-anchored Capture from the device, announced to the host, so the
    // host's transfer table knows its anchor and I40 can be asserted against
    // it.  Returns the digest a commit would carry.
    ppcp_digest deviceAnnouncesCapture(const std::string &captureId, const std::string &shotId)
    {
        if (!streamOpen) {
            ppcp_instant at{};
            EXPECT_EQ(ppcp_instant_make(&at, dev.tb.c_str(), dev.tb.size(), dev.clockNs),
                      PPCP_OK);
            ppcp_stream st{};
            EXPECT_EQ(ppcp_stream_make(&st, "str:cam", kSession, "src-cam",
                                       PPCP_STREAM_KIND_VIDEO, "p-cap", dev.tb.c_str(),
                                       PPCP_SHOT_WINDOWED, &at), PPCP_OK);
            EXPECT_EQ(ppcp_peer_stream_open(dev.p, &st), PPCP_OK);
            toHost();
            streamOpen = true;
        }
        ppcp_capture c{};
        EXPECT_EQ(ppcp_capture_make_shot(&c, captureId.c_str(), shotId.c_str(), "str:cam",
                                         PPCP_COMPLETE), PPCP_OK);
        const std::uint8_t bytes[] = { 1, 2, 3, 4, 5, 6, 7, 8 };
        ppcp_digest d{};
        EXPECT_EQ(ppcp_payload_digest(bytes, sizeof bytes, &d), PPCP_OK);
        EXPECT_EQ(ppcp_capture_set_digest(&c, &d, sizeof bytes), PPCP_OK);
        EXPECT_EQ(ppcp_peer_capture_announce(dev.p, &c, false, nullptr, nullptr, 0), PPCP_OK);
        toHost();
        return d;
    }
    bool streamOpen = false;
};

}  // namespace

// ── CT-I20 — arbitration is the host's, and only the host's ───────────────

TEST(PpcpArbitration, ANonHostPeerCannotArbitrate)
{
    // The device end is `role: capture` and declares Mint, not Arbitrate.
    // ppcp_arbiter_new() refuses it — I20 by construction, and CONF §1d's
    // negative half: a peer that arbitrated without declaring it is
    // non-conformant and the refusal is what stops that reaching a wire.
    DevicePeer dev;
    ASSERT_NO_FATAL_FAILURE(dev.build());

    std::vector<std::uint8_t> storage(ppcp_arbiter_sizeof(), 0);
    ppcp_arbiter *a = nullptr;
    Ids ids;
    auto idFn = [](void *ctx, ppcp_id *out) -> ppcp_result {
        Ids *i = static_cast<Ids *>(ctx);
        std::string s;
        i->next(&s);
        return ppcp_id_set(out, s.c_str(), s.size());
    };
    EXPECT_NE(ppcp_arbiter_new(storage.data(), storage.size(), dev.p, idFn, &ids, &a), PPCP_OK);
    EXPECT_EQ(a, nullptr);

    // And this application's bridge reports the refusal rather than degrading
    // to something that looks like it worked.
    PpcpShotBridge b;
    b.attach(dev.p, nullptr, nullptr);
    PpcpShotBridge::Config cfg;
    cfg.peerId = dev.peerId;
    std::string err;
    EXPECT_FALSE(b.start(cfg, [&ids](std::string *o) { return ids.next(o); }, &err));
    EXPECT_FALSE(err.empty());
    EXPECT_FALSE(b.active());
}

// ── CT-I8 — two nominators of the SAME basis, both retained ───────────────

TEST(PpcpArbitration, TwoAcousticNominatorsFromDifferentPeersBothAppear)
{
    Fixture F;
    ASSERT_NO_FATAL_FAILURE(F.build());
    ASSERT_NO_FATAL_FAILURE(F.declare());
    ASSERT_NO_FATAL_FAILURE(F.openSession());
    ASSERT_NO_FATAL_FAILURE(F.startBridge());
    EXPECT_TRUE(F.bridge.active());

    F.declareRelation(/*offsetNs=*/0, /*sigmaNs=*/100000.0);

    const std::int64_t t = F.nowNs() + 10 * kMs;

    // The device's microphone hears it…
    F.deviceNominates(t, 0.9, kBasisAcoustic);
    // …and so does ours, 3 ms later — inside the 50 ms coincidence window, so
    // 8.2b treats them as nominating the SAME Shot.
    std::string err;
    ASSERT_TRUE(F.bridge.nominate(F.hostMicSourceId(), kBasisAcoustic, t + 3 * kMs, 0,
                                  0.7, nullptr, nullptr, &err)) << err;

    EXPECT_EQ(F.bridge.stats().nominated, 1u);
    EXPECT_EQ(F.bridge.stats().observedForeign, 1u);
    EXPECT_EQ(F.bridge.groupCount(), 1u) << "8.2b groups by instant, not by modality";

    // 8.2h — issue no earlier than `issue_hold_ns` after the earliest
    // contributing Candidate.
    ppcp_sim_clock_advance(&F.hostClk, 400 * kMs);
    F.bridge.pump(F.nowNs());

    ASSERT_EQ(F.shots.size(), 1u) << "two microphones, one swing, one Shot";
    // ⚠ THE ASSERTION THE OLD ARBITER FAILS.  `ShotArbiter` models three fixed
    // modalities in fixed slots; a second `acoustic` nomination overwrites the
    // first and nothing anywhere records that it happened.  Here BOTH are in
    // `Shot.candidates`, which is 8.2f and I8.
    EXPECT_EQ(F.shotCandidateCounts.front(), 2u);
}

// ── CT-I7 / 8.2e — a late Candidate ATTACHES and `t0` is not revised ──────

TEST(PpcpArbitration, ACandidateArrivingAfterTheShotAttachesAndT0IsNotRevised)
{
    Fixture F;
    ASSERT_NO_FATAL_FAILURE(F.build());
    ASSERT_NO_FATAL_FAILURE(F.declare());
    ASSERT_NO_FATAL_FAILURE(F.openSession());
    ASSERT_NO_FATAL_FAILURE(F.startBridge());
    F.declareRelation(0, 100000.0);

    const std::int64_t t = F.nowNs() + 10 * kMs;
    std::string err;
    ASSERT_TRUE(F.bridge.nominate(F.hostMicSourceId(), kBasisAcoustic, t, 0, 0.9, nullptr,
                                  nullptr, &err)) << err;

    ppcp_sim_clock_advance(&F.hostClk, 400 * kMs);
    ASSERT_EQ(F.bridge.pump(F.nowNs()), 1u);
    ASSERT_EQ(F.shots.size(), 1u);

    const std::string shotId = F.shots.front();
    const std::size_t candsAtIssue = F.shotCandidateCounts.front();
    EXPECT_EQ(candsAtIssue, 1u);

    // Now a device Candidate for the same event arrives LATE — after the Shot
    // was issued.
    F.declareRelation(0, 100000.0);
    F.deviceNominates(t + 5 * kMs, 0.8, kBasisAcoustic);
    F.bridge.pump(F.nowNs());

    // 8.2e: it ATTACHES.  I7: no second Shot, no revision, and the same id.
    // ⚠ AND `t0` COULD NOT HAVE BEEN REVISED EVEN IF THIS HOST WANTED TO:
    // libppcp has no setter for it anywhere, which is I7 by API surface as well
    // as by behaviour.  `ppcp_shot_attach_candidate()` takes a Candidate rather
    // than an instant precisely so that attaching cannot move it.
    EXPECT_EQ(F.shots.size(), 1u) << "a late Candidate does not produce a second Shot";
    EXPECT_EQ(F.shots.front(), shotId);
    EXPECT_EQ(F.shotCandidateCounts.size(), 1u);
}

// ── 8.2d — an over-wide sigma EXCLUDES and RETAINS ───────────────────────

TEST(PpcpArbitration, AnOverWideSigmaExcludesTheCandidateAndKeepsIt)
{
    Fixture F;
    ASSERT_NO_FATAL_FAILURE(F.build());
    ASSERT_NO_FATAL_FAILURE(F.declare());
    ASSERT_NO_FATAL_FAILURE(F.openSession());

    PpcpShotBridge::Config cfg;
    cfg.peerId = "host-1";
    cfg.maxConversionSigmaNs = 1.0e6;   // 1 ms — this host's policy, not libppcp's
    std::string err;
    ASSERT_TRUE(F.bridge.start(cfg, [&F](std::string *o) { return F.ids.next(o); }, &err))
        << err;

    // A relation that exists but is bad: 20 ms of offset uncertainty, twenty
    // times the policy's bound.  8.2d is reached only where a relation EXISTS —
    // a missing or `unrelated` one is decided by the specification and the
    // policy is never asked.
    F.declareRelation(0, 20.0e6);

    const std::int64_t t = F.nowNs() + 10 * kMs;
    F.deviceNominates(t, 0.95, kBasisAcoustic);

    EXPECT_GE(F.bridge.stats().excluded, 1u) << "the policy was consulted and said no";
    // Exclusion is a CONCLUSION, not a discard: the Candidate is retained and
    // remains evidence (I8).  A consumer may re-derive `t0` later with a better
    // clock, which is exactly what retaining it is for.
    EXPECT_GE(F.bridge.retainedCount() + F.bridge.groupCount(), 1u);
}

// ── 8.2i1 — a peer declaring `unrelated` puts EVERY candidate in retention ─

TEST(PpcpArbitration, WithNoRelationEveryForeignCandidateIsRetainedAndNoneIsGrouped)
{
    Fixture F;
    ASSERT_NO_FATAL_FAILURE(F.build());
    ASSERT_NO_FATAL_FAILURE(F.declare());
    ASSERT_NO_FATAL_FAILURE(F.openSession());
    ASSERT_NO_FATAL_FAILURE(F.startBridge());
    // No relation is declared at all.

    F.deviceNominates(F.nowNs() + 10 * kMs, 0.9, kBasisAcoustic);
    F.deviceNominates(F.nowNs() + 12 * kMs, 0.9, kBasisAcoustic);

    ppcp_sim_clock_advance(&F.hostClk, 400 * kMs);
    F.bridge.pump(F.nowNs());

    // 5.4b / 8.2i1 — a Candidate whose relation is missing cannot be expressed
    // in `timebase_ref`, so there is not even an instant to group by.  It is
    // retained with no Shot, FOR EVER, and that is a legal and honest state.
    // The alternative — assuming a zero offset — would produce a Shot whose
    // `t0` is a fabrication, and I7 would then forbid correcting it.
    EXPECT_EQ(F.shots.size(), 0u);
    EXPECT_GE(F.bridge.retainedCount(), 2u);
}

// ── CORE 8.1 — the launch monitor row is a ShotLink and NEVER a Candidate ──

TEST(PpcpArbitration, TheLaunchMonitorRowBecomesAnArrivalPairingLinkConfirmedByObserver)
{
    Fixture F;
    ASSERT_NO_FATAL_FAILURE(F.build());
    ASSERT_NO_FATAL_FAILURE(F.declare());
    ASSERT_NO_FATAL_FAILURE(F.openSession());
    ASSERT_NO_FATAL_FAILURE(F.startBridge());
    F.declareRelation(0, 100000.0);

    const std::int64_t t = F.nowNs() + 10 * kMs;
    std::string err;
    ASSERT_TRUE(F.bridge.nominate(F.hostMicSourceId(), kBasisAcoustic, t, 0, 0.9, nullptr,
                                  nullptr, &err)) << err;
    ppcp_sim_clock_advance(&F.hostClk, 400 * kMs);
    ASSERT_EQ(F.bridge.pump(F.nowNs()), 1u);
    ASSERT_EQ(F.shots.size(), 1u);

    const std::size_t candidatesBefore = F.bridge.stats().nominated;
    ASSERT_TRUE(F.bridge.linkForeignShot(F.shots.front(), "GCQ-00417",
                                         "com.foresightsports.gcquad", 1.0, &err)) << err;

    // 8.1e — nothing was synthesised.  The CSV has no timestamp, so no Instant,
    // no Timebase and no TimebaseRelation was invented for it, and the
    // Candidate count did not move.
    EXPECT_EQ(F.bridge.stats().nominated, candidatesBefore);
    EXPECT_EQ(F.bridge.stats().shotLinks, 1u);

    bool sawLink = false;
    ppcp_shot_link got{};
    // Drained DURING the pipe: since F-L13-1 the feed refuses a frame it
    // cannot report, so a frame behind an undrained ring never arrives at all.
    F.toDevice([&](const ppcp_event &e) {
        if (e.kind == PPCP_EVENT_SHOT_LINK && e.msg) { sawLink = true; got = e.msg->body.shot_link.link; }
    });
    ASSERT_TRUE(sawLink);
    EXPECT_EQ(idStr(got.basis), std::string(PPCP_LINK_ARRIVAL_PAIRING));
    EXPECT_TRUE(got.confirmed);
    ASSERT_TRUE(got.has_confirmed_by);
    // 5.16f — `arrival_pairing` is NOT retrospective, which is precisely why
    // `observer` is permitted here and would be refused on
    // `sequence_alignment`.  The host armed the slot and watched the row
    // arrive; that is an observation, not a human decision.
    EXPECT_EQ(got.confirmed_by, PPCP_CONFIRMED_BY_OBSERVER);
    EXPECT_FALSE(ppcp_shot_link_basis_is_retrospective(got.basis.v, got.basis.len));
    EXPECT_EQ(idStr(got.local_shot_id), F.shots.front());
    EXPECT_EQ(idStr(got.foreign_shot_id), std::string("GCQ-00417"));
    ASSERT_TRUE(got.has_foreign_system);
    EXPECT_EQ(idStr(got.foreign_system), std::string("com.foresightsports.gcquad"));
}

// ── CORE §8.4 — an orphan capture request ────────────────────────────────

TEST(PpcpArbitration, CaptureRequestNamesT0InTheSessionTimebaseRef)
{
    Fixture F;
    ASSERT_NO_FATAL_FAILURE(F.build());
    ASSERT_NO_FATAL_FAILURE(F.declare());
    ASSERT_NO_FATAL_FAILURE(F.openSession());
    ASSERT_NO_FATAL_FAILURE(F.startBridge());

    std::string err;
    ASSERT_TRUE(F.bridge.requestCapture("shot:1", 123456789, { "st:dev-1:src-cam:video" },
                                        200 * kMs, 800 * kMs, &err)) << err;

    bool saw = false;
    ppcp_body_capture_request req{};
    F.toDevice([&](const ppcp_event &e) {
        if (e.kind == PPCP_EVENT_CAPTURE_REQUEST && e.msg) { saw = true; req = e.msg->body.capture_request; }
    });
    ASSERT_TRUE(saw);
    EXPECT_EQ(idStr(req.shot_id), std::string("shot:1"));
    // 5.13c — `t0` is in `Session.timebase_ref`.  The OWNER inverts §6.1's
    // conversion into its own convention at its end; a host that did it for
    // them would be applying the correction twice (8.2a, I33).
    EXPECT_EQ(idStr(req.t0.tb), std::string(kHostTimebaseId));
    EXPECT_EQ(req.t0.ns, 123456789);
    EXPECT_EQ(req.pre_ns, 200 * kMs);
    EXPECT_EQ(req.post_ns, 800 * kMs);
    ASSERT_EQ(req.stream_id_count, 1u);
}

// ── I26 — a Candidate names a Source THIS peer declared ──────────────────

TEST(PpcpArbitration, NominatingFromASourceWeDidNotDeclareIsRefused)
{
    Fixture F;
    ASSERT_NO_FATAL_FAILURE(F.build());
    ASSERT_NO_FATAL_FAILURE(F.declare());
    ASSERT_NO_FATAL_FAILURE(F.openSession());
    ASSERT_NO_FATAL_FAILURE(F.startBridge());

    std::string err;
    // The device's microphone.  It is a real Source, declared by a real peer in
    // this Session — and it is not OURS, so nominating from it would be this
    // host claiming an observation it did not make (5.12a, 7.1a).
    EXPECT_FALSE(F.bridge.nominate("src-mic", kBasisAcoustic, F.nowNs(), 0, 0.9, nullptr,
                                   nullptr, &err));
    EXPECT_EQ(F.bridge.stats().nominationsRefused, 1u);
    EXPECT_EQ(F.bridge.stats().nominated, 0u);
}

// ── Erratum E29 / F-S5-1 — A RETAINED CANDIDATE IS RECONSIDERED ────────────
//
// 8.2d1.  The test above asserts that a Candidate with no relation is retained
// and never grouped, which is right and was where this host stopped.  What 8.2d
// did not say, and E29 now does, is what happens when the relation ARRIVES —
// which on a live link is the normal case, because §6.3's burst takes a moment
// to converge and a device nominates the instant it hears something.
//
// ⚠ THE OLD BEHAVIOUR WAS SILENT AND LOOKED CORRECT.  No error, no Shot, and
// every Candidate present in `retainedCount()` exactly as 8.2d requires.  A
// host could run a whole Session arbitrating nothing and every assertion in
// this file would still have passed.
TEST(PpcpArbitration, ACandidateRetainedForWantOfARelationIsReconsideredWhenItArrives)
{
    Fixture F;
    ASSERT_NO_FATAL_FAILURE(F.build());
    ASSERT_NO_FATAL_FAILURE(F.declare());
    ASSERT_NO_FATAL_FAILURE(F.openSession());
    ASSERT_NO_FATAL_FAILURE(F.startBridge());

    // Nominated BEFORE any relation exists — the sync burst has not converged.
    const std::int64_t at = F.nowNs() + 10 * kMs;
    F.deviceNominates(at, 0.9, kBasisAcoustic);
    ppcp_sim_clock_advance(&F.hostClk, 400 * kMs);
    F.bridge.pump(F.nowNs());

    ASSERT_EQ(F.shots.size(), 0u) << "8.2d: with no relation there is not even an instant";
    ASSERT_GE(F.bridge.retainedCount(), 1u);
    ASSERT_EQ(F.bridge.stats().reconsidered, 0u);

    // The relation arrives.  8.2d1: what was retained for want of one is
    // reconsidered, and this host has to say so — the arbiter owns no clock and
    // no event loop, so it cannot notice for itself.
    F.declareRelation(/*offsetNs=*/0, /*sigmaNs=*/1000.0);
    const std::size_t readmitted = F.bridge.reconsider();
    EXPECT_GE(readmitted, 1u) << "E29: the Candidate is re-admitted to arbitration";
    EXPECT_GE(F.bridge.stats().reconsidered, 1u);

    ppcp_sim_clock_advance(&F.hostClk, 400 * kMs);
    F.bridge.pump(F.nowNs());
    EXPECT_EQ(F.shots.size(), 1u) << "and the Shot that could not be issued now is";
    EXPECT_EQ(F.bridge.retainedCount(), 0u);
}

// ── Erratum E28 / F-S5-3 — AN IMPORTED FRAME NEVER REACHES THE ARBITER ─────
//
// MSG §9.1: a device offers a Session it recorded earlier and replays its
// bundle down the link a LIVE Session is running on.  `ppcp_event::imported`
// marks those frames, and an embedding that ignores the flag arbitrates two
// Sessions as one — the replayed Candidates were nominated against another
// `timebase_ref`, possibly days ago, and their instants are numerically
// plausible, so 8.2 groups them by coincidence with live ones and issues Shots
// that never happened.  Nothing is malformed and nothing goes red.
//
// The guard is asserted here as a branch and end to end in the `IOP-3-live`
// interoperability row, which replays a real bundle over a real socket.
TEST(PpcpArbitration, AnImportedCandidateIsCountedAndNeverArbitrated)
{
    Fixture F;
    ASSERT_NO_FATAL_FAILURE(F.build());
    ASSERT_NO_FATAL_FAILURE(F.declare());
    ASSERT_NO_FATAL_FAILURE(F.openSession());
    ASSERT_NO_FATAL_FAILURE(F.startBridge());
    F.declareRelation(0, 1000.0);

    // A live Candidate, so the arbiter has a group for an imported one to be
    // wrongly folded into — which is the failure, not a crash.
    F.deviceNominates(F.nowNs() + 10 * kMs, 0.9, kBasisAcoustic);
    const std::size_t liveForeign = F.bridge.stats().observedForeign;
    ASSERT_GE(liveForeign, 1u);

    // The same shape of Candidate, arriving as part of a REPLAYED Session.
    ppcp_candidate c{};
    ppcp_instant at{};
    ASSERT_EQ(ppcp_instant_make_z(&at, F.dev.tb.c_str(), F.nowNs() + 12 * kMs), PPCP_OK);
    ASSERT_EQ(ppcp_candidate_make(&c, "imported-c-1", F.dev.peerId.c_str(), "src-mic",
                                  kBasisAcoustic, &at, 0.9), PPCP_OK);
    ppcp_msg m{};
    ASSERT_EQ(ppcp_msg_init(&m, PPCP_MT_CANDIDATE, 4242), PPCP_OK);
    m.body.candidate.candidate = c;

    ppcp_event ev{};
    ev.kind = PPCP_EVENT_CANDIDATE;
    ev.msg = &m;
    ev.status = PPCP_OK;
    ev.imported = true;
    F.bridge.observe(ev);

    EXPECT_EQ(F.bridge.stats().observedForeign, liveForeign)
        << "E28: an imported Candidate is not observed by the live arbiter";
    EXPECT_EQ(F.bridge.stats().importedIgnored, 1u)
        << "and it is COUNTED, so a routed replay is distinguishable from no replay";

    // The same event with the flag clear IS arbitrated — otherwise this test
    // would pass for a bridge that had simply stopped observing Candidates.
    ev.imported = false;
    F.bridge.observe(ev);
    EXPECT_EQ(F.bridge.stats().observedForeign, liveForeign + 1);
}

// ── 8.2d as a CORROBORATION policy (PinPointStudio, 27 Aug 2026) ───────────
//
// This host does not record a swing for a device detection it saw no evidence
// of.  Expressed as 8.2d exclusion rather than as a refusal after the fact,
// because a Shot is a fact the moment it is issued (I7) and there is no way to
// take one back — so the only place refusing costs nothing is before it exists.

TEST(PpcpArbitration, AnUncorroboratedDeviceCandidateIssuesNoShot)
{
    Fixture F;
    ASSERT_NO_FATAL_FAILURE(F.build());
    ASSERT_NO_FATAL_FAILURE(F.declare());
    ASSERT_NO_FATAL_FAILURE(F.openSession());
    ASSERT_NO_FATAL_FAILURE(F.declareRelation(0, 1000.0));
    ASSERT_NO_FATAL_FAILURE(F.startBridge());
    // The host saw nothing.
    F.bridge.setCorroborationCallback([](std::int64_t) { return false; });

    F.deviceNominates(F.nowNs(), 0.9, kBasisAcoustic);
    EXPECT_EQ(F.bridge.stats().uncorroborated, 1u);

    ppcp_sim_clock_advance(&F.hostClk, 300 * kMs);
    F.bridge.pump(F.nowNs());

    EXPECT_TRUE(F.shots.empty())
        << "8.2d takes the Candidate out of arbitration, and arbitration is what issues";
    EXPECT_EQ(F.bridge.retainedCount(), 1u)
        << "exclusion is a conclusion, not a discard: the Candidate remains evidence (I8)";
}

TEST(PpcpArbitration, ACorroboratedDeviceCandidateStillIssues)
{
    Fixture F;
    ASSERT_NO_FATAL_FAILURE(F.build());
    ASSERT_NO_FATAL_FAILURE(F.declare());
    ASSERT_NO_FATAL_FAILURE(F.openSession());
    ASSERT_NO_FATAL_FAILURE(F.declareRelation(0, 1000.0));
    ASSERT_NO_FATAL_FAILURE(F.startBridge());
    F.bridge.setCorroborationCallback([](std::int64_t) { return true; });

    F.deviceNominates(F.nowNs(), 0.9, kBasisAcoustic);
    ppcp_sim_clock_advance(&F.hostClk, 300 * kMs);
    F.bridge.pump(F.nowNs());

    EXPECT_EQ(F.bridge.stats().uncorroborated, 0u);
    EXPECT_EQ(F.shots.size(), 1u)
        << "the policy is a corroboration rule, not a way of refusing every device";
}

// ⚠ THE ONE THAT SETTLED A DESIGN ARGUMENT, AND IS KEPT FOR THAT REASON.
//
// Reading 8.2d1 / E29 — "reconsider what was RETAINED for want of a relation" —
// alongside `ppcp_arbiter_reconsider()`, which walks only the retained list, it
// looks as though a policy exclusion must be permanent: the Candidate is marked
// inside its group and the retained list never sees it.  That would have made
// this whole policy unusable, because a phone detects a millisecond or two
// before a host microphone as a matter of course, and every such Candidate
// would have been refused for ever on arrival order alone.  A deferral queue
// was built here to work around it.
//
// It is not permanent, and the deferral was deleted.  libppcp retains an
// excluded Candidate that creates no group — "An excluded Candidate never
// CREATES a Shot ... It is retained, and a Shot issued near it later will pick
// it up" — so reconsider() reaches it after all.  This test is the evidence,
// and it is what `ShotController` relies on when it calls reconsider() as each
// of this host's own detectors fires.
TEST(PpcpArbitration, APolicyExclusionIsRetainedAndReconsiderTakesItBack)
{
    Fixture F;
    ASSERT_NO_FATAL_FAILURE(F.build());
    ASSERT_NO_FATAL_FAILURE(F.declare());
    ASSERT_NO_FATAL_FAILURE(F.openSession());
    ASSERT_NO_FATAL_FAILURE(F.declareRelation(0, 1000.0));
    ASSERT_NO_FATAL_FAILURE(F.startBridge());

    bool corroborates = false;
    F.bridge.setCorroborationCallback([&corroborates](std::int64_t) { return corroborates; });

    // The device beats this host's detector to the punch.
    F.deviceNominates(F.nowNs(), 0.9, kBasisAcoustic);
    EXPECT_EQ(F.bridge.stats().uncorroborated, 1u);
    EXPECT_EQ(F.bridge.retainedCount(), 1u)
        << "a policy exclusion that creates no group is RETAINED, which is what makes it "
           "reachable again";

    // This host's own detector fires a moment later.
    corroborates = true;
    EXPECT_EQ(F.bridge.reconsider(), 1u)
        << "8.2d1/E29 re-admits it: the exclusion was a conclusion about the evidence then, "
           "not a permanent verdict";
    EXPECT_EQ(F.bridge.retainedCount(), 0u);

    ppcp_sim_clock_advance(&F.hostClk, 300 * kMs);
    F.bridge.pump(F.nowNs());
    EXPECT_EQ(F.shots.size(), 1u)
        << "and the Shot issues, so arrival order does not decide the outcome";
}

// ── MSG 8.5 (CR-03) — a device Shot this host never adopted is DECLINED ────
//
// The #105 symptom, from the host's end.  The corroboration policy excludes a
// device Candidate, so no group forms and nothing issues; the device's own 8.2i
// deadline then mints on its authority — "the honest ending to 'it saw the
// strike and we did not'".  Until CR-03 that ending was silent: the bridge
// counted the Shot as `adopted` (every PPCP_OK was), the host recorded nothing,
// and the phone held the clip for the life of the link.
//
// ⚠ AND NOT ON ARRIVAL.  8.5c / R-5: reconsider() can still admit the Candidate
// and adopt the Shot, so the verdict waits out the window first.

TEST(PpcpShotDisposition, AnUnadoptedDeviceShotIsDeclinedNotCorroboratedOnceTheWindowCloses)
{
    Fixture F;
    ASSERT_NO_FATAL_FAILURE(F.build());
    ASSERT_NO_FATAL_FAILURE(F.declare());
    ASSERT_NO_FATAL_FAILURE(F.openSession());
    ASSERT_NO_FATAL_FAILURE(F.declareRelation(0, 1000.0));
    ASSERT_NO_FATAL_FAILURE(F.startBridge());
    F.bridge.setCorroborationCallback([](std::int64_t) { return false; });

    const std::int64_t t = F.nowNs();
    const std::string cid = F.deviceNominates(t, 0.9, kBasisAcoustic);
    ppcp_sim_clock_advance(&F.hostClk, 300 * kMs);
    F.bridge.pump(F.nowNs());
    ASSERT_TRUE(F.shots.empty()) << "precondition: the policy excluded it, nothing issued";

    // 8.2i — the device mints.
    ASSERT_NO_FATAL_FAILURE(F.deviceMints("dev-shot-1", cid, t));
    EXPECT_EQ(F.bridge.stats().adopted, 0u)
        << "⚠ THE STAT THIS FIXES: every PPCP_OK used to count as adopted";
    EXPECT_EQ(F.bridge.stats().notAdopted, 1u);
    EXPECT_TRUE(F.bridge.isAwaitingVerdict("dev-shot-1"));
    const ppcp_digest d = F.deviceAnnouncesCapture("cap-d-1", "dev-shot-1");

    // Inside the window: nothing is said.  The first pump stamps the deadline.
    F.bridge.pump(F.nowNs());
    ppcp_sim_clock_advance(&F.hostClk, 1000 * kMs);
    F.bridge.pump(F.nowNs());
    Fixture::Heard early = F.hearFromHost();
    EXPECT_TRUE(early.declinedShots.empty())
        << "R-5 — declined before the reconsider window closed";

    // Past it (issue_hold 200 ms + heartbeat 1000 ms, from the first pump).
    ppcp_sim_clock_advance(&F.hostClk, 300 * kMs);
    F.bridge.pump(F.nowNs());
    Fixture::Heard h = F.hearFromHost();
    ASSERT_EQ(h.declinedShots.size(), 1u) << "no shot_disposition reached the device";
    EXPECT_EQ(h.declinedShots.front(), "dev-shot-1");
    EXPECT_EQ(h.reasons.front(), "not_corroborated");
    EXPECT_EQ(h.sessions.front(), kSession) << "§8.5 — the envelope names the Shot's Session";
    EXPECT_EQ(h.commits, 0);
    EXPECT_EQ(F.bridge.stats().declined, 1u);
    EXPECT_FALSE(F.bridge.isAwaitingVerdict("dev-shot-1"));
    EXPECT_TRUE(F.shots.empty()) << "a declined Shot is never handed on to be recorded";

    // ⛔ I40 — and no `capture_committed` can follow for a Capture of it.
    EXPECT_TRUE(ppcp_peer_has_declined_shot(F.host->peer(), "dev-shot-1"));
    EXPECT_TRUE(ppcp_peer_has_declined_capture(F.host->peer(), "cap-d-1"));
    EXPECT_EQ(ppcp_peer_capture_committed(F.host->peer(), "cap-d-1", &d), PPCP_ERR_INVALID);
    EXPECT_EQ(F.hearFromHost().commits, 0);

    // Said once: later pumps and a re-sent `shot` say nothing more (8.5c).
    ppcp_sim_clock_advance(&F.hostClk, 2000 * kMs);
    F.bridge.pump(F.nowNs());
    EXPECT_TRUE(F.hearFromHost().declinedShots.empty());
}

// ⭐ THE REASON FOR THE WINDOW.  The phone's Candidate beat this host's
// microphone, was excluded, and the phone minted.  Then the microphone fires:
// `ShotController` calls reconsider(), the Candidate is re-admitted, and the
// device's Shot — re-offered to the arbiter — is ADOPTED under 8.2k.  Declining
// on arrival would have told the phone to drop a swing this host then records.
TEST(PpcpShotDisposition, ADeviceShotAdoptedInsideTheWindowIsNeverDeclined)
{
    Fixture F;
    ASSERT_NO_FATAL_FAILURE(F.build());
    ASSERT_NO_FATAL_FAILURE(F.declare());
    ASSERT_NO_FATAL_FAILURE(F.openSession());
    ASSERT_NO_FATAL_FAILURE(F.declareRelation(0, 1000.0));
    ASSERT_NO_FATAL_FAILURE(F.startBridge());
    bool corroborates = false;
    F.bridge.setCorroborationCallback([&corroborates](std::int64_t) { return corroborates; });

    const std::int64_t t = F.nowNs();
    const std::string cid = F.deviceNominates(t, 0.9, kBasisAcoustic);
    ppcp_sim_clock_advance(&F.hostClk, 300 * kMs);
    F.bridge.pump(F.nowNs());
    ASSERT_NO_FATAL_FAILURE(F.deviceMints("dev-shot-2", cid, t));
    F.bridge.pump(F.nowNs());
    ASSERT_TRUE(F.bridge.isAwaitingVerdict("dev-shot-2"));

    // This host's own detector fires late.
    corroborates = true;
    EXPECT_EQ(F.bridge.reconsider(), 1u);
    EXPECT_FALSE(F.bridge.isAwaitingVerdict("dev-shot-2"));
    EXPECT_EQ(F.bridge.stats().adopted, 1u) << "8.2k — the device's Shot is the one that exists";
    ASSERT_EQ(F.shots.size(), 1u);
    EXPECT_EQ(F.shots.front(), "dev-shot-2") << "I35 — adopted, not competed with";

    ppcp_sim_clock_advance(&F.hostClk, 3000 * kMs);
    F.bridge.pump(F.nowNs());
    EXPECT_TRUE(F.hearFromHost().declinedShots.empty())
        << "an adopted Shot was declined";
    EXPECT_EQ(F.bridge.stats().declined, 0u);
}

// 8.2k at arrival: a group of ours is still inside its issue hold, so the device
// Shot is adopted outright and never waits.  `adopted` counts it; the decline
// machinery never sees it.
TEST(PpcpShotDisposition, ADeviceShotSharingAnUnissuedGroupIsAdoptedAndCountedOnce)
{
    Fixture F;
    ASSERT_NO_FATAL_FAILURE(F.build());
    ASSERT_NO_FATAL_FAILURE(F.declare());
    ASSERT_NO_FATAL_FAILURE(F.openSession());
    ASSERT_NO_FATAL_FAILURE(F.declareRelation(0, 1000.0));
    ASSERT_NO_FATAL_FAILURE(F.startBridge());
    F.bridge.setCorroborationCallback([](std::int64_t) { return true; });

    const std::int64_t t = F.nowNs();
    const std::string cid = F.deviceNominates(t, 0.9, kBasisAcoustic);
    ASSERT_NO_FATAL_FAILURE(F.deviceMints("dev-shot-3", cid, t));   // before our issue hold
    EXPECT_EQ(F.bridge.stats().adopted, 1u);
    EXPECT_EQ(F.bridge.stats().notAdopted, 0u);
    EXPECT_FALSE(F.bridge.isAwaitingVerdict("dev-shot-3"));
    ppcp_sim_clock_advance(&F.hostClk, 3000 * kMs);
    F.bridge.pump(F.nowNs());
    EXPECT_TRUE(F.hearFromHost().declinedShots.empty());
}

// ── MSG 8.5i / 8.5j — a decline owed while the link was down ──────────────
//
// `onSwingFailed` decides 15-40 s after the shot, and on the last swing of a
// session the golfer has pressed Stop by then.  The decline goes into the
// ledger and is paid on the next connection with the owning phone, naming the
// Session the Shot belonged to — the live one through the library's own sender
// (which arms I40), an older one with that Session in the envelope.
TEST(PpcpShotDisposition, OwedDeclinesArePaidOnTheWireInTheirOwnSession)
{
    Fixture F;
    ASSERT_NO_FATAL_FAILURE(F.build());
    ASSERT_NO_FATAL_FAILURE(F.declare());
    ASSERT_NO_FATAL_FAILURE(F.openSession());

    PpcpImportLedger ledger;
    // A commit owed for a Capture of the Shot, not yet paid.
    ASSERT_TRUE(ledger.queueCommitted(CaptureKey{ "dev-1", kSession, "cap-x" }, "", "shot-live"));
    ASSERT_EQ(ledger.pendingCommits("dev-1").size(), 1u);

    EXPECT_EQ(ledger.declineShot("dev-1", kSession, "shot-live", "discarded"), 1u)
        << "E74 — a declining receiver owes no unpaid commit";
    EXPECT_TRUE(ledger.pendingCommits("dev-1").empty());
    EXPECT_FALSE(ledger.queueCommitted(CaptureKey{ "dev-1", kSession, "cap-y" }, "", "shot-live"))
        << "8.5b — and never queues one afterwards";
    ledger.declineShot("dev-1", "sess:yesterday", "shot-old", "not_requested");
    ledger.declineShot("dev-2", kSession, "shot-other-phone", "busy");
    EXPECT_EQ(ledger.owedDeclineCount(), 3u);

    // The phone is here: pay what is owed TO IT, and only that.
    EXPECT_EQ(payOwedDeclines(ledger, F.host->peer(), "dev-1", kSession), 2u);
    Fixture::Heard h = F.hearFromHost();
    ASSERT_EQ(h.declinedShots.size(), 2u);
    EXPECT_EQ(h.declinedShots[0], "shot-live");
    EXPECT_EQ(h.reasons[0], "discarded");
    EXPECT_EQ(h.sessions[0], kSession);
    EXPECT_EQ(h.declinedShots[1], "shot-old");
    EXPECT_EQ(h.reasons[1], "not_requested");
    EXPECT_EQ(h.sessions[1], "sess:yesterday")
        << "§8.5 — a Shot of another Session is named in ITS Session, not the live one";
    EXPECT_EQ(h.commits, 0);
    EXPECT_EQ(ledger.owedDeclineCount(), 1u) << "dev-2's decline waits for dev-2";
    EXPECT_TRUE(ppcp_peer_has_declined_shot(F.host->peer(), "shot-live"))
        << "the live-Session path arms the engine's own I40 guard";

    // Paid once.  8.5i — met again, it is owed again, and said again.
    EXPECT_EQ(payOwedDeclines(ledger, F.host->peer(), "dev-1", kSession), 0u);
    EXPECT_TRUE(ledger.requeueDecline("dev-1", kSession, "shot-live"));
    EXPECT_EQ(payOwedDeclines(ledger, F.host->peer(), "dev-1", kSession), 1u);
    h = F.hearFromHost();
    ASSERT_EQ(h.declinedShots.size(), 1u);
    EXPECT_EQ(h.reasons[0], "discarded") << "the FIRST reason stands on a repeat";
}
