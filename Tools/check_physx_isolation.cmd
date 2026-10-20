@echo off
rem Tools\check_physx_isolation.cmd <preset> [--clean] - 03_WorkPlan M0 criterion 4 (risk R6) for one preset (dbg | dev | test).
rem Configures out\<preset>-nophysx with PT_WITH_PHYSX=OFF, generates its BuildFlags.json (no compilation needed) and
rem compares it with out\<preset>\BuildFlags.json (build the preset first). Exit code 0 = flags identical.
rem --clean removes out\<preset>-nophysx afterwards (about 0.5 GB per preset: the SDK clones of the second tree).
setlocal
if "%~1"=="" goto :usage
set "PT_PRESET=%~1"
set "PT_CLEAN="
if /i "%~2"=="--clean" set "PT_CLEAN=1"
if not "%~2"=="" if not defined PT_CLEAN goto :usage
set "PT_ROOT=%~dp0.."
call "%~dp0vcenv.cmd" || exit /b 1
if not exist "%PT_ROOT%\out\%PT_PRESET%\BuildFlags.json" (echo build preset %PT_PRESET% first 1>&2 & exit /b 2)
set "PT_RC=0"
cmake -S "%PT_ROOT%" --preset %PT_PRESET% -B "%PT_ROOT%\out\%PT_PRESET%-nophysx" -DPT_WITH_PHYSX=OFF >nul || (set "PT_RC=1" & goto :done)
cmake --build "%PT_ROOT%\out\%PT_PRESET%-nophysx" --target PtBuildFlags || (set "PT_RC=1" & goto :done)
cmake "-DPT_A=%PT_ROOT%\out\%PT_PRESET%\BuildFlags.json" "-DPT_B=%PT_ROOT%\out\%PT_PRESET%-nophysx\BuildFlags.json" -P "%~dp0CompareBuildFlags.cmake"
set "PT_RC=%ERRORLEVEL%"
:done
if defined PT_CLEAN (
	echo Removing %PT_ROOT%\out\%PT_PRESET%-nophysx
	cmake -E rm -rf "%PT_ROOT%\out\%PT_PRESET%-nophysx"
)
exit /b %PT_RC%

:usage
echo usage: Tools\check_physx_isolation.cmd dbg^|dev^|test [--clean] 1>&2
exit /b 2
