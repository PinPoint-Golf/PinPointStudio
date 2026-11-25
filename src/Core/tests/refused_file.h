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

// pinpoint::test::RefusedFile: a file a test makes refuse the product, in a way each platform honours (#244).
//
// A test of what the app does when a read or a save fails (keep the value, report it, never lose the file) has to make
// the file system say no. POSIX honours permissions. Windows does not: QFile::setPermissions on a folder sets an
// attribute Windows ignores for the files inside it, and no permission makes a file unreadable. Windows does honour a
// share mode, so there the test holds the file open itself and leaves out of the share the access it refuses.
//
//   RefusedFile r(file, RefusedFile::Refuse::Open);
//       Nobody else can open `file`.
//       POSIX: its permissions cleared. Windows: held open, shared for delete only.
//       Either way it can still be renamed, as a POSIX file with no permissions in a writable folder can, so a set-aside
//       the product should not make still shows. A replace is refused on both platforms (QSaveFile refuses a file it
//       cannot write; Windows will not replace a file that is held open), so to prove the product made no write, a test
//       also checks the product's own write counts.
//   RefusedFile r(file, RefusedFile::Refuse::Replace);
//       `file` can be read, but not replaced or renamed.
//       POSIX: its folder made read-only (r-x), so no file can be made in the folder or renamed in it.
//       Windows: held open, shared for read and write, not for delete. Qt's and libppswing's replace and QFile::rename
//       all need delete access to the file, so each fails.
//
// refused() says whether it is in force; the `file` must exist. release() puts it back (permissions restored, the
// handle closed); the destructor does it too. Release before reading the file back, and before the temporary directory
// goes. It is test-only code (header-only, no library). Include it by relative path, e.g. from src/<Dir>/tests/:
//   #include "../../Core/tests/refused_file.h"

#if defined(_WIN32)
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QString>

namespace pinpoint::test {

class RefusedFile
{
public:
    enum class Refuse { Open, Replace };

    RefusedFile(const QString &file, Refuse what)
    {
        if (!QFileInfo(file).isFile())
            return;
#if defined(_WIN32)
        const DWORD share = what == Refuse::Open ? DWORD(FILE_SHARE_DELETE) : DWORD(FILE_SHARE_READ | FILE_SHARE_WRITE);
        m_handle = ::CreateFileW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(file).utf16()), GENERIC_READ, share,
                                 nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        m_on = m_handle != INVALID_HANDLE_VALUE;
#else
        m_target = what == Refuse::Open ? file : QFileInfo(file).absolutePath();
        m_was    = QFile::permissions(m_target);
        m_on     = QFile::setPermissions(m_target, what == Refuse::Open
                                                       ? QFileDevice::Permissions()
                                                       : QFileDevice::ReadOwner | QFileDevice::ExeOwner);
#endif
    }
    RefusedFile(const RefusedFile &) = delete;
    RefusedFile &operator=(const RefusedFile &) = delete;
    ~RefusedFile() { release(); }

    bool refused() const { return m_on; }

    void release()
    {
        if (!m_on)
            return;
        m_on = false;
#if defined(_WIN32)
        ::CloseHandle(m_handle);
        m_handle = INVALID_HANDLE_VALUE;
#else
        QFile::setPermissions(m_target, m_was);
#endif
    }

private:
    bool m_on = false;
#if defined(_WIN32)
    HANDLE m_handle = INVALID_HANDLE_VALUE;
#else
    QString                  m_target;
    QFileDevice::Permissions m_was;
#endif
};

} // namespace pinpoint::test
