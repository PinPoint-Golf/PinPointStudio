<#
.SYNOPSIS
    Build a Release PinPoint Studio and produce the Windows installer (CPack + Inno Setup).

.DESCRIPTION
    1. Configures a Release build dir (NMake JOM generator, MSVC env from vcvars).
    2. Builds the PinPointStudio target.
    3. Runs CPack with the Inno Setup generator to produce
       PinPointStudioSetup-<version>.exe under the build dir.

    A fresh Release build dir triggers the usual FetchContent downloads
    (whisper ~148 MB, ViTPose ~330 MB, ONNX Runtime, etc.) and takes several minutes.

.PREREQUISITES
    - Visual Studio (vcvars64.bat), Qt 6.11 MSVC, CMake + jom (paths below).
    - Inno Setup 6 (ISCC.exe). Auto-detected in the usual install locations.
    - Build a RELEASE config: the debug CRT (vcruntime140d.dll, ...) is not
      redistributable, so a Debug installer only runs where Visual Studio is present.

.PARAMETER Components
    'both' (default) packages core + cuda in one installer. 'core' produces a small
    core-only installer (the eventual auto-update payload); 'cuda' a standalone GPU
    runtime package. (The two-artifact split is groundwork for in-app auto-update.)

.EXAMPLE
    pwsh -File packaging\build_installer.ps1
    pwsh -File packaging\build_installer.ps1 -Components core
#>
[CmdletBinding()]
param(
    [string]$BuildDir = 'build\Release-Installer',
    [string]$QtPrefix = 'C:\Qt\6.11.0\msvc2022_64',
    [ValidateSet('both','core','cuda')]
    [string]$Components = 'both'
)
# NOTE: deliberately 'Continue', not 'Stop'. vcvars64.bat and cmake/cpack write
# benign messages to stderr (e.g. the cosmetic "'vswhere.exe' is not recognized");
# under 'Stop', PowerShell 5.1 wraps native stderr as a terminating NativeCommandError
# and aborts before anything builds. Real failures are caught via $LASTEXITCODE below.
$ErrorActionPreference = 'Continue'

$repo   = Split-Path -Parent $PSScriptRoot
$cmake  = 'C:\Qt\Tools\CMake_64\bin\cmake.exe'
$cpack  = 'C:\Qt\Tools\CMake_64\bin\cpack.exe'
$vcvars = 'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat'
$jom    = 'C:\Qt\Tools\QtCreator\bin\jom'

# Locate the Inno Setup compiler (CPack's INNOSETUP generator shells out to ISCC).
$iscc = (Get-Command ISCC -ErrorAction SilentlyContinue).Source
if (-not $iscc) {
    foreach ($p in @(
        "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe",
        'C:\Program Files (x86)\Inno Setup 6\ISCC.exe',
        'C:\Program Files\Inno Setup 6\ISCC.exe')) {
        if (Test-Path $p) { $iscc = $p; break }
    }
}
if (-not $iscc) { throw "Inno Setup 6 (ISCC.exe) not found. Install it from https://jrsoftware.org/isdl.php" }

# cpack's preinstall step shells out to the generator's build tool (jom) and the
# Inno generator runs ISCC — so configure, build AND package all run inside the same
# vcvars + jom environment. cpack runs from the build dir so it finds CPackConfig.cmake
# and writes _CPack_Packages / the installer there.
# Distinct output name per variant so successive runs don't overwrite each other:
#   both -> PinPointStudioSetup-<ver>.exe
#   core -> PinPointStudioSetup-<ver>-core.exe   (no CUDA; smaller)
#   cuda -> PinPointStudioSetup-<ver>-cuda.exe   (GPU runtime only)
$version = '0.0.0'
$m = Select-String -Path (Join-Path $repo 'CMakeLists.txt') -Pattern 'project\(PinPointStudio VERSION ([0-9.]+)' | Select-Object -First 1
if ($m) { $version = $m.Matches[0].Groups[1].Value }
$pkgName = "PinPointStudioSetup-$version"
$cpackComponentArg = ''
switch ($Components) {
    'core' { $cpackComponentArg = '-D CPACK_COMPONENTS_ALL=core'; $pkgName += '-core' }
    'cuda' { $cpackComponentArg = '-D CPACK_COMPONENTS_ALL=cuda'; $pkgName += '-cuda' }
}
$bat = @"
@echo on
call "$vcvars"
set PATH=%PATH%;$jom
cd /d "$repo"
"$cmake" -S . -B "$BuildDir" -G "NMake Makefiles JOM" -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$QtPrefix" || exit /b 1
"$cmake" --build "$BuildDir" --target PinPointStudio || exit /b 1
cd /d "$repo\$BuildDir"
"$cpack" -G INNOSETUP -D CPACK_INNOSETUP_EXECUTABLE="$iscc" -D CPACK_PACKAGE_FILE_NAME="$pkgName" $cpackComponentArg || exit /b 1
"@
$batPath = Join-Path $env:TEMP 'pp_build_installer.bat'
$bat | Out-File -FilePath $batPath -Encoding ascii
cmd /c "`"$batPath`""
if ($LASTEXITCODE -ne 0) { throw "Build/package failed (exit $LASTEXITCODE)" }

Write-Host ""
Write-Host "Installer written under $repo\$BuildDir" -ForegroundColor Green
Get-ChildItem (Join-Path $repo $BuildDir) -Filter '*.exe' | Where-Object { $_.Name -like 'PinPointStudioSetup*' } | ForEach-Object { Write-Host "  $($_.FullName)" }
