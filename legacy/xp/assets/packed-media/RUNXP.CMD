@echo off
call "%~dp0INSTALL.CMD" /NoLaunch || exit /b 1

cd /d "C:\QSanguoshaXP"
start "QSanguoshaXP Server" /MIN "C:\QSanguoshaXP\QSanguoshaXP.exe" -server
ping 127.0.0.1 -n 6 > nul
start "" "C:\QSanguoshaXP\QSanguoshaXP.exe" -connect:127.0.0.1
echo launched> "C:\QSanguoshaXP\XP-ACCEPTANCE-LAUNCHED.TXT"
exit /b 0
