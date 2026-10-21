# PT::Editor - the viewer's editor side, phase 1 (02_Architecture 4.5, 11.2; 03 M4): FViewportCameraController (UE editor
# camera), FViewerSession (one scene, selection -> config -> FPhysScene, restart-required rules), FPhysicsViewportClient
# (snapshot -> islands -> DrawAll -> queue -> renderer) and FPhysicsTestEditor with the Engines / Inspector / Stats /
# Console panels (Dear ImGui, docking). SideBySide, replay and the profiler are M12.
pt_add_module(PtEditor STATIC ALIAS PT::Editor
	SOURCES
		Public/PhysicsTestEditor.h
		Public/PhysicsViewportClient.h
		Public/ViewerSession.h
		Public/ViewportCameraController.h
		Private/PhysicsTestEditor.cpp
		Private/PhysicsViewportClient.cpp
		Private/ViewerSession.cpp
		Private/ViewportCameraController.cpp
		Private/Panels/PanelCommon.h
		Private/Panels/ConsolePanel.cpp
		Private/Panels/EnginesPanel.cpp
		Private/Panels/InspectorPanel.cpp
		Private/Panels/StatsPanel.cpp
	PUBLIC_DEPS PT::D3D11DebugRenderer PT::Benchmark PT::Scenarios
	PRIVATE_DEPS PT::ImGui)
set_property(TARGET PtEditor PROPERTY FOLDER "PhysicsTest/Editor")
