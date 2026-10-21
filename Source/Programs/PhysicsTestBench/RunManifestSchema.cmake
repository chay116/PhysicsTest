# RunManifestSchema.cmake - 03_WorkPlan M7 criterion 1: "M3.BoxPile 5k run의 manifest.json이 Config/Schemas/manifest.schema.json을
# 통과하고, mode, instrumentation, timing_valid_for_headline, dt_engine_s, threads, platform(전원 계획 포함) 필드가 모두 채워진다".
# One M3.BoxPile 5k run (PT_FRAMES frames) on PT_BACKEND; Tools/validate_manifest.py (jsonschema, Draft 2020-12, the
# repository .venv with python -I) validates manifest.json and exit.json with --require-fields. The run is shorter than the
# scene's windows, so its manifest also shows the clipped headline window and the empty settled window (null statistics).
# M7 fix round 0: the manifests of two refused runs validate too (ResultsSchema 1: every run that initialised its backend
# writes a physicstest.run/2 manifest) - the engine-option preflight (no scene: scene.built false) and a missing required
# key. Negative controls, each must FAIL: run.mode = "Fast", platform.power_scheme removed, platform.power_scheme_guid = "x",
# an empty window with a 0 ns mean instead of null statistics, and (M8) timing_valid_for_headline true while reasons remain or
# false without any reason (the schema's headline consistency rule).
#   cmake -DPT_BENCH_EXE=<exe> -DPT_OUT_DIR=<dir> -DPT_PYTHON=<python> -DPT_VALIDATOR=<validate_manifest.py> -DPT_BACKEND=Jolt
#         [-DPT_FRAMES=30] -P RunManifestSchema.cmake
# Second mode (M7 fix round 0, PhysicsTest.Bench.ManifestsValidate): -DPT_VALIDATE_DIRS=<dir>|<dir>|... validates every
# manifest.json and exit.json below those directories (validate_manifest.py --recursive --require-fields, the ManifestSchema
# test's broken copies excluded) - what the other bench tests wrote, refused and failed runs included.
cmake_minimum_required(VERSION 3.25)
if(DEFINED PT_VALIDATE_DIRS)
	string(REPLACE "|" ";" _dirs "${PT_VALIDATE_DIRS}")
	execute_process(COMMAND "${PT_PYTHON}" -I "${PT_VALIDATOR}" --recursive --require-fields "--exclude=/ManifestSchema/bad_" ${_dirs}
		RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
	message(STATUS "${_out}${_err}")
	if(NOT _rc EQUAL 0)
		message(FATAL_ERROR "bench outputs do not validate (rc ${_rc})")
	endif()
	return()
endif()
if(NOT PT_FRAMES)
	set(PT_FRAMES 30)
endif()
file(REMOVE_RECURSE "${PT_OUT_DIR}")
execute_process(COMMAND "${PT_BENCH_EXE}" "-Backend=${PT_BACKEND}" -Scenario=M3.BoxPile -Param:N=5000 "-Frames=${PT_FRAMES}" -HostCheck=record -PreRoll=0
		"-Out=${PT_OUT_DIR}/run"
	RESULT_VARIABLE _rc OUTPUT_VARIABLE _stdout ERROR_VARIABLE _stderr TIMEOUT 600)
if(NOT _rc EQUAL 0)
	message(FATAL_ERROR "the M3.BoxPile 5k run exited ${_rc}\n${_stdout}${_stderr}")
endif()
# The refused runs (exit 2 both; their manifests must still be complete run/2 documents).
execute_process(COMMAND "${PT_BENCH_EXE}" "-Backend=${PT_BACKEND}" -Scenario=S00.FreeFall "-ini:PhysicsTest:[PhysicsTest.Backend.${PT_BACKEND}]:NoSuchKey=1"
		"-Out=${PT_OUT_DIR}/refused_options"
	RESULT_VARIABLE _rc_options OUTPUT_VARIABLE _stdout ERROR_VARIABLE _stderr TIMEOUT 120)
execute_process(COMMAND "${PT_BENCH_EXE}" "-Backend=${PT_BACKEND}" -Scenario=S00.FreeFall -RequireEffectiveKey=no.such.key "-Out=${PT_OUT_DIR}/refused_key"
	RESULT_VARIABLE _rc_key OUTPUT_VARIABLE _stdout2 ERROR_VARIABLE _stderr2 TIMEOUT 120)
if(NOT _rc_options EQUAL 2 OR NOT _rc_key EQUAL 2)
	message(FATAL_ERROR "the refused runs exited ${_rc_options} / ${_rc_key}, expected 2 / 2\n${_stdout}${_stderr}${_stdout2}${_stderr2}")
endif()
execute_process(COMMAND "${PT_PYTHON}" -I "${PT_VALIDATOR}" --require-fields "${PT_OUT_DIR}/run" "${PT_OUT_DIR}/refused_options" "${PT_OUT_DIR}/refused_key"
	RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
message(STATUS "${_out}${_err}")
if(NOT _rc EQUAL 0)
	message(FATAL_ERROR "manifest.json / exit.json do not validate (rc ${_rc})")
endif()
file(READ "${PT_OUT_DIR}/run/manifest.json" _manifest)
# The short run clips the windows (docs/ResultsSchema.md 2): headline 1..PT_FRAMES-1, settled (300-599) empty with null statistics.
math(EXPR _last "${PT_FRAMES} - 1")
string(JSON _headline_last GET "${_manifest}" summary windows headline last)
string(JSON _headline_clipped GET "${_manifest}" summary windows headline clipped)
string(JSON _settled_count GET "${_manifest}" summary windows settled count)
string(JSON _settled_type TYPE "${_manifest}" summary windows settled frame_ns)
if(NOT _headline_last EQUAL _last OR NOT _headline_clipped STREQUAL "ON" OR NOT _settled_count EQUAL 0 OR NOT _settled_type STREQUAL "NULL")
	message(FATAL_ERROR "windows: headline last ${_headline_last} clipped ${_headline_clipped}, settled count ${_settled_count} frame_ns ${_settled_type}")
endif()
file(READ "${PT_OUT_DIR}/refused_options/manifest.json" _refused)
string(JSON _built GET "${_refused}" scene built)
string(JSON _desc_type TYPE "${_refused}" scene desc_sha256)
if(NOT _built STREQUAL "OFF" OR NOT _desc_type STREQUAL "NULL")
	message(FATAL_ERROR "the engine-option refusal's manifest: scene.built ${_built}, desc_sha256 ${_desc_type}")
endif()
# Negative controls.
string(JSON _bad_mode SET "${_manifest}" run mode "\"Fast\"")
string(JSON _bad_power REMOVE "${_manifest}" platform power_scheme)
string(JSON _bad_power_guid SET "${_manifest}" platform power_scheme_guid "\"x\"")
string(JSON _zero_stats GET "${_manifest}" summary windows headline frame_ns)
string(JSON _zero_stats SET "${_zero_stats}" mean 0)
string(JSON _bad_window SET "${_manifest}" summary windows settled frame_ns "${_zero_stats}")
# M8 (M7 review): the headline flag must agree with its reasons - true with reasons left, and false without any reason.
string(JSON _bad_headline SET "${_manifest}" timing_valid_for_headline true)
string(JSON _bad_no_reason SET "${_manifest}" timing_invalid_reasons "[]")
foreach(_bad bad_mode bad_power bad_power_guid bad_window bad_headline bad_no_reason)
	file(MAKE_DIRECTORY "${PT_OUT_DIR}/${_bad}")
	file(WRITE "${PT_OUT_DIR}/${_bad}/manifest.json" "${_${_bad}}")
	execute_process(COMMAND "${PT_PYTHON}" -I "${PT_VALIDATOR}" --require-fields "${PT_OUT_DIR}/${_bad}"
		RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
	message(STATUS "${_bad}: ${_out}${_err}")
	if(NOT _rc EQUAL 1)
		message(FATAL_ERROR "the broken copy ${_bad} validated (rc ${_rc}): the schema must reject it")
	endif()
endforeach()
string(JSON _schema GET "${_manifest}" schema)
string(JSON _mode GET "${_manifest}" run mode)
string(JSON _dt GET "${_manifest}" run dt_engine_s)
string(JSON _mask GET "${_manifest}" threads affinity_mask)
string(JSON _series GET "${_manifest}" threads series)
string(JSON _power GET "${_manifest}" platform power_scheme_guid)
string(JSON _valid GET "${_manifest}" timing_valid_for_headline)
message(STATUS "PASS: ${_schema} M3.BoxPile 5k (${PT_BACKEND}) validates: mode ${_mode}, dt_engine_s ${_dt}, affinity ${_mask} (${_series}), power scheme ${_power}, "
	"timing_valid_for_headline ${_valid}; headline window clipped at ${_last}, settled empty (null statistics); the two refused runs' manifests validate; "
	"the six broken copies (mode, power_scheme, power_scheme_guid, a 0 ns empty window, headline true with reasons, false without one) are rejected")
