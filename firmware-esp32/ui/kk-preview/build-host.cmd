@echo off
set VSLANG=1033
call "G:\software\VStudio\VC\Auxiliary\Build\vcvars64.bat" >nul
if not "%errorlevel%"=="0" exit /b %errorlevel%
cmake -S "%~dp0." -B "%~dp0build" -G Ninja -DCMAKE_BUILD_TYPE=Release
if not "%errorlevel%"=="0" exit /b %errorlevel%
cmake --build "%~dp0build"
if not "%errorlevel%"=="0" exit /b %errorlevel%
ctest --test-dir "%~dp0build" --output-on-failure
