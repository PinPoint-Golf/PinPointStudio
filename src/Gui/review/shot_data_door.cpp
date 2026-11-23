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

#include "shot_data_door_impl.h"

#include "../../DomainModel/athlete.h"
#include "../../DomainModel/recording_source.h"
#include "../../DomainModel/session.h"
#include "../../DomainModel/shot.h"
#include "../../Buffer/imu_sample.h"
#include "../../Buffer/swing_window.h"

#include <QFileInfo>
#include <QPointer>

#include <algorithm>
#include <cstring>

namespace pinpoint::review {

ShotDataRead shotDataOf(const LoadedRecording &loaded)
{
    ShotDataRead out;
    out.ok    = loaded.documentRead;
    out.error = loaded.error;
    if (!loaded.documentRead)
        return out;
    out.analysis     = loaded.analysisIn;
    out.originUs     = loaded.analysisOriginUs;
    out.windowEndUs  = std::max<qint64>(0, qint64(loaded.recording.integrity.windowEndUs));
    out.hasSchema    = loaded.hasSchema;
    out.schema       = loaded.schema;
    out.hasWallclock = loaded.hasWallclock;
    out.wallclock    = loaded.wallclock;
    out.capture      = loaded.captureIn;

    const SwingWindow *window = loaded.window.get();
    out.streams.reserve(loaded.storedStreams.size());
    for (const LoadedRecording::StoredStream &d : loaded.storedStreams) {
        ShotDataStream s;
        s.kind = d.kind;
        s.alias = d.alias;
        s.source = d.source;
        s.device = d.device;
        s.setup = d.setup;
        s.capture = d.capture;
        s.encoded = d.encoded;
        s.hasSampleCount = d.hasSampleCount;
        s.sampleCount = d.sampleCount;
        if (window && d.lane != kInvalidSourceId) {
            // The lane's samples as recorded: the stored frame is the ImuSample (imu_sample.h), copied out of the window.
            s.imuLane = true;
            const std::vector<IndexEntry> entries = window->entriesFor(d.lane);
            s.t.reserve(int(entries.size()));
            s.gyroZ.reserve(int(entries.size()));
            s.quat.reserve(int(entries.size()));
            for (const IndexEntry &e : entries) {
                const SourceRing::ReadHandle h = window->payloadOf(e);
                if (!h.data || h.bytes != sizeof(ImuSample))
                    continue;
                ImuSample m{};
                std::memcpy(&m, h.data, sizeof(ImuSample));
                s.t.push_back(qint64(e.timestamp_us));
                s.gyroZ.push_back(double(m.gyro_z));
                s.quat.push_back({ double(m.quat_w), double(m.quat_x), double(m.quat_y), double(m.quat_z) });
            }
        }
        out.streams.push_back(std::move(s));
    }

    for (const analysis::BallSample2D &b : loaded.recording.ball.track.frames) {
        out.ball.t.push_back(qint64(b.t_us));
        out.ball.found.push_back(b.found);
        out.ball.x.push_back(b.center.x());
        out.ball.y.push_back(b.center.y());
        out.ball.r.push_back(double(b.radiusNorm));
        out.ball.conf.push_back(double(b.conf));
    }
    return out;
}

ShotDataResolver makeShotDataResolver()
{
    return [](QObject *sessionObject, const QString &swingDir, QObject *follower, std::function<void()> changed) {
        ShotDataHandle h;
        auto *session = qobject_cast<Session *>(sessionObject);
        if (!session || swingDir.isEmpty())
            return h;
        Shot *shot = session->shotAtFolder(swingDir);   // the edge translation (DEC-90)
        if (!shot)
            return h;
        h.held = true;
        const QPointer<Shot>    sp(shot);
        const QPointer<Session> ss(session);
        h.facts = [sp, ss] {
            ShotDataFacts f;
            if (!sp || !ss) return f;
            f.swingName = QFileInfo(sp->folder()).fileName();                 // the Shot's folder, a property
            if (const Athlete *a = ss->athlete()) {                           // OBJ-ATHLETE-RECORD
                f.hasAthlete  = true;
                f.athleteName = a->record().name;
                f.handedness  = a->record().handedness;
            }
            if (const auto e = ss->entry()) {                                 // OBJ-SESSION-IDENTITY
                f.hasSession  = true;
                f.sessionName = e->identity.folderName();
            }
            const ShotSummary &review = sp->summary();                        // OBJ-SHOT-REVIEW
            f.rating = review.rating;
            f.note   = review.note;
            return f;
        };
        const RecordingSource source = shot->recordingSource();               // a value: the ShotRef and its reader
        h.read = [source] { return shotDataOf(source.read()); };
        if (follower && changed) {
            // Direct, as every listener of a domain event (work_pool.h): heard in command order, on the GUI thread.
            QObject::connect(shot, &Shot::reanalysed, follower,
                             [changed](const ShotRef &) { changed(); }, Qt::DirectConnection);      // EVT-02
            QObject::connect(shot, &Shot::captureCompleted, follower,
                             [changed](const ShotRef &) { changed(); }, Qt::DirectConnection);      // EVT-22
        }
        return h;
    };
}

} // namespace pinpoint::review
