// PhysicsTestViewerApp.cpp - FPhysicsTestViewerApp (see the header). Win32 + D3D11 + Dear ImGui (docking) + ImPlot, from
// the verified prototype prototypes/render_d3d11/src/proto_render.cpp.
#include "PhysicsTestViewerApp.h"

#include "CoreMinimal.h"
#include "Containers/StringConv.h"
#include "D3D11DebugDrawRenderer.h"
#include "D3D11RHI.h"
#include "DebugDrawSettings.h"
#include "HAL/IConsoleManager.h"
#include "Physics/PhysicsBackend.h"
#include "PhysicsTestEditor.h"
#include "PhysicsViewportClient.h"
#include "PhysScene.h"
#include "PTBuildInfo.h"
#include "ViewerSession.h"
#include "ViewportCameraController.h"

#include "Misc/PtThirdPartyIncludesStart.h"
#include <Windows.h>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <implot.h>
#include <nlohmann/json.hpp>
#include "Misc/PtThirdPartyIncludesEnd.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <vector>

// Declared (commented out) in imgui_impl_win32.h so that it does not drag <windows.h> into every user.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT Message, WPARAM WParam, LPARAM LParam);

DEFINE_LOG_CATEGORY_STATIC(LogViewer, Log, All);

namespace PhysicsTest
{
	namespace
	{
		using FJson = nlohmann::ordered_json;

		constexpr FColor GClearColor(40, 42, 46);   // 02 11.1: non-black, so Black islands stay visible
		constexpr int32 GRollingFrames = 60;        // the window of the Stats "snapshot + draw" median

		std::vector<std::string> SplitList(const std::string& Text, const char Separator)
		{
			std::vector<std::string> Items;
			std::string Current;
			bool bInQuotes = false;
			for (const char C : Text)
			{
				if (C == '"')
				{
					bInQuotes = !bInQuotes;
					continue;
				}
				if (C == Separator && !bInQuotes)
				{
					Items.push_back(Current);
					Current.clear();
					continue;
				}
				Current.push_back(C);
			}
			Items.push_back(Current);
			for (std::string& Item : Items)
			{
				while (!Item.empty() && (Item.front() == ' ' || Item.front() == '\t'))
				{
					Item.erase(Item.begin());
				}
				while (!Item.empty() && (Item.back() == ' ' || Item.back() == '\t'))
				{
					Item.pop_back();
				}
			}
			Items.erase(std::remove(Items.begin(), Items.end(), std::string()), Items.end());
			return Items;
		}

		std::filesystem::path GetRepositoryDir()
		{
			return StringConv::Utf8ToPath(PT_SOURCE_DIR).make_preferred();
		}

		std::string MakeTimestamp()
		{
			const std::time_t Now = std::time(nullptr);
			std::tm Local = {};
			localtime_s(&Local, &Now);
			char Text[32];
			std::strftime(Text, sizeof(Text), "%Y%m%d-%H%M%S", &Local);
			return Text;
		}

		struct FStatsSummary
		{
			double Mean = 0.0;
			double Median = 0.0;
			double P95 = 0.0;
			double Min = 0.0;
			double Max = 0.0;
		};
		FStatsSummary Summarize(std::vector<double> Values)
		{
			FStatsSummary Out;
			if (Values.empty())
			{
				return Out;
			}
			std::sort(Values.begin(), Values.end());
			double Sum = 0.0;
			for (const double Value : Values)
			{
				Sum += Value;
			}
			Out.Mean = Sum / static_cast<double>(Values.size());
			Out.Median = Values[(Values.size() - 1) / 2];
			Out.P95 = Values[static_cast<size_t>(std::ceil(0.95 * static_cast<double>(Values.size()))) - 1];
			Out.Min = Values.front();
			Out.Max = Values.back();
			return Out;
		}
		// An integer switch in [Min, Max]: absent keeps Value; present but malformed or out of range is an error (the bench's
		// rule for -Frames / -Threads, BenchConfig.cpp).
		bool ReadIntSwitch(const char* Cmd, const char* Key, const int32 Min, const int32 Max, int32& Value, std::string& OutError)
		{
			std::string Raw;
			if (!FParse::Value(Cmd, Key, Raw))
			{
				return true;
			}
			int32 Parsed = 0;
			if (!FParse::Value(Cmd, Key, Parsed) || Parsed < Min || Parsed > Max)
			{
				OutError = std::string("-") + Key + Raw + " must be an integer in [" + std::to_string(Min) + ", " + std::to_string(Max) + "]";
				return false;
			}
			Value = Parsed;
			return true;
		}

		FJson ToJson(const FStatsSummary& Stats)
		{
			return FJson{ { "mean_ms", Stats.Mean }, { "median_ms", Stats.Median }, { "p95_ms", Stats.P95 }, { "min_ms", Stats.Min }, { "max_ms", Stats.Max } };
		}

	}

	struct FPhysicsTestViewerApp::FImpl
	{
		FViewerAppOptions Options;
		HWND Window = nullptr;
		FD3D11RHI RHI;
		std::unique_ptr<FD3D11DebugDrawRenderer> Renderer;
		FViewerSession Session;
		FPhysicsViewportClient Viewport;
		FViewportCameraController Camera;
		FPhysicsTestEditor Editor;
		FViewerFrameStats FrameStats;
		std::string ImGuiIniPath;
		FViewerSelection InitialSelection;      // the command line's scene (PreInit parses it, Init opens it)
		bool bImGuiInitialised = false;
		bool bWin32Backend = false;
		bool bRequestExit = false;
		bool bCameraPlaced = false;
		// A headless relaunch ran and ended (ProcessRequests): the child wrote the run's outputs (-PerfReport, -Screenshot are
		// forwarded to it), so this process must not overwrite them with its few frames before the restart.
		bool bRelaunchedHeadless = false;
		int32 ExitCode = 0;
		int32 PendingWidth = 0;
		int32 PendingHeight = 0;
		uint64 LastTickCycles = 0;
		uint64 FramesRendered = 0;
		// -PerfReport samples (rendered frames)
		std::vector<double> SampleFrameMs;
		std::vector<double> SampleSceneDrawMs;
		std::vector<double> SampleFlushMs;
		std::vector<double> SampleUiMs;
		std::vector<double> SampleGpuMs;
		std::vector<double> SampleSnapshotMs;
		std::vector<double> SampleDrawAllMs;
		double LastScreenshotMs = -1.0;             // the -Screenshot capture, excluded from FrameMs
		size_t NextViewerCommand = 0;               // -ViewerCmds: the next one to run
		std::vector<double> RecentSnapshotDrawMs;   // ring of the last GRollingFrames "snapshot + draw" values (Stats median)
		size_t RecentSnapshotDrawNext = 0;

		bool CreateMainWindow();
		void PlaceCameraForScene();
		void HandleInput(double DeltaSeconds);
		void ProcessRequests();
		bool TakeScreenshot(const std::filesystem::path& Path);
		void UpdateSnapshotDrawMedian(double Milliseconds);
		void WriteViewerManifest(const std::filesystem::path& Path, const char* Reason) const;
		void WritePerfReport() const;
		FJson MakeSceneJson() const;
		static LRESULT CALLBACK WndProc(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam);
	};

	namespace
	{
		void* GAppImpl = nullptr;   // FPhysicsTestViewerApp::FImpl: the window procedure's way back (one app per process)
	}

	FPhysicsTestViewerApp::FPhysicsTestViewerApp()
		: Impl(std::make_unique<FImpl>())
	{
		GAppImpl = Impl.get();
	}

	FPhysicsTestViewerApp::~FPhysicsTestViewerApp()
	{
		GAppImpl = nullptr;
	}

	bool FPhysicsTestViewerApp::IsRequestingExit() const
	{
		return Impl->bRequestExit;
	}

	int32 FPhysicsTestViewerApp::GetExitCode() const
	{
		return Impl->ExitCode;
	}

	// ---- PreInit ------------------------------------------------------------------------------------------------------
	int32 FPhysicsTestViewerApp::PreInit()
	{
		const char* Cmd = FCommandLine::Get();
		FViewerAppOptions& Options = Impl->Options;
		Options.bHeadless = FParse::Param(Cmd, "Headless");
		FParse::Value(Cmd, "ResX=", Options.Width);
		FParse::Value(Cmd, "ResY=", Options.Height);
		Options.Width = FMath::Clamp(Options.Width, 64, 7680);
		Options.Height = FMath::Clamp(Options.Height, 64, 4320);
		Options.bVSync = !FParse::Param(Cmd, "NoVSync");
		Options.bStartPaused = FParse::Param(Cmd, "Paused");
		// The first scene's selection and the frame counts. An invalid value stops the viewer with exit code 2 in every mode,
		// as the bench does for its command line, instead of opening the scene with other parameters than the ones asked for.
		{
			FViewerSelection& Selection = Impl->InitialSelection;
			FParse::Value(Cmd, "Backend=", Selection.Backend);
			FParse::Value(Cmd, "BackendProfile=", Selection.BackendProfile);
			FParse::Value(Cmd, "Profile=", Selection.PhysicsProfile);
			FParse::Value(Cmd, "Scenario=", Selection.Scenario);
			std::string Error;
			if (!ReadIntSwitch(Cmd, "Threads=", 1, 64, Selection.Threads, Error) || !ReadIntSwitch(Cmd, "Frames=", 0, 10000000, Options.Frames, Error)
				|| !ReadIntSwitch(Cmd, "WarmupFrames=", 0, 10000000, Options.WarmupFrames, Error)
				|| !FBenchRunSpec::ParseScenarioParams(Cmd, Selection.Params, Error))
			{
				UE_LOG(LogViewer, Error, "invalid command line: %s", Error.c_str());
				return 2;
			}
		}
		Options.bExitAfterFrames = FParse::Param(Cmd, "Exit") || Options.bHeadless;
		if (Options.bHeadless && Options.Frames <= 0)
		{
			Options.Frames = 1;
		}
		std::string Text;
		if (FParse::Value(Cmd, "Screenshot=", Text))
		{
			Options.Screenshot = StringConv::Utf8ToPath(Text);
		}
		if (FParse::Value(Cmd, "PerfReport=", Text))
		{
			Options.PerfReport = StringConv::Utf8ToPath(Text);
		}
		Options.bNoUI = FParse::Param(Cmd, "NoUI");
		FParse::Value(Cmd, "UI=", Options.UIPanels, false);   // a comma list
		Options.bWaitForGpu = Options.bHeadless || FParse::Param(Cmd, "WaitGpu");
		if (FParse::Value(Cmd, "Camera=", Text, false))   // a comma list
		{
			const std::vector<std::string> Parts = SplitList(Text, ',');
			if (Parts.size() != 5)
			{
				UE_LOG(LogViewer, Error, "-Camera=%s: expected X,Y,Z,Pitch,Yaw", Text.c_str());
				return 2;
			}
			for (size_t Index = 0; Index < 5; ++Index)
			{
				Options.Camera[Index] = std::atof(Parts[Index].c_str());
			}
			Options.bHasCamera = true;
		}
		// -ViewerCmds: viewer console commands, run one per rendered frame (Tick, before the editor's requests) - M5's scripted
		// engine switch and restart path. Not forwarded to a relaunched process (GetForwardedSwitches).
		if (FParse::Value(Cmd, "ViewerCmds=", Text, false))
		{
			Options.ViewerCommands = SplitList(Text, ',');
		}
		// -ExecCmds: the viewer's own console variables (debug draw) now; the resolver applies the physics ones per scene.
		if (FParse::Value(Cmd, "ExecCmds=", Text, false))
		{
			for (const std::string& Command : SplitList(Text, ','))
			{
				std::string Message;
				if (!IConsoleManager::Get().ProcessUserConsoleInput(Command, ECVF_SetByConsole, &Message))
				{
					UE_LOG(LogViewer, Warning, "-ExecCmds '%s': %s", Command.c_str(), Message.empty() ? "not a console variable of this process" : Message.c_str());
				}
				Impl->Editor.AddLog("-ExecCmds: " + Command);
			}
		}
		return -1;
	}

	// ---- Init ---------------------------------------------------------------------------------------------------------
	bool FPhysicsTestViewerApp::FImpl::CreateMainWindow()
	{
		ImGui_ImplWin32_EnableDpiAwareness();
		WNDCLASSEXW Class = {};
		Class.cbSize = sizeof(Class);
		Class.style = CS_CLASSDC;
		Class.lpfnWndProc = &FImpl::WndProc;
		Class.hInstance = GetModuleHandleW(nullptr);
		Class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
		Class.lpszClassName = L"PhysicsTestViewer";
		RegisterClassExW(&Class);
		RECT Rect = { 0, 0, Options.Width, Options.Height };
		AdjustWindowRect(&Rect, WS_OVERLAPPEDWINDOW, FALSE);
		const std::wstring Title = L"PhysicsTest Viewer (viz mode, not a benchmark) - " + StringConv::Utf8ToWide(PT_BUILD_PRESET);
		Window = CreateWindowExW(0, Class.lpszClassName, Title.c_str(), WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, Rect.right - Rect.left,
			Rect.bottom - Rect.top, nullptr, nullptr, Class.hInstance, nullptr);
		if (Window == nullptr)
		{
			return false;
		}
		RECT Client = {};
		GetClientRect(Window, &Client);
		Options.Width = FMath::Max(64, static_cast<int32>(Client.right - Client.left));
		Options.Height = FMath::Max(64, static_cast<int32>(Client.bottom - Client.top));
		ShowWindow(Window, SW_SHOWDEFAULT);
		UpdateWindow(Window);
		return true;
	}

	bool FPhysicsTestViewerApp::Init()
	{
		FImpl& I = *Impl;
		if (!I.Options.bHeadless && !I.CreateMainWindow())
		{
			UE_LOG(LogViewer, Error, "cannot create the window");
			return false;
		}
		FD3D11RHICreateParams RhiParams;
		RhiParams.WindowHandle = I.Window;
		RhiParams.Width = I.Options.Width;
		RhiParams.Height = I.Options.Height;
		RhiParams.MsaaSamples = 4;
		RhiParams.bVSync = I.Options.bVSync;
		RhiParams.bDebugLayer = FParse::Param(FCommandLine::Get(), "D3DDebug");
		std::string Error;
		if (!I.RHI.Init(RhiParams, Error))
		{
			UE_LOG(LogViewer, Error, "D3D11: %s", Error.c_str());
			return false;
		}
		I.Renderer = std::make_unique<FD3D11DebugDrawRenderer>(I.RHI);
		if (!I.Renderer->Init(Error))
		{
			UE_LOG(LogViewer, Error, "renderer: %s", Error.c_str());
			return false;
		}
		UE_LOG(LogViewer, Display, "D3D11 %s, %dx%d, MSAA %dx, %s", I.RHI.GetAdapterName().c_str(), I.RHI.GetWidth(), I.RHI.GetHeight(), I.RHI.GetMsaaSamples(),
			I.Options.bHeadless ? "headless (offscreen)" : "windowed");

		// Dear ImGui (docking) + ImPlot (the Profiler panel arrives in M12; ImPlot is initialised now so panels can use it).
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImPlot::CreateContext();
		ImGuiIO& IO = ImGui::GetIO();
		IO.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
		if (I.Options.bHeadless)
		{
			IO.IniFilename = nullptr;
			IO.DisplaySize = ImVec2(static_cast<float>(I.Options.Width), static_cast<float>(I.Options.Height));
		}
		else
		{
			const std::filesystem::path IniPath = GetRepositoryDir() / "Saved" / "Config" / "PhysicsTestViewer.imgui.ini";
			std::error_code Ignored;
			std::filesystem::create_directories(IniPath.parent_path(), Ignored);
			I.ImGuiIniPath = StringConv::PathToUtf8(IniPath);
			IO.IniFilename = I.ImGuiIniPath.c_str();
			ImGui_ImplWin32_Init(I.Window);
			I.bWin32Backend = true;
		}
		ImGui::StyleColorsDark();
		ImGui_ImplDX11_Init(I.RHI.GetDevice(), I.RHI.GetContext());
		I.bImGuiInitialised = true;
		// Panels: headless shows Stats only (the screenshots keep the scene visible); -UI= / -NoUI override.
		const std::string Panels = I.Options.bNoUI ? std::string("none") : (!I.Options.UIPanels.empty() ? I.Options.UIPanels : (I.Options.bHeadless ? "Stats" : "all"));
		if (Panels != "all")
		{
			const auto Has = [&Panels](const char* Name) { return Panels.find(Name) != std::string::npos; };
			I.Editor.bShowEngines = Has("Engines");
			I.Editor.bShowInspector = Has("Inspector");
			I.Editor.bShowStats = Has("Stats");
			I.Editor.bShowConsole = Has("Console");
		}

		// The first scene (the command line's selection, parsed in PreInit; defaults otherwise).
		const FViewerSelection& Selection = I.InitialSelection;
		std::string Message;
		const EViewerOpenResult Result = I.Session.Open(Selection, Message);
		I.Editor.SetOpenResult(Result, Message, Selection);
		if (Result != EViewerOpenResult::Opened)
		{
			UE_LOG(LogViewer, Error, "cannot open %s on %s: %s (%s)", Selection.Scenario.c_str(), Selection.Backend.c_str(), Message.c_str(), ToString(Result));
			if (I.Options.bHeadless)
			{
				return false;
			}
		}
		for (int32 Frame = 0; Frame < I.Options.WarmupFrames && I.Session.HasScene(); ++Frame)
		{
			I.Session.TickPhysics();
		}
		I.Session.SetPaused(I.Options.bStartPaused);
		I.LastTickCycles = FPlatformTime::Cycles64();
		return true;
	}

	// ---- camera and input ---------------------------------------------------------------------------------------------
	void FPhysicsTestViewerApp::FImpl::PlaceCameraForScene()
	{
		if (Options.bHasCamera)
		{
			Camera.Location = FVector(Options.Camera[0], Options.Camera[1], Options.Camera[2]);
			Camera.Rotation = FRotator(Options.Camera[3], Options.Camera[4], 0.0);
			bCameraPlaced = true;
			return;
		}
		// A three-quarter view of the movable bodies (the ground box is 200 m wide: framing it would lose the scene).
		FBox Bounds;
		const FPhysicsDebugSnapshot& Snapshot = Viewport.GetSnapshot();
		for (const PtParticleSnapshot& Particle : Snapshot.Particles)
		{
			if (Particle.ObjectState == static_cast<uint8>(EObjectStateType::Static))
			{
				continue;
			}
			Bounds += FVector(Particle.BoundsMin.X, Particle.BoundsMin.Y, Particle.BoundsMin.Z);
			Bounds += FVector(Particle.BoundsMax.X, Particle.BoundsMax.Y, Particle.BoundsMax.Z);
		}
		if (!Bounds.IsValid)
		{
			Bounds = FBox(FVector(-200.0, -200.0, 0.0), FVector(200.0, 200.0, 200.0));
		}
		Bounds += FVector(Bounds.GetCenter().X, Bounds.GetCenter().Y, 0.0);   // keep the ground in view
		const FVector Center = Bounds.GetCenter();
		const double Radius = FMath::Max(Bounds.GetExtent().Size(), 150.0);
		const FVector Direction = FVector(-1.0, -0.55, 0.6).GetSafeNormal();
		Camera.LookAt(Center + Direction * (Radius * 1.9), Center);
		bCameraPlaced = true;
	}

	void FPhysicsTestViewerApp::FImpl::HandleInput(const double DeltaSeconds)
	{
		FViewportInput Input;
		if (!bWin32Backend)
		{
			Camera.Tick(Input, DeltaSeconds);
			return;
		}
		const ImGuiIO& IO = ImGui::GetIO();
		const bool bMouseFree = !IO.WantCaptureMouse;
		const bool bKeysFree = !IO.WantCaptureKeyboard || ImGui::IsMouseDown(ImGuiMouseButton_Right);
		Input.bRightMouse = bMouseFree && ImGui::IsMouseDown(ImGuiMouseButton_Right);
		Input.bLeftMouse = bMouseFree && ImGui::IsMouseDown(ImGuiMouseButton_Left);
		Input.bMiddleMouse = bMouseFree && ImGui::IsMouseDown(ImGuiMouseButton_Middle);
		Input.bAlt = IO.KeyAlt;
		if (bMouseFree)
		{
			Input.MouseDeltaX = IO.MouseDelta.x;
			Input.MouseDeltaY = IO.MouseDelta.y;
			Input.WheelNotches = IO.MouseWheel;
		}
		if (bKeysFree)
		{
			Input.bMoveForward = ImGui::IsKeyDown(ImGuiKey_W);
			Input.bMoveBackward = ImGui::IsKeyDown(ImGuiKey_S);
			Input.bMoveLeft = ImGui::IsKeyDown(ImGuiKey_A);
			Input.bMoveRight = ImGui::IsKeyDown(ImGuiKey_D);
			Input.bMoveUp = ImGui::IsKeyDown(ImGuiKey_E);
			Input.bMoveDown = ImGui::IsKeyDown(ImGuiKey_Q);
			Input.bZoomIn = ImGui::IsKeyDown(ImGuiKey_C);
			Input.bZoomOut = ImGui::IsKeyDown(ImGuiKey_Z);
			if (!Input.bRightMouse)
			{
				if (ImGui::IsKeyPressed(ImGuiKey_F, false))
				{
					Editor.ExecuteConsoleCommand("frame", Session);
				}
				if (ImGui::IsKeyPressed(ImGuiKey_P, false))
				{
					Session.SetPaused(!Session.IsPaused());
				}
				if (ImGui::IsKeyPressed(ImGuiKey_Period, true))
				{
					Session.SetPaused(true);
					Session.RequestSingleStep();
				}
				if (ImGui::IsKeyPressed(ImGuiKey_F9, false))
				{
					Editor.ExecuteConsoleCommand("screenshot", Session);
				}
				if (IO.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_R, false))
				{
					Editor.ExecuteConsoleCommand("reset", Session);
				}
			}
		}
		// Left click without a drag selects the body under the cursor.
		if (bMouseFree && !IO.KeyAlt && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.0f).x == 0.0f
			&& ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.0f).y == 0.0f)
		{
			const FSceneView View = Camera.MakeView(RHI.GetWidth(), RHI.GetHeight());
			FVector Origin;
			FVector Direction;
			View.DeprojectPixel(IO.MousePos.x, IO.MousePos.y, Origin, Direction);
			Viewport.SelectedBody = Viewport.PickBody(Origin, Direction);
		}
		Camera.Tick(Input, DeltaSeconds);
	}

	// ---- Tick ---------------------------------------------------------------------------------------------------------
	void FPhysicsTestViewerApp::Tick()
	{
		FImpl& I = *Impl;
		const uint64 TickBegin = FPlatformTime::Cycles64();
		const double DeltaSeconds = FPlatformTime::ToSeconds64(TickBegin - I.LastTickCycles);
		I.LastTickCycles = TickBegin;

		// Messages.
		if (I.Window != nullptr)
		{
			MSG Message = {};
			while (PeekMessageW(&Message, nullptr, 0, 0, PM_REMOVE))
			{
				TranslateMessage(&Message);
				DispatchMessageW(&Message);
				if (Message.message == WM_QUIT)
				{
					I.bRequestExit = true;
				}
			}
			if (I.bRequestExit)
			{
				return;
			}
			if (I.PendingWidth > 0 && I.PendingHeight > 0)
			{
				I.RHI.Resize(I.PendingWidth, I.PendingHeight);
				I.PendingWidth = 0;
				I.PendingHeight = 0;
			}
			if (IsIconic(I.Window))
			{
				Sleep(10);
				return;
			}
		}
		const int32 Width = I.RHI.GetWidth();
		const int32 Height = I.RHI.GetHeight();

		// ImGui frame start (input state for the camera comes from ImGui's IO).
		ImGui_ImplDX11_NewFrame();
		if (I.bWin32Backend)
		{
			ImGui_ImplWin32_NewFrame();
		}
		else
		{
			ImGuiIO& IO = ImGui::GetIO();
			IO.DisplaySize = ImVec2(static_cast<float>(Width), static_cast<float>(Height));
			IO.DeltaTime = static_cast<float>(DeltaSeconds > 0.0 ? FMath::Min(DeltaSeconds, 0.25) : 1.0 / 60.0);
		}
		ImGui::NewFrame();
		I.HandleInput(DeltaSeconds);

		// Physics: one frame on this (the message-loop) thread unless paused (02 11.2).
		const int32 FramesRun = I.Session.TickPhysics();
		I.FrameStats.PhysicsMs = FramesRun > 0 ? I.Session.GetLastTickMilliseconds() : 0.0;

		// Debug draw: snapshot -> islands -> DrawAll -> queue -> renderer.
		FSceneView View = I.Camera.MakeView(Width, Height);
		FDebugDrawSettings DrawSettings;
		FDebugDrawFlags DrawFlags;
		DebugDraw::ReadConsoleVariables(DrawSettings, DrawFlags);
		I.Renderer->BeginView(View, FSphere(View.ViewOrigin, DrawFlags.Radius));
		I.Renderer->DrawGrid();
		if (I.Session.HasScene())
		{
			I.Viewport.BuildFrame(*I.Session.GetScene(), View, *I.Renderer);
			if (!I.bCameraPlaced)
			{
				// The first frame frames the scene; draw it again from the placed camera.
				I.PlaceCameraForScene();
				View = I.Camera.MakeView(Width, Height);
				I.Renderer->BeginView(View, FSphere(View.ViewOrigin, DrawFlags.Radius));
				I.Renderer->DrawGrid();
				I.Viewport.BuildFrame(*I.Session.GetScene(), View, *I.Renderer);
			}
		}
		I.FrameStats.SceneDrawCpuMs = I.Viewport.GetTimings().GetTotalMs();
		I.FrameStats.bWindowed = !I.Options.bHeadless;
		I.FrameStats.bVSync = I.Options.bVSync;

		// Scene pass.
		I.RHI.BeginGpuTimer();
		I.RHI.BeginScene(GClearColor);
		uint64 Begin = FPlatformTime::Cycles64();
		I.Renderer->Flush(DrawFlags.bSeeThrough);
		I.FrameStats.RendererFlushCpuMs = FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - Begin) * 1000.0;
		I.UpdateSnapshotDrawMedian(I.FrameStats.SceneDrawCpuMs + I.FrameStats.RendererFlushCpuMs);
		I.RHI.ResolveSceneToOutput();

		// UI pass (text of DrawDebugString, panels).
		Begin = FPlatformTime::Cycles64();
		FEditorContext Context{ I.Session, I.Viewport, I.Camera, I.Renderer->GetStats(), I.FrameStats, I.RHI.GetAdapterName(), Width, Height, I.RHI.GetMsaaSamples(),
			I.Options.bHeadless };
		FPhysicsTestEditor::DrawWorldStrings(I.Renderer->GetStrings(), View);
		I.Editor.Draw(Context);
		ImGui::Render();
		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
		I.FrameStats.UiCpuMs = FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - Begin) * 1000.0;
		I.RHI.EndGpuTimer();

		++I.FramesRendered;
		I.FrameStats.FramesRendered = I.FramesRendered;
		const bool bLastFrame = I.Options.bExitAfterFrames && I.Options.Frames > 0 && I.FramesRendered >= static_cast<uint64>(I.Options.Frames);
		// The -Screenshot capture (GPU read-back + synchronous PNG encode, tens of ms) is not part of the frame: its time is
		// taken out of FrameMs, so -PerfReport's frame statistics do not depend on -Screenshot.
		uint64 ScreenshotCycles = 0;
		if (bLastFrame && !I.Options.Screenshot.empty())
		{
			const uint64 ScreenshotBegin = FPlatformTime::Cycles64();
			I.TakeScreenshot(I.Options.Screenshot);
			ScreenshotCycles = FPlatformTime::Cycles64() - ScreenshotBegin;
			I.LastScreenshotMs = FPlatformTime::ToSeconds64(ScreenshotCycles) * 1000.0;
		}
		// A user screenshot (F9 / menu / Engines panel) is requested while the UI of THIS frame is drawn, so it is taken here,
		// before Present: with DXGI_SWAP_EFFECT_FLIP_DISCARD the back buffer is undefined after Present. Its cost is not
		// excluded from FrameMs (interactive use only).
		if (I.Editor.ConsumeScreenshotRequest())
		{
			const std::string Name = "Viewer_" + I.Session.GetSelection().Backend + "_" + (I.Session.HasScene() ? I.Session.GetScenario()->GetName() : "none")
				+ "_" + MakeTimestamp() + ".png";
			I.TakeScreenshot(GetRepositoryDir() / "Saved" / "Screenshots" / StringConv::Utf8ToPath(Name));
		}
		I.RHI.Present();
		if (I.Options.bWaitForGpu)
		{
			I.RHI.WaitForGpu();
		}
		I.FrameStats.GpuMs = I.RHI.GetLastGpuMilliseconds();
		I.FrameStats.FrameMs = FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - TickBegin - ScreenshotCycles) * 1000.0;
		const double InstantFps = I.FrameStats.FrameMs > 0.0 ? 1000.0 / I.FrameStats.FrameMs : 0.0;
		I.FrameStats.Fps = I.FrameStats.Fps <= 0.0 ? InstantFps : I.FrameStats.Fps * 0.9 + InstantFps * 0.1;
		if (!I.Options.PerfReport.empty())
		{
			I.SampleFrameMs.push_back(I.FrameStats.FrameMs);
			I.SampleSceneDrawMs.push_back(I.FrameStats.SceneDrawCpuMs);
			I.SampleFlushMs.push_back(I.FrameStats.RendererFlushCpuMs);
			I.SampleUiMs.push_back(I.FrameStats.UiCpuMs);
			I.SampleGpuMs.push_back(I.FrameStats.GpuMs);
			I.SampleSnapshotMs.push_back(I.Viewport.GetTimings().SnapshotMs);
			I.SampleDrawAllMs.push_back(I.Viewport.GetTimings().DrawAllMs);
		}

		// -ViewerCmds: the next command, as if typed into the Console panel during this frame.
		if (I.NextViewerCommand < I.Options.ViewerCommands.size())
		{
			const std::string& Command = I.Options.ViewerCommands[I.NextViewerCommand++];
			UE_LOG(LogViewer, Display, "viewer command: %s", Command.c_str());
			I.Editor.ExecuteConsoleCommand(Command, I.Session);
		}
		I.ProcessRequests();
		if (bLastFrame)
		{
			I.bRequestExit = true;
		}
	}

	void FPhysicsTestViewerApp::FImpl::ProcessRequests()
	{
		FViewerSelection Selection;
		if (Editor.ConsumeOpenRequest(Selection))
		{
			std::string Message;
			const EViewerOpenResult Result = Session.Open(Selection, Message);
			Editor.SetOpenResult(Result, Message, Selection);
			if (Result == EViewerOpenResult::Opened)
			{
				Viewport.SelectedBody = INDEX_NONE;
			}
		}
		if (Editor.ConsumeResetRequest())
		{
			std::string Message;
			const EViewerOpenResult Result = Session.Reset(Message);
			Editor.SetOpenResult(Result, Message, Session.GetSelection());
		}
		if (Editor.ConsumeFocusRequest())
		{
			FBox Bounds;
			const FPhysicsDebugSnapshot& Snapshot = Viewport.GetSnapshot();
			for (const PtParticleSnapshot& Particle : Snapshot.Particles)
			{
				const bool bSelected = Viewport.SelectedBody != INDEX_NONE && Particle.BodyIndex == static_cast<uint32>(Viewport.SelectedBody);
				if (bSelected || (Viewport.SelectedBody == INDEX_NONE && Particle.ObjectState != static_cast<uint8>(EObjectStateType::Static)))
				{
					Bounds += FVector(Particle.BoundsMin.X, Particle.BoundsMin.Y, Particle.BoundsMin.Z);
					Bounds += FVector(Particle.BoundsMax.X, Particle.BoundsMax.Y, Particle.BoundsMax.Z);
				}
			}
			Camera.FocusOn(Bounds, RHI.GetWidth(), RHI.GetHeight());
		}
		std::string CommandLine;
		if (Editor.ConsumeRestartRequest(CommandLine))
		{
			// The restart-required path (03 M4): the same exe with the new selection, then this process exits.
			wchar_t ExePath[MAX_PATH] = {};
			GetModuleFileNameW(nullptr, ExePath, MAX_PATH);
			std::wstring Line = L"\"" + std::wstring(ExePath) + L"\" " + StringConv::Utf8ToWide(CommandLine);
			// Headless (M5): the relaunch is part of an automated run - no console window, this process's stdout / stderr, and
			// this process waits for it and returns its exit code. Windowed: the new viewer runs on its own.
			// M6 (M5 review): the headless child runs in a job object that kills it when this process ends (a CTest timeout kills
			// only the parent), and it inherits only the three standard handles (PROC_THREAD_ATTRIBUTE_HANDLE_LIST).
			STARTUPINFOEXW Startup = {};
			Startup.StartupInfo.cb = sizeof(Startup.StartupInfo);
			PROCESS_INFORMATION Process = {};
			BOOL bInheritHandles = FALSE;
			DWORD CreationFlags = 0;
			HANDLE Job = nullptr;
			std::vector<unsigned char> AttributeStorage;
			LPPROC_THREAD_ATTRIBUTE_LIST Attributes = nullptr;
			HANDLE Inherited[3] = {};
			DWORD NumInherited = 0;
			if (Options.bHeadless)
			{
				Startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
				Startup.StartupInfo.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
				Startup.StartupInfo.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
				Startup.StartupInfo.hStdError = GetStdHandle(STD_ERROR_HANDLE);
				bInheritHandles = TRUE;
				CreationFlags = CREATE_NO_WINDOW | CREATE_SUSPENDED;
				for (const HANDLE Handle : { Startup.StartupInfo.hStdInput, Startup.StartupInfo.hStdOutput, Startup.StartupInfo.hStdError })
				{
					DWORD HandleFlags = 0;
					const bool bUsable = Handle != nullptr && Handle != INVALID_HANDLE_VALUE && GetHandleInformation(Handle, &HandleFlags)
						&& (HandleFlags & HANDLE_FLAG_INHERIT) != 0;
					if (bUsable && std::find(Inherited, Inherited + NumInherited, Handle) == Inherited + NumInherited)
					{
						Inherited[NumInherited++] = Handle;
					}
				}
				SIZE_T AttributeBytes = 0;
				InitializeProcThreadAttributeList(nullptr, 1, 0, &AttributeBytes);
				AttributeStorage.resize(AttributeBytes);
				Attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(AttributeStorage.data());
				if (NumInherited > 0 && InitializeProcThreadAttributeList(Attributes, 1, 0, &AttributeBytes)
					&& UpdateProcThreadAttribute(Attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, Inherited, NumInherited * sizeof(HANDLE), nullptr, nullptr))
				{
					Startup.lpAttributeList = Attributes;
					Startup.StartupInfo.cb = sizeof(Startup);   // EXTENDED_STARTUPINFO_PRESENT needs the STARTUPINFOEXW size
					CreationFlags |= EXTENDED_STARTUPINFO_PRESENT;
				}
				else
				{
					Attributes = nullptr;   // the standard handles are not inheritable: plain inheritance (as in M5)
				}
				Job = CreateJobObjectW(nullptr, nullptr);
				if (Job != nullptr)
				{
					JOBOBJECT_EXTENDED_LIMIT_INFORMATION Limits = {};
					Limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
					SetInformationJobObject(Job, JobObjectExtendedLimitInformation, &Limits, sizeof(Limits));
				}
			}
			UE_LOG(LogViewer, Display, "restarting: %s", CommandLine.c_str());
			std::fflush(stdout);
			std::fflush(stderr);
			if (CreateProcessW(nullptr, Line.data(), nullptr, nullptr, bInheritHandles, CreationFlags, nullptr, nullptr, &Startup.StartupInfo, &Process))
			{
				if (Options.bHeadless)
				{
					if (Job != nullptr && !AssignProcessToJobObject(Job, Process.hProcess))
					{
						UE_LOG(LogViewer, Warning, "restart: AssignProcessToJobObject failed (%lu); the child is not killed with this process", GetLastError());
					}
					ResumeThread(Process.hThread);
				}
				CloseHandle(Process.hThread);
				if (Options.bHeadless)
				{
					WaitForSingleObject(Process.hProcess, INFINITE);
					DWORD ChildExitCode = 1;
					GetExitCodeProcess(Process.hProcess, &ChildExitCode);
					ExitCode = static_cast<int32>(ChildExitCode);
					bRelaunchedHeadless = true;
					UE_LOG(LogViewer, Display, "the relaunched viewer exited with %d", ExitCode);
				}
				CloseHandle(Process.hProcess);
				bRequestExit = true;
			}
			else
			{
				const DWORD Error = GetLastError();
				UE_LOG(LogViewer, Error, "restart failed: CreateProcessW error %lu", Error);
				Editor.AddLog("restart failed: CreateProcessW error " + std::to_string(Error));
			}
			if (Attributes != nullptr)
			{
				DeleteProcThreadAttributeList(Attributes);
			}
			if (Job != nullptr)
			{
				CloseHandle(Job);
			}
		}
		if (Editor.ConsumeExitRequest())
		{
			bRequestExit = true;
		}
	}

	void FPhysicsTestViewerApp::FImpl::UpdateSnapshotDrawMedian(const double Milliseconds)
	{
		// Stats shows the single frame and the median of the last GRollingFrames frames (the steady value of 03 M4 4b).
		if (RecentSnapshotDrawMs.size() < static_cast<size_t>(GRollingFrames))
		{
			RecentSnapshotDrawMs.push_back(Milliseconds);
		}
		else
		{
			RecentSnapshotDrawMs[RecentSnapshotDrawNext] = Milliseconds;
		}
		RecentSnapshotDrawNext = (RecentSnapshotDrawNext + 1) % static_cast<size_t>(GRollingFrames);
		std::vector<double> Sorted = RecentSnapshotDrawMs;
		const auto Middle = Sorted.begin() + static_cast<std::ptrdiff_t>((Sorted.size() - 1) / 2);
		std::nth_element(Sorted.begin(), Middle, Sorted.end());
		FrameStats.SnapshotDrawMedianMs = *Middle;
		FrameStats.SnapshotDrawMedianFrames = static_cast<int32>(Sorted.size());
	}

	// ---- outputs ------------------------------------------------------------------------------------------------------
	FJson FPhysicsTestViewerApp::FImpl::MakeSceneJson() const
	{
		FJson Scene;
		if (!Session.HasScene())
		{
			return Scene;
		}
		const FViewerSelection& Selection = Session.GetSelection();
		FPhysScene& PhysScene = *Session.GetScene();
		Scene["backend"] = Selection.Backend;
		Scene["backend_display_name"] = PhysScene.GetBackendInfo().DisplayName;
		Scene["variant"] = Session.GetConfigFiles().VariantName;
		Scene["profile"] = Session.GetConfig().ProfileName;
		Scene["scenario"] = Session.GetScenario()->GetName();
		FJson Params = FJson::object();
		for (const auto& [Name, Value] : Session.GetResolvedParams())
		{
			Params[Name] = Value;
		}
		Scene["params"] = Params;
		Scene["seed"] = Session.GetScenario()->GetSeed(Session.GetResolvedParams());
		Scene["scene_sha256"] = Session.GetScenario()->ComputeSceneSha256(Session.GetResolvedParams());
		Scene["desc_sha256"] = Session.GetDescSha256();
		Scene["options_sha256"] = PhysScene.ComputeOptionsSha256();
		Scene["frame"] = PhysScene.GetFrameCount();
		Scene["sim_time_s"] = PhysScene.GetSimTime();
		Scene["num_bodies"] = PhysScene.GetStats().NumBodies;
		Scene["threads"] = Selection.Threads;
		Scene["paused"] = Session.IsPaused();
		return Scene;
	}

	void FPhysicsTestViewerApp::FImpl::WriteViewerManifest(const std::filesystem::path& Path, const char* Reason) const
	{
		// Every manifest the viewer writes is mode viz (03 M4); the analysis stage rejects it (02 11.2).
		FDebugDrawSettings Settings;
		FDebugDrawFlags Flags;
		DebugDraw::ReadConsoleVariables(Settings, Flags);
		FJson Manifest;
		Manifest["schema"] = "physicstest.viewer/1";
		Manifest["mode"] = "viz";
		Manifest["timing_label"] = GVizTimingLabel;
		Manifest["timing_valid_for_headline"] = false;
		Manifest["reason"] = Reason;
		Manifest["utc_local"] = MakeTimestamp();
		Manifest["harness"] = { { "git_sha", PT_BUILD_GIT_SHA }, { "dirty", PT_BUILD_GIT_DIRTY != 0 }, { "preset", PT_BUILD_PRESET },
			{ "configuration", PT_BUILD_CONFIGURATION } };
		Manifest["scene"] = MakeSceneJson();
		Manifest["camera"] = { { "location", { Camera.Location.X, Camera.Location.Y, Camera.Location.Z } }, { "pitch", Camera.Rotation.Pitch },
			{ "yaw", Camera.Rotation.Yaw }, { "fov_deg", Camera.FOVDegrees } };
		Manifest["view"] = { { "width", RHI.GetWidth() }, { "height", RHI.GetHeight() }, { "msaa", RHI.GetMsaaSamples() }, { "adapter", RHI.GetAdapterName() } };
		Manifest["debug_draw"] = { { "enabled", Flags.bEnabled }, { "shapes", Flags.bDrawShapes }, { "collisions", Flags.bDrawCollisions },
			{ "mass", Flags.bDrawMass }, { "bounds", Flags.bDrawBounds }, { "transforms", Flags.bDrawTransforms }, { "islands", Flags.bDrawIslands },
			{ "sleep_state", Flags.bDrawSleepState }, { "colliding_shapes", Flags.bDrawCollidingShapes }, { "joints", Flags.bDrawJoints },
			{ "see_through", Flags.bSeeThrough }, { "max_lines", Flags.MaxLines }, { "radius_cm", Flags.Radius }, { "commands", Viewport.GetNumCommands() },
			{ "rejected_budget", Viewport.GetQueueStats().NumRejectedBudget }, { "rejected_region", Viewport.GetQueueStats().NumRejectedRegion },
			{ "capture_cost", Viewport.GetQueueStats().RequestedCost }, { "render_drawn", Viewport.GetRenderStageStats().NumRendered },
			{ "render_rejected_budget", Viewport.GetRenderStageStats().NumRejectedBudget },
			{ "render_rejected_region", Viewport.GetRenderStageStats().NumRejectedRegion }, { "render_cost", Viewport.GetRenderStageStats().RenderCost },
			{ "settings", { { "inertia_scale", Settings.InertiaScale }, { "line_thickness", Settings.LineThickness } } },
			{ "snapshot_from_host_state", Viewport.GetSnapshot().bFromHostState }, { "contact_points", Viewport.GetSnapshot().Contacts.size() },
			{ "harness_islands", Viewport.HasIslands() ? static_cast<int64>(Viewport.GetIslands().size()) : -1 } };
		const FD3D11DebugDrawStats& Render = Renderer->GetStats();
		Manifest["renderer"] = { { "boxes", Render.NumBoxes }, { "spheres", Render.NumSpheres }, { "capsules", Render.NumCapsules }, { "circles", Render.NumCircles },
			{ "lines", Render.NumLines }, { "points", Render.NumPoints }, { "strings", Render.NumStrings }, { "segments", Render.NumSegments } };
		Manifest["timings_ms_viz"] = { { "physics_frame", FrameStats.PhysicsMs }, { "snapshot", Viewport.GetTimings().SnapshotMs },
			{ "islands", Viewport.GetTimings().IslandsMs }, { "draw_all", Viewport.GetTimings().DrawAllMs }, { "visit", Viewport.GetTimings().VisitMs },
			{ "renderer_flush", FrameStats.RendererFlushCpuMs }, { "ui", FrameStats.UiCpuMs }, { "gpu", FrameStats.GpuMs }, { "frame", FrameStats.FrameMs } };
		std::ofstream File(Path, std::ios::binary | std::ios::trunc);
		File << Manifest.dump(2) << "\n";
	}

	bool FPhysicsTestViewerApp::FImpl::TakeScreenshot(const std::filesystem::path& Path)
	{
		std::string Error;
		if (!RHI.CaptureOutputPng(Path, Error))
		{
			UE_LOG(LogViewer, Error, "screenshot %s: %s", StringConv::PathToUtf8(Path).c_str(), Error.c_str());
			Editor.AddLog("screenshot failed: " + Error);
			ExitCode = 3;
			return false;
		}
		std::filesystem::path ManifestPath = Path;
		ManifestPath += ".json";
		WriteViewerManifest(ManifestPath, "screenshot");
		UE_LOG(LogViewer, Display, "screenshot %s (+ .json, mode viz)", StringConv::PathToUtf8(Path).c_str());
		Editor.AddLog("screenshot " + StringConv::PathToUtf8(Path));
		return true;
	}

	void FPhysicsTestViewerApp::FImpl::WritePerfReport() const
	{
		FJson Report;
		Report["schema"] = "physicstest.viewer.perf/1";
		Report["mode"] = "viz";
		Report["timing_label"] = GVizTimingLabel;
		Report["harness"] = { { "git_sha", PT_BUILD_GIT_SHA }, { "preset", PT_BUILD_PRESET }, { "configuration", PT_BUILD_CONFIGURATION } };
		Report["scene"] = MakeSceneJson();
		Report["view"] = { { "width", RHI.GetWidth() }, { "height", RHI.GetHeight() }, { "msaa", RHI.GetMsaaSamples() }, { "adapter", RHI.GetAdapterName() },
			{ "headless", Options.bHeadless }, { "wait_for_gpu_each_frame", Options.bWaitForGpu }, { "vsync", Options.bVSync && !Options.bHeadless } };
		Report["frames"] = SampleFrameMs.size();
		// The first frames build the buffers and the ImGui font atlas: summarise frames 10.. when there are enough.
		const size_t Skip = SampleFrameMs.size() > 20 ? 10 : 0;
		const auto Tail = [Skip](const std::vector<double>& Values)
		{
			std::vector<double> Out;
			for (size_t Index = Skip; Index < Values.size(); ++Index)
			{
				if (Values[Index] >= 0.0)
				{
					Out.push_back(Values[Index]);
				}
			}
			return Out;
		};
		const FStatsSummary Frame = Summarize(Tail(SampleFrameMs));
		const FStatsSummary SceneDraw = Summarize(Tail(SampleSceneDrawMs));
		const FStatsSummary Flush = Summarize(Tail(SampleFlushMs));
		std::vector<double> SnapshotPlusDraw;
		for (size_t Index = Skip; Index < SampleSceneDrawMs.size(); ++Index)
		{
			SnapshotPlusDraw.push_back(SampleSceneDrawMs[Index] + SampleFlushMs[Index]);
		}
		const FStatsSummary Total = Summarize(SnapshotPlusDraw);
		Report["skipped_first_frames"] = Skip;
		Report["frame"] = ToJson(Frame);
		Report["snapshot"] = ToJson(Summarize(Tail(SampleSnapshotMs)));
		Report["draw_all"] = ToJson(Summarize(Tail(SampleDrawAllMs)));
		Report["scene_draw_cpu"] = ToJson(SceneDraw);
		Report["renderer_flush_cpu"] = ToJson(Flush);
		Report["snapshot_plus_draw_cpu"] = ToJson(Total);
		Report["ui_cpu"] = ToJson(Summarize(Tail(SampleUiMs)));
		Report["gpu"] = ToJson(Summarize(Tail(SampleGpuMs)));
		Report["fps_from_median_frame"] = Frame.Median > 0.0 ? 1000.0 / Frame.Median : 0.0;
		Report["fps_from_p95_frame"] = Frame.P95 > 0.0 ? 1000.0 / Frame.P95 : 0.0;
		// -Screenshot: the capture in the last frame is taken out of that frame's time (Tick); -1 = no screenshot.
		Report["screenshot_ms_excluded_from_frame"] = LastScreenshotMs;
		const FDebugDrawQueueStats& Capture = Viewport.GetQueueStats();
		const FDebugDrawRenderStats& RenderStage = Viewport.GetRenderStageStats();
		Report["debug_draw"] = { { "commands", Viewport.GetNumCommands() }, { "rejected_budget", Capture.NumRejectedBudget },
			{ "rejected_region", Capture.NumRejectedRegion }, { "capture_cost", Capture.RequestedCost }, { "render_drawn", RenderStage.NumRendered },
			{ "render_rejected_budget", RenderStage.NumRejectedBudget }, { "render_rejected_region", RenderStage.NumRejectedRegion },
			{ "render_cost", RenderStage.RenderCost }, { "segments", Renderer->GetStats().NumSegments }, { "boxes", Renderer->GetStats().NumBoxes } };
		std::error_code Ignored;
		if (Options.PerfReport.has_parent_path())
		{
			std::filesystem::create_directories(Options.PerfReport.parent_path(), Ignored);
		}
		std::ofstream File(Options.PerfReport, std::ios::binary | std::ios::trunc);
		File << Report.dump(2) << "\n";
		UE_LOG(LogViewer, Display, "perf (%s): %zu frames, frame median %.3f ms (%.1f fps), snapshot+draw CPU median %.3f ms p95 %.3f ms, GPU median %.3f ms -> %s",
			GVizTimingLabel, SampleFrameMs.size(), Frame.Median, Frame.Median > 0.0 ? 1000.0 / Frame.Median : 0.0, Total.Median, Total.P95,
			Summarize(Tail(SampleGpuMs)).Median, StringConv::PathToUtf8(Options.PerfReport).c_str());
	}

	// ---- Exit ---------------------------------------------------------------------------------------------------------
	void FPhysicsTestViewerApp::Exit()
	{
		FImpl& I = *Impl;
		if (!I.Options.PerfReport.empty() && I.Renderer)
		{
			if (I.bRelaunchedHeadless)
			{
				UE_LOG(LogViewer, Display, "perf: not written by this process - the relaunched viewer wrote %s", StringConv::PathToUtf8(I.Options.PerfReport).c_str());
			}
			else
			{
				I.WritePerfReport();
			}
		}
		I.Session.Close();
		// Modules: every initialised one gets its TermGamePhys (no scene is alive any more), also a module an Open initialised
		// before it failed later (e.g. at the engine-option preflight).
		const int32 NumTerminated = FPhysicsBackendRegistry::Get().TermIdleModules();
		UE_LOG(LogViewer, Display, "TermGamePhys: %d backend module(s)", NumTerminated);
		if (I.bImGuiInitialised)
		{
			ImGui_ImplDX11_Shutdown();
			if (I.bWin32Backend)
			{
				ImGui_ImplWin32_Shutdown();
			}
			ImPlot::DestroyContext();
			ImGui::DestroyContext();
			I.bImGuiInitialised = false;
		}
		I.Renderer.reset();
		I.RHI.Shutdown();
		if (I.Window != nullptr)
		{
			DestroyWindow(I.Window);
			I.Window = nullptr;
		}
	}

	LRESULT CALLBACK FPhysicsTestViewerApp::FImpl::WndProc(HWND Window, const UINT Message, const WPARAM WParam, const LPARAM LParam)
	{
		if (ImGui_ImplWin32_WndProcHandler(Window, Message, WParam, LParam))
		{
			return 1;
		}
		switch (Message)
		{
		case WM_SIZE:
			if (FImpl* App = static_cast<FImpl*>(GAppImpl); App != nullptr && WParam != SIZE_MINIMIZED)
			{
				App->PendingWidth = static_cast<int32>(LOWORD(LParam));
				App->PendingHeight = static_cast<int32>(HIWORD(LParam));
			}
			return 0;
		case WM_SYSCOMMAND:
			if ((WParam & 0xfff0) == SC_KEYMENU)
			{
				return 0;   // no ALT application menu
			}
			break;
		case WM_DESTROY:
			PostQuitMessage(0);
			return 0;
		default:
			break;
		}
		return DefWindowProcW(Window, Message, WParam, LParam);
	}
}
