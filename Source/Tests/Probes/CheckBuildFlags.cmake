# CheckBuildFlags.cmake - CTest PhysicsTest.Probe.BuildFlags (03_WorkPlan M0 criterion 3).
#   cmake -DPT_BUILD_FLAGS=<out/<preset>/BuildFlags.json> -P CheckBuildFlags.cmake
# Fails when DumpBuildFlags.cmake recorded any violation of the flag-profile expectations:
#   Shipped: only Jolt TUs and TUs that compile against Jolt carry /arch:AVX2; PhysX, Bullet and harness have no /arch;
#   every TU is /fp:fast with -MD (Debug: -MDd) and no -MT; PhysX TUs keep /std:c++14; harness TUs are C++20; no /GL;
#   every TU has the same effective _ITERATOR_DEBUG_LEVEL (Debug 2, Release 0), explicit /D "..." defines included.
if(NOT EXISTS "${PT_BUILD_FLAGS}")
	message(FATAL_ERROR "BuildFlags.json not found: '${PT_BUILD_FLAGS}' (build the PtBuildFlags target first)")
endif()
file(READ "${PT_BUILD_FLAGS}" _json)
string(JSON _tu_count GET "${_json}" tu_count)
string(JSON _violations GET "${_json}" violation_count)
string(JSON _physx GET "${_json}" physx_config)
string(JSON _profile GET "${_json}" flag_profile)
string(JSON _config GET "${_json}" configuration)
string(JSON _summary GET "${_json}" summary)
string(JSON _jolt GET "${_json}" jolt_public_defines)
string(REGEX REPLACE "[ \n]+" " " _summary "${_summary}")

message(STATUS "BuildFlags.json: ${_tu_count} TUs, configuration ${_config}, profile ${_profile}, PhysX config ${_physx}")
message(STATUS "Jolt public defines: ${_jolt}")
message(STATUS "Per-group summary: ${_summary}")
if(_violations GREATER 0)
	string(JSON _list GET "${_json}" violations)
	message(FATAL_ERROR "BuildFlags.json lists ${_violations} violation(s):\n${_list}")
endif()
message(STATUS "BuildFlags: PASS (0 violations)")
