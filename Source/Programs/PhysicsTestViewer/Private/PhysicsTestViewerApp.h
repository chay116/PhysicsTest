// PhysicsTestViewerApp.h - FPhysicsTestViewerApp: the viewer program in FEngineLoop phases (02_Architecture 11.2). [MVP M4]
//   PreInit   the command line, console variables (-ExecCmds), the options below
//   Init      the window (or headless), FD3D11RHI, the renderer, Dear ImGui (docking) + ImPlot, the first scene
//   Tick      messages -> camera -> one physics frame (message-loop thread; none while paused) -> snapshot / debug draw ->
//             scene pass -> resolve -> ImGui (panels, DrawDebugString text) -> present; then the editor's requests
//   Exit      the scene, the modules (TermGamePhys), ImGui, the device
//
//   PhysicsTestViewer.exe [-Backend=Jolt] [-BackendProfile=..] [-Profile=Normalized] [-Scenario=M1.SingleBoxDrop]
//       [-Param:Name=Value ...] [-Threads=1] [-ExecCmds="p.Chaos.Solver.DebugDrawCollisions 1, ..."] [-ini:...]
//       [-ResX=1600 -ResY=900] [-NoVSync] [-Paused] [-WarmupFrames=N] [-Frames=N [-Exit]] [-Headless]
//       [-Screenshot=<png>] [-PerfReport=<json>] [-NoUI | -UI=Stats,Engines,...] [-Camera=X,Y,Z,Pitch,Yaw] [-WaitGpu]
//       [-ViewerCmds="open PhysX SdkDefault, restart"]
//   -ViewerCmds runs viewer console commands (Console panel syntax), one per rendered frame from the first frame on (M5:
//   the scripted way to switch engines and to take the restart path headless). A restart from a -Headless process relaunches
//   headless with the same run control (no window, no console window, the child writes to this process's stdout / stderr),
//   waits for it and returns its exit code; a windowed process starts the new viewer and exits.
//   -Headless renders offscreen (no window) and always exits after -Frames (default 1). -WarmupFrames advances the
//   physics N frames before the first rendered frame. -Screenshot captures the last frame (PNG + <png>.json, mode viz).
//   -PerfReport writes per-frame CPU / GPU timings of the rendered frames (JSON, mode viz, "viz mode, not a benchmark").
//   -NoVSync presents without vsync (windowed: with DXGI tearing when supported, so the frame rate is not capped).
//   An invalid -Threads / -Frames / -WarmupFrames / -Param: value stops the viewer with exit code 2 (every mode). The process
//   keeps Windows' default power throttling (QoS); M7's host controls (DL11) decide the policy for the bench and the viewer.
// Keys (windowed): RMB + WASD / EQ / CZ / wheel = fly, F = frame selection (or the scene), P = pause, Period = step,
// Ctrl+R = reset, F9 = screenshot, left click = select.
#pragma once

#include "CoreTypes.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace PhysicsTest
{
	struct FViewerAppOptions
	{
		bool bHeadless = false;
		int32 Width = 1600;
		int32 Height = 900;
		bool bVSync = true;
		bool bStartPaused = false;
		int32 WarmupFrames = 0;
		int32 Frames = 0;               // rendered frames to run (0 = until the window closes; headless: 1)
		bool bExitAfterFrames = false;
		std::filesystem::path Screenshot;
		std::filesystem::path PerfReport;
		bool bNoUI = false;
		std::string UIPanels;           // -UI=Stats,Engines,Inspector,Console (empty = all, headless: Stats)
		bool bHasCamera = false;
		double Camera[5] = {};          // X, Y, Z, Pitch, Yaw
		bool bWaitForGpu = false;       // headless default: one frame at a time (pessimistic fps)
		std::vector<std::string> ViewerCommands;   // -ViewerCmds, one per rendered frame (M5)
	};

	class FPhysicsTestViewerApp
	{
	public:
		FPhysicsTestViewerApp();
		~FPhysicsTestViewerApp();

		// Returns the process exit code when the app must stop (an invalid command line: 2), else -1.
		int32 PreInit();
		bool Init();
		void Tick();
		void Exit();
		[[nodiscard]] bool IsRequestingExit() const;
		[[nodiscard]] int32 GetExitCode() const;

	private:
		struct FImpl;
		std::unique_ptr<FImpl> Impl;
	};
}
