// PhysicsTestEditor.h - FPhysicsTestEditor: the viewer's Dear ImGui front end, phase 1 (02_Architecture 11.2, 03 M4). [MVP M4]
// One Single layout (SideBySide is M12): a main menu (File, View, Show - the debug draw switches bound to the p.Chaos.*
// console variables), a dockspace whose central node stays transparent over the 3D view, and four panels:
//   Engines    the backend list from the registry (Bullet / PhysX appear when M5 registers them, no viewer change), the
//              backend variant, the physics profile, the scenario and its parameters, threads; Apply; play / pause / step /
//              reset / screenshot; the "restart required" prompt (03 M4)
//   Inspector  the outliner (every body) and the details of the selected one (UE's Outliner + Details in one panel)
//   Stats      "stat ChaosCounters" names (NumBodies, NumDynamicBodies, ..., NumManifoldPoints), the viewer timings - every
//              one labelled "viz mode, not a benchmark" - and the debug draw budget
//   Console    console variables / commands (p.Chaos.*, "stat ChaosCounters", pause / step / reset); the harness physics
//              CVars are shown read-only (the scene's resolved config owns them)
// The editor only records requests (open, reset, screenshot, restart, exit); the app carries them out between frames.
#pragma once

#include "CoreTypes.h"
#include "ViewerSession.h"

#include <string>
#include <vector>

namespace PhysicsTest
{
	class FPhysicsViewportClient;
	class FViewportCameraController;
	struct FD3D11DebugDrawStats;
	struct FDebugDrawString;
	struct FSceneView;

	// Every time the viewer shows carries this label (03 M4).
	inline constexpr const char* GVizTimingLabel = "viz mode, not a benchmark";

	// Per-frame timings the app measures (viz mode).
	struct FViewerFrameStats
	{
		double FrameMs = 0.0;            // whole app tick (wall)
		double PhysicsMs = 0.0;          // FPhysScene::Tick of this frame (0 when paused)
		double SceneDrawCpuMs = 0.0;     // snapshot + islands + DrawAll + visit (FPhysicsViewportClient)
		double RendererFlushCpuMs = 0.0; // upload + draw submission
		double UiCpuMs = 0.0;            // ImGui build + render submission
		double GpuMs = -1.0;             // timestamp queries (-1 = not ready)
		double Fps = 0.0;                // smoothed
		double SnapshotDrawMedianMs = -1.0;   // median of SceneDrawCpuMs + RendererFlushCpuMs over the last frames (-1 = none)
		int32 SnapshotDrawMedianFrames = 0;   // how many frames that median covers
		uint64 FramesRendered = 0;
		bool bWindowed = false;
		bool bVSync = true;
	};

	struct FEditorContext
	{
		FViewerSession& Session;
		FPhysicsViewportClient& Viewport;
		FViewportCameraController& Camera;
		const FD3D11DebugDrawStats& RenderStats;
		const FViewerFrameStats& FrameStats;
		const std::string& AdapterName;
		int32 ViewWidth = 0;
		int32 ViewHeight = 0;
		int32 MsaaSamples = 0;
		bool bHeadless = false;
	};

	class FPhysicsTestEditor
	{
	public:
		FPhysicsTestEditor();

		// The ImGui windows of one frame (between ImGui::NewFrame and ImGui::Render).
		void Draw(FEditorContext& Context);
		// Text of RenderString commands, projected with the view (DrawDebugString; ImGui background list, 02 11.1 step 5).
		static void DrawWorldStrings(const std::vector<FDebugDrawString>& Strings, const FSceneView& View);

		// The outcome of the last Open, shown in the Engines panel (RestartRequired offers the relaunch).
		void SetOpenResult(EViewerOpenResult Result, const std::string& Message, const FViewerSelection& Requested);
		// Runs one console line (also the app's -ExecCmds and the Console panel). Returns false for an unknown command.
		bool ExecuteConsoleCommand(const std::string& Line, FViewerSession& Session);
		void AddLog(const std::string& Line);

		// Requests for the app (each consumed once).
		bool ConsumeOpenRequest(FViewerSelection& OutSelection);
		bool ConsumeResetRequest() { return Consume(bResetRequested); }
		bool ConsumeScreenshotRequest() { return Consume(bScreenshotRequested); }
		bool ConsumeFocusRequest() { return Consume(bFocusRequested); }
		bool ConsumeExitRequest() { return Consume(bExitRequested); }
		bool ConsumeRestartRequest(std::string& OutCommandLine);

		bool bShowEngines = true;
		bool bShowInspector = true;
		bool bShowStats = true;
		bool bShowConsole = true;
		bool bShowDemo = false;

	private:
		static bool Consume(bool& bFlag)
		{
			const bool bWas = bFlag;
			bFlag = false;
			return bWas;
		}
		void DrawMainMenu(FEditorContext& Context);
		void DrawEnginesPanel(FEditorContext& Context);
		void DrawInspectorPanel(FEditorContext& Context);
		void DrawStatsPanel(FEditorContext& Context);
		void DrawConsolePanel(FEditorContext& Context);

		FViewerSelection Pending;            // what the Engines panel edits
		bool bPendingInitialised = false;
		// The Engines panel's Config/ lists (per editor: M12 SideBySide has one editor state per view).
		std::string ConfigListsBackend;
		std::vector<std::string> ConfigVariants;
		std::vector<std::string> ConfigProfiles;
		bool bConfigListsStale = true;
		FViewerSelection RequestedOpen;
		bool bOpenRequested = false;
		bool bResetRequested = false;
		bool bScreenshotRequested = false;
		bool bFocusRequested = false;
		bool bExitRequested = false;
		bool bRestartRequested = false;
		std::string RestartCommandLine;
		EViewerOpenResult LastOpenResult = EViewerOpenResult::Opened;
		std::string LastOpenMessage;
		FViewerSelection LastRequested;
		std::vector<std::string> Log;
		std::vector<std::string> History;
		char ConsoleInput[512] = {};
		char OutlinerFilter[128] = {};
		char CVarFilter[128] = "p.Chaos";
	};
}
