@echo off
rem Tools\vcenv.cmd - MSVC x64 developer environment + pinned CMake / Ninja for PhysicsTest (03_WorkPlan M0 task 2).
rem Called by Tools\cmake.cmd and Tools\ctest.cmd; can also be CALLed from an interactive cmd.exe once per session.
rem
rem   PT_VCVARS     optional full path to vcvars64.bat (default: located with vswhere, then the VS 2022 BuildTools path)
rem   PT_CMAKE_BIN  optional directory of cmake.exe  (default: %USERPROFILE%\.cmake-deps\cmake\win\x64\bin)
rem   PT_NINJA_BIN  optional directory of ninja.exe  (default: %USERPROFILE%\.cmake-deps\ninja\win\x64)
rem   PT_SDK_MIRROR optional read-only SDK mirror root (e.g. D:/PhysicsRefs), read by cmake/PTOptions.cmake

if /I "%VSCMD_ARG_TGT_ARCH%"=="x64" goto :tools

set "PT_VS_INSTALLER=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer"
if defined PT_VCVARS goto :vcvars
if not exist "%PT_VS_INSTALLER%\vswhere.exe" goto :vcvars_default
for /f "usebackq delims=" %%I in (`call "%PT_VS_INSTALLER%\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "PT_VCVARS=%%I\VC\Auxiliary\Build\vcvars64.bat"
if defined PT_VCVARS goto :vcvars

:vcvars_default
set "PT_VCVARS=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"

:vcvars
if not exist "%PT_VCVARS%" goto :no_vcvars
rem vcvars64.bat (VsDevCmd extensions) looks for vswhere.exe on PATH.
if exist "%PT_VS_INSTALLER%\vswhere.exe" set "PATH=%PT_VS_INSTALLER%;%PATH%"
call "%PT_VCVARS%" >nul
if errorlevel 1 goto :no_vcvars
if /I not "%VSCMD_ARG_TGT_ARCH%"=="x64" goto :no_vcvars

:tools
if not defined PT_CMAKE_BIN set "PT_CMAKE_BIN=%USERPROFILE%\.cmake-deps\cmake\win\x64\bin"
if not defined PT_NINJA_BIN set "PT_NINJA_BIN=%USERPROFILE%\.cmake-deps\ninja\win\x64"
if exist "%PT_NINJA_BIN%\ninja.exe" set "PATH=%PT_NINJA_BIN%;%PATH%"
if exist "%PT_CMAKE_BIN%\cmake.exe" set "PATH=%PT_CMAKE_BIN%;%PATH%"
where cl.exe >nul 2>nul || goto :no_cl
where cmake.exe >nul 2>nul || goto :no_cmake
where ninja.exe >nul 2>nul || goto :no_ninja
exit /b 0

:no_vcvars
echo [vcenv] error: vcvars64.bat not found or failed ("%PT_VCVARS%"). Set PT_VCVARS to your vcvars64.bat. 1>&2
exit /b 1
:no_cl
echo [vcenv] error: cl.exe is not on PATH after vcvars64.bat 1>&2
exit /b 1
:no_cmake
echo [vcenv] error: cmake.exe not found (set PT_CMAKE_BIN) 1>&2
exit /b 1
:no_ninja
echo [vcenv] error: ninja.exe not found (set PT_NINJA_BIN) 1>&2
exit /b 1
