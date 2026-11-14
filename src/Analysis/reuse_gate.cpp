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

#include "reuse_gate.h"

#include <QStringList>
#include <utility>


namespace pinpoint::analysis {

namespace {

int codeOf(const QJsonObject &v) { return v.value(QStringLiteral("code")).toInt(); }

QString versionReason(const QJsonObject &v, int want)
{
    return QStringLiteral("version %1, want %2").arg(codeOf(v)).arg(want);
}

} // namespace

ReuseGate::ReuseGate(ReuseGateFacts facts)
    : f_(std::move(facts))
{
    vp_  = f_.versions.value(QStringLiteral("pose")).toObject();
    vpd_ = f_.versions.value(QStringLiteral("poseDtl")).toObject();
    vb_  = f_.versions.value(QStringLiteral("ball")).toObject();
    vs_  = f_.versions.value(QStringLiteral("shaft")).toObject();
    wantScope_ = f_.fullWindow ? QStringLiteral("full") : QStringLiteral("span");
    for (auto it = f_.tuningOverrides.cbegin(); it != f_.tuningOverrides.cend(); ++it)
        if (it.key().startsWith(QLatin1String("pose.")) || it.key().startsWith(QLatin1String("shaft.addressScan"))
            || it.key().startsWith(QLatin1String("shaft.addressStride")) || it.key().startsWith(QLatin1String("ball.")))
            poseOverride_ = true;
}

QString ReuseGate::recordedPoseScope() const    { return vp_.value(QStringLiteral("scope")).toString(); }
QString ReuseGate::recordedPoseModel() const    { return vp_.value(QStringLiteral("model")).toString(); }
QString ReuseGate::recordedPoseDtlModel() const { return vpd_.value(QStringLiteral("model")).toString(); }

bool ReuseGate::mayReloadPose() const
{
    const QString scope = recordedPoseScope();
    return !f_.forceRerun && !poseOverride_ && !vp_.isEmpty()
        && codeOf(vp_) == kPoseStageVersion
        // a full-window pose covers a span request; a span pose covers only a span request
        && (scope == QLatin1String("full") || scope == wantScope_)
        && f_.hasFaceOnCamera;
}

bool ReuseGate::mayReloadPoseDtl() const
{
    return !f_.forceRerun && !poseOverride_ && !vpd_.isEmpty()
        && codeOf(vpd_) == kDtlPoseStageVersion
        && f_.hasPoseDtlBlock && f_.hasDtlCamera;
}

bool ReuseGate::mayReloadBall(bool poseReloaded) const
{
    return !f_.forceRerun && poseReloaded && !vb_.isEmpty()
        && codeOf(vb_) == kBallStageVersion && f_.hasBallBlock;
}

bool ReuseGate::clubInputsMatch() const
{
    const QJsonObject in = f_.versions.value(QStringLiteral("inputs")).toObject();
    if (in.isEmpty())
        return !f_.clubCorrectedNow;   // before step 1.6: computed with the captured club
    const AnalysisInputs recorded = analysisInputsFromJson(in);
    if (recorded.version != kAnalysisInputsVersion)
        return false;
    return recorded.sameClub(f_.inputsNow);
}

// Which hand leads, as every producer reads it (handedness != Left ⇒ the left hand; unknown reads as
// right): pose_runner.cpp's lead/trail labels, shaft_tracker.cpp's lead elbow.
static bool leftLeads(pinpoint::recording::Handedness h) { return h != pinpoint::recording::Handedness::Left; }

bool ReuseGate::leadSideMatches() const
{
    const QJsonObject in = f_.versions.value(QStringLiteral("inputs")).toObject();
    if (in.isEmpty())
        return true;   // before step 1.6: made with the recorded context's handedness (recorded once)
    const AnalysisInputs recorded = analysisInputsFromJson(in);
    if (recorded.version != kAnalysisInputsVersion)
        return false;
    return leftLeads(recorded.handedness) == leftLeads(f_.inputsNow.handedness);
}

bool ReuseGate::poseLeadSwapped() const
{
    const QJsonObject in = f_.versions.value(QStringLiteral("inputs")).toObject();
    if (in.isEmpty())
        return false;
    const AnalysisInputs recorded = analysisInputsFromJson(in);
    return recorded.version == kAnalysisInputsVersion
        && leftLeads(recorded.handedness) != leftLeads(f_.inputsNow.handedness);
}

bool ReuseGate::mayReloadShaft(bool ballReloaded) const
{
    // Reused only on top of a reused pose AND ball (the track was computed from them), when the
    // tracker that wrote it is the one that would run now, when NO tuning override is in play at all
    // (the ladder stages that would otherwise re-run each have their own knobs), and when the club it
    // was computed with is this run's club.
    return !f_.forceRerun && ballReloaded && f_.tuningOverrides.isEmpty() && !vs_.isEmpty()
        && codeOf(vs_) == kShaftStageVersion && f_.hasClubBlock && clubInputsMatch() && leadSideMatches();
}

QMap<QString, QString> ReuseGate::reasons(const ReuseReloaded &r) const
{
    QMap<QString, QString> out;
    if (f_.forceRerun) {
        for (const char *k : { "pose", "ball", "shaft", "ladder", "poseDtl" })
            out.insert(QString::fromLatin1(k), QStringLiteral("forced"));
        return out;
    }
    const QString reused   = QStringLiteral("reused");
    const QString noRecord = QStringLiteral("no record");
    const QString scope    = recordedPoseScope();
    out[QStringLiteral("pose")] =
          r.pose ? reused
        : poseOverride_ ? QStringLiteral("tuning override")
        : vp_.isEmpty() ? noRecord
        : codeOf(vp_) != kPoseStageVersion ? versionReason(vp_, kPoseStageVersion)
        : (scope != QLatin1String("full") && scope != wantScope_) ? QStringLiteral("scope %1, want %2").arg(scope, wantScope_)
        : !f_.hasFaceOnCamera ? QStringLiteral("no face-on camera")
                              : QStringLiteral("recorded track is empty");
    out[QStringLiteral("ball")] =
          r.ball ? reused
        : !r.pose ? QStringLiteral("needs a reloaded pose")
        : vb_.isEmpty() || !f_.hasBallBlock ? noRecord
        : codeOf(vb_) != kBallStageVersion ? versionReason(vb_, kBallStageVersion)
                                           : QStringLiteral("recorded track is empty");
    out[QStringLiteral("shaft")] =
          r.shaft ? reused
        : !r.ball ? QStringLiteral("needs a reloaded ball")
        : !f_.tuningOverrides.isEmpty() ? QStringLiteral("tuning override")
        : vs_.isEmpty() || !f_.hasClubBlock ? noRecord
        : codeOf(vs_) != kShaftStageVersion ? versionReason(vs_, kShaftStageVersion)
        : !clubInputsMatch() ? QStringLiteral("club changed")
        : !leadSideMatches() ? QStringLiteral("handedness changed")
        : mayReloadShaft(r.ball) ? QStringLiteral("no usable track, or no ladder with it")
                                 : QStringLiteral("not reusable");
    out[QStringLiteral("ladder")] = r.ladder ? reused : out[QStringLiteral("shaft")];
    out[QStringLiteral("poseDtl")] =
          r.poseDtl ? reused
        : !f_.hasDtlCamera ? QStringLiteral("no down-the-line camera")
        : poseOverride_ ? QStringLiteral("tuning override")
        : vpd_.isEmpty() || !f_.hasPoseDtlBlock ? noRecord
        : codeOf(vpd_) != kDtlPoseStageVersion ? versionReason(vpd_, kDtlPoseStageVersion)
                                               : QStringLiteral("recorded track is empty");
    return out;
}

QString analysisInputsStaleness(const QJsonObject &versions, const AnalysisInputs &now)
{
    const QJsonObject in = versions.value(QStringLiteral("inputs")).toObject();
    if (in.isEmpty())
        return QStringLiteral("no input stamp");
    const AnalysisInputs rec = analysisInputsFromJson(in);
    if (rec.version != now.version)
        return QStringLiteral("input stamp version %1, want %2").arg(rec.version).arg(now.version);
    QStringList why;
    if (rec.club != now.club)
        why << QStringLiteral("club %1 → %2").arg(rec.club.isEmpty() ? QStringLiteral("(undeclared)") : rec.club,
                                                  now.club.isEmpty() ? QStringLiteral("(undeclared)") : now.club);
    else if (!rec.sameClub(now))
        why << QStringLiteral("club length");
    if (std::abs(rec.heightM - now.heightM) >= 1e-9) why << QStringLiteral("height");
    if (rec.clubSet != now.clubSet)                  why << QStringLiteral("club set");
    if (rec.handedness != now.handedness)            why << QStringLiteral("handedness");
    if (rec.sessionType != now.sessionType)          why << QStringLiteral("session type");
    return why.join(QStringLiteral(", "));
}

} // namespace pinpoint::analysis
