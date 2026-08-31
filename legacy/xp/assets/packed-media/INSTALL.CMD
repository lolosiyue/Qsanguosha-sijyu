@echo off
setlocal
set "TARGET=C:\QSanguoshaXP"

echo Installing QSanguosha XP to %TARGET%...
cd /d "%SystemRoot%" || goto failed
if exist "%TARGET%" rmdir /s /q "%TARGET%"
if exist "%TARGET%" goto failed
mkdir "%TARGET%" || goto failed

echo Copying portable files...
xcopy "%~dp0PAYLOAD\*" "%TARGET%\" /E /I /H /Y /Q > nul || goto failed
if not exist "%TARGET%\XP-PAYLOAD.OK" goto failed
if not exist "%TARGET%\lua\config.lua" goto failed
if not exist "%TARGET%\platforms\qwindows.dll" goto failed

:installed
if /I "%~1"=="/NoLaunch" exit /b 0
cd /d "%TARGET%"
start "" "%TARGET%\QSanguoshaXP.exe"
exit /b 0

:failed
echo.
echo QSanguosha XP installation failed.
pause
exit /b 1
