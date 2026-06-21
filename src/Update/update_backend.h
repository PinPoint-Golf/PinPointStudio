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

#include <QMetaType>
#include <QObject>
#include <QString>

class AppSettings;
class SessionController;

namespace pp::update {

// The state vocabulary, owned here so both UpdateController and every backend
// share one enum (it was previously private to UpdateController). UpdateController
// maps these to the QML strings the update banner switches on — that mapping is
// the single source of truth, so the QML contract cannot drift.
enum class State {
    Unsupported,      // no in-app update engine on this platform/build
    DevBuild,         // engine present but not an installed/AppImage build → inert
    Idle,
    Checking,
    UpToDate,
    UpdateAvailable,
    Downloading,
    Verifying,
    ReadyToRelaunch,
    Error
};

// Immutable description of an offered update, handed up via updateOffered().
struct OfferInfo {
    QString version;        // display version, e.g. "v0.1-alpha4"
    QString releaseNotes;   // markdown body
};

} // namespace pp::update

Q_DECLARE_METATYPE(pp::update::State)
Q_DECLARE_METATYPE(pp::update::OfferInfo)

// Polymorphic engine behind UpdateController — one concrete subclass per OS engine
// family (Linux AppImage / macOS Sparkle / Windows WinSparkle), plus an inert
// fallback. The controller owns exactly one, chosen by makeUpdateBackend(), and
// relays its signals to QML. CPU architecture is NOT a subclass axis: it is a
// PlatformTarget value each backend uses to pick the asset/feed (see
// platform_target.h). The GPU/CUDA sidecar is a separate concern entirely
// (CudaRuntimeController) — it is not a backend.
//
// A backend is **state-reporting**: it emits transitions; the controller keeps the
// canonical mirror and the string mapping. Session-safety *policy* (refusing a
// relaunch while a session is live) stays in the controller — a backend's
// relaunch() is only reached after that guard passes.
class UpdateBackend : public QObject
{
    Q_OBJECT
public:
    explicit UpdateBackend(QObject *parent = nullptr) : QObject(parent) {}
    ~UpdateBackend() override = default;

    // --- Capability / identity (cheap, no side effects) -----------------------
    // State at construction (DevBuild / Unsupported / Idle); the controller seeds
    // its mirror from this, so the old constructor #ifdef ladder disappears.
    virtual pp::update::State initialState() const = 0;
    virtual bool supported() const = 0;
    // True when this backend drives the rich PinPoint state machine (Linux): the
    // controller's Downloading/Verifying/Ready states and progress are live.
    // False for engine-owned-UI backends (WinSparkle/Sparkle) — they pop their own
    // native UI from checkNow() and our state never leaves Idle. The controller
    // never needs a platform #ifdef; this one bit captures the difference.
    virtual bool ownsStateMachine() const = 0;

    // --- Actions (the controller's Q_INVOKABLEs delegate straight here) -------
    // Each backend guards on its own state; checkNow() on a façade backend pops the
    // native engine UI, the rest are no-ops there (base defaults below).
    virtual void checkNow()            = 0;
    virtual void download()            {}
    virtual void relaunch()            {}
    virtual void installOnNextLaunch() {}

    // Clean engine teardown on aboutToQuit (win_sparkle_cleanup / Sparkle release;
    // cancel any in-flight Linux download). Idempotent.
    virtual void shutdown()            {}

    // Keep the engine's automatic-check pref in sync with AppSettings::checkForUpdates.
    virtual void setAutomaticChecks(bool /*enabled*/) {}

signals:
    // Move to a new state. The controller maps it to the QML string and emits its
    // own stateChanged(); `error` is meaningful only for State::Error. NOTE: the
    // UpdateAvailable transition is delivered via updateOffered() instead, so the
    // controller can set latestVersion BEFORE the state flips (preserving the
    // banner's single-repaint emission order).
    void stateChanged(pp::update::State state, const QString &error);
    // A newer version is offered. The controller stores version/notes, emits
    // latestVersionChanged(), transitions to UpdateAvailable, then emits its own
    // updateOffered(version) — in that order.
    void updateOffered(const pp::update::OfferInfo &info);
    // Download progress 0..1 (or -1 indeterminate). Linux-only in practice.
    void progress(double fraction);
    // Human-readable status line for the UI log.
    void status(const QString &message);
};
