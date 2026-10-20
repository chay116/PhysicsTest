# PhysicsTestAutomation.exe - doctest unit tests and (from M2) ABI gates. Skeleton in M0 (02_Architecture 1, 4.5).
# Later milestones add PT::Conformance, PT::PhysicsDebugDraw, PT::BackendRegistry and WHOLE_ARCHIVE(PT::Scenarios).
pt_add_program(PhysicsTestAutomation
	SOURCES
		Private/PhysicsTestAutomationMain.cpp
		Private/Tests/CoreMathTests.cpp
		Private/Tests/CoreMiscTests.cpp
	DEPS PT::Core PT::Doctest PT::BuildInfo)
add_test(NAME PhysicsTest.Automation COMMAND PhysicsTestAutomation)
set_tests_properties(PhysicsTest.Automation PROPERTIES LABELS "unit")
