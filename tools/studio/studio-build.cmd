@echo off
rem studio-build.cmd - the ONE way to build on GOLFSIMPC from the Mac. Lives in the repo so it
rem is never retyped through ssh quoting. ASCII ONLY: a multi-byte character (an em dash on line 2,
rem 11 Oct) makes cmd misread the lines after it under a UTF-8 console, so the cd into the Release
rem tree failed and cmake configured in-source in the repo root. Run it by file:
rem   ssh developer@GOLFSIMPC.local "cmd /c C:\Users\developer\Projects\PinPointStudio\tools\studio\studio-build.cmd [app|tool|both|deploy|migrate|tests] [pull|nopull]"
rem "migrate" builds only pps_migrate_step2 (refactor step 2.8a, the step-2 migrations) in the same tree, in a mode
rem of its own, so a failure there can never fail "tool" or "both". Log: migrate_build.log; exit 6 on failure.
rem "tests" is the Windows test lane (platform refactor B0.2). It REFUSES (exit 10) while PinPointStudio.exe
rem runs (a session may be on) or while any process whose image name contains Foresight, FSX or GSPro runs
rem (Premier shares the cabin's GPU); each match is logged. It checks before the pull and again before each
rem ctest pass. Then, in build\tests-vs18 (Debug, NMake JOM, the tests/ umbrella; never a third tree):
rem cmake . (log tests_cmake.log, the Windows test list in tests_list.log), jom -j16 (tests_build.log; exit 11
rem on a configure or build failure), then ctest -j8 --timeout 900 -LE qml_ui (tests_ctest.log; 900 s
rem is the per-test cap for any test without its own TIMEOUT, so a hang ends as a Timeout and exit 14), then
rem the qml_ui label (tests_qml_ui.log): one ctest test per tst_*.qml, named qml_ui.tst_<file>, each with its
rem own TIMEOUT from src/Gui/CMakeLists.txt, run at -j of the jobs count (platform refactor B0.2b, #251: a slow
rem or stuck file times out alone and every other file is still run and reported). Both passes run, with
rem C:\Qt\6.11.0\msvc2022_64\bin on PATH, QT_QPA_PLATFORM=offscreen and QT_QPA_FONTDIR=C:\Windows\Fonts
rem (or system fonts are invisible). jom -j16 by default (Product Owner, 24 Nov): the 285K has 24 cores, about
rem 22 GB stay free, and a Debug cl.exe was seen at 40-380 MB, so 16 at once stay far inside memory. Lower it
rem with "jobs N" (1 to 24), e.g. studio-build.cmd tests pull jobs 8. Pass 2 runs that many QML files at once
rem (each one offscreen process of about 0.5 GB); pass 1 stays at -j8, since test timing, not build time, is
rem what its job count changes. Excluded, as able to reach a real store here (the list is
rem TEXCL at the ctest call): headless_probe_test (it launches the app, whose HOME/XDG_CONFIG_HOME isolation
rem Windows ignores); model_browser_test and inspector_refresh_test (their XDG_DATA_HOME isolation is
rem Linux-only, #187: on Windows they write and delete under the real %APPDATA%); and, for "#187: reads the
rem real settings store", every suite that reaches ppSettings() through AthleteStore, RuleData or ReportMailer
rem without QSettings::setPath (45 today). They are still built. Real stores are also checked around the run:
rem %APPDATA%\PinPointStudio\PinPointStudio.ini (settings and pairings: copied to %TEMP% and hashed before,
rem re-hashed after), the Recycle Bin's item count, the Credential Manager's entry count (cmdkey) and a
rem listing of %APPDATA%. A changed settings file (it holds the pairing keys) is FIRST copied, as changed,
rem off the share, into a folder icacls limits to this account; the log names its path, never its contents:
rem %LOCALAPPDATA%\PinPointStudio\studio-build\settings-kept\settings-changed-<yyyyMMdd-HHmmss>.ini, never deleted (the
rem change may be the studio's own: a setting or a pairing made while a pass ran); only then is the copy
rem taken before put back, and only if PinPoint Studio is not running. Both copies are kept and both paths
rem logged. If the changed file cannot be kept, nothing is put back (exit 15). Exit 12 when a test fails,
rem 14 when a test hits its timeout, 13 when a real store changed, 15 as above; 15 > 13 > 10 > 14 > 12.
rem Counts and checks: tests_summary.log.
rem FIRST RUN AFTER THE COMMIT THAT ADDS THIS MODE: the cabin's older copy of this script has no "tests"
rem mode and would pull underneath itself, then build and deploy the app. So pull by hand first
rem (git pull --ff-only in the checkout), then run: studio-build.cmd tests nopull
rem Run it only when no session is on and Premier is closed; a full Debug rebuild takes tens of minutes, so
rem start it detached and poll the logs.
rem Defaults: both, pull. Pulls origin/main fast-forward (refuses on a dirty tree) and builds in
rem build\Release-Installer, the studio's one Release tree: the app (cmake . first so the embedded
rem sha is HEAD's), copied over the installed PinPointStudio.exe so the Start menu runs it, and
rem swinglab_run, which runs in place beside that tree's DLLs and models. "deploy" only copies the
rem app over, for when the build finished while PinPoint Studio was open. The studio's other tree
rem is build\tests-vs18 (Debug, the unit-test suites). A full app build is about 2 minutes: poll the
rem log after 60 s, never wait out a watchdog. Logs: C:\PinPointStudio\scratch\studio-build\
rem
rem PATCHED BUILD - uncommitted Mac work, built without committing it:
rem   ssh developer@GOLFSIMPC.local "cmd /c C:\Users\developer\Projects\PinPointStudio\tools\studio\studio-build.cmd [app|tool|both] [pull|nopull] patch C:\PinPointStudio\scratch\<run>\wip.patch"
rem With a patch, mode and pull|nopull must both be given (the arguments are positional); deploy
rem takes no patch. Make the patch on the Mac with tools/studio/make-wip-patch.sh <run-dir>, where
rem <run-dir> is /mnt/swingdata/scratch/<run> (the Mac mount of C:\PinPointStudio, so the file is
rem C:\PinPointStudio\scratch\<run>\wip.patch here). Expected form: git diff --binary --no-renames
rem HEAD, taken through a scratch index in which new untracked files are intent-to-add (git add -N),
rem so new files arrive as "new file" hunks and there are no rename hunks. The patch must not touch
rem this script: cmd reads a running batch file from disk, so patching it mid-run breaks the run.
rem Order: refuse a dirty tree; pull if asked; git apply --check; list the files the patch CREATES
rem (git apply --summary "create mode" lines) into the log folder; git apply; build as normal; then
rem ALWAYS restore, on success and on every build failure: git checkout -- . for tracked files, and
rem delete exactly the listed created files (plus their folder if left empty) - never a blanket
rem git clean, so untracked build trees are safe. git status --short is printed after, and a
rem PATCHED BUILD line names the patch file, its SHA-256 (certutil) and HEAD. RESTORE INCOMPLETE
rem (exit 9) means the tree was not left clean. If the run is killed mid-build the tree stays
rem patched and the next run refuses as dirty: on the cabin, git checkout -- . and delete the
rem files listed in C:\PinPointStudio\scratch\studio-build\patch_created.txt.
rem The app embeds HEAD's sha, so a patched exe reports HEAD: the PATCHED BUILD line ties it to the
rem patch. Exit codes: 3 dirty, 4 pull, 5 app build, 6 tool build, 7 deploy, 8 patch, 9 restore,
rem 10 tests refused (studio busy, or APPDATA or TEMP unset), 11 tests build, 12 a test failed, 13 a real store
rem changed, 14 a test hit its timeout, 15 a changed settings file could not be kept (nothing put back).
rem Ranks in the tests mode: 15 > 13 > 10 > 14 > 12. Any other mode name is refused with exit 8.
rem "tests" takes a patch too: the suites run on the patched tree and the checkout is restored after.
setlocal
set WHAT=%~1
if "%WHAT%"=="" set WHAT=both
set PULL=%~2
if "%PULL%"=="" set PULL=pull
if "%WHAT%"=="deploy" set PULL=nopull
set PFILE=
set TJ=16
rem After mode and pull|nopull come optional pairs: patch <file>, and (tests only) jobs <n>, in either order.
:argloop
if "%~3"=="" goto argsdone
if "%~3"=="patch" goto argpatch
if "%~3"=="jobs" goto argjobs
echo USAGE: studio-build.cmd [app^|tool^|both^|deploy^|migrate^|tests] [pull^|nopull] [patch C:\...\wip.patch] [jobs N]
exit /b 8
:argpatch
if "%~4"=="" ( echo PATCH: no patch file given & exit /b 8 )
if "%WHAT%"=="deploy" ( echo PATCH: deploy builds nothing, so it takes no patch & exit /b 8 )
set "PFILE=%~f4"
shift /3
shift /3
goto argloop
:argjobs
if not "%WHAT%"=="tests" ( echo JOBS: only the tests mode takes a job count & exit /b 8 )
if "%~4"=="" ( echo JOBS: no job count given & exit /b 8 )
set TJN=
set /a TJN=%~4 2>nul
if not "%TJN%"=="%~4" ( echo JOBS: %~4 is not a whole number & exit /b 8 )
if %TJN% LSS 1 ( echo JOBS: %~4 is out of range 1 to 24 & exit /b 8 )
if %TJN% GTR 24 ( echo JOBS: %~4 is out of range 1 to 24 & exit /b 8 )
set TJ=%TJN%
shift /3
shift /3
goto argloop
:argsdone
for %%m in (app tool both deploy migrate tests) do if "%WHAT%"=="%%m" goto modeok
echo USAGE: studio-build.cmd [app^|tool^|both^|deploy^|migrate^|tests] - "%WHAT%" is not a mode
exit /b 8
:modeok
set EXITRC=0
set PATCHED=
set REPO=C:\Users\developer\Projects\PinPointStudio
set LOGS=C:\PinPointStudio\scratch\studio-build
set BIN=C:\Users\developer\AppData\Local\Programs\PinPointStudio\bin
set RI=%REPO%\build\Release-Installer
set NEWLIST=%LOGS%\patch_created.txt
if not exist "%LOGS%" mkdir "%LOGS%"
set TB=%REPO%\build\tests-vs18
set TSUM=%LOGS%\tests_summary.log
if not "%WHAT%"=="tests" goto busydone
if exist "%TSUM%" del /f /q "%TSUM%"
call :say tests: start %DATE% %TIME%
if not defined APPDATA ( call :say tests: REFUSED - APPDATA is not set, so the real-store check cannot see & exit /b 10 )
if not defined TEMP ( call :say tests: REFUSED - TEMP is not set, so the settings cannot be copied first & exit /b 10 )
call :busy
if defined BUSY ( call :say tests: REFUSED - the studio is busy, nothing pulled or built & exit /b 10 )
:busydone
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
if not defined PFILE goto patchdone
if not exist "%PFILE%" ( echo PATCH: file not found %PFILE% & exit /b 8 )
set PSHA=
for /f "skip=1 delims=" %%h in ('certutil -hashfile "%PFILE%" SHA256') do if not defined PSHA set "PSHA=%%h"
if not defined PSHA ( echo PATCH: certutil could not hash %PFILE% & exit /b 8 )
set "PSHA=%PSHA: =%"
for /f %%i in ('git rev-parse --short HEAD') do set HEADSHA=%%i
echo PATCH: %PFILE% sha256 %PSHA%
git apply --numstat "%PFILE%" | findstr /i /c:"tools/studio/studio-build.cmd" >nul
if not errorlevel 1 ( echo PATCH: refusing - the patch touches studio-build.cmd itself & exit /b 8 )
git apply --check "%PFILE%"
if errorlevel 1 ( echo PATCH: git apply --check FAILED - nothing applied & exit /b 8 )
if exist "%NEWLIST%" del /f /q "%NEWLIST%"
type nul > "%NEWLIST%"
for /f "tokens=1,2,3,*" %%a in ('git apply --summary "%PFILE%"') do if "%%a"=="create" if "%%b"=="mode" echo %%d>> "%NEWLIST%"
for /f %%i in ('type "%NEWLIST%" ^| find /c /v ""') do set NNEW=%%i
echo PATCH: %NNEW% new file(s) listed in %NEWLIST%
rem In a .cmd file "set" resets ERRORLEVEL, so take git apply's code before marking the tree patched.
git apply "%PFILE%"
set ARC=%ERRORLEVEL%
set PATCHED=1
if not "%ARC%"=="0" ( echo PATCH: git apply FAILED & set "EXITRC=8" & goto fail )
echo PATCH: applied over %HEADSHA%
git status --short
:patchdone
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
set PATH=%PATH%;C:\Qt\Tools\QtCreator\bin\jom;C:\Qt\Tools\CMake_64\bin
if "%WHAT%"=="deploy" goto deploy
if "%WHAT%"=="tool" goto tool
if "%WHAT%"=="migrate" goto migrate
if "%WHAT%"=="tests" goto tests
echo app: cmake %TIME%
cd /d "%RI%"
C:\Qt\Tools\CMake_64\bin\cmake.exe . > "%LOGS%\app_cmake.log" 2>&1
echo app: jom %TIME%
jom -j8 PinPointStudio > "%LOGS%\app_build.log" 2>&1
set RC=%ERRORLEVEL%
echo app: build exit %RC% %TIME%
if not "%RC%"=="0" ( type "%LOGS%\app_build.log" | findstr /i "error" & set "EXITRC=5" & goto fail )
:deploy
tasklist /fi "imagename eq PinPointStudio.exe" | find /i "PinPointStudio.exe" >nul
if not errorlevel 1 (
  echo app: NOT deployed - PinPoint Studio is running. Quit it, then run studio-build.cmd deploy
  if "%WHAT%"=="deploy" exit /b 7
  goto afterdeploy
)
copy /y "%RI%\PinPointStudio.exe" "%BIN%\PinPointStudio.exe" >nul
if errorlevel 1 ( echo app: COPY FAILED & set "EXITRC=7" & goto fail )
echo app: deployed %BIN%\PinPointStudio.exe
:afterdeploy
if "%WHAT%"=="app" goto done
if "%WHAT%"=="deploy" goto done
:tool
echo tool: jom %TIME%
cd /d "%RI%"
jom -j8 swinglab_run > "%LOGS%\tool_build.log" 2>&1
set RC=%ERRORLEVEL%
echo tool: build exit %RC% %TIME%
if not "%RC%"=="0" ( type "%LOGS%\tool_build.log" | findstr /i "error" & set "EXITRC=6" & goto fail )
echo tool: %RI%\swinglab_run.exe
goto done
:migrate
echo migrate: jom pps_migrate_step2 %TIME%
cd /d "%RI%"
jom -j8 pps_migrate_step2 > "%LOGS%\migrate_build.log" 2>&1
set RC=%ERRORLEVEL%
echo migrate: build exit %RC% %TIME%
if not "%RC%"=="0" ( type "%LOGS%\migrate_build.log" | findstr /i "error" & set "EXITRC=6" & goto fail )
echo migrate: %RI%\pps_migrate_step2.exe
goto done
:tests
rem The Windows test lane. ctest's -E and -R patterns stay on lines of their own, outside ( ) blocks.
set CRC1=0
set CRC2=0
set STORECHANGED=
set KEEPFAIL=
set THUNG=
if not exist "%TB%\CMakeCache.txt" ( call :say tests: no configured tree at %TB% & set "EXITRC=11" & goto fail )
call :say tests: cmake %TIME%
cd /d "%TB%"
C:\Qt\Tools\CMake_64\bin\cmake.exe . > "%LOGS%\tests_cmake.log" 2>&1
set RC=%ERRORLEVEL%
if not "%RC%"=="0" ( call :say tests: cmake FAILED, exit %RC%, see tests_cmake.log & type "%LOGS%\tests_cmake.log" | findstr /i "error" & set "EXITRC=11" & goto fail )
C:\Qt\Tools\CMake_64\bin\ctest.exe --test-dir "%TB%" -N > "%LOGS%\tests_list.log" 2>&1
set TLIST=unknown
for /f "tokens=3" %%n in ('findstr /c:"Total Tests:" "%LOGS%\tests_list.log"') do set TLIST=%%n
rem The exclusions. The qml_ui label is pass 2's (-LE qml_ui here). Reaching a real store: headless_probe_test
rem launches the app; model_browser_test and inspector_refresh_test write under the real %APPDATA% (#187).
set "TEXCL=headless_probe_test|model_browser_test|inspector_refresh_test"
rem #187: reads the real settings store. These link athlete_store.cpp (the real [athletes] group), RuleData
rem (its load reads ui/diagnostics* through ppSettings), with no QSettings::setPath. They
rem come back when #187's isolation helper lands.
set "TEXCL=%TEXCL%|app_state_test|athlete_dump_ids_test|diag_footer_test|diag_parallel_live_test|diag_stage_panel_test"
set "TEXCL=%TEXCL%|golfer_input_test|regrade_ledger_new_needs_ini_test|review_views_shot_test|session_diagnostics_model_test"
set "TEXCL=%TEXCL%|session_focus_test|session_series_test|shot_number_test|work_ons_controller_test|charts_consistency_presenter_test"
set "TEXCL=%TEXCL%|charts_delivery_presenter_test|charts_posture_presenter_test|charts_relations_presenter_test"
set "TEXCL=%TEXCL%|charts_traces_presenter_test|charts_wrist_presenter_test|club_baseline_provider_test|consistency_env_test"
set "TEXCL=%TEXCL%|core_pack_test|delivery_picture_test|diagnostics_catalogue_integrity_test|diagnostics_health_test"
set "TEXCL=%TEXCL%|fire_findings_test|folder_series_test|live_measure_source_test|lm_corridor_test|manifest_migration_test"
set "TEXCL=%TEXCL%|measure_dump_test|outcome_summary_test|reduction_rule_golden_test|reference_sets_test|report_lm_test"
set "TEXCL=%TEXCL%|report_model_test|rule_version_golden_test|seed_conversion_test|session_consistency_test|session_delivery_test"
set "TEXCL=%TEXCL%|session_diagnostics_test|session_posture_test|swing_themes_golden_test"
set "TEXCL=%TEXCL%|variability_clubs_test|variability_env_test"
C:\Qt\Tools\CMake_64\bin\ctest.exe --test-dir "%TB%" -N -LE qml_ui -E "^(%TEXCL%)$" > "%LOGS%\tests_list_run.log" 2>&1
set TRUN=unknown
for /f "tokens=3" %%n in ('findstr /c:"Total Tests:" "%LOGS%\tests_list_run.log"') do set TRUN=%%n
C:\Qt\Tools\CMake_64\bin\ctest.exe --test-dir "%TB%" -N -L qml_ui > "%LOGS%\tests_list_qml.log" 2>&1
set QRUN=unknown
for /f "tokens=3" %%n in ('findstr /c:"Total Tests:" "%LOGS%\tests_list_qml.log"') do set QRUN=%%n
call :say tests: pass 1 will run %TRUN% of them, then pass 2 the %QRUN% QML files of the qml_ui label at -j%TJ%
call :say tests: %TLIST% tests registered; jom -j%TJ% %TIME%
jom -j%TJ% > "%LOGS%\tests_build.log" 2>&1
set RC=%ERRORLEVEL%
call :say tests: build exit %RC% %TIME%
if not "%RC%"=="0" ( type "%LOGS%\tests_build.log" | findstr /i /c:": error" /c:": fatal error" & set "EXITRC=11" & goto fail )
set PATH=C:\Qt\6.11.0\msvc2022_64\bin;%PATH%
set QT_QPA_PLATFORM=offscreen
set QT_QPA_FONTDIR=C:\Windows\Fonts
call :busy
if defined BUSY ( call :say tests: REFUSED before ctest - the studio became busy & set "EXITRC=10" & goto fail )
call :stores before
call :say tests: ctest pass 1, all but the qml_ui label and the excluded suites %TIME%
C:\Qt\Tools\CMake_64\bin\ctest.exe --test-dir "%TB%" -j8 --timeout 900 --output-on-failure -LE qml_ui -E "^(%TEXCL%)$" > "%LOGS%\tests_ctest.log" 2>&1
set CRC1=%ERRORLEVEL%
call :say tests: ctest pass 1 exit %CRC1% %TIME%
call :tally "%LOGS%\tests_ctest.log"
call :busy
if defined BUSY ( call :say tests: the qml_ui label NOT run - the studio became busy & set "EXITRC=10" & goto storesafter )
call :say tests: ctest pass 2, the qml_ui label at -j%TJ% %TIME%
C:\Qt\Tools\CMake_64\bin\ctest.exe --test-dir "%TB%" -j%TJ% --timeout 900 --output-on-failure -L qml_ui --no-tests=error > "%LOGS%\tests_qml_ui.log" 2>&1
set CRC2=%ERRORLEVEL%
call :say tests: ctest pass 2 exit %CRC2% %TIME%
call :tally "%LOGS%\tests_qml_ui.log"
:storesafter
call :stores after
if not "%CRC1%"=="0" if "%EXITRC%"=="0" set EXITRC=12
if not "%CRC2%"=="0" if "%EXITRC%"=="0" set EXITRC=12
if defined THUNG if not "%EXITRC%"=="10" set EXITRC=14
if defined STORECHANGED set EXITRC=13
if defined KEEPFAIL set EXITRC=15
call :say tests: end %DATE% %TIME% - exit %EXITRC%
goto done
:done
cd /d "%REPO%"
if defined PATCHED call :restore
echo == status after
git status --short
if defined PFILE echo PATCHED BUILD %PFILE% sha256 %PSHA% over %HEADSHA% - exit %EXITRC%
echo == end
exit /b %EXITRC%

:fail
rem Every failure after the patch is applied lands here, so the checkout is always restored.
cd /d "%REPO%"
if not defined PATCHED exit /b %EXITRC%
call :restore
echo == status after
git status --short
echo PATCHED BUILD %PFILE% sha256 %PSHA% over %HEADSHA% - exit %EXITRC%
echo == end
exit /b %EXITRC%

:restore
rem Put the checkout back to HEAD: tracked files from the index, then exactly the files the patch
rem created (listed before applying). Never a blanket clean.
cd /d "%REPO%"
echo PATCH: restoring checkout
git checkout -- .
for /f "usebackq delims=" %%f in ("%NEWLIST%") do call :delnew "%%f"
set PATCHED=
set LEFT=
for /f %%i in ('git status --short ^| find /c /v ""') do set LEFT=%%i
if not "%LEFT%"=="0" (
  echo RESTORE INCOMPLETE - %LEFT% path^(s^) still differ from HEAD
  if "%EXITRC%"=="0" set EXITRC=9
)
goto :eof

:delnew
set "NF=%~1"
set "NF=%NF:/=\%"
if exist "%REPO%\%NF%" del /f /q "%REPO%\%NF%"
for %%d in ("%REPO%\%NF%") do set "ND=%%~dpd"
rd "%ND:~0,-1%" 2>nul
goto :eof

:say
rem One line to the console and to tests_summary.log. Redirect first, so a trailing digit is never a handle.
echo %*
>> "%TSUM%" echo %*
goto :eof

:busy
rem Sets BUSY when PinPoint Studio, or anything named Foresight, FSX or GSPro, runs; logs each match.
rem Fails closed: a tasklist that lists nothing counts as busy.
set BUSY=
set NPROC=0
for /f %%c in ('tasklist /fo csv /nh ^| find /c ","') do set NPROC=%%c
if "%NPROC%"=="0" ( call :say tests: BUSY - tasklist listed no processes, so the guard cannot see & set "BUSY=1" )
tasklist /fi "imagename eq PinPointStudio.exe" | find /i "PinPointStudio.exe" >nul
if not errorlevel 1 ( call :say tests: BUSY - PinPointStudio.exe is running & set "BUSY=1" )
for /f "tokens=1 delims=," %%p in ('tasklist /fo csv /nh ^| findstr /i "foresight fsx gspro"') do ( call :say tests: BUSY - %%~p is running & set "BUSY=1" )
if not defined BUSY call :say tests: studio idle - %NPROC% processes, none PinPoint Studio, Foresight, FSX or GSPro
goto :eof

:tally
rem The ctest summary line and every test that did not pass.
findstr /c:"tests passed" /c:"(Failed)" /c:"(Timeout)" /c:"(SEGFAULT)" /c:"(Subprocess aborted)" /c:"(Exception)" /c:"(Not Run)" /c:"(Disabled)" /c:"(ILLEGAL)" /c:"(OTHER_FAULT)" /c:"(BAD_COMMAND)" /c:"(Child aborted)" /c:"(Interrupt)" /c:"(Numerical)" /c:"(Exit code" %1 > "%LOGS%\tests_tally.tmp"
type "%LOGS%\tests_tally.tmp"
type "%LOGS%\tests_tally.tmp" >> "%TSUM%"
findstr /c:"(Timeout)" "%LOGS%\tests_tally.tmp" >nul
if not errorlevel 1 ( call :say tests: TIMEOUT - a test hit its cap, so it may hang & set "THUNG=1" )
del /f /q "%LOGS%\tests_tally.tmp"
goto :eof

:stores
rem %1 is before or after. The studio's real stores, which no test may change: the settings file (which
rem also holds the phone pairings), the Recycle Bin, the Credential Manager, and the top of %APPDATA%.
set "SINI=%APPDATA%\PinPointStudio\PinPointStudio.ini"
set "SSNAP=%TEMP%\pps-tests-settings.ini"
set SH=
if exist "%SINI%" for /f "skip=1 delims=" %%h in ('certutil -hashfile "%SINI%" SHA256') do if not defined SH set "SH=%%h"
if not defined SH set SH=none
set "SH=%SH: =%"
set RB=unknown
for /f %%c in ('powershell -NoProfile -Command "(New-Object -ComObject Shell.Application).Namespace(10).Items().Count"') do set RB=%%c
set CK=0
for /f %%c in ('cmdkey /list ^| find /c "Target:"') do set CK=%%c
dir /b "%APPDATA%" > "%LOGS%\tests_appdata_%1.txt" 2>&1
call :say tests: stores %1 - settings sha256 %SH%, Recycle Bin %RB% items, Credential Manager %CK% entries
if "%1"=="after" goto storescmp
set SH0=%SH%
set RB0=%RB%
set CK0=%CK%
if exist "%SINI%" copy /y "%SINI%" "%SSNAP%" >nul
goto :eof
:storescmp
if not "%SH%"=="%SH0%" ( call :say tests: REAL STORE CHANGED - the settings file %SINI% & set STORECHANGED=1 & call :putback )
if not "%RB%"=="%RB0%" ( call :say tests: REAL STORE CHANGED - the Recycle Bin went from %RB0% to %RB% items & set STORECHANGED=1 )
if not "%CK%"=="%CK0%" ( call :say tests: REAL STORE CHANGED - the Credential Manager went from %CK0% to %CK% entries & set STORECHANGED=1 )
fc "%LOGS%\tests_appdata_before.txt" "%LOGS%\tests_appdata_after.txt" >nul
if errorlevel 1 ( call :say tests: NOTE - the top of %APPDATA% changed, fc below & fc "%LOGS%\tests_appdata_before.txt" "%LOGS%\tests_appdata_after.txt" >> "%TSUM%" )
if not defined STORECHANGED if exist "%SSNAP%" del /f /q "%SSNAP%"
goto :eof

:putback
rem A test, or the studio itself while a pass ran, changed the real settings. FIRST keep the changed file under
rem a name of its own that nothing deletes, so no setting or pairing is lost. Only then put the copy taken
rem before back, unless PinPoint Studio now runs. If the changed file cannot be kept, put nothing back.
rem It holds the phone pairing keys, so it goes in this account's own profile, not on the share the Mac
rem mounts, in a folder only this account can open. The run log names the path, never the contents.
set SKEPT=
if not defined LOCALAPPDATA ( call :say tests: settings NOT put back - LOCALAPPDATA is not set, so the changed file has nowhere private to go. %SINI% is left as the run left it & set "KEEPFAIL=1" & goto :eof )
set "KDIR=%LOCALAPPDATA%\PinPointStudio\studio-build\settings-kept"
if not exist "%KDIR%" mkdir "%KDIR%" 2>nul
icacls "%KDIR%" /inheritance:r /grant:r "%USERNAME%:(OI)(CI)F" >nul 2>&1
if errorlevel 1 call :say tests: NOTE - icacls could not limit %KDIR% to %USERNAME%; it stays private to the profile
for /f %%t in ('powershell -NoProfile -Command "Get-Date -Format yyyyMMdd-HHmmss"') do set "SKEPT=%KDIR%\settings-changed-%%t.ini"
if not defined SKEPT set "SKEPT=%KDIR%\settings-changed-%RANDOM%%RANDOM%.ini"
if exist "%SKEPT%" set "SKEPT=%SKEPT%.%RANDOM%.ini"
copy /y "%SINI%" "%SKEPT%" >nul
set SHK=
if exist "%SKEPT%" for /f "skip=1 delims=" %%h in ('certutil -hashfile "%SKEPT%" SHA256') do if not defined SHK set "SHK=%%h"
if not defined SHK set SHK=none
set "SHK=%SHK: =%"
if not "%SHK%"=="%SH%" ( call :say tests: settings NOT put back - the changed file could not be kept at %SKEPT%. %SINI% is left as the run left it & set "KEEPFAIL=1" & goto :eof )
call :say tests: the changed settings are kept at %SKEPT%, never deleted
tasklist /fi "imagename eq PinPointStudio.exe" | find /i "PinPointStudio.exe" >nul
if not errorlevel 1 ( call :say tests: settings NOT put back - PinPoint Studio is running. The copy taken before is %SSNAP% & goto :eof )
if not exist "%SSNAP%" ( call :say tests: there was no settings file before. The one made during the run is left at %SINI% & goto :eof )
copy /y "%SSNAP%" "%SINI%" >nul
set SH2=
for /f "skip=1 delims=" %%h in ('certutil -hashfile "%SINI%" SHA256') do if not defined SH2 set "SH2=%%h"
if not defined SH2 set SH2=none
set "SH2=%SH2: =%"
if "%SH2%"=="%SH0%" ( call :say tests: settings put back from %SSNAP%, hash matches the one taken before. The changed file is %SKEPT% ) else ( call :say tests: settings put back FAILED. The copy taken before is kept at %SSNAP%, the changed file at %SKEPT% )
goto :eof )
if not exist "%SSNAP%" ( call :say tests: there was no settings file before. The one a test made is left at %SINI% & goto :eof )
copy /y "%SSNAP%" "%SINI%" >nul
set SH2=
for /f "skip=1 delims=" %%h in ('certutil -hashfile "%SINI%" SHA256') do if not defined SH2 set "SH2=%%h"
if not defined SH2 set SH2=none
set "SH2=%SH2: =%"
if "%SH2%"=="%SH0%" ( call :say tests: settings put back from %SSNAP%, hash matches the one taken before ) else ( call :say tests: settings put back FAILED. The copy taken before is kept at %SSNAP% )
goto :eof
