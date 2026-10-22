# RunStampCheck.cmake - the conformance stamp of one backend (03_WorkPlan M10 task 4; 02_Architecture 12.4).
#   1. PhysicsTestBench -Conformance -Backend=<b> -StampDir=<dir>: exit 0; conformance.json says the stamp was written and is
#      valid; <dir> holds exactly one physicstest.stamp/1 file named after its key, with G10b among its gates - a pass (M10 fix
#      round 1: -Conformance turns a G10b Skip into a fail, so a valid stamp always carries the single-thread proof) - and
#      pending_sign_offs 0.
#   2. a run of the same backend with -StampDir=<dir> records that stamp: manifest conformance_stamp valid, the same key.
#   3. a run with an -ini: switch (here the Base value of DefaultGravityZ again: the same physics, another configuration
#      command line) has another key (config_overrides): no stamp, valid false.
#   cmake -DPT_BENCH_EXE=<exe> -DPT_OUT_DIR=<dir> -DPT_BACKEND=<name> -P RunStampCheck.cmake
cmake_minimum_required(VERSION 3.25)
file(REMOVE_RECURSE "${PT_OUT_DIR}")
set(_stamps "${PT_OUT_DIR}/stamps")
execute_process(COMMAND "${PT_BENCH_EXE}" -Conformance "-Backend=${PT_BACKEND}" "-StampDir=${_stamps}" "-Out=${PT_OUT_DIR}/conformance"
	RESULT_VARIABLE _rc OUTPUT_VARIABLE _stdout ERROR_VARIABLE _stderr TIMEOUT 600)
if(NOT _rc EQUAL 0)
	message(FATAL_ERROR "-Conformance exited ${_rc}\n${_stdout}${_stderr}")
endif()
file(READ "${PT_OUT_DIR}/conformance/conformance.json" _conformance)
string(JSON _written GET "${_conformance}" stamp written)
string(JSON _valid GET "${_conformance}" stamp valid)
string(JSON _key GET "${_conformance}" stamp key_sha256)
if(NOT _written OR NOT _valid)
	message(FATAL_ERROR "conformance.json stamp: written ${_written}, valid ${_valid}\n${_conformance}")
endif()
file(GLOB _files "${_stamps}/*.json")
list(LENGTH _files _count)
if(NOT _count EQUAL 1)
	message(FATAL_ERROR "${_count} stamp file(s) in ${_stamps}, expected 1")
endif()
file(READ "${_stamps}/${_key}.json" _stamp)
string(JSON _schema GET "${_stamp}" schema)
string(JSON _stamp_valid GET "${_stamp}" valid)
string(JSON _pending GET "${_stamp}" pending_sign_offs)
string(JSON _gates LENGTH "${_stamp}" gates)
set(_g10b "")
math(EXPR _last "${_gates} - 1")
foreach(_index RANGE 0 ${_last})
	string(JSON _id GET "${_stamp}" gates ${_index} id)
	if(_id STREQUAL "G10b")
		string(JSON _g10b GET "${_stamp}" gates ${_index} result)
	endif()
endforeach()
if(NOT _schema STREQUAL "physicstest.stamp/1" OR NOT _stamp_valid OR NOT _pending EQUAL 0 OR NOT _g10b STREQUAL "pass")
	message(FATAL_ERROR "stamp ${_key}: schema ${_schema}, valid ${_stamp_valid}, pending ${_pending}, G10b '${_g10b}'")
endif()
# 2. A run of the stamped configuration finds the stamp.
execute_process(COMMAND "${PT_BENCH_EXE}" "-Backend=${PT_BACKEND}" -Scenario=S00.FreeFall -Frames=5 -HostCheck=record -PreRoll=0 "-StampDir=${_stamps}"
		"-Out=${PT_OUT_DIR}/run"
	RESULT_VARIABLE _rc OUTPUT_VARIABLE _stdout ERROR_VARIABLE _stderr TIMEOUT 120)
if(NOT _rc EQUAL 0)
	message(FATAL_ERROR "the stamped run exited ${_rc}\n${_stdout}${_stderr}")
endif()
file(READ "${PT_OUT_DIR}/run/manifest.json" _manifest)
string(JSON _run_valid GET "${_manifest}" conformance_stamp valid)
string(JSON _run_key GET "${_manifest}" conformance_stamp key_sha256)
if(NOT _run_valid OR NOT _run_key STREQUAL _key)
	message(FATAL_ERROR "the stamped run: conformance_stamp valid ${_run_valid}, key ${_run_key} (stamp ${_key})")
endif()
# 3. An -ini: switch is another configuration (config_overrides), even when it repeats the Base value: no stamp.
execute_process(COMMAND "${PT_BENCH_EXE}" "-Backend=${PT_BACKEND}" -Scenario=S00.FreeFall -Frames=5 -HostCheck=record -PreRoll=0 "-StampDir=${_stamps}"
		"-ini:PhysicsTest:[/Script/Engine.PhysicsSettings]:DefaultGravityZ=-980.0" "-Out=${PT_OUT_DIR}/override"
	RESULT_VARIABLE _rc OUTPUT_VARIABLE _stdout ERROR_VARIABLE _stderr TIMEOUT 120)
if(NOT _rc EQUAL 0)
	message(FATAL_ERROR "the override run exited ${_rc}\n${_stdout}${_stderr}")
endif()
file(READ "${PT_OUT_DIR}/override/manifest.json" _override)
string(JSON _override_valid GET "${_override}" conformance_stamp valid)
string(JSON _override_key GET "${_override}" conformance_stamp key_sha256)
if(_override_valid OR _override_key STREQUAL _key)
	message(FATAL_ERROR "the override run must not find the stamp: valid ${_override_valid}, key ${_override_key}")
endif()
message(STATUS "PASS: ${PT_BACKEND} stamp ${_key} (G10b ${_g10b}); the stamped run finds it, the -ini: run (key ${_override_key}) does not")
