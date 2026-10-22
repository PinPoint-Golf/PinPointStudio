// Standalone test for address_marks.h: a drawn person mask, the hip rows read for the body's
// outer edges — through a hole, past the hands, never beyond the cap; the DTL rear side chosen
// away from the hands; no mask on the hip rows ⇒ not a measurement.
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
    std::printf(g_fail ? "FAILED (%d)\n" : "ALL PASS\n", g_fail);
    return g_fail ? 1 : 0;
}
