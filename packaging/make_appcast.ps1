<#
.SYNOPSIS
    Sign a Windows installer with the offline EdDSA key and emit the WinSparkle
    appcast (appcast-win.xml) for a release. Phase P2 of
    docs/implementation/windows_update_impl.md — see also windows_release_runbook.md.

.DESCRIPTION
    WinSparkle's feed is a Sparkle appcast XML hosted as a release asset (design
    docs/design/windows_update.md §4.1). This script:
      1. resolves the version (display + monotonic build number) from src/Core/version.h,
      2. signs the `-core` installer with the offline EdDSA private key via
         winsparkle-tool (the signature WinSparkle pins-verifies before running it),
      3. writes appcast-win.xml with the enclosure URL pointing at the SPECIFIC release
         tag (so the signed bytes match what is downloaded).

    The PRIVATE KEY NEVER LEAVES YOUR MACHINE and is never a CI secret — this script
    runs locally as part of the maintainer's signing step. Sign the EXACT bytes you
    will publish (the CI-built draft installer), not a local rebuild.

.PARAMETER PrivateKeyFile
    Path to the offline EdDSA private key (from `winsparkle-tool generate-key`).

.PARAMETER Tag
    The git tag of the GitHub release (used to build the enclosure download URL:
    https://github.com/<Repo>/releases/download/<Tag>/<installer>). Free-form.

.PARAMETER Installer
    Path to the `-core` installer to sign (the WinSparkle enclosure). If omitted, the
    newest PinPointStudioSetup-*-core.exe under the build dirs is used.

.PARAMETER NotesUrl
    Optional URL shown as "what's new" in WinSparkle's window. Typically
    https://github.com/<Repo>/releases/download/<Tag>/release-notes-win.html

.EXAMPLE
    pwsh -File packaging\make_appcast.ps1 -PrivateKeyFile C:\keys\pinpoint_win.key -Tag v0.1-alpha2
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$PrivateKeyFile,
    [Parameter(Mandatory)][string]$Tag,
    [string]$Installer = '',
    [string]$Repo      = 'PinPoint-Golf/PinPointStudio',
    [string]$NotesUrl  = '',
    [string]$OutFile   = '',
    [string]$ToolPath  = ''
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot

# ── winsparkle-tool (fetched by CMake under the build dir's _deps) ─────────────
if (-not $ToolPath) {
    $cand = Get-ChildItem -Path (Join-Path $repoRoot 'build') -Recurse -Filter 'winsparkle-tool.exe' `
            -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($cand) { $ToolPath = $cand.FullName }
}
if (-not $ToolPath -or -not (Test-Path $ToolPath)) {
    throw "winsparkle-tool.exe not found (build the app once so CMake fetches WinSparkle, or pass -ToolPath)."
}
if (-not (Test-Path $PrivateKeyFile)) { throw "Private key not found: $PrivateKeyFile" }

# ── Installer (the -core enclosure) ───────────────────────────────────────────
if (-not $Installer) {
    $cand = Get-ChildItem -Path (Join-Path $repoRoot 'build') -Recurse -Filter 'PinPointStudioSetup-*-core.exe' `
            -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if ($cand) { $Installer = $cand.FullName }
}
if (-not $Installer -or -not (Test-Path $Installer)) {
    throw "No -core installer found. Build it with packaging\build_installer.ps1 -Components core, or pass -Installer."
}
$Installer = (Resolve-Path -LiteralPath $Installer).Path   # absolute, so the appcast path is too
$installerName = Split-Path -Leaf $Installer
$length = (Get-Item $Installer).Length

# ── Version (single source of truth: src/Core/version.h) ──────────────────────
$verH = Get-Content (Join-Path $repoRoot 'src\Core\version.h') -Raw
function Get-VerField($pattern) {
    $m = [regex]::Match($verH, $pattern)
    if (-not $m.Success) { throw "Could not parse version.h ($pattern)" }
    return $m.Groups[1].Value
}
$major   = Get-VerField '#define\s+PINPOINT_VERSION_MAJOR\s+(\d+)'
$minor   = Get-VerField '#define\s+PINPOINT_VERSION_MINOR\s+(\d+)'
$postfix = [regex]::Match($verH, '#define\s+PINPOINT_VERSION_POSTFIX\s+"([^"]*)"').Groups[1].Value
$build   = Get-VerField '#define\s+PINPOINT_VERSION_BUILD\s+(\d+)'
$shortVersion = "v$major.$minor$postfix"   # display (sparkle:shortVersionString)
# $build is the monotonic compare key (sparkle:version) — matches win_sparkle_set_app_build_version.

# ── Sign the EXACT installer bytes ────────────────────────────────────────────
Write-Host "Signing $installerName with EdDSA key…" -ForegroundColor Cyan
$signature = (& $ToolPath sign --private-key-file $PrivateKeyFile $Installer | Select-Object -First 1).Trim()
if (-not $signature) { throw "winsparkle-tool sign produced no signature." }

$enclosureUrl = "https://github.com/$Repo/releases/download/$Tag/$installerName"
$pubDate = (Get-Date).ToUniversalTime().ToString('r')   # RFC 822

$notesElement = ''
if ($NotesUrl) {
    $notesElement = "      <sparkle:releaseNotesLink>$NotesUrl</sparkle:releaseNotesLink>`n"
}

# ── Emit appcast-win.xml ──────────────────────────────────────────────────────
$xml = @"
<?xml version="1.0" encoding="utf-8"?>
<rss version="2.0" xmlns:sparkle="http://www.andymatuschak.org/xml-namespaces/sparkle">
  <channel>
    <title>PinPoint Studio</title>
    <description>PinPoint Studio updates</description>
    <language>en</language>
    <item>
      <title>PinPoint Studio $shortVersion</title>
$notesElement      <pubDate>$pubDate</pubDate>
      <enclosure
        url="$enclosureUrl"
        sparkle:version="$build"
        sparkle:shortVersionString="$shortVersion"
        sparkle:os="windows"
        length="$length"
        type="application/octet-stream"
        sparkle:edSignature="$signature" />
    </item>
  </channel>
</rss>
"@

if (-not $OutFile) { $OutFile = Join-Path (Split-Path -Parent $Installer) 'appcast-win.xml' }
# Write UTF-8 WITHOUT a BOM — a BOM before <?xml trips some feed parsers. (PS 5.1's
# Out-File -Encoding utf8 emits a BOM, so use the .NET writer with BOM-less encoding.)
[System.IO.File]::WriteAllText($OutFile, $xml, (New-Object System.Text.UTF8Encoding($false)))

Write-Host ""
Write-Host "appcast written: $OutFile" -ForegroundColor Green
Write-Host "  version (compare): $build   short: $shortVersion" -ForegroundColor Green
Write-Host "  enclosure:         $enclosureUrl" -ForegroundColor Green
Write-Host "  length:            $length bytes" -ForegroundColor Green
Write-Host ""
Write-Host "Next: upload BOTH $installerName and appcast-win.xml to release $Tag, then publish" -ForegroundColor Yellow
Write-Host "      it non-prerelease so releases/latest/download/appcast-win.xml resolves." -ForegroundColor Yellow
