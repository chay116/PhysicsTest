// PhysXBackendTests.cpp - PhysX-specific unit tests (03_WorkPlan M5; built only with PT_WITH_PHYSX). The shared SDK
// suites (SdkBackendTests, Abi.SdkBackendsA0toA7, ScenarioTests) take PhysX unchanged through the generated registry
// (without -BackendProfile=: PhysX.UE4Compat, the DefaultForEngine variant); this file holds what only PhysX has: the four variants, the
// process-wide tolerances scale, the CPU dispatchers, the UE4Compat readback, the filter shader truth table, the error
// callback and the kept contacts of sleeping islands.
// PT_PHYSX_IN_REGISTRY mirrors the CMake target property PT_IN_BACKEND_REGISTRY of PtBackendPhysX
// (PhysicsTestAutomation/CMakeLists.txt).
//
// The PhysX module is GLOBAL_SCENE_SETTINGS: one set of init options (ToleranceScale) per initialisation. The shared suites
// initialise it without options; the tests here use the variants' options and therefore terminate an idle PhysX module
// whose options differ before they build a scene (FPhysXModuleScope), and terminate it again when they are done, so the
// order of the test cases does not matter.
#include "CoreMinimal.h"

#include "BenchmarkRunner.h"
#include "Containers/StringConv.h"
#include "Fixtures/FreeFallFixture.h"
#include "Physics/EffectiveSettings.h"
#include "Physics/PhysicsBackend.h"
#include "Physics/PhysicsDebugSnapshot.h"
#include "Physics/PhysicsProfile.h"
#include "Physics/PhysicsInterface.h"
#include "PhysicsTestScenario.h"
#include "PhysScene.h"
#include "PhysXBackend/PhysXBackendEntry.h"
#include "PhysXBackend/PhysXBackendTestHooks.h"

#include "Misc/PtThirdPartyIncludesStart.h"
#include <doctest/doctest.h>
#include "Misc/PtThirdPartyIncludesEnd.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#ifndef PT_PHYSX_IN_REGISTRY
	#error "PT_PHYSX_IN_REGISTRY must be defined by PhysicsTestAutomation/CMakeLists.txt"
#endif

using namespace PhysicsTest;

namespace
{
	constexpr const char* GVariants[] = { "UE4Compat", "UE4Compat-TGS", "UE4Compat-Stab", "SdkDefault" };

	std::filesystem::path PhysXTestConfigDir()
	{
		std::string Override;
		if (FParse::Value(FCommandLine::Get(), "ConfigDir=", Override))
		{
			return StringConv::Utf8ToPath(Override);
		}
		return StringConv::Utf8ToPath(PT_CONFIG_DIR);
	}

	// Base -> profile -> Config/Profiles/Backend/PhysX.<Variant>.ini (the bench's resolution with -BackendProfile=).
	FResolvedPhysicsConfig LoadPhysXConfig(const std::string& Variant, const std::string& Profile)
	{
		FBenchRunSpec Spec;
		Spec.Backend = "PhysX";
		Spec.PhysicsProfile = Profile;
		Spec.BackendProfile = Variant;
		Spec.ConfigDir = PhysXTestConfigDir();
		FResolvedPhysicsConfig Config;
		FBenchConfig Files;
		std::string Error;
		const bool bLoaded = LoadBenchConfig(Spec, Config, Files, Error);
		INFO(Error);
		REQUIRE(bLoaded);
		return Config;
	}

	FPhysicsBackendModule& GetPhysXModule()
	{
		std::string Error;
		FPhysicsBackendModule* Module = FPhysicsBackendRegistry::Get().FindOrLoadModule("PhysX", Error);
		REQUIRE_MESSAGE(Module != nullptr, Error);
		return *Module;
	}

	// An idle PhysX module initialised with other init options than Wanted is terminated, so that the next FPhysScene can
	// initialise it with Wanted (the registry refuses a second set of options while it is initialised).
	void PreparePhysXModule(const std::vector<std::pair<std::string, std::string>>& Wanted)
	{
		FPhysicsBackendModule& Module = GetPhysXModule();
		if (Module.IsInitialized() && Module.GetInitParams().InitOptions != Wanted)
		{
			REQUIRE(Module.GetNumLiveScenes() == 0);
			REQUIRE(Module.TermGamePhys() == PT_OK);
		}
	}

	// Leaves the PhysX module terminated when it was initialised with variant options, so that the shared suites (no
	// options) can initialise it again whatever the test order is.
	struct FPhysXModuleScope
	{
		FPhysXModuleScope() = default;
		FPhysXModuleScope(const FPhysXModuleScope&) = delete;
		FPhysXModuleScope& operator=(const FPhysXModuleScope&) = delete;
		~FPhysXModuleScope()
		{
			std::string Error;
			FPhysicsBackendModule* Module = FPhysicsBackendRegistry::Get().FindOrLoadModule("PhysX", Error);
			if (Module != nullptr && Module->IsInitialized() && Module->GetNumLiveScenes() == 0 && !Module->GetInitParams().InitOptions.empty())
			{
				Module->TermGamePhys();
			}
		}
	};

	std::unique_ptr<FPhysScene> MakePhysXScenario(const char* ScenarioName, const FScenarioParams& Overrides, const FResolvedPhysicsConfig& Config)
	{
		PreparePhysXModule(Config.InitOptions);
		IPhysicsTestScenario* Scenario = FScenarioRegistry::Get().Find(ScenarioName);
		REQUIRE(Scenario != nullptr);
		FScenarioParams Params;
		std::string Error;
		REQUIRE_MESSAGE(Scenario->ResolveParams(Overrides, Params, Error), Error);
		const FScenarioCapacity Capacity = Scenario->GetCapacity(Params);
		FPhysSceneInitParams InitParams;
		InitParams.DebugName = ScenarioName;
		InitParams.BackendName = "PhysX";
		InitParams.Config = &Config;
		InitParams.MaxBodiesHint = Capacity.MaxBodies;
		InitParams.MaxBodyPairsHint = Capacity.MaxBodyPairs;
		InitParams.MaxContactConstraintsHint = Capacity.MaxContactConstraints;
		auto Scene = std::make_unique<FPhysScene>(InitParams);
		FRandomStream Random(Scenario->GetSeed(Params));
		const FScenarioContext Context{ Params, Random, &Config };
		Scenario->BuildScene(*Scene, Context);
		Scene->PrepareForSimulation();
		return Scene;
	}

	std::vector<FEffectiveSetting> ReadEffective(const FPhysScene& Scene)
	{
		std::vector<FEffectiveSetting> Settings;
		std::string Error;
		const bool bParsed = ParseEffectiveSettings(Scene.DumpEffectiveSettings(), Settings, Error);
		INFO(Error);
		REQUIRE(bParsed);
		return Settings;
	}

	const FEffectiveSetting& RequireSetting(const std::vector<FEffectiveSetting>& Settings, const char* Key)
	{
		const auto Found = std::find_if(Settings.begin(), Settings.end(), [Key](const FEffectiveSetting& S) { return S.Key == Key; });
		const std::string KeyText(Key);
		INFO(KeyText);
		REQUIRE(Found != Settings.end());
		return *Found;
	}

	double Canonical(const std::vector<FEffectiveSetting>& Settings, const char* Key)
	{
		const FEffectiveSetting& Setting = RequireSetting(Settings, Key);
		const std::string KeyText(Key);
		INFO(KeyText);
		REQUIRE(Setting.bHasCanonical);
		return Setting.Canonical;
	}

	bool IsFiniteState(const FBodyInstanceState& State)
	{
		const FVector T = State.BodyTM.GetTranslation();
		return std::isfinite(T.X) && std::isfinite(T.Y) && std::isfinite(T.Z) && std::isfinite(State.LinearVelocity.Size()) && std::isfinite(State.AngularVelocityInRadians.Size());
	}

	std::string Hex(const uint64 Value)
	{
		char Text[24];
		std::snprintf(Text, sizeof(Text), "0x%016llx", static_cast<unsigned long long>(Value));
		return Text;
	}

	uint32 TickFrames(FPhysScene& Scene, const int32 Frames)
	{
		uint32 Bits = 0;
		for (int32 Frame = 0; Frame < Frames; ++Frame)
		{
			Scene.Tick(Scene.GetTestSettings().FixedDeltaTime);
			Bits |= Scene.GetLastFrameTiming().EngineErrorBits;
		}
		return Bits;
	}
}

// The static entry answers this harness's ABI major only and names the module; the generated registry lists the backend
// exactly when its CMake target says so.
TEST_CASE("PhysicsTest.PhysXBackend.Registered")
{
	const PtBackendApi* Api = PT_GetBackendApi_PhysX(PT_ABI_VERSION_MAJOR);
	REQUIRE(Api != nullptr);
	CHECK(Api->StructSize == sizeof(PtBackendApi));
	CHECK(Api->AbiVersion == PT_ABI_VERSION);
	CHECK(std::string(Api->ModuleName) == "PhysX");
	CHECK(PT_GetBackendApi_PhysX(PT_ABI_VERSION_MAJOR + 1u) == nullptr);

	PtBackendInfo Info = {};
	Info.StructSize = sizeof(PtBackendInfo);
	REQUIRE(Api->GetInfo(&Info) == PT_OK);
	CHECK(std::string(Info.Name) == "PhysX");
	CHECK(std::string(Info.BuildFlagsJson).find("\"backend\":\"PhysX\"") != std::string::npos);
	// One PxPhysics and one PxTolerancesScale per process (02 7.5): the registry and the viewer read this flag.
	CHECK((Info.ModuleFlags & PT_MODULE_GLOBAL_SCENE_SETTINGS) != 0u);
	CHECK((Info.ModuleFlags & PT_MODULE_NO_REINIT) == 0u);   // gate A0 re-initialises
	CHECK(std::string(Info.DisplayName).find("UE4 settings on PhysX 5.11") != std::string::npos);

	const std::vector<std::string> Names = FPhysicsBackendRegistry::Get().GetAvailableNames();
	const bool bListed = std::find(Names.begin(), Names.end(), "PhysX") != Names.end();
	CHECK(bListed == (PT_PHYSX_IN_REGISTRY != 0));
	if (bListed)
	{
		std::string Error;
		FPhysicsBackendModule* Module = FPhysicsBackendRegistry::Get().FindOrLoadModule("PhysX", Error);
		REQUIRE_MESSAGE(Module != nullptr, Error);
		CHECK_FALSE(Module->IsDll());   // the static path
		CHECK(&Module->GetApi() == Api);
		CHECK(Module->GetInfo().Name == "PhysX");
	}
}

// 03 M5: every variant creates and steps S00.FreeFall, M1.SingleBoxDrop a/b and M3.BoxPile 1k/5k under UEParity and
// Normalized through the ABI; S00: |z60 - 501.83328| <= 2e-3 cm. (The full-length runs are the bench's, Progress M5.)
TEST_CASE("PhysicsTest.PhysXBackend.VariantsRunTheM5Scenes")
{
	const FPhysXModuleScope Scope;
#if defined(_DEBUG)
	constexpr int32 PileFrames = 5;   // the PhysX debug libraries are slow; the frame count does not change what is checked
#else
	constexpr int32 PileFrames = 30;
#endif
	for (const char* Variant : GVariants)
	{
		for (const char* Profile : { "UEParity", "Normalized" })
		{
			const std::string Label = std::string(Variant) + " / " + Profile;
			CAPTURE(Label);
			const FResolvedPhysicsConfig Config = LoadPhysXConfig(Variant, Profile);
			CHECK(Config.VariantName == Variant);
			{
				std::unique_ptr<FPhysScene> Scene = MakePhysXScenario("S00.FreeFall", {}, Config);
				CHECK(TickFrames(*Scene, Fixtures::FFreeFall::Frames) == 0u);
				const double Z60 = Scene->GetBodies()[0]->State.BodyTM.GetTranslation().Z;
				const double V60 = Scene->GetBodies()[0]->State.LinearVelocity.Z;
				MESSAGE(Label << " S00.FreeFall: z60 = " << Z60 << " cm (|dz| " << std::abs(Z60 - 501.83328) << "), v60 = " << V60 << " cm/s");
				CHECK(std::abs(Z60 - 501.83328) <= 2e-3);
				CHECK(std::abs(V60 + 980.00005) <= 1e-2);
			}
			for (const char* Case : { "a", "b" })
			{
				std::unique_ptr<FPhysScene> Scene = MakePhysXScenario("M1.SingleBoxDrop", { { "Case", Case } }, Config);
				CHECK(TickFrames(*Scene, 120) == 0u);
				const FBodyInstance& Box = *Scene->GetBodies()[1];
				CHECK(IsFiniteState(Box.State));
				const double Z = Box.State.BodyTM.GetTranslation().Z;
				MESSAGE(Label << " M1.SingleBoxDrop " << std::string(Case) << ": box z = " << Z << " cm after 120 frames, state " << static_cast<int32>(Box.State.ObjectState));
				if (std::string(Case) == "a")
				{
					CHECK(Z == doctest::Approx(50.0).epsilon(0.05));   // resting on the ground (half edge 50 cm)
				}
				else
				{
					CHECK((Z > 20.0 && Z < 150.0));
				}
			}
			for (const char* N : { "1000", "5000" })
			{
				std::unique_ptr<FPhysScene> Scene = MakePhysXScenario("M3.BoxPile", { { "N", N } }, Config);
				CHECK(Scene->GetBodies().size() == static_cast<size_t>(std::stoi(N)) + 1u);
				CHECK(TickFrames(*Scene, PileFrames) == 0u);
				bool bAllFinite = true;
				for (const FBodyInstance* Body : Scene->GetBodies())
				{
					bAllFinite = bAllFinite && IsFiniteState(Body->State);
				}
				CHECK(bAllFinite);
				const std::vector<FEffectiveSetting> Settings = ReadEffective(*Scene);
				CHECK(Canonical(Settings, "shape.wrapping") == 0.0);
				CHECK(RequireSetting(Settings, "physx.fidelity").Native.find("UE4 settings on PhysX 5.11") != std::string::npos);
			}
		}
	}
}

// 03 M5: UE4Compat, -TGS and -Stab are three PxScenes of one PxPhysics in one process; SdkDefault after UE4Compat is refused
// with an error and no crash (the registry for the init options, CreateScene for the scene's units, the kit for a second
// InitGamePhys); after TermGamePhys the module initialises with SdkDefault's (1, 10).
TEST_CASE("PhysicsTest.PhysXBackend.UE4CompatFamilySharesOneProcess")
{
	const FPhysXModuleScope Scope;
	const FResolvedPhysicsConfig Compat = LoadPhysXConfig("UE4Compat", "UEParity");
	const FResolvedPhysicsConfig Tgs = LoadPhysXConfig("UE4Compat-TGS", "UEParity");
	const FResolvedPhysicsConfig Stab = LoadPhysXConfig("UE4Compat-Stab", "UEParity");
	const FResolvedPhysicsConfig SdkDefault = LoadPhysXConfig("SdkDefault", "UEParity");
	// The family states no init key: (100, 1000) is the backend's InitGamePhys default, so PhysX without a variant shares the
	// process too (M5 integration: the registry compares init options as text). SdkDefault states (1, 10).
	CHECK(Compat.InitOptions.empty());
	REQUIRE_FALSE(SdkDefault.InitOptions.empty());
	CHECK(Compat.InitOptions == Tgs.InitOptions);
	CHECK(Compat.InitOptions == Stab.InitOptions);
	CHECK(Compat.InitOptions != SdkDefault.InitOptions);
	CHECK(Compat.SceneOptions != Tgs.SceneOptions);
	CHECK(Compat.SceneOptions != Stab.SceneOptions);

	FPhysicsBackendModule& Module = GetPhysXModule();
	{
		std::unique_ptr<FPhysScene> Scenes[3] = { MakePhysXScenario("M1.SingleBoxDrop", { { "Case", "a" } }, Compat),
			MakePhysXScenario("M1.SingleBoxDrop", { { "Case", "a" } }, Tgs), MakePhysXScenario("M1.SingleBoxDrop", { { "Case", "a" } }, Stab) };
		CHECK(Module.GetNumLiveScenes() == 3);
		for (int32 Frame = 0; Frame < 120; ++Frame)
		{
			for (std::unique_ptr<FPhysScene>& Scene : Scenes)
			{
				Scene->Tick(Scene->GetTestSettings().FixedDeltaTime);
				CHECK(Scene->GetLastFrameTiming().EngineErrorBits == 0u);
			}
		}
		const char* const ExpectedSolver[3] = { "ePGS, stabilization off", "eTGS, stabilization off", "ePGS, stabilization on" };
		for (int32 Index = 0; Index < 3; ++Index)
		{
			CAPTURE(Index);
			const std::vector<FEffectiveSetting> Settings = ReadEffective(*Scenes[Index]);
			CHECK(RequireSetting(Settings, "solver.type").Native.find(ExpectedSolver[Index]) != std::string::npos);
			CHECK(Canonical(Settings, "physx.tolerances_length") == 100.0);
			CHECK(Canonical(Settings, "physx.tolerances_speed") == 1000.0);
			const double Z = Scenes[Index]->GetBodies()[1]->State.BodyTM.GetTranslation().Z;
			MESSAGE(std::string(ExpectedSolver[Index]) << ": box at rest at z = " << Z << " cm");
			CHECK(Z == doctest::Approx(50.0).epsilon(0.05));
		}

		// 1. The host registry: SdkDefault's init options while the module holds the UE4Compat family's.
		FBackendInitParams Params;
		Params.InitOptions = SdkDefault.InitOptions;
		std::string Error;
		CHECK(FPhysicsBackendRegistry::Get().FindOrLoad("PhysX", Params, Error) == nullptr);
		MESSAGE("registry: " << Error);
		CHECK(Error.find("GLOBAL_SCENE_SETTINGS") != std::string::npos);
		// 2. The backend: a scene with SdkDefault's units (LengthScale 0.01) under the (100, 1000) PxPhysics.
		std::vector<PtKeyValue> Options;
		for (const auto& [Key, Value] : SdkDefault.SceneOptions)
		{
			Options.push_back(PtKeyValue{ Key.c_str(), Value.c_str() });
		}
		PtSceneDesc Desc = {};
		Desc.StructSize = sizeof(PtSceneDesc);
		Desc.RunMode = PT_RUN_BENCH;
		Desc.Gravity = PtVec3{ 0.0, 0.0, -980.0 };
		Desc.NominalDeltaTime = 1.0 / 60.0;
		Desc.AsyncFixedTimeStepSize = PT_ASYNC_DT_SYNC;
		Desc.RestitutionThreshold = -1.0;
		Desc.NumThreads = 1;
		Desc.DebugName = "SdkDefaultUnderUE4Compat";
		Desc.Options = Options.data();
		Desc.NumOptions = static_cast<uint32_t>(Options.size());
		CHECK(FPhysicsBackendScene::Create(Module, Desc, Error) == nullptr);
		MESSAGE("CreateScene: " << Error);
		CHECK(Error.find("one tolerances scale per process") != std::string::npos);
		// 3. The kit: a second InitGamePhys of the initialised module.
		std::vector<PtKeyValue> InitOptions;
		for (const auto& [Key, Value] : SdkDefault.InitOptions)
		{
			InitOptions.push_back(PtKeyValue{ Key.c_str(), Value.c_str() });
		}
		PtInitParams RawInit = {};
		RawInit.StructSize = sizeof(PtInitParams);
		RawInit.MaxThreads = 1;
		RawInit.Options = InitOptions.data();
		RawInit.NumOptions = static_cast<uint32_t>(InitOptions.size());
		CHECK(Module.GetApi().InitGamePhys(&RawInit) == PT_E_BAD_STATE);
		// Nothing of that touched the three scenes.
		for (std::unique_ptr<FPhysScene>& Scene : Scenes)
		{
			CHECK(TickFrames(*Scene, 10) == 0u);
		}
		CHECK(Module.GetNumLiveScenes() == 3);
	}
	// A fresh PxPhysics (TermGamePhys released the old one) takes SdkDefault's scale: its "own process".
	std::unique_ptr<FPhysScene> Scene = MakePhysXScenario("S00.FreeFall", {}, SdkDefault);
	CHECK(TickFrames(*Scene, Fixtures::FFreeFall::Frames) == 0u);
	const std::vector<FEffectiveSetting> Settings = ReadEffective(*Scene);
	CHECK(Canonical(Settings, "physx.tolerances_length") == 1.0);
	CHECK(Canonical(Settings, "physx.tolerances_speed") == 10.0);
	CHECK(std::abs(Scene->GetBodies()[0]->State.BodyTM.GetTranslation().Z - 501.83328) <= 2e-3);
}

// 03 M5: the M3.BoxPile 5k run on FPhysXCPUDispatcherSingleThread ends on the same native hash as on
// PxDefaultCpuDispatcherCreate(0) after 600 frames, and the single-thread dispatcher never re-enters deeper than 1. The
// inline dispatcher behind the counting proxy shows the recursion the LIFO stack avoids (negative control).
TEST_CASE("PhysicsTest.PhysXBackend.DispatcherMatchesInlineDefault")
{
	const FPhysXModuleScope Scope;
#if defined(_DEBUG)
	const char* const N = "1000";   // dbg: the PhysX debug libraries need ~100 ms per 5k frame; the full run is the dev / test one
	constexpr int32 Frames = 120;
#else
	const char* const N = "5000";
	constexpr int32 Frames = 600;
#endif
	const FResolvedPhysicsConfig Config = LoadPhysXConfig("UE4Compat", "UEParity");
	FResolvedPhysicsConfig Inline = Config;
	Inline.SceneOptions.emplace_back("CpuDispatcher", "PxDefault");   // the last value of a key wins
	struct FRun
	{
		uint64 Hash = 0;
		uint64 HashAt100 = 0;
		double MaxReentrancy = -1.0;
		std::string Dispatcher;
		uint32 ErrorBits = 0;
	};
	const auto Run = [&](const FResolvedPhysicsConfig& RunConfig) -> FRun
	{
		std::unique_ptr<FPhysScene> Scene = MakePhysXScenario("M3.BoxPile", { { "N", N } }, RunConfig);
		FRun Result;
		Result.ErrorBits |= TickFrames(*Scene, 100);
		Result.HashAt100 = Scene->ComputeNativeStateHash();
		Result.ErrorBits |= TickFrames(*Scene, Frames - 100);
		Result.Hash = Scene->ComputeNativeStateHash();
		const std::vector<FEffectiveSetting> Settings = ReadEffective(*Scene);
		Result.MaxReentrancy = Canonical(Settings, "physx.dispatcher_max_reentrancy");
		Result.Dispatcher = RequireSetting(Settings, "physx.cpu_dispatcher").Native;
		return Result;
	};
	const FRun Single = Run(Config);
	const FRun Default = Run(Inline);
	MESSAGE("M3.BoxPile " << std::string(N) << ", " << Frames << " frames: single-thread h_native " << Hex(Single.Hash) << ", PxDefaultCpuDispatcherCreate(0) "
		<< Hex(Default.Hash) << " (at frame 100: " << Hex(Single.HashAt100) << " / " << Hex(Default.HashAt100) << ")");
	MESSAGE("  " << Single.Dispatcher);
	MESSAGE("  " << Default.Dispatcher);
	CHECK(Single.ErrorBits == 0u);
	CHECK(Default.ErrorBits == 0u);
	CHECK(Single.Dispatcher.find("FPhysXCPUDispatcherSingleThread") != std::string::npos);
	CHECK(Default.Dispatcher.find("PxDefaultCpuDispatcherCreate(0)") != std::string::npos);
	CHECK(Single.HashAt100 == Default.HashAt100);
	CHECK(Single.Hash == Default.Hash);
	CHECK(Single.MaxReentrancy <= 1.0);
	CHECK(Single.MaxReentrancy == 1.0);   // PhysX does release follow-up tasks inside run() / release(): the stack is used
	CHECK(Default.MaxReentrancy > 1.0);   // the inline dispatcher recurses along the task chain
}

// T >= 2: PxDefaultCpuDispatcherCreate(T) (02 9.6); the worker count is read back from the dispatcher.
TEST_CASE("PhysicsTest.PhysXBackend.WorkerPoolDispatcher")
{
	const FPhysXModuleScope Scope;
	const FResolvedPhysicsConfig Config = LoadPhysXConfig("UE4Compat", "Normalized");
	FResolvedPhysicsConfig Threaded = Config;
	Threaded.TestSettings.NumThreads = 2;   // the scene's PtSceneDesc::NumThreads (the module keeps its first MaxThreads)
	std::unique_ptr<FPhysScene> Single = MakePhysXScenario("M3.BoxPile", { { "N", "1000" } }, Config);
	std::unique_ptr<FPhysScene> Pool = MakePhysXScenario("M3.BoxPile", { { "N", "1000" } }, Threaded);
	CHECK(TickFrames(*Single, 60) == 0u);
	CHECK(TickFrames(*Pool, 60) == 0u);
	const std::vector<FEffectiveSetting> Settings = ReadEffective(*Pool);
	CHECK(Canonical(Settings, "threads.workers") == 2.0);
	CHECK(RequireSetting(Settings, "threads.workers").Source == "READBACK");
	CHECK(RequireSetting(Settings, "physx.cpu_dispatcher").Native.find("PxDefaultCpuDispatcherCreate(2)") != std::string::npos);
	CHECK(Canonical(ReadEffective(*Single), "threads.workers") == 0.0);
	// PhysX's result does not depend on the worker count here (01_Feasibility 3.2 saw the same on its hello scene).
	const uint64 SingleHash = Single->ComputeNativeStateHash();
	const uint64 PoolHash = Pool->ComputeNativeStateHash();
	MESSAGE("BoxPile 1k Normalized 60 frames: h_native T=1 " << Hex(SingleHash) << ", T=2 " << Hex(PoolHash));
	CHECK(SingleHash == PoolHash);
}

// 03 M5: UE4Compat readback - contact offset 2 cm (50 cm cube) / 8 cm (10 m box), tolerances (100, 1000), bounce 200 cm/s,
// sleep 50 / stabilisation 25 (x SleepFamily). SdkDefault reads back PhysX's own values. Every value here is READBACK.
TEST_CASE("PhysicsTest.PhysXBackend.UE4CompatReadback")
{
	const FPhysXModuleScope Scope;
	struct FExpected
	{
		const char* Variant;
		double ContactMin, ContactMax, Length, Speed, SleepMin, SleepMax, StabMin, StabMax, Iterations;
		const char* BroadPhase;
	};
	// SdkDefault: 0.02 x 1 m = 2 cm on every shape; thresholds 5e-5 / 2.5e-5 x (10 m/s)^2 = 50 / 25 cm^2/s^2 whatever the
	// SleepFamily (no UE4 multiplier).
	const FExpected Cases[] = {
		{ "UE4Compat", 2.0, 8.0, 100.0, 1000.0, 2.5, 50.0, 25.0, 25.0, 8.0, "eSAP" },
		{ "SdkDefault", 2.0, 2.0, 1.0, 10.0, 50.0, 50.0, 25.0, 25.0, 4.0, "ePABP" },
	};
	for (const FExpected& Case : Cases)
	{
		const std::string VariantText(Case.Variant);
		CAPTURE(VariantText);
		const FResolvedPhysicsConfig Config = LoadPhysXConfig(Case.Variant, "UEParity");
		PreparePhysXModule(Config.InitOptions);
		FPhysSceneInitParams InitParams;
		InitParams.DebugName = "PhysXReadback";
		InitParams.BackendName = "PhysX";
		InitParams.Config = &Config;
		FPhysScene Scene(InitParams);
		FBodySetup Big;
		Big.AggGeom.BoxElems.emplace_back(1000.0);   // a 10 m box: half extent 500 cm -> 0.02 x 500 = 10 -> clamp 8 cm
		const FBodySetup* BigSetup = Scene.CreateBodySetup(std::move(Big));
		FBodySetup Small;
		Small.AggGeom.BoxElems.emplace_back(50.0);   // a 50 cm cube: 0.02 x 25 = 0.5 -> clamp 2 cm
		const FBodySetup* SmallSetup = Scene.CreateBodySetup(std::move(Small));
		FBodyInstance Template;
		FProfileResolver::ApplyBodyInstanceDefaults(Config, Template);
		FBodyInstance Ground = Template;
		Ground.bStatic = true;
		Ground.bSimulatePhysics = false;
		Scene.SpawnBody(Ground, BigSetup, FTransform(FVector(0.0, 0.0, -500.0)));
		FBodyInstance Cube = Template;
		Cube.bSimulatePhysics = true;
		const FBodyInstance* Normal = Scene.SpawnBody(Cube, SmallSetup, FTransform(FVector(0.0, 0.0, 1000.0)));
		Cube.SleepFamily = ESleepFamily::Sensitive;   // x 1/20 (p.SensitiveSleepThresholdMultiplier)
		Scene.SpawnBody(Cube, SmallSetup, FTransform(FVector(200.0, 0.0, 1000.0)));
		Scene.FlushDeferredActors();
		const std::vector<FEffectiveSetting> Settings = ReadEffective(Scene);
		for (const char* Key : { "physx.contact_offset_min", "physx.contact_offset_max", "physx.tolerances_length", "physx.tolerances_speed",
			"contact.restitution_threshold", "physx.sleep_threshold_min", "physx.sleep_threshold_max", "physx.stabilization_threshold_min",
			"physx.stabilization_threshold_max", "solver.iterations", "broadphase.type", "world.gravity", "body.damping", "body.max_angular_velocity",
			"sleep.enabled", "threads.workers", "shape.wrapping", "physx.cpu_dispatcher" })
		{
			const std::string KeyText(Key);
			CAPTURE(KeyText);
			CHECK(RequireSetting(Settings, Key).Source == "READBACK");
		}
		MESSAGE(VariantText << ": contact offsets " << RequireSetting(Settings, "physx.contact_offset_max").Native << "; " << RequireSetting(Settings, "sleep.criteria").Native);
		CHECK(Canonical(Settings, "physx.contact_offset_min") == doctest::Approx(Case.ContactMin).epsilon(1e-6));
		CHECK(Canonical(Settings, "physx.contact_offset_max") == doctest::Approx(Case.ContactMax).epsilon(1e-6));
		CHECK(Canonical(Settings, "physx.tolerances_length") == Case.Length);
		CHECK(Canonical(Settings, "physx.tolerances_speed") == Case.Speed);
		CHECK(Canonical(Settings, "contact.restitution_threshold") == doctest::Approx(200.0).epsilon(1e-6));
		CHECK(Canonical(Settings, "physx.sleep_threshold_min") == doctest::Approx(Case.SleepMin).epsilon(1e-5));
		CHECK(Canonical(Settings, "physx.sleep_threshold_max") == doctest::Approx(Case.SleepMax).epsilon(1e-5));
		CHECK(Canonical(Settings, "physx.stabilization_threshold_min") == doctest::Approx(Case.StabMin).epsilon(1e-5));
		CHECK(Canonical(Settings, "physx.stabilization_threshold_max") == doctest::Approx(Case.StabMax).epsilon(1e-5));
		CHECK(Canonical(Settings, "solver.iterations") == Case.Iterations);
		CHECK(RequireSetting(Settings, "broadphase.type").Native.find(Case.BroadPhase) != std::string::npos);
		CHECK(Canonical(Settings, "world.gravity") == doctest::Approx(-980.0).epsilon(1e-6));
		CHECK(Canonical(Settings, "body.max_angular_velocity") == doctest::Approx(3600.0 * 3.14159265358979323846 / 180.0).epsilon(1e-6));   // UEParity 62.83 rad/s
		CHECK(Canonical(Settings, "errors.capacity_bits") == 16.0);   // eOUT_OF_MEMORY
		// The rules themselves are verified against the engine: every shape holds the variant's contact offset rule, every
		// PxMaterial its static friction rule.
		CHECK(RequireSetting(Settings, "contact.offset_rule").Source == "READBACK");
		CHECK(RequireSetting(Settings, "contact.offset_rule").Native.find("3 of 3 shapes") != std::string::npos);
		CHECK(RequireSetting(Settings, "material.static_friction_rule").Source == "READBACK");
		CHECK(RequireSetting(Settings, "material.static_friction_rule").Native.find(VariantText == "UE4Compat" ? "equals_dynamic" : "chaos_pair") != std::string::npos);
		CHECK(RequireSetting(Settings, "material.static_friction_rule").Native.find("1 of 1 PxMaterials hold it") != std::string::npos);
		// The combine modes are read back from the PxMaterials too (03 M5 3a, M5 round 0), and the [M] filter rules from the scene.
		for (const char* Key : { "material.friction_combine", "material.restitution_combine", "physx.filter_shader_rules" })
		{
			const std::string KeyText(Key);
			CAPTURE(KeyText);
			CHECK(RequireSetting(Settings, Key).Source == "READBACK");
		}
		CHECK(RequireSetting(Settings, "material.friction_combine").Native.find("1 of 1 PxMaterials hold the host's mode") != std::string::npos);
		CHECK(RequireSetting(Settings, "material.restitution_combine").Native.find("1 of 1 PxMaterials hold the host's mode") != std::string::npos);
		// canonical = the engine objects that do not hold the rule (docs/Abi.md 5.14 rule keys; G9 compares it with 0).
		for (const char* Key : { "contact.offset_rule", "material.static_friction_rule", "material.friction_combine", "material.restitution_combine" })
		{
			const std::string KeyText(Key);
			CAPTURE(KeyText);
			CHECK(Canonical(Settings, Key) == 0.0);
		}
		CHECK(Canonical(Settings, "physx.filter_shader_rules") == 0.0);   // Kill / UnlessKinematicPairs (every variant file)
		// Adapter counters are SET_ONLY (docs/Abi.md 5.14).
		for (const char* Key : { "physx.start_asleep_skipped", "physx.error_bits", "physx.dispatcher_max_reentrancy" })
		{
			const std::string KeyText(Key);
			CAPTURE(KeyText);
			CHECK(RequireSetting(Settings, Key).Source != "READBACK");
		}
		// Mass readback (checkpoint A): PhysX keeps the harness mass (UE's mass^0.75 rule, 01_Feasibility 3.2: 37.384 kg).
		PtMassProperties Mass = {};
		REQUIRE(Scene.GetBackendScene().ReadMassProperties(Normal->ActorHandle.Index, std::span<PtMassProperties>(&Mass, 1)) == PT_OK);
		MESSAGE(VariantText << ": 50 cm cube mass " << Mass.Mass << " kg (harness " << Normal->MassProperties.Mass << " kg), inertia (" << Mass.InertiaDiagonal.X << ", "
			<< Mass.InertiaDiagonal.Y << ", " << Mass.InertiaDiagonal.Z << ") kg cm^2");
		CHECK(Mass.Mass == doctest::Approx(Normal->MassProperties.Mass).epsilon(1e-6));
		CHECK(Mass.Mass == doctest::Approx(37.384).epsilon(1e-4));
		CHECK(Mass.InertiaDiagonal.X == doctest::Approx(Normal->MassProperties.InertiaTensor.X).epsilon(1e-6));
	}
}

// Lead item (e) (M6): UE4 on PhysX 3.4 had no gyroscopic term, so the UE4Compat family never sets
// eENABLE_GYROSCOPIC_FORCES (GyroscopicForces=Ignore) whatever the body asks; SdkDefault passes the body's request through
// (FromBody). Both profiles disable gyroscopic torque, so G9's body.gyroscopic rule cannot tell the two apart: this case
// asks for it. A free 100 x 50 x 20 cm box spinning about a non-principal axis keeps its angular velocity without the term
// (no torque) and turns it with the term (Euler's equations: dw_y/dt = (Izz - Ixx) / Iyy w_x w_z = 1.85 rad/s^2 here).
TEST_CASE("PhysicsTest.PhysXBackend.GyroscopicForcesPerVariant")
{
	const FPhysXModuleScope Scope;
	for (const char* Variant : GVariants)
	{
		const std::string VariantText(Variant);
		CAPTURE(VariantText);
		const bool bFromBody = VariantText == "SdkDefault";
		const FResolvedPhysicsConfig Config = LoadPhysXConfig(Variant, "UEParity");
		PreparePhysXModule(Config.InitOptions);
		FPhysSceneInitParams InitParams;
		InitParams.DebugName = "PhysXGyroscopic";
		InitParams.BackendName = "PhysX";
		InitParams.Config = &Config;
		FPhysScene Scene(InitParams);
		FBodySetup Box;
		Box.AggGeom.BoxElems.emplace_back(100.0, 50.0, 20.0);
		const FBodySetup* BoxSetup = Scene.CreateBodySetup(std::move(Box));
		FBodyInstance Template;
		FProfileResolver::ApplyBodyInstanceDefaults(Config, Template);
		Template.bSimulatePhysics = true;
		Template.bEnableGravity = false;
		Template.LinearDamping = 0.0f;
		Template.AngularDamping = 0.0f;
		Template.bOverrideMass = true;
		Template.MassInKgOverride = 10.0f;
		Template.bGyroscopicTorqueEnabled = true;
		FBodyInstance* Body = Scene.SpawnBody(Template, BoxSetup, FTransform(FVector(0.0, 0.0, 1000.0)));
		Scene.FlushDeferredActors();
		const std::vector<FEffectiveSetting> Settings = ReadEffective(Scene);
		const FEffectiveSetting& Gyroscopic = RequireSetting(Settings, "body.gyroscopic");
		MESSAGE(VariantText << ": " << Gyroscopic.Native);
		CHECK(Gyroscopic.Source == "READBACK");   // the actor flag, PxRigidBody::getRigidBodyFlags
		CHECK(Canonical(Settings, "body.gyroscopic") == (bFromBody ? 1.0 : 0.0));
		CHECK(Gyroscopic.Native.find(bFromBody ? "1 of 1 dynamic bodies have eENABLE_GYROSCOPIC_FORCES" : "0 of 1 dynamic bodies have eENABLE_GYROSCOPIC_FORCES")
			!= std::string::npos);
		CHECK(Gyroscopic.Native.find(bFromBody ? "GyroscopicForces=FromBody" : "GyroscopicForces=Ignore; 1 requests ignored") != std::string::npos);
		const FVector W0(1.0, 0.0, 1.0);
		FPhysicsInterface::SetAngularVelocity_AssumesLocked(Body->ActorHandle, W0);
		TickFrames(Scene, 30);
		const FVector W = Body->State.AngularVelocityInRadians;
		const double Change = (W - W0).Size();
		MESSAGE(VariantText << ": w (" << W.X << ", " << W.Y << ", " << W.Z << ") rad/s after 30 frames, |dw| " << Change);
		if (bFromBody)
		{
			CHECK(Change > 0.1);   // about 0.9 rad/s of w_y after 0.5 s
		}
		else
		{
			// No torque and no gyroscopic term: w is kept - exactly with PGS; TGS drifts by 2e-5 rad/s over 30 frames (its
			// per-substep integration, also seen in G2's S02), four orders of magnitude below what the term does.
			CHECK(Change < 1e-3);
		}
	}
}

// The UE4 filter shader [M] (02 8.2): word layout and truth table. A kinematic-kinematic pair is suppressed unless a shape
// asks for kinematic pairs; a kinematic-static pair and a pair that does not block mutually follow the variant's two [M]
// rules (PhysicsFiltering.h: NonBlockingPairFilter Kill / Suppress, KinematicStaticPairFilter UnlessKinematicPairs /
// Always; M5 round 0); every other pair is eCONTACT_DEFAULT plus CCD and the notifications on request. The table runs
// without a constant block (the defaults) and with each of the four rule combinations.
TEST_CASE("PhysicsTest.PhysXBackend.FilterShaderTruthTable")
{
	// Word layout: word0 body index, word1 block mask, word2 component id (the body index), word3 [6][5][21].
	uint32_t Words[4] = {};
	PT_PhysXTest_MakeSimFilterData(7u, 0xABCD1234u, 13u, PT_PHYSX_TEST_EPDF_CCD | PT_PHYSX_TEST_EPDF_STATIC_SHAPE, Words);
	CHECK(Words[0] == 7u);
	CHECK(Words[1] == 0xABCD1234u);
	CHECK(Words[2] == 7u);
	CHECK(Words[3] == ((13u << 21) | PT_PHYSX_TEST_EPDF_CCD | PT_PHYSX_TEST_EPDF_STATIC_SHAPE));
	CHECK((Words[3] >> 26) == 0u);           // the 6 extra filter bits (UE's mask filter): none
	CHECK(((Words[3] >> 21) & 0x1Fu) == 13u);   // the channel
	PT_PhysXTest_MakeSimFilterData(0u, 0u, 31u, 0x1FFFFFu, Words);
	CHECK(Words[3] == 0xFFFFFFFFu >> 6);     // channel 31 and all 21 flag bits fill exactly the low 26 bits

	const uint32_t Kinds[] = { PT_PHYSX_TEST_OBJECT_STATIC, PT_PHYSX_TEST_OBJECT_KINEMATIC, PT_PHYSX_TEST_OBJECT_DYNAMIC };
	const uint32_t Channel0 = 3u;
	const uint32_t Channel1 = 17u;
	// Rules: -1 = the plain hook (no constant block, the defaults), else bit 0 NonBlockingPairFilter=Suppress, bit 1
	// KinematicStaticPairFilter=Always.
	for (const int32 Rules : { -1, 0, 1, 2, 3 })
	{
		CAPTURE(Rules);
		const bool bNonBlockingSuppress = Rules > 0 && (Rules & 1) != 0;
		const bool bKinematicStaticAlways = Rules > 0 && (Rules & 2) != 0;
		int32 NumCases = 0;
		int32 NumSuppressed = 0;
		int32 NumKilled = 0;
		int32 NumKept = 0;
		for (const uint32_t Kind0 : Kinds)
		{
			for (const uint32_t Kind1 : Kinds)
			{
				for (int32 BlockCase = 0; BlockCase < 4; ++BlockCase)   // bit 0: 0 blocks 1's channel, bit 1: 1 blocks 0's channel
				{
					for (const uint32_t Flags0 : { 0u, PT_PHYSX_TEST_EPDF_KINEMATIC_KINEMATIC_PAIRS, PT_PHYSX_TEST_EPDF_CCD, PT_PHYSX_TEST_EPDF_CONTACT_NOTIFY })
					{
						for (const uint32_t Flags1 : { 0u, PT_PHYSX_TEST_EPDF_CCD })
						{
							const uint32_t Block0 = (BlockCase & 1) != 0 ? (1u << Channel1) | 0x1u : 0x1u;   // never its own channel only
							const uint32_t Block1 = (BlockCase & 2) != 0 ? (1u << Channel0) | 0x2u : 0x2u;
							PtPhysXTestFilterObject A = { Kind0, {} };
							PtPhysXTestFilterObject B = { Kind1, {} };
							PT_PhysXTest_MakeSimFilterData(1u, Block0, Channel0, Flags0, A.Words);
							PT_PhysXTest_MakeSimFilterData(2u, Block1, Channel1, Flags1, B.Words);
							PtPhysXTestFilterResult Got = {};
							if (Rules < 0)
							{
								PT_PhysXTest_RunSimFilterShader(&A, &B, &Got);
							}
							else
							{
								PT_PhysXTest_RunSimFilterShaderWithRules(&A, &B, bNonBlockingSuppress ? 1u : 0u, bKinematicStaticAlways ? 1u : 0u, &Got);
							}
							// The expected answer, from the rules above.
							const bool bNoDynamic = Kind0 != PT_PHYSX_TEST_OBJECT_DYNAMIC && Kind1 != PT_PHYSX_TEST_OBJECT_DYNAMIC;
							const bool bWithStatic = Kind0 == PT_PHYSX_TEST_OBJECT_STATIC || Kind1 == PT_PHYSX_TEST_OBJECT_STATIC;
							const bool bKinematicPairsAsked = ((Flags0 | Flags1) & PT_PHYSX_TEST_EPDF_KINEMATIC_KINEMATIC_PAIRS) != 0;
							const bool bMutualBlock = BlockCase == 3;
							uint32_t ExpectedFilter = PT_PHYSX_TEST_FILTER_DEFAULT;
							uint32_t ExpectedPair = PT_PHYSX_TEST_PAIR_SOLVE_CONTACT | PT_PHYSX_TEST_PAIR_DETECT_DISCRETE_CONTACT;
							if (bNoDynamic && (!bKinematicPairsAsked || (bWithStatic && bKinematicStaticAlways)))
							{
								ExpectedFilter = PT_PHYSX_TEST_FILTER_SUPPRESS;
							}
							else if (!bMutualBlock)
							{
								ExpectedFilter = bNonBlockingSuppress ? PT_PHYSX_TEST_FILTER_SUPPRESS : PT_PHYSX_TEST_FILTER_KILL;
							}
							else
							{
								ExpectedPair |= ((Flags0 | Flags1) & PT_PHYSX_TEST_EPDF_CCD) != 0 ? PT_PHYSX_TEST_PAIR_DETECT_CCD_CONTACT : 0u;
								ExpectedPair |= ((Flags0 | Flags1) & PT_PHYSX_TEST_EPDF_CONTACT_NOTIFY) != 0
									? PT_PHYSX_TEST_PAIR_NOTIFY_TOUCH_FOUND | PT_PHYSX_TEST_PAIR_NOTIFY_TOUCH_PERSISTS | PT_PHYSX_TEST_PAIR_NOTIFY_CONTACT_POINTS : 0u;
							}
							CAPTURE(Kind0);
							CAPTURE(Kind1);
							CAPTURE(BlockCase);
							CAPTURE(Flags0);
							CAPTURE(Flags1);
							CHECK(Got.FilterFlags == ExpectedFilter);
							if (ExpectedFilter == PT_PHYSX_TEST_FILTER_DEFAULT)
							{
								CHECK(Got.PairFlags == ExpectedPair);
								++NumKept;
							}
							NumSuppressed += ExpectedFilter == PT_PHYSX_TEST_FILTER_SUPPRESS ? 1 : 0;
							NumKilled += ExpectedFilter == PT_PHYSX_TEST_FILTER_KILL ? 1 : 0;
							++NumCases;
						}
					}
				}
			}
		}
		const std::string RulesText = std::string(bNonBlockingSuppress ? "Suppress" : "Kill") + ", kinematic-static "
			+ (bKinematicStaticAlways ? "Always" : "UnlessKinematicPairs");
		MESSAGE("filter shader truth table, rules " << Rules << " (non-blocking " << RulesText << "): " << NumCases << " cases (" << NumKept << " kept, " << NumKilled << " killed, "
			<< NumSuppressed << " suppressed)");
		CHECK(NumCases == 3 * 3 * 4 * 4 * 2);
		CHECK(NumKept > 0);
		CHECK(NumSuppressed > 0);
		CHECK((NumKilled > 0) == !bNonBlockingSuppress);
	}
}

// A PhysX error report (FPhysXErrorCallback) becomes the PxErrorCode bit in the next frame's engine error bits and stays in
// the scene's step stats; the harness test key is a runtime SetOption only, never a scene option.
TEST_CASE("PhysicsTest.PhysXBackend.ErrorReportsBecomeEngineErrorBits")
{
	const FPhysXModuleScope Scope;
	constexpr uint32 InvalidOperation = 0x8u;   // PxErrorCode::eINVALID_OPERATION
	constexpr const char* TestKey = "pt.test.report_error";
	const FResolvedPhysicsConfig Config = LoadPhysXConfig("UE4Compat", "Normalized");
	std::unique_ptr<FPhysScene> Scene = MakePhysXScenario("S00.FreeFall", {}, Config);
	CHECK(TickFrames(*Scene, 1) == 0u);
	REQUIRE(Scene->GetBackendScene().SetOption(TestKey, "1") == PT_OK);
	CHECK(TickFrames(*Scene, 1) == InvalidOperation);
	CHECK(TickFrames(*Scene, 1) == 0u);   // one frame reports it once
	PtStepStats Stats = {};
	Stats.StructSize = sizeof(PtStepStats);
	REQUIRE(Scene->GetBackendScene().GetStepStats(Stats) == PT_OK);
	CHECK((Stats.EngineErrorBits & InvalidOperation) != 0u);   // sticky for the run
	const std::vector<FEffectiveSetting> Settings = ReadEffective(*Scene);
	CHECK(Canonical(Settings, "physx.error_bits") == static_cast<double>(InvalidOperation));
	// Not a scene option: CreateScene (variants, the bench's preflight) rejects it.
	std::string Error;
	const PtKeyValue Option{ TestKey, "1" };
	PtSceneDesc Desc = {};
	Desc.StructSize = sizeof(PtSceneDesc);
	Desc.RunMode = PT_RUN_BENCH;
	Desc.Gravity = PtVec3{ 0.0, 0.0, -980.0 };
	Desc.NominalDeltaTime = 1.0 / 60.0;
	Desc.AsyncFixedTimeStepSize = PT_ASYNC_DT_SYNC;
	Desc.RestitutionThreshold = -1.0;
	Desc.NumThreads = 1;
	Desc.DebugName = "TestKeyAsSceneOption";
	Desc.Options = &Option;
	Desc.NumOptions = 1;
	CHECK(FPhysicsBackendScene::Create(GetPhysXModule(), Desc, Error) == nullptr);
	CHECK(Error.find("unknown scene option") != std::string::npos);
}

// Variant keys are validated (an unknown key or value fails CreateScene / InitGamePhys with the reason, the bench's
// preflight and the viewer show it), and the init option uses UE's struct syntax.
TEST_CASE("PhysicsTest.PhysXBackend.OptionsAreValidated")
{
	const FPhysXModuleScope Scope;
	FPhysicsBackendModule& Module = GetPhysXModule();
	if (Module.IsInitialized())
	{
		REQUIRE(Module.GetNumLiveScenes() == 0);
		REQUIRE(Module.TermGamePhys() == PT_OK);
	}
	const std::pair<const char*, const char*> BadInit[] = {
		{ "ToleranceScale", "(Length=100)" },
		{ "ToleranceScale", "100,1000" },
		{ "ToleranceScale", "(Length=-1,Speed=10)" },
		{ "ToleranceScale", "(Length=100,Speed=1000,Mass=1)" },
		{ "LengthScale", "1" },   // a scene option, not an init option
	};
	for (const auto& [Key, Value] : BadInit)
	{
		const std::string Text = std::string(Key) + "=" + Value;
		CAPTURE(Text);
		FBackendInitParams Params;
		Params.InitOptions = { { Key, Value } };
		CHECK(Module.InitGamePhys(Params) == PT_E_INVALID_ARG);
		CHECK_FALSE(Module.IsInitialized());
		MESSAGE(Module.GetLastErrorMessage());
	}
	FBackendInitParams Good;
	Good.InitOptions = { { "ToleranceScale", "( length = 50 , SPEED=500 )" } };
	REQUIRE(Module.InitGamePhys(Good) == PT_OK);
	const auto MakeDesc = [](const PtKeyValue* Options, const uint32_t NumOptions)
	{
		PtSceneDesc Desc = {};
		Desc.StructSize = sizeof(PtSceneDesc);
		Desc.RunMode = PT_RUN_BENCH;
		Desc.Gravity = PtVec3{ 0.0, 0.0, -980.0 };
		Desc.NominalDeltaTime = 1.0 / 60.0;
		Desc.AsyncFixedTimeStepSize = PT_ASYNC_DT_SYNC;
		Desc.RestitutionThreshold = -1.0;
		Desc.NumThreads = 1;
		Desc.DebugName = "OptionsAreValidated";
		Desc.Options = Options;
		Desc.NumOptions = NumOptions;
		return Desc;
	};
	// Tolerance length 50 means "1 m = 50 units": LengthScale 0.5 matches it, the default 1 (cm) does not.
	std::string Error;
	{
		const PtKeyValue Units{ "LengthScale", "0.5" };
		const PtSceneDesc Desc = MakeDesc(&Units, 1);
		std::unique_ptr<FPhysicsBackendScene> Scene = FPhysicsBackendScene::Create(Module, Desc, Error);
		CHECK_MESSAGE(Scene != nullptr, Error);
		const PtSceneDesc Mismatch = MakeDesc(nullptr, 0);
		CHECK(FPhysicsBackendScene::Create(Module, Mismatch, Error) == nullptr);
		CHECK(Error.find("one tolerances scale per process") != std::string::npos);
	}
	const PtKeyValue BadScene[] = {
		{ "BroadPhaseType", "MBP" },
		{ "SolverType", "Jacobi" },
		{ "ScratchMemorySize", "1000" },
		{ "PositionIterations", "0" },
		{ "LengthScale", "0" },
		{ "BounceThresholdVelocity", "-1" },
		{ "CpuDispatcher", "TaskGraph" },
		{ "NonBlockingPairFilter", "Ignore" },
		{ "KinematicStaticPairFilter", "Never" },
		{ "NoSuchKey", "1" },
	};
	for (const PtKeyValue& Option : BadScene)
	{
		const std::string Text = std::string(Option.Key) + "=" + Option.Value;
		CAPTURE(Text);
		const PtKeyValue Options[2] = { { "LengthScale", "0.5" }, Option };
		const PtSceneDesc Desc = MakeDesc(Options, 2);
		CHECK(FPhysicsBackendScene::Create(Module, Desc, Error) == nullptr);
		MESSAGE(Error);
	}
	CHECK(Module.TermGamePhys() == PT_OK);
}

// The snapshot keeps the contacts of a sleeping island (PT_CONTACT_SLEEPING), as the Jolt backend does (Progress M4):
// PhysX reports no contact for a pair whose dynamic bodies sleep. A wake command drops the kept pair until PhysX reports it
// again.
TEST_CASE("PhysicsTest.PhysXBackend.SleepingContactsAreKept")
{
	const FPhysXModuleScope Scope;
	FResolvedPhysicsConfig Config = LoadPhysXConfig("UE4Compat", "UEParity");
	Config.TestSettings.RunMode = EPhysicsRunMode::Viz;   // the listener exists in viz runs (or with PT_INSTR_CONTACTS)
	std::unique_ptr<FPhysScene> Scene = MakePhysXScenario("M1.SingleBoxDrop", { { "Case", "a" } }, Config);
	FBodyInstance& Box = *Scene->GetBodies()[1];
	int32 Frame = 0;
	for (; Frame < 600 && Box.State.ObjectState != EObjectStateType::Sleeping; ++Frame)
	{
		Scene->Tick(Scene->GetTestSettings().FixedDeltaTime);
	}
	MESSAGE("UE4Compat UEParity M1.SingleBoxDrop a: the box sleeps after " << Frame << " frames at z = " << Box.State.BodyTM.GetTranslation().Z << " cm");
	REQUIRE(Box.State.ObjectState == EObjectStateType::Sleeping);
	TickFrames(*Scene, 5);   // a few steps asleep: PhysX reports nothing for the pair now
	const auto CountBoxGround = [&Scene](uint32& OutSleeping, double& OutMinPhi, double& OutMaxPhi, double& OutMinNormalZ)
	{
		FPhysicsDebugSnapshot Snapshot;
		REQUIRE(Scene->CaptureDebugSnapshot(Snapshot));
		uint32 Count = 0;
		OutSleeping = 0;
		OutMinPhi = 1e30;
		OutMaxPhi = -1e30;
		OutMinNormalZ = 1e30;
		for (const PtContactPoint& Contact : Snapshot.Contacts)
		{
			if (!((Contact.Body0 == 1u && Contact.Body1 == 0u) || (Contact.Body0 == 0u && Contact.Body1 == 1u)))
			{
				continue;
			}
			++Count;
			CHECK(Contact.Body0 == 1u);   // Body0 = the more mobile body
			CHECK(std::isnan(Contact.NormalImpulse));
			CHECK((Contact.Flags & PT_CONTACT_ACTIVE) != 0u);   // a resting contact carries the box's weight (the solver's impulse)
			OutSleeping += (Contact.Flags & PT_CONTACT_SLEEPING) != 0u ? 1u : 0u;
			OutMinPhi = std::min(OutMinPhi, Contact.Phi);
			OutMaxPhi = std::max(OutMaxPhi, Contact.Phi);
			OutMinNormalZ = std::min(OutMinNormalZ, Contact.Normal.Z);
		}
		return Count;
	};
	uint32 Sleeping = 0;
	double MinPhi = 0.0;
	double MaxPhi = 0.0;
	double MinNormalZ = 0.0;
	const uint32 Kept = CountBoxGround(Sleeping, MinPhi, MaxPhi, MinNormalZ);
	MESSAGE("kept: " << Kept << " box-ground points, " << Sleeping << " flagged sleeping, Phi in [" << MinPhi << ", " << MaxPhi << "] cm, normal.z >= " << MinNormalZ);
	CHECK(Kept >= 1u);
	CHECK(Sleeping == Kept);
	CHECK(MinNormalZ > 0.999);
	CHECK(MinPhi >= -2.5);
	CHECK(MaxPhi <= 0.5);
	// A wake command: the kept pair is gone from a snapshot between steps, and the next step reports it awake.
	Box.WakeInstance();
	CHECK(Scene->GetBackendScene().FlushCommands() == PT_OK);
	CHECK(CountBoxGround(Sleeping, MinPhi, MaxPhi, MinNormalZ) == 0u);
	Scene->Tick(Scene->GetTestSettings().FixedDeltaTime);
	CHECK(CountBoxGround(Sleeping, MinPhi, MaxPhi, MinNormalZ) >= 1u);
	CHECK(Sleeping == 0u);
}

// 03 M5 (task 4 for PhysX): desc_sha256 is the same for every PhysX variant and Null per scene and profile (engine keys
// go to options_sha256 only).
TEST_CASE("PhysicsTest.PhysXBackend.DescShaEqualAcrossVariants")
{
	const FPhysXModuleScope Scope;
	const std::pair<const char*, FScenarioParams> Cases[] = {
		{ "S00.FreeFall", {} },
		{ "M1.SingleBoxDrop", { { "Case", "a" } } },
		{ "M1.SingleBoxDrop", { { "Case", "b" } } },
		{ "M3.BoxPile", { { "N", "1000" } } },
	};
	const auto Hash = [](const char* ScenarioName, const FScenarioParams& Params, const std::string& Backend, const FResolvedPhysicsConfig& Config,
		std::string& OutOptions) -> std::string
	{
		std::unique_ptr<FPhysScene> Scene;
		if (Backend == "PhysX")
		{
			Scene = MakePhysXScenario(ScenarioName, Params, Config);
		}
		else
		{
			IPhysicsTestScenario* Scenario = FScenarioRegistry::Get().Find(ScenarioName);
			REQUIRE(Scenario != nullptr);
			FScenarioParams Resolved;
			std::string Error;
			REQUIRE_MESSAGE(Scenario->ResolveParams(Params, Resolved, Error), Error);
			FPhysSceneInitParams InitParams;
			InitParams.DebugName = ScenarioName;
			InitParams.BackendName = Backend;
			InitParams.Config = &Config;
			Scene = std::make_unique<FPhysScene>(InitParams);
			FRandomStream Random(Scenario->GetSeed(Resolved));
			const FScenarioContext Context{ Resolved, Random, &Config };
			Scenario->BuildScene(*Scene, Context);
		}
		IPhysicsTestScenario* Scenario = FScenarioRegistry::Get().Find(ScenarioName);
		FScenarioParams Resolved;
		std::string Error;
		REQUIRE(Scenario->ResolveParams(Params, Resolved, Error));
		const std::vector<float> Dts(static_cast<size_t>(Scenario->GetDefaultFrames(Resolved)), Config.TestSettings.FixedDeltaTime);
		OutOptions = Scene->ComputeOptionsSha256();
		return Scene->ComputeDescSha256(Dts);
	};
	for (const char* Profile : { "UEParity", "Normalized" })
	{
		FBenchRunSpec Spec;
		Spec.Backend = "Null";
		Spec.PhysicsProfile = Profile;
		Spec.ConfigDir = PhysXTestConfigDir();
		FResolvedPhysicsConfig NullConfig;
		FBenchConfig Files;
		std::string Error;
		REQUIRE_MESSAGE(LoadBenchConfig(Spec, NullConfig, Files, Error), Error);
		for (const auto& [Name, Params] : Cases)
		{
			const std::string SceneText = std::string(Profile) + " " + Name + (Params.empty() ? "" : " " + Params.begin()->first + "=" + Params.begin()->second);
			CAPTURE(SceneText);
			std::string NullOptions;
			const std::string NullDesc = Hash(Name, Params, "Null", NullConfig, NullOptions);
			std::vector<std::string> VariantOptions;
			for (const char* Variant : GVariants)
			{
				CAPTURE(Variant);
				std::string Options;
				CHECK(Hash(Name, Params, "PhysX", LoadPhysXConfig(Variant, Profile), Options) == NullDesc);
				VariantOptions.push_back(Options);
			}
			for (size_t A = 0; A < VariantOptions.size(); ++A)
			{
				for (size_t B = A + 1; B < VariantOptions.size(); ++B)
				{
					CHECK(VariantOptions[A] != VariantOptions[B]);   // the engine keys differ
				}
			}
			MESSAGE(SceneText << ": desc " << NullDesc.substr(0, 16) << " (Null = PhysX x 4 variants)");
		}
	}
}

// M5 round 0: a PhysX run that names no variant (a bench run without -BackendProfile=, the viewer's "(Default / none)")
// resolves PhysX.UE4Compat, the one PhysX variant file with [PhysicsTest.Variant] DefaultForEngine=True: every PhysX manifest
// carries the variant label and the fidelity notes (02 8.2) and the run gets UE4Compat's UE4PhysSubstep clamp rule. Bullet
// and Jolt keep their <Backend>.Default file; Null has no variant.
TEST_CASE("PhysicsTest.PhysXBackend.DefaultVariantIsUE4Compat")
{
	const auto Resolve = [](const char* Backend, FBenchConfig& OutFiles) -> FResolvedPhysicsConfig
	{
		FBenchRunSpec Spec;
		Spec.Backend = Backend;
		Spec.PhysicsProfile = "UEParity";
		Spec.ConfigDir = PhysXTestConfigDir();
		FResolvedPhysicsConfig Config;
		std::string Error;
		const bool bLoaded = LoadBenchConfig(Spec, Config, OutFiles, Error);
		INFO(Error);
		REQUIRE(bLoaded);
		return Config;
	};
	FBenchConfig Files;
	const FResolvedPhysicsConfig Config = Resolve("PhysX", Files);
	CHECK(Files.VariantName == "UE4Compat");
	CHECK(Config.VariantName == "UE4Compat");
	CHECK(Files.VariantLabel == "UE4 settings on PhysX 5.11");
	REQUIRE_FALSE(Files.FidelityNotes.empty());
	CHECK(Files.FidelityNotes[0].find("UE4 settings on PhysX 5.11") != std::string::npos);
	CHECK(Config.InitOptions.empty());   // the same process as UE4Compat / -TGS / -Stab
	const FResolvedConfigValue* Clamp = Config.Find("PhysicsTestSettings.SubstepClampRule");
	REQUIRE(Clamp != nullptr);
	CHECK(Clamp->Value == "UE4PhysSubstep");
	// The explicit file and the default are the same configuration.
	const FResolvedPhysicsConfig Explicit = LoadPhysXConfig("UE4Compat", "UEParity");
	CHECK(Explicit.SceneOptions == Config.SceneOptions);
	CHECK(Explicit.PhysicsSha256() == Config.PhysicsSha256());
	FBenchConfig BulletFiles;
	Resolve("Bullet", BulletFiles);
	CHECK(BulletFiles.VariantName == "Default");
	FBenchConfig NullFiles;
	Resolve("Null", NullFiles);
	CHECK(NullFiles.VariantName.empty());
}
