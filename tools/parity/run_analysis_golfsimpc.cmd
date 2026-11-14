@echo off
rem run_analysis_golfsimpc.cmd - the ANALYSIS half of the refactor's parity baseline, on GOLFSIMPC
rem (the cabin PC). ASCII ONLY and CRLF, like tools\studio\studio-build.cmd: cmd misreads a batch
rem file with multi-byte characters. Run it by file from the Mac:
rem   ssh developer@GOLFSIMPC.local "cmd /c C:\Users\developer\Projects\PinPointStudio\tools\parity\run_analysis_golfsimpc.cmd <label>"
rem FIRST build swinglab_run at the commit under test, the one way:
rem   ssh developer@GOLFSIMPC.local "cmd /c C:\Users\developer\Projects\PinPointStudio\tools\studio\studio-build.cmd tool pull"
rem   (or "tool nopull patch C:\PinPointStudio\scratch\<run>\wip.patch" for uncommitted Mac work).
rem What it does: runs build\Release-Installer\swinglab_run.exe IN PLACE (beside its DLLs and
rem models; Qt's bin on PATH) over the 61 pinned-pose swings of parity_set.json, through
rem parity_baseline.py --half analysis: swinglab_run <corpus swing> --out <run> --pose <pose2>
rem [--dtl-pose <pose2_dtl>], so no pose is re-inferred. It reads the corpus and writes only
rem C:\PinPointStudio\scratch\parity\<label>\ (= /mnt/swingdata/scratch/parity/<label>/ on the Mac):
rem out\analysis.json and out\run_analysis.json (commit, binary, per-swing seconds and exit codes).
rem The result trees are deleted once digested. About 6-8 s a swing, so 61 swings take 6-9 min:
rem a foreground ssh call is fine; for a detached launch poll for analysis.done (it holds the exit
rem code and is written last). The log is analysis.log beside out\.
rem The Python it runs is found beside this file (%~dp0), so a copy of tools\parity\ on the share
rem works as well as the checkout (uncommitted Mac work: cp tools/parity/* to
rem /mnt/swingdata/scratch/parity/_tools/ and run C:\PinPointStudio\scratch\parity\_tools\ this file).
rem PARITY_PYTHON overrides the interpreter (default the swinglab venv; Python 3.7 or later).
rem Exit codes: 0 every swing ok; 1 a swing failed (see the log); 2 refused (no label, label
rem already used); 3 no swinglab_run.exe; 4 no python.
setlocal
set LABEL=%~1
if "%LABEL%"=="" (
  echo USAGE: run_analysis_golfsimpc.cmd ^<label^>
  exit /b 2
)
set REPO=C:\Users\developer\Projects\PinPointStudio
set RI=%REPO%\build\Release-Installer
set EXE=%RI%\swinglab_run.exe
set PY=C:\Users\developer\.swinglab-venv\Scripts\python.exe
if defined PARITY_PYTHON set "PY=%PARITY_PYTHON%"
set RUN=C:\PinPointStudio\scratch\parity\%LABEL%
if not exist "%EXE%" (
  echo NO BINARY: %EXE% - build it first with tools\studio\studio-build.cmd tool
  exit /b 3
)
if not exist "%PY%" (
  echo NO PYTHON: %PY% - set PARITY_PYTHON
  exit /b 4
)
if exist "%RUN%\out\analysis.json" (
  echo LABEL USED: %RUN%\out\analysis.json exists - pick a new label
  exit /b 2
)
if not exist "%RUN%" mkdir "%RUN%"
set "PATH=C:\Qt\6.11.0\msvc2022_64\bin;%RI%;%PATH%"
set PARITY_REPO=%REPO%
set LOG=%RUN%\analysis.log
if exist "%RUN%\analysis.done" del /f /q "%RUN%\analysis.done"
>"%LOG%" echo == run_analysis_golfsimpc %LABEL% %DATE% %TIME%
git -C "%REPO%" log -1 --oneline >>"%LOG%" 2>&1
git -C "%REPO%" status --short >>"%LOG%" 2>&1
for %%F in ("%EXE%") do >>"%LOG%" echo swinglab_run.exe %%~tF %%~zF bytes
cd /d "%RI%"
"%PY%" -I -X utf8 "%~dp0parity_baseline.py" "%LABEL%" --half analysis --swinglab "%EXE%" --set "%~dp0parity_set.json" >>"%LOG%" 2>&1
set RC=%ERRORLEVEL%
>>"%LOG%" echo == exit %RC% %DATE% %TIME%
>"%RUN%\analysis.done" echo %RC%
findstr /b /c:"analysis:" /c:"refusing" /c:"==" /c:"[" "%LOG%" | findstr /v /c:"] ok"
exit /b %RC%
