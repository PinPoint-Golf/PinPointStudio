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

#include "trunk_ceremony_hand_over.h"

#include "../Core/pp_debug.h"
#include "../DomainModel/rig.h"
#include "../DomainModel/session.h"

#include <QElapsedTimer>

namespace pinpoint {

TrunkCeremonyHandOver::TrunkCeremonyHandOver(std::function<Session *()> openSession, Rig *rig, QObject *parent)
    : QObject(parent), m_openSession(std::move(openSession)), m_rig(rig)
{
}

int TrunkCeremonyHandOver::owed() const { return int(m_owed.size()); }

void TrunkCeremonyHandOver::take(const QByteArray &bytes, const QList<TrunkCeremonySolve> &solves)
{
    Session *s = m_openSession ? m_openSession() : nullptr;
    m_owed.append({ bytes, solves, s ? s->id() : SessionId() });
    if (!s) {
        // The session started without its folder (it is made at the first swing): kept for it.
        ppWarn() << "[TrunkCeremony] the session has no folder yet - the trunk calibration's recording is kept until it has one";
        return;
    }
    retry();
}

void TrunkCeremonyHandOver::retry()
{
    if (m_owed.isEmpty()) return;
    Session *s = m_openSession ? m_openSession() : nullptr;
    if (!s) return;
    const QString why = writeInto(s);
    if (!why.isEmpty())
        emit notSaved(why);
}

QString TrunkCeremonyHandOver::writeInto(Session *s)
{
    bool wrote = false;
    while (!m_owed.isEmpty()) {
        Owed &o = m_owed.first();
        if (o.session.isNull())
            o.session = s->id();   // the session that was starting when it was taken, now that it has a folder
        if (o.session != s->id()) {
            // Its own session is no longer the one running: a calibration means something only for its own session,
            // so it is never written into another (the owner's ruling, 27 Nov 2026).
            ppWarn() << "[TrunkCeremony] the trunk calibration's recording for session" << o.session.toString()
                     << "was never saved, and that session is no longer running - it is lost, not written into"
                     << s->id().toString();
            m_owed.removeFirst();
            emit notSaved(QStringLiteral("The trunk calibration of an earlier session was never saved."));
            continue;
        }
        QElapsedTimer t;
        t.start();
        const TrunkCeremonyWrite w = s->saveTrunkCeremony(o.bytes);
        const qint64 ms = t.elapsed();
        if (!w.ok()) {
            ppWarn() << "[TrunkCeremony] the trunk calibration's recording was not saved into session" << s->id().toString()
                     << "after" << ms << "ms - kept, to try again:" << w.result.error;
            return w.result.error.isEmpty() ? QStringLiteral("The trunk calibration could not be saved.") : w.result.error;
        }
        ppInfo() << "[TrunkCeremony] wrote" << w.fileName << "into session" << s->id().toString() << "-"
                 << qPrintable(QString::number(double(o.bytes.size()) / 1e6, 'f', 2)) << "MB in" << ms << "ms"
                 << (w.asideName.isEmpty() ? "" : "; the earlier one kept as") << w.asideName;
        const Owed done = m_owed.takeFirst();
        tellRig(done.session, w.fileName, w.asideName, done.solves);
        wrote = true;
    }
    if (wrote)
        emit saved();
    return {};
}

void TrunkCeremonyHandOver::sessionEnding()
{
    if (m_owed.isEmpty()) return;
    Session *s = m_openSession ? m_openSession() : nullptr;
    QString why = s ? writeInto(s) : QStringLiteral("The session's folder was never made.");
    if (m_owed.isEmpty()) return;
    dropAll("its session ended");
    emit notSaved(QStringLiteral("The session ended without its trunk calibration saved: %1").arg(why));
}

void TrunkCeremonyHandOver::quitting()
{
    if (m_owed.isEmpty()) return;
    if (Session *s = m_openSession ? m_openSession() : nullptr)
        writeInto(s);
    if (!m_owed.isEmpty())
        dropAll("the app is quitting");
}

void TrunkCeremonyHandOver::dropAll(const char *why)
{
    for (const Owed &o : m_owed)
        ppWarn() << "[TrunkCeremony] the trunk calibration's recording was never saved -" << why << "- it is lost"
                 << qPrintable(o.session.isNull() ? QStringLiteral("(no session folder was made)")
                                                  : QStringLiteral("(session %1)").arg(o.session.toString()));
    m_owed.clear();
}

void TrunkCeremonyHandOver::tellRig(const SessionId &session, const QString &fileName, const QString &asideName,
                                    const QList<TrunkCeremonySolve> &solves)
{
    if (!m_rig || fileName.isEmpty()) return;
    // A record pointing at the earlier file this write copied aside follows it to its new name.
    // Collected first, then set: attachEvidence changes the list being read.
    if (!asideName.isEmpty()) {
        QList<QPair<rig::PlacementKey, QDateTime>> moved;
        for (const rig::SensorSetup &x : m_rig->parts().state.sensors)
            if (x.calibration && x.calibration->evidence && x.calibration->evidence->sessionId == session
                && x.calibration->evidence->fileName == fileName)
                moved.append({ x.key, x.calibration->observedAtUtc });
        for (const auto &m : moved)
            m_rig->attachEvidence(m.first, m.second, { session, asideName });
    }
    for (const TrunkCeremonySolve &solve : solves)
        if (!m_rig->attachEvidence(rig::PlacementKey(rig::DeviceKey::fromPersistedId(solve.deviceId)), solve.observedAtUtc,
                                   { session, fileName }))
            ppInfo() << "[TrunkCeremony] no alignment record in force for" << solve.deviceId
                     << "solved at" << solve.observedAtUtc.toString(Qt::ISODateWithMs) << "- no pointer set";
}

} // namespace pinpoint
