@echo off
setlocal
set "ROOT=%~dp0.."
powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%\scripts\build_gcc.ps1" -Example bridge
if errorlevel 1 exit /b %errorlevel%
call "%ROOT%\host\build-windows.bat"
exit /b %errorlevel%
