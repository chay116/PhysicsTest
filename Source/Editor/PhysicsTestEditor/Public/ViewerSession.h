// ViewerSession.h - FViewerSession: the one scene of the viewer and its life cycle (02_Architecture 11.2, 03 M4). [MVP M4]
// Renderer-free (unit tests PhysicsTest.Viewer.Session*). A selection (backend, variant, physics profile, scenario,
// parameters, threads) is resolved like a bench run (LoadBenchConfig: Base -> profile -> variant -> -ini: / -ExecCmds) and
// then switched to RunMode=Viz, so every manifest the viewer writes says mode viz and SDK backends install their
// viz-only listeners (Jolt's contact listener). Opening a selection destroys the previous scene first; backend modules stay
// initialised for the life of the process (a module is never TermGamePhys'd and re-initialised: PhysX allows one
// InitGamePhys, 02 8.2).
// Switching that one process cannot do is reported as RestartRequired instead of failing (03 M4):
//   - the target module is a second UE_RUNTIME module (ONE_PER_PROCESS: Chaos);
//   - the target module has GLOBAL_SCENE_SETTINGS and is already initialised with other init options (PhysX SdkDefault vs
//     the UE4Compat family: one PxPhysics / PxTolerancesScale per process, PxPhysics.h:997, PxSceneDesc.h:541 [V]);
//   - InitGamePhys of a ONE_PER_PROCESS / NO_REINIT module refuses in this process;
//   - a ONE_PER_PROCESS / NO_REINIT module is already initialised with another MaxThreads (the thread budget is fixed at
//     InitGamePhys, Chaos' -corelimit is process-wide; the registry would only warn and keep the first one).
// The app then relaunches itself with MakeCommandLine(selection, GetForwardedSwitches(command line)) and exits.
#pragma once

#include "BenchmarkRunner.h"
#include "CoreTypes.h"
#include "Physics/PhysicsProfile.h"
#include "PhysicsTestScenario.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace PhysicsTest
{
	class FPhysScene;
	class FPhysicsBackendModule;

	struct FViewerSelection
	{
		std::string Backend = "Jolt";
		std::string BackendProfile;          // "" = <Backend>.Default when that file exists, else none
		std::string PhysicsProfile = "Normalized";
		std::string Scenario = "M1.SingleBoxDrop";
		FScenarioParams Params;              // only the overrides; the scenario's defaults fill the rest
		int32 Threads = 1;

		[[nodiscard]] bool operator==(const FViewerSelection&) const = default;
	};

	enum class EViewerOpenResult : uint8
	{
		Opened,
		RestartRequired,
		Failed,
	};

	[[nodiscard]] const char* ToString(EViewerOpenResult Result);

	// What decides between "switch in this process" and "restart required" (pure, unit-tested).
	struct FBackendSwitchState
	{
		bool bLoadRefusedOnePerProcess = false;   // FindOrLoadModule refused a second UE_RUNTIME module
		bool bInitialized = false;                // the target module is already initialised in this process
		uint32 ModuleFlags = 0;                   // PT_MODULE_*
		std::vector<std::pair<std::string, std::string>> CurrentInitOptions;   // when initialised
		std::vector<std::pair<std::string, std::string>> WantedInitOptions;
		bool bInitGamePhysRefused = false;        // InitGamePhys failed when the session tried it
		uint32 CurrentMaxThreads = 0;             // when initialised
		uint32 WantedMaxThreads = 0;
	};
	[[nodiscard]] EViewerOpenResult ClassifyBackendSwitch(const FBackendSwitchState& State, std::string& OutReason);

	class FViewerSession
	{
	public:
		FViewerSession();
		~FViewerSession();
		FViewerSession(const FViewerSession&) = delete;
		FViewerSession& operator=(const FViewerSession&) = delete;

		// Builds the selection's scene (the previous one is destroyed first unless the result is RestartRequired / Failed
		// before anything changed). OutMessage explains RestartRequired / Failed.
		EViewerOpenResult Open(const FViewerSelection& Selection, std::string& OutMessage);
		// Rebuilds the current selection (frame 0).
		EViewerOpenResult Reset(std::string& OutMessage);
		void Close();

		void SetPaused(bool bInPaused) { bPaused = bInPaused; }
		[[nodiscard]] bool IsPaused() const { return bPaused; }
		// While paused: the next TickPhysics runs exactly one frame.
		void RequestSingleStep() { bSingleStepRequested = true; }
		// One FPhysScene::Tick(FixedDeltaTime) unless paused (a requested single step runs even while paused). Returns the
		// number of frames run (0 or 1).
		int32 TickPhysics();

		[[nodiscard]] bool HasScene() const { return Scene != nullptr; }
		[[nodiscard]] FPhysScene* GetScene() const { return Scene.get(); }
		[[nodiscard]] IPhysicsTestScenario* GetScenario() const { return Scenario; }
		[[nodiscard]] const FViewerSelection& GetSelection() const { return Selection; }
		[[nodiscard]] const FScenarioParams& GetResolvedParams() const { return ResolvedParams; }
		[[nodiscard]] const FResolvedPhysicsConfig& GetConfig() const { return *Config; }
		[[nodiscard]] const FBenchConfig& GetConfigFiles() const { return ConfigFiles; }
		[[nodiscard]] uint64 GetFrameCount() const;
		[[nodiscard]] double GetLastTickMilliseconds() const { return LastTickMilliseconds; }   // wall time of the last physics frame
		[[nodiscard]] const std::string& GetDescSha256() const { return DescSha256; }

		// The command line of a fresh viewer process with this selection (the restart path). Extra is appended verbatim.
		// A value with spaces or commas is quoted; a double quote inside a value cannot pass FParse (no escape) and is dropped.
		[[nodiscard]] static std::string MakeCommandLine(const FViewerSelection& Selection, const std::string& Extra);
		// The switches of CommandLine that configure the session rather than select it, verbatim (quoting kept), for the
		// restarted process: -ini:..., -ExecCmds=, -ConfigDir=, -ResX=, -ResY=, -NoVSync, -D3DDebug,
		// -ChaosDll=, -UI=, -NoUI. The selection (-Backend, -Profile, ...) comes from MakeCommandLine; run control
		// (-Headless, -Frames, -Exit, -Paused, -WarmupFrames, -Screenshot, -PerfReport, -Camera) is not carried over.
		[[nodiscard]] static std::string GetForwardedSwitches(const char* CommandLine);
		// Config/Profiles/Backend/<Backend>.<Variant>.ini -> the variant names; Config/Profiles/<Name>.ini -> the profile names.
		[[nodiscard]] static std::vector<std::string> ListBackendVariants(const std::string& Backend);
		[[nodiscard]] static std::vector<std::string> ListPhysicsProfiles();

	private:
		std::unique_ptr<FPhysScene> Scene;
		std::unique_ptr<FResolvedPhysicsConfig> Config;
		FBenchConfig ConfigFiles;
		IPhysicsTestScenario* Scenario = nullptr;
		FViewerSelection Selection;
		FScenarioParams ResolvedParams;
		std::string DescSha256;
		double LastTickMilliseconds = 0.0;
		bool bPaused = false;
		bool bSingleStepRequested = false;
	};
}
