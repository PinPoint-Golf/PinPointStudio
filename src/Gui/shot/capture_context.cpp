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

#include "capture_context.h"

#include "app_settings.h"
#include "athlete_controller.h"
#include "camera_instance.h"
#include "hm_instance.h"
#include "imu_instance.h"
#include "imu_manager.h"
#include "session_controller.h"
#include "../../Buffer/swing_window.h"
#include "../../Buffer/format_descriptor.h"
#include "../../Vocabulary/Club/club_vocabulary.h"
#include "../../Core/pp_debug.h"
#include "../../DomainModel/athlete.h"   // isUnassigned: a capture into Unassigned records no golfer (J-67)
#include "../../DomainModel/recording_repository.h"
#include "../../Export/recording_header.h"   // captureAnalysisContext — the one builder of the context
#include "../../Export/swing_doc.h"          // the integrity blocks, the data warning, the one projection
#include "../../Export/swing_exporter.h"     // SwingExportJob
#include "../../Export/swing_store.h"        // the document's path, for the log
#include "../../IMU/hm_frame.h"              // isSelected() — no frame, no binding

#include <QDate>
#include <QDateTime>
#include <QSet>

#include <algorithm>
#include <variant>

namespace pinpoint::capture {

namespace rec = pinpoint::recording;
using pinpoint::analysis::BallTrack2D;
using pinpoint::analysis::ImuSegmentBinding;

// ── The athlete and the session ───────────────────────────────────────────────────────────────

CaptureAthlete readCaptureAthlete(const AthleteController *athlete, const SessionController *session)
{
    CaptureAthlete a;
    // A capture into the Unassigned owner records NO golfer (J-67; information_model.md §5.11): its swing header's athlete
    // block stays empty, as with no golfer chosen — whether or not Unassigned is the athlete picked — so no handedness,
    // height, cohort, bag or club-length prior of "Unassigned" is ever recorded or learned.
    const pinpoint::Athlete *picked = athlete ? athlete->currentAthlete() : nullptr;
    if (athlete && !(picked && picked->isUnassigned())) {
        a.name       = athlete->currentName();
        a.uuid       = athlete->currentUuid();
        a.handedness = athlete->currentHandedness();
        a.heightM    = athlete->currentHeightM();
        a.sex        = athlete->currentSex();
        a.dobIso     = athlete->currentDob();
        a.bag        = athlete->clubsFor(a.uuid);
    }
    // The session's club is the captured club. Nothing is seeded into review.club (refactor 6.2): a session with no club
    // records the shot undeclared, and its findings fire as "club assumed" (R-CLUB-4).
    if (session)
        a.sessionClub = session->activeClub();
    return a;
}

AthleteIdentity athleteIdentity(const AthleteController *athlete)
{
    // The token the session store composes the athlete's folder from (refactor step 2.3): the folder's
    // own name for an athlete with athlete.json — chosen once, kept on a rename (D4) — and today's name
    // for an athlete still in settings (until M1).
    return athlete ? AthleteIdentity{ athlete->currentSessionOwner().name, athlete->currentUuid() } : AthleteIdentity{};
}

pinpoint::Athlete *currentAthlete(const AthleteController *athlete)
{
    return athlete ? athlete->currentAthlete() : nullptr;
}

namespace {

// The IMU → segment bindings, resolved from the live instances: the Witmotion lanes through the
// role resolver (no sessionType gate: a pelvis sensor is a pelvis sensor in every session type), then
// the HackMotion units, which the first pass cannot see.
std::vector<ImuSegmentBinding> liveBindings(const ImuManager *imuManager,
                                            const std::vector<pinpoint::SourceId> &lanes)
{
    std::vector<ImuSegmentBinding> out;
    if (!imuManager)
        return out;
    const auto inWindow = [&lanes](pinpoint::SourceId sid) {
        return std::find(lanes.begin(), lanes.end(), sid) != lanes.end();
    };
    const QVariantList insts = imuManager->instances();
    for (const QVariant &v : insts) {
        auto *imu = qobject_cast<ImuInstance *>(v.value<QObject *>());
        if (!imu) continue;
        const pinpoint::SourceId sid = imu->sourceId();
        if (!inWindow(sid))
            continue;   // this IMU is not a source in the captured window
        ImuSegmentBinding b;
        b.source = sid;
        b.role   = pinpoint::analysis::segmentRoleForPlacementRole(imuManager->roleForDevice(imu->deviceId()));
        b.alignA = imu->alignA();
        b.mountM = imu->mountM();
        // Calibration status snapshot — persisted into swing.json's analysis.bindings so SwingLab can
        // filter by provenance.
        b.anatCalibrated       = imu->anatCalibrated();
        b.calibrated           = imu->fullyCalibrated();
        b.mountDeviationDeg    = imu->mountDeviationDeg();
        b.mountGravityErrorDeg = imu->mountGravityErrorDeg();
        if (imu->calibratedAtUtc().isValid()) {
            b.calibratedAtUtc = imu->calibratedAtUtc().toString(Qt::ISODateWithMs);
            b.calibAgeSec     = imu->calibratedAtUtc().msecsTo(QDateTime::currentDateTimeUtc()) / 1000.0;
        }
        // The trunk stand-tall solve's δ tier, σ_δ and gate residuals, when it set A/M (trunk design
        // §6.8); not present for an arm calibration.
        b.trunkCalib = imu->trunkCalibProvenance();
        out.push_back(b);
    }

    // ── HackMotion, which the loop above cannot see ──────────────────────────────────────────
    // An HmInstance is a PEER of ImuInstance, not a subclass, so the cast above returns null for it.
    for (const QVariant &v : insts) {
        auto *hm = qobject_cast<HmInstance *>(v.value<QObject *>());
        if (!hm) continue;
        // ⚠ NO FRAME, NO BINDING. Until a directed capture has selected a candidate,
        // hm_frame::toAnatomical() returns identity by design: a lane with no anatomical frame must
        // drive nothing rather than drive a plausible guess.
        if (!pinpoint::hm_frame::isSelected()) {
            ppWarn() << "[ShotProcessor]" << hm->deviceDescription()
                     << "— no HackMotion frame candidate selected, so this device's"
                        " lanes are recorded but not bound; no wrist metric will be"
                        " produced from them.";
            continue;
        }
        HmUnit *const units[] = { hm->unitLowerArm(), hm->unitPalm() };
        for (HmUnit *const unit : units) {
            if (!unit) continue;
            const pinpoint::SourceId sid = unit->sourceId();
            if (sid == pinpoint::kInvalidSourceId || !inWindow(sid))
                continue;   // no lane, or not a source in the captured window
            // ⚠ PLACEMENT IS UNIT-KEYED FOR A HACKMOTION: ask the canonical resolver which object
            // holds each role and match on identity, rather than respell "<deviceId>#lowerArm".
            QString role;
            for (const QString &r : pinpoint::imu_roles::placementRoleOrder()) {
                if (imuManager->instanceForRole(r) == static_cast<QObject *>(unit)) {
                    role = r;
                    break;
                }
            }
            if (role.isEmpty())
                continue;   // unassigned unit — recorded, but bound to no segment
            ImuSegmentBinding b;
            b.source     = sid;
            b.role       = pinpoint::analysis::segmentRoleForPlacementRole(role);
            b.hackMotion = true;
            // A is IDENTITY for this lane by design; M carries the frame constant. Read off the unit
            // so the binding and the live readout cannot diverge. The mount deviations stay 0: there
            // is no mount solve for this device.
            b.alignA = unit->alignA();
            b.mountM = unit->mountM();
            b.anatCalibrated = unit->anatCalibrated();
            b.calibrated     = unit->anatCalibrated();
            out.push_back(b);
        }
    }
    return out;
}

// The club's geometry from its bag record, in the export job's fields (the same reads the old live
// builder and the old export job each made).
void clubFromRecord(const QVariantMap &rec, pinpoint::SwingExportJob &job)
{
    const int lengthMm = rec.value(QStringLiteral("lengthMm")).toInt();
    if (lengthMm > 0)
        job.clubLengthM = lengthMm / 1000.0;
    job.shaftType = rec.value(QStringLiteral("shaftType")).toString();
    job.bandCentersMm.clear();
    for (const QVariant &bv : rec.value(QStringLiteral("bandCentersMm")).toList())
        job.bandCentersMm.push_back(bv.toDouble());
    const double hoselMm = rec.value(QStringLiteral("hoselFromButtMm")).toDouble();
    if (hoselMm > 0)
        job.hoselFromButtMm = hoselMm;
    job.shaftLengthMm = rec.value(QStringLiteral("shaftLengthMm")).toDouble();
    job.handsEndMm    = rec.value(QStringLiteral("handsEndMm")).toDouble();
}

} // namespace

// ── The Recording, at the freeze ──────────────────────────────────────────────────────────────

CaptureSnapshot snapshotAtFreeze(const pinpoint::SwingWindow &window, const std::vector<CaptureCamera> &cameras,
                                 const ImuManager *imuManager, const AppSettings *settingsIn,
                                 const CaptureAthlete &athlete, int sessionType, int shotSource,
                                 int64_t impactUs)
{
    AppSettings  fallback;
    const AppSettings &settings = settingsIn ? *settingsIn : fallback;

    CaptureSnapshot snap;
    snap.athlete = athlete;
    rec::Recording &r = snap.recording;
    r.window = &window;
    r.t0Us   = window.startTimestampUs();
    const int64_t t0 = r.t0Us;

    // ── Each camera's facts, read ONCE, here: what the Recording carries and the export writes ──
    const QVariantMap fixedInPlace = settings.cameraFixedInPlace();
    for (const CaptureCamera &c : cameras) {
        pinpoint::CaptureCameraFacts f;
        f.source       = c.source;
        f.perspective  = int(c.ctrl->perspective());
        f.mirrored     = c.ctrl->isMirrored();
        f.fixedInPlace = fixedInPlace.value(c.ctrl->cameraKey()).toBool();
        f.searchRoi    = c.ctrl->roi();
        // The baseline the live detector learned BEFORE the shot. It re-learns over the empty mat after
        // impact, during the analysis — so the export must write this copy, not read the camera again.
        const auto &bl = c.ctrl->ballBaseline();
        if (bl.valid()) {
            f.baselineBlob   = bl.blob;
            f.baselineW      = bl.w;
            f.baselineH      = bl.h;
            f.baselineRoi    = bl.roi;
            f.baselineRHat   = bl.rHat;
            f.baselineFps    = bl.fps;
            f.baselineNoise0 = bl.noise0;
        }
        snap.cameraFacts.push_back(f);
    }
    snap.imuOrientationFilter = settings.imuOrientationFilter();

    // ── Sources by role: every camera with frames, in capture order, through the one mapping ──
    for (const pinpoint::CaptureCameraFacts &f : snap.cameraFacts) {
        rec::CameraStream s;
        s.source       = f.source;
        s.perspective  = rec::perspectiveFromCode(f.perspective);
        s.mirrored     = f.mirrored;
        s.fixedInPlace = f.fixedInPlace;
        r.sources.cameras.push_back(s);
    }
    // The IMU lanes, from the window's own formats (a marker lane is not one).
    bool hostFusedLane = false;
    {
        QSet<pinpoint::SourceId> seen;
        for (const pinpoint::IndexEntry &e : window.entries()) {
            if (seen.contains(e.source_id)) continue;
            seen.insert(e.source_id);
            const pinpoint::FormatDescriptor &fd = window.formatOf(e.source_id);
            if (fd.device == pinpoint::DeviceKind::Marker_App)
                continue;
            if (const auto *imf = std::get_if<pinpoint::ImuFormat>(&fd.format)) {
                r.sources.imuLanes.push_back(e.source_id);
                if (imf->device != pinpoint::DeviceKind::IMU_HackMotion)
                    hostFusedLane = true;
            }
        }
    }

    // ── Capture facts ──
    r.capture.sessionType          = sessionType;
    r.capture.shotSource           = shotSource;
    r.capture.impactUs             = impactUs >= 0 ? impactUs - t0 : -1;
    r.capture.motionCaptureQuality = settings.motionCaptureQuality();

    // ── Ball inputs ──
    // The stream: the face-on camera's accumulator snapshot, else any camera's (detection enabled
    // mid-swing, an unusual rig). Window-relative here; the job builder adds t0Us back.
    for (int pass = 0; pass < 2 && r.ball.track.frames.empty(); ++pass) {
        for (const CaptureCamera &c : cameras) {
            if (pass == 0 && c.ctrl->perspective() != CameraInstance::FaceOn)
                continue;
            if (c.ball.frames.empty())
                continue;
            BallTrack2D bt = c.ball;
            bt.camera = c.source;
            for (pinpoint::analysis::BallSample2D &s : bt.frames)
                s.t_us -= t0;
            if (bt.launchTUs >= 0)
                bt.launchTUs -= t0;
            r.ball.track = std::move(bt);
            break;
        }
    }
    // The search box and the empty-mat baseline: the face-on camera's frozen facts, read as the
    // Recording loader reads them back (the last face-on stream that has one).
    for (const pinpoint::CaptureCameraFacts &f : snap.cameraFacts) {
        if (f.perspective != int(CameraInstance::FaceOn))
            continue;
        if (!f.searchRoi.isEmpty())
            r.ball.searchRoi = f.searchRoi;
        // The baseline carried IN MEMORY: the export writes these same bytes (fillExportFromSnapshot)
        // to the swing folder after the analysis. D5 makes it authoritative on this path too.
        if (f.baselineValid() && !f.baselineRoi.isEmpty()) {
            pinpoint::analysis::BallBaselineRef ref;
            ref.bytes  = f.baselineBlob;
            ref.w      = f.baselineW;
            ref.h      = f.baselineH;
            ref.roi    = f.baselineRoi;
            ref.rHat   = f.baselineRHat;
            ref.fps    = f.baselineFps;
            ref.noise0 = f.baselineNoise0;
            r.ball.baseline = std::move(ref);
        }
    }

    // ── IMU bindings, and each host-fused lane's output rate as it stood ──
    r.imuBindings = liveBindings(imuManager, r.sources.imuLanes);
    if (imuManager)
        for (const QVariant &v : imuManager->instances())
            if (auto *imu = qobject_cast<ImuInstance *>(v.value<QObject *>()))
                snap.imuOutputRateHz.insert(imu->sourceId(), imu->outputRateHz());

    // ── Integrity inputs ── what the integrity rule judges against: the host filter the
    // host-fused lanes are fused with (what the exporter records on each), the frozen window.
    r.integrity.hostOrientationFilter = hostFusedLane ? snap.imuOrientationFilter : QString();
    r.integrity.windowStartUs = 0;
    r.integrity.windowEndUs   = window.endTimestampUs() - t0;

    // ── The persistent club-length prior (club_length_fusion.h) ──
    // Keyed athleteUuid|clubName|cameraKey (a px prior is meaningless if the camera moves) and only
    // trusted while that camera is marked fixed in place.
    pinpoint::SwingExportJob ctxJob;   // the fields captureAnalysisContext reads
    ctxJob.handedness     = athlete.handedness;
    ctxJob.athleteHeightM = athlete.heightM;
    if (!athlete.sessionClub.isEmpty()) {
        ctxJob.clubName = athlete.sessionClub;
        clubFromRecord(athlete.bag.value(athlete.sessionClub).toMap(), ctxJob);

        const CameraInstance *faceOnCtrl = nullptr;
        for (const CaptureCamera &c : cameras)
            if (c.ctrl && c.ctrl->perspective() == CameraInstance::FaceOn) { faceOnCtrl = c.ctrl; break; }
        const QString faceOnKey = faceOnCtrl ? faceOnCtrl->cameraKey() : QString();
        if (!faceOnKey.isEmpty() && fixedInPlace.value(faceOnKey).toBool()) {
            // Stashed even on a cold/absent entry — the update after the analysis still needs the
            // key to seed a brand-new prior on the first confident fuse.
            snap.lengthPriorKey = athlete.uuid + QLatin1Char('|') + athlete.sessionClub + QLatin1Char('|') + faceOnKey;
            const QVariantMap prior = settings.clubLenPrior().value(snap.lengthPriorKey).toMap();
            if (!prior.isEmpty()) {
                const int entryW = prior.value(QStringLiteral("frameW"), 0).toInt();
                const int entryH = prior.value(QStringLiteral("frameH"), 0).toInt();
                // Camera-move self-heal: an entry with recorded dims must match the live camera's
                // frame size; one that predates the field is trusted as it is.
                const bool dimsOk = (entryW <= 0 || entryH <= 0)
                    || (entryW == faceOnCtrl->frameWidth() && entryH == faceOnCtrl->frameHeight());
                if (dimsOk) {
                    auto &st = snap.lengthPriorState;
                    st.emaPx       = prior.value(QStringLiteral("emaPx"), -1.0).toDouble();
                    st.varPx       = prior.value(QStringLiteral("varPx"), 0.0).toDouble();
                    st.n           = prior.value(QStringLiteral("n"), 0).toInt();
                    st.disagreeRun = prior.value(QStringLiteral("disagreeRun"), 0).toInt();
                    if (st.n > 0 && st.emaPx > 0.0) {
                        ctxJob.priorClubLenPx    = st.emaPx;
                        ctxJob.priorClubLenVarPx = st.varPx;
                        ctxJob.priorClubLenN     = st.n;
                    }
                }
            }
        }
    }

    // ── The analysis context recorded (IM-1): the one builder, on the swing's date ──
    r.context = pinpoint::captureAnalysisContext(ctxJob, athlete.sex, athlete.dobIso, QDate::currentDate(),
                                                 athlete.bag);
    return snap;
}

void fillExportFromSnapshot(pinpoint::SwingExportJob &job, const CaptureSnapshot &snap)
{
    const CaptureAthlete &a = snap.athlete;
    job.athleteName    = a.name;
    job.athleteUuid    = a.uuid;
    job.handedness     = a.handedness;
    job.athleteHeightM = a.heightM;
    // capture.club: the club the session was on, its geometry and the prior the live fuse used —
    // the same values the analysis context records, from the same read.
    const rec::RecordedClub &club = snap.recording.context.club;
    if (!a.sessionClub.isEmpty())
        clubFromRecord(a.bag.value(a.sessionClub).toMap(), job);
    job.clubName = a.sessionClub;
    if (club.lengthPrior.present()) {
        job.priorClubLenPx    = club.lengthPrior.px;
        job.priorClubLenVarPx = club.lengthPrior.varPx;
        job.priorClubLenN     = club.lengthPrior.n;
    }
    job.analysisContext = snap.recording.context;

    // Every other Recording input the export writes, from the freeze (capture_export_facts.h).
    for (pinpoint::SwingExportCamera &cam : job.cameras)
        if (const pinpoint::CaptureCameraFacts *f = pinpoint::captureCameraFactsFor(snap.cameraFacts, cam.sourceId))
            pinpoint::applyCaptureCameraFacts(cam, *f);
    pinpoint::applyCaptureSettings(job, snap.recording.capture.motionCaptureQuality, snap.imuOrientationFilter);
}

// ── The live join's document ──────────────────────────────────────────────────────────────────

LiveDocument writeLiveDocument(const LiveDocumentInput &in)
{
    LiveDocument out;
    out.manifest = in.header;
    out.club     = in.capturedClub;

    // The integrity verdicts the analysis service reached for this Recording, into the header: a
    // verdict that checked nothing (or none reached) writes no block.
    if (in.integrity) {
        const auto &imu = in.integrity->imu;
        const auto &cap = in.integrity->capture;
        pinpoint::applyImuIntegrity(out.manifest, imu ? &*imu : nullptr);
        pinpoint::applyCaptureIntegrity(out.manifest, cap ? &*cap : nullptr);
        if (imu && imu->warns())
            ppInfo() << "[ShotProcessor] IMU re-fusion parity FAILED — worst" << imu->worstMaxDeg << "deg over"
                     << imu->sourcesChecked << "source(s); shot flagged not re-analysable";
        if (cap && cap->camerasChecked > 0) {
            if (cap->holes > 0)
                ppWarn() << "[ShotProcessor] capture lost" << cap->framesLost << "frame(s) in" << cap->holes
                         << "hole(s), worst" << cap->worstHoleMs << "ms,"
                         << (cap->preImpact ? "before impact" : "after impact")
                         << "— shot flagged and excluded from the session assessment";
            if (cap->clipped())
                ppWarn() << "[ShotProcessor] capture cut short —" << cap->missingStartMs
                         << "ms missing at the start," << cap->missingEndMs << "ms at the end,"
                         << cap->framesBeforeImpact << "frame(s) before impact on the worst camera"
                         << (cap->startsAfterImpact ? "— no swing on it: shot flagged and excluded from the session assessment"
                                                    : "— shot flagged, still assessed");
        }
    }

    out.dataWarningDetail = pinpoint::dataWarningDetailFrom(out.manifest);
    // The refused-club fact and the down-the-line top-out-of-view fact are read from analysis.* on
    // disk; the header carries no analysis, so they are added from the analysis in hand — same keys.
    if (in.analysis && in.analysis->shaft.refusedReason)
        out.dataWarningDetail.insert(QStringLiteral("clubRefused"),
            QString::fromLatin1(pinpoint::analysis::shaftRefusedReasonName(in.analysis->shaft.refusedReason)));
    if (in.analysis && in.analysis->shaftDtl.topOutOfView)
        pinpoint::insertDtlTopOutOfView(out.dataWarningDetail, in.analysis->shaftDtl.handsFromTopPx,
                                        in.analysis->shaftDtl.topClubPx);

    // THE FIRST WRITE, through the one door. No parallel-write race: the export worker wrote only
    // media and returned, and the analyzer returned a value.
    if (in.persist == pinpoint::PersistPath::FullDocument
        || in.persist == pinpoint::PersistPath::AnalysisOnlyDocument) {
        if (!in.repository) {
            out.error = QStringLiteral("no recording repository — the swing document was not written");
        } else {
            pinpoint::NewRecording doc;
            doc.header     = out.manifest;
            doc.analysis   = in.analysis;
            pinpoint::PersistOptions opts;
            opts.savePose = in.savePose;
            const pinpoint::WriteResult w = in.repository->create(in.shot, doc, opts);
            out.wrote = w.ok();
            if (!out.wrote)
                out.error = w.error;
            out.documentPath = pinpoint::SwingStore::ppswPath(in.swingDir);
        }
    }

    // The shot's detail (the carousel row, the in-window replay): the one projection of the analysis
    // block this document gets, from the header it is written with. Window time.
    if (in.analysis)
        out.analysisDetail = pinpoint::liveAnalysisDetail(*in.analysis, in.windowT0Us, out.manifest);
    return out;
}

} // namespace pinpoint::capture
