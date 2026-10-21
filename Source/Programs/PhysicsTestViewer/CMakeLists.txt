# PhysicsTestViewer.exe - the viewer, phase 1 (02_Architecture 4.5, 11.2; 03_WorkPlan M4): PT::Editor + the backend
# registry + every scenario. Win32 + D3D11 + Dear ImGui (docking); -Headless renders offscreen for automated checks.
# PhysicsTestViewerTests.exe - the viewer-side doctest cases (PhysicsTest.Viewer.*: camera, session, offscreen renderer).
# Only with PT_WITH_VIEWER (dbg, dev): the test preset and the bench exe never link D3D.
pt_add_program(PhysicsTestViewer
	SOURCES
		Private/PhysicsTestViewerApp.cpp
		Private/PhysicsTestViewerApp.h
		Private/PhysicsTestViewerMain.cpp
	DEPS PT::Core PT::Editor PT::BackendRegistry PT::BuildInfo PT::ImGui PT::Json)
pt_link_scenarios(PhysicsTestViewer)
target_compile_definitions(PhysicsTestViewer PRIVATE "PT_SOURCE_DIR=\"${CMAKE_SOURCE_DIR}\"")
add_dependencies(PhysicsTestViewer PtBackend_Null)   # NullDll is listed when the DLL sits next to the exe

pt_add_program(PhysicsTestViewerTests
	SOURCES
		Tests/ViewerTests.cpp
		Tests/ViewerTestsMain.cpp
	DEPS PT::Core PT::Editor PT::BackendRegistry PT::BuildInfo PT::Doctest)
pt_link_scenarios(PhysicsTestViewerTests)

# ODR / flag isolation (docs/Abi.md 5.13, DL46): both programs link the /arch:AVX2 Jolt adapter objects together with the
# editor, renderer and ImGui TUs, so their linker maps join PhysicsTest.Probe.AdapterComdats (Tests/Probes).
target_link_options(PhysicsTestViewer PRIVATE "/MAP:$<TARGET_FILE_DIR:PhysicsTestViewer>/PhysicsTestViewer.map")
target_link_options(PhysicsTestViewerTests PRIVATE "/MAP:$<TARGET_FILE_DIR:PhysicsTestViewerTests>/PhysicsTestViewerTests.map")

# doctest exits 0 when a filter matches nothing, so each entry also requires a non-empty, all-passing summary line. The
# regex needs a doubled backslash before "|" in a quoted argument: CMake turns a single one into a bare "|", an
# alternation that any summary (failures included) would match.
add_test(NAME PhysicsTest.Unit.Viewer COMMAND PhysicsTestViewerTests "-tc=PhysicsTest.Viewer.*" "-tce=PhysicsTest.Viewer.RendererOffscreenColors")
set_tests_properties(PhysicsTest.Unit.Viewer PROPERTIES
	LABELS "unit;viewer"
	PASS_REGULAR_EXPRESSION "test cases: +[1-9][0-9]* \\| +[1-9][0-9]* passed \\| +0 failed")
# The offscreen renderer case alone: on a host without a hardware D3D11 device (WARP arrives in M12) it prints the marker
# below and CTest reports the entry as skipped, not passed.
add_test(NAME PhysicsTest.Unit.ViewerRenderer COMMAND PhysicsTestViewerTests "-tc=PhysicsTest.Viewer.RendererOffscreenColors")
set_tests_properties(PhysicsTest.Unit.ViewerRenderer PROPERTIES
	LABELS "unit;viewer"
	PASS_REGULAR_EXPRESSION "test cases: +1 \\| +1 passed \\| +0 failed"
	SKIP_REGULAR_EXPRESSION "PT_SKIPPED: no hardware D3D11 device")

# Headless smoke (no window, offscreen D3D11): the viewer opens a scene, renders, writes the PNG and its mode-viz manifest,
# and exits 0. Null (host snapshot) and, when built, Jolt (backend snapshot, contacts, islands).
set(_pt_viewer_shots "${CMAKE_BINARY_DIR}/_viewer")
set(_pt_viewer_backends Null)
if(PT_WITH_JOLT)
	list(APPEND _pt_viewer_backends Jolt)
endif()
foreach(_backend IN LISTS _pt_viewer_backends)
	pt_add_script_test(PhysicsTest.Viewer.Headless${_backend}
		SCRIPT "${CMAKE_SOURCE_DIR}/Source/Tests/Probes/CheckFatalExit.cmake"
		ARGS "-DPT_EXE=$<TARGET_FILE:PhysicsTestViewer>" -DPT_EXPECT_RC=0 "-DPT_EXPECT_REGEX=screenshot .*Headless${_backend}[.]png"
			"-DPT_ARGS=-Headless|-Backend=${_backend}|-Scenario=M1.SingleBoxDrop|-WarmupFrames=120|-Paused|-Frames=3|-ResX=800|-ResY=450|-ExecCmds=p.Chaos.Solver.DebugDrawCollisions 1, p.Chaos.Solver.DebugDrawTransforms 1, p.Chaos.Solver.DebugDrawIslands 1|-Screenshot=${_pt_viewer_shots}/Headless${_backend}.png"
			"-DPT_EXPECT_FILE=${_pt_viewer_shots}/Headless${_backend}.png.json"
			"-DPT_EXPECT_FILE_REGEX=.mode.: .viz.*.timing_label.: .viz mode, not a benchmark.*.backend.: .${_backend}.*.frame.: 120"
		LABELS "viewer")
	# No hardware D3D11 device (a GPU-less CI host; WARP arrives in M12): skipped, not failed. The viewer logs the failing
	# D3D11CreateDevice call and exits non-zero; the script echoes the output, which this property matches.
	set_tests_properties(PhysicsTest.Viewer.Headless${_backend} PROPERTIES SKIP_REGULAR_EXPRESSION "D3D11CreateDevice [(]hardware")
endforeach()
