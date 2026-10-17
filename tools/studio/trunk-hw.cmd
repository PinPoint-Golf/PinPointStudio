@echo off
rem trunk-hw.cmd - the trunk-IMU hardware session's studio passes (design
rem docs/design/trunk_imu_sequence_grf_design.md section 16 step 2; protocol
rem docs/research/trunk_imu/hardware_worn_session.md). ASCII only: cmd misreads lines
rem after a multi-byte character under a UTF-8 console.
rem
rem   ssh developer@GOLFSIMPC.local "cmd /c C:\Users\developer\Projects\PinPointStudio\tools\studio\trunk-hw.cmd <session-folder> <stand-tall swing or ->"
rem
rem The stand-tall swing is the FALLBACK route (no ceremony yet): its stillest 2 s calibrate the hip
rem and chest bindings of every shot. "-" = the ceremony ran, so each shot keeps its saved calibration.
rem
rem Reads C:\PinPointStudio\Mark-Liversedge\<session> and writes ONLY to
rem C:\PinPointStudio\scratch\trunk-hw\<session>\{dense,bound,unbound,refuse}\<swing>.
rem Needs swinglab_run built from a tree that has the trunk flags (studio-build.cmd tool).
setlocal enabledelayedexpansion
set SESSION=%~1
set STAND=%~2
if "%SESSION%"=="" ( echo usage: trunk-hw.cmd session-folder stand-tall-swing & exit /b 2 )
if "%STAND%"=="" set STAND=-
set REPO=C:\Users\developer\Projects\PinPointStudio
set RI=%REPO%\build\Release-Installer
set SRCDIR=C:\PinPointStudio\Mark-Liversedge\%SESSION%
set OUT=C:\PinPointStudio\scratch\trunk-hw\%SESSION%
set PATH=C:\Qt\6.11.0\msvc2022_64\bin;%PATH%
set CALIB=
if not "%STAND%"=="-" (
  if not exist "%SRCDIR%\%STAND%" ( echo no stand-tall swing %SRCDIR%\%STAND% & exit /b 3 )
  set CALIB=--trunk-calib-from "%SRCDIR%\%STAND%"
)
if not exist "%RI%\swinglab_run.exe" ( echo no swinglab_run in %RI% & exit /b 4 )
for %%P in (dense bound unbound refuse) do if not exist "%OUT%\%%P" mkdir "%OUT%\%%P"
echo == trunk-hw %SESSION% stand=%STAND% %DATE% %TIME% > "%OUT%\trunk-hw.log"
for /d %%W in ("%SRCDIR%\swing_*") do (
  echo == %%~nxW >> "%OUT%\trunk-hw.log"
  "%RI%\swinglab_run.exe" "%%W" --out "%OUT%\dense\%%~nxW" --full-window >> "%OUT%\trunk-hw.log" 2>&1
  "%RI%\swinglab_run.exe" "%%W" --out "%OUT%\bound\%%~nxW" !CALIB! >> "%OUT%\trunk-hw.log" 2>&1
  "%RI%\swinglab_run.exe" "%%W" --out "%OUT%\unbound\%%~nxW" --ignore-role pelvis,thorax >> "%OUT%\trunk-hw.log" 2>&1
  "%RI%\swinglab_run.exe" "%%W" --out "%OUT%\refuse\%%~nxW" --refuse-orientation >> "%OUT%\trunk-hw.log" 2>&1
)
echo == end %DATE% %TIME% >> "%OUT%\trunk-hw.log"
echo == end
