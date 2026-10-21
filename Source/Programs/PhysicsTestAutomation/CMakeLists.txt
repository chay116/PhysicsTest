# PhysicsTestAutomation.exe - doctest unit tests (M1+) and the ABI gates (M2+) (02_Architecture 1, 4.5, 14.1).
# Links the backend registry (static Null) and the AbiTwin test backend; PtBackend_Null.dll is built next to the exe for
# the DLL-path gates. Later milestones add PT::PhysicsDebugDraw and WHOLE_ARCHIVE(PT::Scenarios).
pt_add_program(PhysicsTestAutomation
	SOURCES
		Private/PhysicsTestAutomationMain.cpp
		Private/Tests/AbiGateTests.cpp
		Private/Tests/BackendCommonTests.cpp
		Private/Tests/BodyUtilsTests.cpp
		Private/Tests/CoreConfigTests.cpp
		Private/Tests/CoreMathTests.cpp
		Private/Tests/CoreMiscTests.cpp
		Private/Tests/PhysicsBackendTests.cpp
		Private/Tests/PhysicsProfileTests.cpp
		Private/Tests/PhysicsTypesTests.cpp
		Private/Tests/PhysSceneTests.cpp
		Private/Tests/StepPlannerTests.cpp
	DEPS PT::Core PT::PhysicsCore PT::Conformance PT::BackendRegistry PT::Backend::Null PT::BackendCommon PtBackendAbiTwin PT::Doctest PT::BuildInfo)
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
foreach(_area Core Config Types BodyUtils StepPlanner Analytic Profile PhysScene Abi BackendCommon Backend)
	add_test(NAME PhysicsTest.Unit.${_area} COMMAND PhysicsTestAutomation "-tc=PhysicsTest.${_area}.*")
	set_tests_properties(PhysicsTest.Unit.${_area} PROPERTIES
		LABELS "unit"
		PASS_REGULAR_EXPRESSION "test cases: +[1-9][0-9]* \\| +[1-9][0-9]* passed \\| +0 failed")
endforeach()

# Non-ASCII command line through wmain: the value must arrive as UTF-8 (PhysicsTest.Core.CommandLineUtf8Probe).
add_test(NAME PhysicsTest.Unit.CommandLineUtf8
	COMMAND PhysicsTestAutomation "-tc=PhysicsTest.Core.CommandLineUtf8Probe" "-PtUtf8Probe=한글 경로")
set_tests_properties(PhysicsTest.Unit.CommandLineUtf8 PROPERTIES
	LABELS "unit"
	PASS_REGULAR_EXPRESSION "test cases: +1 \\| +1 passed \\| +0 failed"
	FAIL_REGULAR_EXPRESSION "no -PtUtf8Probe= argument")
