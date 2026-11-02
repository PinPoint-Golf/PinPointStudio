/*
 * Copyright (c) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#pragma once

#include "launch_monitor_reading.h"

#include <QString>

#include <optional>

namespace pinpoint::lm {

// Decides WHICH swing a launch monitor reading belongs to.
//
// The problem: LastShot.CSV is one row rewritten in place. It carries no timestamp
// we can trust, and its Shot ID is FSX2020's own counter with no relationship to
// ours. Nothing in the file says which of our swings it describes.
//
// The rule, which is a policy choice and not a deduction:
//
//     shot detected ──► arm for this swing (displacing whatever was armed)
//     reading arrives ─► armed?  yes ─► it is this swing's; disarm
//                                no  ─► discard it
//
// A swing waits INDEFINITELY for its row — there is no timeout. What ends the wait
// is the next shot, because once another ball has been struck any subsequent write
// describes that one. So a swing whose row never arrives simply has no launch
// monitor data, which is the honest outcome; guessing would be worse.
//
// The second problem is that the row usually arrives before there is anywhere to
// put it. ShotProcessor writes swing.json only after both its workers finish, and
// analysis alone runs 12-37 s, so the reading is normally in hand long before a
// swingDir exists. A claimed reading with no destination is therefore PARKED and
// flushed by noteSwingDir(). It can equally arrive after — during first replay —
// in which case the destination is already known and it is claimed outright.
//
// WHY A SINGLE SLOT IS ENOUGH. ShotProcessor handles exactly one shot at a time,
// and ShotController::armed requires the processor to be idle, so no new shot can
// be detected while one is processing or replaying. Every shotProcessed therefore
// refers to the most recent shotDetected, and there can never be two swings
// waiting at once. If that invariant is ever relaxed, this class needs a map and
// this comment needs deleting.
//
// WHEN THE DEVICE STATES A CONTACT TIME, ORDER IS CHECKED AGAINST IT. Foresight Premier stamps
// every shot (LaunchMonitorReading::contactAtMs) and also SILENTLY DROPS a shot hit before it is
// ready (docs/design/foresight_premier_connector.md §2.6.3). Order alone then goes wrong without a
// symptom: the swing whose shot Premier dropped stays armed, and if we in turn miss Premier's NEXT
// shot, that reading is claimed by the wrong swing. So when both our impact time (given to
// noteShotDetected) and the reading's contact time are known, a claim must agree to within
// kContactWindowMs:
//
//     |Δ| ≤ window ─► claim, as above
//     Δ  > window ─► the reading is for a LATER shot: our swing's reading is never coming
//                    (Premier dropped it). Disarm, and the reading is unpaired.
//     Δ  < −window ─► the reading is for an EARLIER shot we never saw. Discard it; stay armed.
//
// A reading with no contact time (FSX2020, GSPro) or a shot with no impact time is decided by
// order alone, exactly as before.
//
// Pure logic, no Qt objects and no I/O, so all of it is exercised in a unit test.
class ShotPairing
{
public:
    enum class Disposition {
        Claimed,    // belongs to `swingDir`, write it now
        Parked,     // belongs to the armed swing, held until noteSwingDir()
        Discarded,  // nothing was armed — not ours to attribute
    };

    // How far a stated contact time may sit from our impact and still be this swing's. A
    // starting value, not a measurement: both clocks are the same PC, and Premier's contact
    // estimate is ±0.05 s (§2.6.6), but our side is unmeasured. Shots in a rapid-fire run
    // were ~6 s apart, so this still tells neighbours apart. Every timed claim logs its Δ
    // (LaunchMonitorController) so the cabin test can size it.
    static constexpr qint64 kContactWindowMs = 1500;

    // Why a timed reading was refused. None for every order-only decision.
    enum class TimeCheck { None, Matched, BeforeImpact, AfterWindow };

    struct Offer {
        Disposition disposition = Disposition::Discarded;
        QString     swingDir;   // set only for Claimed
        TimeCheck   timeCheck = TimeCheck::None;
        std::optional<qint64> contactDeltaMs;   // reading contact − our impact, when both known
    };

    // A shot was committed. Arms for it, displacing any swing still waiting — that
    // swing gets no launch monitor data, deliberately. `impactUtcMs` is our impact on the
    // wall clock (epoch ms), when the caller knows it.
    void noteShotDetected(std::optional<qint64> impactUtcMs = std::nullopt)
    {
        m_armed = true;
        m_impactUtcMs = impactUtcMs;
        m_swingDir.clear();
        // A parked reading here means the previous swing never reached a swingDir
        // (its analysis or export failed). It has nowhere to go.
        m_parked.reset();
    }

    // A reading turned up. Returns what should happen to it.
    Offer offerReading(const LaunchMonitorReading &reading)
    {
        if (!m_armed)
            return {};                          // Discarded

        TimeCheck check = TimeCheck::None;
        std::optional<qint64> delta;
        if (m_impactUtcMs && reading.contactAtMs) {
            delta = *reading.contactAtMs - *m_impactUtcMs;
            if (*delta < -kContactWindowMs)
                return { Disposition::Discarded, QString(), TimeCheck::BeforeImpact, delta };
            if (*delta > kContactWindowMs) {
                m_armed = false;                // this swing's reading is never coming
                return { Disposition::Discarded, QString(), TimeCheck::AfterWindow, delta };
            }
            check = TimeCheck::Matched;
        }
        m_armed = false;

        if (!m_swingDir.isEmpty())
            return { Disposition::Claimed, m_swingDir, check, delta };

        m_parked = reading;
        return { Disposition::Parked, QString(), check, delta };
    }

    // A later, fuller statement of the reading already parked (the same deviceShotId —
    // Premier's club data, which lands a shot late). Replaces it in place so the swing gets
    // the whole reading when it reaches disk. False when nothing parked matches.
    bool replaceParked(const LaunchMonitorReading &reading)
    {
        if (!m_parked || m_parked->deviceShotId.isEmpty()
            || m_parked->deviceShotId != reading.deviceShotId)
            return false;
        m_parked = reading;
        return true;
    }

    // The armed swing reached disk. Returns a reading if one was parked for it.
    std::optional<LaunchMonitorReading> noteSwingDir(const QString &swingDir)
    {
        m_swingDir = swingDir;
        if (!m_parked)
            return std::nullopt;
        auto out = m_parked;
        m_parked.reset();
        return out;
    }

    // The shot failed before producing a swingDir. Anything held for it is dropped.
    void noteShotFailed()
    {
        m_armed = false;
        m_impactUtcMs.reset();
        m_parked.reset();
        m_swingDir.clear();
    }

    bool armed()      const { return m_armed; }
    bool hasParked()  const { return m_parked.has_value(); }

private:
    bool    m_armed = false;
    std::optional<qint64> m_impactUtcMs;           // our impact, wall clock, when known
    QString m_swingDir;                             // "" until shotProcessed
    std::optional<LaunchMonitorReading> m_parked;   // claimed, awaiting a destination
};

} // namespace pinpoint::lm
