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

// THE STAND-IN DEVICES' SWITCHES (src/Core/synthetic_devices.h; testing_developer_guide.md, "The quit pins").
// One source, built twice (src/Gui/tests/CMakeLists.txt):
//   synthetic_devices_test      with PP_SYNTHETIC_DEVICES=1, as a Debug app with the option is;
//   synthetic_devices_off_test  with PP_SYNTHETIC_DEVICES=0, as every other build is.
// The oracles are the rules the header's comment and the guide state, written here as literal cases (valid values,
// malformed ones, and each switch's preconditions), never values read back from the parser.

#include "synthetic_devices.h"
#include "PpMessageLog.h"

#include <QtTest>

#ifndef PP_SYNTHETIC_DEVICES
#  error "synthetic_devices_test is built with PP_SYNTHETIC_DEVICES=0 or =1 (CMakeLists.txt)"
#endif

using namespace pinpoint;

namespace {

// The three switches, set and restored around each case, so no case reads another's or the shell's.
const char *const kVars[] = {"PINPOINT_HEADLESS_PROBE", "PINPOINT_PROBE_SYNTHETIC", "PINPOINT_PROBE_SYNTHETIC_SLOW"};

QStringList logSince(int &seq)
{
    QStringList out;
    for (const auto &e : PpMessageLog::instance()->fetchSince(seq))
        out << e.message;
    return out;
}

QByteArray rowName(const char *v)
{
    return *v ? QByteArray(v).toPercentEncoding() : QByteArray("(empty)");
}

bool anyContains(const QStringList &lines, const QString &text)
{
    for (const auto &l : lines)
        if (l.contains(text))
            return true;
    return false;
}

} // namespace

class SyntheticDevicesTest : public QObject {
    Q_OBJECT

    QList<std::optional<QByteArray>> m_saved;

    static void set(const char *name, const char *value)
    {
        if (value)
            qputenv(name, value);
        else
            qunsetenv(name);
    }

private slots:
    void init()
    {
        m_saved.clear();
        for (const char *v : kVars)
            m_saved << (qEnvironmentVariableIsSet(v) ? std::optional<QByteArray>(qgetenv(v)) : std::nullopt);
        for (const char *v : kVars)
            qunsetenv(v);
    }
    void cleanup()
    {
        for (int i = 0; i < int(std::size(kVars)); ++i)
            set(kVars[i], m_saved[i] ? m_saved[i]->constData() : nullptr);
    }

    // ---- the build: the option reaches the header, and the names are the documented ones ----

    void builtMatchesTheOption()
    {
        QCOMPARE(synthetic::kBuilt, PP_SYNTHETIC_DEVICES == 1);
        QCOMPARE(QByteArray(synthetic::kPartsVar), QByteArray("PINPOINT_PROBE_SYNTHETIC"));
        QCOMPARE(QByteArray(synthetic::kSlowVar), QByteArray("PINPOINT_PROBE_SYNTHETIC_SLOW"));
    }

    // ---- PINPOINT_PROBE_SYNTHETIC=<part>[,<part>]...: the parser, a pure function of the string ----

    void partsValid_data()
    {
        QTest::addColumn<QByteArray>("value");
        QTest::addColumn<QList<QByteArray>>("parts");
        QTest::newRow("one") << QByteArray("camera") << QList<QByteArray>{"camera"};
        QTest::newRow("two") << QByteArray("camera,imu") << QList<QByteArray>{"camera", "imu"};
        QTest::newRow("dash-digit") << QByteArray("imu-scan2") << QList<QByteArray>{"imu-scan2"};
        QTest::newRow("32 chars") << QByteArray(32, 'a') << QList<QByteArray>{QByteArray(32, 'a')};
    }
    void partsValid()
    {
        QFETCH(QByteArray, value);
        QFETCH(QList<QByteArray>, parts);
        const auto got = synthetic::parseParts(value);
        QVERIFY2(got.has_value(), value.constData());
        QCOMPARE(*got, parts);
    }

    void partsMalformed_data()
    {
        QTest::addColumn<QByteArray>("value");
        for (const char *v : {"", ",", "camera,", ",camera", "camera,,imu", "Camera", "1camera", "-camera", "camera-",
                              "cam_era", "cam.era", "camera imu", " camera", "camera ", "camera;imu", "camera\n",
                              "camera,camera", "1", "*"}) {
            QTest::newRow(rowName(v).constData()) << QByteArray(v);
        }
        QTest::newRow("33 chars") << QByteArray(33, 'a');
        QTest::newRow("NUL inside") << QByteArray("cam\0era", 7);
    }
    void partsMalformed()
    {
        QFETCH(QByteArray, value);
        QVERIFY2(!synthetic::parseParts(value).has_value(), value.toPercentEncoding().constData());
    }

    // ---- PINPOINT_PROBE_SYNTHETIC_SLOW=<part>.<step>:<ms> ----

    void slowValid_data()
    {
        QTest::addColumn<QByteArray>("value");
        QTest::addColumn<QByteArray>("call");
        QTest::addColumn<int>("ms");
        QTest::newRow("stop") << QByteArray("camera.stop:2000") << QByteArray("camera.stop") << 2000;
        QTest::newRow("1 ms") << QByteArray("imu.disconnect:1") << QByteArray("imu.disconnect") << 1;
        QTest::newRow("the ceiling") << QByteArray("imu.disconnect:120000") << QByteArray("imu.disconnect") << 120000;
        QTest::newRow("dashes") << QByteArray("imu-scan.model-load:750") << QByteArray("imu-scan.model-load") << 750;
    }
    void slowValid()
    {
        QFETCH(QByteArray, value);
        QFETCH(QByteArray, call);
        QFETCH(int, ms);
        const auto got = synthetic::parseSlow(value);
        QVERIFY2(got.has_value(), value.constData());
        QCOMPARE(got->call, call);
        QCOMPARE(got->ms, ms);
    }

    void slowMalformed_data()
    {
        QTest::addColumn<QByteArray>("value");
        for (const char *v : {"", "camera.stop", "camera.stop:", ":2000", "camera:2000", ".stop:2000", "camera.:2000",
                              "camera..stop:10", "camera.stop.now:10", "Camera.stop:10", "camera.Stop:10",
                              "camera.stop:0", "camera.stop:00", "camera.stop:02000", "camera.stop:+2000",
                              "camera.stop:-1", "camera.stop:120001", "camera.stop:999999999999999999999",
                              "camera.stop:2000ms", "camera.stop:2.5", "camera.stop:1e3", "camera.stop: 2000",
                              "camera.stop:2000 ", " camera.stop:2000", "camera.stop:2000:1",
                              "camera.stop:2000,imu.stop:10", "camera.stop=2000"}) {
            QTest::newRow(rowName(v).constData()) << QByteArray(v);
        }
    }
    void slowMalformed()
    {
        QFETCH(QByteArray, value);
        QVERIFY2(!synthetic::parseSlow(value).has_value(), value.toPercentEncoding().constData());
    }

    // ---- the readers: only under PINPOINT_HEADLESS_PROBE=1, only in a build with the option ----

    void onNeedsTheHeadlessProbe()
    {
        set("PINPOINT_PROBE_SYNTHETIC", "camera");
        int seq = PpMessageLog::instance()->currentSeq();
        QVERIFY(!synthetic::on("camera"));                 // a golfer's launch: the switch alone does nothing...
        set("PINPOINT_HEADLESS_PROBE", "01");
        QVERIFY(!synthetic::on("camera"));                 // ...nor with the probe's switch spelt any other way
        set("PINPOINT_HEADLESS_PROBE", "true");
        QVERIFY(!synthetic::on("camera"));
        QVERIFY(logSince(seq).isEmpty());                  // and says nothing: not a probe, so no probe line
    }

    void onWithBothSwitches()
    {
        set("PINPOINT_HEADLESS_PROBE", "1");
        set("PINPOINT_PROBE_SYNTHETIC", "camera,imu");
        QCOMPARE(synthetic::on("camera"), synthetic::kBuilt);
        QCOMPARE(synthetic::on("imu"), synthetic::kBuilt);
        QVERIFY(!synthetic::on("microphone"));             // a part not listed
        QVERIFY(!synthetic::on("cam"));                    // not a prefix match
        set("PINPOINT_PROBE_SYNTHETIC", nullptr);
        QVERIFY(!synthetic::on("camera"));                 // unset: nothing
    }

    void onWithoutTheOptionSaysSo()
    {
        if (synthetic::kBuilt)
            QSKIP("the option-off case is synthetic_devices_off_test's");
        set("PINPOINT_HEADLESS_PROBE", "1");
        set("PINPOINT_PROBE_SYNTHETIC", "camera");
        int seq = PpMessageLog::instance()->currentSeq();
        QVERIFY(!synthetic::on("camera"));
        const QStringList lines = logSince(seq);
        QVERIFY2(anyContains(lines, "[HeadlessProbe] synthetic switch ignored: PINPOINT_PROBE_SYNTHETIC"),
                 qPrintable(lines.join('\n')));
        QVERIFY2(anyContains(lines, "no stand-in devices"), qPrintable(lines.join('\n')));
    }

    void onMalformedIsLoggedAndIgnored()
    {
        set("PINPOINT_HEADLESS_PROBE", "1");
        set("PINPOINT_PROBE_SYNTHETIC", "camera,,imu");
        int seq = PpMessageLog::instance()->currentSeq();
        QVERIFY(!synthetic::on("camera"));                 // never acted on: not even the well-formed part
        QVERIFY(!synthetic::on("imu"));
        const QStringList lines = logSince(seq);
        QVERIFY2(anyContains(lines, "[HeadlessProbe] synthetic switch ignored: PINPOINT_PROBE_SYNTHETIC"),
                 qPrintable(lines.join('\n')));
        // Set but empty. On Unix the variable then exists, empty, and is malformed, not "off". On Windows an empty
        // assignment removes the variable (_putenv_s), so the case cannot be set up there: it reads as unset, which is
        // off and says nothing. Each branch first checks that its set-up took.
        set("PINPOINT_PROBE_SYNTHETIC", "");
        seq = PpMessageLog::instance()->currentSeq();
        QVERIFY(!synthetic::on("camera"));
#ifdef Q_OS_WIN
        QVERIFY(!qEnvironmentVariableIsSet("PINPOINT_PROBE_SYNTHETIC"));
        QVERIFY(logSince(seq).isEmpty());
#else
        QVERIFY(qEnvironmentVariableIsSet("PINPOINT_PROBE_SYNTHETIC"));
        QVERIFY(anyContains(logSince(seq), "[HeadlessProbe] synthetic switch ignored"));
#endif
    }

    void slowReader()
    {
        set("PINPOINT_PROBE_SYNTHETIC_SLOW", "camera.stop:2000");
        QCOMPARE(synthetic::slowMs("camera.stop"), 0);     // no headless probe: never slow
        set("PINPOINT_HEADLESS_PROBE", "1");
        QCOMPARE(synthetic::slowMs("camera.stop"), synthetic::kBuilt ? 2000 : 0);
        QCOMPARE(synthetic::slowMs("camera.start"), 0);    // another call
        QCOMPARE(synthetic::slowMs("imu.stop"), 0);        // another part's call of the same name
        set("PINPOINT_PROBE_SYNTHETIC_SLOW", "camera.stop:2000ms");
        int seq = PpMessageLog::instance()->currentSeq();
        QCOMPARE(synthetic::slowMs("camera.stop"), 0);     // malformed: logged, ignored, never a guess
        QVERIFY(anyContains(logSince(seq), "[HeadlessProbe] synthetic switch ignored: PINPOINT_PROBE_SYNTHETIC_SLOW"));
        set("PINPOINT_PROBE_SYNTHETIC_SLOW", nullptr);
        seq = PpMessageLog::instance()->currentSeq();
        QCOMPARE(synthetic::slowMs("camera.stop"), 0);     // unset: not slow, and nothing said
        QVERIFY(logSince(seq).isEmpty());
    }

    // ---- the log convention the driver looks for: exactly "[HeadlessProbe] synthetic: <part>" ----

    void startedLine()
    {
        int seq = PpMessageLog::instance()->currentSeq();
        synthetic::logStarted("camera", "(two stand-in cameras)");
        const QStringList lines = logSince(seq);
        QCOMPARE(lines.size(), 1);
        QVERIFY2(lines.first().startsWith("[HeadlessProbe] synthetic: camera "), qPrintable(lines.first()));
        QVERIFY2(lines.first().contains("(two stand-in cameras)"), qPrintable(lines.first()));
    }
};

QTEST_GUILESS_MAIN(SyntheticDevicesTest)
#include "synthetic_devices_test.moc"
