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

import QtQuick
import QtTest
import PinPointStudio

// The badge's row marks: `dot` (a filled 10-px dot, a strength), `ring` (a 2-px ring, a fault) and
// `plain` (no fill circle). The original kinds are unchanged: still on their faint fill.
Item {
    id: probe
    width: 400; height: 200

    PpBadge { id: dot;   kind: "dot" }
    PpBadge { id: ring;  kind: "ring" }
    PpBadge { id: plainDot; kind: "dot"; plain: true }
    PpBadge { id: plainCheck; kind: "check"; plain: true }
    PpBadge { id: check; kind: "check" }
    PpBadge { id: target; kind: "target" }
    PpBadge { id: small; kind: "ring"; size: Theme.sp(8) }

    TestCase {
        id: tc
        name: "PpBadge"
        when: windowShown

        function test_dotIsAFilledTenPixelDot() {
            const d = tc.findChild(dot, "badgeDot")
            verify(d !== null)
            compare(d.width, Theme.sp(10)); compare(d.height, d.width); compare(d.radius, d.width / 2)
            verify(Qt.colorEqual(d.color, dot.tone))
            verify(Qt.colorEqual(dot.tone, Theme.colorAccent), "a strength defaults to the accent")
            verify(tc.findChild(dot, "badgeRing") === null)
        }

        function test_ringIsATwoPixelRing() {
            const r = tc.findChild(ring, "badgeRing")
            verify(r !== null)
            compare(r.width, Theme.sp(10))
            compare(r.border.width, Math.max(1.5, Theme.sp(2)))
            verify(Qt.colorEqual(r.border.color, ring.tone))
            compare(r.color.a, 0, "empty inside")
            verify(Qt.colorEqual(ring.tone, Theme.colorAttention), "a fault defaults to amber")
        }

        function test_markNeverOutgrowsTheBadge() {
            compare(tc.findChild(small, "badgeRing").width, Theme.sp(8))
        }

        function test_plainDropsTheFill() {
            compare(plainDot.color.a, 0)
            compare(plainCheck.color.a, 0)
            verify(dot.color.a > 0, "a dot without plain keeps the fill")
        }

        function test_originalKindsUnchanged() {
            verify(Qt.colorEqual(check.tone, Theme.colorGood))
            verify(Qt.colorEqual(target.tone, Theme.colorAccent))
            compare(check.size, Theme.sp(20))
            fuzzyCompare(check.color.a, Theme.dark ? 0.16 : 0.12, 0.01)
            fuzzyCompare(target.color.a, Theme.dark ? 0.16 : 0.12, 0.01)
            verify(tc.findChild(check, "badgeDot") === null && tc.findChild(check, "badgeRing") === null)
        }
    }
}
