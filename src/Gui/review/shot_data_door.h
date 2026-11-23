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

// THE REVIEW VIEWS' DOOR TO A SHOT (refactor 5.8a; build/refactor/step-4/LOCAL-COPIES.md row 10). The data viewer
// (SwingDataSource) and the 3-D rig (SwingRigDriver) are presenters over the Shot: they read what they show from its
// owners, never from the swing file, and follow the Shot's events, so a re-analysis shows at once. What each holds is a
// derived display cache (Product Owner, 23 Nov 2026): filled from here, refreshed on the Shot's events, never written
// back, never a source for anything else.
//
// EACH FACT FROM ITS OWNER (the Solution Architect, 23 Nov 2026): the athlete's from the Athlete's record
// (OBJ-ATHLETE-RECORD), the session's from the Session's identity, the review's from the Shot (OBJ-SHOT-REVIEW), and
// the analysis, the capture block, the schema, the clock, the window and the streams from the Shot's Recording, read
// through its RecordingSource (OBJ-RECORDING, OBJ-ANALYSIS). No whole-document read.
//
// WHY A DOOR: the two presenters are QML types of the module, which the QML suites compile without the domain model.
// The module holds only this header; the composition root installs the one implementation (shot_data_door.cpp, the
// app's), which resolves the Shot at its edge through the Session a view is given (`app.session`), as the replay's
// resolver does (main.cpp). With no door installed (a QML suite, a tool) a presenter shows nothing.
//
// Values only: a read is made on the GUI thread, carried to a worker (the charts' lane, R-CON-5), and its result applied
// back on the GUI thread.

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVector>

#include <array>
#include <functional>
#include <vector>

namespace pinpoint::review {

// One stored stream as the Recording describes it, with an IMU lane's samples as recorded.
struct ShotDataStream {
    QString     kind;                                // "video" | "imu" | "ball" | …
    QString     alias;
    QJsonObject source, device, setup, capture, encoded;   // the stream's own blocks ({} when absent)
    bool        hasSampleCount = false;
    int         sampleCount = 0;
    // An IMU lane read into the Recording's window: its samples, window-relative.
    bool                            imuLane = false;
    QVector<qint64>                 t;
    QVector<double>                 gyroZ;           // deg/s
    QVector<std::array<double, 4>>  quat;            // w, x, y, z
};

// The recorded ball stream (the Recording's ball inputs), for a swing whose analysis has no ball block.
struct ShotDataBall {
    QVector<qint64> t;
    QVector<bool>   found;
    QVector<double> x, y, r, conf;
};

// What one read of the Shot's Recording gives a review view (any thread).
struct ShotDataRead {
    bool        ok = false;            // the document was read
    QString     error;
    QJsonObject analysis;              // the analysis block (OBJ-ANALYSIS)
    qint64      originUs = 0;          // the analysis' read origin (windowTimeUs)
    qint64      windowEndUs = 0;       // the capture window's end, window-relative (0: not recorded)
    bool        hasSchema = false;
    QString     schema;
    bool        hasWallclock = false;
    QString     wallclock;
    QJsonObject capture;               // the capture block: its settings and host
    std::vector<ShotDataStream> streams;
    ShotDataBall ball;
};

// What the owners say now (the GUI thread).
struct ShotDataFacts {
    QString swingName;                 // the Shot's folder name ("swing_0003")
    bool    hasAthlete = false;        // the session's Athlete, and its record
    QString athleteName, handedness;
    bool    hasSession = false;
    QString sessionName;               // the Session's folder name
    int     rating = 0;                // the Shot's review
    QString note;
};

// A shot a view is pointed at, as the door resolved it.
struct ShotDataHandle {
    bool                           held = false;   // a Session the view was given holds a Shot at that folder
    std::function<ShotDataFacts()> facts;          // GUI thread
    std::function<ShotDataRead()>  read;           // any thread: the Shot's Recording
};

// (session, swingDir, follower, changed): the Shot the Session holds at `swingDir`. While `follower` lives, `changed`
// is called on the GUI thread when that Shot is re-analysed (EVT-02) or completed (EVT-22).
using ShotDataResolver = std::function<ShotDataHandle(QObject *session, const QString &swingDir, QObject *follower,
                                                      std::function<void()> changed)>;

inline ShotDataResolver &shotDataResolverSlot()
{
    static ShotDataResolver resolver;
    return resolver;
}
// The composition root's half: main.cpp installs the app's door (makeShotDataResolver, shot_data_door.cpp).
inline void installShotDataResolver(ShotDataResolver r) { shotDataResolverSlot() = std::move(r); }
inline const ShotDataResolver &shotDataResolver() { return shotDataResolverSlot(); }

} // namespace pinpoint::review
