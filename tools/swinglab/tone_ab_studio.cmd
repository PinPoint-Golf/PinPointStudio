@echo off
rem tone_ab_studio.cmd - #183's analysis A/B on the studio PC. ASCII ONLY and CRLF, like
rem tools\studio\studio-build.cmd: cmd misreads a batch file with multi-byte characters.
rem   tone_ab_studio.cmd one^|corpus^|digest^|strata ^<share^> ^<repo^> ^<python^> ^<qt-bin^> [^<one-id^>]
rem     share   the share's root as the studio PC sees it; the run's root is ^<share^>\scratch\i183-ab
rem     repo    the studio PC's checkout, whose build\Release-Installer holds swinglab_run.exe
rem     python  the swinglab venv's python.exe;  qt-bin  Qt's bin folder (its DLLs go on PATH)
rem     one-id  phase one only: the manifest id of the one-swing step's swing
rem Nothing private is a default: tools/swinglab/tone_ab.sh passes all of them from its environment.
rem Launched detached from the Mac by tone_ab.sh (Win32_Process Create), from a copy of tools\swinglab\tone_ab_*
rem on the share (^<share^>\scratch\i183-ab\_tools\), never from the checkout. Runs swinglab_run.exe IN PLACE
rem (beside its DLLs and models) through tone_ab_run.py. It reads only the input copies under the run's root
rem (in and in_nobase, and the corpus pins, read only) and writes only under the run's root.
rem Console: ^<phase^>.console.log; exit code: ^<phase^>.rc.txt (written last).
setlocal
set PHASE=%~1
if "%~5"=="" (
  echo USAGE: tone_ab_studio.cmd one^|corpus^|digest^|strata share repo python qt-bin [one-id]
  exit /b 2
)
set SHARE=%~2
set ROOT=%SHARE%\scratch\i183-ab
set REPO=%~3
set RI=%REPO%\build\Release-Installer
set EXE=%RI%\swinglab_run.exe
set PY=%~4
set QTBIN=%~5
set ONEARG=
if not "%~6"=="" set ONEARG=--one-id %~6
set LOG=%ROOT%\%PHASE%.console.log
if exist "%ROOT%\%PHASE%.rc.txt" del /f /q "%ROOT%\%PHASE%.rc.txt"
>>"%LOG%" echo == tone_ab_studio %PHASE% start %DATE% %TIME%
git -C "%REPO%" log -1 --oneline >>"%LOG%" 2>&1
git -C "%REPO%" status --short >>"%LOG%" 2>&1
for %%F in ("%EXE%") do >>"%LOG%" echo swinglab_run.exe %%~tF %%~zF bytes
set "PATH=%QTBIN%;%RI%;%PATH%"
cd /d "%RI%"
"%PY%" -I -X utf8 "%~dp0tone_ab_run.py" --phase %PHASE% --share "%SHARE%" --root "%ROOT%" --exe "%EXE%" %ONEARG% >>"%LOG%" 2>&1
set RC=%ERRORLEVEL%
>>"%LOG%" echo == tone_ab_studio %PHASE% end %DATE% %TIME% - exit %RC%
>"%ROOT%\%PHASE%.rc.tmp" echo %RC%
move /y "%ROOT%\%PHASE%.rc.tmp" "%ROOT%\%PHASE%.rc.txt" >nul
exit /b %RC%
