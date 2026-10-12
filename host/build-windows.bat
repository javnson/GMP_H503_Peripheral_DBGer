@echo off
setlocal
set "BASE=%~dp0."
cmake -S "%BASE%" -B "%BASE%\build" -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 exit /b %errorlevel%
cmake --build "%BASE%\build" --config Release
if errorlevel 1 exit /b %errorlevel%
ctest --test-dir "%BASE%\build" -C Release --output-on-failure
if errorlevel 1 exit /b %errorlevel%
cmake --install "%BASE%\build" --config Release --prefix "%BASE%\dist"
exit /b %errorlevel%
