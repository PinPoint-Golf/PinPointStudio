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

// THE CAPTURE SIDE OF THE RECORDING CONTRACT (refactor step 1.5; step 1 design §4.1, §4.10;
// docs/developer/information_model.md §2): the live facts → the Recording, and the Recording → the
// shot's first document.
//
//   readCaptureAthlete(athlete, session)   the athlete and session facts capture records, read ONCE
//   snapshotAtFreeze(window, cameras, …)   the RECORDING of the shot, at the window freeze: the
//                                          sources by role, the capture facts, the ball inputs
//                                          (the accumulator snapshot, the search box, the empty-mat
//                                          baseline), the IMU bindings, the integrity inputs and
//                                          the analysis context (captureAnalysisContext(), the one
//                                          builder of the recorded context; Export/recording_header.h)
//   fillExportFromSnapshot(job, snap)      the export job's athlete, club, analysis context — and every
//                                          other Recording input it writes (each camera's placement,
//                                          search box and empty-mat baseline, the pose-quality tier,
//                                          the host IMU filter, each lane's rate and calibration) —
//                                          FROM THAT SNAPSHOT, so the file holds what the analysis used
//   writeLiveDocument(…)                   the live join's first write: the integrity verdicts into
//                                          the header, RecordingRepository::create, the shot's data
//                                          warning and its detail (the one projection)
//
// WHY AT THE FREEZE. Before step 1.5 the live analysis job read the athlete, the session and the
// settings when the window froze, and the export job read them AGAIN 12–37 s later (audit E-01): a
// club changed in between landed in the file but not in the analysis. Now both are made from one
// value, read once, here.
//
// Live analysis and re-analysis then build their job with the one builder (buildAnalysisJob) and
// run it through the analysis service; the effective context of a live shot takes the club capture
// seeds into review.club as the declared club (effective_context.h), exactly what re-analysis reads
// back — so an uncorrected shot gets the same job either way (recording_contract_test).
//
// Adapters ring (src/Gui/shot, the directory default): the only code that reads live devices,
// controllers and settings into the Recording. ShotProcessor (Application) calls it.

#include <QJsonObject>
#include <QString>
#include <QVariantMap>
#include <vector>

#include "../../Identity/identity.h"           // ShotRef: the live join's write is keyed by it (step 2.4)
#include "../../Recording/recording.h"
#include "../../Vocabulary/Analysis/swing_analysis.h"        // BallTrack2D, SwingAnalysis
#include "../../Analysis/recording_integrity.h"   // IntegrityVerdicts
#include "../../Club/club_length_fusion.h"        // LengthPriorState
#include "../../Export/capture_export_facts.h"     // the Recording's inputs the export takes from the freeze
#include <QHash>
#include "shot_outcome.h"                          // PersistPath

class AppSettings;
class AthleteController;
class CameraInstance;
class ImuManager;
class SessionController;

namespace pinpoint {
class Athlete;
class SwingWindow;
class RecordingRepository;
struct SwingExportJob;
}

namespace pinpoint::capture {

// What capture reads of the athlete and the session for one shot.
struct CaptureAthlete {
    QString     name, uuid;
    QString     handedness;      // the athlete record's spelling ("Right" / "Left" / "")
    double      heightM = 0.0;   // 0 = unknown
    QString     sex, dobIso;     // the club set on the swing's date only — never recorded (IM-7)
    QString     sessionClub;     // the session's active club; "" = none (undeclared)
    QVariantMap bag;             // the athlete's bag: club id → record (AthleteController::clubsFor)
    // No club is seeded into review.club (refactor 6.2; R-CLUB-4, DEC-67): the session's club is the CAPTURED club
    // (above), and a session with none records the shot UNDECLARED — never the athlete's primary club or a "DRIVER" stub.
};
CaptureAthlete readCaptureAthlete(const AthleteController *athlete, const SessionController *session);

// Who the athlete is, for the session folder (refactor step 3 moves this into the Session). `name` is
// the token SessionStore composes the athlete folder (and the session folder's name) from:
// AthleteController::currentSessionOwner().name — the athlete folder's own name for an athlete with
// athlete.json (D4: fixed at creation, kept on a rename), the typed name for one still in settings.
struct AthleteIdentity { QString name, uuid; };
AthleteIdentity athleteIdentity(const AthleteController *athlete);
// The current Athlete (refactor step 3.4a-ii): the one capture starts, resumes and ends its sessions through.
// Null when none is selected. Here, beside athleteIdentity, so the capture service needs no controller header.
pinpoint::Athlete *currentAthlete(const AthleteController *athlete);

// One camera with frames in the window, as the capture holds it.
struct CaptureCamera {
    CameraInstance    *ctrl   = nullptr;
    pinpoint::SourceId source = pinpoint::kInvalidSourceId;
    // The ball-detector accumulator frozen at the window, ON THE RING CLOCK, with the launch when it
    // fell inside the window. Empty = the detector saw nothing on this camera.
    pinpoint::analysis::BallTrack2D ball;
};

struct CaptureSnapshot {
    pinpoint::recording::Recording recording;   // window = the frozen window; times window-relative
    CaptureAthlete                 athlete;
    // The persistent club-length prior this shot joined (club_length_fusion.h), keyed
    // athleteUuid|club|face-on cameraKey, and only while that camera is fixed in place: what
    // ShotProcessor folds the fused length into after the analysis. Empty key = nothing to update.
    QString                              lengthPriorKey;
    pinpoint::analysis::LengthPriorState lengthPriorState;
    // The Recording's inputs as the export writes them (capture_export_facts.h), frozen here: each
    // camera's setup, search box and baseline; the host orientation filter setting; each host-fused
    // IMU lane's output rate (by its source). The export never reads the camera or the settings again.
    std::vector<pinpoint::CaptureCameraFacts> cameraFacts;
    QString                                   imuOrientationFilter;
    QHash<pinpoint::SourceId, int>            imuOutputRateHz;
};

// The Recording of the shot, at the freeze. `impactUs` is the trigger's instant on the ring clock.
// A null `settings` reads the defaults; a null `imu` binds no IMU.
CaptureSnapshot snapshotAtFreeze(const pinpoint::SwingWindow &window, const std::vector<CaptureCamera> &cameras,
                                 const ImuManager *imu, const AppSettings *settings,
                                 const CaptureAthlete &athlete, int sessionType, int shotSource,
                                 int64_t impactUs);

// The export job's athlete (name, uuid, handedness, height), its club (name, geometry, the length
// prior), its analysisContext block and the capture-wide inputs (quality, IMU filter), all from the
// snapshot. Run AFTER the job's cameras and IMU devices are listed: it overwrites their Recording facts
// (applyCaptureCameraFacts / applyCaptureImuFacts) with the frozen ones.
void fillExportFromSnapshot(pinpoint::SwingExportJob &job, const CaptureSnapshot &snapshot);

// ── The live join's document ──────────────────────────────────────────────────────────────────
struct LiveDocumentInput {
    pinpoint::RecordingRepository *repository = nullptr;
    pinpoint::ShotRef     shot;       // the key the allocation issued (step 2.4): what the write is keyed by
    QString               swingDir;   // its folder: a property (the document path reported back)
    pinpoint::PersistPath persist = pinpoint::PersistPath::None;
    // The header written: the exporter's manifest (FullDocument) or the analysis-only header from
    // the one header writer (AnalysisOnlyDocument). The verdicts are put into it here.
    QJsonObject           header;
    const pinpoint::analysis::IntegrityVerdicts *integrity = nullptr;   // null = none reached
    const pinpoint::analysis::SwingAnalysis     *analysis  = nullptr;   // null = no analysis
    int64_t               windowT0Us = 0;     // the window's start on the ring clock
    QString               capturedClub;       // the session's club at the shot, for the row ("" = undeclared); no review.club is seeded
    bool                  savePose = true;    // Storage → Save pose keypoints
};
struct LiveDocument {
    bool        wrote = false;      // a swing document was committed
    QString     error;              // why not, when a write was asked for and failed
    QJsonObject manifest;           // the header as written, with the integrity blocks
    QVariantMap dataWarningDetail;  // the shot's data warning; empty when nothing warns
    QVariantMap analysisDetail;     // the one projection of the analysis block the document got
    QString     club;               // the club the document records (review.club)
    QString     documentPath;       // where it was written (for the log)
};
// Runs off the GUI thread (the document is ~28 MB on a wrist swing).
LiveDocument writeLiveDocument(const LiveDocumentInput &in);

} // namespace pinpoint::capture
