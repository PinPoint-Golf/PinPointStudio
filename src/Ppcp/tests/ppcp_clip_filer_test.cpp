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

// PpcpClipFiler — the ORDERING, which is the part that is easy to get wrong.
//
// The capture request goes out at the shot; the swing folder is allocated 4-11 s
// later; the clip may arrive on either side of that, and a shot may produce no
// folder at all. A clip that arrives early must be PARKED, never dropped —
// dropping it is precisely the defect this work exists to remove.

#include "../../Gui/shot/ppcp_clip_filer.h"
#include "../../Export/swing_doc.h"
#include "../ppcp_import_ledger.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <cmath>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <cstdio>
#include <vector>

static int g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}

static PpcpClip makeClip(const QString &shotId, const QString &captureId,
                         const QByteArray &bytes, ppcp_completeness comp = PPCP_COMPLETE)
{
    PpcpClip c;
    c.captureId    = captureId;
    c.streamId     = QStringLiteral("st:abcdef0123456789:video");
    c.peerId       = QStringLiteral("peer:phone-1");
    c.sourceId     = QStringLiteral("src:cam-wide");
    c.sessionId    = QStringLiteral("ses:live-1");
    c.shotId       = shotId;
    c.completeness = comp;
    c.container    = QStringLiteral("video/quicktime");
    c.payload      = bytes;
    return c;
}

// A swing folder with a written swing.json, as the pipeline would leave one.
static QString makeSwing(const QString &root, const QString &swingId)
{
    const QString dir = root + QStringLiteral("/2026-09-01_Mark_Wrist_01/") + swingId;
    QDir().mkpath(dir);
    QJsonObject m;
    m[QStringLiteral("schema")]  = QStringLiteral("pinpoint.swing/1");
    m[QStringLiteral("streams")] = QJsonArray{};
    // nullptr analysis: this suite is about where the CLIP goes, and a swing
    // that has not been analysed is exactly the state a clip lands into.
    QString err;
    pinpoint::SwingDocWriter::writeSwingJson(dir, m, nullptr, &err);
    return dir;
}

static QJsonObject streamElement(const QString &swingDir, const QString &alias)
{
    const QJsonObject root = pinpoint::SwingStore::load(swingDir);
    for (const QJsonValue &v : root.value(QStringLiteral("streams")).toArray()) {
        const QJsonObject el = v.toObject();
        if (el.value(QStringLiteral("alias")).toString() == alias) return el;
    }
    return {};
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    std::printf("=== PpcpClipFiler — ordering, identity and absence ===\n\n");

    const QString kAlias = QStringLiteral("iphone-wide-1f2e4491");

    // ── The clip beats the swing folder ───────────────────────────────────
    {
        std::printf("-- a clip that arrives BEFORE the folder is parked, not dropped --\n");
        QTemporaryDir tmp;
        Ppcp::PpcpImportLedger led;
        led.setPath(QDir(tmp.path()).filePath("ppcp-ledger.json").toStdString());

        PpcpClipFiler filer;
        filer.setLedger(&led);
        int filed = 0;
        QObject::connect(&filer, &PpcpClipFiler::clipFiled,
                         [&](const QString &, const QString &) { ++filed; });

        filer.onCaptureAsked(QStringLiteral("shot:1"), QStringLiteral("peer:phone-1"),
                             QStringLiteral("src:cam-wide"),
                             QStringLiteral("st:abcdef0123456789:video"), kAlias);

        // The phone is quick and the pipeline is slow.
        filer.onClipReady(makeClip(QStringLiteral("shot:1"), QStringLiteral("cap:a"),
                                   QByteArray(2048, 'A')));
        check(filed == 0, "nothing filed yet — there is nowhere to put it");
        check(led.captureCount() == 0, "and nothing in the ledger yet");

        const QString dir = makeSwing(tmp.path(), QStringLiteral("swing_0007"));
        filer.onSwingReady(dir);
        check(filed == 1, "the folder lands and the parked clip is filed");
        check(QFile::exists(dir + QStringLiteral("/") + kAlias + QStringLiteral(".mov")),
              "…as a .mov, the extension from the CONTAINER (ENC 6g)");
        check(led.captureCount() == 1, "…recorded in the ledger");

        const auto *rec = led.capture(Ppcp::CaptureKey{ "peer:phone-1", "ses:live-1", "cap:a" });
        check(rec != nullptr, "…under the opaque I34 key, all three parts");
        if (rec) {
            check(rec->swingRef.swingId == "swing_0007", "…linked to the swing");
            check(rec->swingRef.streamAlias == kAlias.toStdString(), "…and to the stream");
        }
        check(led.pendingCommits("peer:phone-1").size() == 1,
              "…and a capture_committed is OWED (I38: without it the phone cannot evict)");

        const QJsonObject el = streamElement(dir, kAlias);
        check(el.value(QStringLiteral("origin")).toObject()
                .value(QStringLiteral("transfer")).toString() == QStringLiteral("complete"),
              "swing.json says the transfer completed");
        check(!el.value(QStringLiteral("file")).toString().isEmpty(),
              "…and now names a file");

        // I34 — a re-arrival changes nothing. Two VideoInputPpcp instances can
        // assemble the same Capture, so this is the ordinary path, not a replay.
        filer.onClipReady(makeClip(QStringLiteral("shot:1"), QStringLiteral("cap:a"),
                                   QByteArray(2048, 'A')));
        check(led.captureCount() == 1, "a re-arrival is AlreadyHeld — nothing written twice");
    }

    // ── The folder beats the clip ─────────────────────────────────────────
    {
        std::printf("\n-- the ordinary order: folder first, clip second --\n");
        QTemporaryDir tmp;
        Ppcp::PpcpImportLedger led;
        led.setPath(QDir(tmp.path()).filePath("ppcp-ledger.json").toStdString());
        PpcpClipFiler filer;
        filer.setLedger(&led);

        filer.onCaptureAsked(QStringLiteral("shot:2"), QStringLiteral("peer:phone-1"),
                             QStringLiteral("src:cam-wide"),
                             QStringLiteral("st:abcdef0123456789:video"), kAlias);
        const QString dir = makeSwing(tmp.path(), QStringLiteral("swing_0008"));
        filer.onSwingReady(dir);

        // Before anything arrives, the swing already says a clip is owed.
        QJsonObject el = streamElement(dir, kAlias);
        check(el.value(QStringLiteral("origin")).toObject()
                .value(QStringLiteral("transfer")).toString() == QStringLiteral("requested"),
              "the swing records a clip as REQUESTED before it arrives");
        check(el.value(QStringLiteral("file")).toString().isEmpty(),
              "…with no file, so no reader offers it as video");

        filer.onClipReady(makeClip(QStringLiteral("shot:2"), QStringLiteral("cap:b"),
                                   QByteArray(1024, 'B')));
        el = streamElement(dir, kAlias);
        check(el.value(QStringLiteral("origin")).toObject()
                .value(QStringLiteral("transfer")).toString() == QStringLiteral("complete"),
              "…and completes in place, one element not two");
    }

    // ── The shot that produced NOTHING, which is the one that matters most ─
    {
        std::printf("\n-- a swing folder with no swing.json still takes the clip --\n");
        // ⚠ THIS IS THE CASE EVERY TEST ABOVE WAS TOO KIND TO CATCH.  They all
        // wrote a document first.  On hardware, a shot with no local camera and
        // no IMU allocates a folder and writes NOTHING into it -- three empty
        // folders, measured -- and that is exactly the shot a phone clip is most
        // valuable for: the host saw nothing and the phone saw the swing.
        QTemporaryDir tmp;
        Ppcp::PpcpImportLedger led;
        led.setPath(QDir(tmp.path()).filePath("ppcp-ledger.json").toStdString());
        PpcpClipFiler filer;
        filer.setLedger(&led);
        int filed = 0;
        QObject::connect(&filer, &PpcpClipFiler::clipFiled,
                         [&](const QString &, const QString &) { ++filed; });

        // A folder and nothing in it — what maybeJoin leaves behind when both
        // the export and the analysis fail.
        const QString dir = tmp.path() + QStringLiteral("/2026-09-01_Mark_Wrist_01/swing_0011");
        QDir().mkpath(dir);
        check(!pinpoint::SwingStore::hasDocument(dir), "no document, as the pipeline left it");

        filer.onCaptureAsked(QStringLiteral("shot:6"), QStringLiteral("peer:phone-1"),
                             QStringLiteral("src:cam-wide"),
                             QStringLiteral("st:abcdef0123456789:video"), kAlias);
        filer.onSwingReady(dir);
        check(!pinpoint::SwingStore::hasDocument(dir),
              "…and still none: a document is not invented for a clip that may never come");

        PpcpClip c = makeClip(QStringLiteral("shot:6"), QStringLiteral("cap:h"),
                              QByteArray(4096, 'H'));
        c.canonicalNs = { 1000000, 5000000, 9000000 };   // §6.1 instants, device timebase
        filer.onClipReady(c);

        check(filed == 1, "the clip is filed anyway");
        check(pinpoint::SwingStore::hasDocument(dir),
              "…and a document is created for it, once there are real bytes");
        check(QFile::exists(dir + QStringLiteral("/") + kAlias + QStringLiteral(".mov")),
              "…with the video beside it");

        const QJsonObject el = streamElement(dir, kAlias);
        const QJsonObject fr = el.value(QStringLiteral("frames")).toObject();
        check(fr.value(QStringLiteral("count")).toInt() == 3, "frames.count written");
        const QJsonArray t = fr.value(QStringLiteral("t_us")).toArray();
        check(t.size() == 3 && t.at(0).toInt() == 0 && t.at(1).toInt() == 4000
                            && t.at(2).toInt() == 8000,
              "…t_us rebased on the clip's own first frame, in microseconds");
        // Without frames the loader and the replay source both skip the element,
        // so a clip written without them is a file nothing will ever open.
        check(!fr.isEmpty(), "frames present — otherwise replay never shows it");
        // The file's frame rate, measured from those instants: three frames 4 ms
        // apart is 250 fps.  Missing, the replay assumed 30 and ran a 240 fps
        // clip at 8x, over in half a second (2 Sept 2026).
        const double fps = el.value(QStringLiteral("playback")).toObject()
                               .value(QStringLiteral("fps")).toDouble(0.0);
        check(std::abs(fps - 250.0) < 0.5, "playback.fps measured from the frame times");
    }

    // ── `absent` is an answer, not a failure ──────────────────────────────
    {
        std::printf("\n-- absent is a first-class ANSWER (I10, 7.3b) --\n");
        QTemporaryDir tmp;
        Ppcp::PpcpImportLedger led;
        led.setPath(QDir(tmp.path()).filePath("ppcp-ledger.json").toStdString());
        PpcpClipFiler filer;
        filer.setLedger(&led);

        filer.onCaptureAsked(QStringLiteral("shot:3"), QStringLiteral("peer:phone-1"),
                             QStringLiteral("src:cam-wide"),
                             QStringLiteral("st:abcdef0123456789:video"), kAlias);
        const QString dir = makeSwing(tmp.path(), QStringLiteral("swing_0009"));
        filer.onSwingReady(dir);

        PpcpClip gone = makeClip(QStringLiteral("shot:3"), QStringLiteral("cap:c"),
                                 QByteArray(), PPCP_ABSENT);
        gone.absentReason = QStringLiteral("outside_buffer");
        filer.onClipReady(gone);

        const QJsonObject o = streamElement(dir, kAlias).value(QStringLiteral("origin")).toObject();
        check(o.value(QStringLiteral("transfer")).toString() == QStringLiteral("absent"),
              "recorded as absent");
        check(o.value(QStringLiteral("reason")).toString() == QStringLiteral("outside_buffer"),
              "…with the owner's reason");
        check(o.value(QStringLiteral("completeness")).toString() == QStringLiteral("absent"),
              "…and the owner's completeness, carried not inferred");
        check(led.pendingCommits("peer:phone-1").empty(),
              "nothing is owed back: there were no bytes to hold");
    }

    // ── A shot that produced no folder ────────────────────────────────────
    {
        std::printf("\n-- a shot with no swing folder has nowhere to put a clip --\n");
        QTemporaryDir tmp;
        Ppcp::PpcpImportLedger led;
        led.setPath(QDir(tmp.path()).filePath("ppcp-ledger.json").toStdString());
        PpcpClipFiler filer;
        filer.setLedger(&led);
        int filed = 0;
        QObject::connect(&filer, &PpcpClipFiler::clipFiled,
                         [&](const QString &, const QString &) { ++filed; });

        filer.onCaptureAsked(QStringLiteral("shot:4"), QStringLiteral("peer:phone-1"),
                             QStringLiteral("src:cam-wide"),
                             QStringLiteral("st:abcdef0123456789:video"), kAlias);
        filer.onSwingFailed();
        filer.onClipReady(makeClip(QStringLiteral("shot:4"), QStringLiteral("cap:d"),
                                   QByteArray(512, 'D')));
        check(filed == 0, "not filed");
        check(led.captureCount() == 0, "and not admitted — there is no swing to link it to");
    }

    // ── Identity: a clip anchored to no shot, and one for a shot we never asked about
    {
        std::printf("\n-- a clip is matched by the Capture's own anchor (I27), never by order --\n");
        QTemporaryDir tmp;
        Ppcp::PpcpImportLedger led;
        led.setPath(QDir(tmp.path()).filePath("ppcp-ledger.json").toStdString());
        PpcpClipFiler filer;
        filer.setLedger(&led);

        filer.onCaptureAsked(QStringLiteral("shot:5"), QStringLiteral("peer:phone-1"),
                             QStringLiteral("src:cam-wide"),
                             QStringLiteral("st:abcdef0123456789:video"), kAlias);
        const QString dir = makeSwing(tmp.path(), QStringLiteral("swing_0010"));
        filer.onSwingReady(dir);

        PpcpClip orphan = makeClip(QString(), QStringLiteral("cap:e"), QByteArray(64, 'E'));
        filer.onClipReady(orphan);
        check(led.captureCount() == 0, "a clip anchored to no Shot is not filed anywhere");

        filer.onClipReady(makeClip(QStringLiteral("shot:99"), QStringLiteral("cap:f"),
                                   QByteArray(64, 'F')));
        check(led.captureCount() == 0, "a clip for a shot nothing asked about is not filed");

        // A preview frame must NEVER reach the swing library.
        PpcpClip prev = makeClip(QStringLiteral("shot:5"), QStringLiteral("cap:g"),
                                 QByteArray(64, 'G'));
        prev.preview = true;
        filer.onClipReady(prev);
        check(led.captureCount() == 0, "a PREVIEW payload never reaches the swing library");
    }

    // ── MSG 8.5 (CR-03, H18) — the filer's three decision points ──────────
    //
    // The filer decides; PpcpHostService owes and sends (asserted on the wire in
    // ppcp_arbitration_test's PpcpShotDisposition rows).  What is asserted here
    // is that each decision emits exactly one decline with 8.5's reason, names
    // the phone that owns the Capture, and that a storage failure emits none.
    struct Declined { QString shot, peer, session, reason; };
    {
        std::printf("\n-- MSG 8.5: a swing abandoned after analysis is declined `discarded` --\n");
        QTemporaryDir tmp;
        Ppcp::PpcpImportLedger led;
        led.setPath(QDir(tmp.path()).filePath("ppcp-ledger.json").toStdString());
        PpcpClipFiler filer;
        filer.setLedger(&led);
        std::vector<Declined> said;
        QObject::connect(&filer, &PpcpClipFiler::shotDeclined,
                         [&](const QString &sh, const QString &p, const QString &se,
                             const QString &r) { said.push_back({ sh, p, se, r }); });

        // Two Streams asked of one phone, one of another.
        filer.onCaptureAsked(QStringLiteral("shot:20"), QStringLiteral("peer:phone-1"),
                             QStringLiteral("src:cam-wide"), QStringLiteral("st:1"), kAlias);
        filer.onCaptureAsked(QStringLiteral("shot:20"), QStringLiteral("peer:phone-1"),
                             QStringLiteral("src:cam-tele"), QStringLiteral("st:2"),
                             QStringLiteral("tele"));
        filer.onCaptureAsked(QStringLiteral("shot:20"), QStringLiteral("peer:phone-2"),
                             QStringLiteral("src:cam-wide"), QStringLiteral("st:3"),
                             QStringLiteral("other"));
        filer.onSwingFailed();
        check(said.size() == 2, "one decline per PHONE asked, not per Stream");
        check(said.size() == 2 && said[0].shot == QStringLiteral("shot:20")
                  && said[0].peer == QStringLiteral("peer:phone-1")
                  && said[1].peer == QStringLiteral("peer:phone-2"),
              "…each naming the owning phone");
        check(said.size() == 2 && said[0].reason == QStringLiteral("discarded")
                  && said[1].reason == QStringLiteral("discarded"),
              "…with 8.5's `discarded` (\"Received, then abandoned\")");

        // 8.5i — a clip of it arriving later says it again, and is not filed.
        filer.onClipReady(makeClip(QStringLiteral("shot:20"), QStringLiteral("cap:20"),
                                   QByteArray(256, 'Z')));
        check(said.size() == 3 && said[2].reason == QStringLiteral("discarded")
                  && said[2].session == QStringLiteral("ses:live-1"),
              "a clip of the abandoned shot repeats the decline, in the clip's Session (8.5i)");
        check(led.pendingCommits("peer:phone-1").empty(), "and no capture_committed is owed");
        check(filer.stats().declined == 3, "counted");
    }
    {
        std::printf("\n-- MSG 8.5: a clip for a shot nothing asked for is declined `not_requested` --\n");
        QTemporaryDir tmp;
        Ppcp::PpcpImportLedger led;
        led.setPath(QDir(tmp.path()).filePath("ppcp-ledger.json").toStdString());
        PpcpClipFiler filer;
        filer.setLedger(&led);
        std::vector<Declined> said;
        QObject::connect(&filer, &PpcpClipFiler::shotDeclined,
                         [&](const QString &sh, const QString &p, const QString &se,
                             const QString &r) { said.push_back({ sh, p, se, r }); });

        filer.onClipReady(makeClip(QStringLiteral("shot:unasked"), QStringLiteral("cap:u"),
                                   QByteArray(64, 'U')));
        check(said.size() == 1 && said[0].reason == QStringLiteral("not_requested"),
              "declined `not_requested`");
        check(said.size() == 1 && said[0].peer == QStringLiteral("peer:phone-1")
                  && said[0].session == QStringLiteral("ses:live-1"),
              "…to the phone that owns it, in the Capture's Session");
        check(led.pendingCommits("peer:phone-1").empty(), "…and no capture_committed is owed");

        // A clip anchored to NO Shot has nothing to decline (8.5 is per Shot).
        filer.onClipReady(makeClip(QString(), QStringLiteral("cap:n"), QByteArray(64, 'N')));
        check(said.size() == 1, "a clip with no Shot anchor declines nothing");

        // A Shot that scrolled off the tracked eight WAS asked for: `discarded`.
        for (int i = 0; i < 9; ++i)
            filer.onCaptureAsked(QStringLiteral("shot:old-%1").arg(i),
                                 QStringLiteral("peer:phone-1"), QStringLiteral("src:cam-wide"),
                                 QStringLiteral("st:1"), kAlias);
        filer.onClipReady(makeClip(QStringLiteral("shot:old-0"), QStringLiteral("cap:o"),
                                   QByteArray(64, 'O')));
        check(said.size() == 2 && said[1].reason == QStringLiteral("discarded"),
              "a clip for a shot we asked about and have since forgotten is `discarded`, "
              "not `not_requested` — each value has one meaning");
    }
    {
        std::printf("\n-- MSG 8.5 / E80: a clip that cannot be WRITTEN declines nothing --\n");
        QTemporaryDir tmp;
        Ppcp::PpcpImportLedger led;
        led.setPath(QDir(tmp.path()).filePath("ppcp-ledger.json").toStdString());
        PpcpClipFiler filer;
        filer.setLedger(&led);
        int said = 0;
        QObject::connect(&filer, &PpcpClipFiler::shotDeclined,
                         [&](const QString &, const QString &, const QString &,
                             const QString &) { ++said; });
        // A "folder" whose parent is a FILE: nothing can ever be written under it.
        QFile blocker(QDir(tmp.path()).filePath(QStringLiteral("not-a-dir")));
        blocker.open(QIODevice::WriteOnly);
        blocker.write("x");
        blocker.close();
        const QString dir = blocker.fileName() + QStringLiteral("/swing_0099");

        filer.onCaptureAsked(QStringLiteral("shot:disk"), QStringLiteral("peer:phone-1"),
                             QStringLiteral("src:cam-wide"),
                             QStringLiteral("st:abcdef0123456789:video"), kAlias);
        filer.onSwingReady(dir);
        filer.onClipReady(makeClip(QStringLiteral("shot:disk"), QStringLiteral("cap:disk"),
                                   QByteArray(128, 'K')));
        check(filer.stats().failed == 1, "precondition: the write failed");
        check(said == 0, "⛔ SILENT — storage is transient and a decline is final (E80)");
        check(led.pendingCommits("peer:phone-1").empty(),
              "and nothing is committed either: the phone keeps its copy (I38)");
    }

    std::printf("\n%s (%d failure%s)\n", g_fail == 0 ? "ALL PASS" : "FAILURES",
                g_fail, g_fail == 1 ? "" : "s");
    return g_fail == 0 ? 0 : 1;
}
