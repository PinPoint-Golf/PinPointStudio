@echo off
rem studio-diag.cmd - builds the two diagnostics tools on GOLFSIMPC for the posture validation
rem (docs/design/posture_metrics_design.md section 9): measure_dump (per-shot measure readings as a CSV,
rem read-only on the library) and regrade_ledger (re-reduces diagnostics.json under the checkout's
rem pack). ASCII ONLY, like studio-build.cmd: a multi-byte character makes cmd misread the lines after
rem it under a UTF-8 console. Run it by file:
rem   ssh developer@GOLFSIMPC.local "cmd /c C:\Users\developer\Projects\PinPointStudio\tools\studio\studio-diag.cmd [pull|nopull]"
rem Default: pull. Pulls origin/main fast-forward (refuses on a dirty tree), runs cmake . in
rem build\tests-vs18 (the studio's Debug unit-test tree, NMake JOM), builds the two targets and prints
rem their paths. Runs nothing else. Log: C:\PinPointStudio\scratch\studio-build\diag_build.log
setlocal
set PULL=%~1
if "%PULL%"=="" set PULL=pull
set REPO=C:\Users\developer\Projects\PinPointStudio
set LOGS=C:\PinPointStudio\scratch\studio-build
set TV=%REPO%\build\tests-vs18
if not exist "%LOGS%" mkdir "%LOGS%"
cd /d "%REPO%"
for /f %%i in ('git status --short ^| find /c /v ""') do set DIRTY=%%i
if not "%DIRTY%"=="0" (
  echo TREE NOT CLEAN - refusing
  git status --short
  exit /b 3
)
if "%PULL%"=="pull" (
  git pull --ff-only
  if errorlevel 1 ( echo PULL FAILED & exit /b 4 )
)
git log -1 --oneline
if not exist "%TV%\CMakeCache.txt" ( echo NO TREE: %TV% is not configured & exit /b 8 )
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
set PATH=%PATH%;C:\Qt\Tools\QtCreator\bin\jom;C:\Qt\Tools\CMake_64\bin
echo diag: cmake %TIME%
cd /d "%TV%"
C:\Qt\Tools\CMake_64\bin\cmake.exe . > "%LOGS%\diag_build.log" 2>&1
if errorlevel 1 ( echo diag: CMAKE FAILED - see %LOGS%\diag_build.log & exit /b 5 )
echo diag: jom %TIME%
jom -j8 measure_dump regrade_ledger >> "%LOGS%\diag_build.log" 2>&1
set RC=%ERRORLEVEL%
echo diag: build exit %RC% %TIME%
if not "%RC%"=="0" ( type "%LOGS%\diag_build.log" | findstr /i "error" & exit /b 6 )
for /f "delims=" %%f in ('dir /s /b "%TV%\measure_dump.exe" 2^>nul') do echo diag: %%f
for /f "delims=" %%f in ('dir /s /b "%TV%\regrade_ledger.exe" 2^>nul') do echo diag: %%f
cd /d "%REPO%"
echo == status after
git status --short
echo == end
