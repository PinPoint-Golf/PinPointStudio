@echo off
rem studio-build.cmd — the ONE way to build on GOLFSIMPC from the Mac. Lives in the repo so it
rem is never retyped through ssh quoting. Run it by file:
rem   ssh developer@GOLFSIMPC.local "cmd /c C:\Users\developer\Projects\PinPointStudio\tools\studio\studio-build.cmd [app|tool|both] [pull|nopull]"
rem Defaults: both, pull. Pulls origin/main fast-forward (refuses on a dirty tree), builds the
rem Release app in build\Release-Installer (cmake . first so the embedded sha is HEAD's), copies
rem it beside the installed app as bin\PinPointStudio.dag.exe (use-dag.cmd swaps it in), and
rem builds swinglab_run in build\swinglab-vs18 as swinglab_run_final.exe. A full app build is
rem about 2 minutes: poll the log after 60 s, never wait out a watchdog. Logs: C:\PinPointStudio\scratch\studio-build\
setlocal
set WHAT=%~1
if "%WHAT%"=="" set WHAT=both
set PULL=%~2
if "%PULL%"=="" set PULL=pull
set REPO=C:\Users\developer\Projects\PinPointStudio
set LOGS=C:\PinPointStudio\scratch\studio-build
set BIN=C:\Users\developer\AppData\Local\Programs\PinPointStudio\bin
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
if "%WHAT%"=="tool" goto tool
echo app: cmake %TIME%
cd /d "%REPO%\build\Release-Installer"
C:\Qt\Tools\CMake_64\bin\cmake.exe . > "%LOGS%\app_cmake.log" 2>&1
echo app: jom %TIME%
jom -j8 PinPointStudio > "%LOGS%\app_build.log" 2>&1
set RC=%ERRORLEVEL%
echo app: build exit %RC% %TIME%
if not "%RC%"=="0" ( type "%LOGS%\app_build.log" | findstr /i "error" & exit /b 5 )
copy /y PinPointStudio.exe "%BIN%\PinPointStudio.dag.exe" >nul
echo app: deployed %BIN%\PinPointStudio.dag.exe
if "%WHAT%"=="app" goto done
:tool
echo tool: jom %TIME%
cd /d "%REPO%\build\swinglab-vs18"
jom -j8 swinglab_run > "%LOGS%\tool_build.log" 2>&1
set RC=%ERRORLEVEL%
echo tool: build exit %RC% %TIME%
if not "%RC%"=="0" ( type "%LOGS%\tool_build.log" | findstr /i "error" & exit /b 6 )
copy /y swinglab_run.exe "%REPO%\build\Desktop_Qt_6_11_0_MSVC2022_64bit-Release\swinglab_run_final.exe" >nul
echo tool: deployed swinglab_run_final.exe
:done
cd /d "%REPO%"
echo == status after
git status --short
echo == end
