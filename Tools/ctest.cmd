@echo off
rem Tools\ctest.cmd - run CTest inside the same environment as Tools\cmake.cmd.
rem   Tools\ctest.cmd --preset test
rem   Tools\ctest.cmd --preset dbg -R PhysicsTest.Smoke
setlocal
call "%~dp0vcenv.cmd" || exit /b 1
ctest %*
exit /b %ERRORLEVEL%
