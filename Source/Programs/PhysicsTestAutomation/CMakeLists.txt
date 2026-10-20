# PhysicsTestAutomation.exe - doctest unit tests (M1+) and, from M2, ABI gates (02_Architecture 1, 4.5, 14.1).
# Later milestones add PT::Conformance, PT::PhysicsDebugDraw, PT::BackendRegistry and WHOLE_ARCHIVE(PT::Scenarios).
pt_add_program(PhysicsTestAutomation
	SOURCES
		Private/PhysicsTestAutomationMain.cpp
		Private/Tests/BodyUtilsTests.cpp
		Private/Tests/CoreConfigTests.cpp
		Private/Tests/CoreMathTests.cpp
		Private/Tests/CoreMiscTests.cpp
		Private/Tests/PhysicsProfileTests.cpp
		Private/Tests/PhysicsTypesTests.cpp
		Private/Tests/PhysSceneTests.cpp
		Private/Tests/StepPlannerTests.cpp
	DEPS PT::Core PT::PhysicsCore PT::Doctest PT::BuildInfo)
# The profile tests read the repository's Config/ (overridable with -ConfigDir=).
target_compile_definitions(PhysicsTestAutomation PRIVATE "PT_CONFIG_DIR=\"${CMAKE_SOURCE_DIR}/Config\"")

# Everything in one process, as before.
add_test(NAME PhysicsTest.Automation COMMAND PhysicsTestAutomation)
set_tests_properties(PhysicsTest.Automation PROPERTIES LABELS "unit")

# One CTest entry per test area (PhysicsTest.<Area>.*), so a failure names its area. doctest exits 0 when a filter
# matches nothing, so each entry also requires a non-empty, all-passing summary line.
foreach(_area Core Config Types BodyUtils StepPlanner Analytic Profile PhysScene)
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
