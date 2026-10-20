# CheckHarnessCVars.cmake - CTest driver of PhysicsTest.Probe.HarnessCVars (M1 fix round).
#   cmake -DPT_PROBE_EXE=<PtProbeHarnessCVars.exe> -DPT_PROBE_MAP=<its link map> -DPT_AUTOMATION_EXE=<PhysicsTestAutomation.exe>
#         -P CheckHarnessCVars.cmake
# PASS when
#   1 the probe exits 0 (harness physics CVars registered and enforced without BodyUtils, see HarnessCVarsProbe.cpp),
#   2 its link map has PhysicsProfile and PhysicsConsoleVariables but none of BodyUtils / BodyInstance / PhysScene
#     (otherwise the probe would not test the dead-stripped case at all),
#   3 the probe and PhysicsTestAutomation print the same physics_sha256 for UEParity and Normalized (the hash must not
#     depend on which other console variables a program registers).
foreach(_req PT_PROBE_EXE PT_PROBE_MAP PT_AUTOMATION_EXE)
	if(NOT DEFINED ${_req})
		message(FATAL_ERROR "CheckHarnessCVars.cmake: -D${_req}=... is required")
	endif()
endforeach()

# 1
execute_process(COMMAND "${PT_PROBE_EXE}" OUTPUT_VARIABLE _probe_out ERROR_VARIABLE _probe_err RESULT_VARIABLE _rc)
message(STATUS "probe exit code ${_rc}\n${_probe_out}${_probe_err}")
if(NOT _rc EQUAL 0)
	message(FATAL_ERROR "CheckHarnessCVars: the probe failed (exit code ${_rc})")
endif()

# 2
if(NOT EXISTS "${PT_PROBE_MAP}")
	message(FATAL_ERROR "CheckHarnessCVars: link map '${PT_PROBE_MAP}' does not exist")
endif()
file(READ "${PT_PROBE_MAP}" _map)
foreach(_needed PhysicsProfile.cpp.obj PhysicsConsoleVariables.cpp.obj)
	string(FIND "${_map}" "${_needed}" _at)
	if(_at EQUAL -1)
		message(FATAL_ERROR "CheckHarnessCVars: '${_needed}' is not in the link map (unexpected map format or target)")
	endif()
endforeach()
foreach(_absent BodyUtils.cpp.obj BodyInstance.cpp.obj PhysScene.cpp.obj)
	string(FIND "${_map}" "${_absent}" _at)
	if(NOT _at EQUAL -1)
		message(FATAL_ERROR "CheckHarnessCVars: '${_absent}' is linked into the probe, so it no longer tests a program without it")
	endif()
endforeach()

# 3
execute_process(COMMAND "${PT_AUTOMATION_EXE}" "-tc=PhysicsTest.Profile.ShippedFilesLoad" "-PtPrintPhysicsSha256"
	OUTPUT_VARIABLE _auto_out ERROR_VARIABLE _auto_err RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
	message(FATAL_ERROR "CheckHarnessCVars: PhysicsTestAutomation failed (exit code ${_rc})\n${_auto_out}${_auto_err}")
endif()
foreach(_profile UEParity Normalized)
	string(REGEX MATCH "physics_sha256 ${_profile}=([0-9a-f]+)" _m "${_probe_out}")
	set(_probe_hash "${CMAKE_MATCH_1}")
	string(REGEX MATCH "physics_sha256 ${_profile}=([0-9a-f]+)" _m "${_auto_out}")
	set(_auto_hash "${CMAKE_MATCH_1}")
	string(LENGTH "${_probe_hash}" _len)
	if(NOT _len EQUAL 64 OR NOT "${_probe_hash}" STREQUAL "${_auto_hash}")
		message(FATAL_ERROR "CheckHarnessCVars: physics_sha256 of ${_profile} differs: probe '${_probe_hash}', PhysicsTestAutomation '${_auto_hash}'")
	endif()
	message(STATUS "physics_sha256 ${_profile} = ${_probe_hash} in both programs")
endforeach()
message(STATUS "CheckHarnessCVars: PASS")
