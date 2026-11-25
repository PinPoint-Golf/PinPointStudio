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

#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

// CloudFallback — the one cloud-fallback piece of the three media controllers (speech to text,
// text to speech, the AI Coach). Each controller has a local engine and a cloud engine behind an API
// key; the user's force-cloud preference (AppSettings::cloudFallback{Stt,Tts,Llm}) picks one when the
// key is configured. This holds what the three share:
//
//   - the state: whether the cloud toggle is available (a key is configured), whether the cloud engine
//     is in use, and the key the live cloud engine was built with;
//   - apply(): the engine the preference and the key ask for, switched to only when it differs from the
//     one in use, OR when staying on cloud but the key changed (the user corrected a bad key);
//   - refresh(): availability re-read from the key, its change announced, then apply() (a newly entered
//     key may enable a force-cloud preference that was waiting on it);
//   - toggle(): the badge's flip of the persisted preference, which then drives apply().
//
// What differs between the controllers is a Policy (which keys, whether "no local GPU" makes cloud the
// only path) and the Hooks each passes in: how the preference is read and written, and how the switch
// is made. The switch itself stays the controller's: it alone knows its worker and thread. On the cloud
// path this marks the cloud engine in use and records the key BEFORE calling the switch; on the local
// path it forgets the key, and marking the cloud engine out of use is left to the controller's switch
// (which may decline, as the AI Coach's does when it is dark).
//
// Plain C++, no QObject: the controllers keep their own signals and slots, and the availability change
// reaches QML through the controller's signal, emitted from Hooks::availabilityChanged.
class CloudFallback
{
public:
    struct Policy {
        // Secret ids tried in order; the first non-empty value is the key.
        QStringList keyIds;
        // True when the controller has no CPU path, so no local GPU makes the cloud engine the only one
        // (the AI Coach): then the preference is treated as on, and toggle() is refused.
        bool noLocalGpuMeansCloud = false;
    };

    // The three controllers' policies. The controllers construct with these, and the test uses the same.
    // Speech to text: its own Azure key, or the shared text-to-speech key.
    static Policy speechToText() { return { { QStringLiteral("azureSttApiKey"), QStringLiteral("azureTtsApiKey") }, false }; }
    // Text to speech: the Azure text-to-speech key.
    static Policy textToSpeech() { return { { QStringLiteral("azureTtsApiKey") }, false }; }
    // The AI Coach: the Gemini key; no local GPU means cloud.
    static Policy aiCoach()      { return { { QStringLiteral("geminiApiKey") }, true }; }

    struct Hooks {
        // Reads one secret by id (SecretsManager::read in the app).
        std::function<QString(const QString &id)> readSecret;
        // The user's force-cloud preference; nullopt when the controller has no settings (then apply()
        // and toggle() do nothing).
        std::function<std::optional<bool>()> preference;
        std::function<void(bool)>            setPreference;
        // Whether a local GPU is available; read only when Policy::noLocalGpuMeansCloud.
        std::function<bool()> localGpu;
        // Build the cloud engine with this key. Called after usingCloud() is set and the key recorded.
        std::function<void(const QString &key)> switchToCloud;
        // Go back to the local engine. Called after the recorded key is cleared; the controller's
        // switch marks the cloud engine out of use (setUsingCloud(false)) when it switches.
        std::function<void()> switchToLocal;
        // Availability changed (the controller emits its NOTIFY signal).
        std::function<void()> availabilityChanged;
    };

    CloudFallback(Policy policy, Hooks hooks);

    // The configured key: the first non-empty secret of Policy::keyIds, or empty.
    QString key() const;
    bool    keyPresent() const { return !key().isEmpty(); }

    void apply();
    void refresh();
    void toggle();

    bool    available() const  { return m_available; }
    bool    usingCloud() const { return m_usingCloud; }
    QString loadedKey() const  { return m_loadedKey; }

    // For a controller's own writers: its constructor's initial state, its worker's backend reports and
    // its other paths to the cloud. None of them calls a hook or announces anything.
    void setAvailable(bool available)       { m_available = available; }
    void setUsingCloud(bool usingCloud)     { m_usingCloud = usingCloud; }
    void setLoadedKey(const QString &key)   { m_loadedKey = key; }

private:
    Policy  m_policy;
    Hooks   m_hooks;
    bool    m_available  = false;
    bool    m_usingCloud = false;
    QString m_loadedKey;   // the key the live cloud engine was built with; empty when local
};
