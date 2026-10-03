@echo off
rem Run network_runner.py with the options below.

set MODES=08p
set RUNS=1
set GENERAL=s4_huangzhong
set GENERAL2=
set CONSOLE=
set LOG_DIR=
set LABEL=
rem CONSOLE shows server output in this terminal.
rem When enabled, server.log is skipped but the marker file is still written.

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
