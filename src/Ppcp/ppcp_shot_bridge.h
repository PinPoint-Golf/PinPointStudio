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

// The arbitration bridge: this host's detectors nominate, libppcp arbitrates.
// Work package H5.  CORE §8.1, §8.2, §8.4, §8.5f; MSG §7.
//
// ⚠ IT REPLACES `ShotArbiter`, IT DOES NOT WRAP IT, and that is the whole
// design.  `src/Gui/shot/shot_arbiter.h` models THREE FIXED MODALITIES
// (`ArbSource::Acoustic | Imu | Ball | Pose`) in a fixed-width ring, discards every
// candidate on `decide()`, and rejects anything inside a 1500 ms refractory.
// Each of those is an I8 violation the moment a second peer is in the Session:
//
//   - a host microphone and a device microphone are two Sources of the SAME
//     `basis: acoustic`, and a per-modality slot silently keeps one.  CT-I8
//     exists for exactly that failure, and it is silent by construction;
//   - `decide()` throws the losers away, and 5.13d/8.2f require the issued Shot
//     to reference EVERY contributing and excluded Candidate — "exclusion is a
//     conclusion; the Candidate remains evidence";
//   - a refractory drops a nomination rather than retaining it, so a Candidate
//     that arrives late has no Shot AND no record, where 8.2e says it ATTACHES.
//
// Layering the two would give a Session two arbiters disagreeing about which
// Candidates exist.  So `ShotController` asks this class whether it is active
// and, when it is, does not touch `m_arbiter` at all.
//
// ── WHAT `tb:host` IS, EXACTLY ─────────────────────────────────────────────
//
// `EventBuffer::nowMicros()` and `Ppcp::hostNowNs()` are the SAME `steady_clock`
// reading in different units — microseconds and nanoseconds.  So an
// `estImpactUs` from the acoustic or IMU detector is a reading of `tb:host`
// multiplied by 1000, and nominate() takes nanoseconds because a Candidate's
// instant is nanoseconds.  This is asserted by a test rather than left as a
// comment, because the day one of the two clocks changes is the day every
// arbitrated `t0` is wrong by an unbounded amount with nothing red.
//
// ── I33 / 5.12e — THE CONVERSION HAPPENS HERE AND EXACTLY ONCE ─────────────
//
// The nominator applies §6.1's canonical-instant conversion, because only the
// nominator holds that frame's exposure and the Source's `timing`.  The
// arbiter then applies a TimebaseRelation and nothing else (8.2a).  A host that
// converted again would double the correction — and the error is
// exposure-dependent, so it looks like clock bias rather than like a bug.
// `ppcp_candidate_make_canonical()` is the one function that does it.
//
// ── 8.1b — WHAT IS NEVER A CANDIDATE ──────────────────────────────────────
//
// The GCQuad writes a two-line CSV rewritten in place, with no trustworthy
// timestamp and a shot counter unrelated to anything of ours.  It has an owning
// peer (us, who observed its arrival) and NO clock relation, which is 8.1's
// middle column: a `ShotLink` with `basis: arrival_pairing`, asserted live by
// the observer.  There is deliberately NO function on this class that takes a
// launch monitor reading and returns a Candidate, and 8.1e is why: a peer must
// not synthesise a Timebase, a TimebaseRelation or an Instant for a record that
// has none in order to route it through nomination.  linkForeignShot() is the
// only entry point, and it cannot produce a Candidate.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <ppcp/model.h>
#include <ppcp/peer.h>
#include <ppcp/shot.h>

#include "ppcp_live_session.h"
#include "ppcp_source_declaration.h"

namespace Ppcp {

// CORE 5.12's open registry, in the spellings §5.12 tabulates.  Strings and not
// an enum, because 10.3a/I13 make an unknown value round-trip rather than be
// fatal — and because a fixed enumeration is the shape the arbiter this class
// replaces got wrong.
inline constexpr const char *kBasisAcoustic = "acoustic";
inline constexpr const char *kBasisMotion   = "motion";
inline constexpr const char *kBasisExternal = "external";
// Not in §5.12's table: the ball-launch detector is a vision nominator and the
// registry is open (10.3a).  A receiver that does not know the value carries it
// unchanged, which is exactly what an open registry is for.
inline constexpr const char *kBasisVision   = "vision";

class PpcpShotBridge {
public:
    struct Config {
        // CORE 5.1a — this host's own Id, which every Candidate it nominates
        // carries in `peer_id`.
        std::string peerId;

        // 8.2d — APPLICATION POLICY, and the reason the library takes a
        // callback rather than a number (I14).  A Candidate whose conversion
        // into `timebase_ref` carries more uncertainty than this is excluded
        // from setting `t0` and RETAINED in `Shot.candidates`.
        //
        // 5 ms because that is a frame at 200 fps and the arbitrated instant is
        // read against video; it has not been measured on a rig, and CORE B8
        // says the same of the two Session parameters beside it.  It is one
        // number in one place so a measurement replaces it here and nowhere
        // else.
        double maxConversionSigmaNs = 5.0e6;

        // ── MSG 8.5 / 8.5c — how long a device Shot may wait to be adopted ──
        //
        // A device-minted Shot this host did not adopt is DECLINED
        // (`not_corroborated`), but not on arrival: its Candidate is retained,
        // and reconsider() — on this host's own detector firing, or a relation
        // arriving — can still admit it, at which point the Shot is re-offered
        // and adopted under 8.2k.  Declining first and adopting after would put
        // a `shot_disposition` on the wire for a swing this host then records
        // (CR-03 round-1 PinPointStudio review, R-5).  So the verdict waits this
        // long, measured from the first pump() after the Shot arrived.
        //
        // 0 derives it from the Session: `issue_hold_ns + heartbeat_interval`,
        // the same span 8.2i gives the device before it may mint.  A detector
        // of ours that has not fired within that span of the device's mint —
        // itself that span after the strike — is not going to.
        std::int64_t declineHoldNs = 0;
    };

    // ── 8.2d as a CORROBORATION policy ─────────────────────────────────────
    //
    // Does this host have evidence of its own for an event at `atRefNs`, a
    // reading of `Session.timebase_ref`?  True admits the Candidate; false
    // excludes it under 8.2d's third case, which takes it out of arbitration —
    // and since arbitration is what issues, a device detection this host cannot
    // corroborate produces NO SHOT rather than a Shot the host then declines to
    // record.  The device's own 8.2i deadline then mints on its own authority,
    // which is the honest ending to "it saw the strike and we did not".
    //
    // ⚠ IT IS ASKED ONLY ABOUT A FOREIGN CANDIDATE.  This host's own Candidate
    // IS the corroboration, and putting it to the same test would be asking the
    // witness to corroborate itself.
    //
    // ⚠ AND THE ORDER OF ARRIVAL DOES NOT DECIDE THE OUTCOME, THOUGH WE SPENT
    // A BUILD BELIEVING IT DID.  The worry was that a device Candidate beating
    // this host's own detector by a few milliseconds would be excluded and
    // never recoverable, since `ppcp_arbiter_reconsider()` (8.2d1 / E29) walks
    // only the RETAINED list.  It is recoverable, because a policy-excluded
    // Candidate that creates no group IS retained — libppcp: "An excluded
    // Candidate never CREATES a Shot ... It is retained, and a Shot issued near
    // it later will pick it up."  So the whole remedy is to call reconsider()
    // when this host's own detector fires, which `ShotController` does.  A
    // deferral queue built here first was deleted once a test asked the library
    // instead of the specification.
    using CorroborationFn = std::function<bool(std::int64_t atRefNs)>;

    // 8.3e — Shot and Candidate ids SHOULD be UUIDs and a peer MUST NOT mint in
    // another peer's namespace.  The library has no random source (ground rule
    // 8) so the embedding supplies them, as it does for `link_id` and the RV
    // pairing nonces.  Returns false to refuse, which stops a Shot being issued
    // rather than issuing one with a made-up id.
    using IdFn = std::function<bool(std::string *out)>;

    // A Shot this host issued (8.2h) or adopted (5.13d).  Handed by const
    // reference into the engine's own storage: `t0` has no setter anywhere in
    // libppcp, which is I7 by surface as well as by behaviour.
    using ShotFn = std::function<void(const ppcp_shot &)>;

    // MSG 8.5 — this bridge decided not to keep a Shot (a device Shot it never
    // adopted).  The embedding owns the durable half — the owed-decline queue
    // that survives a dropped link (8.5i) — so the statement is handed out
    // rather than sent from here.  Unset, the bridge sends it on its own peer
    // with ppcp_peer_shot_disposition(), which is what a test wants.
    using DeclineFn = std::function<void(const std::string &shotId, const char *reason)>;

    // MSG 7.3 — a device asked this host for an interval it never nominated.
    // Not an error path: 8.4b makes the answer a Capture, possibly `absent`.
    using CaptureRequestFn = std::function<void(const ppcp_body_capture_request &,
                                                std::uint64_t inReplyTo)>;

    PpcpShotBridge();
    ~PpcpShotBridge();
    PpcpShotBridge(const PpcpShotBridge &) = delete;
    PpcpShotBridge &operator=(const PpcpShotBridge &) = delete;

    void attach(ppcp_peer *peer, const PpcpSourceDeclaration *declaration,
                const PpcpLiveSession *session);
    void detach();

    // Builds the arbiter.  I20: PPCP_ERR_INVALID unless the peer is `role:
    // host` AND declares Arbitrate — a non-host attempting to arbitrate is
    // refused by libppcp and this reports the refusal rather than falling back.
    bool start(const Config &cfg, IdFn idFn, std::string *err = nullptr);
    void stop();

    // ⚠ THE PREDICATE `ShotController` BRANCHES ON.  True once the arbiter
    // exists — that is, once a Session with a PPCP peer in it is running.  The
    // host's own `ShotArbiter` is then not consulted at all, for the reasons in
    // the header note above.
    bool active() const { return m_arbiter != nullptr; }

    void setShotCallback(ShotFn f) { m_onShot = std::move(f); }
    void setCaptureRequestCallback(CaptureRequestFn f) { m_onCaptureRequest = std::move(f); }
    // Null disables the corroboration policy; conversion-uncertainty exclusion
    // (the original 8.2d third case) is unaffected either way.
    void setCorroborationCallback(CorroborationFn f) { m_onCorroborate = std::move(f); }
    void setDeclineCallback(DeclineFn f) { m_onDecline = std::move(f); }

    // ── MSG 8.5 — the device Shots still waiting for a verdict ─────────────
    //
    // True while `shotId` is a device-minted Shot this host has neither adopted
    // nor declined yet.  Anything else that would decline it — a clip for it
    // arriving at the filer as `not_requested` — asks this first and leaves the
    // verdict here, because only the bridge knows whether reconsider() is
    // about to adopt it.
    bool isAwaitingVerdict(const std::string &shotId) const;
    // Whether this bridge declined `shotId` (8.5c: nothing un-declines it).
    bool hasDeclined(const std::string &shotId) const;
    // The link is going: no Candidate can arrive to adopt what is still
    // waiting, so the verdict is reached now.  Returns the ids, and the caller
    // records each as an OWED `not_corroborated` decline (8.5i: "at its next
    // connection with the owning peer where it declined while not connected").
    // Nothing is sent from here — the peer may already be unreachable.
    std::vector<std::string> abandonAwaiting();

    // ── Nomination (CORE 5.12, 8.1) ────────────────────────────────────────
    //
    // `sourceId` names a Source THIS HOST declared (I26 / 5.12a — libppcp
    // refuses anything else before a wire sees it, and so does the arbiter).
    // `rawHostNs` is the detector's estimate on `tb:host`, in nanoseconds, with
    // acoustic time of flight ALREADY APPLIED where there was any (8.1d);
    // `tof` then records the correction and its dispersion, and is null where
    // there was none.
    //
    // `exposureNs` is that frame's exposure, and is ignored for a Source whose
    // profile has no `format` — a microphone, an IMU — where 6.1d fixes
    // `convention: mid` and the canonical instant is the raw instant.
    //
    // EVERY nomination is emitted (7.1d, I8): one this host promotes, one it
    // does not, and one it later excludes.  Withholding a loser destroys the
    // only evidence that explains why detection fired.
    bool nominate(const std::string &sourceId, const char *basis, std::int64_t rawHostNs,
                  std::int64_t exposureNs, double confidence, const ppcp_estimate *tof,
                  std::string *outCandidateId = nullptr, std::string *err = nullptr);

    // Every event the peer raised.  `candidate`, `shot` and `capture_request`
    // are acted on; everything else is ignored, and nothing is consumed.
    //
    // ⚠ AN IMPORTED FRAME NEVER REACHES THE ARBITER (erratum E28, F-S5-3).
    // `ppcp_event::imported` is true for a frame belonging to a Session
    // REPLAYED onto this link under MSG §9.1 — a device offering a stored
    // Session while a live one is running.  Those Candidates were nominated in
    // another Session, against another `timebase_ref`, possibly days ago.
    // Feeding them here arbitrates two Sessions as one, and because the
    // instants are numerically plausible nothing is malformed and nothing goes
    // red: 8.2 groups them by coincidence with live Candidates and issues Shots
    // that never happened.  That is what E28 was raised for.
    void observe(const ppcp_event &ev);

    // 8.2d1 (erratum E29, F-S5-1) — RECONSIDER what was retained for want of a
    // relation, now that one has arrived.  Returns how many were re-admitted.
    //
    // ⚠ THE ARBITER CANNOT CALL THIS FOR ITSELF and the failure is silent.  A
    // Candidate nominated before §6.3's sync burst converged has no relation
    // into `timebase_ref`, so 8.2d retains it — correctly — and 8.2d said
    // nothing about the relation arriving a moment later, which on a live link
    // it always does.  Under the reading this class had, that Candidate stayed
    // retained for the whole Session: no Shot, no error, and every Candidate
    // present exactly as 8.2d requires.  Called from observe() on a
    // `relation_update` and from PpcpHostPeer whenever this host's own
    // estimator publishes one (6.1f).
    std::size_t reconsider();

    // 8.2h — issues every group whose earliest contributing Candidate is at
    // least `issue_hold_ns` old.  `nowRefNs` is a reading of
    // `Session.timebase_ref`, which for this host is hostNowNs().  Returns how
    // many Shots were issued on this call.
    std::size_t pump(std::int64_t nowRefNs);

    // ── CORE §8.4 — an orphan capture request ──────────────────────────────
    //
    // MSG 7.3 — asks an owner for an interval around a `t0` it never nominated,
    // which is the case a host arbitrating across peers creates constantly: one
    // device hears the shot, another device's camera holds the frames.
    // Conferred by Arbitrate (I20).
    bool requestCapture(const std::string &shotId, std::int64_t t0RefNs,
                        const std::vector<std::string> &streamIds,
                        std::int64_t preNs, std::int64_t postNs,
                        std::string *err = nullptr);

    // ── CORE 8.1 / 8.5f — the launch monitor row ───────────────────────────
    //
    // A live ASSOCIATION, never a nomination: `basis: arrival_pairing`,
    // `confirmed: true`, `confirmed_by: observer` — exact by construction,
    // because this host armed the slot when it detected the swing and the next
    // reading to arrive claimed it.  5.16f permits `observer` here precisely
    // because `arrival_pairing` is not retrospective.
    //
    // `foreignSystem` is reverse-DNS (5.16, 10.2a).  `foreignShotId` is
    // whatever the device called it — the GCQuad's own shot counter, which is
    // unrelated to anything of ours and is carried rather than interpreted.
    bool linkForeignShot(const std::string &localShotId, const std::string &foreignShotId,
                         const std::string &foreignSystem, double confidence,
                         std::string *err = nullptr);

    struct Stats {
        std::size_t nominated       = 0;   // this host's own, emitted (7.1d)
        std::size_t observedForeign = 0;   // Candidates that arrived from a peer
        std::size_t excluded        = 0;   // 8.2d — a conclusion, not a discard
        std::size_t issued          = 0;
        std::size_t late            = 0;   // 8.2h — this host is running slow
        // ⚠ ADOPTED MEANS ADOPTED.  This counted every PPCP_OK from
        // ppcp_arbiter_observe_shot(), which answers OK whether or not it took
        // the Shot — so a device Shot this host ignored read as one it had
        // attached to (CR-03 round-1 review).  Now: 8.2k only, including a
        // Shot re-offered after reconsider() and adopted then.
        std::size_t adopted         = 0;   // 8.2k — the device's Shot is the one
        std::size_t extended        = 0;   // 5.13d — our own Shot, extended
        std::size_t linkedShots     = 0;   // 8.2l — both issued; `shot_link` sent
        // A device Shot matching no group of ours when it arrived: held for the
        // reconsider window, then adopted or declined.
        std::size_t notAdopted      = 0;
        // MSG 8.5 — declined `not_corroborated` once the window closed.
        std::size_t declined        = 0;
        std::size_t captureRequests = 0;
        std::size_t shotLinks       = 0;
        std::size_t nominationsRefused = 0;  // I26 — a Source we do not own
        // E28 — frames of a REPLAYED Session that were kept away from the live
        // arbiter.  Counted rather than dropped quietly: a number here is the
        // difference between "no Session was offered" and "one was, and it was
        // routed correctly".
        std::size_t importedIgnored = 0;
        // E29 — Candidates re-admitted to arbitration when a relation arrived.
        std::size_t reconsidered = 0;
        // ⚠ EVENTS THAT REACHED observe() WITH NO ARBITER, AND THE REASON THIS
        // COUNTER EXISTS.  Until the application started the bridge, a phone's
        // `candidate` or `shot` was decoded, validated, queued, dequeued and
        // dropped at the first line of observe() — no counter moved, nothing
        // was logged, and a phone that nominated was indistinguishable from one
        // that never did.  A number here is the difference between "no device
        // nominated" and "one did and we were not listening", and the second is
        // the failure that took a day to see.
        std::size_t unarbitrated = 0;
        // The corroboration policy's own number, kept apart from `excluded`
        // (conversion uncertainty) because they answer different questions: one
        // says "too uncertain to place", the other "we saw nothing".
        std::size_t uncorroborated      = 0;
    };
    const Stats &stats() const;

    // 8.2d / 8.2i1 — Candidates held with no Shot: a missing or `unrelated`
    // relation leaves not even an instant to group by.  The honest answer for
    // a peer declaring `unrelated` timebases is that EVERY one of its
    // candidates lands here (CONF §5).
    std::size_t retainedCount() const;
    std::size_t groupCount() const;
    // How many Shot ids the report-once memory is holding.  For the test that
    // asserts it is bounded; nothing else has a reason to read it.
    std::size_t reportedCount() const { return m_reported.size(); }

private:
    static ppcp_result idTrampoline(void *ctx, ppcp_id *out);
    static bool policyTrampoline(void *ctx, const ppcp_candidate *c,
                                 const ppcp_timebase_relation *rel, double sigmaNs);

    const ppcp_capture_profile *profileForSource(const ppcp_source *s) const;
    const ppcp_source *ownSource(const std::string &sourceId) const;
    void collectIssued();

    // What observe_shot() made of a Shot, read back off the arbiter — libppcp
    // answers PPCP_OK for all four and says nothing else.
    enum class ShotFate { Adopted, Extended, Linked, Unmatched };
    ShotFate offerShot(const ppcp_shot &s);
    bool isForeignDeviceShot(const ppcp_shot &s) const;
    // Re-offers every Shot awaiting a verdict — after reconsider(), and on
    // every pump() — and declines the ones whose window has closed.
    void settleAwaiting(std::int64_t nowRefNs, bool haveNow);
    std::int64_t declineHoldNs() const;
    void decline(const std::string &shotId, const char *reason);

    ppcp_peer                   *m_peer = nullptr;
    const PpcpSourceDeclaration *m_declaration = nullptr;
    const PpcpLiveSession       *m_session = nullptr;
    Config                       m_cfg;
    IdFn                         m_idFn;
    ShotFn                       m_onShot;
    CaptureRequestFn             m_onCaptureRequest;
    CorroborationFn              m_onCorroborate;
    DeclineFn                    m_onDecline;

    // Set for the duration of one ppcp_arbiter_observe() call on a foreign
    // Candidate, so the policy trampoline — which libppcp calls back into with
    // no context of its own beyond the Candidate — knows the corroboration
    // question applies at all.
    bool                         m_judgingForeign = false;

    bool isOwnCandidate(const ppcp_candidate &c) const;

    std::vector<std::uint8_t>    m_storage;
    ppcp_arbiter                *m_arbiter = nullptr;
    Stats                        m_stats;
    // Shot ids already handed to the embedding, so a Shot is reported once even
    // though its group stays in the arbiter for later attachment (8.2e).
    //
    // ⚠ BOUNDED BY TIME, AND IT USED NOT TO BE.  This was a bare id list cleared
    // only by start()/stop(), so it grew by one per Shot for the whole Session
    // and every collectIssued() walked all of it.  Each id now carries its
    // Shot's `t0`, and one past reportedHorizonNs() is forgotten — see
    // collectIssued() for why forgetting cannot re-report.
    struct Reported {
        std::string  id;
        std::int64_t t0Ns = 0;   // `Session.timebase_ref`, as the Shot carries it
    };
    std::vector<Reported>        m_reported;
    // The latest `nowRefNs` pump() was given.  collectIssued() is also reached
    // from observe() and reconsider(), which have no clock; they trim against
    // this.  Kept as a MAXIMUM, because the forget/skip rule in collectIssued()
    // is only safe while the clock it reads never goes backwards.
    std::int64_t                 m_lastNowRefNs = 0;
    bool                         m_haveNow = false;
    // libppcp's reclaim horizon for an issued group (hold + window + margin +
    // 120 s), recomputed here rather than read from the library.
    std::int64_t reportedHorizonNs() const;

    // ── MSG 8.5 / 8.5c — device Shots awaiting a verdict ───────────────────
    //
    // The whole Shot is kept, because re-offering it to the arbiter is how it
    // gets adopted once reconsider() has admitted its Candidate.  `deadlineNs`
    // is -1 until the first pump() after arrival stamps it: observe() has no
    // clock of its own, and the window is a span of host time, not of `t0`.
    struct Awaiting {
        ppcp_shot    shot{};
        std::int64_t deadlineNs = -1;
    };
    std::vector<Awaiting>        m_awaiting;
    // Bounded like the arbiter's own group table: sixteen swings awaiting a
    // verdict at once is sixteen swings inside two seconds.
    static constexpr std::size_t kMaxAwaiting = PPCP_ARBITER_MAX_GROUPS;
    // Shots this bridge declined.  8.5c: a later Candidate attaching to one
    // does not revoke it, so a declined Shot is never re-offered or reported.
    std::vector<std::string>     m_declinedHere;
    static constexpr std::size_t kMaxDeclinedHere = 64;
};

}  // namespace Ppcp
