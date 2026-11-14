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

// swing_store.h — THE door to a swing's document on disk.
//
// A swing directory holds its document in one of two formats:
//   swing.ppsw  the binary format (libppswing, docs/implementation/swing_storage_impl.md),
//               written by every writer since the Phase 2 switch (Sept 2026);
//   swing.json  the legacy text format, read indefinitely and never written again.
//
// Every reader and writer goes through here, so "is there a document", "how old is it" and
// "read it" have one answer whichever format the swing is in. A directory holding both is
// mid-conversion or was rewritten by this code, and swing.ppsw wins: it is always the newer.
//
// ⚠ THIS HEADER NAMES NO libppswing TYPE. It is included by translation units that compile
// into dozens of test binaries; the ppsw headers stay inside swing_store.cpp and ppsw_qt.cpp,
// the two files of the pinpoint_swingstore library every one of those targets links.

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <functional>
#include <utility>

namespace pinpoint::SwingStore {

enum class Format { None, Ppsw, Json };

struct DocInfo {
    Format  format  = Format::None;
    QString path;          // the file that will be read; empty when there is none
    qint64  size    = 0;
    qint64  mtimeMs = 0;
    bool exists() const { return format != Format::None; }
};

QString ppswPath(const QString &swingDir);          // <dir>/swing.ppsw
QString jsonPath(const QString &swingDir);          // <dir>/swing.json
QString legacySummaryPath(const QString &swingDir); // <dir>/swing_summary.json (JSON-era sidecar)

DocInfo info(const QString &swingDir);
bool    hasDocument(const QString &swingDir);

// IS THIS STILL THE DOCUMENT A SIDECAR WAS STAMPED FROM? Every cache written to disk beside a swing
// (swing_phasegrid.json, a JSON-era swing_summary.json) records the document's size and mtime when it
// was built, and every reader of such a stamp asks HERE, so they all agree on what "stale" means
// (issue #174: the Club delivery note counted copied grids as stale while the grid reader rebuilt them).
//
// Same size, and the same mtime — at the resolution the document's clock still has. A copy or restore
// can carry a document's mtime to the whole second only: macOS's openrsync (rsync 2.6.9 compatible)
// truncates mtimes when it copies with -a, so 1794051638750 came back as 1794051638000 on 11 Nov 2026
// (the share itself, smbfs onto NTFS, keeps 100 ns; other old copy tools truncate too). So when the
// document's mtime NOW has no
// milliseconds, the stamp matches within that second. Safe because the other direction is never
// relaxed (a stamp of …000 against a document at …001 is a rewrite), and a false match needs a rewrite
// of the very same byte size inside the same second — a re-analysis takes far longer than that, and a
// filesystem that only ever keeps whole seconds already had that blind spot with an exact compare.
// A copy that resets the mtime (plain cp) is a different file to any stat, and still reads stale.
//
// In-process comparisons of two stats of the same file (LazyDocument::stillCurrent) stay exact.
inline bool sameDocumentStamp(qint64 stampSize, qint64 stampMtimeMs, qint64 size, qint64 mtimeMs)
{
    if (stampSize != size) return false;
    if (stampMtimeMs == mtimeMs) return true;
    return mtimeMs > 0 && stampMtimeMs > 0 && mtimeMs % 1000 == 0 && stampMtimeMs / 1000 == mtimeMs / 1000;
}

// The whole document. Empty on no document or any read/parse failure; *error says which.
QJsonObject load(const QString &swingDir, QString *error = nullptr);

// ONE FULL PARSE FOR SEVERAL READERS OF THE SAME SWING (issue #142). The phase-grid reader and
// the summary reader each fall back to load() when their sidecar (or, for a swing.ppsw, its summary
// block) cannot answer. A freshly filed JSON-era swing misses both sidecars: two full parses of a
// ~33 MB document, back to back, for one shot. A caller that runs both readers hands each the same
// LazyDocument, and whichever needs the document first pays for the parse; the other reuses it. A
// reader that is answered without the document never touches it, so a swing whose sidecars are
// warm (or a .ppsw with a current summary block, for the summary) parses nothing on its behalf.
//
// NOT A CACHE AND NOT SHARED: it lives on the caller's stack for one shot's reads and is never
// handed to another thread. The document it holds is the one on disk at the moment of the parse. A
// second reader that runs later gets THAT document, not a re-read: under a concurrent rewrite it
// can be the older one. That is harmless for anything it returns (a caller that stamps the
// document before reading, as SessionDiagnosticsModel::detectShot does, regrades on the next
// activation) and is never indexed: stillCurrent() below gates every sidecar write.
//
// ONLY A GOOD PARSE IS KEPT. An empty result (no document, a sharing violation, a scanner holding
// the file, an exporter mid-rename, a corrupt file) is not latched: the next root() tries again, so
// the second reader gets the same chance it had when each reader parsed for itself.
class LazyDocument {
public:
    explicit LazyDocument(QString swingDir) : m_dir(std::move(swingDir)) {}
    const QJsonObject &root()
    {
        if (!m_loaded) {
            const DocInfo before = info(m_dir);   // stamped BEFORE the read, as every sidecar guard is
            QJsonObject   r      = load(m_dir);
            if (!r.isEmpty()) {
                m_info   = before;
                m_root   = std::move(r);
                m_loaded = true;
            }
        }
        return m_root;     // empty after a failed parse; the next call retries
    }
    bool           loaded() const   { return m_loaded; }
    // The document's size and mtime as stamped just before the parse root() holds (empty if none).
    const DocInfo &parsedInfo() const { return m_info; }
    const QString &swingDir() const { return m_dir; }

    // Drop the parsed document (it can be ~100 MB in memory). A later root() parses again.
    void release()
    {
        m_root   = QJsonObject();
        m_info   = DocInfo();
        m_loaded = false;
    }

    // Is the document on disk still the one root() parsed? A reader that writes a sidecar guarded
    // on the file's CURRENT size and mtime asks first: a rewrite between the parse and the write
    // would otherwise index old content under the new document's guard. False when nothing parsed.
    bool stillCurrent() const
    {
        if (!m_loaded) return false;
        const DocInfo now = info(m_dir);
        return now.format == m_info.format && now.size == m_info.size && now.mtimeMs == m_info.mtimeMs;
    }

private:
    QString     m_dir;
    QJsonObject m_root;
    DocInfo     m_info;
    bool        m_loaded = false;
};

// Only the `summary` block, read from the root chunk of a swing.ppsw without decoding the rest.
// Empty for a JSON document, a .ppsw written without one, or a read failure.
//
// heads, when given, receives what swingDeviceTokens() reads, out of the same root chunk:
// { streams: [ {kind, alias, file, setup, instrument} … ], launchMonitor: { kind } } — each stream
// trimmed to those keys BEFORE conversion, so its frame stamps and IMU samples are never copied.
// For a summary block written before it carried `devices` (swing_doc_reader.cpp readSwingSummary).
QJsonObject loadSummaryBlock(const QString &swingDir, QJsonObject *heads = nullptr);

// THE DOCUMENT WITHOUT ITS PER-FRAME TRACKS — everything a reader of the swing's ANALYSIS needs
// (clock, capture, review, streams, analysis.metrics/phases/kinematicSequence and the rest of the
// analysis block's small fields) and none of the pose. The pose is the bulk of the ~28 MB
// document whose full parse froze the GUI at the end of a session (ShotListModel::loadSessionDir,
// 2026-09-16); a reader of metric curves was paying 150–220 ms a swing on the share to throw
// nearly all of it away. This reads the root chunk and the chunks on the path to
// /analysis/metrics only — 6–9 ms a swing (the Charts panel's SessionSeriesModel).
//
// WHAT IS OMITTED, and the rule is one rule whatever the writer's chunking:
//   · every chunk not on the path to /analysis/metrics is never read, and its key is removed;
//   · analysis.{pose2d, poseDtl, skeleton3d, club, clubDtl, club3d, ball, impact} — the
//     per-frame tracks — are removed even where a small one sat inline in a chunk that WAS read,
//     so a caller never sees a track on one swing and not on the next because of where the
//     writer split (splitBytes).
// When a document's `analysis` was itself split out ("/analysis", 2 of 50 swings on 7 Oct 2026:
// its inline children alone ran past splitBytes), that chunk is read — it is on the path — and
// descended like the root: its metrics ref is followed, its other refs dropped unread.
//
// A JSON-era swing.json has no chunks: it is parsed WHOLE (the full cost) and the same keys are
// removed, so the result has the same shape. Empty on no document or a read failure.
QJsonObject loadAnalysis(const QString &swingDir, QString *error = nullptr);

// The document with only the named chunks read (paths such as "/analysis/skeleton3d"): everything
// the root holds inline, plus each chunk on the path to one of `chunkPaths`. Every other chunk is
// left unread and its key removed. A JSON-era swing.json has no chunks and comes back whole. Empty
// on no document or a read failure. For a caller reading many swings at once — the 3-D panel's
// session view — where a full load() would convert every pose track only to throw it away.
QJsonObject loadParts(const QString &swingDir, const QStringList &chunkPaths, QString *error = nullptr);

// Write the document as swing.ppsw (atomic: temp file + rename), then remove any swing.json and
// swing_summary.json beside it — the rewrite supersedes both. Removing them is what migrates a
// JSON-era swing the first time anything rewrites it.
bool save(const QString &swingDir, const QJsonObject &root, QString *error = nullptr);

// The same encoding save() uses, to an arbitrary path (no sidecar cleanup). For tools that write
// a document somewhere other than a swing directory.
bool writeFile(const QString &path, const QJsonObject &root, QString *error = nullptr);

// Convert one JSON-era swing, proving the result before touching the original:
//   parse swing.json with libppswing's own parser (exact int/double, the path proven 170/170 on
//   the corpus) → add `summary` from summaryFn → write swing.ppsw → reopen it with a fresh
//   reader → semantic equality of (everything but /summary) against the parse.
// Only on EQUAL is swing.json (and swing_summary.json) deleted, and only when deleteJson is set.
// A failed verify removes the .ppsw it wrote and leaves the JSON exactly as it was.
struct ConvertResult {
    enum class Outcome { Converted, AlreadyPpsw, NoDocument, Failed };
    Outcome outcome   = Outcome::Failed;
    qint64  jsonBytes = 0;
    qint64  ppswBytes = 0;
    QString error;     // Failed: what failed, including the first differing JSON Pointer
};
using SummaryFn = std::function<QJsonObject(const QJsonObject &root, const QString &swingDir)>;
ConvertResult convertDir(const QString &swingDir, const SummaryFn &summaryFn, bool deleteJson);

// Every directory under root (root itself included) that holds a swing document, sorted.
// Dot-directories (.pinpoint-trash) are not entered: a trashed swing is the user's to restore.
QStringList swingDirsUnder(const QString &root);

// The document as compact JSON text — the interchange form (zip export, `ppsw dump`).
QByteArray toJsonText(const QJsonObject &root);

// IS THIS VIDEO STREAM ON THE SWING'S CLOCK? A `streams[]` video element's frames.t_us are the
// window's (relative to clock.t0_us) — except a clip filed late by the phone path, which
// ppcp_clip_filer.cpp times from ITS OWN first frame (exact for playback, not aligned to the
// window) and marks so: `frames.clock: "clip"` (kFramesClockClip, written by
// SwingDocWriter::updateStreamOrigin). ABSENT MEANS THE WINDOW'S CLOCK, which is right for every
// stream ever written before the marker, since none of them is a phone clip. A reader that needs
// the window clock (the capture check's coverage half, issue #53) leaves a stream that answers
// false out of anything timed against the window. (It is a statement about the clock, not about
// where the bytes came from: `origin` is provenance and analysis never reads it — swing_doc_test.)
inline constexpr char kFramesClockClip[] = "clip";
inline bool streamOnWindowClock(const QJsonObject &stream)
{
    return stream.value(QStringLiteral("frames")).toObject().value(QStringLiteral("clock")).toString()
           != QLatin1String(kFramesClockClip);
}

} // namespace pinpoint::SwingStore
