// SdkBackendTests.cpp - the SDK backends through FPhysScene and the C ABI (03_WorkPlan M3: Jolt; M5 adds Bullet and PhysX
// to the registry and they run here unchanged): S00.FreeFall within the gate tolerance, no shape wrapper in the M1 / M3
// scenes (and the wrapper counts when one is needed), the resting contact of M1.SingleBoxDrop a in the Chaos convention,
// the kinematic velocity reference point, PT_READ_ALL's indices, capacity / temp-allocator health on M3.BoxPile, and the
// body commands against the Null reference on bodies in free flight.
#include "CoreMinimal.h"

#include "BenchmarkRunner.h"
#include "Containers/StringConv.h"
#include "Fixtures/FreeFallFixture.h"
#include "Physics/EffectiveSettings.h"
#include "Physics/PhysicsBackend.h"
#include "Physics/PhysicsDebugSnapshot.h"
#include "Physics/PhysicsInterface.h"
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
	std::filesystem::path SdkTestConfigDir()
	{
		std::string Override;
		if (FParse::Value(FCommandLine::Get(), "ConfigDir=", Override))
		{
			return StringConv::Utf8ToPath(Override);
		}
		return StringConv::Utf8ToPath(PT_CONFIG_DIR);
	}

	FResolvedPhysicsConfig LoadSdkConfig(const std::string& Backend, const std::string& Profile)
	{
		FBenchRunSpec Spec;
		Spec.Backend = Backend;
		Spec.PhysicsProfile = Profile;
		Spec.ConfigDir = SdkTestConfigDir();
		FResolvedPhysicsConfig Config;
		FBenchConfig Files;
		std::string Error;
		const bool bLoaded = LoadBenchConfig(Spec, Config, Files, Error);
		INFO(Error);
		REQUIRE(bLoaded);
		return Config;
	}

	std::vector<std::string> SdkBackendNames()
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

	std::unique_ptr<FPhysScene> MakeScenarioScene(const char* ScenarioName, const FScenarioParams& Overrides, const std::string& Backend,
		const FResolvedPhysicsConfig& Config)
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
		auto Scene = std::make_unique<FPhysScene>(InitParams);
		FRandomStream Random(Scenario->GetSeed(Params));
		const FScenarioContext Context{ Params, Random, &Config };
		Scenario->BuildScene(*Scene, Context);
		Scene->PrepareForSimulation();
		return Scene;
	}

	const FEffectiveSetting* FindSetting(const std::vector<FEffectiveSetting>& Settings, const char* Key)
	{
		const auto Found = std::find_if(Settings.begin(), Settings.end(), [Key](const FEffectiveSetting& S) { return S.Key == Key; });
		return Found != Settings.end() ? &*Found : nullptr;
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
}

// 03 M3: "Jolt가 ABI를 통해 S00.FreeFall(Normalized)에서 |z60 − 501.83328| ≤ 2e-3 cm, |v60 + 980.00005| ≤ 1e-2 cm/s다".
TEST_CASE("PhysicsTest.SdkBackend.FreeFallS00")
{
	const std::vector<std::string> Backends = SdkBackendNames();
	REQUIRE_FALSE(Backends.empty());
	for (const std::string& Backend : Backends)
	{
		for (const char* Profile : { "Normalized", "UEParity" })
		{
			const std::string ProfileText(Profile);
			CAPTURE(Backend);
			CAPTURE(ProfileText);
			const FResolvedPhysicsConfig Config = LoadSdkConfig(Backend, Profile);
			std::unique_ptr<FPhysScene> Scene = MakeScenarioScene("S00.FreeFall", {}, Backend, Config);
			REQUIRE(Scene->GetBodies().size() == 1u);
			const FBodyInstance& Sphere = *Scene->GetBodies()[0];
			for (int32 Frame = 0; Frame < Fixtures::FFreeFall::Frames; ++Frame)
			{
				Scene->Tick(Config.TestSettings.FixedDeltaTime);
			}
			const double Z60 = Sphere.State.BodyTM.GetTranslation().Z;
			const double V60 = Sphere.State.LinearVelocity.Z;
			const float Dt = Scene->GetLastPlan().StepDeltaTime;
			MESSAGE(Backend << " / " << ProfileText << ": z60 = " << Z60 << " cm (|dz| = " << std::abs(Z60 - 501.83328) << "), v60 = " << V60
				<< " cm/s (|dv| = " << std::abs(V60 + 980.00005) << "), reference z " << Fixtures::FFreeFall::ExpectedZ(60, Dt) << " v "
				<< Fixtures::FFreeFall::ExpectedVz(60, Dt));
			CHECK(Scene->GetStepCount() == 60u);
			CHECK(std::abs(Z60 - 501.83328) <= 2e-3);
			CHECK(std::abs(V60 + 980.00005) <= 1e-2);
			CHECK(Sphere.State.ObjectState == EObjectStateType::Dynamic);
			CHECK(std::abs(Sphere.State.BodyTM.GetTranslation().X) < 1e-9);
		}
	}
	// The same scenario on Null reproduces the M2 fixture exactly (the scenario adds nothing to the fixture).
	const FResolvedPhysicsConfig Config = LoadSdkConfig("Null", "Normalized");
	std::unique_ptr<FPhysScene> Scene = MakeScenarioScene("S00.FreeFall", {}, "Null", Config);
	for (int32 Frame = 0; Frame < 60; ++Frame)
	{
		Scene->Tick(Config.TestSettings.FixedDeltaTime);
	}
	CHECK(std::abs(Scene->GetBodies()[0]->State.BodyTM.GetTranslation().Z - Fixtures::FFreeFall::ExpectedZ60) <= Fixtures::FFreeFall::Tolerance);
	CHECK(std::abs(Scene->GetBodies()[0]->State.LinearVelocity.Z - Fixtures::FFreeFall::ExpectedVz(60, Scene->GetLastPlan().StepDeltaTime)) <= 1e-9);
}

// 03 M3: "M3.BoxPile과 M1.SingleBoxDrop에서 shape.wrapping = 0이다" (DL42), and the counter counts real wrappers.
TEST_CASE("PhysicsTest.SdkBackend.ShapeWrapping")
{
	for (const std::string& Backend : SdkBackendNames())
	{
		CAPTURE(Backend);
		for (const char* Profile : { "UEParity", "Normalized" })
		{
			const std::string ProfileText(Profile);
			CAPTURE(ProfileText);
			const FResolvedPhysicsConfig Config = LoadSdkConfig(Backend, Profile);
			const std::pair<const char*, FScenarioParams> Cases[] = {
				{ "M1.SingleBoxDrop", { { "Case", "a" } } },
				{ "M1.SingleBoxDrop", { { "Case", "b" } } },
				{ "M3.BoxPile", { { "N", "1000" } } },
				{ "M3.BoxPile", { { "N", "5000" } } },
			};
			for (const auto& [Name, Params] : Cases)
			{
				const std::string SceneText = std::string(Name) + " " + Params.begin()->second;
				CAPTURE(SceneText);
				std::unique_ptr<FPhysScene> Scene = MakeScenarioScene(Name, Params, Backend, Config);
				const std::vector<FEffectiveSetting> Settings = ReadEffective(*Scene);
				const FEffectiveSetting* Wrapping = FindSetting(Settings, "shape.wrapping");
				REQUIRE(Wrapping != nullptr);
				CHECK(Wrapping->bHasCanonical);
				CHECK(Wrapping->Canonical == 0.0);
				CHECK(Wrapping->Source == "READBACK");
				MESSAGE(Backend << " " << ProfileText << " " << SceneText << ": shape.wrapping = " << Wrapping->Native);
			}
		}

		// Positive control: an offset COM (COMNudge), a capsule (UE Z -> engine axis) and a two-element compound.
		FPhysSceneInitParams InitParams;
		InitParams.DebugName = "WrapperCount";
		InitParams.BackendName = Backend;
		FPhysScene Scene(InitParams);
		FBodySetup Box;
		Box.AggGeom.BoxElems.emplace_back(50.0);
		const FBodySetup* BoxSetup = Scene.CreateBodySetup(std::move(Box));
		FBodySetup Capsule;
		Capsule.AggGeom.SphylElems.emplace_back(25.0, 100.0);
		const FBodySetup* CapsuleSetup = Scene.CreateBodySetup(std::move(Capsule));
		FBodySetup Pair;
		Pair.AggGeom.BoxElems.emplace_back(50.0);
		Pair.AggGeom.SphereElems.emplace_back(25.0);
		Pair.AggGeom.SphereElems.back().Center = FVector(0.0, 0.0, 60.0);
		const FBodySetup* PairSetup = Scene.CreateBodySetup(std::move(Pair));
		FBodyInstance Template;
		Template.bSimulatePhysics = true;
		Template.bEnableGravity = false;
		Scene.SpawnBody(Template, BoxSetup, FTransform(FVector(0.0, 0.0, 1000.0)));        // native: no wrapper
		FBodyInstance Nudged = Template;
		Nudged.COMNudge = FVector(10.0, 0.0, 0.0);
		Scene.SpawnBody(Nudged, BoxSetup, FTransform(FVector(500.0, 0.0, 1000.0)));        // offset_com
		Scene.SpawnBody(Template, CapsuleSetup, FTransform(FVector(1000.0, 0.0, 1000.0))); // axis_remap
		Scene.SpawnBody(Template, PairSetup, FTransform(FVector(1500.0, 0.0, 1000.0)));    // compound (its float COM is the harness COM)
		Scene.FlushDeferredActors();
		const std::vector<FEffectiveSetting> Settings = ReadEffective(Scene);
		const FEffectiveSetting* Wrapping = FindSetting(Settings, "shape.wrapping");
		REQUIRE(Wrapping != nullptr);
		MESSAGE(Backend << " wrapper control: " << Wrapping->Native);
		CHECK(Wrapping->Canonical >= 3.0);
		CHECK(Wrapping->Native.find("axis_remap 1") != std::string::npos);
		CHECK(Wrapping->Native.find("compound 1") != std::string::npos);
		// Only the nudged box needs a COM decorator: the compound's COM, computed by the engine in float, equals the
		// harness COM within float resolution (M3 fix round: a 1e-6 cm test in float metres wrapped every compound).
		CHECK(Wrapping->Native.find("offset_com 1") != std::string::npos);
		PtMassProperties PairMass = {};
		REQUIRE(Scene.GetBackendScene().ReadMassProperties(3, std::span<PtMassProperties>(&PairMass, 1)) == PT_OK);
		const FVector HarnessPairCom = Scene.GetBodies()[3]->MassProperties.CenterOfMass;
		CHECK(std::abs(PairMass.CenterOfMassLocal.Translation.Z - HarnessPairCom.Z) < 1e-3);
		CHECK(std::abs(HarnessPairCom.Z) > 1.0);   // the sphere moves the COM up: the test is not trivially at 0
		// The engine's COM is the harness COM (offset 10 cm along X): ReadMassProperties of the nudged body.
		PtMassProperties Mass = {};
		REQUIRE(Scene.GetBackendScene().ReadMassProperties(1, std::span<PtMassProperties>(&Mass, 1)) == PT_OK);
		CHECK(Mass.CenterOfMassLocal.Translation.X == doctest::Approx(10.0).epsilon(1e-5));
		CHECK(Mass.Mass == doctest::Approx(Scene.GetBodies()[1]->MassProperties.Mass).epsilon(1e-5));
	}
}

// 03 M3: the resting contact of M1.SingleBoxDrop a in the snapshot: normal . (+Z) > 0.999, -2.5 cm <= Phi <= 0.5 cm
// (Jolt keeps up to mPenetrationSlop = 2 cm of penetration, ContactConstraintManager.cpp:1904-1906 [V]).
TEST_CASE("PhysicsTest.SdkBackend.SingleBoxDropRestingContact")
{
	for (const std::string& Backend : SdkBackendNames())
	{
		CAPTURE(Backend);
		// Normalized for the full 300 frames (the criterion). UEParity lets the box fall asleep after about half a second at
		// rest, and a sleeping body has no contact in the step, so it is checked at frame 50 (resting, still awake).
		for (const auto& [Profile, NumFrames] : { std::pair<const char*, int32>{ "Normalized", 300 }, { "UEParity", 50 } })
		{
			const std::string ProfileText(Profile);
			CAPTURE(ProfileText);
			FResolvedPhysicsConfig Config = LoadSdkConfig(Backend, Profile);
			Config.TestSettings.RunMode = EPhysicsRunMode::Viz;   // the contact listener exists in viz runs only
			std::unique_ptr<FPhysScene> Scene = MakeScenarioScene("M1.SingleBoxDrop", { { "Case", "a" } }, Backend, Config);
			if (!Scene->GetBackendInfo().Has(PT_CAP_CONTACT_POINTS))
			{
				MESSAGE(Backend << " declares no contact points: skipped");
				continue;
			}
			for (int32 Frame = 0; Frame < NumFrames; ++Frame)
			{
				Scene->Tick(Config.TestSettings.FixedDeltaTime);
			}
			const FBodyInstance& Box = *Scene->GetBodies()[1];
			FPhysicsDebugSnapshot Snapshot;
			REQUIRE(Scene->CaptureDebugSnapshot(Snapshot));
			CHECK(Snapshot.Particles.size() == 2u);
			int32 NumBoxGround = 0;
			double MinPhi = 1e30;
			double MaxPhi = -1e30;
			double MinNormalZ = 1e30;
			for (const PtContactPoint& Contact : Snapshot.Contacts)
			{
				if (!((Contact.Body0 == 1u && Contact.Body1 == 0u) || (Contact.Body0 == 0u && Contact.Body1 == 1u)))
				{
					continue;
				}
				++NumBoxGround;
				CHECK(Contact.Body0 == 1u);   // Body0 = the more mobile body (the dynamic box)
				CHECK(std::isnan(Contact.NormalImpulse));   // Q8: Jolt exposes no impulse
				MinPhi = std::min(MinPhi, Contact.Phi);
				MaxPhi = std::max(MaxPhi, Contact.Phi);
				MinNormalZ = std::min(MinNormalZ, Contact.Normal.Z);
			}
			MESSAGE(Backend << " " << ProfileText << ": " << NumBoxGround << " box-ground contact points, normal.z >= " << MinNormalZ << ", Phi in [" << MinPhi
				<< ", " << MaxPhi << "] cm, box z = " << Box.State.BodyTM.GetTranslation().Z << " cm, state " << static_cast<int32>(Box.State.ObjectState));
			CHECK(NumBoxGround >= 1);
			CHECK(MinNormalZ > 0.999);
			CHECK(MinPhi >= -2.5);
			CHECK(MaxPhi <= 0.5);
			CHECK(Box.State.BodyTM.GetTranslation().Z == doctest::Approx(50.0).epsilon(0.05));
		}
		// Bench runs install no listener: no contacts, listeners.installed 0 (02 9.4).
		const FResolvedPhysicsConfig Bench = LoadSdkConfig(Backend, "Normalized");
		std::unique_ptr<FPhysScene> Scene = MakeScenarioScene("M1.SingleBoxDrop", { { "Case", "a" } }, Backend, Bench);
		for (int32 Frame = 0; Frame < 120; ++Frame)
		{
			Scene->Tick(Bench.TestSettings.FixedDeltaTime);
		}
		FPhysicsDebugSnapshot Snapshot;
		if (Scene->CaptureDebugSnapshot(Snapshot))
		{
			CHECK(Snapshot.Contacts.empty());
		}
		const std::vector<FEffectiveSetting> BenchSettings = ReadEffective(*Scene);
		const FEffectiveSetting* Listeners = FindSetting(BenchSettings, "listeners.installed");
		REQUIRE(Listeners != nullptr);
		CHECK(Listeners->Canonical == 0.0);
	}
}

// M2 review (3): a kinematic body reports the velocity of its actor ORIGIN (Chaos ApplyKinematicTargets), not of its COM.
TEST_CASE("PhysicsTest.SdkBackend.KinematicOriginVelocity")
{
	std::vector<std::string> Backends = SdkBackendNames();
	Backends.insert(Backends.begin(), "Null");
	for (const std::string& Backend : Backends)
	{
		CAPTURE(Backend);
		FPhysSceneInitParams InitParams;
		InitParams.DebugName = "KinematicOrigin";
		InitParams.BackendName = Backend;
		FPhysScene Scene(InitParams);
		FBodySetup Box;
		Box.AggGeom.BoxElems.emplace_back(40.0);
		const FBodySetup* BoxSetup = Scene.CreateBodySetup(std::move(Box));
		FBodyInstance Template;
		Template.bSimulatePhysics = false;   // kinematic
		Template.COMNudge = FVector(20.0, 0.0, 0.0);
		FBodyInstance* Body = Scene.SpawnBody(Template, BoxSetup, FTransform(FVector(0.0, 0.0, 500.0)));
		Scene.FlushDeferredActors();
		const double Dt = static_cast<double>(Scene.GetTestSettings().FixedDeltaTime);
		// 1) Rotate 30 deg about Z in place: the origin does not move (the COM sweeps 20 cm * 30 deg).
		Scene.SetKinematicTarget_AssumesLocked(Body, FTransform(FRotator(0.0, 30.0, 0.0), FVector(0.0, 0.0, 500.0)), true);
		Scene.Tick(Scene.GetTestSettings().FixedDeltaTime);
		MESSAGE(Backend << " rotate in place: v = " << Body->State.LinearVelocity.X << ", " << Body->State.LinearVelocity.Y << " cm/s, w.z = "
			<< Body->State.AngularVelocityInRadians.Z << " rad/s");
		CHECK(Body->State.LinearVelocity.Size() < 0.05);
		CHECK(Body->State.AngularVelocityInRadians.Z == doctest::Approx(FMath::DegreesToRadians(30.0) / Dt).epsilon(1e-4));
		CHECK(Body->State.BodyTM.GetTranslation().Z == doctest::Approx(500.0));
		// 2) Translate 60 cm along X: v = 60 / dt.
		Scene.SetKinematicTarget_AssumesLocked(Body, FTransform(FRotator(0.0, 30.0, 0.0), FVector(60.0, 0.0, 500.0)), true);
		Scene.Tick(Scene.GetTestSettings().FixedDeltaTime);
		CHECK(Body->State.LinearVelocity.X == doctest::Approx(60.0 / Dt).epsilon(1e-4));
		CHECK(std::abs(Body->State.AngularVelocityInRadians.Z) < 1e-3);
		// 3) No target: the velocity drops to zero (Chaos mode Reset) and the body stays.
		Scene.Tick(Scene.GetTestSettings().FixedDeltaTime);
		CHECK(Body->State.LinearVelocity.Size() < 1e-6);
		CHECK(Body->State.BodyTM.GetTranslation().X == doctest::Approx(60.0).epsilon(1e-6));
	}
}

// M2 review (2): PT_READ_ALL writes OutIndices[i] = i whenever OutIndices is given (the Kit does it for every backend).
TEST_CASE("PhysicsTest.SdkBackend.ReadAllWritesIndices")
{
	std::vector<std::string> Backends = SdkBackendNames();
	Backends.insert(Backends.begin(), { "Null", "NullDll" });
	for (const std::string& Backend : Backends)
	{
		CAPTURE(Backend);
		const FResolvedPhysicsConfig Config = LoadSdkConfig(Backend == "NullDll" ? "Null" : Backend, "Normalized");
		std::unique_ptr<FPhysScene> Scene = MakeScenarioScene("M1.SingleBoxDrop", {}, Backend, Config);
		Scene->Tick(Config.TestSettings.FixedDeltaTime);
		const PtBackendApi& Api = Scene->GetBackendScene().GetApi();
		std::vector<uint32_t> Indices(8, 0xDEADu);
		std::vector<PtBodyState> States(8);
		uint32_t Count = 0;
		REQUIRE(Api.ReadBodyStates(Scene->GetBackendScene().GetSceneId(), PT_READ_ALL, Indices.data(), States.data(), 8, &Count) == PT_OK);
		REQUIRE(Count == 2u);
		CHECK(Indices[0] == 0u);
		CHECK(Indices[1] == 1u);
		CHECK(Indices[2] == 0xDEADu);   // nothing past the count
	}
}

// Capacity (02 9.4, G12 in M6): M3.BoxPile 1k on its declared hints - no update error, no temp-allocator fallback.
TEST_CASE("PhysicsTest.SdkBackend.BoxPileCapacity")
{
	for (const std::string& Backend : SdkBackendNames())
	{
		CAPTURE(Backend);
		const FResolvedPhysicsConfig Config = LoadSdkConfig(Backend, "Normalized");
		std::unique_ptr<FPhysScene> Scene = MakeScenarioScene("M3.BoxPile", { { "N", "1000" } }, Backend, Config);
		uint32 ErrorBits = 0;
		for (int32 Frame = 0; Frame < 150; ++Frame)
		{
			Scene->Tick(Config.TestSettings.FixedDeltaTime);
			ErrorBits |= Scene->GetLastFrameTiming().EngineErrorBits;
		}
		CHECK(ErrorBits == 0u);
		const std::vector<FEffectiveSetting> Settings = ReadEffective(*Scene);
		for (const FEffectiveSetting& Setting : Settings)
		{
			if (Setting.Key.find("temp_allocator") != std::string::npos || Setting.Key == "jolt.capacity" || Setting.Key == "jolt.update_error_bits")
			{
				MESSAGE(Backend << " " << Setting.Key << " = " << Setting.Native);
			}
		}
		if (const FEffectiveSetting* Fallbacks = FindSetting(Settings, "jolt.temp_allocator_fallbacks"))
		{
			CHECK(Fallbacks->Canonical == 0.0);
		}
		// The pile is falling and colliding: most boxes are awake (sleep is off in Normalized).
		CHECK(Scene->GetStats().NumAwakeBodies == 1000);
	}
}

// The ABI body commands on bodies in free flight (no contact): an SDK backend agrees with the Null reference. Impulses
// and velocity changes are deferred to the step on every backend (docs/Abi.md 5.10, 7.1): bodies 6 to 10 check the
// orderings an immediate engine impulse (Jolt Body::AddImpulse, Bullet applyCentralImpulse) would get wrong.
TEST_CASE("PhysicsTest.SdkBackend.CommandsMatchNull")
{
	struct FResult
	{
		std::vector<FBodyInstanceState> States;
		FVector ReadBeforeImpulse = FVector::ZeroVector;   // body 7: FPhysicsInterface reads around an AddImpulse
		FVector ReadAfterImpulse = FVector::ZeroVector;
		FVector ReadAngularAfterImpulse = FVector::ZeroVector;
	};
	const auto Run = [](const std::string& Backend) -> FResult
	{
		FResolvedPhysicsConfig Config = LoadSdkConfig(Backend, "UEParity");   // sleep allowed: PutToSleep is honoured
		FPhysSceneInitParams InitParams;
		InitParams.DebugName = "Commands";
		InitParams.BackendName = Backend;
		InitParams.Config = &Config;
		FPhysScene Scene(InitParams);
		FBodySetup Box;
		Box.AggGeom.BoxElems.emplace_back(40.0, 30.0, 20.0);
		const FBodySetup* BoxSetup = Scene.CreateBodySetup(std::move(Box));
		FBodyInstance Template;
		FProfileResolver::ApplyBodyInstanceDefaults(Config, Template);
		Template.bSimulatePhysics = true;
		Template.bOverrideMass = true;
		Template.MassInKgOverride = 10.0f;
		std::vector<FBodyInstance*> Bodies;
		for (int32 Index = 0; Index < 11; ++Index)
		{
			Bodies.push_back(Scene.SpawnBody(Template, BoxSetup, FTransform(FVector(1000.0 * Index, 0.0, 5000.0))));
		}
		Scene.FlushDeferredActors();
		const float Dt = Config.TestSettings.FixedDeltaTime;
		// A raw ABI command (the host's own verbs never send a waking force before PutToSleep: FPhysScene holds forces).
		const auto SendRaw = [&Scene](const FBodyInstance* Body, const uint32 Type, const FVector& Vector, const uint32 Flags)
		{
			PtBodyCommand Command = {};
			Command.Type = Type;
			Command.Body = Body->ActorHandle.Index;
			Command.Flags = Flags;
			Command.Transform.Rotation.W = 1.0;
			Command.Vector = PtVec3{ Vector.X, Vector.Y, Vector.Z };
			Scene.GetBackendScene().EnqueueCommand(Command);
		};
		FResult Result;
		Bodies[0]->AddImpulse(FVector(1000.0, 0.0, 0.0), false);                 // 10 kg: +100 cm/s
		Bodies[1]->AddImpulse(FVector(0.0, 50.0, 0.0), true);                    // velocity change
		Bodies[2]->SetLinearVelocity(FVector(0.0, 0.0, 300.0), false);
		Bodies[3]->AddAngularImpulseInRadians(FVector(0.0, 0.0, 2.0), true);     // dw = 2 rad/s
		Bodies[4]->PutInstanceToSleep();
		// 6: AddImpulse, then SetLinearVelocity in the same frame: the step gives V + J/m (UE: LinearImpulseVelocity is added
		// to the set velocity in Integrate). An immediate impulse would be overwritten by the set.
		Bodies[6]->AddImpulse(FVector(0.0, 2000.0, 0.0), false);                 // +200 cm/s on top of the set velocity
		Bodies[6]->SetLinearVelocity(FVector(150.0, 0.0, 0.0), false);
		// 8: AddImpulse, then PutInstanceToSleep: the impulse waits for the step after the wake (Null keeps it pending).
		Bodies[8]->AddImpulse(FVector(0.0, 0.0, 3000.0), false);
		Bodies[8]->AddAngularImpulseInRadians(FVector(1.0, 0.0, 0.0), true);
		Bodies[8]->PutInstanceToSleep();
		for (int32 Frame = 0; Frame < 30; ++Frame)
		{
			Bodies[5]->AddForce(FVector(0.0, 1000.0, 0.0));                      // 10 kg: 100 cm/s^2 every frame
			if (Frame == 10)
			{
				// 7: a read between the impulse and the step (flush + pull) sees the velocity before the impulse.
				Result.ReadBeforeImpulse = FPhysicsInterface::GetLinearVelocity_AssumesLocked(Bodies[7]->ActorHandle);
				Bodies[7]->AddImpulse(FVector(500.0, 0.0, 0.0), false);
				Bodies[7]->AddAngularImpulseInRadians(FVector(0.0, 0.0, 3.0), true);
				Result.ReadAfterImpulse = FPhysicsInterface::GetLinearVelocity_AssumesLocked(Bodies[7]->ActorHandle);
				Result.ReadAngularAfterImpulse = FPhysicsInterface::GetAngularVelocity_AssumesLocked(Bodies[7]->ActorHandle);
				// 9: a waking force, then PUT_TO_SLEEP in one push: the force dies with the push (never acts after a later wake).
				SendRaw(Bodies[9], PT_CMD_ADD_FORCE, FVector(0.0, 0.0, 50000.0), 0u);
				SendRaw(Bodies[9], PT_CMD_PUT_TO_SLEEP, FVector::ZeroVector, 0u);
				// 10: force, sleep, wake in one push: the body is awake at the step, so the force acts (as Null).
				SendRaw(Bodies[10], PT_CMD_ADD_FORCE, FVector(0.0, 0.0, 50000.0), 0u);
				SendRaw(Bodies[10], PT_CMD_PUT_TO_SLEEP, FVector::ZeroVector, 0u);
				SendRaw(Bodies[10], PT_CMD_WAKE_UP, FVector::ZeroVector, 0u);
			}
			if (Frame == 20)
			{
				Bodies[4]->WakeInstance();
				Bodies[8]->WakeInstance();
				Bodies[9]->WakeInstance();
			}
			Scene.Tick(Dt);
		}
		for (FBodyInstance* Body : Bodies)
		{
			Result.States.push_back(Body->State);
		}
		return Result;
	};
	const FResult Null = Run("Null");
	for (const std::string& Backend : SdkBackendNames())
	{
		CAPTURE(Backend);
		const FResult Sdk = Run(Backend);
		REQUIRE(Sdk.States.size() == Null.States.size());
		for (size_t Index = 0; Index < Null.States.size(); ++Index)
		{
			CAPTURE(Index);
			const FBodyInstanceState& A = Null.States[Index];
			const FBodyInstanceState& B = Sdk.States[Index];
			// float engine state (cm at 1e-5 m resolution near 50 m) against double: generous absolute bounds.
			CHECK((A.BodyTM.GetTranslation() - B.BodyTM.GetTranslation()).Size() < 0.05);
			CHECK((A.LinearVelocity - B.LinearVelocity).Size() < 0.05);
			CHECK((A.AngularVelocityInRadians - B.AngularVelocityInRadians).Size() < 1e-3);
			CHECK(A.ObjectState == B.ObjectState);
		}
		MESSAGE(Backend << ": body 7 read after AddImpulse v = (" << Sdk.ReadAfterImpulse.X << ", " << Sdk.ReadAfterImpulse.Y << ", " << Sdk.ReadAfterImpulse.Z
			<< ") cm/s (Null " << Null.ReadAfterImpulse.X << ", " << Null.ReadAfterImpulse.Y << ", " << Null.ReadAfterImpulse.Z << ")");
		CHECK((Sdk.ReadAfterImpulse - Null.ReadAfterImpulse).Size() < 0.05);
		CHECK((Sdk.ReadAfterImpulse - Sdk.ReadBeforeImpulse).Size() < 1e-3);   // deferred: nothing before the step
		CHECK(Sdk.ReadAngularAfterImpulse.Size() < 1e-6);
	}
	// Sanity on the reference itself (UEParity: linear damping 0.01 1/s, so velocities are within 1% of the undamped values).
	CHECK(Null.States[4].ObjectState == EObjectStateType::Dynamic);   // slept, then woken at frame 20
	CHECK(Null.States[5].LinearVelocity.Y > 0.0);
	CHECK(Null.States[6].LinearVelocity.X == doctest::Approx(150.0).epsilon(0.01));   // the set velocity ...
	CHECK(Null.States[6].LinearVelocity.Y == doctest::Approx(200.0).epsilon(0.01));   // ... plus the impulse of the same frame
	CHECK(Null.ReadAfterImpulse.X == Null.ReadBeforeImpulse.X);
	CHECK(Null.States[7].LinearVelocity.X == doctest::Approx(50.0).epsilon(0.01));
	CHECK(Null.States[7].AngularVelocityInRadians.Z == doctest::Approx(3.0));
	// 8 and 4 both slept from frame 0 to 20: 8 differs by the impulse it kept through the sleep.
	CHECK(Null.States[8].ObjectState == EObjectStateType::Dynamic);
	CHECK(Null.States[8].LinearVelocity.Z - Null.States[4].LinearVelocity.Z == doctest::Approx(300.0).epsilon(0.01));
	CHECK(Null.States[8].AngularVelocityInRadians.X == doctest::Approx(1.0));
	// 9 slept at frame 10 with V = 0 and woke at 20, as 4 did: equal velocity, the force of the sleeping push never acted.
	CHECK(Null.States[9].ObjectState == EObjectStateType::Dynamic);
	CHECK(Null.States[9].LinearVelocity.Z == doctest::Approx(Null.States[4].LinearVelocity.Z));
	// 10: V = 0 at frame 10 (PUT_TO_SLEEP), then 20 steps of gravity and one step of 50000 / 10 kg = 5000 cm/s^2.
	CHECK(Null.States[10].LinearVelocity.Z == doctest::Approx((-980.0 * 20.0 + 5000.0) / 60.0).epsilon(0.01));
}

// G2 readback (M6) note, docs/Abi.md 5.7: an engine may re-diagonalise the inertia it was given and order the principal
// moments its own way (Jolt: largest first, MassProperties::DecomposePrincipalMomentsOfInertia). ReadMassProperties then
// returns a permuted diagonal with a matching rotation, so the comparable quantity is the tensor R diag(I) R^T.
TEST_CASE("PhysicsTest.SdkBackend.MassReadbackIsATensor")
{
	const auto Tensor = [](const FQuat& Rotation, const FVector& Diagonal, double (&Out)[3][3])
	{
		const FVector Axes[3] = { Rotation.GetAxisX(), Rotation.GetAxisY(), Rotation.GetAxisZ() };
		const double Moments[3] = { Diagonal.X, Diagonal.Y, Diagonal.Z };
		for (int Row = 0; Row < 3; ++Row)
		{
			for (int Column = 0; Column < 3; ++Column)
			{
				Out[Row][Column] = 0.0;
				for (int Axis = 0; Axis < 3; ++Axis)
				{
					const double A[3] = { Axes[Axis].X, Axes[Axis].Y, Axes[Axis].Z };
					Out[Row][Column] += Moments[Axis] * A[Row] * A[Column];
				}
			}
		}
	};
	for (const std::string& Backend : SdkBackendNames())
	{
		CAPTURE(Backend);
		FPhysSceneInitParams InitParams;
		InitParams.DebugName = "MassReadback";
		InitParams.BackendName = Backend;
		FPhysScene Scene(InitParams);
		FBodySetup Plank;
		Plank.AggGeom.BoxElems.emplace_back(200.0, 50.0, 20.0);   // I_x < I_y < I_z: a sorted engine diagonal is a permutation
		const FBodySetup* PlankSetup = Scene.CreateBodySetup(std::move(Plank));
		FBodyInstance Template;
		Template.bSimulatePhysics = true;
		Template.bEnableGravity = false;
		const FBodyInstance* Body = Scene.SpawnBody(Template, PlankSetup, FTransform(FVector(0.0, 0.0, 500.0)));
		Scene.FlushDeferredActors();
		PtMassProperties Engine = {};
		REQUIRE(Scene.GetBackendScene().ReadMassProperties(Body->ActorHandle.Index, std::span<PtMassProperties>(&Engine, 1)) == PT_OK);
		double Harness[3][3];
		double Read[3][3];
		Tensor(Body->MassProperties.RotationOfMass, Body->MassProperties.InertiaTensor, Harness);
		Tensor(FQuat(Engine.CenterOfMassLocal.Rotation.X, Engine.CenterOfMassLocal.Rotation.Y, Engine.CenterOfMassLocal.Rotation.Z, Engine.CenterOfMassLocal.Rotation.W),
			FVector(Engine.InertiaDiagonal.X, Engine.InertiaDiagonal.Y, Engine.InertiaDiagonal.Z), Read);
		double Difference = 0.0;
		double Norm = 0.0;
		for (int Row = 0; Row < 3; ++Row)
		{
			for (int Column = 0; Column < 3; ++Column)
			{
				Difference += (Read[Row][Column] - Harness[Row][Column]) * (Read[Row][Column] - Harness[Row][Column]);
				Norm += Harness[Row][Column] * Harness[Row][Column];
			}
		}
		const double RelError = std::sqrt(Difference / Norm);
		const bool bPermuted = Engine.InertiaDiagonal.X != doctest::Approx(Body->MassProperties.InertiaTensor.X).epsilon(1e-5);
		MESSAGE(Backend << ": harness diagonal (" << Body->MassProperties.InertiaTensor.X << ", " << Body->MassProperties.InertiaTensor.Y << ", "
			<< Body->MassProperties.InertiaTensor.Z << "), engine diagonal (" << Engine.InertiaDiagonal.X << ", " << Engine.InertiaDiagonal.Y << ", "
			<< Engine.InertiaDiagonal.Z << ")" << std::string(bPermuted ? " permuted" : "") << ", tensor relative error " << RelError);
		CHECK(RelError < 1e-5);
		CHECK(Engine.Mass == doctest::Approx(Body->MassProperties.Mass).epsilon(1e-6));
	}
}
