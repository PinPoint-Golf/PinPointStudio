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

// impact_anchor.h — the address ball found by its departure, on synthetic frames: a 1280×1024 mat,
// a ball that is there through the backswing and gone after impact, and the things that fooled the
// background-subtraction track on the corpus — bright tape on the shaft above the toes, and a mark
// on the mat that never leaves.

#include "../impact_anchor.h"

#include <cstdio>

using namespace pinpoint::analysis;

static int g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}

static cv::Mat mat(uchar level = 60) { return cv::Mat(1024, 1280, CV_8UC1, cv::Scalar(level)); }
static void disc(cv::Mat &m, int x, int y, int r, uchar v) { cv::circle(m, cv::Point(x, y), r, cv::Scalar(v), cv::FILLED); }

int main()
{
    std::printf("=== impact_anchor: the address ball by its departure ===\n");
    const QPointF p1Head(640, 950);          // the tracker's clubhead at address
    const double  toeY = 900;                // the lowest foot point

    // Backswing frames: the ball at (665, 985), tape on the shaft at (650, 820) that MOVES (so it is
    // in some frames and not others), a permanent mat mark at (600, 990). After impact: ball gone,
    // the mark still there.
    std::vector<cv::Mat> before, after;
    for (int k = 0; k < 5; ++k) {
        cv::Mat b = mat();
        disc(b, 665, 985, 7, 230);
        disc(b, 600, 990, 7, 200);
        disc(b, 640 + 20 * k, 820, 9, 250);  // tape, different place each frame
        before.push_back(b);
        cv::Mat a = mat();
        disc(a, 600, 990, 7, 200);
        after.push_back(a);
    }
    const cv::Mat B = medianFrame(before), A = medianFrame(after);
    check(!B.empty() && B.type() == CV_32F, "median of the backswing frames");

    const AddressBall ball = findAddressBallByDeparture(B, A, p1Head, toeY);
    std::printf("    found (%.0f, %.0f) r %.0f score %.1f\n", ball.px.x(), ball.px.y(), ball.radiusPx, ball.score);
    check(ball.ok && std::hypot(ball.px.x() - 665, ball.px.y() - 985) <= 2.0,
          "the ball that LEFT is found, within 2 px");
    check(std::abs(ball.px.x() - 600) > 20, "the mat mark that stayed is not the ball");
    check(ball.px.y() > toeY, "nothing above the toe line (the tape on the shaft) is taken");

    // A ball outside the search zone (far from the P1 head) is not found — the ball sits by the club.
    const AddressBall far = findAddressBallByDeparture(B, A, QPointF(200, 950), toeY);
    check(!far.ok || std::hypot(far.px.x() - 665, far.px.y() - 985) > 50,
          "the search stays beside the P1 clubhead");

    // No ball at all: before and after identical ⇒ nothing (never fabricate).
    const AddressBall none = findAddressBallByDeparture(A, A, p1Head, toeY);
    check(!none.ok, "no departure ⇒ no ball");

    // Off ⇒ nothing.
    ImpactAnchorConfig off; off.enabled = false;
    check(!findAddressBallByDeparture(B, A, p1Head, toeY, off).ok, "impactAnchor.enabled=false ⇒ nothing");

    // The line from the hands to the ball.
    double th = 0, len = 0;
    ballLine(QPointF(640, 600), QPointF(665, 985), th, len);
    check(std::abs(len - std::hypot(25.0, 385.0)) < 1e-9, "ballLine length");
    check(std::abs(th - std::atan2(385.0, 25.0)) < 1e-12, "ballLine angle (image atan2 convention)");

    std::printf("\n%s (%d failures)\n", g_fail ? "FAIL" : "PASS", g_fail);
    return g_fail;
}
