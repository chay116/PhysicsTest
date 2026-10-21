# CheckFatalExit.cmake - CTest driver for programs that must end with a given exit code (a Fatal log: 3; the bench exit
# codes 2 / 5 / 6, M3).
#   cmake -DPT_EXE=<exe> -DPT_EXPECT_RC=3 "-DPT_EXPECT_REGEX=<regex>" ["-DPT_ARGS=a|b|c"]
#         [-DPT_EXPECT_FILE=<path> "-DPT_EXPECT_FILE_REGEX=<regex>"] [-DPT_EXPECT_FILE2=<path> "-DPT_EXPECT_FILE2_REGEX=<regex>"]
#         -P CheckFatalExit.cmake
# PASS when the exit code equals PT_EXPECT_RC and stdout + stderr match PT_EXPECT_REGEX (and, when given, PT_EXPECT_FILE /
# PT_EXPECT_FILE2 exist after the run and their text matches PT_EXPECT_FILE_REGEX / PT_EXPECT_FILE2_REGEX; the files are
# removed before the run). PT_ARGS: arguments separated by '|' (a ';' list would be split by CTest).
foreach(_req PT_EXE PT_EXPECT_RC PT_EXPECT_REGEX)
	if(NOT DEFINED ${_req})
		message(FATAL_ERROR "CheckFatalExit.cmake: -D${_req}=... is required")
	endif()
endforeach()

set(_args "")
if(DEFINED PT_ARGS)
	string(REPLACE "|" ";" _args "${PT_ARGS}")
endif()
foreach(_file_var PT_EXPECT_FILE PT_EXPECT_FILE2)
	if(DEFINED ${_file_var})
		file(REMOVE "${${_file_var}}")
	endif()
endforeach()
execute_process(COMMAND "${PT_EXE}" ${_args} OUTPUT_VARIABLE _out ERROR_VARIABLE _err RESULT_VARIABLE _rc)
message(STATUS "exit code ${_rc}\nstdout: ${_out}\nstderr: ${_err}")
if(NOT "${_rc}" STREQUAL "${PT_EXPECT_RC}")
	message(FATAL_ERROR "CheckFatalExit: exit code '${_rc}', expected ${PT_EXPECT_RC}")
endif()
if(NOT "${_out}${_err}" MATCHES "${PT_EXPECT_REGEX}")
	message(FATAL_ERROR "CheckFatalExit: output does not match /${PT_EXPECT_REGEX}/")
endif()
foreach(_file_var PT_EXPECT_FILE PT_EXPECT_FILE2)
	if(NOT DEFINED ${_file_var})
		continue()
	endif()
	set(_path "${${_file_var}}")
	if(NOT EXISTS "${_path}")
		message(FATAL_ERROR "CheckFatalExit: '${_path}' was not written")
	endif()
	file(READ "${_path}" _file_text)
	if(DEFINED ${_file_var}_REGEX AND NOT _file_text MATCHES "${${_file_var}_REGEX}")
		message(FATAL_ERROR "CheckFatalExit: '${_path}' does not match /${${_file_var}_REGEX}/:
${_file_text}")
	endif()
	message(STATUS "CheckFatalExit: ${_path} matches")
endforeach()
message(STATUS "CheckFatalExit: PASS")
