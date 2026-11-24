// SPDX-License-Identifier: GPL-3.0-or-later
// wt901_config_retry_test — the driver half of the connect-time configuration check
// (issue #105). wt901_config_check_test pins the decision; this pins the driver that
// acts on it: WT9011DCL_Base really sends ORIENT / AXIS6 / RRATE again on a wrong
// read-back, stops after ConfigCheck::kMaxAttempts, reports every read-back and the
// verdict, and drops a check whose link went away.
//
// No sensor and no Bluetooth: FakeWitmotion is a register model behind the driver's
// writeToDevice(). A write (FF AA reg lo hi) sets the register, unless the model is
// told to ignore that many ORIENT writes (the 27 Oct failure); a READADDR request
// (FF AA 27 reg 00) queues a 20-byte 55 71 answer, which pump() feeds back through
// receiveData() the way the BLE transport does.

#include <QCoreApplication>
#include <QList>
#include <QString>

#include <cstdio>
#include <deque>
#include <map>

#include "wt9011dcl_base.h"

using pinpoint::wt901::ConfigCheck;
using V = ConfigCheck::Verdict;

// The try count is three; the checks below count against the constant, so pin its value too.
static_assert(ConfigCheck::kMaxAttempts == 3, "the Witmotion's settings check tries three times");

static int g_fail = 0;

#define CHECK(label, cond)                                                       \
    do { bool ok = (cond);                                                       \
         std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", label);                \
         if (!ok) ++g_fail; } while (0)

namespace {

constexpr quint8  kReadAddr = 0x27;
constexpr quint16 kRate100  = 0x09;

class FakeWitmotion : public WT9011DCL_Base
{
public:
    // ORIENT writes the "device" drops before one takes; < 0 = never takes.
    int ignoreOrientWrites = 0;
    int configSends        = 0;

    struct Checked { int verdict; int attempt; QString detail; };
    struct Readback { int reg; int got; int want; int attempt; };
    QList<Checked>  verdicts;
    QList<Readback> readbacks;

    FakeWitmotion()
    {
        m_regs[pinpoint::wt901::kRegOrient] = 0x0000;   // horizontal, as the 27 Oct sensor
        m_regs[pinpoint::wt901::kRegAxis6]  = 0x0000;
        m_regs[pinpoint::wt901::kRegRRate]  = 0x0006;
        connect(this, &WT9011DCL_Base::configurationChecked, this,
                [this](int v, int a, const QString &d) { verdicts.append({v, a, d}); });
        connect(this, &WT9011DCL_Base::configReadback, this,
                [this](int r, int g, int w, int a) { readbacks.append({r, g, w, a}); });
    }

    Transport       transport()    const override { return Transport::Ble; }
    ImuCapabilities capabilities() const override { return wt901Defaults(); }

    void start() { startConfigCheck(kRate100); }

    // The device answers each read request in order, after the writes before it.
    void pump()
    {
        while (!m_answers.empty()) {
            const QByteArray frame = m_answers.front();
            m_answers.pop_front();
            receiveData(frame);
        }
    }
    // An unsolicited ORIENT answer (no request in flight), as a mis-framed 0x71 would be.
    void strayOrientAnswer()
    {
        QByteArray f(20, '\0');
        f[0] = char(0x55); f[1] = char(0x71); f[2] = char(pinpoint::wt901::kRegOrient);
        receiveData(f);
    }
    bool answersPending() const { return !m_answers.empty(); }

    int mismatches() const
    {
        int n = 0;
        for (const auto &r : readbacks) n += (r.got != r.want);
        return n;
    }

protected:
    void sendConfiguration() override
    {
        ++configSends;
        sendCommand(pinpoint::wt901::kRegOrient, pinpoint::wt901::kOrientVertical);
        sendCommand(pinpoint::wt901::kRegAxis6,  pinpoint::wt901::kAxis6SixAxis);
        sendCommand(pinpoint::wt901::kRegRRate,  kRate100);
    }

    void writeToDevice(const QByteArray &d) override
    {
        if (d.size() != 5 || quint8(d[0]) != 0xFF || quint8(d[1]) != 0xAA)
            return;
        const quint8  reg   = quint8(d[2]);
        const quint16 value = quint16(quint8(d[3]) | (quint8(d[4]) << 8));
        if (reg == kReadAddr) {
            const quint8  target = quint8(d[3]);
            const quint16 v      = m_regs.count(target) ? m_regs[target] : 0;
            QByteArray f(20, '\0');
            f[0] = char(0x55); f[1] = char(0x71);
            f[2] = char(target); f[3] = 0;
            f[4] = char(v & 0xFF); f[5] = char(v >> 8);
            m_answers.push_back(f);
            return;
        }
        if (reg == pinpoint::wt901::kRegOrient && ignoreOrientWrites != 0) {
            if (ignoreOrientWrites > 0) --ignoreOrientWrites;
            return;   // the write was not applied
        }
        m_regs[reg] = value;
    }

private:
    std::map<quint8, quint16> m_regs;
    std::deque<QByteArray>    m_answers;
};

void matchesFirstTime()
{
    std::printf("read-back matches first time\n");
    FakeWitmotion imu;
    imu.start();
    imu.pump();
    CHECK("configuration sent once", imu.configSends == 1);
    CHECK("three read-backs reported", imu.readbacks.size() == 3);
    CHECK("none mismatched", imu.mismatches() == 0);
    CHECK("one verdict: Confirmed on attempt 1",
          imu.verdicts.size() == 1 && imu.verdicts[0].verdict == int(V::Confirmed)
          && imu.verdicts[0].attempt == 1 && imu.verdicts[0].detail.isEmpty());
}

void mismatchTakesOnSecondTry()
{
    std::printf("27 Oct: ORIENT not applied, takes on the second try\n");
    FakeWitmotion imu;
    imu.ignoreOrientWrites = 1;
    imu.start();
    imu.pump();
    CHECK("configuration sent twice", imu.configSends == 2);
    CHECK("the wrong ORIENT reported as a mismatch", imu.mismatches() == 1
          && imu.readbacks[0].reg == pinpoint::wt901::kRegOrient
          && imu.readbacks[0].got == 0x0000 && imu.readbacks[0].want == 0x0001);
    CHECK("Retry then Confirmed on attempt 2",
          imu.verdicts.size() == 2 && imu.verdicts[0].verdict == int(V::Retry)
          && imu.verdicts[0].detail.contains(QStringLiteral("ORIENT read back wrong"))
          && imu.verdicts[1].verdict == int(V::Confirmed) && imu.verdicts[1].attempt == 2);
}

void mismatchNeverTakes()
{
    std::printf("ORIENT never takes: bounded, ends Failed (not ready)\n");
    FakeWitmotion imu;
    imu.ignoreOrientWrites = -1;
    imu.start();
    imu.pump();
    CHECK("configuration sent exactly kMaxAttempts times",
          imu.configSends == ConfigCheck::kMaxAttempts);
    CHECK("one mismatch per attempt", imu.mismatches() == ConfigCheck::kMaxAttempts);
    CHECK("Retry, Retry, then Failed naming ORIENT",
          imu.verdicts.size() == ConfigCheck::kMaxAttempts
          && imu.verdicts[0].verdict == int(V::Retry) && imu.verdicts[1].verdict == int(V::Retry)
          && imu.verdicts.last().verdict == int(V::Failed)
          && imu.verdicts.last().attempt == ConfigCheck::kMaxAttempts
          && imu.verdicts.last().detail == QStringLiteral("ORIENT read back wrong"));
    CHECK("nothing more sent or queued", !imu.answersPending());
    // A settled check ignores stray read-backs and its attempt's timeout.
    QCoreApplication::processEvents();
    imu.strayOrientAnswer();
    CHECK("no read-back or verdict after Failed", imu.verdicts.size() == ConfigCheck::kMaxAttempts
          && imu.readbacks.size() == 3 * ConfigCheck::kMaxAttempts);
}

void disconnectCancels()
{
    std::printf("a dropped link ends the check\n");
    FakeWitmotion imu;
    imu.ignoreOrientWrites = -1;
    imu.start();
    emit imu.disconnected();
    imu.pump();
    CHECK("late answers are not counted", imu.readbacks.isEmpty());
    CHECK("no verdict, no resend", imu.verdicts.isEmpty() && imu.configSends == 1);
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);   // the driver's per-attempt QTimer needs an event loop thread
    matchesFirstTime();
    mismatchTakesOnSecondTry();
    mismatchNeverTakes();
    disconnectCancels();
    std::printf(g_fail ? "\n%d check(s) FAILED\n" : "\nall checks passed\n", g_fail);
    return g_fail ? 1 : 0;
}
