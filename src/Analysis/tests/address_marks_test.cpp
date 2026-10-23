// Standalone test for address_marks.h: a drawn person mask, the hip rows read for the body's
// outer edges — through a hole, past the hands, never beyond the cap; the DTL rear side chosen
// away from the hands; no mask on the hip rows ⇒ not a measurement. Stage version 3: the DTL
// P-frames with the rear side frozen from address, the back line's angle and the butt line's
// travel in their signs and units, and the gap rule that keeps the trail arm off the neck.
//
//   cmake --build build/tests --target address_marks_test
//   ctest --test-dir build/tests -R address_marks_test --output-on-failure

#include "../../Body/address_marks.h"

#include <cmath>
#include <cstdio>
#include <opencv2/imgproc.hpp>

using namespace pinpoint::analysis::addressmarks;

static int g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}
static bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

int main()
{
    std::printf("address_marks_test\n");
    const int W = 640, H = 480;
    // A body: torso x 300..420 over y 150..330, legs below; hips at y 300, joints at x 330 / 390.
    cv::Mat mask(H, W, CV_32F, cv::Scalar(0));
    cv::rectangle(mask, cv::Rect(300, 150, 121, 181), cv::Scalar(1), cv::FILLED);
    cv::rectangle(mask, cv::Rect(305, 330, 50, 140), cv::Scalar(1), cv::FILLED);
    cv::rectangle(mask, cv::Rect(365, 330, 50, 140), cv::Scalar(1), cv::FILLED);

    {
        double l, r; int rows;
        check(outerExtents(mask, 330, 390, 300, 7, 200, l, r, rows), "§1 a body on the hip rows is measured");
        check(near(l, 300, 0.5) && near(r, 420, 0.5), "§1 the outer edges are the body's, not the joints");
        check(rows == 15, "§1 every row of the band contributed");
    }
    {
        // A hole (a belt buckle the mask missed) three pixels wide does not end the walk.
        cv::Mat m2 = mask.clone();
        cv::rectangle(m2, cv::Rect(350, 290, 3, 30), cv::Scalar(0), cv::FILLED);
        double l, r; int rows;
        check(outerExtents(m2, 330, 390, 300, 7, 200, l, r, rows) && near(l, 300, 0.5) && near(r, 420, 0.5),
              "§2 a 3 px hole in the mask is crossed");
        // By default the walk goes the whole reach and keeps the farthest body pixel: a 6 px gap
        // (the glove in front of the thigh) is crossed; with a gap limit it is an edge.
        cv::Mat m3 = mask.clone();
        cv::rectangle(m3, cv::Rect(400, 280, 6, 60), cv::Scalar(0), cv::FILLED);
        check(outerExtents(m3, 330, 390, 300, 7, 200, l, r, rows) && near(r, 420, 0.5), "§2 a gap inside the reach is crossed: the farthest body pixel is the edge");
        check(outerExtents(m3, 330, 390, 300, 7, 200, l, r, rows, 0.5, 4) && near(r, 399, 0.5), "§2 with a gap limit of 4 px the 6 px gap ends the walk");
    }
    {
        // The walk never runs past the cap: a background person touching the body is cut at the reach.
        cv::Mat m4 = mask.clone();
        cv::rectangle(m4, cv::Rect(420, 150, 220, 181), cv::Scalar(1), cv::FILLED);
        double l, r; int rows;
        check(outerExtents(m4, 330, 390, 300, 7, 100, l, r, rows) && near(r, 390 + 100, 0.5), "§3 the walk stops at the reach from its own hip");
    }
    {
        // Thin mask at the waist: the left hip joint falls just outside, the seed is found nearby.
        cv::Mat m5(H, W, CV_32F, cv::Scalar(0));
        cv::rectangle(m5, cv::Rect(366, 150, 60, 181), cv::Scalar(1), cv::FILLED);
        double l, r; int rows;
        check(outerExtents(m5, 360, 390, 300, 7, 200, l, r, rows) && near(l, 366, 0.5) && near(r, 425, 0.5),
              "§4 a seed within reach of the joint is taken");
        check(!outerExtents(m5, 330, 390, 300, 7, 200, l, r, rows), "§4 a joint with no person within reach on its side ⇒ not a measurement");
        // Two legs with the hands between them (the wrists' row): each side walks from ITS hip,
        // so the hands in the middle are never measured as an edge.
        cv::Mat m8(H, W, CV_32F, cv::Scalar(0));
        cv::rectangle(m8, cv::Rect(300, 350, 45, 100), cv::Scalar(1), cv::FILLED);   // left thigh 300..344
        cv::rectangle(m8, cv::Rect(375, 350, 45, 100), cv::Scalar(1), cv::FILLED);   // right thigh 375..419
        cv::rectangle(m8, cv::Rect(352, 380, 16, 40), cv::Scalar(1), cv::FILLED);    // the hands 352..367
        check(outerExtents(m8, 322, 397, 400, 7, 200, l, r, rows) && near(l, 300, 0.5) && near(r, 419, 0.5),
              "§4 the thighs' outer edges, not the hands between them");
        // The hands in FRONT of the lead thigh, a thin gap behind them: the edge is still the thigh.
        cv::Mat m9 = m8.clone();
        cv::rectangle(m9, cv::Rect(380, 380, 20, 40), cv::Scalar(1), cv::FILLED);   // a glove over the right thigh's inner half
        cv::rectangle(m9, cv::Rect(400, 380, 5, 40), cv::Scalar(0), cv::FILLED);    // the gap behind it
        check(outerExtents(m9, 322, 390, 400, 7, 200, l, r, rows) && near(r, 419, 0.5),
              "§4 a glove in front of the thigh with a gap behind it: the thigh is still the edge");
        // Nothing within the seed reach ⇒ no measurement.
        cv::Mat m6(H, W, CV_32F, cv::Scalar(0));
        cv::rectangle(m6, cv::Rect(500, 150, 60, 181), cv::Scalar(1), cv::FILLED);
        check(!outerExtents(m6, 330, 390, 300, 7, 200, l, r, rows), "§4 no person on the hip rows ⇒ not a measurement");
        // A body clipped by the frame: the walk reaches the image border, which is not an edge.
        cv::Mat m10(H, W, CV_32F, cv::Scalar(0));
        cv::rectangle(m10, cv::Rect(0, 150, 120, 181), cv::Scalar(1), cv::FILLED);   // body runs off the left edge
        check(!outerExtents(m10, 40, 100, 300, 7, 200, l, r, rows), "§4 a body clipped by the frame's edge is not measured on that side");
    }
    {
        // measureView: normalised output; DTL rear side away from the hands.
        ViewMarks fo = measureView(mask, W, H, 123, 330, 300, 390, 300, 360, false);
        check(fo.found && near(fo.leftX * W, 300, 0.5) && near(fo.rightX * W, 420, 0.5) && near(fo.rowY * H, 300, 0.5),
              "§5 face-on: edges normalised, row at the hips");
        check(!std::isfinite(fo.buttX) && fo.side == 0, "§5 face-on has no butt mark");
        ViewMarks d1 = measureView(mask, W, H, 123, 355, 300, 365, 300, 440, true);   // hands to the right
        check(d1.found && d1.side == -1 && near(d1.buttX * W, 300, 0.5), "§5 DTL: hands right ⇒ the rear is the left edge");
        ViewMarks d2 = measureView(mask, W, H, 123, 355, 300, 365, 300, 280, true);   // hands to the left
        check(d2.found && d2.side == +1 && near(d2.buttX * W, 420, 0.5), "§5 DTL: hands left ⇒ the rear is the right edge");
        ViewMarks d3 = measureView(mask, W, H, 123, 355, 300, 365, 300, kNan, true);
        check(d3.found && !std::isfinite(d3.buttX), "§5 DTL without hands: edges measured, no butt side claimed");
        ViewMarks bad = measureView(mask, W, H, 1, kNan, 300, 390, 300, 360, false);
        check(!bad.found, "§5 a missing hip joint ⇒ not a measurement");
    }
    {
        // §6 the hip joints' row is read on both cameras whatever the wrists and knees say (the
        // wrists' row was tried and rejected — see ViewMarks::rowSource). An arm hanging outside
        // the hip at that height IS the outline the line follows.
        cv::Mat m7 = mask.clone();
        cv::rectangle(m7, cv::Rect(420, 240, 26, 80), cv::Scalar(1), cv::FILLED);   // an elbow, rows 240..320, inside the reach
        ViewMarks atHips = measureView(m7, W, H, 1, 330, 300, 390, 300, 360, false, 360.0, 420.0);
        check(atHips.found && near(atHips.rightX * W, 445, 0.5) && atHips.rowSource == 0 && near(atHips.rowY * H, 300, 0.5),
              "§6 face-on: the hip row, the outline including the arm");
        ViewMarks dtlRow = measureView(m7, W, H, 1, 355, 300, 365, 300, 440, true, 360.0, 420.0);
        check(dtlRow.rowSource == 0 && near(dtlRow.rowY * H, 300, 0.5), "§6 down the line: the hip row");
    }
    {
        // §7 TPI's spine angle line (measureBackLine): a trunk tilted 45° forward, as at address
        // down the line, with a neck beyond the shoulders. The rear points are read at a RIGHT
        // ANGLE to the trunk axis on the rear side, not along an image row.
        const double r2 = std::sqrt(0.5);
        const cv::Point2d hip(200, 350), u(r2, -r2), nRear(-r2, -r2);   // up-right axis; rear = up-left
        const double L = 170, halfW = 50, neckHalfW = 20, neckLen = 60;
        const cv::Point2d sh = hip + u * L, ear = sh + u * neckLen;
        auto quad = [&](cv::Mat &m, cv::Point2d a, cv::Point2d b, double hw) {
            const cv::Point2d n(-u.y, u.x);
            std::vector<cv::Point> q { a + n * hw, b + n * hw, b - n * hw, a - n * hw };
            cv::fillConvexPoly(m, q, cv::Scalar(1));
        };
        cv::Mat body(H, W, CV_32F, cv::Scalar(0));
        quad(body, hip - u * 40, sh, halfW);           // the trunk, from below the hips to the shoulders
        quad(body, sh - u * 5, ear + u * 30, neckHalfW); // the neck and head, narrower
        ViewMarks v; v.found = true; v.side = -1;      // the hands on the right: the rear is image-left
        measureBackLine(body, W, H, v, hip.x, hip.y, sh.x, sh.y, ear.x, ear.y, 200);
        const cv::Point2d tb = hip + nRear * halfW;                 // behind the hip-joint midpoint
        const cv::Point2d nk = sh + u * (0.5 * neckLen) + nRear * neckHalfW;   // behind halfway to the ears
        check(std::isfinite(v.tailboneX) && near(v.tailboneX * W, tb.x, 1.5) && near(v.tailboneY * H, tb.y, 1.5),
              "§7 the tailbone: the rear outline at a right angle behind the hip-joint midpoint");
        check(std::isfinite(v.neckX) && near(v.neckX * W, nk.x, 1.5) && near(v.neckY * H, nk.y, 1.5),
              "§7 the back of the neck: the rear outline behind the point halfway from the shoulders to the ears");
        check(v.backWalks == 11, "§7 all eleven walks of each point contributed");
        // A row read at the neck's height would have met the trunk's upper back far behind the neck.
        int rowRear = -1;
        for (int x = 0; x < W && rowRear < 0; ++x) if (body.at<float>(int(std::lround(nk.y)), x) > 0.5f) rowRear = x;
        check(rowRear >= 0 && rowRear < nk.x - 15, "§7 (a horizontal row at that height reads the upper back instead)");

        // The rear on the other side: hands on the left, the same body mirrored.
        cv::Mat flipped; cv::flip(body, flipped, 1);
        ViewMarks w; w.found = true; w.side = +1;
        measureBackLine(flipped, W, H, w, W - 1 - hip.x, hip.y, W - 1 - sh.x, sh.y, W - 1 - ear.x, ear.y, 200);
        check(std::isfinite(w.neckX) && near(w.neckX * W, W - 1 - nk.x, 1.5) && near(w.tailboneY * H, tb.y, 1.5),
              "§7 side +1: the rear is image-right, mirrored");

        // Refusals: no rear side, a missing ear, a body clipped at the frame's edge.
        ViewMarks n0; n0.found = true; n0.side = 0;
        measureBackLine(body, W, H, n0, hip.x, hip.y, sh.x, sh.y, ear.x, ear.y, 200);
        check(!std::isfinite(n0.tailboneX) && !std::isfinite(n0.neckX), "§7 no rear side (no hands) ⇒ no spine points");
        ViewMarks ne; ne.found = true; ne.side = -1;
        measureBackLine(body, W, H, ne, hip.x, hip.y, sh.x, sh.y, kNan, kNan, 200);
        check(!std::isfinite(ne.neckX) && !std::isfinite(ne.tailboneX), "§7 no ears ⇒ no spine points (never half a line)");
        cv::Mat clipped = body.clone();
        cv::rectangle(clipped, cv::Rect(0, 0, W, 150), cv::Scalar(1), cv::FILLED);   // the body runs off the top edge
        ViewMarks nc; nc.found = true; nc.side = -1;
        measureBackLine(clipped, W, H, nc, hip.x, hip.y, sh.x, sh.y, ear.x, ear.y, 400);
        check(!std::isfinite(nc.neckX), "§7 a walk that reaches the frame's edge finds no edge: the neck is not measured");
        // A 3 px hole in the mask on the walk is crossed (the farthest person pixel is the edge).
        cv::Mat holed = body.clone();
        cv::circle(holed, cv::Point(int(hip.x + nRear.x * 25), int(hip.y + nRear.y * 25)), 2, cv::Scalar(0), cv::FILLED);
        ViewMarks nh; nh.found = true; nh.side = -1;
        measureBackLine(holed, W, H, nh, hip.x, hip.y, sh.x, sh.y, ear.x, ear.y, 200);
        check(near(nh.tailboneX * W, tb.x, 1.5), "§7 a hole inside the back is crossed");
    }
    {
        // §8 the DTL P-frames past address (posture_chart_design.md §10 stage 4a): the rear side is
        // FROZEN from address. At the top the hands are up and over the trail shoulder — on the
        // REAR side from down the line — and "away from the hands" would put the butt on the belly.
        ViewMarks addr = measureView(mask, W, H, 1, 355, 300, 365, 300, 440, true);   // hands right ⇒ rear left
        check(addr.found && addr.side == -1, "§8 address: the rear is image-left");
        ViewMarks topByHands = measureView(mask, W, H, 2, 355, 300, 365, 300, 280, true);   // hands now left
        check(topByHands.side == +1, "§8 (choosing by the hands at the top would flip the rear side)");
        ViewMarks top = measureViewFrozenSide(mask, W, H, 2, 355, 300, 365, 300, addr.side);
        check(top.found && top.side == -1 && near(top.buttX * W, 300, 0.5), "§8 frozen: the rear stays image-left, the butt on the left edge");
        ViewMarks noSide = measureViewFrozenSide(mask, W, H, 2, 355, 300, 365, 300, 0);
        check(noSide.found && noSide.side == 0 && !std::isfinite(noSide.buttX), "§8 no frozen side ⇒ edges, no butt claimed");
        // One hip joint seen (the lead hip behind the trail hip as the pelvis turns): it seeds both walks.
        ViewMarks oneHip = measureViewFrozenSide(mask, W, H, 2, 360, 300, 360, 300, -1);
        check(oneHip.found && near(oneHip.buttX * W, 300, 0.5) && near(oneHip.rightX * W, 420, 0.5),
              "§8 one hip joint seeds both walks; the outline is the same");
        DtlPhaseMarks pm; pm.phase = 2; pm.eventTUs = 77; pm.v = top;
        check(pm.refused.empty() && pm.v.found, "§8 a DtlPhaseMarks carries the view and no refusal when found");
        AddressMarks am; am.dtl = addr; am.dtlPhases.push_back(pm);
        check(am.dtlPhases.size() == 1 && am.any(), "§8 AddressMarks carries the P-frames beside the address views");
    }
    {
        // §9 the two readings' sign and units. spineBackLine: the back line against the vertical,
        // + over the ball (spineForwardBend's atan2(toward·dx, −dy)). hipDepthButt: the butt line's
        // travel from address in cm, + toward the ball (pelvisThrust's sign), through the ruler.
        const double r2 = std::sqrt(0.5);
        const cv::Point2d hip(200, 350), u(r2, -r2);
        const double L = 170, halfW = 50, neckHalfW = 20, neckLen = 60;
        const cv::Point2d sh = hip + u * L, ear = sh + u * neckLen;
        auto quad = [&](cv::Mat &m, cv::Point2d a, cv::Point2d b, double hw, cv::Point2d uu) {
            const cv::Point2d n(-uu.y, uu.x);
            std::vector<cv::Point> q { a + n * hw, b + n * hw, b - n * hw, a - n * hw };
            cv::fillConvexPoly(m, q, cv::Scalar(1));
        };
        cv::Mat body(H, W, CV_32F, cv::Scalar(0));
        quad(body, hip - u * 40, sh, halfW, u);
        quad(body, sh - u * 5, ear + u * 30, neckHalfW, u);
        ViewMarks v; v.found = true; v.side = -1;               // rear image-left ⇒ the ball is image-right
        measureBackLine(body, W, H, v, hip.x, hip.y, sh.x, sh.y, ear.x, ear.y, 200);
        // The back is wider at the hips than at the neck (50 vs 20 px), so the line tailbone → neck
        // leans 30 px further toward the ball than the trunk axis over its 200 px: the expected
        // angle is the analytic points' own, 45° + atan(30/200).
        const cv::Point2d nRear(-r2, -r2);
        const cv::Point2d tb = hip + nRear * halfW, nk = sh + u * (0.5 * neckLen) + nRear * neckHalfW;
        const double expect = std::atan2(nk.x - tb.x, -(nk.y - tb.y)) * 180.0 / 3.14159265358979323846;
        check(near(expect, 45.0 + std::atan(30.0 / 200.0) * 180.0 / 3.14159265358979323846, 1e-6), "§9 (the analytic back line)");
        const double b45 = backLineBendDeg(v, W, H, +1.0);
        check(near(b45, expect, 1.0), "§9 a trunk 45° over the ball: the back line reads its own angle, + over the ball");
        check(near(backLineBendDeg(v, W, H, -1.0), -expect, 1.0), "§9 (the same line with the ball on the other side reads negative: the sign is toward the ball)");
        // The same golfer mirrored (ball image-left) reads the same +45°.
        cv::Mat flipped; cv::flip(body, flipped, 1);
        ViewMarks w; w.found = true; w.side = +1;
        measureBackLine(flipped, W, H, w, W - 1 - hip.x, hip.y, W - 1 - sh.x, sh.y, W - 1 - ear.x, ear.y, 200);
        check(near(backLineBendDeg(w, W, H, -1.0), expect, 1.0), "§9 mirrored, the ball image-left: the same positive angle");
        // Standing up: the trunk at 35° from the vertical reads 35°.
        const double a35 = 35.0 * 3.14159265358979323846 / 180.0;
        const cv::Point2d u35(std::sin(a35), -std::cos(a35));
        const cv::Point2d sh35 = hip + u35 * L, ear35 = sh35 + u35 * neckLen;
        cv::Mat body35(H, W, CV_32F, cv::Scalar(0));
        quad(body35, hip - u35 * 40, sh35, halfW, u35);
        quad(body35, sh35 - u35 * 5, ear35 + u35 * 30, neckHalfW, u35);
        ViewMarks s35; s35.found = true; s35.side = -1;
        measureBackLine(body35, W, H, s35, hip.x, hip.y, sh35.x, sh35.y, ear35.x, ear35.y, 200);
        const double b35 = backLineBendDeg(s35, W, H, +1.0);
        check(near(b35, 35.0 + std::atan(30.0 / 200.0) * 180.0 / 3.14159265358979323846, 1.0) && near(b45 - b35, 10.0, 1.0),
              "§9 the trunk 10° more upright reads 10° less on the back line (standing up = smaller)");
        ViewMarks none; none.found = true;
        check(!std::isfinite(backLineBendDeg(none, W, H, 1.0)), "§9 no back-line points ⇒ no angle");
        check(!std::isfinite(backLineBendDeg(v, W, H, 0.0)), "§9 no direction to the ball ⇒ no angle");

        // hipDepthButt: the rear image-left, the ball image-right. The butt 10 px toward the ball
        // (rightward) at 0.5 cm/px is +5 cm; 6 px away is −3 cm; no ruler ⇒ none.
        ViewMarks a0; a0.buttX = 300.0 / W;
        ViewMarks in10; in10.buttX = 310.0 / W;
        ViewMarks out6; out6.buttX = 294.0 / W;
        check(near(buttShiftCm(in10, a0, W, +1.0, 0.5), 5.0, 1e-9), "§9 the butt 10 px toward the ball at 0.5 cm/px: +5 cm");
        check(near(buttShiftCm(out6, a0, W, +1.0, 0.5), -3.0, 1e-9), "§9 6 px away from the ball: −3 cm");
        check(near(buttShiftCm(a0, a0, W, +1.0, 0.5), 0.0, 1e-12), "§9 address against itself: 0");
        // Mirrored (ball image-left): the butt moving LEFT is toward the ball, +.
        ViewMarks m0; m0.buttX = 340.0 / W;
        ViewMarks mIn; mIn.buttX = 330.0 / W;
        check(near(buttShiftCm(mIn, m0, W, -1.0, 0.5), 5.0, 1e-9), "§9 mirrored: 10 px leftward toward the ball is +5 cm");
        check(!std::isfinite(buttShiftCm(in10, a0, W, +1.0, 0.0)), "§9 no ruler ⇒ no centimetres, never pixels");
        ViewMarks nob;
        check(!std::isfinite(buttShiftCm(nob, a0, W, +1.0, 0.5)), "§9 no butt on the frame ⇒ none");
    }
    {
        // §10 the gap rule past address: at the top the trail arm sits behind the neck from down
        // the line. With the address rule (cross any gap) the neck reads the arm; with a gap
        // limit the walk ends at the body's own outline.
        const double r2 = std::sqrt(0.5);
        const cv::Point2d hip(300, 350), u(r2, -r2), nRear(-r2, -r2);
        const double L = 150, halfW = 45, neckHalfW = 18, neckLen = 50;
        const cv::Point2d sh = hip + u * L, ear = sh + u * neckLen;
        auto quad = [&](cv::Mat &m, cv::Point2d a, cv::Point2d b, double hw) {
            const cv::Point2d n(-u.y, u.x);
            std::vector<cv::Point> q { a + n * hw, b + n * hw, b - n * hw, a - n * hw };
            cv::fillConvexPoly(m, q, cv::Scalar(1));
        };
        cv::Mat body(H, W, CV_32F, cv::Scalar(0));
        quad(body, hip - u * 40, sh, halfW);
        quad(body, sh - u * 5, ear + u * 30, neckHalfW);
        const cv::Point2d neckSeed = sh + u * (0.5 * neckLen);
        // The trail arm: a blob 12 px behind the back of the neck, along the rear normal.
        const cv::Point2d armC = neckSeed + nRear * (neckHalfW + 12 + 15);
        cv::circle(body, cv::Point(int(std::lround(armC.x)), int(std::lround(armC.y))), 15, cv::Scalar(1), cv::FILLED);
        ViewMarks any; any.found = true; any.side = -1;
        measureBackLine(body, W, H, any, hip.x, hip.y, sh.x, sh.y, ear.x, ear.y, 200);
        const cv::Point2d nk = neckSeed + nRear * neckHalfW;
        check(std::isfinite(any.neckX) && std::hypot(any.neckX * W - nk.x, any.neckY * H - nk.y) > 20,
              "§10 (crossing any gap, the neck lands on the arm behind it)");
        ViewMarks gapped; gapped.found = true; gapped.side = -1;
        measureBackLine(body, W, H, gapped, hip.x, hip.y, sh.x, sh.y, ear.x, ear.y, 200, 0.0, 0.5, 0.05, 11, 4);
        check(std::isfinite(gapped.neckX) && near(gapped.neckX * W, nk.x, 1.5) && near(gapped.neckY * H, nk.y, 1.5),
              "§10 a 4 px gap limit ends the walk at the back of the neck");
        check(std::isfinite(gapped.tailboneX) && near(gapped.tailboneX * W, (hip + nRear * halfW).x, 1.5),
              "§10 the tailbone is unchanged by the gap limit");
        // A 3 px hole inside the back is still crossed under a 4 px limit.
        cv::Mat holed = body.clone();
        cv::circle(holed, cv::Point(int(hip.x + nRear.x * 25), int(hip.y + nRear.y * 25)), 1, cv::Scalar(0), cv::FILLED);
        ViewMarks gh; gh.found = true; gh.side = -1;
        measureBackLine(holed, W, H, gh, hip.x, hip.y, sh.x, sh.y, ear.x, ear.y, 200, 0.0, 0.5, 0.05, 11, 4);
        check(near(gh.tailboneX * W, (hip + nRear * halfW).x, 1.5), "§10 a 3 px hole inside the back is crossed under the limit");
    }
    {
        // §11 THE HEAD OUTLINE (stage version 4, measureHead): a head disc r 40 at (320, 120) on a neck and a torso
        // whose shoulders top out at y 175; the ears 70 px apart (S = 70). The circle is the crown and the width:
        // top at y 80, 81 px across (x 280…360), centred on x 320 — wherever the face points sit inside the head.
        cv::Mat body(H, W, CV_32F, cv::Scalar(0));
        cv::circle(body, cv::Point(320, 120), 40, cv::Scalar(1), cv::FILLED);
        cv::rectangle(body, cv::Rect(300, 150, 41, 30), cv::Scalar(1), cv::FILLED);
        cv::rectangle(body, cv::Rect(220, 175, 201, 225), cv::Scalar(1), cv::FILLED);
        const double hx[5] = { 320, 305, 335, 285, 355 }, hy[5] = { 150, 140, 140, 130, 130 };
        const bool ok5[5] = { true, true, true, true, true };
        ViewMarks v;
        measureHead(body, W, H, v, hx, hy, ok5, 175.0);
        check(v.headFound && v.headRefused.empty(), "§11 a head on the mask is found");
        check(near(v.crownY * H, 80, 0.5), "§11 the crown is the mask's top over the head");
        check(near(v.headR * H, 40.5, 0.6) && near(v.headCx * W, 320, 0.6) && near(v.headCy * H, 80 + 40.5, 0.6),
              "§11 the circle touches the crown with the head's width as its diameter");
        check(near(v.headLeftX * W, 280, 0.5) && near(v.headRightX * W, 360, 0.5), "§11 the widest row's ends are the head's outline");
        // looking down: the face points 18 px lower and 6 px forward, still inside the head — the same circle
        const double hxd[5] = { 326, 311, 341, 291, 361 }, hyd[5] = { 156, 148, 148, 138, 138 };
        ViewMarks d;
        measureHead(body, W, H, d, hxd, hyd, ok5, 175.0);
        check(d.headFound && near(d.headCx, v.headCx, 1e-9) && near(d.headCy, v.headCy, 1e-9) && near(d.headR, v.headR, 1e-9),
              "§11 looking down moves the face points, not the circle");
        // a cap standing 60 px proud of the skull: the crown is too far above the face for a head
        cv::Mat cap = body.clone();
        cv::rectangle(cap, cv::Rect(290, 20, 61, 70), cv::Scalar(1), cv::FILLED);
        ViewMarks c;
        measureHead(cap, W, H, c, hx, hy, ok5, 175.0);
        check(!c.headFound && c.headRefused.find("too far above") != std::string::npos, "§11 a tall cap: refused, said");
        // a hood joining the head near the crown: the outline is wider than a head
        cv::Mat hood = body.clone();
        cv::rectangle(hood, cv::Rect(330, 83, 80, 47), cv::Scalar(1), cv::FILLED);
        ViewMarks h;
        measureHead(hood, W, H, h, hx, hy, ok5, 175.0);
        check(!h.headFound && h.headRefused.find("hood") != std::string::npos, "§11 a hood joining the head: refused, said");
        // something joining the head's side once it is a head wide (down the line: the upper back behind the head):
        // the walk stops above it; joined before the head is three quarters of its width, refused
        cv::Mat side = body.clone();
        cv::rectangle(side, cv::Rect(340, 87, 100, 43), cv::Scalar(1), cv::FILLED);
        ViewMarks sd;
        measureHead(side, W, H, sd, hx, hy, ok5, 175.0);
        check(!sd.headFound && sd.headRefused.find("steps out") != std::string::npos, "§11 a step out at half a head's width: refused, said");
        // the head at the frame's top: the crown is out of the picture
        cv::Mat top(H, W, CV_32F, cv::Scalar(0));
        cv::circle(top, cv::Point(320, 30), 40, cv::Scalar(1), cv::FILLED);
        cv::rectangle(top, cv::Rect(220, 85, 201, 300), cv::Scalar(1), cv::FILLED);
        const double hyt[5] = { 60, 50, 50, 40, 40 };
        ViewMarks t;
        measureHead(top, W, H, t, hx, hyt, ok5, 85.0);
        check(!t.headFound && t.headRefused.find("top of the frame") != std::string::npos, "§11 the head cut by the frame's top: refused");
        // no mask at the head (a dark hoodie on a dark room: the segmenter lost the golfer), and too few points
        cv::Mat none(H, W, CV_32F, cv::Scalar(0));
        cv::rectangle(none, cv::Rect(220, 175, 201, 225), cv::Scalar(1), cv::FILLED);
        ViewMarks n;
        measureHead(none, W, H, n, hx, hy, ok5, 175.0);
        check(!n.headFound && n.headRefused == "no person mask at the head", "§11 no mask at the head: refused");
        const bool one[5] = { true, false, false, false, false };
        ViewMarks o;
        measureHead(body, W, H, o, hx, hy, one, 175.0);
        check(!o.headFound && !o.headRefused.empty(), "§11 one head point: refused");
    }
    std::printf(g_fail ? "FAILED (%d)\n" : "ALL PASS\n", g_fail);
    return g_fail ? 1 : 0;
}
