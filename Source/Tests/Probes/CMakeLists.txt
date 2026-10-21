# CTest probes for the M0 scaffold (03_WorkPlan M0 task 6, 02_Architecture 4.5). Labels: probe.
#   PhysicsTest.Probe.ThirdPartyIncludes  Core -> wrapper -> doctest / ImGui / ImPlot / nlohmann-json   (criterion 9)
#   PhysicsTest.Probe.FlagLeak            static lib linking Jolt PRIVATE leaks no /arch or JPH_*      (criterion 5)
#   PhysicsTest.Probe.BuildFlags          BuildFlags.json has no flag-profile violation                (criterion 3)
#   PhysicsTest.Probe.RefsClean           reference checkouts untouched, no PxConfig.h/.dxil/.spv       (criterion 2)
#   PhysicsTest.Probe.LongestObjectPath   every object path < 240 characters                           (criterion 8)
#   PhysicsTest.Probe.PinGuard            wrong SDK tag / mirror or external source dir / wrong HEAD
#                                         stop configure                                               (criterion 8)
#   PhysicsTest.Probe.NegativeScaleFatal  FTransform composition with negative scale stops every build (M0 fix round)
#   PhysicsTest.Probe.HarnessCVars        a profile-only program (no BodyUtils / BodyInstance / PhysScene objects) has
#                                         the harness physics CVars and hashes profiles like PhysicsTestAutomation
#                                         (M1 fix round)
#   PhysicsTest.Probe.CheckModules        A9: PtBackend_Null.dll exports only PT_GetBackendApi and imports only the VC
#                                         runtime, the UCRT and KERNEL32 (Tools/check_modules.py --strict-crt: no
#                                         msvcp140, the Chaos template uses no C++ library DLL)                   (M2)
#   PhysicsTest.Probe.KitNamespaces       the Automation linker map splits Kit symbols by backend inline namespace (M2)
#   PhysicsTest.Probe.BackendIsolation    backend TUs have no harness Core / PhysicsCore include path, define
#                                         PT_BACKEND_NS (M2)
#   PhysicsTest.Probe.KitNeedsBackendNs   a TU including the Kit without PT_BACKEND_NS stops with #error: compiled
#                                         directly with its compile_commands.json line (no nested build)          (M2)

pt_add_program(PtProbeThirdPartyIncludes
	SOURCES ThirdPartyIncludesProbe.cpp
	DEPS PT::Core PT::Doctest PT::ImGuiHeaders PT::Json
	FOLDER "PhysicsTest/Tests")
add_test(NAME PhysicsTest.Probe.ThirdPartyIncludes COMMAND PtProbeThirdPartyIncludes)

pt_add_program(PtProbeNegativeScaleFatal SOURCES NegativeScaleFatalProbe.cpp DEPS PT::Core FOLDER "PhysicsTest/Tests")
pt_add_script_test(PhysicsTest.Probe.NegativeScaleFatal
	SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/CheckFatalExit.cmake"
	ARGS "-DPT_EXE=$<TARGET_FILE:PtProbeNegativeScaleFatal>" -DPT_EXPECT_RC=3
		"-DPT_EXPECT_REGEX=LogCore: Fatal: FTransform::Multiply: negative scale composition")

# Links PT::PhysicsCore for the resolver only. /MAP lets the driver prove that BodyUtils.cpp.obj is not linked.
pt_add_program(PtProbeHarnessCVars SOURCES HarnessCVarsProbe.cpp DEPS PT::Core PT::PhysicsCore FOLDER "PhysicsTest/Tests")
target_compile_definitions(PtProbeHarnessCVars PRIVATE "PT_CONFIG_DIR=\"${CMAKE_SOURCE_DIR}/Config\"")
target_link_options(PtProbeHarnessCVars PRIVATE "/MAP:$<TARGET_FILE_DIR:PtProbeHarnessCVars>/PtProbeHarnessCVars.map")
pt_add_script_test(PhysicsTest.Probe.HarnessCVars
	SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/CheckHarnessCVars.cmake"
	ARGS "-DPT_PROBE_EXE=$<TARGET_FILE:PtProbeHarnessCVars>" "-DPT_PROBE_MAP=$<TARGET_FILE_DIR:PtProbeHarnessCVars>/PtProbeHarnessCVars.map"
		"-DPT_AUTOMATION_EXE=$<TARGET_FILE:PhysicsTestAutomation>")

if(PT_WITH_JOLT)
	add_library(PtProbeJoltSeam STATIC JoltSeam/PtJoltSeam.cpp JoltSeam/PtJoltSeam.h)
	target_include_directories(PtProbeJoltSeam PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/JoltSeam")
	target_link_libraries(PtProbeJoltSeam PRIVATE PT::Jolt PT::BackendCommon)   # PRIVATE, like PT::Backend::Jolt
	pt_target_defaults(PtProbeJoltSeam)
	set_property(TARGET PtProbeJoltSeam PROPERTY FOLDER "PhysicsTest/Tests")
	pt_add_program(PtProbeJoltSeamConsumer SOURCES JoltSeam/SeamConsumer.cpp DEPS PtProbeJoltSeam FOLDER "PhysicsTest/Tests")
	add_test(NAME PhysicsTest.Probe.FlagLeak COMMAND PtProbeJoltSeamConsumer)
endif()

pt_add_script_test(PhysicsTest.Probe.BuildFlags
	SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/CheckBuildFlags.cmake"
	ARGS "-DPT_BUILD_FLAGS=${CMAKE_BINARY_DIR}/BuildFlags.json")
pt_add_script_test(PhysicsTest.Probe.RefsClean
	SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/CheckRefsClean.cmake"
	ARGS "-DPT_SDK_MIRROR=${PT_SDK_MIRROR}" "-DPT_SOURCE_DIR=${CMAKE_SOURCE_DIR}" "-DGIT_EXECUTABLE=${GIT_EXECUTABLE}")
pt_add_script_test(PhysicsTest.Probe.LongestObjectPath
	SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/CheckLongestPath.cmake"
	ARGS "-DPT_BINARY_DIR=${CMAKE_BINARY_DIR}" -DPT_LIMIT=240)
pt_add_script_test(PhysicsTest.Probe.PinGuard
	SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/PinGuardProbe.cmake"
	ARGS "-DPT_SOURCE_DIR=${CMAKE_SOURCE_DIR}" "-DPT_BINARY_DIR=${CMAKE_BINARY_DIR}" "-DPT_PROBE_DIR=${CMAKE_BINARY_DIR}/_probe"
		"-DPT_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}" "-DPT_C_COMPILER=${CMAKE_C_COMPILER}" "-DPT_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
		"-DPT_SDK_MIRROR=${PT_SDK_MIRROR}" "-DGIT_EXECUTABLE=${GIT_EXECUTABLE}"
	LABELS "configure")

# ---- M2: backend boundary probes ----------------------------------------------------------------------------------
# A9 DLL hygiene. Python from the repository .venv when present (python -I), else the system interpreter; dumpbin from
# the MSVC toolset that links this build.
if(EXISTS "${CMAKE_SOURCE_DIR}/.venv/Scripts/python.exe")
	set(PT_PYTHON "${CMAKE_SOURCE_DIR}/.venv/Scripts/python.exe")
else()
	find_package(Python3 COMPONENTS Interpreter QUIET)
	set(PT_PYTHON "${Python3_EXECUTABLE}")
endif()
get_filename_component(_pt_msvc_bin "${CMAKE_LINKER}" DIRECTORY)
if(PT_PYTHON AND EXISTS "${_pt_msvc_bin}/dumpbin.exe")
	add_test(NAME PhysicsTest.Probe.CheckModules
		COMMAND "${PT_PYTHON}" -I "${CMAKE_SOURCE_DIR}/Tools/check_modules.py" "$<TARGET_FILE:PtBackend_Null>" --strict-crt --dumpbin "${_pt_msvc_bin}/dumpbin.exe")
	set_tests_properties(PhysicsTest.Probe.CheckModules PROPERTIES PASS_REGULAR_EXPRESSION "PASS PtBackend_Null.dll" FAIL_REGULAR_EXPRESSION "FAIL ")
else()
	message(WARNING "PhysicsTest.Probe.CheckModules disabled: python (${PT_PYTHON}) or dumpbin (${_pt_msvc_bin}) not found")
endif()

pt_add_script_test(PhysicsTest.Probe.KitNamespaces
	SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/CheckKitNamespaces.cmake"
	ARGS "-DPT_MAP=$<TARGET_FILE_DIR:PhysicsTestAutomation>/PhysicsTestAutomation.map" "-DPT_ALLOWED=Null$<SEMICOLON>AbiTwin$<SEMICOLON>AutomationTest"
		"-DPT_REQUIRED=Null$<SEMICOLON>AbiTwin")
pt_add_script_test(PhysicsTest.Probe.BackendIsolation
	SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/CheckBackendIsolation.cmake"
	ARGS "-DPT_COMPILE_COMMANDS=${CMAKE_BINARY_DIR}/compile_commands.json" "-DPT_SOURCE_DIR=${CMAKE_SOURCE_DIR}")

# Must not compile (EXCLUDE_FROM_ALL: never built). The target only puts the TU, with a backend's flags but without
# PT_BACKEND_NS, into compile_commands.json; the probe script runs that compiler line itself (output in _probe/, no
# nested build of this tree) and expects the Kit's #error.
add_library(PtProbeKitWithoutBackendNs OBJECT EXCLUDE_FROM_ALL KitWithoutBackendNsProbe.cpp)
target_link_libraries(PtProbeKitWithoutBackendNs PRIVATE PT::Abi)
pt_target_defaults(PtProbeKitWithoutBackendNs)
set_property(TARGET PtProbeKitWithoutBackendNs PROPERTY FOLDER "PhysicsTest/Tests")
pt_add_script_test(PhysicsTest.Probe.KitNeedsBackendNs
	SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/CheckKitNeedsBackendNs.cmake"
	ARGS "-DPT_COMPILE_COMMANDS=${CMAKE_BINARY_DIR}/compile_commands.json" "-DPT_PROBE_DIR=${CMAKE_BINARY_DIR}/_probe/KitNeedsBackendNs")

foreach(_test ThirdPartyIncludes NegativeScaleFatal HarnessCVars FlagLeak BuildFlags RefsClean LongestObjectPath PinGuard CheckModules KitNamespaces
		BackendIsolation KitNeedsBackendNs)
	if(TEST PhysicsTest.Probe.${_test})
		get_test_property(PhysicsTest.Probe.${_test} LABELS _labels)
		if(_labels)
			set(_labels "probe;${_labels}")
		else()
			set(_labels "probe")
		endif()
		set_tests_properties(PhysicsTest.Probe.${_test} PROPERTIES LABELS "${_labels}")
	endif()
endforeach()
