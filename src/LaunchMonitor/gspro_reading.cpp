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

#include "gspro_reading.h"

#include <QStringLiteral>

namespace pinpoint::lm {

namespace {

std::optional<LaunchMonitorReading> fail(QString *error, const QString &why)
{
    if (error)
        *error = why;
    return std::nullopt;
}

// Gate on the presence bit, never on the value. See the header.
inline void take(std::optional<double> &out, uint32_t present, uint32_t bit, double value)
{
    if ((present & bit) != 0u)
        out = value;
}

} // namespace

double gsProMetresToYards(double metres)
{
    // Exact by definition: a yard is 0.9144 m.
    return metres / 0.9144;
}

std::optional<LaunchMonitorReading> readingFromGsProMessage(const gsp_message &message,
                                                            QString *error)
{
    if (error)
        error->clear();

    // ⚠ ONLY A SHOT BECOMES A READING. A heartbeat and a status message are the
    // same JSON object with the ball and club flags false (protocol §4.2), and
    // GSPro answers all three with the same 200 — so the reply is the library's
    // business and the DISTINCTION is ours. A connector that turned a heartbeat
    // into a reading would attribute an empty shot to a swing every few seconds.
    if (message.kind != GSP_MSG_SHOT)
        return fail(error, QStringLiteral("not a shot message"));

    LaunchMonitorReading r;

    // ── Provenance ──────────────────────────────────────────────────────────
    // The client's own counter, as provenance only — exactly as the GCQuad's Shot
    // ID is. It decides nothing: libgspro already flags a repeated shot number
    // (GSP_MSGF_SHOT_NUMBER_REPEATED) because clients restart their counters, and
    // several send 0 on every heartbeat.
    r.deviceShotId = QString::number(static_cast<qlonglong>(message.shot_number));
    // ⚠ EMPTY, AND NOT AN OVERSIGHT: the shot message has no club field. See the
    // header, and GsProMonitor::setPlayerClub for the direction it travels.
    r.deviceClub.clear();

    // ── Ball ────────────────────────────────────────────────────────────────
    // ⚠ DERIVE FIRST, ON A COPY. A client that sent only BackSpin and SideSpin
    // ([MLM] does) has no TotalSpin or SpinAxis on the wire, and the library will
    // compute both from the pair — setting their presence bits as it goes, so the
    // gating below picks them up. On a copy because `message` is the caller's and
    // the derived values must not look like something the device sent.
    gsp_ball_data ball = message.ball;
    (void)gsp_ball_data_derive(&ball);

    take(r.ballSpeed,       ball.present, GSP_BALL_SPEED,       ball.speed);
    // ⚠ SIGN CONVENTION UNVERIFIED (protocol §3.5, unknown U6). Nothing here is
    // graded on spin axis today; anything that starts to must confirm the sign
    // against a real device first, because a mirrored axis reads as a plausible
    // fade where there was a draw.
    take(r.spinAxis,        ball.present, GSP_BALL_SPIN_AXIS,   ball.spin_axis);
    take(r.spinRate,        ball.present, GSP_BALL_TOTAL_SPIN,  ball.total_spin);
    take(r.backSpin,        ball.present, GSP_BALL_BACK_SPIN,   ball.back_spin);
    take(r.sideSpin,        ball.present, GSP_BALL_SIDE_SPIN,   ball.side_spin);
    take(r.launchDirection, ball.present, GSP_BALL_HLA,         ball.hla);
    take(r.launchAngle,     ball.present, GSP_BALL_VLA,         ball.vla);
    take(r.carryDistance,   ball.present, GSP_BALL_CARRY_DISTANCE, ball.carry_distance);

    // ── Club ────────────────────────────────────────────────────────────────
    const uint32_t club = message.club.present;
    take(r.clubheadSpeed, club, GSP_CLUB_SPEED,           message.club.speed);
    take(r.attackAngle,   club, GSP_CLUB_ANGLE_OF_ATTACK, message.club.angle_of_attack);
    take(r.faceAngle,     club, GSP_CLUB_FACE_TO_TARGET,  message.club.face_to_target);
    take(r.clubPath,      club, GSP_CLUB_PATH,            message.club.path);
    take(r.lieAngle,      club, GSP_CLUB_LIE,             message.club.lie);
    take(r.dynamicLoft,   club, GSP_CLUB_LOFT,            message.club.loft);
    take(r.closureRate,   club, GSP_CLUB_CLOSURE_RATE,    message.club.closure_rate);
    // ⚠ FACE IMPACT IS THE ONE PLACE A CLIENT IS KNOWN TO DISAGREE WITH THE
    // VENDOR. [MLM] puts FACE-TO-PATH in HorizontalFaceImpact rather than a strike
    // position (protocol §3.3) — the same key, a different quantity, and both are
    // small signed numbers so neither looks wrong. Mapped as the vendor documents
    // it, because that is what the field is FOR; the first capture from an MLM2PRO
    // is what settles whether this connector needs a per-device exception, and
    // that capture is what libgspro's .gswire recorder exists to take.
    take(r.strikeLocation, club, GSP_CLUB_HORIZONTAL_FACE_IMPACT,
         message.club.horizontal_face_impact);
    // Unit unstated on the wire; assumed mm, which is the reading's unit.
    take(r.strikeHeight,   club, GSP_CLUB_VERTICAL_FACE_IMPACT,
         message.club.vertical_face_impact);

    // ── Units ───────────────────────────────────────────────────────────────
    // Distances only, and only under "Meters". Speeds are left alone: every client
    // that can be read sends mph regardless, no client is known to send "Meters" at
    // all, and converting a speed on a guess would corrupt the one number this
    // connector exists to check our camera estimate against (protocol §3.6, U3).
    if (message.units == GSP_UNITS_METERS) {
        if (r.carryDistance)
            r.carryDistance = gsProMetresToYards(*r.carryDistance);
    }

    // ── Values the device states implicitly ─────────────────────────────────
    // The same three the GCQuad path derives, by the same rules and in the same
    // place, so a reading means the same thing whichever connector produced it.
    // Every input is the device's, so the results are the device's too.
    if (!r.faceToPath && r.faceAngle && r.clubPath)
        r.faceToPath = *r.faceAngle - *r.clubPath;

    if (!r.smashFactor && r.ballSpeed && r.clubheadSpeed && *r.clubheadSpeed > 0.0)
        r.smashFactor = *r.ballSpeed / *r.clubheadSpeed;

    if (!r.spinLoft && r.dynamicLoft && r.attackAngle)
        r.spinLoft = *r.dynamicLoft - *r.attackAngle;

    // A shot whose flags said "contains ball data" and which carried no numbers is
    // not a reading. libgspro flags that case (GSP_MSGF_BALL_FLAG_WITHOUT_OBJECT);
    // here it simply has nothing to attribute.
    if (!r.hasAnyValue())
        return fail(error, QStringLiteral("shot message carried no measured values"));

    return r;
}

} // namespace pinpoint::lm
