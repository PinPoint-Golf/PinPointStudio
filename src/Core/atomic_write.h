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

// atomicWrite — the one way a file is replaced (R-PERS-2; docs/developer/information_model.md). Platform
// ring: Qt only, no PinPoint type, so every ring may use it (src/Gui/tests/architecture_lint_test.cpp).
//
// Write a file via a temporary beside it and a rename over it, so a crash, a kill or a full disk leaves
// either the old file or the new one, never half of either. Three forms, one mechanism:
//
//     atomicWrite(path, bytes)            the whole content in memory
//     atomicWrite(path, writer)           a callback that streams into the temporary (a copy, an image
//                                         encoder, anything that wants a QIODevice)
//     AtomicFile f(path); f.open(); …; f.commit()
//                                         the same, held across calls: the PDF writer's pages, the raw
//                                         sidecar written frame by frame inside the export loop
//
// Each returns an AtomicWriteResult { ok, error, detail }: `error` is a sentence a user can act on and
// names the file; `detail` is the operating system's own reason, for the log.
//
// It is QSaveFile underneath:
//   - the temporary has a UNIQUE name beside `path`, so two processes saving one file (the app and
//     tools/diagnostics/regrade_ledger) cannot write into each other's temporary, and nothing named
//     `<path>.tmp` can get in the way;
//   - commit() flushes it, asks the OS to sync it, and renames it OVER the original in one step
//     (rename(2) on macOS and Linux, a replacing move on Windows). There is no moment at which `path`
//     does not exist — never remove-then-rename;
//   - the direct-write fallback is OFF, so a directory in which no temporary can be made fails the save
//     rather than writing the file in place (which would bring back the truncation this exists to
//     prevent).
// A failure at any step — open, a short write, a write error that only surfaces on the flush, the
// rename — returns ok = false, leaves the original byte-identical, and deletes the temporary. An
// existing file that is itself read-only is refused, not replaced.
//
// The parent directory is made by default (AtomicWriteOptions::makeParent). A site that writes INTO a
// folder that must already exist (a swing folder, a session folder) passes makeParent = false: a
// folder trashed under it must not come back holding one lone sidecar.
//
// It does not sync the directory after the rename. The one writer that does, and the one exception to
// "one helper": the swing file (swing.ppsw), written by libppswing's own writeFileAtomic, which syncs
// the file and its folder (D8, build/refactor/step-2/BRIEF.md §8.1).
//
// Header-only on purpose: there is no Core library and every test compiles the sources it needs, so a
// .cpp would have to be added to every link line that reaches one of the call sites.
//
//   cmake --build build/tests --target atomic_write_test -j 6
//   ctest --test-dir build/tests -R '^atomic_write_test$' --output-on-failure

#include <QByteArray>
#include <QDir>
#include <QFileDevice>
#include <QFileInfo>
#include <QIODevice>
#include <QSaveFile>
#include <QString>

#include "post_shot_trace.h"   // [PostShot] (refactor 3.7-S): the commit's cost, on the GUI thread only

#include <functional>

namespace pinpoint {

struct AtomicWriteResult {
    bool    ok = false;
    QString error;    // for the user: what failed, naming the file. Empty when ok.
    QString detail;   // for the log: the operating system's reason (QFileDevice::errorString()). May be empty.
    explicit operator bool() const { return ok; }
};

struct AtomicWriteOptions {
    bool makeParent = true;   // make the parent directory first. False: it must already exist.
    bool text       = false;  // open in QIODevice::Text (CRLF line ends on Windows), for a text file

    // A file written INTO a folder that must already be there (a swing or session folder).
    static AtomicWriteOptions intoExistingFolder() { AtomicWriteOptions o; o.makeParent = false; return o; }
};

// The streaming form, held across calls. Not copyable; an AtomicFile that is destroyed without a
// successful commit() leaves the original untouched and its temporary deleted.
class AtomicFile
{
public:
    explicit AtomicFile(const QString &path, AtomicWriteOptions options = {})
        : m_path(path), m_options(options), m_file(path) {}
    AtomicFile(const AtomicFile &) = delete;
    AtomicFile &operator=(const AtomicFile &) = delete;
    ~AtomicFile() { if (m_file.isOpen()) m_file.cancelWriting(); }

    const QString &path() const { return m_path; }
    bool isOpen() const { return m_file.isOpen(); }

    // Make the temporary. A failure here touches nothing.
    AtomicWriteResult open()
    {
        if (m_path.isEmpty())
            return failed(QStringLiteral("No file name was given."));
        if (m_options.makeParent)
            QDir().mkpath(QFileInfo(m_path).absolutePath());
        m_file.setDirectWriteFallback(false);   // the default; set so nobody has to look it up
        QIODevice::OpenMode mode = QIODevice::WriteOnly;
        if (m_options.text)
            mode |= QIODevice::Text;
        m_shortWrite = false;
        if (!m_file.open(mode))
            return failed(QStringLiteral("Could not write to %1.").arg(m_path));
        return { true, {}, {} };
    }

    // The temporary, for a writer that needs a device (QPdfWriter, QImage::save). Null until open().
    // A write through it that fails is remembered by QSaveFile and fails commit().
    QIODevice *device() { return m_file.isOpen() ? &m_file : nullptr; }

    // A short write is remembered: commit() then refuses, so a truncated temporary never replaces the
    // original.
    bool write(const char *data, qint64 size)
    {
        if (!m_file.isOpen() || m_file.write(data, size) != size) {
            m_shortWrite = true;
            return false;
        }
        return true;
    }
    bool write(const QByteArray &bytes) { return write(bytes.constData(), bytes.size()); }

    // Rename the temporary over `path`, or — after a short write — discard it.
    AtomicWriteResult commit()
    {
        if (!m_file.isOpen())
            return failed(QStringLiteral("Could not write to %1.").arg(m_path));
        if (m_shortWrite) {
            const QString why = m_file.errorString();
            m_file.cancelWriting();
            return { false, diskFull(), why };
        }
        // ⏱ The flush of what is still buffered, the sync to disk (FlushFileBuffers on Windows, fsync elsewhere) and
        // the rename, as one step: the line shows it under whichever step wrote the file.
        PostShotTrace::Scope trace("commit");
        if (!m_file.commit()) {
            return { false,
                     m_file.error() == QFileDevice::RenameError
                         ? QStringLiteral("Could not replace %1.").arg(m_path)
                         : diskFull(),
                     m_file.errorString() };
        }
        return { true, {}, {} };
    }

    // Discard the temporary; the original is untouched.
    void cancel() { if (m_file.isOpen()) m_file.cancelWriting(); }

    // The OS's reason for the last failure (QFileDevice::errorString()).
    QString errorString() const { return m_file.errorString(); }

    // "Could not write to <path> — the disk may be full." — a writer's failure with no reason of its own.
    QString diskFull() const { return QStringLiteral("Could not write to %1 — the disk may be full.").arg(m_path); }

private:
    AtomicWriteResult failed(const QString &error) const { return { false, error, m_file.errorString() }; }

    QString            m_path;
    AtomicWriteOptions m_options;
    QSaveFile          m_file;
    bool               m_shortWrite = false;
};

// Write `bytes` to `path` whole.
inline AtomicWriteResult atomicWrite(const QString &path, const QByteArray &bytes, AtomicWriteOptions options = {})
{
    AtomicFile f(path, options);
    if (AtomicWriteResult r = f.open(); !r.ok)
        return r;
    f.write(bytes);   // a short write is remembered; commit() reports it and keeps the original
    return f.commit();
}

// The yes/no form, for a caller that wants only whether it landed and, when not, the reason in the user's
// words. An empty `path` is refused in its own words: it is what a caller gets from an app-data location
// that does not exist (`diagnosticsFilePath`), and "could not write ''" would say nothing. Moved here from
// src/Diagnostics/engine/pack_io (refactor step 3.2), where it wrapped this helper from a rules directory.
inline bool atomicWrite(const QString &path, const QByteArray &bytes, QString *whyNot)
{
    if (path.isEmpty()) {
        if (whyNot) *whyNot = QStringLiteral("No writable application data location.");
        return false;
    }
    const AtomicWriteResult r = atomicWrite(path, bytes);
    if (!r.ok && whyNot) *whyNot = r.error;
    return r.ok;
}

// The callback form: `writer` streams the content into `out` and returns false to abandon the write,
// optionally with a reason in `whyNot` (otherwise the result says the disk may be full).
using AtomicWriter = std::function<bool(QIODevice &out, QString &whyNot)>;

inline AtomicWriteResult atomicWrite(const QString &path, const AtomicWriter &writer, AtomicWriteOptions options = {})
{
    AtomicFile f(path, options);
    if (AtomicWriteResult r = f.open(); !r.ok)
        return r;
    QString whyNot;
    if (!writer || !writer(*f.device(), whyNot)) {
        const QString detail = f.errorString();
        f.cancel();
        return { false, whyNot.isEmpty() ? f.diskFull() : whyNot, detail };
    }
    return f.commit();
}

} // namespace pinpoint
