<#
.SYNOPSIS
    Local release: build the Windows installer from THIS working tree and stage it
    where another account on this PC can install it over its official copy.

.DESCRIPTION
    The official binary on a test/production account is a per-user Inno Setup
    install (%LOCALAPPDATA%\Programs\PinPointStudio), which no other account can
    write into. So a local release is not "overwrite the files": it is the same
    installer the release runbook produces, minus signing, appcast and GitHub,
    dropped into a folder both accounts can read (C:\PinPointStudio is shared,
    Authenticated Users have Modify) and run from the other account - an in-place
    upgrade of that account's install, no admin prompt.

    1. Builds the installer with packaging\build_installer.ps1 (Release,
       PP_SHIPPING_BUILD=ON, incremental in build\Release-Installer).
    2. Copies it to the drop folder with the git sha in the name (LATEST.txt notes
       whether the tree had uncommitted changes).
    3. Optionally (-InstallHere) upgrades THIS account's own install silently,
       as a smoke test of the package. Never launches the app.

    The version number is NOT changed: a local release carries the same
    PINPOINT_VERSION_* as the last official release, so the updater sees an
    equal build number and stays quiet, and the next official release bumps
    BUILD exactly as the runbook says. The sha in the file name is how you tell
    local builds apart. Never sign, appcast or upload a local release.

.PARAMETER Components
    'core' (default) - the app; the same payload an update ships. 'both' adds the
    CUDA/cuDNN runtime (use when the GPU runtime itself changed; ~1.7 GB).
    'cuda' - the GPU runtime alone.

.PARAMETER QtPrefix
    Qt kit to build against. Default: the newest C:\Qt\6.*\msvc2022_64 present
    (build_installer.ps1's own default names a kit this box may not have).

.PARAMETER DropDir
    Where the installer is staged for the other account. Default
    C:\PinPointStudio\local-release.

.PARAMETER InstallHere
    After staging, run the installer silently for the current account
    (/SILENT /SUPPRESSMSGBOXES /NORESTART /CLOSEAPPLICATIONS). A running
    PinPointStudio on this account is closed by the installer.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File packaging\local_release.ps1
    powershell -ExecutionPolicy Bypass -File packaging\local_release.ps1 -InstallHere
    powershell -ExecutionPolicy Bypass -File packaging\local_release.ps1 -Components both
#>
[CmdletBinding()]
param(
    [ValidateSet('core','both','cuda')]
    [string]$Components = 'core',
    [string]$QtPrefix = '',
    [string]$DropDir = 'C:\PinPointStudio\local-release',
    [switch]$InstallHere
)
# 'Continue', not 'Stop', for the same reason as build_installer.ps1: native tools
# write benign lines to stderr and PowerShell 5.1 would abort on them under 'Stop'.
$ErrorActionPreference = 'Continue'

$repo = Split-Path -Parent $PSScriptRoot

# ── Qt kit: newest 6.x MSVC kit actually installed ──────────────────────────
if (-not $QtPrefix) {
    $kits = Get-ChildItem 'C:\Qt' -Directory -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -match '^6\.\d+' } |
            Sort-Object { [version]$_.Name } -Descending
    foreach ($k in $kits) {
        $candidate = Join-Path $k.FullName 'msvc2022_64'
        if (Test-Path (Join-Path $candidate 'bin\qmake.exe')) { $QtPrefix = $candidate; break }
    }
    if (-not $QtPrefix) { throw "No Qt 6 MSVC kit found under C:\Qt; pass -QtPrefix." }
}

# ── Identity of what is being built ─────────────────────────────────────────
$sha   = (& git -C $repo rev-parse --short HEAD 2>$null)
if (-not $sha) { $sha = 'nogit' }
$dirty = ''
if ((& git -C $repo status --porcelain 2>$null | Measure-Object).Count -gt 0) { $dirty = '-dirty' }
$branch = (& git -C $repo rev-parse --abbrev-ref HEAD 2>$null)

Write-Host ""
Write-Host "Local release: $branch @ $sha$dirty  components=$Components  qt=$QtPrefix" -ForegroundColor Cyan
if ($dirty) {
    Write-Host "  working tree has uncommitted changes - LATEST.txt will say so" -ForegroundColor Yellow
}

# ── 1. Build the installer (same script, same build dir as a real release) ──
$before = Get-Date
try {
    & (Join-Path $PSScriptRoot 'build_installer.ps1') -Components $Components -QtPrefix $QtPrefix
} catch {
    throw "build_installer.ps1 failed: $($_.Exception.Message)"
}

# ── 2. Find the installer this run produced ─────────────────────────────────
$buildDir = Join-Path $repo 'build\Release-Installer'
$all = Get-ChildItem $buildDir -Filter 'PinPointStudioSetup-*.exe' | Where-Object { $_.LastWriteTime -ge $before }
switch ($Components) {
    'core' { $all = $all | Where-Object { $_.Name -like '*-core.exe' } }
    'cuda' { $all = $all | Where-Object { $_.Name -like '*-cuda.exe' } }
    'both' { $all = $all | Where-Object { $_.Name -notlike '*-core.exe' -and $_.Name -notlike '*-cuda.exe' } }
}
$exe = $all | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $exe) { throw "No fresh PinPointStudioSetup-*.exe for '$Components' under $buildDir" }

# ── 3. Stage it where the other account can reach it ────────────────────────
New-Item -ItemType Directory -Force -Path $DropDir | Out-Null
$destName = '{0}-local-{1}.exe' -f $exe.BaseName, $sha
$dest     = Join-Path $DropDir $destName
Copy-Item -Path $exe.FullName -Destination $dest -Force
$note = @(
    "file:       $destName",
    "built:      $(Get-Date -Format 'yyyy-MM-dd HH:mm')",
    "commit:     $branch @ $sha$dirty",
    "components: $Components",
    "install:    run the file from the account that should get it (in-place upgrade, no admin),",
    "            or unattended:  `"$dest`" /SILENT /SUPPRESSMSGBOXES /NORESTART /CLOSEAPPLICATIONS"
) -join "`r`n"
$note | Out-File -FilePath (Join-Path $DropDir 'LATEST.txt') -Encoding utf8

Write-Host ""
Write-Host "Staged: $dest" -ForegroundColor Green
Write-Host ("        {0:N0} MB" -f ($exe.Length / 1MB))
Write-Host ""
Write-Host "To promote it: sign in to the account that runs the official build and run" -ForegroundColor Cyan
Write-Host "    $dest"
Write-Host "or, unattended from that account:"
Write-Host "    `"$dest`" /SILENT /SUPPRESSMSGBOXES /NORESTART /CLOSEAPPLICATIONS"

# ── 4. Optional: upgrade THIS account's install as a package smoke test ─────
if ($InstallHere) {
    Write-Host ""
    Write-Host "Installing on this account ($env:USERNAME) silently..." -ForegroundColor Cyan
    $p = Start-Process -FilePath $dest `
                       -ArgumentList '/SILENT','/SUPPRESSMSGBOXES','/NORESTART','/CLOSEAPPLICATIONS' `
                       -Wait -PassThru
    if ($p.ExitCode -ne 0) { throw "Installer exited with code $($p.ExitCode)" }
    # The CPack layout puts the exe under bin\; search rather than assume.
    $installRoot = Join-Path $env:LOCALAPPDATA 'Programs\PinPointStudio'
    $fi = Get-ChildItem $installRoot -Recurse -Filter 'PinPointStudio.exe' -ErrorAction SilentlyContinue |
          Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if ($fi) {
        Write-Host ("Installed: {0}  ({1:yyyy-MM-dd HH:mm}, {2:N0} MB)" -f $fi.FullName, $fi.LastWriteTime, ($fi.Length / 1MB)) -ForegroundColor Green
        if ($fi.LastWriteTime -lt $before) {
            throw "The installed exe predates this build - the installer did not replace it"
        }
    } else {
        throw "Installer reported success but no PinPointStudio.exe under $installRoot"
    }
}
