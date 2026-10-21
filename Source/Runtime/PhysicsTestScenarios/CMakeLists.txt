# PT::Scenarios - scenarios and their static registry (02_Architecture 4.5, A.12, docs/Scenes.md). M3: S00.FreeFall,
# M1.SingleBoxDrop, M3.BoxPile and the free-fall fixture (moved here from PT::Conformance so that Conformance -> Scenarios
# has no cycle). M6 adds the gate scenes S00d and S01-S08 (Private/Scenarios/Gates, constants in Public/GateScenes.h).
# Programs link it with WHOLE_ARCHIVE (pt_link_scenarios): the IMPLEMENT_PHYSICS_TEST_SCENARIO registrars
# are not referenced otherwise.
pt_add_module(PtScenarios STATIC ALIAS PT::Scenarios
	SOURCES
		Public/PhysicsTestScenario.h
		Public/Fixtures/FreeFallFixture.h
		Public/GateScenes.h
		Private/PhysicsTestScenario.cpp
		Private/Fixtures/FreeFallFixture.cpp
		Private/Scenarios/ScenarioHelpers.h
		Private/Scenarios/Gates/S00_FreeFall.cpp
		Private/Scenarios/Gates/S00d_FreeFallDamped.cpp
		Private/Scenarios/Gates/S01_UnitImpulse.cpp
		Private/Scenarios/Gates/S02_SpinInertia.cpp
		Private/Scenarios/Gates/S03_CapsuleRest.cpp
		Private/Scenarios/Gates/S04_ContactNormal.cpp
		Private/Scenarios/Gates/S05_RestitutionCombine.cpp
		Private/Scenarios/Gates/S06_FrictionSlide.cpp
		Private/Scenarios/Gates/S07_ClampProbe.cpp
		Private/Scenarios/Gates/S08_StartAsleep.cpp
		Private/Scenarios/Behavior/M1_SingleBoxDrop.cpp
		Private/Scenarios/Perf/M3_BoxPile.cpp
	PUBLIC_DEPS PT::PhysicsCore)
target_include_directories(PtScenarios PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Private/Scenarios")

# pt_link_scenarios(<target>): every scenario registrar reaches <target>. The library is linked normally (PT::Conformance
# also links it, and CMake's LINK_LIBRARY:WHOLE_ARCHIVE feature refuses one library with two features) and MSVC's
# /WHOLEARCHIVE:<file name> keeps all of its objects.
function(pt_link_scenarios Target)
	target_link_libraries(${Target} PRIVATE PT::Scenarios)
	target_link_options(${Target} PRIVATE "/WHOLEARCHIVE:$<TARGET_FILE_NAME:PtScenarios>")
endfunction()
