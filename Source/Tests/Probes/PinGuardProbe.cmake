# PinGuardProbe.cmake - CTest PhysicsTest.Probe.PinGuard (03_WorkPlan M0 criterion 8, first half).
# Runs throw-away configures that MUST stop with FATAL_ERROR:
#   1. PT_JOLT_GIT_TAG set to a tag name (v5.5.0)                    -> pin guard in PTOptions.cmake, before any download
#   2. PT_PHYSX_GIT_TAG set to a different full SHA                  -> same guard
#   3. FETCHCONTENT_SOURCE_DIR_JOLT = the read-only mirror           -> "never built in place" guard (PtFetch.cmake)
#   4. the same with the mirror path upper-cased                     -> the guard compares paths case-insensitively
#   5. FETCHCONTENT_SOURCE_DIR_JOLT = an external directory, no PT_SDK_MIRROR -> "outside FETCHCONTENT_BASE_DIR" guard
#   6. FETCHCONTENT_SOURCE_DIR_JOLT = a git checkout inside the probe's own _deps at another HEAD -> HEAD guard
#      (a throw-away repository with one empty commit, so the case does not depend on this repository having .git)
# Already-populated small dependencies of the calling build tree are reused through FETCHCONTENT_SOURCE_DIR_<NAME>.
# Nothing is downloaded. PT_PROBE_DIR is removed when the probe finishes.
#
#   cmake -DPT_SOURCE_DIR=... -DPT_BINARY_DIR=... -DPT_PROBE_DIR=... -DPT_MAKE_PROGRAM=... -DPT_C_COMPILER=...
#         -DPT_CXX_COMPILER=... [-DPT_SDK_MIRROR=...] [-DGIT_EXECUTABLE=...] -P PinGuardProbe.cmake
set(_base
	-G Ninja
	"-DCMAKE_MAKE_PROGRAM=${PT_MAKE_PROGRAM}"
	"-DCMAKE_C_COMPILER=${PT_C_COMPILER}"
	"-DCMAKE_CXX_COMPILER=${PT_CXX_COMPILER}"
	-DCMAKE_BUILD_TYPE=Release
	-DPT_PRESET=pinguard-probe)
foreach(_dep imgui implot stb nlohmann_json doctest)
	string(TOUPPER "${_dep}" _upper)
	if(EXISTS "${PT_BINARY_DIR}/_deps/${_dep}-src")
		list(APPEND _base "-DFETCHCONTENT_SOURCE_DIR_${_upper}=${PT_BINARY_DIR}/_deps/${_dep}-src")
	endif()
endforeach()
set(_common ${_base} "-DPT_SDK_MIRROR=${PT_SDK_MIRROR}")
set(_jolt_only -DPT_WITH_BULLET=OFF -DPT_WITH_PHYSX=OFF)

if(NOT GIT_EXECUTABLE)
	find_program(GIT_EXECUTABLE git REQUIRED)
endif()

file(REMOVE_RECURSE "${PT_PROBE_DIR}")
set(_failures "")

# _pt_expect_fatal(<case> <regex> [MIRROR_UNSET] <configure args>...)
function(_pt_expect_fatal Name Regex)
	set(_args ${ARGN})
	set(_cache ${_common})
	if("MIRROR_UNSET" IN_LIST _args)
		list(REMOVE_ITEM _args MIRROR_UNSET)
		set(_cache ${_base} "-DPT_SDK_MIRROR=")
	endif()
	set(_dir "${PT_PROBE_DIR}/${Name}")
	execute_process(
		COMMAND "${CMAKE_COMMAND}" -S "${PT_SOURCE_DIR}" -B "${_dir}" ${_cache} ${_args}
		OUTPUT_VARIABLE _out ERROR_VARIABLE _err RESULT_VARIABLE _rc)
	# CMake wraps long messages: match with every whitespace run collapsed to one space.
	string(REGEX REPLACE "[ \t\r\n]+" " " _all "${_out}${_err}")
	string(REGEX MATCH "${Regex}" _hit "${_all}")
	if(_rc EQUAL 0)
		list(APPEND _failures "${Name}: configure SUCCEEDED but must fail")
	elseif(NOT _hit)
		string(SUBSTRING "${_err}" 0 1500 _excerpt)
		list(APPEND _failures "${Name}: configure failed for another reason (expected /${Regex}/):\n${_excerpt}")
	else()
		message(STATUS "${Name}: FATAL as expected -> '${_hit}'")
	endif()
	file(REMOVE_RECURSE "${_dir}")
	set(_failures "${_failures}" PARENT_SCOPE)
endfunction()

_pt_expect_fatal(tag_name "PT_JOLT_GIT_TAG='v5.5.0' differs from the pinned SHA"
	-DPT_JOLT_GIT_TAG=v5.5.0 ${_jolt_only})
_pt_expect_fatal(other_sha "PT_PHYSX_GIT_TAG='[0-9a-f]+' differs from the pinned SHA"
	-DPT_PHYSX_GIT_TAG=0123456789abcdef0123456789abcdef01234567 -DPT_WITH_JOLT=OFF -DPT_WITH_BULLET=OFF)
if(PT_SDK_MIRROR)
	_pt_expect_fatal(in_place "is inside the read-only mirror"
		"-DFETCHCONTENT_SOURCE_DIR_JOLT=${PT_SDK_MIRROR}/JoltPhysics" ${_jolt_only})
	string(TOUPPER "${PT_SDK_MIRROR}/JoltPhysics" _mirror_upper)
	_pt_expect_fatal(in_place_case "is inside the read-only mirror"
		"-DFETCHCONTENT_SOURCE_DIR_JOLT=${_mirror_upper}" ${_jolt_only})
endif()
# This repository stands in for "some external checkout": the location guard fires before any git command.
_pt_expect_fatal(external_dir "is outside this build tree's FETCHCONTENT_BASE_DIR"
	MIRROR_UNSET "-DFETCHCONTENT_SOURCE_DIR_JOLT=${PT_SOURCE_DIR}" ${_jolt_only})

# Throw-away checkout at an unpinned HEAD, inside the probe build tree's _deps (= its FETCHCONTENT_BASE_DIR), so only
# the HEAD guard can reject it. Identity and signing are given on the command line: no dependency on global git config.
set(_fake "${PT_PROBE_DIR}/wrong_head/_deps/jolt-src")
file(MAKE_DIRECTORY "${_fake}")
execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${_fake}" init -q RESULT_VARIABLE _rc_init)
execute_process(
	COMMAND "${GIT_EXECUTABLE}" -C "${_fake}" -c user.name=PhysicsTest -c user.email=probe@invalid
		-c commit.gpgsign=false commit -q --allow-empty --no-verify -m "PinGuard probe: unpinned HEAD"
	RESULT_VARIABLE _rc_commit ERROR_VARIABLE _err_commit)
if(NOT _rc_init EQUAL 0 OR NOT _rc_commit EQUAL 0)
	list(APPEND _failures "wrong_head: could not create the throw-away repository in ${_fake}: ${_err_commit}")
else()
	_pt_expect_fatal(wrong_head "is at HEAD '[0-9a-f]+' .*expected the pinned SHA"
		"-DFETCHCONTENT_SOURCE_DIR_JOLT=${_fake}" ${_jolt_only})
endif()

file(REMOVE_RECURSE "${PT_PROBE_DIR}")
if(_failures)
	string(REPLACE ";" "\n" _text "${_failures}")
	message(FATAL_ERROR "PinGuard: FAIL\n${_text}")
endif()
message(STATUS "PinGuard: PASS")
