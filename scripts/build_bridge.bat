@echo off
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build_gcc.ps1" -Example bridge
if errorlevel 1 exit /b %errorlevel%
call "%~dp0..\host\build-windows.bat"
exit /b %errorlevel%
