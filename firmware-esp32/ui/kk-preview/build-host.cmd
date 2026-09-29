@echo off
setlocal
rem Uses CMake's detected toolchain, or the caller's Developer Command Prompt.
python "%~dp0build-host.py" %*
exit /b %errorlevel%
