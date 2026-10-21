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
# and exits 0. Null (host snapshot) and every SDK backend (backend snapshot, contacts, islands); M5 adds Bullet and PhysX
# (variant UE4Compat) without a viewer code change. Entries: <test suffix>|<backend>|<variant or ->.
set(_pt_viewer_shots "${CMAKE_BINARY_DIR}/_viewer")
set(_pt_viewer_backends "Null|Null|-")
if(PT_WITH_JOLT)
	list(APPEND _pt_viewer_backends "Jolt|Jolt|-")
endif()
if(PT_WITH_BULLET)
	list(APPEND _pt_viewer_backends "Bullet|Bullet|-")
endif()
if(PT_WITH_PHYSX)
	list(APPEND _pt_viewer_backends "PhysX|PhysX|UE4Compat")
endif()
foreach(_entry IN LISTS _pt_viewer_backends)
	string(REPLACE "|" ";" _parts "${_entry}")
	list(GET _parts 0 _name)
	list(GET _parts 1 _backend)
	list(GET _parts 2 _variant)
	set(_variant_arg "")
	set(_variant_regex "")
	if(NOT _variant STREQUAL "-")
		set(_variant_arg "|-BackendProfile=${_variant}")
		set(_variant_regex ".*.variant.: .${_variant}.")
	endif()
	pt_add_script_test(PhysicsTest.Viewer.Headless${_name}
		SCRIPT "${CMAKE_SOURCE_DIR}/Source/Tests/Probes/CheckFatalExit.cmake"
		ARGS "-DPT_EXE=$<TARGET_FILE:PhysicsTestViewer>" -DPT_EXPECT_RC=0 "-DPT_EXPECT_REGEX=screenshot .*Headless${_name}[.]png"
			"-DPT_ARGS=-Headless|-Backend=${_backend}${_variant_arg}|-Scenario=M1.SingleBoxDrop|-WarmupFrames=120|-Paused|-Frames=3|-ResX=800|-ResY=450|-ExecCmds=p.Chaos.Solver.DebugDrawCollisions 1, p.Chaos.Solver.DebugDrawTransforms 1, p.Chaos.Solver.DebugDrawIslands 1|-Screenshot=${_pt_viewer_shots}/Headless${_name}.png"
			"-DPT_EXPECT_FILE=${_pt_viewer_shots}/Headless${_name}.png.json"
			"-DPT_EXPECT_FILE_REGEX=.mode.: .viz.*.timing_label.: .viz mode, not a benchmark.*.backend.: .${_backend}.${_variant_regex}.*.frame.: 120"
		LABELS "viewer")
	# No hardware D3D11 device (a GPU-less CI host; WARP arrives in M12): skipped, not failed. The viewer logs the failing
	# D3D11CreateDevice call and exits non-zero; the script echoes the output, which this property matches.
	set_tests_properties(PhysicsTest.Viewer.Headless${_name} PROPERTIES SKIP_REGULAR_EXPRESSION "D3D11CreateDevice [(]hardware")
endforeach()

# M5 (M4 carry-over (1)): the "restart required" relaunch end to end, headless. PhysX UE4Compat -> UE4Compat-TGS switches in
# this process (one PxPhysics, same (100, 1000)); -> SdkDefault is RestartRequired (PxTolerancesScale (1, 10) is
# process-wide); "restart" relaunches the viewer headless with the SdkDefault selection and this run's run control (no window,
# no console window, same stdout), waits for it and returns its exit code. The output regex checks both halves of the 03 1.6
# rule in order: the TGS switch opens in this process, then SdkDefault needs the restart. The child writes the screenshot,
# its manifest and the -PerfReport (variant SdkDefault, frame 4 and 4 rendered frames: the child runs -Frames=4 on its own
# fresh scene); the parent, which rendered 3 frames before the restart, must not overwrite the report (M5 round 0).
if(PT_WITH_PHYSX)
	pt_add_script_test(PhysicsTest.Viewer.HeadlessRestart
		SCRIPT "${CMAKE_SOURCE_DIR}/Source/Tests/Probes/CheckFatalExit.cmake"
		ARGS "-DPT_EXE=$<TARGET_FILE:PhysicsTestViewer>" -DPT_EXPECT_RC=0
			"-DPT_EXPECT_REGEX=opened M1.SingleBoxDrop on PhysX [(][^/]*/ UE4Compat-TGS[)].*open PhysX / SdkDefault: RestartRequired.*GLOBAL_SCENE_SETTINGS.*restarting: -Backend=PhysX -BackendProfile=SdkDefault .*-Headless.*opened M1.SingleBoxDrop on PhysX [(][^/]*/ SdkDefault[)].*screenshot .*HeadlessRestart[.]png.*the relaunched viewer exited with 0"
			"-DPT_ARGS=-Headless|-Backend=PhysX|-BackendProfile=UE4Compat|-Scenario=M1.SingleBoxDrop|-Frames=4|-ResX=640|-ResY=360|-ViewerCmds=open PhysX UE4Compat-TGS, open PhysX SdkDefault, restart|-Screenshot=${_pt_viewer_shots}/HeadlessRestart.png|-PerfReport=${_pt_viewer_shots}/HeadlessRestart.perf.json"
			"-DPT_EXPECT_FILE=${_pt_viewer_shots}/HeadlessRestart.png.json"
			"-DPT_EXPECT_FILE_REGEX=.mode.: .viz.*.backend.: .PhysX.*.variant.: .SdkDefault.*.frame.: 4,"
			"-DPT_EXPECT_FILE2=${_pt_viewer_shots}/HeadlessRestart.perf.json"
			"-DPT_EXPECT_FILE2_REGEX=.scene.: [{].*.variant.: .SdkDefault.*.frame.: 4,.*.frames.: 4,"
		LABELS "viewer")
	set_tests_properties(PhysicsTest.Viewer.HeadlessRestart PROPERTIES SKIP_REGULAR_EXPRESSION "D3D11CreateDevice [(]hardware")
endif()
