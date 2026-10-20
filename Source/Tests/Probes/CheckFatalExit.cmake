# CheckFatalExit.cmake - CTest driver for probes that must terminate through a Fatal log (exit code 3).
#   cmake -DPT_EXE=<exe> -DPT_EXPECT_RC=3 "-DPT_EXPECT_REGEX=<regex>" -P CheckFatalExit.cmake
# PASS when the exit code equals PT_EXPECT_RC and stdout + stderr match PT_EXPECT_REGEX.
foreach(_req PT_EXE PT_EXPECT_RC PT_EXPECT_REGEX)
	if(NOT DEFINED ${_req})
		message(FATAL_ERROR "CheckFatalExit.cmake: -D${_req}=... is required")
	endif()
endforeach()

execute_process(COMMAND "${PT_EXE}" OUTPUT_VARIABLE _out ERROR_VARIABLE _err RESULT_VARIABLE _rc)
message(STATUS "exit code ${_rc}\nstdout: ${_out}\nstderr: ${_err}")
if(NOT "${_rc}" STREQUAL "${PT_EXPECT_RC}")
	message(FATAL_ERROR "CheckFatalExit: exit code '${_rc}', expected ${PT_EXPECT_RC}")
endif()
if(NOT "${_out}${_err}" MATCHES "${PT_EXPECT_REGEX}")
	message(FATAL_ERROR "CheckFatalExit: output does not match /${PT_EXPECT_REGEX}/")
endif()
message(STATUS "CheckFatalExit: PASS")
