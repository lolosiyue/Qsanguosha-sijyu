@echo off
rem ============================================================
rem  real-network test entry (calls network_runner.py)
rem  All options are set below. Leave empty = use default.
rem ============================================================

rem ---- your choices (edit here) ------------------------------
set MODES=20p
set RUNS=1
set GENERAL=s4_huangzhong
set GENERAL2=
set CONSOLE=
set LOG_DIR=
set LABEL=
rem  CONSOLE = set to 1 to show server output on this terminal
rem  (server.log is skipped; marker file still written)
rem ------------------------------------------------------------

set "ARGS=--exe-root "%~dp0..\..""
if not "%MODES%"==""    set "ARGS=%ARGS% --modes "%MODES%""
if not "%RUNS%"==""     set "ARGS=%ARGS% --runs %RUNS%"
if not "%GENERAL%"==""  set "ARGS=%ARGS% --general "%GENERAL%""
if not "%GENERAL2%"=="" set "ARGS=%ARGS% --general2 "%GENERAL2%""
if not "%CONSOLE%"==""  set "ARGS=%ARGS% --console"
if not "%LOG_DIR%"==""  set "ARGS=%ARGS% --log-dir "%LOG_DIR%""
if not "%LABEL%"==""    set "ARGS=%ARGS% --label "%LABEL%""

python "%~dp0network_runner.py" %ARGS%
exit /b %ERRORLEVEL%
