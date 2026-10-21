# CheckFatalExit.cmake - CTest driver for programs that must end with a given exit code (a Fatal log: 3; the bench exit
# codes 2 / 5 / 6, M3).
#   cmake -DPT_EXE=<exe> -DPT_EXPECT_RC=3 "-DPT_EXPECT_REGEX=<regex>" ["-DPT_ARGS=a|b|c"]
#         [-DPT_EXPECT_FILE=<path> "-DPT_EXPECT_FILE_REGEX=<regex>"] -P CheckFatalExit.cmake
# PASS when the exit code equals PT_EXPECT_RC and stdout + stderr match PT_EXPECT_REGEX (and, when given, PT_EXPECT_FILE
# exists after the run and its text matches PT_EXPECT_FILE_REGEX; the file is removed before the run). PT_ARGS:
# arguments separated by '|' (a ';' list would be split by CTest).
foreach(_req PT_EXE PT_EXPECT_RC PT_EXPECT_REGEX)
	if(NOT DEFINED ${_req})
		message(FATAL_ERROR "CheckFatalExit.cmake: -D${_req}=... is required")
	endif()
endforeach()

set(_args "")
if(DEFINED PT_ARGS)
	string(REPLACE "|" ";" _args "${PT_ARGS}")
endif()
if(DEFINED PT_EXPECT_FILE)
	file(REMOVE "${PT_EXPECT_FILE}")
endif()
execute_process(COMMAND "${PT_EXE}" ${_args} OUTPUT_VARIABLE _out ERROR_VARIABLE _err RESULT_VARIABLE _rc)
message(STATUS "exit code ${_rc}\nstdout: ${_out}\nstderr: ${_err}")
if(NOT "${_rc}" STREQUAL "${PT_EXPECT_RC}")
	message(FATAL_ERROR "CheckFatalExit: exit code '${_rc}', expected ${PT_EXPECT_RC}")
endif()
if(NOT "${_out}${_err}" MATCHES "${PT_EXPECT_REGEX}")
	message(FATAL_ERROR "CheckFatalExit: output does not match /${PT_EXPECT_REGEX}/")
endif()
if(DEFINED PT_EXPECT_FILE)
	if(NOT EXISTS "${PT_EXPECT_FILE}")
		message(FATAL_ERROR "CheckFatalExit: '${PT_EXPECT_FILE}' was not written")
	endif()
	file(READ "${PT_EXPECT_FILE}" _file_text)
	if(DEFINED PT_EXPECT_FILE_REGEX AND NOT _file_text MATCHES "${PT_EXPECT_FILE_REGEX}")
		message(FATAL_ERROR "CheckFatalExit: '${PT_EXPECT_FILE}' does not match /${PT_EXPECT_FILE_REGEX}/:
${_file_text}")
	endif()
	message(STATUS "CheckFatalExit: ${PT_EXPECT_FILE} matches")
endif()
message(STATUS "CheckFatalExit: PASS")
