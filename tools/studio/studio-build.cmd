@echo off
rem studio-build.cmd — the ONE way to build on GOLFSIMPC from the Mac. Lives in the repo so it
rem is never retyped through ssh quoting. Run it by file:
rem   ssh developer@GOLFSIMPC.local "cmd /c C:\Users\developer\Projects\PinPointStudio\tools\studio\studio-build.cmd [app|tool|both|deploy] [pull|nopull]"
rem Defaults: both, pull. Pulls origin/main fast-forward (refuses on a dirty tree) and builds in
rem build\Release-Installer, the studio's one Release tree: the app (cmake . first so the embedded
rem sha is HEAD's), copied over the installed PinPointStudio.exe so the Start menu runs it, and
rem swinglab_run, which runs in place beside that tree's DLLs and models. "deploy" only copies the
rem app over, for when the build finished while PinPoint Studio was open. The studio's other tree
rem is build\tests-vs18 (Debug, the unit-test suites). A full app build is about 2 minutes: poll the
rem log after 60 s, never wait out a watchdog. Logs: C:\PinPointStudio\scratch\studio-build\
setlocal
set WHAT=%~1
if "%WHAT%"=="" set WHAT=both
set PULL=%~2
if "%PULL%"=="" set PULL=pull
if "%WHAT%"=="deploy" set PULL=nopull
set REPO=C:\Users\developer\Projects\PinPointStudio
set LOGS=C:\PinPointStudio\scratch\studio-build
set BIN=C:\Users\developer\AppData\Local\Programs\PinPointStudio\bin
set RI=%REPO%\build\Release-Installer
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
if not "%RC%"=="0" ( type "%LOGS%\app_build.log" | findstr /i "error" & exit /b 5 )
:deploy
tasklist /fi "imagename eq PinPointStudio.exe" | find /i "PinPointStudio.exe" >nul
if not errorlevel 1 (
  echo app: NOT deployed - PinPoint Studio is running. Quit it, then run studio-build.cmd deploy
  if "%WHAT%"=="deploy" exit /b 7
  goto afterdeploy
)
copy /y "%RI%\PinPointStudio.exe" "%BIN%\PinPointStudio.exe" >nul
if errorlevel 1 ( echo app: COPY FAILED & exit /b 7 )
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
if not "%RC%"=="0" ( type "%LOGS%\tool_build.log" | findstr /i "error" & exit /b 6 )
echo tool: %RI%\swinglab_run.exe
:done
cd /d "%REPO%"
echo == status after
git status --short
echo == end
