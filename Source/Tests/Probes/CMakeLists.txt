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

foreach(_test ThirdPartyIncludes NegativeScaleFatal HarnessCVars FlagLeak BuildFlags RefsClean LongestObjectPath PinGuard)
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
