# RunSmoke.cmake - CTest driver for one SDK smoke exe (03_WorkPlan M0 criteria 6, 7).
#   cmake -DPT_SMOKE_EXE=<PtSmoke*.exe> -P RunSmoke.cmake
# PASS requires exit code 0 AND exactly one PT_SMOKE_JSON line that is valid JSON with "pass": true and object-valued
# "build" / "sdk" fields. The line is machine-read later (G0, M7), so a malformed line fails the smoke test now.
if(NOT PT_SMOKE_EXE)
	message(FATAL_ERROR "RunSmoke.cmake: -DPT_SMOKE_EXE=<exe> is required")
endif()

execute_process(
	COMMAND "${PT_SMOKE_EXE}"
	OUTPUT_VARIABLE _out
	ERROR_VARIABLE _err
	ECHO_OUTPUT_VARIABLE
	ECHO_ERROR_VARIABLE
	RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
	message(FATAL_ERROR "RunSmoke: ${PT_SMOKE_EXE} exited with '${_rc}'")
endif()

string(FIND "${_out}" "PT_SMOKE_JSON " _first)
string(FIND "${_out}" "PT_SMOKE_JSON " _last REVERSE)
if(_first EQUAL -1)
	message(FATAL_ERROR "RunSmoke: no PT_SMOKE_JSON line in the output")
endif()
if(NOT _first EQUAL _last)
	message(FATAL_ERROR "RunSmoke: more than one PT_SMOKE_JSON line in the output")
endif()
string(REGEX MATCH "PT_SMOKE_JSON ([^\r\n]*)" _line "${_out}")
set(_json "${CMAKE_MATCH_1}")

# string(JSON) parses the whole document on every call, so any syntax error anywhere in the line is reported.
string(JSON _pass ERROR_VARIABLE _json_err GET "${_json}" pass)
if(_json_err)
	message(FATAL_ERROR "RunSmoke: PT_SMOKE_JSON is not valid JSON (${_json_err}):\n${_json}")
endif()
foreach(_member build sdk)
	string(JSON _type ERROR_VARIABLE _json_err TYPE "${_json}" ${_member})
	if(_json_err OR NOT _type STREQUAL "OBJECT")
		message(FATAL_ERROR "RunSmoke: PT_SMOKE_JSON member '${_member}' must be an object (${_type} ${_json_err})")
	endif()
endforeach()
if(NOT _pass)
	message(FATAL_ERROR "RunSmoke: PT_SMOKE_JSON reports pass=${_pass}")
endif()
string(JSON _engine GET "${_json}" engine)
message(STATUS "RunSmoke: ${_engine} PASS (PT_SMOKE_JSON valid)")
