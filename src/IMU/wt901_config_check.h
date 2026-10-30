// SPDX-License-Identifier: GPL-3.0-or-later
// ---------------------------------------------------------------------------
// wt901_config_check.h — did the Witmotion take the configuration we sent it?
// ---------------------------------------------------------------------------
//
// On every connect WT9011DCL_BLE::initializeDevice() writes ORIENT = 0x0001
// (vertical installation), AXIS6 = 0x0001 (6-axis fusion) and RRATE, runtime
// only, and reads the three registers back. Issue #105: on 27 Oct 2026 one
// WT901BLE67 came up with the ORIENT write not applied — standing upright it
// read gravity on +Z instead of +Y — and the read-back went only to the
// sensor's own diagnostic lines, so nothing noticed and the trunk routine
// waited forever on "isn't upright". A relaunch (a fresh connect, so the
// configuration sent again) fixed it at once.
//
// This is the decision half of the fix: one attempt = send the configuration,
// then collect the three read-backs. A wrong answer, or no answer within the
// caller's timeout, sends the configuration again, up to kMaxAttempts. Then:
//
//   Confirmed   every register read back as sent.
//   Failed      ORIENT or AXIS6 still reads back wrong. The sensor's axes (or
//               its fusion) are not the ones every solve assumes, so it is not
//               ready, and the golfer is told so rather than left waiting.
//   Unverified  only missing answers, or only RRATE wrong. Nothing shows the
//               frame is wrong, so the sensor stays usable and the log says
//               what could not be confirmed (the measured data rate is shown
//               separately, so a wrong RRATE is visible anyway).
//
// Pure and header-only — no Qt, no clock, no device — so the decision is
// testable without a sensor. The driver owns the writes, the timer and the
// logging.
#ifndef PINPOINT_WT901_CONFIG_CHECK_H
#define PINPOINT_WT901_CONFIG_CHECK_H

#include <cstdint>

namespace pinpoint::wt901 {

// The registers the check reads back (the WT901 register addresses).
inline constexpr uint8_t kRegRRate  = 0x03;
inline constexpr uint8_t kRegOrient = 0x23;
inline constexpr uint8_t kRegAxis6  = 0x24;

inline constexpr uint16_t kOrientVertical = 0x0001;
inline constexpr uint16_t kAxis6SixAxis   = 0x0001;

inline const char *configRegName(uint8_t reg)
{
    switch (reg) {
    case kRegOrient: return "ORIENT";
    case kRegAxis6:  return "AXIS6";
    case kRegRRate:  return "RRATE";
    default:         return "?";
    }
}

class ConfigCheck
{
public:
    static constexpr int kMaxAttempts = 3;

    enum class Verdict : uint8_t {
        Pending,      // still waiting for read-backs in this attempt
        Retry,        // this attempt failed — send the configuration again
        Confirmed,
        Failed,
        Unverified,
    };

    // A new connection: attempt 1 is about to be sent.
    void begin(uint16_t expectedRRate)
    {
        m_expectRRate = expectedRRate;
        m_attempt     = 0;
        m_active      = true;
        nextAttempt();
    }

    // Arms the next attempt; the caller sends the configuration after this.
    void nextAttempt()
    {
        ++m_attempt;
        m_pending   = kAllBits;
        m_wrongBits = 0;
    }

    void cancel() { m_active = false; m_pending = 0; }

    bool active()  const { return m_active; }
    int  attempt() const { return m_attempt; }

    // True when a read-back of `reg` is one this attempt is waiting for. Any
    // other 0x71 (unsolicited, mis-framed, or from an attempt already settled)
    // must not be counted.
    bool expecting(uint8_t reg) const { return m_active && (m_pending & bitFor(reg)) != 0; }

    uint16_t expected(uint8_t reg) const
    {
        switch (reg) {
        case kRegOrient: return kOrientVertical;
        case kRegAxis6:  return kAxis6SixAxis;
        case kRegRRate:  return m_expectRRate;
        default:         return 0;
        }
    }

    // Records one read-back. Returns Pending until every register of this
    // attempt has answered, then the attempt's verdict.
    Verdict onReadback(uint8_t reg, uint16_t raw)
    {
        const uint8_t bit = bitFor(reg);
        if (!m_active || (m_pending & bit) == 0)
            return Verdict::Pending;
        m_pending &= static_cast<uint8_t>(~bit);
        if (raw != expected(reg))
            m_wrongBits |= bit;
        if (m_pending != 0)
            return Verdict::Pending;
        return settle(/*missingBits=*/0);
    }

    // The caller's timer for this attempt fired: whatever has not answered
    // counts as unanswered.
    Verdict onTimeout()
    {
        if (!m_active || m_pending == 0)
            return Verdict::Pending;
        const uint8_t missing = m_pending;
        m_pending = 0;
        return settle(missing);
    }

    // After a final verdict: which registers were wrong / unanswered on the
    // last attempt (bit masks of kBit*).
    uint8_t wrongBits()   const { return m_wrongBits; }
    uint8_t missingBits() const { return m_missingBits; }

    static constexpr uint8_t kBitOrient = 1u << 0;
    static constexpr uint8_t kBitAxis6  = 1u << 1;
    static constexpr uint8_t kBitRRate  = 1u << 2;

private:
    static constexpr uint8_t kAllBits   = kBitOrient | kBitAxis6 | kBitRRate;
    static constexpr uint8_t kFrameBits = kBitOrient | kBitAxis6;   // what decides "not ready"

    static constexpr uint8_t bitFor(uint8_t reg)
    {
        return reg == kRegOrient ? kBitOrient
             : reg == kRegAxis6  ? kBitAxis6
             : reg == kRegRRate  ? kBitRRate
             : 0;
    }

    Verdict settle(uint8_t missingBits)
    {
        m_missingBits = missingBits;
        if (m_wrongBits == 0 && missingBits == 0) {
            m_active = false;
            return Verdict::Confirmed;
        }
        if (m_attempt < kMaxAttempts)
            return Verdict::Retry;
        m_active = false;
        return (m_wrongBits & kFrameBits) ? Verdict::Failed : Verdict::Unverified;
    }

    uint16_t m_expectRRate = 0;
    int      m_attempt     = 0;
    bool     m_active      = false;
    uint8_t  m_pending     = 0;
    uint8_t  m_wrongBits   = 0;
    uint8_t  m_missingBits = 0;
};

} // namespace pinpoint::wt901

#endif // PINPOINT_WT901_CONFIG_CHECK_H
