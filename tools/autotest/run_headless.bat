@echo off
rem ============================================================
rem  headless stress-test entry (calls headless_runner.py)
rem  All options are set below. Leave empty = use default.
rem ============================================================

rem ---- your choices (edit here) ------------------------------
set MODES=20p
set GAMES=3
set PARALLEL=1
set LOG_DIR=
set LABEL=
rem  PARALLEL = total parallel processes; when there are fewer
rem  modes than PARALLEL, the same mode is duplicated round-robin
rem ------------------------------------------------------------

set "ARGS=--exe-root "%~dp0..\..""
if not "%MODES%"==""   set "ARGS=%ARGS% --modes "%MODES%""
if not "%GAMES%"==""   set "ARGS=%ARGS% --games %GAMES%"
if not "%PARALLEL%"=="" set "ARGS=%ARGS% --parallel %PARALLEL%"
if not "%LOG_DIR%"==""  set "ARGS=%ARGS% --log-dir "%LOG_DIR%""
if not "%LABEL%"==""    set "ARGS=%ARGS% --label "%LABEL%""

python "%~dp0headless_runner.py" %ARGS%
exit /b %ERRORLEVEL%
