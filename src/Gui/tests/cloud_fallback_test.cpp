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

// CloudFallback (media/cloud_fallback.h) — the one cloud-fallback piece of the speech-to-text,
// text-to-speech and AI Coach controllers, run under each controller's own policy (the same
// CloudFallback::speechToText() / textToSpeech() / aiCoach() objects the controllers construct with).
// The secrets, the preference, the GPU and the two switches are fakes: no SecretsManager (it reads
// the developer's environment first), no AppSettings, no engine, no audio.
//
// The rules, each as the three controllers stated them before the piece (the controllers' own
// comments at 31c791ac):
//   R1 the key: speech to text uses its own Azure key, else the shared text-to-speech key; text to
//      speech only the text-to-speech key; the AI Coach only the Gemini key
//   R2 no settings: apply and toggle do nothing
//   R3 the cloud engine is used when the preference asks for it and a key exists; the AI Coach also
//      when there is no local GPU (it has no CPU path); a GPU never matters to the other two
//   R4 rebuild only when the desired backend changes, OR when staying on cloud the key changed
//   R5 on the cloud path the piece marks cloud in use and records the key before the switch; on the
//      local path it forgets the key and leaves "in use" to the controller's switch
//   R6 refresh: availability follows the key, announced once per change and before the switch it
//      may lead to, and is updated even with no settings
//   R7 toggle flips the persisted preference, only when a key is configured; the AI Coach's is
//      refused with no local GPU (cloud is mandatory)
//   R8 a controller's own writers (a backend report, a cloud path that records no key) are honoured
//      by the next apply

#include "media/cloud_fallback.h"

#include <QHash>
#include <QStringList>
#include <QTest>

namespace {

// The fakes behind the hooks. `events` records, in order, what the piece asked for.
struct Rig {
    QHash<QString, QString> secrets;
    bool                    hasSettings = true;
    bool                    pref        = false;
    bool                    gpu         = true;
    // What the controller's local switch does about "in use". Speech to text and text to speech
    // always clear it; the AI Coach's clears it only when the coach is enabled.
    bool                    localSwitchClearsUsing = true;
    QStringList             events;
    CloudFallback          *piece = nullptr;

    CloudFallback::Hooks hooks()
    {
        CloudFallback::Hooks h;
        h.readSecret = [this](const QString &id) { return secrets.value(id); };
        h.preference = [this]() -> std::optional<bool> {
            if (!hasSettings) return std::nullopt;
            return pref;
        };
        h.setPreference = [this](bool v) { pref = v; events << QStringLiteral("pref=%1").arg(v); };
        h.localGpu      = [this]() { return gpu; };
        h.switchToCloud = [this](const QString &key) {
            // R5: in use and the key are set before the switch runs.
            events << QStringLiteral("cloud:%1 using=%2 loaded=%3")
                          .arg(key).arg(piece->usingCloud()).arg(piece->loadedKey());
        };
        h.switchToLocal = [this]() {
            events << QStringLiteral("local using=%1 loaded=%2")
                          .arg(piece->usingCloud()).arg(piece->loadedKey());
            if (localSwitchClearsUsing)
                piece->setUsingCloud(false);
        };
        h.availabilityChanged = [this]() {
            events << QStringLiteral("available=%1").arg(piece->available());
        };
        return h;
    }
};

struct Fixture {
    Rig           rig;
    CloudFallback piece;
    explicit Fixture(const CloudFallback::Policy &policy)
        : piece(policy, rig.hooks()) { rig.piece = &piece; }
    QStringList take() { QStringList e = rig.events; rig.events.clear(); return e; }
};

const QString kStt    = QStringLiteral("azureSttApiKey");
const QString kTts    = QStringLiteral("azureTtsApiKey");
const QString kGemini = QStringLiteral("geminiApiKey");

} // namespace

class CloudFallbackTest : public QObject
{
    Q_OBJECT

    enum Which { Stt, Tts, Coach };
    static CloudFallback::Policy policy(int w)
    {
        switch (w) {
        case Stt:  return CloudFallback::speechToText();
        case Tts:  return CloudFallback::textToSpeech();
        default:   return CloudFallback::aiCoach();
        }
    }
    static QString ownKey(int w) { return w == Stt ? kStt : w == Tts ? kTts : kGemini; }

    void addPolicies()
    {
        QTest::addColumn<int>("which");
        QTest::newRow("speech to text") << int(Stt);
        QTest::newRow("text to speech") << int(Tts);
        QTest::newRow("AI Coach")       << int(Coach);
    }

private slots:
    // R1
    void speechToTextKeyIsItsOwnElseTheSharedTtsKey()
    {
        Fixture f(CloudFallback::speechToText());
        QCOMPARE(f.piece.key(), QString());
        f.rig.secrets[kTts] = QStringLiteral("T");
        QCOMPARE(f.piece.key(), QStringLiteral("T"));
        f.rig.secrets[kStt] = QStringLiteral("S");
        QCOMPARE(f.piece.key(), QStringLiteral("S"));
        f.rig.secrets[kGemini] = QStringLiteral("G");
        f.rig.secrets.remove(kStt);
        f.rig.secrets.remove(kTts);
        QVERIFY(!f.piece.keyPresent());
    }
    void textToSpeechKeyIsOnlyTheTtsKey()
    {
        Fixture f(CloudFallback::textToSpeech());
        f.rig.secrets[kStt]    = QStringLiteral("S");
        f.rig.secrets[kGemini] = QStringLiteral("G");
        QVERIFY(!f.piece.keyPresent());
        f.rig.secrets[kTts] = QStringLiteral("T");
        QCOMPARE(f.piece.key(), QStringLiteral("T"));
    }
    void aiCoachKeyIsOnlyTheGeminiKey()
    {
        Fixture f(CloudFallback::aiCoach());
        f.rig.secrets[kStt] = QStringLiteral("S");
        f.rig.secrets[kTts] = QStringLiteral("T");
        QVERIFY(!f.piece.keyPresent());
        f.rig.secrets[kGemini] = QStringLiteral("G");
        QCOMPARE(f.piece.key(), QStringLiteral("G"));
    }

    // R1 with R4: the speech-to-text cloud engine moves to its own key when one is added.
    void speechToTextRebuildsWhenItsOwnKeyArrives()
    {
        Fixture f(CloudFallback::speechToText());
        f.rig.pref = true;
        f.rig.secrets[kTts] = QStringLiteral("T");
        f.piece.apply();
        QCOMPARE(f.take(), QStringList{ QStringLiteral("cloud:T using=1 loaded=T") });
        f.rig.secrets[kStt] = QStringLiteral("S");
        f.piece.apply();
        QCOMPARE(f.take(), QStringList{ QStringLiteral("cloud:S using=1 loaded=S") });
    }

    // R2
    void noSettingsMeansApplyAndToggleDoNothing_data() { addPolicies(); }
    void noSettingsMeansApplyAndToggleDoNothing()
    {
        QFETCH(int, which);
        Fixture f(policy(which));
        f.rig.hasSettings = false;
        f.rig.gpu = false;
        f.rig.secrets[ownKey(which)] = QStringLiteral("A");
        f.piece.setAvailable(true);
        f.piece.apply();
        f.piece.toggle();
        QCOMPARE(f.take(), QStringList());
        QVERIFY(!f.piece.usingCloud());
    }

    // R3, R4, R5: preference on with a key goes to cloud once; a second apply is a no-op.
    void preferenceOnWithKeyGoesToCloudOnce_data() { addPolicies(); }
    void preferenceOnWithKeyGoesToCloudOnce()
    {
        QFETCH(int, which);
        Fixture f(policy(which));
        f.rig.pref = true;
        f.rig.secrets[ownKey(which)] = QStringLiteral("A");
        f.piece.apply();
        QCOMPARE(f.take(), QStringList{ QStringLiteral("cloud:A using=1 loaded=A") });
        QVERIFY(f.piece.usingCloud());
        f.piece.apply();
        QCOMPARE(f.take(), QStringList());
    }

    // R3: preference on but no key: nothing to switch to, and nothing switched.
    void preferenceOnWithoutKeyStaysLocal_data() { addPolicies(); }
    void preferenceOnWithoutKeyStaysLocal()
    {
        QFETCH(int, which);
        Fixture f(policy(which));
        f.rig.pref = true;
        f.rig.gpu  = false;
        f.piece.apply();
        QCOMPARE(f.take(), QStringList());
        QVERIFY(!f.piece.usingCloud());
    }

    // R4: the key corrected while staying on cloud rebuilds with the new key.
    void keyChangedWhileOnCloudRebuilds_data() { addPolicies(); }
    void keyChangedWhileOnCloudRebuilds()
    {
        QFETCH(int, which);
        Fixture f(policy(which));
        f.rig.pref = true;
        f.rig.secrets[ownKey(which)] = QStringLiteral("A");
        f.piece.apply();
        f.take();
        f.rig.secrets[ownKey(which)] = QStringLiteral("B");
        f.piece.apply();
        QCOMPARE(f.take(), QStringList{ QStringLiteral("cloud:B using=1 loaded=B") });
        f.piece.apply();
        QCOMPARE(f.take(), QStringList());
    }

    // R4, R5: preference off goes local once, the key forgotten before the switch runs.
    void preferenceOffGoesLocal_data() { addPolicies(); }
    void preferenceOffGoesLocal()
    {
        QFETCH(int, which);
        Fixture f(policy(which));
        f.rig.pref = true;
        f.rig.secrets[ownKey(which)] = QStringLiteral("A");
        f.piece.apply();
        f.take();
        f.rig.pref = false;
        f.piece.apply();
        QCOMPARE(f.take(), QStringList{ QStringLiteral("local using=1 loaded=") });
        QVERIFY(!f.piece.usingCloud());
        QCOMPARE(f.piece.loadedKey(), QString());
        f.piece.apply();
        QCOMPARE(f.take(), QStringList());
    }

    // R3, R4: the key removed while on cloud goes local, whatever the preference says.
    void keyRemovedWhileOnCloudGoesLocal_data() { addPolicies(); }
    void keyRemovedWhileOnCloudGoesLocal()
    {
        QFETCH(int, which);
        Fixture f(policy(which));
        f.rig.pref = true;
        f.rig.secrets[ownKey(which)] = QStringLiteral("A");
        f.piece.apply();
        f.take();
        f.rig.secrets.clear();
        f.piece.apply();
        QCOMPARE(f.take(), QStringList{ QStringLiteral("local using=1 loaded=") });
    }

    // R5: the piece does not clear "in use" on the local path itself: a local switch that declines
    // (the AI Coach's, when the coach is dark) is asked again on the next apply.
    void aDecliningLocalSwitchIsAskedAgain()
    {
        Fixture f(CloudFallback::aiCoach());
        f.rig.pref = true;
        f.rig.secrets[kGemini] = QStringLiteral("G");
        f.piece.apply();
        f.take();
        f.rig.localSwitchClearsUsing = false;
        f.rig.pref = false;
        f.piece.apply();
        QCOMPARE(f.take(), QStringList{ QStringLiteral("local using=1 loaded=") });
        QVERIFY(f.piece.usingCloud());
        f.piece.apply();
        QCOMPARE(f.take(), QStringList{ QStringLiteral("local using=1 loaded=") });
    }

    // R3: no local GPU makes the AI Coach use the cloud with the preference off; never the other two.
    void noLocalGpuMeansCloudForTheCoachOnly_data()
    {
        QTest::addColumn<int>("which");
        QTest::addColumn<bool>("expectCloud");
        QTest::newRow("speech to text") << int(Stt)   << false;
        QTest::newRow("text to speech") << int(Tts)   << false;
        QTest::newRow("AI Coach")       << int(Coach) << true;
    }
    void noLocalGpuMeansCloudForTheCoachOnly()
    {
        QFETCH(int, which);
        QFETCH(bool, expectCloud);
        Fixture f(policy(which));
        f.rig.pref = false;
        f.rig.gpu  = false;
        f.rig.secrets[ownKey(which)] = QStringLiteral("A");
        f.piece.apply();
        QCOMPARE(f.take(), expectCloud ? QStringList{ QStringLiteral("cloud:A using=1 loaded=A") }
                                       : QStringList());
        // With a GPU the preference alone decides, for all three.
        f.rig.gpu = true;
        f.piece.apply();
        QCOMPARE(f.take(), expectCloud ? QStringList{ QStringLiteral("local using=1 loaded=") }
                                       : QStringList());
        QVERIFY(!f.piece.usingCloud());
    }

    // R6: availability follows the key; announced once per change, before the switch it leads to.
    void refreshAnnouncesAvailabilityBeforeSwitching_data() { addPolicies(); }
    void refreshAnnouncesAvailabilityBeforeSwitching()
    {
        QFETCH(int, which);
        Fixture f(policy(which));
        f.rig.pref = true;
        f.piece.refresh();
        QCOMPARE(f.take(), QStringList());
        QVERIFY(!f.piece.available());

        f.rig.secrets[ownKey(which)] = QStringLiteral("A");
        f.piece.refresh();
        QCOMPARE(f.take(), (QStringList{ QStringLiteral("available=1"),
                                         QStringLiteral("cloud:A using=1 loaded=A") }));
        f.piece.refresh();
        QCOMPARE(f.take(), QStringList());

        f.rig.secrets.clear();
        f.piece.refresh();
        QCOMPARE(f.take(), (QStringList{ QStringLiteral("available=0"),
                                         QStringLiteral("local using=1 loaded=") }));
    }

    // R6 with R2: with no settings, refresh still updates and announces availability.
    void refreshWithoutSettingsStillUpdatesAvailability_data() { addPolicies(); }
    void refreshWithoutSettingsStillUpdatesAvailability()
    {
        QFETCH(int, which);
        Fixture f(policy(which));
        f.rig.hasSettings = false;
        f.rig.secrets[ownKey(which)] = QStringLiteral("A");
        f.piece.refresh();
        QCOMPARE(f.take(), QStringList{ QStringLiteral("available=1") });
        QVERIFY(f.piece.available());
    }

    // R7: toggle flips the preference only when a key is configured.
    void toggleFlipsThePreferenceWhenAvailable_data() { addPolicies(); }
    void toggleFlipsThePreferenceWhenAvailable()
    {
        QFETCH(int, which);
        Fixture f(policy(which));
        f.piece.toggle();
        QCOMPARE(f.take(), QStringList());
        f.piece.setAvailable(true);
        f.piece.toggle();
        QCOMPARE(f.take(), QStringList{ QStringLiteral("pref=1") });
        f.piece.toggle();
        QCOMPARE(f.take(), QStringList{ QStringLiteral("pref=0") });
    }

    // R7: with no local GPU the coach's toggle is refused; the other two still flip.
    void toggleWithoutGpu_data()
    {
        QTest::addColumn<int>("which");
        QTest::addColumn<bool>("expectFlip");
        QTest::newRow("speech to text") << int(Stt)   << true;
        QTest::newRow("text to speech") << int(Tts)   << true;
        QTest::newRow("AI Coach")       << int(Coach) << false;
    }
    void toggleWithoutGpu()
    {
        QFETCH(int, which);
        QFETCH(bool, expectFlip);
        Fixture f(policy(which));
        f.rig.gpu = false;
        f.piece.setAvailable(true);
        f.piece.toggle();
        QCOMPARE(f.take(), expectFlip ? QStringList{ QStringLiteral("pref=1") } : QStringList());
    }

    // R8: a cloud path that records no key (the coach's download failure, its CPU-only report) is
    // rebuilt with the key on the next apply; a backend report of "local" is switched back to cloud.
    void aCloudPathWithoutARecordedKeyIsRebuilt()
    {
        Fixture f(CloudFallback::aiCoach());
        f.rig.pref = true;
        f.rig.secrets[kGemini] = QStringLiteral("G");
        f.piece.setUsingCloud(true);
        f.piece.apply();
        QCOMPARE(f.take(), QStringList{ QStringLiteral("cloud:G using=1 loaded=G") });
    }
    void aBackendReportOfLocalIsSwitchedBack_data() { addPolicies(); }
    void aBackendReportOfLocalIsSwitchedBack()
    {
        QFETCH(int, which);
        Fixture f(policy(which));
        f.rig.pref = true;
        f.rig.secrets[ownKey(which)] = QStringLiteral("A");
        f.piece.apply();
        f.take();
        f.piece.setUsingCloud(false);   // e.g. speech to text's worker reported a local label
        f.piece.apply();
        QCOMPARE(f.take(), QStringList{ QStringLiteral("cloud:A using=1 loaded=A") });
    }
};

QTEST_GUILESS_MAIN(CloudFallbackTest)
#include "cloud_fallback_test.moc"
