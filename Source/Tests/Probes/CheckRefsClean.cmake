# CheckRefsClean.cmake - CTest PhysicsTest.Probe.RefsClean (03_WorkPlan M0 criterion 2, 02_Architecture 4.5).
#   cmake -DPT_SDK_MIRROR=<mirror> -DPT_SOURCE_DIR=<repo> -DGIT_EXECUTABLE=<git> -P CheckRefsClean.cmake
# 1. `git status --porcelain` of JoltPhysics, PhysX and bullet3 in the read-only mirror is empty
#    (also with --ignored, which catches generated files that the SDKs' own .gitignore would hide, e.g. PxConfig.h).
# 2. No PxConfig.h, *.dxil or *.spv exists in the mirror or in this repository's source tree (out/ and .venv excluded).
# The probe itself must not write into the mirror: plain `git status` takes optional locks (it creates and deletes
# .git/index.lock and may rewrite .git/index to refresh stat data). --no-optional-locks (and GIT_OPTIONAL_LOCKS=0 for
# any nested git) keeps every call read-only.
set(_failures "")
set(ENV{GIT_OPTIONAL_LOCKS} 0)

if(PT_SDK_MIRROR)
	foreach(_repo JoltPhysics PhysX bullet3)
		set(_dir "${PT_SDK_MIRROR}/${_repo}")
		if(NOT EXISTS "${_dir}/.git")
			list(APPEND _failures "${_dir} is not a git checkout")
			continue()
		endif()
		foreach(_mode "" "--ignored")
			execute_process(COMMAND "${GIT_EXECUTABLE}" --no-optional-locks -C "${_dir}" status --porcelain ${_mode}
				OUTPUT_VARIABLE _out ERROR_VARIABLE _err RESULT_VARIABLE _rc)
			string(STRIP "${_out}" _out)
			if(NOT _rc EQUAL 0)
				list(APPEND _failures "git status ${_mode} failed in ${_dir}: ${_err}")
			elseif(_out)
				list(APPEND _failures "git status --porcelain ${_mode} in ${_dir} is not empty:\n${_out}")
			else()
				message(STATUS "${_repo}: git status --porcelain ${_mode} -> empty")
			endif()
		endforeach()
		file(GLOB_RECURSE _generated LIST_DIRECTORIES false "${_dir}/*PxConfig.h" "${_dir}/*.dxil" "${_dir}/*.spv")
		if(_generated)
			list(APPEND _failures "generated files in ${_dir}: ${_generated}")
		endif()
	endforeach()
else()
	message(STATUS "PT_SDK_MIRROR is not set: the reference checkouts are not used by this build tree; mirror checks skipped")
endif()

foreach(_sub Source cmake Tools docs prototypes)
	if(EXISTS "${PT_SOURCE_DIR}/${_sub}")
		file(GLOB_RECURSE _generated LIST_DIRECTORIES false
			"${PT_SOURCE_DIR}/${_sub}/*PxConfig.h" "${PT_SOURCE_DIR}/${_sub}/*.dxil" "${PT_SOURCE_DIR}/${_sub}/*.spv")
		if(_generated)
			list(APPEND _failures "generated files in the repository source tree: ${_generated}")
		endif()
	endif()
endforeach()

if(_failures)
	string(REPLACE ";" "\n" _text "${_failures}")
	message(FATAL_ERROR "RefsClean: FAIL\n${_text}")
endif()
message(STATUS "RefsClean: PASS")
