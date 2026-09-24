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

#include "ppcp_shot_bridge.h"

#include <algorithm>
#include <cstring>

namespace Ppcp {
namespace {

std::string idStr(const ppcp_id &id) { return std::string(id.v, id.len); }

}  // namespace

PpcpShotBridge::PpcpShotBridge() = default;
PpcpShotBridge::~PpcpShotBridge() = default;

void PpcpShotBridge::attach(ppcp_peer *peer, const PpcpSourceDeclaration *declaration,
                            const PpcpLiveSession *session)
{
    stop();
    m_peer = peer;
    m_declaration = declaration;
    m_session = session;
}

void PpcpShotBridge::detach()
{
    stop();
    m_peer = nullptr;
    m_declaration = nullptr;
    m_session = nullptr;
}

ppcp_result PpcpShotBridge::idTrampoline(void *ctx, ppcp_id *out)
{
    PpcpShotBridge *self = static_cast<PpcpShotBridge *>(ctx);
    if (!self || !out || !self->m_idFn) return PPCP_ERR_INVALID;
    std::string s;
    if (!self->m_idFn(&s) || s.empty()) return PPCP_ERR_INVALID;
    return ppcp_id_set(out, s.c_str(), s.size());
}

bool PpcpShotBridge::policyTrampoline(void *ctx, const ppcp_candidate *c,
                                      const ppcp_timebase_relation *rel, double sigmaNs)
{
    // 8.2d — exclude-and-retain, and this is the ONLY thing in this application
    // that decides "too uncertain".  It is called only where a relation EXISTS:
    // a missing or `unrelated` one is decided by the specification, not by
    // policy, and libppcp never asks about those.
    PpcpShotBridge *self = static_cast<PpcpShotBridge *>(ctx);
    (void)rel;
    if (!self) return true;
    if (sigmaNs > self->m_cfg.maxConversionSigmaNs) {
        ++self->m_stats.excluded;
        return false;   // excluded from setting `t0`; STILL in Shot.candidates (I8)
    }

    // ── 8.2d, second application: corroboration ────────────────────────────
    //
    // Still "this Candidate may not set `t0`", and still exclude-and-retain —
    // but the question is now whether this HOST saw anything, not how uncertain
    // the conversion was.  A group in which every Candidate is excluded issues
    // no Shot at all (libppcp: "every Candidate excluded: no Shot (8.2d)"),
    // which is exactly the outcome wanted: a device detection this host cannot
    // corroborate produces silence rather than a Shot the host then declines to
    // record.  The device's own 8.2i deadline then mints on its own authority,
    // which is the honest ending — it saw the strike and we did not.
    if (self->m_judgingForeign && self->m_onCorroborate && c) {
        std::int64_t atRefNs = 0;
        const ppcp_id *ref = ppcp_peer_timebase_ref(self->m_peer);
        ppcp_instant   at_ref{};
        if (ref
            && ppcp_relations_convert(ppcp_peer_relations(self->m_peer), &c->at, ref,
                                      &at_ref) == PPCP_OK)
            atRefNs = at_ref.ns;
        else
            return true;   // no relation: 8.2d's FIRST case decides, not policy

        if (!self->m_onCorroborate(atRefNs)) {
            ++self->m_stats.uncorroborated;
            return false;
        }
    }
    return true;
}

bool PpcpShotBridge::isOwnCandidate(const ppcp_candidate &c) const
{
    // 5.1a — a Candidate carries the id of the peer that nominated it.  Read
    // off the live peer rather than off `Config::peerId`, so there is one
    // answer and it cannot drift from what the engine actually declares.
    const ppcp_id *self = m_peer ? ppcp_peer_id(m_peer) : nullptr;
    if (!self) return false;
    return idStr(*self) == idStr(c.peer_id);
}

bool PpcpShotBridge::start(const Config &cfg, IdFn idFn, std::string *err)
{
    stop();
    if (!m_peer) { if (err) *err = "no peer attached"; return false; }
    if (!idFn) { if (err) *err = "no id source: 8.3e forbids the library minting one"; return false; }
    m_cfg = cfg;
    m_idFn = std::move(idFn);

    m_storage.assign(ppcp_arbiter_sizeof(), 0);
    ppcp_arbiter *a = nullptr;
    // I20 — refused for a peer that is not `role: host` or does not declare
    // Arbitrate.  Reported, never worked around: a host that arbitrated without
    // declaring it fails CONF §1d, and the failure would be silent on the wire.
    const ppcp_result r = ppcp_arbiter_new(m_storage.data(), m_storage.size(), m_peer,
                                           &PpcpShotBridge::idTrampoline, this, &a);
    if (r != PPCP_OK || !a) {
        if (err) *err = std::string("ppcp_arbiter_new: ") + ppcp_result_str(r);
        m_storage.clear();
        return false;
    }
    (void)ppcp_arbiter_set_policy(a, &PpcpShotBridge::policyTrampoline, this);
    m_arbiter = a;
    m_reported.clear();
    m_lastNowRefNs = 0;
    m_haveNow = false;
    m_awaiting.clear();
    m_declinedHere.clear();
    return true;
}

void PpcpShotBridge::stop()
{
    // ⚠ WHAT IS STILL AWAITING A VERDICT IS DROPPED HERE, NOT DECLINED.  The
    // embedding that wants it declined calls abandonAwaiting() first, while it
    // still knows which owner and Session to owe the statement to; stop() is
    // also reached from start() and detach(), where there is nobody to tell.
    m_arbiter = nullptr;
    m_storage.clear();
    m_reported.clear();
    m_lastNowRefNs = 0;
    m_haveNow = false;
    m_awaiting.clear();
}

const ppcp_source *PpcpShotBridge::ownSource(const std::string &sourceId) const
{
    if (!m_declaration) return nullptr;
    for (const ppcp_source &s : m_declaration->sources())
        if (idStr(s.id) == sourceId) return &s;
    return nullptr;
}

const ppcp_capture_profile *PpcpShotBridge::profileForSource(const ppcp_source *s) const
{
    // 5.12e / I33 — the profile supplies `timing.convention` and, for
    // `nominal_frame_start`, the offset.  A Source whose profile has no
    // `format` — a microphone, an IMU — takes NULL: 6.1d fixes `convention:
    // mid` there and the canonical instant is the raw instant, so passing a
    // profile that has no exposure to apply would be pretending there is one.
    if (!s || s->profile_count == 0) return nullptr;
    for (std::size_t i = 0; i < s->profile_count; ++i)
        if (s->profiles[i].format.present) return &s->profiles[i];
    return nullptr;
}

bool PpcpShotBridge::nominate(const std::string &sourceId, const char *basis,
                              std::int64_t rawHostNs, std::int64_t exposureNs,
                              double confidence, const ppcp_estimate *tof,
                              std::string *outCandidateId, std::string *err)
{
    if (!m_peer || !m_arbiter) { if (err) *err = "arbitration is not running"; return false; }

    const ppcp_source *src = ownSource(sourceId);
    if (!src) {
        // I26 / 5.12a / 7.1a — a Candidate names a Source THIS peer declared,
        // that Source names a Timebase it declared, and `at` is expressed in
        // that timebase.  Refused here as well as by libppcp, so the counter
        // records a detector wired to a Source the declaration never carried
        // instead of the failure arriving as a generic INVALID.
        ++m_stats.nominationsRefused;
        if (err) *err = "no declared Source named " + sourceId + " (I26)";
        return false;
    }

    std::string cid;
    if (!m_idFn || !m_idFn(&cid) || cid.empty()) {
        if (err) *err = "could not mint a Candidate id";
        return false;
    }

    // I33 / 5.12e — the canonical-instant conversion, applied ONCE, here, by
    // the nominator, because only the nominator holds the exposure and the
    // Source's `timing`.  8.2a then forbids the arbiter applying it again.
    ppcp_candidate c{};
    const ppcp_result mr = ppcp_candidate_make_canonical(
        &c, cid.c_str(), src, profileForSource(src), basis, rawHostNs,
        exposureNs, confidence, tof);
    if (mr != PPCP_OK) {
        if (err) *err = std::string("ppcp_candidate_make_canonical: ") + ppcp_result_str(mr);
        return false;
    }

    // 7.1d — EVERY nomination is emitted, before this host knows whether it
    // will win.  Emitting only winners destroys the evidence that explains why
    // detection fired, and CT-I8 asserts the loser survives.
    const ppcp_result nr = ppcp_peer_nominate(m_peer, &c);
    if (nr != PPCP_OK) {
        if (err) *err = std::string("ppcp_peer_nominate: ") + ppcp_result_str(nr);
        return false;
    }
    ++m_stats.nominated;

    // 8.2a — and the host arbitrates its OWN Candidates on the same terms as
    // anyone else's.  A host that grouped only foreign candidates would have a
    // per-peer slot, which is the same defect as a per-modality one.
    bool excluded = false;
    (void)ppcp_arbiter_observe(m_arbiter, &c, &excluded);

    if (outCandidateId) *outCandidateId = cid;
    return true;
}

void PpcpShotBridge::observe(const ppcp_event &ev)
{
    if (!m_arbiter || !ev.msg) {
        // Count only what arbitration would have acted on.  Every other event
        // kind reaches here in the ordinary course of a Session and saying so
        // would bury the one case worth seeing.
        if (!m_arbiter && ev.msg
            && (ev.kind == PPCP_EVENT_CANDIDATE || ev.kind == PPCP_EVENT_SHOT
                || ev.kind == PPCP_EVENT_CAPTURE_REQUEST))
            ++m_stats.unarbitrated;
        return;
    }

    // ⚠ E28 / F-S5-3 — BEFORE THE SWITCH, AND BEFORE collectIssued().  A frame
    // of a replayed Session is scoped to that Session by the envelope's
    // `session_id` and belongs to no part of the live arbitration: not its
    // Candidates, not its Shots, not its `capture_request`s.  Returning here
    // is the whole guard, and the reason it is one line at the top rather than
    // a case-by-case test is that the next message type added to this switch
    // would otherwise inherit the defect.
    if (ev.imported) {
        ++m_stats.importedIgnored;
        return;
    }

    switch (ev.kind) {
    case PPCP_EVENT_CANDIDATE: {
        const ppcp_candidate &c = ev.msg->body.candidate.candidate;
        // The corroboration policy applies to a Candidate this host did not
        // nominate, and to no other.  Excluded here is not excluded for ever:
        // a Candidate that creates no group is RETAINED, and reconsider() —
        // which `ShotController` calls the moment one of this host's own
        // detectors fires — puts it back to the same question.
        bool excluded = false;
        m_judgingForeign = !isOwnCandidate(c);
        if (ppcp_arbiter_observe(m_arbiter, &c, &excluded) == PPCP_OK)
            ++m_stats.observedForeign;
        m_judgingForeign = false;
        break;
    }
    case PPCP_EVENT_SHOT: {
        // 8.2k — a DEVICE-minted Shot referencing a Candidate this host still
        // holds is NOT competed with: the host attaches its own Candidates to
        // it and issues nothing of its own (I35).  8.2l — where both issued,
        // neither is withdrawn and the host emits `shot_link` with `basis:
        // shared_candidate`.  Both are libppcp's, and both are why this arm
        // hands the Shot straight over rather than deciding anything.
        //
        // ⚠ WHAT IT DOES DECIDE, SINCE CR-03, IS WHETHER ANYTHING CAME OF IT.
        // A device Shot that shares no Candidate with any group here — the
        // common case is 8.2i minting after our corroboration policy excluded
        // its Candidate — is one this host is not going to record.  Until MSG
        // 8.5 there was nothing to say, and the phone held the clip for the
        // life of the link.  It is held here for the reconsider window and
        // then DECLINED `not_corroborated` (settleAwaiting()).
        const ppcp_shot &shot = ev.msg->body.shot.shot;
        const std::string id = idStr(shot.id);
        // 8.5c — a Shot already declined stays declined; a re-sent `shot`
        // extending it is attachment, and "attaching a Candidate does not
        // revoke a decline".
        if (hasDeclined(id)) break;
        const ShotFate fate = offerShot(shot);
        if (fate == ShotFate::Unmatched && isForeignDeviceShot(shot)) {
            const bool known = std::any_of(m_awaiting.begin(), m_awaiting.end(),
                                           [&id](const Awaiting &a) {
                                               return idStr(a.shot.id) == id;
                                           });
            if (known) {
                // The device re-sent it extended (5.13d); keep the newer list
                // so the re-offer sees every Candidate it now names.
                for (Awaiting &a : m_awaiting)
                    if (idStr(a.shot.id) == id) a.shot = shot;
            } else {
                ++m_stats.notAdopted;
                if (m_awaiting.size() >= kMaxAwaiting) {
                    // ⚠ Full: the OLDEST gets its verdict now rather than being
                    // dropped silently.  It has waited longest, and silence was
                    // the whole defect MSG 8.5 exists to end.
                    const std::string oldest = idStr(m_awaiting.front().shot.id);
                    m_awaiting.erase(m_awaiting.begin());
                    decline(oldest, "not_corroborated");
                }
                m_awaiting.push_back(Awaiting{ shot, -1 });
            }
        }
        break;
    }
    // 8.2d1 (erratum E29) — a relation arrived, so what was retained for want
    // of one is reconsidered.  The engine has already folded the update into
    // ppcp_peer_relations() by the time this event is raised.
    case PPCP_EVENT_RELATION_UPDATE:
        m_stats.reconsidered += ppcp_arbiter_reconsider(m_arbiter);
        // …and a device Shot waiting on that Candidate may now be adoptable.
        settleAwaiting(0, /*haveNow=*/false);
        break;
    case PPCP_EVENT_CAPTURE_REQUEST:
        // 8.4b — answered with a Capture, possibly `absent` with
        // `absent_reason: outside_buffer`, and NEVER with an `error`: an absent
        // capture is a result, not a failure (I10).
        ++m_stats.captureRequests;
        if (m_onCaptureRequest)
            m_onCaptureRequest(ev.msg->body.capture_request, ev.msg->env.msg_id);
        break;
    default:
        break;
    }
    collectIssued();
}

std::size_t PpcpShotBridge::reconsider()
{
    if (!m_arbiter) return 0;
    const std::size_t n = ppcp_arbiter_reconsider(m_arbiter);
    m_stats.reconsidered += n;
    // ⭐ 8.2k, LATE — THE REASON A DEVICE SHOT IS HELD RATHER THAN DECLINED ON
    // ARRIVAL.  A re-admitted Candidate forms (or joins) a group; a device Shot
    // naming it, re-offered now, finds that group unissued and is ADOPTED — the
    // device's Shot is the one that exists, and this host records it.
    if (n) settleAwaiting(0, /*haveNow=*/false);
    // A re-admitted Candidate may complete a group that is already past its
    // issue hold, so the Shot it now belongs to can be reported on this call
    // rather than waiting for the next pump.
    if (n) collectIssued();
    return n;
}

std::size_t PpcpShotBridge::pump(std::int64_t nowRefNs)
{
    if (!m_arbiter) return 0;
    std::size_t issued = 0;
    if (!m_haveNow || nowRefNs > m_lastNowRefNs) m_lastNowRefNs = nowRefNs;
    m_haveNow = true;
    (void)ppcp_arbiter_pump(m_arbiter, nowRefNs, &issued);
    m_stats.issued = ppcp_arbiter_issued_count(m_arbiter);
    // 8.2h — a group issued after the mint deadline overlaps the window in
    // which the nominating peer is entitled to mint, and produces two Shots for
    // one event with no defect on either side.  Counted, because it is how a
    // host finds out it is running slow rather than finding out from a user.
    m_stats.late = ppcp_arbiter_late_count(m_arbiter);
    // After issuing, so a group of ours issued on this very pump near a
    // waiting device Shot is seen as the 8.2l crossing it is, and linked.
    settleAwaiting(nowRefNs, /*haveNow=*/true);
    collectIssued();
    return issued;
}

// ── MSG 8.5 — device Shots, and the verdict on the ones we never adopted ────

bool PpcpShotBridge::isForeignDeviceShot(const ppcp_shot &s) const
{
    // Only a DEVICE's own mint is this host's to decline here.  A Shot this
    // host issued comes back only as a 5.13d extension, and is adopted.
    if (s.authority != PPCP_AUTHORITY_DEVICE) return false;
    const ppcp_id *self = m_peer ? ppcp_peer_id(m_peer) : nullptr;
    return !(self && idStr(*self) == idStr(s.issued_by));
}

PpcpShotBridge::ShotFate PpcpShotBridge::offerShot(const ppcp_shot &s)
{
    // ⚠ libppcp ANSWERS PPCP_OK WHETHER OR NOT IT TOOK THE SHOT, and exposes
    // nothing else, so the fate is read back off the arbiter's groups — without
    // changing libppcp, which is its own team's (PPCP spec precedes
    // implementation).  The three outcomes are exactly observe_shot()'s three
    // arms: an issued group carrying THIS id (5.13d, or 8.2k just now), an
    // issued group of OURS sharing a Candidate with it (8.2l), or neither.
    const std::string id = idStr(s.id);
    const auto groupWithId = [this, &id]() {
        for (std::size_t i = 0; i < PPCP_ARBITER_MAX_GROUPS; ++i) {
            const ppcp_shot *g = ppcp_arbiter_shot_at(m_arbiter, i);
            if (g && idStr(g->id) == id) return true;
        }
        return false;
    };
    const bool hadIt = groupWithId();
    if (ppcp_arbiter_observe_shot(m_arbiter, &s) != PPCP_OK) return ShotFate::Unmatched;
    if (groupWithId()) {
        if (hadIt) { ++m_stats.extended; return ShotFate::Extended; }
        ++m_stats.adopted;
        return ShotFate::Adopted;
    }
    for (std::size_t i = 0; i < PPCP_ARBITER_MAX_GROUPS; ++i) {
        const ppcp_shot *g = ppcp_arbiter_shot_at(m_arbiter, i);
        if (!g) continue;
        for (std::size_t j = 0; j < g->candidate_count; ++j)
            for (std::size_t k = 0; k < s.candidate_count; ++k)
                if (ppcp_id_equal(&g->candidates[j], &s.candidates[k])) {
                    ++m_stats.linkedShots;
                    return ShotFate::Linked;
                }
    }
    return ShotFate::Unmatched;
}

std::int64_t PpcpShotBridge::declineHoldNs() const
{
    if (m_cfg.declineHoldNs > 0) return m_cfg.declineHoldNs;
    if (m_session)
        return m_session->config().issueHoldNs
               + static_cast<std::int64_t>(m_session->config().heartbeatIntervalMs) * 1000000;
    return PPCP_DEFAULT_ISSUE_HOLD_NS
           + static_cast<std::int64_t>(PPCP_DEFAULT_HEARTBEAT_MS) * 1000000;
}

void PpcpShotBridge::settleAwaiting(std::int64_t nowRefNs, bool haveNow)
{
    if (!m_arbiter || m_awaiting.empty()) return;
    for (auto it = m_awaiting.begin(); it != m_awaiting.end();) {
        // Re-offered every time: a Shot that shares no group is a no-op in the
        // arbiter, and one that now does is adopted (8.2k) or linked (8.2l).
        const ShotFate fate = offerShot(it->shot);
        if (fate != ShotFate::Unmatched) {
            // 8.2l — linked, not declined: this host issued its own Shot for
            // the same swing and keeps THAT one.  A clip anchored to the
            // device's Shot reaches the filer as one nobody asked for, and is
            // declined there, `not_requested` — which 8.5a's "a decline of one
            // releases nothing anchored to the other" makes exactly right.
            it = m_awaiting.erase(it);
            continue;
        }
        if (!haveNow) { ++it; continue; }
        if (it->deadlineNs < 0) {
            it->deadlineNs = nowRefNs + declineHoldNs();
            ++it;
            continue;
        }
        if (nowRefNs < it->deadlineNs) { ++it; continue; }
        // ⭐ THE WINDOW HAS CLOSED AND NOTHING HERE AGREED.  Said once, with the
        // reason a person can be shown (8.5's table): "No detector at the
        // receiver agreed that the Shot happened".
        const std::string id = idStr(it->shot.id);
        it = m_awaiting.erase(it);
        decline(id, "not_corroborated");
    }
}

void PpcpShotBridge::decline(const std::string &shotId, const char *reason)
{
    if (shotId.empty() || hasDeclined(shotId)) return;
    m_declinedHere.push_back(shotId);
    if (m_declinedHere.size() > kMaxDeclinedHere) m_declinedHere.erase(m_declinedHere.begin());
    ++m_stats.declined;
    if (m_onDecline) {
        m_onDecline(shotId, reason);
        return;
    }
    // No embedding to owe it through: said directly, which is best effort and
    // is what a bridge under test without a host service wants.
    if (m_peer)
        (void)ppcp_peer_shot_disposition(m_peer, shotId.c_str(), PPCP_DISPOSITION_DECLINED,
                                         reason);
}

bool PpcpShotBridge::isAwaitingVerdict(const std::string &shotId) const
{
    return std::any_of(m_awaiting.begin(), m_awaiting.end(),
                       [&shotId](const Awaiting &a) { return idStr(a.shot.id) == shotId; });
}

bool PpcpShotBridge::hasDeclined(const std::string &shotId) const
{
    return std::find(m_declinedHere.begin(), m_declinedHere.end(), shotId)
           != m_declinedHere.end();
}

std::vector<std::string> PpcpShotBridge::abandonAwaiting()
{
    std::vector<std::string> ids;
    for (const Awaiting &a : m_awaiting) {
        const std::string id = idStr(a.shot.id);
        ids.push_back(id);
        m_declinedHere.push_back(id);
        ++m_stats.declined;
    }
    while (m_declinedHere.size() > kMaxDeclinedHere) m_declinedHere.erase(m_declinedHere.begin());
    m_awaiting.clear();
    return ids;
}

// ⚠ THE LIBRARY'S NUMBER, RESTATED AND NOT INCLUDED.  libppcp is about to
// reclaim an issued arbiter group once `now - t0` exceeds issue hold +
// coincidence window + heartbeat margin + PPCP_ARBITER_RECLAIM_HORIZON_NS
// (120 s).  That constant is not in the headers this builds against yet, so it
// is a local one; when it lands, this should become the library's own.  The
// rule below is safe whatever the library does — it does not depend on a slot
// actually being freed — so a disagreement here costs memory, never a
// duplicate.
namespace {
constexpr std::int64_t kReclaimHorizonNs = 120LL * 1000000000LL;   // PPCP_ARBITER_RECLAIM_HORIZON_NS
}  // namespace

std::int64_t PpcpShotBridge::reportedHorizonNs() const
{
    // The session parameters the ARBITER reads (arb_hold / arb_window /
    // arb_margin in libppcp), from the same peer, with the same defaults — not
    // PpcpLiveSession's config, which is what was asked for rather than what
    // the Session opened with.
    const ppcp_body_session_open *sp = m_peer ? ppcp_peer_session_params(m_peer) : nullptr;
    const bool arb = sp && sp->has_arbitration;
    const std::int64_t hold   = arb ? sp->issue_hold_ns : PPCP_DEFAULT_ISSUE_HOLD_NS;
    const std::int64_t window = arb ? sp->coincidence_window_ns
                                    : PPCP_DEFAULT_COINCIDENCE_WINDOW_NS;
    const std::uint32_t hbMs  = (sp && sp->has_heartbeat_interval) ? sp->heartbeat_interval_ms
                                                                   : PPCP_DEFAULT_HEARTBEAT_MS;
    return hold + window + static_cast<std::int64_t>(hbMs) * 1000000 + kReclaimHorizonNs;
}

void PpcpShotBridge::collectIssued()
{
    if (!m_arbiter || !m_onShot) return;

    // ── The report-once memory, bounded ────────────────────────────────────
    //
    // ⛔ ONE PREDICATE DECIDES BOTH HALVES, AND THAT IS THE WHOLE SAFETY
    // ARGUMENT.  An id is forgotten when its `t0` is past the horizon, and a
    // Shot in the arbiter whose `t0` is past the horizon is never reported —
    // so a forgotten id that is still sitting in a slot (libppcp reclaims only
    // when the table is FULL, so an old group can outlive the horizon by a
    // whole Session) is skipped by the second half instead of being handed on
    // again.  `m_lastNowRefNs` only grows, so once past, always past.
    //
    // ⚠ WHAT THE SKIP COSTS.  A Shot first seen here more than ~2 minutes after
    // its `t0` is not reported at all.  Every path into a slot — issue after
    // the hold, 8.2k adoption inside the decline window — lands within seconds,
    // and the event buffer holds seconds: a Shot that old has no swing left to
    // record.  Such a group is also exactly what libppcp treats as reclaimable.
    const std::int64_t horizon = reportedHorizonNs();
    const auto pastHorizon = [&](std::int64_t t0Ns) {
        return m_haveNow && m_lastNowRefNs - t0Ns > horizon;
    };
    if (m_haveNow)
        m_reported.erase(std::remove_if(m_reported.begin(), m_reported.end(),
                                        [&](const Reported &r) { return pastHorizon(r.t0Ns); }),
                         m_reported.end());

    // Every SLOT, not group_count() of them.  ppcp_arbiter_shot_at() takes a
    // slot index (0..PPCP_ARBITER_MAX_GROUPS-1) and answers NULL for a free
    // one, while group_count() counts slots in use.  libppcp never frees a
    // group today, so the in-use slots are a prefix and the two bounds agree;
    // walking every slot stops that being an assumption this loop depends on.
    // offerShot() reads the arbiter back the same way.
    for (std::size_t i = 0; i < PPCP_ARBITER_MAX_GROUPS; ++i) {
        const ppcp_shot *s = ppcp_arbiter_shot_at(m_arbiter, i);
        if (!s) continue;
        const std::string id = idStr(s->id);
        if (id.empty()) continue;
        if (pastHorizon(s->t0.ns)) continue;
        if (std::any_of(m_reported.begin(), m_reported.end(),
                        [&id](const Reported &r) { return r.id == id; }))
            continue;
        m_reported.push_back({id, s->t0.ns});
        // 8.5c — a Shot this bridge declined is never handed on to be recorded.
        if (hasDeclined(id)) continue;
        m_onShot(*s);
    }
}

bool PpcpShotBridge::requestCapture(const std::string &shotId, std::int64_t t0RefNs,
                                    const std::vector<std::string> &streamIds,
                                    std::int64_t preNs, std::int64_t postNs,
                                    std::string *err)
{
    if (!m_peer) { if (err) *err = "no peer attached"; return false; }
    if (streamIds.empty()) { if (err) *err = "capture_request names at least one Stream"; return false; }

    // 5.13c — `t0` is in `Session.timebase_ref`, which for this host is
    // `tb:host`.  The owner inverts §6.1's conversion at its end when it
    // expresses the interval in its own convention; this host does not, and
    // must not, do it for them.
    const std::string ref = m_session ? m_session->config().timebaseRef
                                      : std::string(kHostTimebaseId);
    ppcp_instant t0{};
    if (ppcp_instant_make(&t0, ref.c_str(), ref.size(), t0RefNs) != PPCP_OK) {
        if (err) *err = "t0 is not a valid Instant on " + ref;
        return false;
    }

    std::vector<ppcp_id> ids;
    ids.reserve(streamIds.size());
    for (const std::string &s : streamIds) {
        ppcp_id id{};
        if (ppcp_id_set(&id, s.c_str(), s.size()) != PPCP_OK) {
            if (err) *err = "stream id is not a valid Id: " + s;
            return false;
        }
        ids.push_back(id);
    }

    const ppcp_result r = ppcp_peer_capture_request(m_peer, shotId.c_str(), &t0,
                                                    ids.data(), ids.size(), preNs, postNs);
    if (r != PPCP_OK) {
        if (err) *err = std::string("ppcp_peer_capture_request: ") + ppcp_result_str(r);
        return false;
    }
    return true;
}

bool PpcpShotBridge::linkForeignShot(const std::string &localShotId,
                                     const std::string &foreignShotId,
                                     const std::string &foreignSystem, double confidence,
                                     std::string *err)
{
    if (!m_peer) { if (err) *err = "no peer attached"; return false; }
    if (!m_idFn) { if (err) *err = "no id source"; return false; }

    std::string lid;
    if (!m_idFn(&lid) || lid.empty()) { if (err) *err = "could not mint a ShotLink id"; return false; }

    ppcp_shot_link l{};
    // 8.1, middle column — a LIVE ASSOCIATION.  `arrival_pairing` is not one of
    // the three retrospective bases (5.16b/f), which is exactly why it may be
    // confirmed by the observer rather than needing a human.
    ppcp_result r = ppcp_shot_link_make(&l, lid.c_str(), localShotId.c_str(),
                                        foreignShotId.c_str(), PPCP_LINK_ARRIVAL_PAIRING,
                                        confidence);
    if (r != PPCP_OK) {
        if (err) *err = std::string("ppcp_shot_link_make: ") + ppcp_result_str(r);
        return false;
    }
    // 5.16e — there is no way to set `confirmed` without saying which kind it
    // was, and this is the kind: the host armed the slot when it detected the
    // swing and observed the reading arrive.
    r = ppcp_shot_link_confirm(&l, PPCP_CONFIRMED_BY_OBSERVER);
    if (r != PPCP_OK) {
        if (err) *err = std::string("ppcp_shot_link_confirm: ") + ppcp_result_str(r);
        return false;
    }
    if (!foreignSystem.empty()) {
        r = ppcp_shot_link_set_foreign_system(&l, foreignSystem.c_str());
        if (r != PPCP_OK) {
            if (err) *err = std::string("ppcp_shot_link_set_foreign_system: ")
                            + ppcp_result_str(r);
            return false;
        }
    }

    r = ppcp_peer_shot_link(m_peer, &l);
    if (r != PPCP_OK) {
        if (err) *err = std::string("ppcp_peer_shot_link: ") + ppcp_result_str(r);
        return false;
    }
    ++m_stats.shotLinks;
    return true;
}

const PpcpShotBridge::Stats &PpcpShotBridge::stats() const { return m_stats; }

std::size_t PpcpShotBridge::retainedCount() const
{
    return m_arbiter ? ppcp_arbiter_retained_count(m_arbiter) : 0;
}

std::size_t PpcpShotBridge::groupCount() const
{
    return m_arbiter ? ppcp_arbiter_group_count(m_arbiter) : 0;
}

}  // namespace Ppcp
