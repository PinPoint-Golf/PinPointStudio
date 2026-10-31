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
.pragma library

// How the updater's state is put into words and colour — the ONE place it is written (D5).
// updateController.state is one of idle / checking / uptodate / available / downloading /
// verifying / ready / error / devbuild / unsupported. Three surfaces read it:
//   the About dialog's badge and button      (shell/PpAboutDialog.qml, style "long")
//   Settings → General's Version row         (settings/GeneralPanel.qml, style "short")
//   the Linux launch banner                  (shell/PpUpdateBanner.qml, bannerTitle/bannerDetail)
// Where the two badges word a state differently, `style` picks the wording: "long" has room for
// "Checking for updates…" and a "Check for updates" button, "short" sits in a row of chips and says
// "Checking…" / "Check now". Every other word is the same on both.
//
// Pure: colours come from the `theme` passed in (Theme), so a binding that calls these records the
// reads (the state, the progress, the theme's colours) and re-evaluates when any changes.

// The badge's tone: what kind of news the state is.
//   "busy"   a check is running             quiet text, no fill
//   "accent" an update is on offer / coming accent
//   "good"   ready to restart, or up to date
//   "warn"   the check failed
//   "quiet"  no updates here (dev build, unsupported platform)
function tone(state) {
    switch (state) {
    case "checking":    return "busy"
    case "available":
    case "downloading":
    case "verifying":   return "accent"
    case "ready":       return "good"
    case "error":       return "warn"
    case "devbuild":
    // Never checked and never can — not "Up to date", which would claim a result the updater is
    // in no position to have.
    case "unsupported": return "quiet"
    default:            return "good"         // uptodate (and idle, where the badge is hidden)
    }
}

// A tone's colours: { fg, bg } — fg is the text and the badge's border, bg its fill.
function colours(t, theme) {
    switch (t) {
    case "busy":   return { fg: theme.colorText2,  bg: "transparent" }
    case "accent": return { fg: theme.colorAccent, bg: theme.colorAccentLight }
    case "warn":   return { fg: theme.colorWarn,   bg: theme.colorWarnLight }
    case "quiet":  return { fg: theme.colorText3,  bg: "transparent" }
    default:       return { fg: theme.colorGood,   bg: theme.colorGoodLight }
    }
}

// The badge's words. `progress` (0..1) is read only while downloading.
function text(state, style, progress) {
    switch (state) {
    case "checking":    return style === "long" ? qsTr("Checking for updates…") : qsTr("Checking…")
    case "available":   return qsTr("Update available")
    case "downloading": return qsTr("Downloading %1%").arg(Math.round(progress * 100))
    case "verifying":   return qsTr("Verifying…")
    case "ready":       return qsTr("Restart to update")
    case "error":       return qsTr("Update check failed")
    case "devbuild":    return qsTr("Development build")
    case "unsupported": return qsTr("Updates not available")
    default:            return qsTr("✓  Up to date")
    }
}

// The whole badge: { text, fg, bg }. Hidden by its callers in the neutral "idle" state.
function badge(state, style, progress, theme) {
    var c = colours(tone(state), theme)
    return { text: text(state, style, progress), fg: c.fg, bg: c.bg }
}

// The contextual action beside the badge: "download" / "restart" / "check", or "" while a check,
// download or verify is in flight (nothing to press).
function actionMode(state) {
    if (state === "available") return "download"
    if (state === "ready")     return "restart"
    if (state === "checking" || state === "downloading" || state === "verifying") return ""
    return "check"
}

function actionLabel(mode, style) {
    if (mode === "download") return qsTr("Download & install")
    if (mode === "restart")  return qsTr("Restart now")
    return style === "long" ? qsTr("Check for updates") : qsTr("Check now")
}

// The launch banner's two lines (it shows only on available / downloading / verifying / ready).
function bannerTitle(state, latestVersion) {
    if (state === "ready")       return qsTr("Update ready")
    if (state === "downloading") return qsTr("Downloading update…")
    if (state === "verifying")   return qsTr("Verifying update…")
    return qsTr("Update available — %1").arg(latestVersion)
}

function bannerDetail(state, latestVersion, currentVersion, progress) {
    if (state === "ready")       return qsTr("Restart to finish installing %1").arg(latestVersion)
    if (state === "downloading") return qsTr("%1%").arg(Math.round(progress * 100))
    if (state === "verifying")   return qsTr("Checking signature")
    return qsTr("You have %1").arg(currentVersion)
}

// The banner's colour (its dot and border): good when ready to restart, accent otherwise.
function bannerColour(state, theme) {
    return state === "ready" ? theme.colorGood : theme.colorAccent
}
