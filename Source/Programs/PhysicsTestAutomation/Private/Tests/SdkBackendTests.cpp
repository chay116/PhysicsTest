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
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
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

	// Variant "" = <Backend>.Default when that file exists, else the variant marked DefaultForEngine (PhysX: UE4Compat), else none
	// (the bench's resolution without -BackendProfile=).
	FResolvedPhysicsConfig LoadSdkConfig(const std::string& Backend, const std::string& Profile, const std::string& Variant = {})
	{
		FBenchRunSpec Spec;
		Spec.Backend = Backend;
		Spec.BackendProfile = Variant;
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

	// R diag(I) R^T: the comparable inertia (an engine may re-diagonalise and order the principal moments its own way).
	void InertiaTensor(const FQuat& Rotation, const FVector& Diagonal, double (&Out)[3][3])
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
	}

	// Frobenius relative error of the engine's inertia tensor against the harness body's.
	double InertiaTensorRelError(const FBodyInstance& Body, const PtMassProperties& Engine)
	{
		double Harness[3][3];
		double Read[3][3];
		InertiaTensor(Body.MassProperties.RotationOfMass, Body.MassProperties.InertiaTensor, Harness);
		InertiaTensor(FQuat(Engine.CenterOfMassLocal.Rotation.X, Engine.CenterOfMassLocal.Rotation.Y, Engine.CenterOfMassLocal.Rotation.Z, Engine.CenterOfMassLocal.Rotation.W),
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
		return std::sqrt(Difference / Norm);
	}

	// Config/Profiles/Backend/<Backend>.<Variant>.ini -> the variant names (the viewer's Engines list); none = { "" }.
	std::vector<std::string> SdkVariantNames(const std::string& Backend)
	{
		std::vector<std::string> Names;
		const std::string Prefix = Backend + ".";
		std::error_code Error;
		for (const std::filesystem::directory_entry& Entry : std::filesystem::directory_iterator(SdkTestConfigDir() / "Profiles" / "Backend", Error))
		{
			const std::string Stem = StringConv::PathToUtf8(Entry.path().stem());
			if (Entry.path().extension() == ".ini" && Stem.size() > Prefix.size() && Stem.compare(0, Prefix.size(), Prefix) == 0)
			{
				Names.push_back(Stem.substr(Prefix.size()));
			}
		}
		std::sort(Names.begin(), Names.end());
		if (Names.empty())
		{
			Names.emplace_back();
		}
		return Names;
	}

	// A GLOBAL_SCENE_SETTINGS module (PhysX: one PxTolerancesScale per process) that is idle and initialised with other init
	// options than Wanted is terminated, so that the next scene initialises it with Wanted (M5).
	void PrepareGlobalSettingsModule(const std::string& Backend, const std::vector<std::pair<std::string, std::string>>& Wanted)
	{
		std::string Error;
		FPhysicsBackendModule* Module = FPhysicsBackendRegistry::Get().FindOrLoadModule(Backend, Error);
		REQUIRE_MESSAGE(Module != nullptr, Error);
		if (Module->IsInitialized() && (Module->GetInfo().ModuleFlags & PT_MODULE_GLOBAL_SCENE_SETTINGS) != 0 && Module->GetInitParams().InitOptions != Wanted)
		{
			REQUIRE(Module->GetNumLiveScenes() == 0);
			REQUIRE(Module->TermGamePhys() == PT_OK);
		}
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
		// A capsule needs an axis remap only where the engine capsule is not along UE's Z (Jolt Y, PhysX X); Bullet's
		// btCapsuleShapeZ needs none (D1 amendment, DL47). PhysX has no wrapper objects and counts the same conditions
		// (several elements = compound, harness COM != PhysX geometric COM = offset_com, U2PSphylBasis = axis_remap; M5).
		const bool bZCapsule = Backend == "Bullet";
		CHECK(Wrapping->Canonical >= (bZCapsule ? 2.0 : 3.0));
		CHECK(Wrapping->Native.find(bZCapsule ? "axis_remap 0" : "axis_remap 1") != std::string::npos);
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
			Config.TestSettings.RunMode = EPhysicsRunMode::Viz;   // the contact listener exists in viz runs (or with PT_INSTR_CONTACTS)
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
				if (Scene->GetBackendInfo().Has(PT_CAP_CONTACT_NORMAL_IMPULSE))
				{
					CHECK(Contact.NormalImpulse >= 0.0);   // Bullet: the last solve's impulse (kg cm/s)
				}
				else
				{
					CHECK(std::isnan(Contact.NormalImpulse));   // Q8: n/a (Jolt exposes no impulse; PhysX's per-point impulse is M9)
				}
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
		const double RelError = InertiaTensorRelError(*Body, Engine);
		const bool bPermuted = Engine.InertiaDiagonal.X != doctest::Approx(Body->MassProperties.InertiaTensor.X).epsilon(1e-5);
		MESSAGE(Backend << ": harness diagonal (" << Body->MassProperties.InertiaTensor.X << ", " << Body->MassProperties.InertiaTensor.Y << ", "
			<< Body->MassProperties.InertiaTensor.Z << "), engine diagonal (" << Engine.InertiaDiagonal.X << ", " << Engine.InertiaDiagonal.Y << ", "
			<< Engine.InertiaDiagonal.Z << ")" << std::string(bPermuted ? " permuted" : "") << ", tensor relative error " << RelError);
		CHECK(RelError < 1e-5);
		CHECK(Engine.Mass == doctest::Approx(Body->MassProperties.Mass).epsilon(1e-6));
	}
}

// M3 review minor (4), fixed in M4: a Jolt assert outside PhysicsSystem::Update (creation, commands, reads, snapshots) used to
// raise only the module counter jolt.asserts, so a dbg run ended with status ok. Now the next frame reports engine error bit
// 0x200 and the scene's step stats keep it (the bench's exit code 3) - also when a query between frames (GetStepStats, as the
// viewer's Stats panel calls it every frame) sees the assert first. The harness test key pt.test.raise_assert (runtime
// SetOption only) counts one assert exactly as the dbg hook does, in every build; as a CreateScene option it is rejected.
TEST_CASE("PhysicsTest.SdkBackend.JoltAssertOutsideUpdateIsAnEngineError")
{
	const std::vector<std::string> Available = FPhysicsBackendRegistry::Get().GetAvailableNames();
	if (std::find(Available.begin(), Available.end(), "Jolt") == Available.end())
	{
		return;
	}
	constexpr uint32 AssertBit = 1u << 9;
	constexpr const char* TestKey = "pt.test.raise_assert";
	const FResolvedPhysicsConfig Config = LoadSdkConfig("Jolt", "Normalized");
	std::unique_ptr<FPhysScene> Scene = MakeScenarioScene("S00.FreeFall", {}, "Jolt", Config);
	Scene->Tick(Config.TestSettings.FixedDeltaTime);
	CHECK((Scene->GetLastFrameTiming().EngineErrorBits & AssertBit) == 0u);
	// Between frames (Idle): the assert belongs to the next frame.
	REQUIRE(Scene->GetBackendScene().SetOption(TestKey, "1") == PT_OK);
	Scene->Tick(Config.TestSettings.FixedDeltaTime);
	CHECK((Scene->GetLastFrameTiming().EngineErrorBits & AssertBit) != 0u);
	PtStepStats Stats = {};
	Stats.StructSize = sizeof(PtStepStats);
	REQUIRE(Scene->GetBackendScene().GetStepStats(Stats) == PT_OK);
	CHECK((Stats.EngineErrorBits & AssertBit) != 0u);   // sticky for the run
	Scene->Tick(Config.TestSettings.FixedDeltaTime);
	CHECK((Scene->GetLastFrameTiming().EngineErrorBits & AssertBit) == 0u);   // one frame reports it once
	// A query between frames sees the assert first (the viewer's Stats panel): the step stats have it at once and the next
	// frame still reports it.
	REQUIRE(Scene->GetBackendScene().SetOption(TestKey, "1") == PT_OK);
	PtStepStats Between = {};
	Between.StructSize = sizeof(PtStepStats);
	REQUIRE(Scene->GetBackendScene().GetStepStats(Between) == PT_OK);
	CHECK((Between.EngineErrorBits & AssertBit) != 0u);
	Scene->Tick(Config.TestSettings.FixedDeltaTime);
	CHECK((Scene->GetLastFrameTiming().EngineErrorBits & AssertBit) != 0u);
	Scene->Tick(Config.TestSettings.FixedDeltaTime);
	CHECK((Scene->GetLastFrameTiming().EngineErrorBits & AssertBit) == 0u);
	// After the last frame: GetStepStats itself checks (the bench reads it after its loop).
	std::unique_ptr<FPhysScene> Fresh = MakeScenarioScene("S00.FreeFall", {}, "Jolt", Config);
	Fresh->Tick(Config.TestSettings.FixedDeltaTime);
	REQUIRE(Fresh->GetBackendScene().SetOption(TestKey, "1") == PT_OK);
	PtStepStats After = {};
	After.StructSize = sizeof(PtStepStats);
	REQUIRE(Fresh->GetBackendScene().GetStepStats(After) == PT_OK);
	CHECK((After.EngineErrorBits & AssertBit) != 0u);
	// The test key is not a scene option: CreateScene (profiles, backend variants, the viewer's preflight) rejects it.
	std::string Error;
	FPhysicsBackendModule* Module = FPhysicsBackendRegistry::Get().FindOrLoadModule("Jolt", Error);
	REQUIRE(Module != nullptr);
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
	CHECK(FPhysicsBackendScene::Create(*Module, Desc, Error) == nullptr);
	CHECK(Error.find("unknown scene option") != std::string::npos);
	MESSAGE("as a CreateScene option: " << Error);
}

// Checkpoint A (03_WorkPlan 1.6, item 2) for every SDK engine and every backend variant file (Jolt, Bullet, PhysX UE4Compat /
// -TGS / -Stab / SdkDefault): S00.FreeFall |z60 - 501.83328| <= 2e-3 cm; the resting contact of M1.SingleBoxDrop a
// (Normalized, 300 frames, viz run: the listener exists) has normal . (+Z) > 0.999; the engine's mass and inertia readback
// of the box equal the harness values within 1e-5 (relative; the inertia as the tensor R diag(I) R^T). One MESSAGE line
// per row is the table of docs/Checkpoints.md. A GLOBAL_SCENE_SETTINGS module is re-initialised between variants with
// other init options (PhysX SdkDefault) and left with its default (no) init options.
TEST_CASE("PhysicsTest.SdkBackend.CheckpointAConventions")
{
	int32 NumRows = 0;
	for (const std::string& Backend : SdkBackendNames())
	{
		for (const std::string& Variant : SdkVariantNames(Backend))
		{
			const std::string Row = Backend + (Variant.empty() ? std::string() : "/" + Variant);
			CAPTURE(Row);
			FResolvedPhysicsConfig Config = LoadSdkConfig(Backend, "Normalized", Variant);
			PrepareGlobalSettingsModule(Backend, Config.InitOptions);
			double Z60 = 0.0;
			{
				std::unique_ptr<FPhysScene> FreeFall = MakeScenarioScene("S00.FreeFall", {}, Backend, Config);
				for (int32 Frame = 0; Frame < Fixtures::FFreeFall::Frames; ++Frame)
				{
					FreeFall->Tick(Config.TestSettings.FixedDeltaTime);
				}
				Z60 = FreeFall->GetBodies()[0]->State.BodyTM.GetTranslation().Z;
			}
			CHECK(std::abs(Z60 - 501.83328) <= 2e-3);

			Config.TestSettings.RunMode = EPhysicsRunMode::Viz;
			std::unique_ptr<FPhysScene> Scene = MakeScenarioScene("M1.SingleBoxDrop", { { "Case", "a" } }, Backend, Config);
			for (int32 Frame = 0; Frame < 300; ++Frame)
			{
				Scene->Tick(Config.TestSettings.FixedDeltaTime);
			}
			const FBodyInstance& Box = *Scene->GetBodies()[1];
			const uint32 GroundIndex = Scene->GetBodies()[0]->ActorHandle.Index;
			FPhysicsDebugSnapshot Snapshot;
			REQUIRE(Scene->CaptureDebugSnapshot(Snapshot));
			int32 NumPoints = 0;
			double MinNormalZ = 1e30;
			for (const PtContactPoint& Contact : Snapshot.Contacts)
			{
				if (Contact.Body0 == Box.ActorHandle.Index && Contact.Body1 == GroundIndex)
				{
					++NumPoints;
					MinNormalZ = std::min(MinNormalZ, Contact.Normal.Z);
				}
			}
			CHECK(NumPoints >= 1);
			CHECK(MinNormalZ > 0.999);

			PtMassProperties Engine = {};
			REQUIRE(Scene->GetBackendScene().ReadMassProperties(Box.ActorHandle.Index, std::span<PtMassProperties>(&Engine, 1)) == PT_OK);
			const double MassRelError = std::abs(Engine.Mass - Box.MassProperties.Mass) / Box.MassProperties.Mass;
			const double InertiaRelError = InertiaTensorRelError(Box, Engine);
			CHECK(MassRelError <= 1e-5);
			CHECK(InertiaRelError <= 1e-5);
			MESSAGE(Row << ": S00 z60 = " << Z60 << " cm (|dz| = " << std::abs(Z60 - 501.83328) << " cm); M1 a resting contact " << NumPoints
				<< " points, min normal.z = " << MinNormalZ << ", box z = " << Box.State.BodyTM.GetTranslation().Z << " cm; mass " << Engine.Mass
				<< " kg (harness " << Box.MassProperties.Mass << " kg, relative error " << MassRelError << "), inertia tensor relative error "
				<< InertiaRelError);
			++NumRows;
		}
	}
	CHECK(NumRows >= static_cast<int32>(SdkBackendNames().size()));
	for (const std::string& Backend : SdkBackendNames())
	{
		PrepareGlobalSettingsModule(Backend, {});
	}
}

// M5 round 0 (the PhysX kept-contact bug): a body put to sleep by PT_CMD_PUT_TO_SLEEP keeps the points of its last step,
// each exactly once and flagged PT_CONTACT_SLEEPING, for as long as it sleeps. M1.SingleBoxDrop a under UEParity in a viz
// run: the box rests awake at frame 40 (it falls asleep by itself later), the command puts it to sleep between two frames,
// and 5 and then 30 frames later the snapshot still holds the box-ground points of the step before the command - the same
// count, never doubled (PhysX listed every point twice before M5 round 0).
TEST_CASE("PhysicsTest.SdkBackend.PutToSleepKeepsEachContactOnce")
{
	for (const std::string& Backend : SdkBackendNames())
	{
		CAPTURE(Backend);
		FResolvedPhysicsConfig Config = LoadSdkConfig(Backend, "UEParity");
		PrepareGlobalSettingsModule(Backend, Config.InitOptions);
		if (Backend == "Jolt")
		{
			// M9: Jolt maps UE's sleep thresholds (SleepThresholdMapping=UE; the resting box would sleep 5 frames after it
			// lands, frame 36). This test is about the command path, so Jolt keeps its own 0.5 s timer: awake at frame 40.
			bool bSet = false;
			for (auto& [Key, Value] : Config.SceneOptions)
			{
				if (Key == "SleepThresholdMapping")
				{
					Value = "Native";
					bSet = true;
				}
			}
			if (!bSet)
			{
				Config.SceneOptions.emplace_back("SleepThresholdMapping", "Native");
			}
		}
		Config.TestSettings.RunMode = EPhysicsRunMode::Viz;   // the contact listener exists in viz runs
		std::unique_ptr<FPhysScene> Scene = MakeScenarioScene("M1.SingleBoxDrop", { { "Case", "a" } }, Backend, Config);
		if (!Scene->GetBackendInfo().Has(PT_CAP_CONTACT_POINTS))
		{
			MESSAGE(Backend << " declares no contact points: skipped");
			continue;
		}
		FBodyInstance& Box = *Scene->GetBodies()[1];
		const uint32 BoxIndex = Box.ActorHandle.Index;
		const uint32 GroundIndex = Scene->GetBodies()[0]->ActorHandle.Index;
		for (int32 Frame = 0; Frame < 40; ++Frame)
		{
			Scene->Tick(Config.TestSettings.FixedDeltaTime);
		}
		REQUIRE(Box.State.ObjectState == EObjectStateType::Dynamic);   // resting, still awake
		const auto CountBoxGround = [&](uint32& OutSleeping)
		{
			FPhysicsDebugSnapshot Snapshot;
			REQUIRE(Scene->CaptureDebugSnapshot(Snapshot));
			uint32 Count = 0;
			OutSleeping = 0;
			for (const PtContactPoint& Contact : Snapshot.Contacts)
			{
				if ((Contact.Body0 == BoxIndex && Contact.Body1 == GroundIndex) || (Contact.Body0 == GroundIndex && Contact.Body1 == BoxIndex))
				{
					++Count;
					OutSleeping += (Contact.Flags & PT_CONTACT_SLEEPING) != 0u ? 1u : 0u;
				}
			}
			return Count;
		};
		uint32 Sleeping = 0;
		const uint32 Before = CountBoxGround(Sleeping);
		CHECK(Before >= 1u);
		CHECK(Sleeping == 0u);
		Box.PutInstanceToSleep();
		REQUIRE(Scene->GetBackendScene().FlushCommands() == PT_OK);
		for (const int32 Frames : { 5, 30 })
		{
			for (int32 Frame = 0; Frame < Frames; ++Frame)
			{
				Scene->Tick(Config.TestSettings.FixedDeltaTime);
			}
			const uint32 After = CountBoxGround(Sleeping);
			MESSAGE(Backend << ": " << Before << " box-ground points before PUT_TO_SLEEP, " << After << " (" << Sleeping << " flagged sleeping) after "
				<< Frames << " more frames, state " << static_cast<int32>(Box.State.ObjectState));
			CHECK(Box.State.ObjectState == EObjectStateType::Sleeping);
			CHECK(After == Before);
			CHECK(Sleeping == After);
		}
	}
	for (const std::string& Backend : SdkBackendNames())
	{
		PrepareGlobalSettingsModule(Backend, {});
	}
}

// The ABI v1.0 contract (M6, docs/Abi.md 5.10; checkpoint A item O-1): a velocity written to a SLEEPING dynamic body wakes
// it, whatever PT_CMDF_AUTO_WAKE says, and the body moves with it - UE 5.8.3's game-thread SetV / SetW wake a sleeping
// particle for a vector that is not nearly zero (SetVBase / SetWBase) and for an exactly zero one (through SetX / SetR),
// SingleParticlePhysicsProxy.h:322-340, :362-380, :1217-1244, :240-251, :261-271 [V]; FChaosEngineInterface supports only
// bAutoWake = true (ensure, ChaosEngineInterface.cpp:626-658 [V]). SET_BODY_STATE (a teleport plus velocities) wakes too.
// Before the freeze the engines differed (M5 round 0): Null and Jolt kept the body asleep with the velocity, Bullet lost
// the velocity (updateActivationState zeroes an ISLAND_SLEEPING body, btDiscreteDynamicsWorld.cpp:605-620 [V]), PhysX woke
// it for a non-zero vector only (NpRigidDynamic.cpp:253, :286 [V]). Body 0: SET_LINEAR_VELOCITY without AUTO_WAKE; body 1:
// SET_BODY_STATE at its own pose with the same velocity; body 2: SET_LINEAR_VELOCITY with the zero vector.
TEST_CASE("PhysicsTest.SdkBackend.VelocityOfASleepingBody")
{
	std::vector<std::string> Backends = SdkBackendNames();
	Backends.insert(Backends.begin(), "Null");
	for (const std::string& Backend : Backends)
	{
		CAPTURE(Backend);
		const FResolvedPhysicsConfig Config = LoadSdkConfig(Backend, "UEParity");   // sleep allowed
		PrepareGlobalSettingsModule(Backend, Config.InitOptions);
		FPhysSceneInitParams InitParams;
		InitParams.DebugName = "SleepingVelocity";
		InitParams.BackendName = Backend;
		InitParams.Config = &Config;
		FPhysScene Scene(InitParams);
		FBodySetup Box;
		Box.AggGeom.BoxElems.emplace_back(40.0);
		const FBodySetup* BoxSetup = Scene.CreateBodySetup(std::move(Box));
		FBodyInstance Template;
		FProfileResolver::ApplyBodyInstanceDefaults(Config, Template);
		Template.bSimulatePhysics = true;
		Template.bEnableGravity = false;
		const FTransform Poses[3] = { FTransform(FVector(0.0, 0.0, 5000.0)), FTransform(FVector(0.0, 1000.0, 5000.0)), FTransform(FVector(0.0, 2000.0, 5000.0)) };
		FBodyInstance* Bodies[3] = { Scene.SpawnBody(Template, BoxSetup, Poses[0]), Scene.SpawnBody(Template, BoxSetup, Poses[1]),
			Scene.SpawnBody(Template, BoxSetup, Poses[2]) };
		Scene.FlushDeferredActors();
		const float Dt = Config.TestSettings.FixedDeltaTime;
		for (FBodyInstance* Body : Bodies)
		{
			Body->PutInstanceToSleep();
		}
		Scene.Tick(Dt);
		Scene.Tick(Dt);
		for (FBodyInstance* Body : Bodies)
		{
			REQUIRE(Body->State.ObjectState == EObjectStateType::Sleeping);
		}
		PtBodyCommand SetVelocity = {};
		SetVelocity.Type = PT_CMD_SET_LINEAR_VELOCITY;
		SetVelocity.Body = Bodies[0]->ActorHandle.Index;
		SetVelocity.Transform.Rotation.W = 1.0;
		SetVelocity.Vector = PtVec3{ 100.0, 0.0, 0.0 };
		Scene.GetBackendScene().EnqueueCommand(SetVelocity);
		PtBodyCommand SetState = {};
		SetState.Type = PT_CMD_SET_BODY_STATE;
		SetState.Body = Bodies[1]->ActorHandle.Index;
		SetState.Transform.Rotation.W = 1.0;
		SetState.Transform.Translation = PtVec3{ Poses[1].GetTranslation().X, Poses[1].GetTranslation().Y, Poses[1].GetTranslation().Z };
		SetState.Vector = PtVec3{ 100.0, 0.0, 0.0 };
		Scene.GetBackendScene().EnqueueCommand(SetState);
		PtBodyCommand SetZero = SetVelocity;
		SetZero.Body = Bodies[2]->ActorHandle.Index;
		SetZero.Vector = PtVec3{ 0.0, 0.0, 0.0 };
		Scene.GetBackendScene().EnqueueCommand(SetZero);
		for (int32 Frame = 0; Frame < 3; ++Frame)
		{
			Scene.Tick(Dt);
		}
		for (int32 Index = 0; Index < 3; ++Index)
		{
			CAPTURE(Index);
			CHECK(Bodies[Index]->State.ObjectState == EObjectStateType::Dynamic);
			const double Vx = Bodies[Index]->State.LinearVelocity.X;
			if (Index < 2)
			{
				CHECK(Vx == doctest::Approx(100.0).epsilon(0.01));   // UEParity linear damping 0.01 1/s
				CHECK(Bodies[Index]->State.BodyTM.GetTranslation().X > 4.0);   // it moved (3 steps at 100 cm/s = 5 cm)
			}
			else
			{
				CHECK(std::abs(Vx) < 1e-6);
			}
		}
		MESSAGE(Backend << ": SET_LINEAR_VELOCITY (no AUTO_WAKE), SET_BODY_STATE and a zero SET_LINEAR_VELOCITY wake the sleeping body; v.x "
			<< Bodies[0]->State.LinearVelocity.X << " / " << Bodies[1]->State.LinearVelocity.X << " cm/s");
	}
	for (const std::string& Backend : SdkBackendNames())
	{
		PrepareGlobalSettingsModule(Backend, {});
	}
}

// 03 M5 3a and docs/Abi.md 5.14 across the SDK backends (M5 round 0): the material combine rules are read back from the
// engine (PhysX PxMaterial, Jolt PhysicsSystem::GetCombine*, Bullet's global callbacks), and what the host passed in or the
// adapter counted itself is never READBACK.
TEST_CASE("PhysicsTest.SdkBackend.ReadbackIsReadFromTheEngine")
{
	for (const std::string& Backend : SdkBackendNames())
	{
		CAPTURE(Backend);
		const FResolvedPhysicsConfig Config = LoadSdkConfig(Backend, "UEParity");
		PrepareGlobalSettingsModule(Backend, Config.InitOptions);
		std::unique_ptr<FPhysScene> Scene = MakeScenarioScene("M1.SingleBoxDrop", { { "Case", "a" } }, Backend, Config);
		const std::vector<FEffectiveSetting> Settings = ReadEffective(*Scene);
		for (const char* Key : { "material.friction_combine", "material.restitution_combine", "shape.wrapping" })
		{
			const FEffectiveSetting* Setting = FindSetting(Settings, Key);
			const std::string KeyText(Key);
			CAPTURE(KeyText);
			REQUIRE(Setting != nullptr);
			CHECK(Setting->Source == "READBACK");
		}
		for (const FEffectiveSetting& Setting : Settings)
		{
			const bool bCounter = Setting.Key.ends_with(".start_asleep_skipped") || (Setting.Key.ends_with("error_bits") && Setting.Key != "errors.capacity_bits");
			if (bCounter)
			{
				CAPTURE(Setting.Key);
				CHECK(Setting.Source == "SET_ONLY");
			}
		}
		// Module level: the host's MaxThreads, the adapter's scene / assert / initialisation counters, the Kit's MXCSR restores
		// (PhysX reads its live scenes from PxPhysics::getNbScenes: READBACK).
		const PtBackendApi& Api = Scene->GetBackendScene().GetApi();
		PtTextBuffer Query = {};
		Api.DumpEffectiveSettings(0, &Query);
		std::string Text(Query.Length > 0 ? Query.Length : 1u, '\0');
		PtTextBuffer Buffer = { Text.data(), static_cast<uint32_t>(Text.size()), 0 };
		REQUIRE(Api.DumpEffectiveSettings(0, &Buffer) == PT_OK);
		Text.resize(Buffer.Length > 0 ? Buffer.Length - 1 : 0);
		std::vector<FEffectiveSetting> Module;
		std::string Error;
		REQUIRE_MESSAGE(ParseEffectiveSettings(Text, Module, Error), Error);
		int32 NumChecked = 0;
		for (const FEffectiveSetting& Setting : Module)
		{
			const bool bInputOrCounter = Setting.Key == "kit.mxcsr_restores" || Setting.Key.ends_with(".max_threads") || Setting.Key.ends_with(".asserts")
				|| Setting.Key.ends_with(".initializations") || (Setting.Key.ends_with(".live_scenes") && Backend != "PhysX");
			if (bInputOrCounter)
			{
				CAPTURE(Setting.Key);
				CHECK(Setting.Source == "SET_ONLY");
				++NumChecked;
			}
			if (Setting.Key == "physx.live_scenes")
			{
				CHECK(Setting.Source == "READBACK");
				CHECK(Setting.Canonical >= 1.0);
			}
		}
		CHECK(NumChecked >= 3);   // max_threads, live_scenes or initializations, kit.mxcsr_restores
	}
	for (const std::string& Backend : SdkBackendNames())
	{
		PrepareGlobalSettingsModule(Backend, {});
	}
}

namespace
{
	// Signed distance of a world point to a body's single shape at a pose (the contact-point checks below).
	double ShapeSignedDistance(const FBodyInstance& Body, const FTransform& Pose, const FVector& Point)
	{
		const FKAggregateGeom& Geom = Body.BodySetup->AggGeom;
		if (!Geom.BoxElems.empty())
		{
			const FKBoxElem& Box = Geom.BoxElems[0];
			const FVector Local = (Box.GetTransform() * Pose).InverseTransformPositionNoScale(Point);
			const FVector Q(std::abs(Local.X) - 0.5 * Box.X, std::abs(Local.Y) - 0.5 * Box.Y, std::abs(Local.Z) - 0.5 * Box.Z);
			const FVector Outside(std::max(Q.X, 0.0), std::max(Q.Y, 0.0), std::max(Q.Z, 0.0));
			return Outside.Size() + std::min(std::max(Q.X, std::max(Q.Y, Q.Z)), 0.0);
		}
		if (!Geom.SphereElems.empty())
		{
			return (Point - Pose.TransformPositionNoScale(Geom.SphereElems[0].Center)).Size() - Geom.SphereElems[0].Radius;
		}
		const FKSphylElem& Capsule = Geom.SphylElems[0];
		const FVector Local = (Capsule.GetTransform() * Pose).InverseTransformPositionNoScale(Point);
		const double Z = std::clamp(Local.Z, -0.5 * Capsule.Length, 0.5 * Capsule.Length);
		return (Local - FVector(0.0, 0.0, Z)).Size() - Capsule.Radius;
	}
}

// M6 contract (c) (docs/Abi.md 7.1): PointOn1 lies on Body1's surface and PointOn0 on Body0's, whatever shape pair and report
// order the engine has. PhysX puts PxContactPairPoint::position on one of the two shapes (or between them) depending on the
// narrow-phase path and flips only the normal when the report order differs (PhysXSimEventCallback.h); the adapter projects
// the point onto the report's second shape. Static supports here are not the ground: a dynamic sphere on a static box
// (the sphere is created first, so the pair order differs from the narrow phase's type order), a lying capsule on a static
// box, and a dynamic box on a static sphere; each starts 1 cm deep, so step 1's points must be Phi = -1 cm apart along n.
// Also the ABI v1.0 flag rule (contract (b)): ACTIVE and SPECULATIVE never together.
TEST_CASE("PhysicsTest.SdkBackend.ContactPointsOnSurfaces")
{
	for (const std::string& Backend : SdkBackendNames())
	{
		CAPTURE(Backend);
		for (const char* Profile : { "Normalized", "UEParity" })
		{
			const std::string ProfileText(Profile);
			CAPTURE(ProfileText);
			FResolvedPhysicsConfig Config = LoadSdkConfig(Backend, Profile);
			PrepareGlobalSettingsModule(Backend, Config.InitOptions);
			Config.TestSettings.RunMode = EPhysicsRunMode::Viz;
			FPhysSceneInitParams InitParams;
			InitParams.DebugName = "ContactPoints";
			InitParams.BackendName = Backend;
			InitParams.Config = &Config;
			FPhysScene Scene(InitParams);
			if (!Scene.GetBackendInfo().Has(PT_CAP_CONTACT_POINTS))
			{
				MESSAGE(Backend << " declares no contact points: skipped");
				continue;
			}
			FBodyInstance Static;
			Static.bStatic = true;
			Static.bSimulatePhysics = false;
			FBodyInstance Dynamic;
			FProfileResolver::ApplyBodyInstanceDefaults(Config, Dynamic);
			Dynamic.bSimulatePhysics = true;
			Dynamic.bOverrideMass = true;
			Dynamic.MassInKgOverride = 10.0f;
			const auto MakeSetup = [&Scene](const int32 Kind, const double A, const double B) -> const FBodySetup*
			{
				FBodySetup Setup;
				if (Kind == 0)
				{
					Setup.AggGeom.SphereElems.emplace_back(A);
				}
				else if (Kind == 1)
				{
					Setup.AggGeom.BoxElems.emplace_back(A);
				}
				else
				{
					Setup.AggGeom.SphylElems.emplace_back(A, B);
				}
				return Scene.CreateBodySetup(std::move(Setup));
			};
			// 0: a far static ground plate (keeps body index 0 static, as every scene); then dynamic / static pairs.
			Scene.SpawnBody(Static, MakeSetup(1, 100.0, 0.0), FTransform(FVector(0.0, 5000.0, -50.0)));
			Scene.SpawnBody(Dynamic, MakeSetup(0, 25.0, 0.0), FTransform(FVector(-500.0, 0.0, 60.0 + 25.0 - 1.0)));               // 1 sphere
			Scene.SpawnBody(Static, MakeSetup(1, 60.0, 0.0), FTransform(FVector(-500.0, 0.0, 30.0)));                            // 2 box under it
			Scene.SpawnBody(Dynamic, MakeSetup(2, 25.0, 100.0), FTransform(FRotator(0.0, 0.0, 90.0), FVector(0.0, 0.0, 100.0 + 25.0 - 1.0)));  // 3 capsule
			Scene.SpawnBody(Static, MakeSetup(1, 300.0, 0.0), FTransform(FVector(0.0, 0.0, -50.0)));                             // 4 box under it (top z 100)
			Scene.SpawnBody(Dynamic, MakeSetup(1, 40.0, 0.0), FTransform(FVector(500.0, 0.0, 100.0 + 20.0 - 1.0)));               // 5 box
			Scene.SpawnBody(Static, MakeSetup(0, 50.0, 0.0), FTransform(FVector(500.0, 0.0, 50.0)));                             // 6 sphere under it
			Scene.FlushDeferredActors();
			Scene.PrepareForSimulation();
			std::vector<FTransform> Initial;
			for (const FBodyInstance* Body : Scene.GetBodies())
			{
				Initial.push_back(Body->State.BodyTM);
			}
			Scene.Tick(Config.TestSettings.FixedDeltaTime);
			FPhysicsDebugSnapshot Snapshot;
			REQUIRE(Scene.CaptureDebugSnapshot(Snapshot));
			const std::pair<uint32, uint32> Pairs[3] = { { 1u, 2u }, { 3u, 4u }, { 5u, 6u } };
			const char* const Names[3] = { "sphere on box", "capsule on box", "box on sphere" };
			for (int32 Slot = 0; Slot < 3; ++Slot)
			{
				const auto [Mobile, Support] = Pairs[Slot];
				int32 Points = 0;
				double WorstOn0 = 0.0;
				double WorstOn1 = 0.0;
				double WorstPhi = 0.0;
				for (const PtContactPoint& Contact : Snapshot.Contacts)
				{
					CHECK_FALSE(((Contact.Flags & PT_CONTACT_ACTIVE) != 0u && (Contact.Flags & PT_CONTACT_SPECULATIVE) != 0u));
					CHECK_FALSE(((Contact.Flags & PT_CONTACT_ACTIVE_APPROX) != 0u && (Contact.Flags & PT_CONTACT_SPECULATIVE) != 0u));
					if (!((Contact.Body0 == Mobile && Contact.Body1 == Support) || (Contact.Body0 == Support && Contact.Body1 == Mobile)))
					{
						continue;
					}
					++Points;
					CHECK(Contact.Body0 == Mobile);   // Body0 = the more mobile body
					const FVector P0(Contact.PointOn0.X, Contact.PointOn0.Y, Contact.PointOn0.Z);
					const FVector P1(Contact.PointOn1.X, Contact.PointOn1.Y, Contact.PointOn1.Z);
					const FVector N(Contact.Normal.X, Contact.Normal.Y, Contact.Normal.Z);
					WorstOn0 = std::max(WorstOn0, std::abs(ShapeSignedDistance(*Scene.GetBodies()[Mobile], Initial[Mobile], P0)));
					WorstOn1 = std::max(WorstOn1, std::abs(ShapeSignedDistance(*Scene.GetBodies()[Support], Initial[Support], P1)));
					WorstPhi = std::max(WorstPhi, std::abs(FVector::DotProduct(P0 - P1, N) - Contact.Phi));
					CHECK(Contact.Phi < 0.0);
					CHECK(N.Z > 0.99);
				}
				const std::string Pair(Names[Slot]);
				CAPTURE(Pair);
				MESSAGE(Backend << " " << ProfileText << " " << Pair << ": " << Points << " points, PointOn0 off its body " << WorstOn0 << " cm, PointOn1 off its body "
					<< WorstOn1 << " cm, |(P0 - P1).n - Phi| " << WorstPhi << " cm");
				CHECK(Points > 0);
				CHECK(WorstOn0 <= 0.05);
				CHECK(WorstOn1 <= 0.05);
				CHECK(WorstPhi <= 0.05);
			}
		}
	}
	for (const std::string& Backend : SdkBackendNames())
	{
		PrepareGlobalSettingsModule(Backend, {});
	}
}

// M6 contract (b) (docs/Abi.md 7.1): a command that wakes a body of a sleeping pair drops the pair's kept points until the
// next step, on every engine: Jolt and PhysX drop the woken body's kept pairs (FlushKeptContactCommands), and Bullet - which
// keeps a sleeping pair's manifold itself - leaves the woken body's pairs out of the snapshot until its next narrow phase.
// After that step the pair is reported afresh, not sleeping.
TEST_CASE("PhysicsTest.SdkBackend.WakeDropsKeptContacts")
{
	for (const std::string& Backend : SdkBackendNames())
	{
		CAPTURE(Backend);
		FResolvedPhysicsConfig Config = LoadSdkConfig(Backend, "UEParity");
		PrepareGlobalSettingsModule(Backend, Config.InitOptions);
		Config.TestSettings.RunMode = EPhysicsRunMode::Viz;
		std::unique_ptr<FPhysScene> Scene = MakeScenarioScene("M1.SingleBoxDrop", { { "Case", "a" } }, Backend, Config);
		if (!Scene->GetBackendInfo().Has(PT_CAP_CONTACT_POINTS))
		{
			continue;
		}
		FBodyInstance& Box = *Scene->GetBodies()[1];
		const float Dt = Config.TestSettings.FixedDeltaTime;
		for (int32 Frame = 0; Frame < 40; ++Frame)
		{
			Scene->Tick(Dt);
		}
		const auto Count = [&Scene](uint32& OutSleeping)
		{
			FPhysicsDebugSnapshot Snapshot;
			REQUIRE(Scene->CaptureDebugSnapshot(Snapshot));
			OutSleeping = 0;
			for (const PtContactPoint& Contact : Snapshot.Contacts)
			{
				OutSleeping += (Contact.Flags & PT_CONTACT_SLEEPING) != 0u ? 1u : 0u;
			}
			return static_cast<uint32>(Snapshot.Contacts.size());
		};
		uint32 Sleeping = 0;
		REQUIRE(Count(Sleeping) > 0u);
		Box.PutInstanceToSleep();
		Scene->Tick(Dt);
		Scene->Tick(Dt);
		const uint32 Kept = Count(Sleeping);
		CHECK(Kept > 0u);
		CHECK(Sleeping == Kept);
		Box.WakeInstance();
		REQUIRE(Scene->GetBackendScene().FlushCommands() == PT_OK);   // the wake reaches the backend, no step yet
		const uint32 AfterWake = Count(Sleeping);
		Scene->Tick(Dt);
		const uint32 Fresh = Count(Sleeping);
		MESSAGE(Backend << ": kept " << Kept << " sleeping points; after WAKE_UP before a step " << AfterWake << "; after the step " << Fresh << " (" << Sleeping
			<< " sleeping)");
		CHECK(AfterWake == 0u);
		CHECK(Fresh > 0u);
		CHECK(Sleeping == 0u);
	}
	for (const std::string& Backend : SdkBackendNames())
	{
		PrepareGlobalSettingsModule(Backend, {});
	}
}
