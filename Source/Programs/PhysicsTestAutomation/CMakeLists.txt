# PhysicsTestAutomation.exe - doctest unit tests (M1+) and the ABI gates (M2+) (02_Architecture 1, 4.5, 14.1).
# Links the backend registry (static Null, Jolt from M3) and the AbiTwin test backend; PtBackend_Null.dll is built next to
# the exe for the DLL-path gates. M3 adds every scenario (pt_link_scenarios) and PT::Benchmark; M4 adds PT::PhysicsDebugDraw
# (renderer-free: the DebugDraw area runs in every preset, the test preset included).
# M5: one test file and one CTest area per new SDK backend (BulletBackend, PhysXBackend), so the two never share a file.
# Each file calls its backend's static entry directly, so the backend library is linked here even while it is a stub
# outside the generated registry; PT_<ENGINE>_IN_REGISTRY tells the file which registry state to expect.
set(_pt_automation_backend_tests "")
set(_pt_automation_backend_deps "")
set(_pt_automation_backend_areas "")
if(PT_WITH_BULLET)
	list(APPEND _pt_automation_backend_tests Private/Tests/BulletBackendTests.cpp)
	list(APPEND _pt_automation_backend_deps PT::Backend::Bullet)
	list(APPEND _pt_automation_backend_areas BulletBackend)
	get_target_property(_pt_in_registry PtBackendBullet PT_IN_BACKEND_REGISTRY)
	if(_pt_in_registry)
		set_source_files_properties(Private/Tests/BulletBackendTests.cpp PROPERTIES COMPILE_DEFINITIONS PT_BULLET_IN_REGISTRY=1)
	else()
		set_source_files_properties(Private/Tests/BulletBackendTests.cpp PROPERTIES COMPILE_DEFINITIONS PT_BULLET_IN_REGISTRY=0)
	endif()
endif()
if(PT_WITH_PHYSX)
	list(APPEND _pt_automation_backend_tests Private/Tests/PhysXBackendTests.cpp)
	list(APPEND _pt_automation_backend_deps PT::Backend::PhysX)
	list(APPEND _pt_automation_backend_areas PhysXBackend)
	get_target_property(_pt_in_registry PtBackendPhysX PT_IN_BACKEND_REGISTRY)
	if(_pt_in_registry)
		set_source_files_properties(Private/Tests/PhysXBackendTests.cpp PROPERTIES COMPILE_DEFINITIONS PT_PHYSX_IN_REGISTRY=1)
	else()
		set_source_files_properties(Private/Tests/PhysXBackendTests.cpp PROPERTIES COMPILE_DEFINITIONS PT_PHYSX_IN_REGISTRY=0)
	endif()
endif()

# M6 round 0: the SDK backends this build must gate (GateTests.cpp checks the gate targets against them, so a build that
# drops a backend fails PhysicsTest.Gates.* instead of gating fewer targets).
set(_pt_gate_sdk_backends "")
if(PT_WITH_JOLT)
	list(APPEND _pt_gate_sdk_backends Jolt)
endif()
foreach(_pt_gate_engine Bullet PhysX)
	string(TOUPPER "${_pt_gate_engine}" _pt_gate_engine_upper)
	if(PT_WITH_${_pt_gate_engine_upper})
		get_target_property(_pt_in_registry PtBackend${_pt_gate_engine} PT_IN_BACKEND_REGISTRY)
		if(_pt_in_registry)
			list(APPEND _pt_gate_sdk_backends ${_pt_gate_engine})
		endif()
	endif()
endforeach()
string(REPLACE ";" "," _pt_gate_sdk_backends_text "${_pt_gate_sdk_backends}")
set_property(SOURCE Private/Tests/GateTests.cpp APPEND PROPERTY COMPILE_DEFINITIONS "PT_GATE_SDK_BACKENDS=\"${_pt_gate_sdk_backends_text}\"")

pt_add_program(PhysicsTestAutomation
	SOURCES
		Private/PhysicsTestAutomationMain.cpp
		Private/Tests/AbiGateTests.cpp
		Private/Tests/BackendCommonTests.cpp
		Private/Tests/BenchTests.cpp
		Private/Tests/BodyUtilsTests.cpp
		Private/Tests/CoreConfigTests.cpp
		Private/Tests/CoreMathTests.cpp
		Private/Tests/CoreMiscTests.cpp
		Private/Tests/DebugDrawTests.cpp
		Private/Tests/GateTests.cpp
		Private/Tests/PhysicsBackendTests.cpp
		Private/Tests/PhysicsProfileTests.cpp
		Private/Tests/PhysicsTypesTests.cpp
		Private/Tests/PhysSceneTests.cpp
		Private/Tests/ScenarioTests.cpp
		Private/Tests/SdkBackendTests.cpp
		Private/Tests/StepPlannerTests.cpp
		${_pt_automation_backend_tests}
	DEPS PT::Core PT::PhysicsCore PT::Conformance PT::Benchmark PT::PhysicsDebugDraw PT::BackendRegistry PT::Backend::Null PT::BackendCommon PtBackendAbiTwin PT::Doctest
		PT::BuildInfo PT::Json ${_pt_automation_backend_deps})
pt_link_scenarios(PhysicsTestAutomation)   # the M3 scenes (WHOLE_ARCHIVE)
# The profile tests read the repository's Config/ (overridable with -ConfigDir=).
target_compile_definitions(PhysicsTestAutomation PRIVATE "PT_CONFIG_DIR=\"${CMAKE_SOURCE_DIR}/Config\"")
# Test TUs that include the Kit / BackendCommon headers use their own inline namespace (A.4), distinct from the linked
# backends' (Null, AbiTwin). The map file feeds PhysicsTest.Probe.KitNamespaces.
target_compile_definitions(PhysicsTestAutomation PRIVATE PT_BACKEND_NS=AutomationTest)
target_link_options(PhysicsTestAutomation PRIVATE "/MAP:$<TARGET_FILE_DIR:PhysicsTestAutomation>/PhysicsTestAutomation.map")
add_dependencies(PhysicsTestAutomation PtBackend_Null)

# Everything in one process, as before.
add_test(NAME PhysicsTest.Automation COMMAND PhysicsTestAutomation)
set_tests_properties(PhysicsTest.Automation PROPERTIES LABELS "unit")

# One CTest entry per test area (PhysicsTest.<Area>.*), so a failure names its area. doctest exits 0 when a filter
# matches nothing, so each entry also requires a non-empty, all-passing summary line.
foreach(_area Core Config Types BodyUtils StepPlanner Analytic Profile PhysScene Abi BackendCommon Backend Scenario SdkBackend DebugDraw Bench
		${_pt_automation_backend_areas})
	add_test(NAME PhysicsTest.Unit.${_area} COMMAND PhysicsTestAutomation "-tc=PhysicsTest.${_area}.*")
	set_tests_properties(PhysicsTest.Unit.${_area} PROPERTIES
		LABELS "unit"
		PASS_REGULAR_EXPRESSION "test cases: +[1-9][0-9]* \\| +[1-9][0-9]* passed \\| +0 failed")
endforeach()

# M6: one CTest entry per physics gate (PhysicsTest.Gates.<Gate>: every backend x variant x profile, 02 14.3), plus the
# tolerance-file checks and the gate runner's selection rules (registered skips, G12 alone). The doctest filter names the
# case exactly (G1 must not also run G1b).
foreach(_gate G1 G1b G2 G3 G4 G5 G6 G7 G9 G12 ToleranceFileIsComplete SelectionRules)
	add_test(NAME PhysicsTest.Gates.${_gate} COMMAND PhysicsTestAutomation "-tc=PhysicsTest.Gates.${_gate}")
	set_tests_properties(PhysicsTest.Gates.${_gate} PROPERTIES
		LABELS "unit;gates"
		PASS_REGULAR_EXPRESSION "test cases: +1 \\| +1 passed \\| +0 failed")
endforeach()

# Non-ASCII command line through wmain: the value must arrive as UTF-8 (PhysicsTest.Core.CommandLineUtf8Probe).
add_test(NAME PhysicsTest.Unit.CommandLineUtf8
	COMMAND PhysicsTestAutomation "-tc=PhysicsTest.Core.CommandLineUtf8Probe" "-PtUtf8Probe=한글 경로")
set_tests_properties(PhysicsTest.Unit.CommandLineUtf8 PROPERTIES
	LABELS "unit"
	PASS_REGULAR_EXPRESSION "test cases: +1 \\| +1 passed \\| +0 failed"
	FAIL_REGULAR_EXPRESSION "no -PtUtf8Probe= argument")
