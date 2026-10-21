# PTPython.cmake - the Python interpreter of the harness tests (M7 schema tests, M8 Tools/ptbench) and its probes.
#
# PT_PYTHON is the repository .venv's interpreter (02_Architecture 4.7: the pinned packages of Tools/ptbench/requirements.lock,
# run with python -I), else the system one. At configure time two probes record what that interpreter can import:
#   PT_PYTHON_HAS_JSONSCHEMA  jsonschema (Tools/validate_manifest.py)
#   PT_PYTHON_HAS_PTBENCH     the ptbench package and its analysis dependencies (pip install -e Tools/ptbench, M8)
# pt_add_python_test() registers a test that needs one of them; without it the test is still registered but SKIPPED with a
# message that says how to install (M7 review: a system Python without jsonschema must not fail silently or pass by
# accident). Re-run the configure step after installing the packages.
include_guard(GLOBAL)

if(EXISTS "${CMAKE_SOURCE_DIR}/.venv/Scripts/python.exe")
	set(PT_PYTHON "${CMAKE_SOURCE_DIR}/.venv/Scripts/python.exe")
else()
	find_package(Python3 COMPONENTS Interpreter QUIET)
	set(PT_PYTHON "${Python3_EXECUTABLE}")
endif()

function(_pt_python_probe OutVar Code)
	set(_ok OFF)
	if(PT_PYTHON)
		execute_process(COMMAND "${PT_PYTHON}" -I -c "${Code}" RESULT_VARIABLE _rc OUTPUT_QUIET ERROR_QUIET TIMEOUT 120)
		if(_rc EQUAL 0)
			set(_ok ON)
		endif()
	endif()
	set(${OutVar} ${_ok} PARENT_SCOPE)
endfunction()

_pt_python_probe(PT_PYTHON_HAS_JSONSCHEMA "import jsonschema")
_pt_python_probe(PT_PYTHON_HAS_PTBENCH "import ptbench, ptbench.analysis.report, jsonschema, numpy, pandas, pyarrow, matplotlib")
if(NOT PT_PYTHON)
	message(STATUS "PT_PYTHON: none found - the Python tests are registered as skipped")
else()
	message(STATUS "PT_PYTHON: ${PT_PYTHON} (jsonschema ${PT_PYTHON_HAS_JSONSCHEMA}, ptbench ${PT_PYTHON_HAS_PTBENCH})")
endif()

# pt_python_has(<jsonschema|ptbench> <out var> <out hint>): whether PT_PYTHON can import the requirement, and the
# install hint for the skip message.
function(pt_python_has Requires OutHave OutHint)
	if(Requires STREQUAL "jsonschema")
		set(_have ${PT_PYTHON_HAS_JSONSCHEMA})
		set(_hint "jsonschema is not importable by '${PT_PYTHON}' (python -I): create the repository .venv and pip install -r Tools/ptbench/requirements.lock, then re-configure")
	elseif(Requires STREQUAL "ptbench")
		set(_have ${PT_PYTHON_HAS_PTBENCH})
		set(_hint "ptbench is not importable by '${PT_PYTHON}' (python -I): in the repository .venv pip install -r Tools/ptbench/requirements.lock -r Tools/ptbench/requirements-build.txt, then pip install --no-build-isolation --no-deps -e Tools/ptbench, then re-configure")
	else()
		message(FATAL_ERROR "pt_python_has: unknown requirement '${Requires}'")
	endif()
	if(NOT PT_PYTHON)
		set(_have OFF)
	endif()
	set(${OutHave} ${_have} PARENT_SCOPE)
	set(${OutHint} "${_hint}" PARENT_SCOPE)
endfunction()

# pt_add_skipped_python_test(<name> <hint>): a test that only reports why it cannot run (CTest "Skipped").
function(pt_add_skipped_python_test Name Hint)
	add_test(NAME ${Name} COMMAND "${CMAKE_COMMAND}" -E echo "PT_SKIP: ${Hint}")
	set_tests_properties(${Name} PROPERTIES SKIP_REGULAR_EXPRESSION "PT_SKIP:")
endfunction()

# pt_add_python_test(<name> REQUIRES jsonschema|ptbench COMMAND <args...> [LABELS ...] [ENVIRONMENT ...])
# COMMAND starts after the interpreter (python -I is prepended). A missing requirement registers a test that prints
# "PT_SKIP: ..." and is reported as Skipped (SKIP_REGULAR_EXPRESSION).
function(pt_add_python_test Name)
	cmake_parse_arguments(PARSE_ARGV 1 Arg "" "REQUIRES" "COMMAND;LABELS;ENVIRONMENT")
	pt_python_has("${Arg_REQUIRES}" _have _hint)
	if(_have)
		add_test(NAME ${Name} COMMAND "${PT_PYTHON}" -I ${Arg_COMMAND})
	else()
		pt_add_skipped_python_test(${Name} "${_hint}")
	endif()
	if(Arg_LABELS)
		set_tests_properties(${Name} PROPERTIES LABELS "${Arg_LABELS}")
	endif()
	if(Arg_ENVIRONMENT)
		set_tests_properties(${Name} PROPERTIES ENVIRONMENT "${Arg_ENVIRONMENT}")
	endif()
endfunction()
