// ScenarioTests.cpp - the M3 scenarios and the run hashes (03_WorkPlan M3, 02_Architecture 12.3, DL43): the registry,
// parameter resolution and scene_sha256, the M3.BoxPile layout, desc_sha256 equal across engines (Null vs every SDK
// backend) and across backend variants that differ only in engine keys, options_sha256 allowed to differ; plus the M2
// review fixes in FPhysScene (TermBody before the flush, InitBody twice).
#include "CoreMinimal.h"

#include "BenchmarkRunner.h"
#include "Containers/StringConv.h"
#include "Physics/EffectiveSettings.h"
#include "Physics/PhysicsBackend.h"
#include "Physics/PhysicsProfile.h"
#include "PhysicsTestScenario.h"
#include "PhysScene.h"

#include "Misc/PtThirdPartyIncludesStart.h"
#include <doctest/doctest.h>
#include "Misc/PtThirdPartyIncludesEnd.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

using namespace PhysicsTest;

namespace
{
	std::filesystem::path TestConfigDir()
	{
		std::string Override;
		if (FParse::Value(FCommandLine::Get(), "ConfigDir=", Override))
		{
			return StringConv::Utf8ToPath(Override);
		}
		return StringConv::Utf8ToPath(PT_CONFIG_DIR);
	}

	// Base -> Config/Profiles/<Profile>.ini -> the backend's Default variant when it exists (the bench's resolution).
	FResolvedPhysicsConfig LoadConfig(const std::string& Backend, const std::string& Profile, FBenchConfig* OutFiles = nullptr)
	{
		FBenchRunSpec Spec;
		Spec.Backend = Backend;
		Spec.PhysicsProfile = Profile;
		Spec.ConfigDir = TestConfigDir();
		FResolvedPhysicsConfig Config;
		FBenchConfig Files;
		std::string Error;
		const bool bLoaded = LoadBenchConfig(Spec, Config, Files, Error);
		INFO(Error);
		REQUIRE(bLoaded);
		if (OutFiles != nullptr)
		{
			*OutFiles = Files;
		}
		return Config;
	}

	std::vector<std::string> GetSdkBackends()
	{
		std::vector<std::string> Names;
		for (const std::string& Name : FPhysicsBackendRegistry::Get().GetAvailableNames())
		{
			if (Name != "Null" && Name != "NullDll" && Name != "Chaos")
			{
				Names.push_back(Name);
			}
		}
		return Names;
	}

	struct FBuiltScene
	{
		std::unique_ptr<FPhysScene> Scene;
		std::string DescSha256;
		std::string OptionsSha256;
	};

	// Builds Scenario (with Overrides) on Backend under Config, as the bench does, and hashes it for Frames frames.
	FBuiltScene BuildScenario(const char* ScenarioName, const FScenarioParams& Overrides, const std::string& Backend, const FResolvedPhysicsConfig& Config,
		const int32 Frames = 0)
	{
		IPhysicsTestScenario* Scenario = FScenarioRegistry::Get().Find(ScenarioName);
		REQUIRE(Scenario != nullptr);
		FScenarioParams Params;
		std::string Error;
		REQUIRE_MESSAGE(Scenario->ResolveParams(Overrides, Params, Error), Error);
		const FScenarioCapacity Capacity = Scenario->GetCapacity(Params);
		FPhysSceneInitParams InitParams;
		InitParams.DebugName = ScenarioName;
		InitParams.BackendName = Backend;
		InitParams.Config = &Config;
		InitParams.MaxBodiesHint = Capacity.MaxBodies;
		InitParams.MaxBodyPairsHint = Capacity.MaxBodyPairs;
		InitParams.MaxContactConstraintsHint = Capacity.MaxContactConstraints;
		FBuiltScene Built;
		Built.Scene = std::make_unique<FPhysScene>(InitParams);
		FRandomStream Random(Scenario->GetSeed(Params));
		const FScenarioContext Context{ Params, Random, &Config };
		Scenario->BuildScene(*Built.Scene, Context);
		const int32 NumFrames = Frames > 0 ? Frames : Scenario->GetDefaultFrames(Params);
		const std::vector<float> Dts(static_cast<size_t>(NumFrames), Config.TestSettings.FixedDeltaTime);
		Built.DescSha256 = Built.Scene->ComputeDescSha256(Dts);
		Built.OptionsSha256 = Built.Scene->ComputeOptionsSha256();
		return Built;
	}

	struct FSceneCase
	{
		const char* Scenario;
		FScenarioParams Params;
	};

	std::vector<FSceneCase> GetM3SceneCases()
	{
		return {
			FSceneCase{ "S00.FreeFall", {} },
			FSceneCase{ "M1.SingleBoxDrop", { { "Case", "a" } } },
			FSceneCase{ "M1.SingleBoxDrop", { { "Case", "b" } } },
			FSceneCase{ "M3.BoxPile", { { "N", "1000" } } },
		};
	}
}

TEST_CASE("PhysicsTest.Scenario.RegistryHasTheM3Scenes")
{
	const std::vector<IPhysicsTestScenario*> All = FScenarioRegistry::Get().GetAll();
	REQUIRE(All.size() >= 3u);
	for (const char* Name : { "S00.FreeFall", "M1.SingleBoxDrop", "M3.BoxPile" })
	{
		const std::string NameText(Name);
		CAPTURE(NameText);
		const IPhysicsTestScenario* Scenario = FScenarioRegistry::Get().Find(Name);
		REQUIRE(Scenario != nullptr);
		CHECK(std::string(Scenario->GetName()) == Name);
		std::string Lower = NameText;
		std::transform(Lower.begin(), Lower.end(), Lower.begin(), [](const char C) { return static_cast<char>(C >= 'A' && C <= 'Z' ? C - 'A' + 'a' : C); });
		CHECK(FScenarioRegistry::Get().Find(Lower) == Scenario);   // IDs compare case-insensitively
		FScenarioParams Params;
		std::string Error;
		CHECK(Scenario->ResolveParams({}, Params, Error));
		CHECK(Scenario->GetDefaultFrames(Params) > 0);
		CHECK_FALSE(Scenario->GetWindows(Params).empty());
	}
	CHECK(FScenarioRegistry::Get().Find("S00.FreeFall")->GetDefaultFrames({}) == 60);
	CHECK(FScenarioRegistry::Get().Find("M1.SingleBoxDrop")->GetDefaultFrames({}) == 300);
	CHECK(FScenarioRegistry::Get().Find("M3.BoxPile")->GetDefaultFrames({}) == 600);
	// 02 9.5: M3.BoxPile declares headline 1-599, impact 1-179, settled 300-599.
	const std::vector<FStepWindow> Windows = FScenarioRegistry::Get().Find("M3.BoxPile")->GetWindows({});
	REQUIRE(Windows.size() == 3u);
	CHECK((Windows[0].Name == "headline" && Windows[0].First == 1u && Windows[0].Last == 599u));
	CHECK((Windows[1].Name == "impact" && Windows[1].First == 1u && Windows[1].Last == 179u));
	CHECK((Windows[2].Name == "settled" && Windows[2].First == 300u && Windows[2].Last == 599u));
}

TEST_CASE("PhysicsTest.Scenario.ParamsAndSceneHash")
{
	const IPhysicsTestScenario& Pile = *FScenarioRegistry::Get().Find("M3.BoxPile");
	FScenarioParams Params;
	std::string Error;
	REQUIRE(Pile.ResolveParams({}, Params, Error));
	CHECK(Params.at("N") == "1000");
	CHECK(Params.at("Seed") == "24301");
	CHECK(Pile.MakeCanonicalJson(Params) == "{\"name\":\"M3.BoxPile\",\"params\":{\"N\":\"1000\",\"Seed\":\"24301\"},\"seed\":24301,\"version\":1}");
	const std::string Sha1000 = Pile.ComputeSceneSha256(Params);
	CHECK(Sha1000.size() == 64u);
	REQUIRE(Pile.ResolveParams({ { "n", "5000" } }, Params, Error));   // parameter names compare case-insensitively
	CHECK(Params.at("N") == "5000");
	CHECK(Pile.ComputeSceneSha256(Params) != Sha1000);
	CHECK_FALSE(Pile.ResolveParams({ { "N", "7" } }, Params, Error));
	CHECK(Error.find("not one of") != std::string::npos);
	CHECK_FALSE(Pile.ResolveParams({ { "Count", "5" } }, Params, Error));
	const IPhysicsTestScenario& Drop = *FScenarioRegistry::Get().Find("M1.SingleBoxDrop");
	CHECK_FALSE(Drop.ResolveParams({ { "Case", "c" } }, Params, Error));
	REQUIRE(Drop.ResolveParams({ { "Case", "b" } }, Params, Error));
	CHECK(Params.at("Case") == "b");
	CHECK(Drop.GetSeed(Params) == FRandomStream::DefaultSeed);   // no Seed parameter: the scenario default
	CHECK(Drop.MakeCanonicalJson(Params) == "{\"name\":\"M1.SingleBoxDrop\",\"params\":{\"Case\":\"b\"},\"seed\":24301,\"version\":1}");
}

// M4 (M3 review minor): scene_sha256 covers the seed also for a scene that declares no Seed parameter, so a change of
// GetDefaultSeed() changes the hash (02 12.3).
TEST_CASE("PhysicsTest.Scenario.SceneHashCoversDefaultSeed")
{
	struct FSeedOnlyScenario final : IPhysicsTestScenario
	{
		int32 Seed = FRandomStream::DefaultSeed;
		const char* GetName() const override { return "Test.SeedOnly"; }
		int32 GetVersion() const override { return 1; }
		EScenarioKind GetKind() const override { return EScenarioKind::Gate; }
		bool IsChaotic() const override { return false; }
		const char* GetDescription() const override { return "test only"; }
		int32 GetDefaultFrames(const FScenarioParams&) const override { return 1; }
		std::vector<FStepWindow> GetWindows(const FScenarioParams&) const override { return {}; }
		FScenarioCapacity GetCapacity(const FScenarioParams&) const override { return {}; }
		void BuildScene(FPhysScene&, const FScenarioContext&) override {}
		int32 GetDefaultSeed() const override { return Seed; }
	};
	FSeedOnlyScenario Scenario;
	FScenarioParams Params;
	std::string Error;
	REQUIRE(Scenario.ResolveParams({}, Params, Error));
	const std::string Before = Scenario.ComputeSceneSha256(Params);
	Scenario.Seed = 7;
	CHECK(Scenario.ComputeSceneSha256(Params) != Before);
	CHECK(Scenario.MakeCanonicalJson(Params) == "{\"name\":\"Test.SeedOnly\",\"params\":{},\"seed\":7,\"version\":1}");
}

// M3 fix round: Seed is an Int32 parameter. A malformed value is rejected (it used to become std::atoi's 0), and the
// stored value is canonical, so scene_sha256 follows the seed, not its spelling.
TEST_CASE("PhysicsTest.Scenario.SeedParameter")
{
	const IPhysicsTestScenario& Pile = *FScenarioRegistry::Get().Find("M3.BoxPile");
	FScenarioParams Default;
	FScenarioParams Params;
	std::string Error;
	REQUIRE(Pile.ResolveParams({}, Default, Error));
	CHECK(Pile.GetSeed(Default) == 24301);
	CHECK(Pile.GetSeed(Default) == Pile.GetDefaultSeed());
	for (const char* Bad : { "abc", "", "-", "+5", " 5", "5 ", "12abc", "0x5EED", "1.5", "2147483648", "-2147483649" })
	{
		const std::string BadText(Bad);
		CAPTURE(BadText);
		Error.clear();
		CHECK_FALSE(Pile.ResolveParams({ { "Seed", Bad } }, Params, Error));
		CHECK(Error.find("not a decimal 32-bit integer") != std::string::npos);
	}
	REQUIRE(Pile.ResolveParams({ { "Seed", "024301" } }, Params, Error));
	CHECK(Params.at("Seed") == "24301");
	CHECK(Pile.ComputeSceneSha256(Params) == Pile.ComputeSceneSha256(Default));
	REQUIRE(Pile.ResolveParams({ { "seed", "-7" } }, Params, Error));
	CHECK(Params.at("Seed") == "-7");
	CHECK(Pile.GetSeed(Params) == -7);
	REQUIRE(Pile.ResolveParams({ { "Seed", "-2147483648" } }, Params, Error));
	CHECK(Pile.GetSeed(Params) == -2147483647 - 1);
	REQUIRE(Pile.ResolveParams({ { "Seed", "2147483647" } }, Params, Error));
	CHECK(Pile.GetSeed(Params) == 2147483647);
	bool bDeclaredInt32 = false;
	for (const FScenarioParamDesc& Desc : Pile.GetParamDescs())
	{
		bDeclaredInt32 = bDeclaredInt32 || (Desc.Name == "Seed" && Desc.Type == EScenarioParamType::Int32);
	}
	CHECK(bDeclaredInt32);
}

TEST_CASE("PhysicsTest.Scenario.BoxPileLayout")
{
	const FResolvedPhysicsConfig Config = LoadConfig("Null", "Normalized");
	for (const auto& [N, Expected] : { std::pair<const char*, int32>{ "1000", 1000 }, { "5000", 5000 }, { "10000", 10000 } })
	{
		const std::string NText(N);
		CAPTURE(NText);
		const FBuiltScene Built = BuildScenario("M3.BoxPile", { { "N", N } }, "Null", Config, 2);
		const FPhysScene& Scene = *Built.Scene;
		CHECK(Scene.GetStats().NumBodies == Expected + 1);
		CHECK(Scene.GetStats().NumDynamicBodies == Expected);
		CHECK(Scene.GetStats().NumStaticBodies == 1);
		const FBodyInstance& Ground = *Scene.GetBodies()[0];
		CHECK(Ground.IsStatic());   // the ground is body index 0
		CHECK(Ground.ActorHandle.Index == 0u);
		// Every cube: one 100 cm box element, COM at the centre, 1000 kg, lowest layer at z = 100, top layer at z = 1450.
		double MinZ = 1e30;
		double MaxZ = -1e30;
		double MaxShift = 0.0;
		for (size_t Index = 1; Index < Scene.GetBodies().size(); ++Index)
		{
			const FBodyInstance& Box = *Scene.GetBodies()[Index];
			MinZ = std::min(MinZ, Box.State.BodyTM.GetTranslation().Z);
			MaxZ = std::max(MaxZ, Box.State.BodyTM.GetTranslation().Z);
			const double GridX = std::round(Box.State.BodyTM.GetTranslation().X / 75.0) * 75.0;   // centres on a 150 cm grid offset by 75 or 0
			MaxShift = std::max(MaxShift, std::abs(Box.State.BodyTM.GetTranslation().X - GridX));
			if (Index == 1)
			{
				CHECK(Box.MassProperties.Mass == doctest::Approx(1000.0));
				CHECK(Box.MassProperties.CenterOfMass.Size() < 1e-9);
				REQUIRE(Box.BodySetup->AggGeom.BoxElems.size() == 1u);
				CHECK(Box.BodySetup->AggGeom.BoxElems[0].X == 100.0);
			}
		}
		CHECK(MinZ == doctest::Approx(100.0));
		CHECK(MaxZ == doctest::Approx(100.0 + 9 * 150.0));
		CHECK(MaxShift <= 5.0 + 1e-9);
	}
	// The same seed builds the same scene; another seed does not.
	const FBuiltScene A = BuildScenario("M3.BoxPile", {}, "Null", Config, 2);
	const FBuiltScene B = BuildScenario("M3.BoxPile", {}, "Null", Config, 2);
	const FBuiltScene C = BuildScenario("M3.BoxPile", { { "Seed", "7" } }, "Null", Config, 2);
	CHECK(A.DescSha256 == B.DescSha256);
	CHECK(A.DescSha256 != C.DescSha256);
}

// 03 M3: "같은 씬·프로필에서 Null과 Jolt의 desc_sha256이 같다".
TEST_CASE("PhysicsTest.Scenario.DescShaEqualAcrossEngines")
{
	const std::vector<std::string> SdkBackends = GetSdkBackends();
	MESSAGE("SDK backends compared with Null: " << SdkBackends.size());
	for (const char* Profile : { "UEParity", "Normalized" })
	{
		const std::string ProfileText(Profile);
		CAPTURE(ProfileText);
		for (const FSceneCase& Case : GetM3SceneCases())
		{
			const std::string SceneText = std::string(Case.Scenario) + (Case.Params.empty() ? "" : " " + Case.Params.begin()->first + "=" + Case.Params.begin()->second);
			CAPTURE(SceneText);
			const FResolvedPhysicsConfig NullConfig = LoadConfig("Null", Profile);
			const FBuiltScene Null = BuildScenario(Case.Scenario, Case.Params, "Null", NullConfig);
			for (const std::string& Backend : SdkBackends)
			{
				CAPTURE(Backend);
				const FResolvedPhysicsConfig Config = LoadConfig(Backend, Profile);
				const FBuiltScene Sdk = BuildScenario(Case.Scenario, Case.Params, Backend, Config);
				CHECK(Sdk.DescSha256 == Null.DescSha256);
				CHECK(Sdk.OptionsSha256 != Null.OptionsSha256);   // the frame driver differs (Native vs Harness)
				MESSAGE(ProfileText << " " << SceneText << ": desc " << Null.DescSha256.substr(0, 16) << " (Null = " << Backend << ")");
			}
		}
	}
	// Profiles change the inputs (damping, angular limit): another desc.
	const FBuiltScene Parity = BuildScenario("M1.SingleBoxDrop", {}, "Null", LoadConfig("Null", "UEParity"));
	const FBuiltScene Normalized = BuildScenario("M1.SingleBoxDrop", {}, "Null", LoadConfig("Null", "Normalized"));
	CHECK(Parity.DescSha256 != Normalized.DescSha256);
}

// 03 M3: "엔진 키만 다른 시험용 변형 두 개로 같은 씬을 만들어도 desc_sha256이 같다 ... options_sha256은 달라도 된다".
TEST_CASE("PhysicsTest.Scenario.DescShaEqualAcrossEngineKeyVariants")
{
	const std::vector<std::string> SdkBackends = GetSdkBackends();
	if (std::find(SdkBackends.begin(), SdkBackends.end(), "Jolt") == SdkBackends.end())
	{
		MESSAGE("Jolt is not in this build: the variant test is skipped");
		return;
	}
	FConfigFile Base;
	FPhysicsProfile Profile;
	std::string Error;
	REQUIRE(FConfigFile::LoadFile(StringConv::PathToUtf8(TestConfigDir() / "BasePhysicsTest.ini"), Base, Error));
	REQUIRE(FPhysicsProfile::LoadIni(StringConv::PathToUtf8(TestConfigDir() / "Profiles" / "Normalized.ini"), Profile, Error));
	const auto MakeConfig = [&](const char* VariantText) -> FResolvedPhysicsConfig
	{
		FBackendVariant Variant;
		std::string VariantError;
		REQUIRE_MESSAGE(FBackendVariant::FromText(VariantText, "TestVariant.ini", Variant, VariantError), VariantError);
		FProfileResolveInputs Inputs;
		Inputs.Base = &Base;
		Inputs.Profile = &Profile;
		Inputs.Variant = &Variant;
		FResolvedPhysicsConfig Config;
		REQUIRE_MESSAGE(FProfileResolver::Resolve(Inputs, Config, VariantError), VariantError);
		return Config;
	};
	// Two test variants that differ only in [PhysicsTest.Backend.Jolt] keys (engine keys: options_sha256).
	const FResolvedPhysicsConfig VariantA = MakeConfig("[PhysicsTest.Variant]\nEngine=Jolt\nName=TestA\n[PhysicsTest.Backend.Jolt]\nNumVelocitySteps=10\n"
		"NumPositionSteps=2\n");
	const FResolvedPhysicsConfig VariantB = MakeConfig("[PhysicsTest.Variant]\nEngine=Jolt\nName=TestB\n[PhysicsTest.Backend.Jolt]\nNumVelocitySteps=14\n"
		"NumPositionSteps=3\nConvexRadius=2.5\nUseLargeIslandSplitter=false\n");
	REQUIRE(VariantA.SceneOptions.size() == 2u);
	REQUIRE(VariantB.SceneOptions.size() == 4u);
	for (const FSceneCase& Case : GetM3SceneCases())
	{
		const std::string SceneText(Case.Scenario);
		CAPTURE(SceneText);
		const FBuiltScene A = BuildScenario(Case.Scenario, Case.Params, "Jolt", VariantA);
		const FBuiltScene B = BuildScenario(Case.Scenario, Case.Params, "Jolt", VariantB);
		CHECK(A.DescSha256 == B.DescSha256);
		CHECK(A.OptionsSha256 != B.OptionsSha256);
		// The variant really reached the engine: Jolt reports the velocity steps it uses.
		std::vector<FEffectiveSetting> Settings;
		REQUIRE(ParseEffectiveSettings(B.Scene->DumpEffectiveSettings(), Settings, Error));
		const auto Iterations = std::find_if(Settings.begin(), Settings.end(), [](const FEffectiveSetting& S) { return S.Key == "solver.iterations"; });
		REQUIRE(Iterations != Settings.end());
		CHECK(Iterations->Canonical == 14.0);
	}
}

TEST_CASE("PhysicsTest.Scenario.DescShaCoversItsInputs")
{
	const FResolvedPhysicsConfig Config = LoadConfig("Null", "Normalized");
	const FBuiltScene Reference = BuildScenario("M1.SingleBoxDrop", {}, "Null", Config, 300);
	CHECK(BuildScenario("M1.SingleBoxDrop", {}, "Null", Config, 300).DescSha256 == Reference.DescSha256);
	CHECK(BuildScenario("M1.SingleBoxDrop", {}, "Null", Config, 299).DescSha256 != Reference.DescSha256);       // dt sequence
	CHECK(BuildScenario("M1.SingleBoxDrop", { { "Case", "b" } }, "Null", Config, 300).DescSha256 != Reference.DescSha256);   // pose
	FResolvedPhysicsConfig Gravity = Config;
	Gravity.Settings.DefaultGravityZ = -981.0f;
	CHECK(BuildScenario("M1.SingleBoxDrop", {}, "Null", Gravity, 300).DescSha256 != Reference.DescSha256);
	FResolvedPhysicsConfig Threads = Config;
	Threads.TestSettings.NumThreads = 2;   // a run option: options_sha256 only
	const FBuiltScene TwoThreads = BuildScenario("M1.SingleBoxDrop", {}, "Null", Threads, 300);
	CHECK(TwoThreads.DescSha256 == Reference.DescSha256);
	CHECK(TwoThreads.OptionsSha256 != Reference.OptionsSha256);
}

// M2 review (4): TermBody before the flush leaves no actor behind; InitBody twice never creates two actors.
TEST_CASE("PhysicsTest.PhysScene.TermBodyBeforeFlushAndInitBodyTwice")
{
	for (const char* Backend : { "Null", "NullDll" })
	{
		const std::string BackendText(Backend);
		CAPTURE(BackendText);
		FPhysSceneInitParams InitParams;
		InitParams.DebugName = "TermBeforeFlush";
		InitParams.BackendName = Backend;
		FPhysScene Scene(InitParams);
		FBodySetup Setup;
		Setup.AggGeom.SphereElems.emplace_back(10.0);
		const FBodySetup* Sphere = Scene.CreateBodySetup(std::move(Setup));
		FBodyInstance Template;
		Template.bSimulatePhysics = true;
		FBodyInstance* Kept = Scene.SpawnBody(Template, Sphere, FTransform(FVector(0.0, 0.0, 100.0)));
		FBodyInstance* Dropped = Scene.SpawnBody(Template, Sphere, FTransform(FVector(0.0, 0.0, 200.0)));
		CHECK(Scene.GetNumDeferredActors() == 2);
		Dropped->TermBody();   // deferred, not flushed: leaves the pending list
		CHECK(Scene.GetNumDeferredActors() == 1);
		Scene.FlushDeferredActors();
		CHECK(Scene.GetBackendScene().GetNumBodies() == 1u);
		CHECK(Kept->ActorHandle.IsValid());
		CHECK_FALSE(Dropped->ActorHandle.IsValid());
		CHECK(Scene.GetStats().NumBodies == 1);

		// InitBody on a live body: the old actor is released first, the body is deferred once.
		Kept->InitBody(Sphere, FTransform(FVector(0.0, 0.0, 300.0)), &Scene);
		CHECK(Scene.GetNumDeferredActors() == 1);
		CHECK_FALSE(Kept->ActorHandle.IsValid());
		Kept->InitBody(Sphere, FTransform(FVector(0.0, 0.0, 400.0)), &Scene);   // and again while deferred
		CHECK(Scene.GetNumDeferredActors() == 1);
		Scene.FlushDeferredActors();
		CHECK(Kept->ActorHandle.IsValid());
		CHECK(Scene.GetStats().NumBodies == 1);   // one live actor: index 0 released, index 1 created
		CHECK(Scene.GetBackendScene().GetNumBodies() == 2u);
		CHECK(Kept->ActorHandle.Index == 1u);
		Scene.Tick(1.0f / 60.0f);
		CHECK(Kept->State.BodyTM.GetTranslation().Z < 400.0);
	}
}

// M3 fix round 1: SpawnBody copies the template without its registration (scene, handle, deferred flag). A body of
// another scene as the template - deferred there, or a copy whose scene is gone - must not make InitBody's TermBody
// reach that scene (before the fix: a no-op on a live scene, a use-after-free on a destroyed one).
TEST_CASE("PhysicsTest.PhysScene.SpawnBodyDropsTheTemplatesRegistration")
{
	FPhysSceneInitParams InitParams;
	InitParams.BackendName = "Null";
	InitParams.DebugName = "SceneA";
	auto SceneA = std::make_unique<FPhysScene>(InitParams);
	FBodySetup SetupA;
	SetupA.AggGeom.SphereElems.emplace_back(10.0);
	const FBodySetup* SphereA = SceneA->CreateBodySetup(std::move(SetupA));
	FBodyInstance Template;
	Template.bSimulatePhysics = true;
	FBodyInstance* InA = SceneA->SpawnBody(Template, SphereA, FTransform(FVector(0.0, 0.0, 100.0)));
	REQUIRE(SceneA->GetNumDeferredActors() == 1);

	InitParams.DebugName = "SceneB";
	FPhysScene SceneB(InitParams);
	FBodySetup SetupB;
	SetupB.AggGeom.SphereElems.emplace_back(10.0);
	const FBodySetup* SphereB = SceneB.CreateBodySetup(std::move(SetupB));
	// 1) A body still deferred in a live scene as the template: scene A keeps its own deferred body.
	FBodyInstance* FromLive = SceneB.SpawnBody(*InA, SphereB, FTransform(FVector(0.0, 0.0, 200.0)));
	CHECK(SceneA->GetNumDeferredActors() == 1);
	CHECK(SceneB.GetNumDeferredActors() == 1);
	CHECK(FromLive->GetPhysicsScene() == &SceneB);
	SceneA->FlushDeferredActors();
	CHECK(InA->ActorHandle.IsValid());

	// 2) A copy of a live body whose scene is then destroyed: SpawnBody must not touch scene A.
	const FBodyInstance Orphan = *InA;   // carries scene A's pointer and actor handle
	SceneA.reset();
	FBodyInstance* FromOrphan = SceneB.SpawnBody(Orphan, SphereB, FTransform(FVector(0.0, 0.0, 300.0)));
	CHECK(SceneB.GetNumDeferredActors() == 2);
	CHECK_FALSE(FromOrphan->ActorHandle.IsValid());
	CHECK(FromOrphan->State.ObjectState == EObjectStateType::Uninitialized);
	SceneB.FlushDeferredActors();
	CHECK(FromLive->ActorHandle.IsValid());
	CHECK(FromOrphan->ActorHandle.IsValid());
	CHECK(SceneB.GetBackendScene().GetNumBodies() == 2u);
	CHECK(SceneB.GetNumDeferredActors() == 0);
}
