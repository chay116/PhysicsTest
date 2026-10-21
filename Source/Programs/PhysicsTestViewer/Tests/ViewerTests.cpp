// ViewerTests.cpp - the viewer's renderer-side units (03_WorkPlan M4), built only with PT_WITH_VIEWER (dbg, dev):
//   Camera*      FViewportCameraController: fly mode (RMB + WASD / EQ, acceleration and damping), mouse look and the pitch
//                clamp, the wheel speed step, FOV keys, F framing; FSceneView: hFOV 90 on the major axis, reversed-Z depth
//   Session*     FViewerSession: one physics frame per tick, pause, "one step" = FrameCount + 1, Null <-> Jolt without a
//                restart, the restart-required rules and the relaunch command line
//   Renderer*    FD3D11DebugDrawRenderer offscreen (hardware D3D11, no window): the clear colour, the Chaos state colours in
//                the pixels, see-through vs depth-tested lines
// Synthetic input only exercises the controller math (the user's desktop is never driven).
#include "CoreMinimal.h"

#include "D3D11DebugDrawRenderer.h"
#include "D3D11RHI.h"
#include "DebugDrawSettings.h"
#include "HAL/IConsoleManager.h"
#include "Physics/PhysicsBackend.h"
#include "Physics/PhysicsConsoleVariables.h"
#include "PhysicsDebugDraw.h"
#include "PhysicsTestEditor.h"
#include "PhysicsViewportClient.h"
#include "PhysScene.h"
#include "PTBuildInfo.h"
#include "SceneView.h"
#include "ViewerSession.h"
#include "ViewportCameraController.h"

#include "Misc/PtThirdPartyIncludesStart.h"
#include <doctest/doctest.h>
#include "Misc/PtThirdPartyIncludesEnd.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

using namespace PhysicsTest;

namespace
{
	constexpr double GDt = 1.0 / 60.0;

	void TickFor(FViewportCameraController& Camera, const FViewportInput& Input, const double Seconds)
	{
		const int32 Steps = static_cast<int32>(std::lround(Seconds / GDt));
		for (int32 Step = 0; Step < Steps; ++Step)
		{
			Camera.Tick(Input, GDt);
		}
	}
}

TEST_CASE("PhysicsTest.Viewer.CameraFlyForwardAccelerationAndDamping")
{
	FViewportCameraController Camera;
	Camera.Location = FVector::ZeroVector;
	Camera.Rotation = FRotator(0.0, 90.0, 0.0);   // looking along +Y
	FViewportInput Input;
	Input.bRightMouse = true;
	Input.bMoveForward = true;
	// 20000 cm/s^2 reaches the 1000 cm/s maximum in 0.05 s.
	Camera.Tick(Input, GDt);
	CHECK(Camera.Velocity.Size() == doctest::Approx(FMath::Min(20000.0 * GDt, 1000.0)));
	TickFor(Camera, Input, 1.0);
	CHECK(Camera.Velocity.Size() == doctest::Approx(Camera.GetMaxSpeed()));
	CHECK(Camera.Location.Y > 900.0);   // ~1000 cm/s for ~1 s
	CHECK(std::abs(Camera.Location.X) < 1e-6);
	CHECK(std::abs(Camera.Location.Z) < 1e-6);
	// Release: damping 10 / s, v *= 1 - 10 dt per frame: (5/6)^30 = 0.42 % after 0.5 s, (5/6)^60 < 0.002 % after 1 s.
	Input.bMoveForward = false;
	TickFor(Camera, Input, 0.5);
	CHECK(Camera.Velocity.Size() == doctest::Approx(1000.0 * std::pow(1.0 - 10.0 * GDt, 30)).epsilon(1e-6));
	TickFor(Camera, Input, 0.5);
	CHECK(Camera.Velocity.Size() < 0.1);
}

TEST_CASE("PhysicsTest.Viewer.CameraWorldUpDownAndStrafe")
{
	FViewportCameraController Camera;
	Camera.Location = FVector::ZeroVector;
	Camera.Rotation = FRotator(-45.0, 0.0, 0.0);   // pitched down
	FViewportInput Input;
	Input.bRightMouse = true;
	Input.bMoveUp = true;   // E: world +Z regardless of the pitch
	TickFor(Camera, Input, 0.5);
	CHECK(Camera.Location.Z > 100.0);
	CHECK(std::abs(Camera.Location.X) < 1e-6);
	CHECK(std::abs(Camera.Location.Y) < 1e-6);
	Camera.Location = FVector::ZeroVector;
	Camera.Velocity = FVector::ZeroVector;
	Input.bMoveUp = false;
	Input.bMoveRight = true;   // D: camera right (+Y at yaw 0)
	TickFor(Camera, Input, 0.5);
	CHECK(Camera.Location.Y > 100.0);
	CHECK(std::abs(Camera.Location.Z) < 1e-6);
	// Without the right button the keys do nothing.
	const FVector Before = Camera.Location;
	Camera.Velocity = FVector::ZeroVector;
	Input.bRightMouse = false;
	TickFor(Camera, Input, 0.5);
	CHECK(Camera.Location == Before);
}

TEST_CASE("PhysicsTest.Viewer.CameraMouseLookWheelAndFov")
{
	FViewportCameraController Camera;
	Camera.Rotation = FRotator(0.0, 0.0, 0.0);
	FViewportInput Input;
	Input.bRightMouse = true;
	Input.MouseDeltaX = 100.0;
	Input.MouseDeltaY = 40.0;   // mouse down -> look down
	Camera.Tick(Input, GDt);
	CHECK(Camera.Rotation.Yaw == doctest::Approx(100.0 * FViewportCameraController::MouseLookDegreesPerPixel));
	CHECK(Camera.Rotation.Pitch == doctest::Approx(-40.0 * FViewportCameraController::MouseLookDegreesPerPixel));
	Input.MouseDeltaX = 0.0;
	Input.MouseDeltaY = -10000.0;
	Camera.Tick(Input, GDt);
	CHECK(Camera.Rotation.Pitch == doctest::Approx(FViewportCameraController::MaxPitchDegrees));
	// Wheel in fly mode: +-10 % speed per notch.
	Input.MouseDeltaY = 0.0;
	Input.WheelNotches = 1.0;
	Camera.Tick(Input, GDt);
	CHECK(Camera.SpeedScale == doctest::Approx(1.1));
	Input.WheelNotches = -2.0;
	Camera.Tick(Input, GDt);
	CHECK(Camera.SpeedScale == doctest::Approx(1.1 / 1.21));
	// C zooms in (smaller FOV), Z out.
	Input.WheelNotches = 0.0;
	Input.bZoomIn = true;
	TickFor(Camera, Input, 0.5);
	CHECK(Camera.FOVDegrees == doctest::Approx(90.0 - 30.0));
	Input.bZoomIn = false;
	Input.bZoomOut = true;
	TickFor(Camera, Input, 0.5);
	CHECK(Camera.FOVDegrees == doctest::Approx(90.0));
	// Without the right button the wheel dollies along the view direction.
	FViewportCameraController Dolly;
	Dolly.Location = FVector::ZeroVector;
	Dolly.Rotation = FRotator(0.0, 0.0, 0.0);
	FViewportInput Wheel;
	Wheel.WheelNotches = 2.0;
	Dolly.Tick(Wheel, GDt);
	CHECK(Dolly.Location.X == doctest::Approx(2.0 * FViewportCameraController::DollyCentimetresPerNotch));
}

TEST_CASE("PhysicsTest.Viewer.SceneViewProjectionAndFocus")
{
	FViewportCameraController Camera;
	Camera.Location = FVector::ZeroVector;
	Camera.Rotation = FRotator(0.0, 0.0, 0.0);
	FSceneView View = Camera.MakeView(1600, 900);
	// hFOV 90 on the major (X) axis: a point 45 degrees to the right lands on the right edge.
	double X = 0.0;
	double Y = 0.0;
	REQUIRE(View.ProjectWorldToPixel(FVector(1000.0, 1000.0, 0.0), X, Y));
	CHECK(X == doctest::Approx(1600.0));
	CHECK(Y == doctest::Approx(450.0));
	CHECK(View.ProjScaleX == doctest::Approx(1.0));
	CHECK(View.ProjScaleY == doctest::Approx(1600.0 / 900.0));
	// UE axes: +Z world is up on screen, +Y world is right.
	REQUIRE(View.ProjectWorldToPixel(FVector(1000.0, 0.0, 100.0), X, Y));
	CHECK(Y < 450.0);
	// Reversed-Z, infinite far: clip z / w = near / depth (1 at the near plane, -> 0 far away).
	const auto Depth = [&View](const FVector& P)
	{
		const FVector R = P - View.ViewOrigin;
		double Clip[4] = {};
		for (int32 Row = 0; Row < 4; ++Row)
		{
			Clip[Row] = View.ViewProjRelative[Row][0] * R.X + View.ViewProjRelative[Row][1] * R.Y + View.ViewProjRelative[Row][2] * R.Z + View.ViewProjRelative[Row][3];
		}
		return Clip[2] / Clip[3];
	};
	CHECK(Depth(FVector(10.0, 0.0, 0.0)) == doctest::Approx(1.0));
	CHECK(Depth(FVector(1000.0, 0.0, 0.0)) == doctest::Approx(0.01));
	CHECK(Depth(FVector(1.0e7, 0.0, 0.0)) > 0.0);
	REQUIRE_FALSE(View.ProjectWorldToPixel(FVector(-100.0, 0.0, 0.0), X, Y));   // behind the camera
	// Portrait viewport: the 90 degrees go to the vertical (major) axis.
	const FSceneView Portrait = Camera.MakeView(600, 1000);
	CHECK(Portrait.ProjScaleY == doctest::Approx(1.0));
	// Deproject inverts project.
	FVector Origin;
	FVector Direction;
	View.DeprojectPixel(1600.0, 450.0, Origin, Direction);
	CHECK(Direction.X == doctest::Approx(std::sqrt(0.5)));
	CHECK(Direction.Y == doctest::Approx(std::sqrt(0.5)));
	// F: the bounding sphere of the selection fits the smaller FOV, the centre is on the view axis.
	FViewportCameraController Focus;
	Focus.Rotation = FRotator(-30.0, 45.0, 0.0);
	const FBox Bounds(FVector(-50.0, -50.0, 0.0), FVector(50.0, 50.0, 100.0));
	Focus.FocusOn(Bounds, 1600, 900);
	const FSceneView FocusView = Focus.MakeView(1600, 900);
	REQUIRE(FocusView.ProjectWorldToPixel(Bounds.GetCenter(), X, Y));
	CHECK(X == doctest::Approx(800.0));
	CHECK(Y == doctest::Approx(450.0));
	for (const FVector& Corner : { Bounds.Min, Bounds.Max, FVector(Bounds.Min.X, Bounds.Max.Y, Bounds.Min.Z), FVector(Bounds.Max.X, Bounds.Min.Y, Bounds.Max.Z) })
	{
		REQUIRE(FocusView.ProjectWorldToPixel(Corner, X, Y));
		CHECK((X >= 0.0 && X <= 1600.0 && Y >= 0.0 && Y <= 900.0));
	}
}

namespace
{
	// Null, and Jolt in a PT_WITH_JOLT build: such a build must list Jolt in the registry, so a broken registration fails
	// here instead of the switch test quietly running Null <-> Null.
	std::vector<std::string> GetViewerTestBackends()
	{
		std::vector<std::string> Backends = { "Null" };
		const std::vector<std::string> Available = PhysicsTest::FPhysicsBackendRegistry::Get().GetAvailableNames();
		const bool bListed = std::find(Available.begin(), Available.end(), "Jolt") != Available.end();
		if constexpr (PT_BUILD_WITH_JOLT != 0)
		{
			REQUIRE_MESSAGE(bListed, "PT_WITH_JOLT build, but the backend registry does not list Jolt");
		}
		if (bListed)
		{
			Backends.push_back("Jolt");
		}
		return Backends;
	}
}

// 03 M4: "Null ↔ Jolt 전환이 재시작 없이 된다. 일시정지 중 '한 스텝'은 FrameCount를 정확히 1 올린다."
TEST_CASE("PhysicsTest.Viewer.SessionPauseStepAndSwitch")
{
	const std::vector<std::string> Backends = GetViewerTestBackends();
	FViewerSession Session;
	std::string Message;
	// Null -> Jolt -> Null -> Jolt in one process, each followed by the pause / step check.
	for (const std::string& Backend : { Backends.front(), Backends.back(), Backends.front(), Backends.back() })
	{
		CAPTURE(Backend);
		FViewerSelection Selection;
		Selection.Backend = Backend;
		Selection.Scenario = "M1.SingleBoxDrop";
		REQUIRE_MESSAGE(Session.Open(Selection, Message) == EViewerOpenResult::Opened, Message);
		REQUIRE(Session.GetScene()->GetBackendName() == Backend);
		CHECK(Session.GetConfig().TestSettings.RunMode == EPhysicsRunMode::Viz);   // viewer runs are always viz
		CHECK(Session.GetFrameCount() == 0u);
		// Running: one physics frame per tick.
		for (int32 Tick = 0; Tick < 3; ++Tick)
		{
			CHECK(Session.TickPhysics() == 1);
		}
		CHECK(Session.GetFrameCount() == 3u);
		// Paused: nothing moves, whatever the number of ticks.
		Session.SetPaused(true);
		for (int32 Tick = 0; Tick < 5; ++Tick)
		{
			CHECK(Session.TickPhysics() == 0);
		}
		CHECK(Session.GetFrameCount() == 3u);
		// One step: exactly +1, then paused again.
		Session.RequestSingleStep();
		for (int32 Tick = 0; Tick < 4; ++Tick)
		{
			Session.TickPhysics();
		}
		CHECK(Session.GetFrameCount() == 4u);
		CHECK(Session.IsPaused());
		Session.RequestSingleStep();
		Session.RequestSingleStep();   // two requests before a tick are still one step
		Session.TickPhysics();
		Session.TickPhysics();
		CHECK(Session.GetFrameCount() == 5u);
		Session.SetPaused(false);
		// Reset rebuilds the scene at frame 0.
		REQUIRE(Session.Reset(Message) == EViewerOpenResult::Opened);
		CHECK(Session.GetFrameCount() == 0u);
	}
	MESSAGE("switched " << Backends.front() << " <-> " << Backends.back() << " 4 times in one process without a restart");
}

// The Console does not set a harness physics CVar under a live scene (FPhysScene would treat the change as fatal at its next
// SpawnBody / FlushDeferredActors); it still shows the value, and the viewer's own CVars (p.Chaos.*) are set as before.
TEST_CASE("PhysicsTest.Viewer.ConsoleHarnessCVarsReadOnly")
{
	FViewerSession Session;
	std::string Message;
	FViewerSelection Selection;
	Selection.Backend = "Null";
	Selection.Scenario = "M1.SingleBoxDrop";
	REQUIRE_MESSAGE(Session.Open(Selection, Message) == EViewerOpenResult::Opened, Message);
	IConsoleVariable* Nudge = PhysicsCVars::FindHarnessConsoleVariable("p.ComNudgeAffectsInertia");
	REQUIRE(Nudge != nullptr);
	const std::string Before = Nudge->GetString();
	FPhysicsTestEditor Editor;
	CHECK_FALSE(Editor.ExecuteConsoleCommand("p.ComNudgeAffectsInertia 0", Session));
	CHECK_FALSE(Editor.ExecuteConsoleCommand("p.comnudgeaffectsinertia=0", Session));   // UE names are case-insensitive
	CHECK(Nudge->GetString() == Before);
	CHECK(Editor.ExecuteConsoleCommand("p.ComNudgeAffectsInertia", Session));          // showing it is fine
	IConsoleVariable* Islands = IConsoleManager::Get().FindConsoleVariable("p.Chaos.Solver.DebugDrawIslands");
	REQUIRE(Islands != nullptr);
	const std::string IslandsBefore = Islands->GetString();
	CHECK(Editor.ExecuteConsoleCommand("p.Chaos.Solver.DebugDrawIslands 1", Session));
	CHECK(Islands->GetBool());
	Islands->Set(IslandsBefore.c_str(), ECVF_SetByConsole);
	// The scene keeps running.
	CHECK(Session.TickPhysics() == 1);
	CHECK(Session.GetFrameCount() == 1u);
}

TEST_CASE("PhysicsTest.Viewer.RestartRequiredRules")
{
	std::string Reason;
	FBackendSwitchState State;
	CHECK(ClassifyBackendSwitch(State, Reason) == EViewerOpenResult::Opened);   // a fresh module
	// GLOBAL_SCENE_SETTINGS (PhysX): the same init options switch in process, other ones need a restart.
	State.bInitialized = true;
	State.ModuleFlags = PT_MODULE_GLOBAL_SCENE_SETTINGS;
	State.CurrentInitOptions = { { "physx.ToleranceLength", "100" } };
	State.WantedInitOptions = { { "physx.ToleranceLength", "100" } };
	CHECK(ClassifyBackendSwitch(State, Reason) == EViewerOpenResult::Opened);
	State.WantedInitOptions = { { "physx.ToleranceLength", "1" } };
	CHECK(ClassifyBackendSwitch(State, Reason) == EViewerOpenResult::RestartRequired);
	CHECK(Reason.find("GLOBAL_SCENE_SETTINGS") != std::string::npos);
	// A module without the flag reuses its first initialisation.
	State.ModuleFlags = 0;
	CHECK(ClassifyBackendSwitch(State, Reason) == EViewerOpenResult::Opened);
	// A second UE_RUNTIME module (Chaos) and an InitGamePhys refusal of a one-per-process module need a restart.
	FBackendSwitchState Chaos;
	Chaos.bLoadRefusedOnePerProcess = true;
	CHECK(ClassifyBackendSwitch(Chaos, Reason) == EViewerOpenResult::RestartRequired);
	FBackendSwitchState Refused;
	Refused.bInitGamePhysRefused = true;
	Refused.ModuleFlags = PT_MODULE_ONE_PER_PROCESS | PT_MODULE_NO_REINIT;
	CHECK(ClassifyBackendSwitch(Refused, Reason) == EViewerOpenResult::RestartRequired);
	Refused.ModuleFlags = 0;
	CHECK(ClassifyBackendSwitch(Refused, Reason) == EViewerOpenResult::Failed);   // a plain init failure: a restart would not help
	// The relaunch command line carries the whole selection.
	FViewerSelection Selection;
	Selection.Backend = "PhysX";
	Selection.BackendProfile = "SdkDefault";
	Selection.PhysicsProfile = "UEParity";
	Selection.Scenario = "M3.BoxPile";
	Selection.Params = { { "N", "5000" } };
	Selection.Threads = 4;
	CHECK(FViewerSession::MakeCommandLine(Selection, "-NoVSync") == "-Backend=PhysX -BackendProfile=SdkDefault -Profile=UEParity -Scenario=M3.BoxPile -Param:N=5000 -Threads=4 -NoVSync");
	// ... plus the session switches of the running process, verbatim (quotes kept); selection and run control are not
	// carried over (the new selection replaces the old one, a restart is an interactive session).
	const char* const Running = "-Backend=Jolt -Scenario=M1.SingleBoxDrop -Param:N=10 -ini:Engine:[/Script/Engine.PhysicsSettings]:DefaultGravityZ=-490 "
		"-ExecCmds=\"p.Chaos.Solver.DebugDrawCollisions 1, p.Chaos.DebugDraw.Radius 0\" -ConfigDir=\"D:/My Config\" -ResX=1280 -ResY=720 -NoVSync -Headless "
		"-Frames=60 -Exit -Paused -WarmupFrames=10 -Screenshot=\"C:/a b.png\" -Camera=1,2,3,4,5 -UI=Stats,Engines -D3DDebug";
	const std::string Forwarded = FViewerSession::GetForwardedSwitches(Running);
	CHECK(Forwarded == "-ini:Engine:[/Script/Engine.PhysicsSettings]:DefaultGravityZ=-490 -ExecCmds=\"p.Chaos.Solver.DebugDrawCollisions 1, p.Chaos.DebugDraw.Radius 0\" "
		"-ConfigDir=\"D:/My Config\" -ResX=1280 -ResY=720 -NoVSync -UI=Stats,Engines -D3DDebug");
	const std::string Relaunch = FViewerSession::MakeCommandLine(Selection, Forwarded);
	std::string ExecCmds;
	CHECK(FParse::Value(Relaunch.c_str(), "ExecCmds=", ExecCmds, false));
	CHECK(ExecCmds == "p.Chaos.Solver.DebugDrawCollisions 1, p.Chaos.DebugDraw.Radius 0");
	std::string Backend;
	CHECK(FParse::Value(Relaunch.c_str(), "Backend=", Backend));
	CHECK(Backend == "PhysX");   // the new selection, not the running one
	CHECK_FALSE(FParse::Param(Relaunch.c_str(), "Headless"));
	MESSAGE("relaunch: " << Relaunch);
	// Values with spaces or commas are quoted; a double quote cannot pass FParse and is dropped.
	FViewerSelection Odd = Selection;
	Odd.Params = { { "Label", "a \"b\", c" } };
	CHECK(FViewerSession::MakeCommandLine(Odd, "").find("-Param:Label=\"a b, c\"") != std::string::npos);
	// A ONE_PER_PROCESS / NO_REINIT module keeps its first thread budget: another MaxThreads needs a restart.
	FBackendSwitchState Threads;
	Threads.bInitialized = true;
	Threads.ModuleFlags = PT_MODULE_NO_REINIT;
	Threads.CurrentMaxThreads = 1;
	Threads.WantedMaxThreads = 4;
	CHECK(ClassifyBackendSwitch(Threads, Reason) == EViewerOpenResult::RestartRequired);
	CHECK(Reason.find("MaxThreads") != std::string::npos);
	Threads.WantedMaxThreads = 1;
	CHECK(ClassifyBackendSwitch(Threads, Reason) == EViewerOpenResult::Opened);
	Threads.ModuleFlags = 0;   // other modules reuse their first initialisation (the registry logs a warning)
	Threads.WantedMaxThreads = 4;
	CHECK(ClassifyBackendSwitch(Threads, Reason) == EViewerOpenResult::Opened);
	// The registry reports why a load failed as a value, not only as text.
	std::string LoadError;
	EBackendLoadFailure Failure = EBackendLoadFailure::None;
	CHECK(FPhysicsBackendRegistry::Get().FindOrLoadModule("NoSuchEngine", LoadError, &Failure) == nullptr);
	CHECK(Failure == EBackendLoadFailure::UnknownBackend);
	CHECK(FPhysicsBackendRegistry::Get().FindOrLoadModule("Null", LoadError, &Failure) != nullptr);
	CHECK(Failure == EBackendLoadFailure::None);
	// The Engines panel lists come from Config/.
	const std::vector<std::string> Variants = FViewerSession::ListBackendVariants("Jolt");
	CHECK(std::find(Variants.begin(), Variants.end(), "Default") != Variants.end());
	const std::vector<std::string> Profiles = FViewerSession::ListPhysicsProfiles();
	CHECK(std::find(Profiles.begin(), Profiles.end(), "UEParity") != Profiles.end());
	CHECK(std::find(Profiles.begin(), Profiles.end(), "Normalized") != Profiles.end());
	// An unknown scenario or backend fails cleanly (no Fatal).
	FViewerSession Session;
	std::string Message;
	FViewerSelection Bad;
	Bad.Scenario = "No.Such";
	CHECK(Session.Open(Bad, Message) == EViewerOpenResult::Failed);
	Bad.Scenario = "S00.FreeFall";
	Bad.Backend = "NoSuchEngine";
	CHECK(Session.Open(Bad, Message) == EViewerOpenResult::Failed);
}

namespace
{
	// Counts the pixels of each exact colour (RGB) of an RGBA8 image.
	std::map<uint32, int64> CountColors(const std::vector<uint8>& Rgba)
	{
		std::map<uint32, int64> Counts;
		for (size_t Pixel = 0; Pixel + 3 < Rgba.size(); Pixel += 4)
		{
			++Counts[(static_cast<uint32>(Rgba[Pixel]) << 16) | (static_cast<uint32>(Rgba[Pixel + 1]) << 8) | Rgba[Pixel + 2]];
		}
		return Counts;
	}
	int64 CountOf(const std::map<uint32, int64>& Counts, const FColor& Color)
	{
		const auto Found = Counts.find((static_cast<uint32>(Color.R) << 16) | (static_cast<uint32>(Color.G) << 8) | Color.B);
		return Found != Counts.end() ? Found->second : 0;
	}
}

// The renderer end to end, offscreen: M1.SingleBoxDrop on Null and Jolt through the viewport client; the pixels hold the
// non-black clear colour (40, 42, 46), the dynamic box yellow (255, 255, 0) and the static ground box red (255, 0, 0).
TEST_CASE("PhysicsTest.Viewer.RendererOffscreenColors")
{
	FD3D11RHI RHI;
	FD3D11RHICreateParams Params;
	Params.Width = 640;
	Params.Height = 360;
	std::string Error;
	if (!RHI.Init(Params, Error))
	{
		// doctest has no run-time skip: the CTest entry PhysicsTest.Unit.ViewerRenderer maps this marker to "skipped"
		// (SKIP_REGULAR_EXPRESSION), so a GPU-less host does not count the case as passed.
		MESSAGE("PT_SKIPPED: no hardware D3D11 device (" << Error << ")");
		return;
	}
	FD3D11DebugDrawRenderer Renderer(RHI);
	REQUIRE_MESSAGE(Renderer.Init(Error), Error);
	const std::vector<std::string> Backends = GetViewerTestBackends();
	for (const std::string& Backend : Backends)
	{
		CAPTURE(Backend);
		FViewerSession Session;
		FViewerSelection Selection;
		Selection.Backend = Backend;
		std::string Message;
		REQUIRE_MESSAGE(Session.Open(Selection, Message) == EViewerOpenResult::Opened, Message);
		FViewportCameraController Camera;
		Camera.LookAt(FVector(-400.0, -250.0, 300.0), FVector(0.0, 0.0, 100.0));
		const FSceneView View = Camera.MakeView(RHI.GetWidth(), RHI.GetHeight());
		FPhysicsViewportClient Viewport;
		DebugDraw::ResetConsoleVariables();
		Renderer.BeginView(View, FSphere(View.ViewOrigin, 3000.0));
		Renderer.DrawGrid();
		Viewport.BuildFrame(*Session.GetScene(), View, Renderer);
		RHI.BeginGpuTimer();
		RHI.BeginScene(FColor(40, 42, 46));
		Renderer.Flush(true);
		RHI.ResolveSceneToOutput();
		RHI.EndGpuTimer();
		std::vector<uint8> Pixels;
		int32 Width = 0;
		int32 Height = 0;
		REQUIRE(RHI.ReadOutputPixels(Pixels, Width, Height));
		const std::map<uint32, int64> Counts = CountColors(Pixels);
		const int64 Clear = CountOf(Counts, FColor(40, 42, 46));
		const int64 Yellow = CountOf(Counts, FColor(255, 255, 0));
		const int64 Red = CountOf(Counts, FColor(255, 0, 0));
		MESSAGE(Backend << ": clear " << Clear << ", yellow (dynamic box) " << Yellow << ", red (static ground) " << Red << " of " << Width * Height
			<< " px; boxes " << Renderer.GetStats().NumBoxes << ", segments " << Renderer.GetStats().NumSegments);
		CHECK(Renderer.GetStats().NumBoxes == 2);   // the ground and the cube
		CHECK(Clear > Width * Height / 2);
		CHECK(Yellow > 100);
		CHECK(Red > 100);
		CHECK(CountOf(Counts, FColor(0, 0, 0)) == 0);   // nothing black (no sleeping island drawn, the clear is not black)
	}
}
