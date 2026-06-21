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

#pragma once

#include <QString>

class QJsonArray;

// Pure decision logic for the Linux AppImage updater — NO QObject, network, or
// subprocess. Lifted out of UpdateController so every branch the in-app updater
// takes (version comparison, which asset to offer, whether a signature is
// trusted) is a free function that src/Update/tests can exercise directly with
// canned inputs. LinuxAppImageBackend keeps the impure parts (the GitHub GET, the
// QProcess gpg call, the rename swap) and calls these for each decision.
//
// Authoritative design: docs/design/linux_update.md.
namespace pp::update::linux_logic {

// Rank a version postfix: a clean release outranks any prerelease; rc > beta >
// alpha. (Moved verbatim from UpdateController — behaviour unchanged.)
int postfixRank(const QString &postfix);

// Compare "v<maj>.<min>[-<word><n>]" strings. >0 if a is newer than b, <0 older,
// 0 equal — OR unparseable (treated as "not newer" so the updater never prompts
// on garbage). (Moved verbatim from UpdateController.)
int compareVersion(const QString &a, const QString &b);

// Extract the offered version from an AppImage **asset filename**
// (PinPointStudio-<ver>-<archToken>.AppImage), which package_appimage.sh derives
// from version.h at build time — NOT from the git tag. Empty if the name does not
// match the expected pattern for the given arch, so a wrong-arch asset is never
// mistaken for an offer. archToken comes from PlatformTarget::assetArchToken().
QString versionFromAssetName(const QString &name, const QString &archToken);

// The outcome of scanning the GitHub Releases feed for an installable build.
struct FeedPick {
    QString appImageName;   // matched *-<archToken>.AppImage asset name
    QString sigUrl;         // detached <appImageName>.sig download URL ("" if none)
    QString notes;          // release body (markdown)
    QString version;        // versionFromAssetName(appImageName, archToken)
    bool    found = false;  // true iff a same-arch AppImage was located
};

// Find the newest non-draft release that publishes a same-arch AppImage. Releases
// without a matching AppImage (Windows-only, asset-less, or other-arch) are
// skipped, not treated as errors. `releases` is the parsed GitHub
// /releases response array (newest first); this does no network I/O.
FeedPick pickLatestAppImage(const QJsonArray &releases, const QString &archToken);

// Parse `gpg --status-fd` output and decide trust: require a single VALIDSIG line
// whose 40-hex signing-key fingerprint equals the pinned fingerprint (case-
// insensitive). The gpg process invocation itself stays in the backend; only the
// parse — the previously-untestable tail of verifySignatureBlocking — lives here.
bool isTrustedValidSig(const QString &gpgStatusOutput, const QString &pinnedFpr);

// True when a pinned fingerprint is still the all-zeros placeholder. The verify
// gate refuses every update until a real release key lands (design §6, P2); this
// isolates that refusal so it is a one-line test.
bool isPlaceholderFingerprint(const QString &fpr);

} // namespace pp::update::linux_logic
