@echo off
rem Tools\cmake.cmd - run CMake inside the MSVC x64 environment with the pinned CMake 4.1 / Ninja 1.13 (vcenv.cmd).
rem   Tools\cmake.cmd --preset test
rem   Tools\cmake.cmd --build --preset test
rem Offline / fast SDK clones from a local read-only mirror:  set PT_SDK_MIRROR=D:/PhysicsRefs
setlocal
call "%~dp0vcenv.cmd" || exit /b 1
cmake %*
exit /b %ERRORLEVEL%
