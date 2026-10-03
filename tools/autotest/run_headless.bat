@echo off
rem Run headless_runner.py with the options below.

set MODES=08p
set GAMES=3
set PARALLEL=1
set GENERAL=
set GENERAL2=
set SPAWNDELAY=3
set LOG_DIR=
set LABEL=
rem EXE and SEED are passed explicitly; defaults are selected below.
if not defined EXE if exist "%~dp0..\..\release\QSanguosha.exe" set "EXE=%~dp0..\..\release\QSanguosha.exe"
if not defined EXE set "EXE=%~dp0..\..\debug\QSanguosha.exe"
if not defined SEED for /f %%i in ('powershell -NoProfile -Command "Get-Date -Format yyyyMMdd"') do set "SEED=%%i"
rem PARALLEL sets the process limit; repeated modes fill unused slots.
rem GENERAL forces the lord's general in each game.
rem GENERAL2 forces the lord's deputy in dual-general mode.
rem SPAWNDELAY sets the interval between starts; stagger them to avoid antivirus blocking.

set "ARGS=--exe-root "%~dp0..\.." --exe "%EXE%" --seed %SEED%"
if not "%MODES%"==""   set "ARGS=%ARGS% --modes "%MODES%""
if not "%GAMES%"==""   set "ARGS=%ARGS% --games %GAMES%"
if not "%PARALLEL%"=="" set "ARGS=%ARGS% --parallel %PARALLEL%"
if not "%GENERAL%"==""  set "ARGS=%ARGS% --general "%GENERAL%""
if not "%GENERAL2%"=="" set "ARGS=%ARGS% --general2 "%GENERAL2%""
if not "%SPAWNDELAY%"=="" set "ARGS=%ARGS% --spawn-delay %SPAWNDELAY%"
if not "%LOG_DIR%"==""  set "ARGS=%ARGS% --log-dir "%LOG_DIR%""
if not "%LABEL%"==""    set "ARGS=%ARGS% --label "%LABEL%""

python "%~dp0headless_runner.py" %ARGS%
exit /b %ERRORLEVEL%