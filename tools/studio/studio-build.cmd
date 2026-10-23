@echo off
rem studio-build.cmd - the ONE way to build on GOLFSIMPC from the Mac. Lives in the repo so it
rem is never retyped through ssh quoting. ASCII ONLY: a multi-byte character (an em dash on line 2,
rem 11 Oct) makes cmd misread the lines after it under a UTF-8 console, so the cd into the Release
rem tree failed and cmake configured in-source in the repo root. Run it by file:
rem   ssh developer@GOLFSIMPC.local "cmd /c C:\Users\developer\Projects\PinPointStudio\tools\studio\studio-build.cmd [app|tool|both|deploy] [pull|nopull]"
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
rem patch. Exit codes: 3 dirty, 4 pull, 5 app build, 6 tool build, 7 deploy, 8 patch, 9 restore.
setlocal
set WHAT=%~1
if "%WHAT%"=="" set WHAT=both
set PULL=%~2
if "%PULL%"=="" set PULL=pull
if "%WHAT%"=="deploy" set PULL=nopull
set PFILE=
if "%~3"=="" goto argsdone
if not "%~3"=="patch" (
  echo USAGE: studio-build.cmd [app^|tool^|both^|deploy] [pull^|nopull] [patch C:\...\wip.patch]
  exit /b 8
)
if "%~4"=="" ( echo PATCH: no patch file given & exit /b 8 )
if "%WHAT%"=="deploy" ( echo PATCH: deploy builds nothing, so it takes no patch & exit /b 8 )
set "PFILE=%~f4"
:argsdone
set EXITRC=0
set PATCHED=
set REPO=C:\Users\developer\Projects\PinPointStudio
set LOGS=C:\PinPointStudio\scratch\studio-build
set BIN=C:\Users\developer\AppData\Local\Programs\PinPointStudio\bin
set RI=%REPO%\build\Release-Installer
set NEWLIST=%LOGS%\patch_created.txt
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
