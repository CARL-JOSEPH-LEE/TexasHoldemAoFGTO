@echo off
setlocal
cd /d "%~dp0"
python tools\prepare_sources.py
if errorlevel 1 exit /b %errorlevel%
python tools\package_gui.py %*
exit /b %errorlevel%
