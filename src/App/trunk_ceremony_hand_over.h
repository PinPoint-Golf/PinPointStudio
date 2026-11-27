/*
 * Copyright (C) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#pragma once

// TrunkCeremonyHandOver: THE TRUNK CALIBRATION'S RAW RECORDING, HANDED TO ITS SESSION (FILE-SESS-11; platform refactor
// C-14, #380, which fixes #265; information_model.md FILE-SESS-11; lifecycle_model.md SES-S1, SES-S2, GU-18).
//
// The trunk ceremony recorder (src/Gui/imu/trunk_ceremony_recorder.h) records the ceremony and holds it until a session
// runs; then it hands the ceremony's bytes here (take) and forgets it. From then on the ceremony belongs to THAT session:
// this object writes it through the running Session (Session::saveTrunkCeremony → SessionRepository), synchronously on
// the GUI thread (the owner's ruling, 27 Nov 2026), and logs how long each write took.
//
// NO CEREMONY IS LOST SILENTLY (#265; the rulings of 26 Nov 2026, morning batch 4, and 27 Nov 2026):
//   - it is kept until a write succeeds: a failed write keeps it, and it is tried again within its own session —
//     when the session's folder appears (retry), just before the session ends (sessionEnding) and at quit (quitting);
//   - every failure is logged, and a failed write into a running session is reported (notSaved; main.cpp posts the
//     same kind of notice as a swing that cannot be saved): if the calibration cannot be saved, nothing else will be;
//   - it is never written into a later session: the calibration means something only for its own. If its session ends,
//     or the app quits, with it still unwritten, it is dropped and the loss is logged.
//
// THE RIG'S POINTER (rig_design_addendum.md decision 2; ruled 26 Nov 2026): after a good write, each trunk sensor's
// alignment record this ceremony solved (its stand-tall `calibratedAt` is the record's observed time) is pointed at the
// file (Rig::attachEvidence). A record pointing at the earlier file this write copied aside follows it to its new name.
// A failed write sets no pointer.
//
// Owner thread only. Application ring: it composes a Session and the Rig, and holds no file name.

#include "../Vocabulary/Identity/identity.h"

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>

#include <functional>

namespace pinpoint {

class Rig;
class Session;

// A trunk sensor this ceremony solved: its device id (the PlacementKey's) and the stand-tall solve's `calibratedAt`.
struct TrunkCeremonySolve {
    QString   deviceId;
    QDateTime observedAtUtc;
};

class TrunkCeremonyHandOver : public QObject
{
    Q_OBJECT
public:
    // `openSession`: the session running now, or null (none, or its folder not made yet). `rig`: may be null (no pointer).
    TrunkCeremonyHandOver(std::function<Session *()> openSession, Rig *rig, QObject *parent = nullptr);

    // The recorder's hand-over, while a session runs: the ceremony's bytes (tcf::toJson) and what it solved. Written at
    // once when the session can take it; else kept for it.
    void take(const QByteArray &bytes, const QList<TrunkCeremonySolve> &solves);
    // How many ceremonies are taken and not yet written (0 or 1, and 2 only after a re-check mid-session).
    int owed() const;

    // Try again: the running session's folder appeared (ShotProcessor::activeSessionDirChanged).
    void retry();
    // The running session is about to end: a last try, then whatever is still owed is dropped, logged.
    void sessionEnding();
    // The app is quitting: a last try into the running session, then whatever is still owed is dropped, logged.
    void quitting();

signals:
    // A write into the running session failed: why, in the store's words. Its session is still running.
    void notSaved(const QString &detail);
    // Everything owed is written.
    void saved();

private:
    struct Owed {
        QByteArray                bytes;
        QList<TrunkCeremonySolve> solves;
        SessionId                 session;   // the session it belongs to; null until one is open to take it
    };

    // Writes what is owed into `s`, in the order taken, dropping (logged, reported) any owed to another session; stops
    // at the first failure and returns why ("" when nothing is left owed).
    QString writeInto(Session *s);
    void tellRig(const SessionId &session, const QString &fileName, const QString &asideName,
                 const QList<TrunkCeremonySolve> &solves);
    void dropAll(const char *why);

    std::function<Session *()> m_openSession;
    Rig                       *m_rig = nullptr;
    QList<Owed>                m_owed;
};

} // namespace pinpoint
