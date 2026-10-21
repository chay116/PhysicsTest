// BulletBackendTests.cpp - Bullet-specific unit tests (03_WorkPlan M5; built only with PT_WITH_BULLET). The shared SDK
// suites (SdkBackendTests, Abi.SdkBackendsA0toA7, ScenarioTests) take Bullet unchanged now that it is in the generated
// registry; this file holds what only Bullet has: its box margins (03 M5 criterion: 50 cm cube 0.025 m, 100 cm cube 0.04 m),
// the effective keys it declares, the PT_MODULE_GLOBAL_SCENE_SETTINGS combine rule, UE's damping order and angular limit
// through the internal pre-tick, kinematic velocities, gravity changes reaching sleeping bodies, pool overflows as capacity
// bits, real contact impulses, native islands, and the M4 kept-contact semantics of Jolt reproduced by Bullet's own
// persistent manifolds. PT_BULLET_IN_REGISTRY mirrors the CMake target property PT_IN_BACKEND_REGISTRY of PtBackendBullet
// (PhysicsTestAutomation/CMakeLists.txt). No Bullet header is visible here: everything goes through FPhysScene and the C ABI.
#include "CoreMinimal.h"

#include "BenchmarkRunner.h"
#include "BulletBackend/BulletBackendEntry.h"
#include "Containers/StringConv.h"
#include "IslandBuilder.h"
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
#include <memory>
#include <string>
#include <vector>

#ifndef PT_BULLET_IN_REGISTRY
	#error "PT_BULLET_IN_REGISTRY must be defined by PhysicsTestAutomation/CMakeLists.txt"
#endif

using namespace PhysicsTest;

namespace
{
	std::filesystem::path BulletTestConfigDir()
	{
		std::string Override;
		if (FParse::Value(FCommandLine::Get(), "ConfigDir=", Override))
		{
			return StringConv::Utf8ToPath(Override);
		}
		return StringConv::Utf8ToPath(PT_CONFIG_DIR);
	}

	FResolvedPhysicsConfig LoadBulletConfig(const std::string& Profile, const std::string& Backend = "Bullet")
	{
		FBenchRunSpec Spec;
		Spec.Backend = Backend;
		Spec.PhysicsProfile = Profile;
		Spec.ConfigDir = BulletTestConfigDir();
		FResolvedPhysicsConfig Config;
		FBenchConfig Files;
		std::string Error;
		const bool bLoaded = LoadBenchConfig(Spec, Config, Files, Error);
		INFO(Error);
		REQUIRE(bLoaded);
		return Config;
	}

	std::unique_ptr<FPhysScene> MakeBulletScenario(const char* ScenarioName, const FScenarioParams& Overrides, const FResolvedPhysicsConfig& Config,
		const std::string& Backend = "Bullet")
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

	std::vector<FEffectiveSetting> ReadEffective(const std::string& Text)
	{
		std::vector<FEffectiveSetting> Settings;
		std::string Error;
		const bool bParsed = ParseEffectiveSettings(Text, Settings, Error);
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

	// The module-level effective settings (DumpEffectiveSettings(0), two-call protocol).
	std::string DumpModuleSettings(const FPhysicsBackendModule& Module)
	{
		const PtBackendApi& Api = Module.GetApi();
		PtTextBuffer Query = {};
		Api.DumpEffectiveSettings(0, &Query);
		std::string Text(Query.Length > 0 ? Query.Length : 1u, '\0');
		PtTextBuffer Buffer = { Text.data(), static_cast<uint32_t>(Text.size()), 0 };
		REQUIRE(Api.DumpEffectiveSettings(0, &Buffer) == PT_OK);
		Text.resize(Buffer.Length > 0 ? Buffer.Length - 1 : 0);
		return Text;
	}

	void Tick(FPhysScene& Scene, const int32 Frames)
	{
		for (int32 Frame = 0; Frame < Frames; ++Frame)
		{
			Scene.Tick(Scene.GetTestSettings().FixedDeltaTime);
		}
	}

	FVector ToVector(const PtVec3& V)
	{
		return FVector(V.X, V.Y, V.Z);
	}

	// A scene with a static ground (top face z = 0) whose material is Ground, and nothing else yet.
	FBodyInstance* SpawnGround(FPhysScene& Scene, const FPhysicalMaterial* Ground)
	{
		FBodySetup Setup;
		Setup.AggGeom.BoxElems.emplace_back(4000.0, 4000.0, 100.0);
		Setup.PhysMaterial = Ground;
		const FBodySetup* GroundSetup = Scene.CreateBodySetup(std::move(Setup));
		FBodyInstance Static;
		Static.bStatic = true;
		return Scene.SpawnBody(Static, GroundSetup, FTransform(FVector(0.0, 0.0, -50.0)));
	}
}

// The static entry answers this harness's ABI major only and names the module; the generated registry lists the backend
// exactly when its CMake target says so (a stub that cannot create a scene stays out of the shared SDK suites).
TEST_CASE("PhysicsTest.BulletBackend.Registered")
{
	const PtBackendApi* Api = PT_GetBackendApi_Bullet(PT_ABI_VERSION_MAJOR);
	REQUIRE(Api != nullptr);
	CHECK(Api->StructSize == sizeof(PtBackendApi));
	CHECK(Api->AbiVersion == PT_ABI_VERSION);
	CHECK(std::string(Api->ModuleName) == "Bullet");
	CHECK(PT_GetBackendApi_Bullet(PT_ABI_VERSION_MAJOR + 1u) == nullptr);

	PtBackendInfo Info = {};
	Info.StructSize = sizeof(PtBackendInfo);
	REQUIRE(Api->GetInfo(&Info) == PT_OK);
	CHECK(std::string(Info.Name) == "Bullet");
	CHECK(std::string(Info.EngineVersion) == "Bullet 3.25");
	CHECK(std::string(Info.BuildFlagsJson).find("\"backend\":\"Bullet\"") != std::string::npos);
	CHECK(std::string(Info.BuildFlagsJson).find("\"BT_USE_DOUBLE_PRECISION\":0") != std::string::npos);
	// 02 7.5: Bullet's global combine callbacks make it GLOBAL_SCENE_SETTINGS; contact impulses, exact active flags and native
	// islands; single threaded; no native frame model.
	CHECK(Info.ModuleFlags == PT_MODULE_GLOBAL_SCENE_SETTINGS);
	for (const uint64 Cap : { PT_CAP_COLLISION, PT_CAP_KINEMATIC, PT_CAP_SLEEPING, PT_CAP_ACTIVE_LIST, PT_CAP_STATE_WRITE, PT_CAP_NATIVE_HASH, PT_CAP_CONTACT_POINTS,
			 PT_CAP_CONTACT_NORMAL_IMPULSE, PT_CAP_CONTACT_FRICTION_IMPULSE, PT_CAP_CONTACT_ACTIVE_EXACT, PT_CAP_NATIVE_ISLANDS, PT_CAP_BOUNDS })
	{
		CHECK((Info.Capabilities & Cap) == Cap);
	}
	CHECK((Info.Capabilities & PT_CAP_MULTITHREADED) == 0u);
	CHECK((Info.Capabilities & PT_CAP_NATIVE_FRAME_MODEL) == 0u);
	REQUIRE(Info.NumRequiredEffectiveKeys == 2u);
	CHECK(std::string(Info.RequiredEffectiveKeys[0]) == "bullet.solver_mode");
	CHECK(std::string(Info.RequiredEffectiveKeys[1]) == "bullet.angular_motion_threshold");

	const std::vector<std::string> Names = FPhysicsBackendRegistry::Get().GetAvailableNames();
	const bool bListed = std::find(Names.begin(), Names.end(), "Bullet") != Names.end();
	CHECK(bListed == (PT_BULLET_IN_REGISTRY != 0));
	if (bListed)
	{
		std::string Error;
		FPhysicsBackendModule* Module = FPhysicsBackendRegistry::Get().FindOrLoadModule("Bullet", Error);
		REQUIRE_MESSAGE(Module != nullptr, Error);
		CHECK_FALSE(Module->IsDll());   // the static path
		CHECK(&Module->GetApi() == Api);
		CHECK(Module->GetInfo().Name == "Bullet");
	}
}

// 03 M5: "Bullet shape.box_margin: 50 cm 큐브 0.025 m, 100 cm 큐브 0.04 m" - btBoxShape's setSafeMargin min(0.04 m, 0.1 x the
// smallest half extent), read back from the shapes (btBoxShape.cpp:17-25, btConvexInternalShape.h:66-81 [V]).
TEST_CASE("PhysicsTest.BulletBackend.BoxMarginReadback")
{
	for (const auto& [Edge, ExpectedCm] : { std::pair<double, double>{ 50.0, 2.5 }, { 100.0, 4.0 }, { 400.0, 4.0 }, { 20.0, 1.0 } })
	{
		CAPTURE(Edge);
		FPhysSceneInitParams InitParams;
		InitParams.DebugName = "BoxMargin";
		InitParams.BackendName = "Bullet";
		FPhysScene Scene(InitParams);
		FBodySetup Cube;
		Cube.AggGeom.BoxElems.emplace_back(Edge);
		const FBodySetup* CubeSetup = Scene.CreateBodySetup(std::move(Cube));
		FBodyInstance Template;
		Template.bSimulatePhysics = true;
		Scene.SpawnBody(Template, CubeSetup, FTransform(FVector(0.0, 0.0, 1000.0)));
		Scene.FlushDeferredActors();
		const std::vector<FEffectiveSetting> Settings = ReadEffective(Scene.DumpEffectiveSettings());
		const FEffectiveSetting& Margin = RequireSetting(Settings, "shape.box_margin");
		MESSAGE("Bullet " << Edge << " cm cube: shape.box_margin = " << Margin.Native);
		CHECK(Margin.Source == "READBACK");
		REQUIRE(Margin.bHasCanonical);
		CHECK(Margin.Canonical == doctest::Approx(ExpectedCm).epsilon(1e-6));
	}
}

// -Describe / A7: the generic keys and Bullet's own, READBACK wherever Bullet holds the value, and the profile values that
// reach Bullet (restitution threshold: UEParity 1000 dt cm/s, Normalized 50 cm/s; the angular limit; sleep).
TEST_CASE("PhysicsTest.BulletBackend.EffectiveSettings")
{
	for (const char* Profile : { "UEParity", "Normalized" })
	{
		const std::string ProfileText(Profile);
		CAPTURE(ProfileText);
		const FResolvedPhysicsConfig Config = LoadBulletConfig(Profile);
		std::unique_ptr<FPhysScene> Scene = MakeBulletScenario("M1.SingleBoxDrop", { { "Case", "a" } }, Config);
		const std::vector<FEffectiveSetting> Settings = ReadEffective(Scene->DumpEffectiveSettings());
		CHECK(FindMissingEffectiveKeys(Settings, Scene->GetBackendInfo()).empty());
		for (const char* Key : { "world.gravity", "solver.type", "solver.iterations", "contact.restitution_threshold", "contact.speculative_distance",
				 "material.friction_combine", "material.restitution_combine", "body.gyroscopic", "sleep.enabled", "sleep.criteria", "ccd.enabled",
				 "determinism.flag", "shape.box_margin", "shape.wrapping", "listeners.installed", "bullet.solver_mode", "bullet.erp2",
				 "bullet.split_impulse_penetration_threshold", "bullet.linear_slop", "bullet.warmstarting_factor", "bullet.deactivation_time",
				 "bullet.force_update_all_aabbs", "bullet.pools", "bullet.debug_drawer" })
		{
			const std::string KeyText(Key);
			CAPTURE(KeyText);
			CHECK(RequireSetting(Settings, Key).Source == "READBACK");
		}
		// The adapter's own counters are SET_ONLY (docs/Abi.md 5.14; M5 round 0).
		for (const char* Key : { "bullet.pool_heap_fallbacks", "bullet.angular_motion_limit_hits", "bullet.start_asleep_skipped", "bullet.engine_error_bits" })
		{
			const std::string KeyText(Key);
			CAPTURE(KeyText);
			CHECK(RequireSetting(Settings, Key).Source == "SET_ONLY");
		}
		// The breaking threshold Bullet applies (no manifold before the first step: the dynamic box's shape): gContactBreakingThreshold
		// 0.02 x the 100 cm cube's angular motion disc (its bounding sphere radius, sqrt(3) x 50 cm) = 1.732 cm, not 2 cm.
		CHECK(RequireSetting(Settings, "contact.speculative_distance").Canonical == doctest::Approx(0.02 * std::sqrt(3.0) * 50.0).epsilon(1e-5));
		CHECK(RequireSetting(Settings, "bullet.debug_drawer").Canonical == 0.0);
		CHECK(RequireSetting(Settings, "listeners.installed").Native.find("gContact*Callback") != std::string::npos);
		CHECK(RequireSetting(Settings, "world.gravity").Canonical == doctest::Approx(-980.0).epsilon(1e-6));
		CHECK(RequireSetting(Settings, "solver.iterations").Canonical == 10.0);
		CHECK(RequireSetting(Settings, "shape.box_margin").Canonical == doctest::Approx(4.0).epsilon(1e-6));
		CHECK(RequireSetting(Settings, "shape.wrapping").Canonical == 0.0);
		CHECK(RequireSetting(Settings, "listeners.installed").Canonical == 0.0);
		CHECK(RequireSetting(Settings, "material.static_friction_rule").Native == "dynamic_only");
		CHECK(RequireSetting(Settings, "material.friction_combine").Native.find("UE combine mode") == 0u);
		CHECK(RequireSetting(Settings, "errors.capacity_bits").Canonical == 3.0);
		CHECK(RequireSetting(Settings, "bullet.angular_motion_threshold").Canonical == doctest::Approx(3.14159265358979 / 4.0 * 60.0).epsilon(1e-6));
		CHECK(RequireSetting(Settings, "body.damping_formula").Native.find("UE:") == 0u);
		const bool bParity = ProfileText == "UEParity";
		const double Threshold = bParity ? 1000.0 * static_cast<double>(Config.TestSettings.FixedDeltaTime) : 50.0;
		CHECK(RequireSetting(Settings, "contact.restitution_threshold").Canonical == doctest::Approx(Threshold).epsilon(1e-6));
		CHECK(RequireSetting(Settings, "sleep.enabled").Canonical == (bParity ? 1.0 : 0.0));
		CHECK(RequireSetting(Settings, "body.max_angular_velocity").Canonical == doctest::Approx(bParity ? 62.831853 : 1e7 * 3.14159265358979 / 180.0).epsilon(1e-5));
		CHECK(RequireSetting(Settings, "body.damping").Canonical == doctest::Approx(bParity ? 0.01 : 0.0));
		CHECK(RequireSetting(Settings, "bullet.start_asleep_skipped").Canonical == 0.0);
		MESSAGE("Bullet " << ProfileText << ": " << Settings.size() << " effective keys; sleep.criteria = " << RequireSetting(Settings, "sleep.criteria").Native);
	}
	// Module level: the process-wide settings this module installed.
	const FResolvedPhysicsConfig Config = LoadBulletConfig("Normalized");
	std::unique_ptr<FPhysScene> Scene = MakeBulletScenario("S00.FreeFall", {}, Config);
	const std::vector<FEffectiveSetting> Module = ReadEffective(DumpModuleSettings(Scene->GetBackendScene().GetModule()));
	CHECK(RequireSetting(Module, "bullet.combine_rule").Canonical == 0.0);
	CHECK(RequireSetting(Module, "bullet.deactivation_time_global").Canonical == doctest::Approx(2.0));
	CHECK(RequireSetting(Module, "bullet.contact_breaking_threshold").Canonical == doctest::Approx(0.02).epsilon(1e-6));   // a factor, not cm
	CHECK(RequireSetting(Module, "bullet.max_threads").Source == "SET_ONLY");
	CHECK(RequireSetting(Module, "bullet.live_scenes").Source == "SET_ONLY");
	CHECK(RequireSetting(Module, "kit.mxcsr_restores").Source == "SET_ONLY");
	CHECK(RequireSetting(Module, "bullet.version").Native.rfind("3.25", 0) == 0u);
}

// DL42 / DL47 on Bullet: one element with its frames at the actor frame is the native primitive; a nudged COM is a one-child
// compound (offset_com); a capsule is btCapsuleShapeZ (no axis remap: the D1 amendment); two elements are a compound. The
// engine's COM is the harness COM (the body frame is the principal frame).
TEST_CASE("PhysicsTest.BulletBackend.ShapeWrapping")
{
	FPhysSceneInitParams InitParams;
	InitParams.DebugName = "BulletWrapperCount";
	InitParams.BackendName = "Bullet";
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
	Scene.SpawnBody(Template, BoxSetup, FTransform(FVector(0.0, 0.0, 1000.0)));          // native: no wrapper
	FBodyInstance Nudged = Template;
	Nudged.COMNudge = FVector(10.0, 0.0, 0.0);
	Scene.SpawnBody(Nudged, BoxSetup, FTransform(FVector(500.0, 0.0, 1000.0)));          // offset_com
	Scene.SpawnBody(Template, CapsuleSetup, FTransform(FVector(1000.0, 0.0, 1000.0)));   // btCapsuleShapeZ: native
	Scene.SpawnBody(Template, PairSetup, FTransform(FVector(1500.0, 0.0, 1000.0)));      // compound
	FBodyInstance KinematicNudged = Nudged;
	KinematicNudged.bSimulatePhysics = false;
	Scene.SpawnBody(KinematicNudged, BoxSetup, FTransform(FVector(2000.0, 0.0, 1000.0))); // kinematic: the actor frame, native
	Scene.FlushDeferredActors();
	const std::vector<FEffectiveSetting> Settings = ReadEffective(Scene.DumpEffectiveSettings());
	const FEffectiveSetting& Wrapping = RequireSetting(Settings, "shape.wrapping");
	MESSAGE("Bullet wrapper control: " << Wrapping.Native);
	CHECK(Wrapping.Source == "READBACK");
	CHECK(Wrapping.Canonical == 2.0);
	CHECK(Wrapping.Native.find("compound 1") != std::string::npos);
	CHECK(Wrapping.Native.find("offset_com 1") != std::string::npos);
	CHECK(Wrapping.Native.find("axis_remap 0") != std::string::npos);
	CHECK(Wrapping.Native.find("local_transform 0") != std::string::npos);
	// The engine's COM is the harness COM: the nudged box (10 cm along X) and the two-element body (the sphere lifts it).
	for (const uint32 Index : { 1u, 3u })
	{
		CAPTURE(Index);
		PtMassProperties Mass = {};
		REQUIRE(Scene.GetBackendScene().ReadMassProperties(Index, std::span<PtMassProperties>(&Mass, 1)) == PT_OK);
		const FBodyInstance& Body = *Scene.GetBodies()[Index];
		// M6 (lead (d)): read from the engine - the compound's child 0 transform (float) - so float resolution, not 1e-9.
		CHECK(std::abs(Mass.CenterOfMassLocal.Translation.X - Body.MassProperties.CenterOfMass.X) < 1e-4);
		CHECK(std::abs(Mass.CenterOfMassLocal.Translation.Z - Body.MassProperties.CenterOfMass.Z) < 1e-4);
		CHECK(Mass.Mass == doctest::Approx(Body.MassProperties.Mass).epsilon(1e-6));
	}
	// The actor poses read back where they were created (the COM frame is undone exactly).
	for (const FBodyInstance* Body : Scene.GetBodies())
	{
		CHECK(Body->State.BodyTM.GetTranslation().Z == doctest::Approx(1000.0).epsilon(1e-6));
	}
	CHECK(Scene.GetBodies()[1]->State.BodyTM.GetTranslation().X == doctest::Approx(500.0).epsilon(1e-6));
	// The capsule stands along Z: its world bounds are 2 x (50 + 25) tall and 2 x 25 wide (the same contact-threshold padding
	// on every side once a step updated the broad phase, btCollisionWorld.cpp:153-160 [V]).
	FPhysicsDebugSnapshot Snapshot;
	REQUIRE(Scene.CaptureDebugSnapshot(Snapshot, PT_SNAP_PARTICLES));
	const PtParticleSnapshot& CapsuleParticle = Snapshot.Particles[2];
	const double Height = CapsuleParticle.BoundsMax.Z - CapsuleParticle.BoundsMin.Z;
	const double Width = CapsuleParticle.BoundsMax.X - CapsuleParticle.BoundsMin.X;
	CHECK(Height - Width == doctest::Approx(100.0).epsilon(1e-5));
	CHECK(Width >= 50.0 - 1e-4);
	CHECK(Width <= 54.0 + 1e-4);
}

// The resting contact of M1.SingleBoxDrop a with Bullet's real impulses (CONTACT_NORMAL_IMPULSE, kg*cm/s): four points, the
// normal on the ground toward the box, a few hundredths of a cm deep, PT_CONTACT_ACTIVE exact, and the normal impulses of
// one step adding up to the box's weight over the step (1000 kg x 980 cm/s^2 x dt).
TEST_CASE("PhysicsTest.BulletBackend.RestingContactImpulses")
{
	FResolvedPhysicsConfig Config = LoadBulletConfig("Normalized");
	Config.TestSettings.RunMode = EPhysicsRunMode::Viz;
	std::unique_ptr<FPhysScene> Scene = MakeBulletScenario("M1.SingleBoxDrop", { { "Case", "a" } }, Config);
	Tick(*Scene, 300);
	FPhysicsDebugSnapshot Snapshot;
	REQUIRE(Scene->CaptureDebugSnapshot(Snapshot));
	REQUIRE(Snapshot.Contacts.size() == 4u);
	double NormalSum = 0.0;
	double FrictionMax = 0.0;
	for (const PtContactPoint& Contact : Snapshot.Contacts)
	{
		CHECK(Contact.Body0 == 1u);
		CHECK(Contact.Body1 == 0u);
		CHECK(Contact.Normal.Z > 0.999);
		CHECK(Contact.Phi <= 0.5);
		CHECK(Contact.Phi >= -2.5);
		CHECK((Contact.Flags & PT_CONTACT_ACTIVE) != 0u);
		CHECK((Contact.Flags & PT_CONTACT_SLEEPING) == 0u);   // Normalized: nothing sleeps
		const FVector Delta = ToVector(Contact.PointOn0) - ToVector(Contact.PointOn1);
		CHECK((Delta | ToVector(Contact.Normal)) == doctest::Approx(Contact.Phi).epsilon(1e-4));
		REQUIRE_FALSE(std::isnan(Contact.NormalImpulse));
		CHECK(Contact.NormalImpulse > 0.0);
		NormalSum += Contact.NormalImpulse;
		FrictionMax = std::max(FrictionMax, ToVector(Contact.FrictionImpulse).Size());
	}
	const double Weight = 1000.0 * 980.0 * static_cast<double>(Config.TestSettings.FixedDeltaTime);
	MESSAGE("Bullet resting box: normal impulses sum " << NormalSum << " kg*cm/s (weight x dt " << Weight << "), largest friction impulse " << FrictionMax);
	CHECK(NormalSum == doctest::Approx(Weight).epsilon(0.02));
	CHECK(FrictionMax < 0.01 * Weight);
	CHECK(Scene->GetBodies()[1]->State.BodyTM.GetTranslation().Z == doctest::Approx(50.0).epsilon(0.002));
}

// M1.SingleBoxDrop b lands on a corner and tips: every point satisfies (PointOn0 - PointOn1) . Normal = Phi (Chaos' per-point
// separation) and the points of one manifold differ in depth.
TEST_CASE("PhysicsTest.BulletBackend.PerPointPhi")
{
	FResolvedPhysicsConfig Config = LoadBulletConfig("Normalized");
	Config.TestSettings.RunMode = EPhysicsRunMode::Viz;
	std::unique_ptr<FPhysScene> Scene = MakeBulletScenario("M1.SingleBoxDrop", { { "Case", "b" } }, Config);
	double LargestSpread = 0.0;
	for (int32 Frame = 0; Frame < 150; ++Frame)
	{
		Tick(*Scene, 1);
		FPhysicsDebugSnapshot Snapshot;
		REQUIRE(Scene->CaptureDebugSnapshot(Snapshot));
		double MinPhi = 1e30;
		double MaxPhi = -1e30;
		for (const PtContactPoint& Contact : Snapshot.Contacts)
		{
			const FVector Delta = ToVector(Contact.PointOn0) - ToVector(Contact.PointOn1);
			CHECK((Delta | ToVector(Contact.Normal)) == doctest::Approx(Contact.Phi).epsilon(1e-4));
			// ABI v1.0 rule (docs/Abi.md 7.1, M6): ACTIVE and SPECULATIVE are exclusive; SPECULATIVE = Phi > 0 without an impulse.
			const bool bActive = (Contact.Flags & PT_CONTACT_ACTIVE) != 0u;
			CHECK(((Contact.Flags & PT_CONTACT_SPECULATIVE) != 0u) == (Contact.Phi > 0.0 && !bActive));
			CHECK_FALSE((bActive && (Contact.Flags & PT_CONTACT_SPECULATIVE) != 0u));
			MinPhi = std::min(MinPhi, Contact.Phi);
			MaxPhi = std::max(MaxPhi, Contact.Phi);
		}
		if (Snapshot.Contacts.size() >= 2u)
		{
			LargestSpread = std::max(LargestSpread, MaxPhi - MinPhi);
		}
	}
	MESSAGE("Bullet: largest Phi spread within one snapshot " << LargestSpread << " cm");
	CHECK(LargestSpread > 0.05);
}

// The M4 kept-contact rule (Jolt: UpdateSleepingContacts) holds natively in Bullet: a sleeping pair is never narrow-phased,
// so its manifold keeps the points and impulses of the step it fell asleep in; they are reported with PT_CONTACT_SLEEPING
// and stay active. The island builder then makes one sleeping island (Black); a wake drops the flag at the next step.
TEST_CASE("PhysicsTest.BulletBackend.SleepingIslandKeepsContacts")
{
	FResolvedPhysicsConfig Config = LoadBulletConfig("UEParity");
	Config.TestSettings.RunMode = EPhysicsRunMode::Viz;
	std::unique_ptr<FPhysScene> Scene = MakeBulletScenario("M1.SingleBoxDrop", {}, Config);
	int32 Frames = 0;
	while (Frames < 600 && Scene->GetBodies()[1]->State.ObjectState != EObjectStateType::Sleeping)
	{
		Tick(*Scene, 1);
		++Frames;
	}
	REQUIRE(Scene->GetBodies()[1]->State.ObjectState == EObjectStateType::Sleeping);
	MESSAGE("Bullet: the box fell asleep after " << Frames << " frames (Bullet's own deactivation: 2 s below 80 cm/s and 1 rad/s)");
	FPhysicsDebugSnapshot Snapshot;
	REQUIRE(Scene->CaptureDebugSnapshot(Snapshot));
	REQUIRE(Snapshot.Contacts.size() == 4u);
	for (const PtContactPoint& Contact : Snapshot.Contacts)
	{
		CHECK((Contact.Flags & PT_CONTACT_SLEEPING) != 0u);
		CHECK((Contact.Flags & PT_CONTACT_ACTIVE) != 0u);   // the last solve's impulse is kept
		CHECK(Contact.Normal.Z > 0.999);
		CHECK(Contact.Phi <= 0.5);
	}
	REQUIRE(Snapshot.Particles.size() == 2u);
	CHECK(Snapshot.Particles[0].NativeIslandId == -1);   // the static ground
	CHECK(Snapshot.Particles[1].NativeIslandId >= 0);    // Bullet's island tag
	FIslandBuilder::Build(Snapshot, Scene->GetConstraints());
	std::vector<FHarnessIsland> Islands;
	FIslandBuilder::GatherIslands(Snapshot, Islands);
	REQUIRE(Islands.size() == 1u);
	CHECK_FALSE(Islands[0].bAwake);
	Scene->GetBodies()[1]->WakeInstance();
	Tick(*Scene, 1);
	FPhysicsDebugSnapshot Awake;
	REQUIRE(Scene->CaptureDebugSnapshot(Awake));
	CHECK_FALSE(Awake.Contacts.empty());
	for (const PtContactPoint& Contact : Awake.Contacts)
	{
		CHECK((Contact.Flags & PT_CONTACT_SLEEPING) == 0u);
	}
	// Bench runs read no contacts (the snapshot reports them where Jolt installs its listener).
	const FResolvedPhysicsConfig Bench = LoadBulletConfig("UEParity");
	std::unique_ptr<FPhysScene> BenchScene = MakeBulletScenario("M1.SingleBoxDrop", {}, Bench);
	Tick(*BenchScene, 60);
	FPhysicsDebugSnapshot BenchSnapshot;
	REQUIRE(BenchScene->CaptureDebugSnapshot(BenchSnapshot));
	CHECK(BenchSnapshot.Contacts.empty());
}

// The kept contacts follow the body commands as in Jolt (M4 fix round 2): PUT_TO_SLEEP between steps keeps the last step's
// points; a state command on the sleeping body (SET_BODY_STATE, which wakes it from ABI v1.0 on) drops them
// (cleanProxyFromPairs: stale points) and the narrow phase reports the pair again; a second PUT_TO_SLEEP keeps it once more.
TEST_CASE("PhysicsTest.BulletBackend.KeptContactsFollowCommands")
{
	FResolvedPhysicsConfig Config = LoadBulletConfig("UEParity");
	Config.TestSettings.RunMode = EPhysicsRunMode::Viz;
	std::unique_ptr<FPhysScene> Scene = MakeBulletScenario("M1.SingleBoxDrop", {}, Config);
	FBodyInstance& Box = *Scene->GetBodies()[1];
	const auto CountContacts = [&Scene](uint32& OutSleeping)
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
	uint32 Touching = 0;
	uint32 Sleeping = 0;
	for (int32 Frame = 0; Frame < 200 && Touching == 0; ++Frame)
	{
		Tick(*Scene, 1);
		Touching = CountContacts(Sleeping);
	}
	REQUIRE(Touching > 0u);
	REQUIRE(Box.State.ObjectState != EObjectStateType::Sleeping);
	CHECK(Sleeping == 0u);
	Box.PutInstanceToSleep();
	for (int32 Frame = 0; Frame < 3; ++Frame)
	{
		Tick(*Scene, 1);
		CHECK(Box.State.ObjectState == EObjectStateType::Sleeping);
		CHECK(CountContacts(Sleeping) == Touching);
		CHECK(Sleeping == Touching);
	}
	// ABI v1.0 (docs/Abi.md 5.10, M6): SET_BODY_STATE wakes the sleeping box and drops its manifolds (cleanProxyFromPairs:
	// stale points); the next narrow phase reports the pair afresh.
	FPhysicsInterface::SetBodyState_AssumesLocked(Box.ActorHandle, Box.State.BodyTM, FVector::ZeroVector, FVector::ZeroVector);
	Tick(*Scene, 1);
	CHECK(Box.State.ObjectState == EObjectStateType::Dynamic);
	const uint32 Rewoken = CountContacts(Sleeping);
	CHECK(Rewoken > 0u);
	CHECK(Sleeping == 0u);
	Box.PutInstanceToSleep();
	Tick(*Scene, 1);
	CHECK(CountContacts(Sleeping) == Rewoken);
	CHECK(Sleeping == Rewoken);
	MESSAGE("Bullet: put to sleep keeps " << Touching << " contact points; SET_BODY_STATE drops them; re-slept keeps " << Rewoken);
}

// Shape0 / Shape1 are the harness element indices: a sphere (element 0, on top) and a box (element 1, at the bottom) resting on
// the ground on the box report Shape0 = 1 (the compound child index), Shape1 = 0.
TEST_CASE("PhysicsTest.BulletBackend.ContactElementIndex")
{
	FResolvedPhysicsConfig Config = LoadBulletConfig("Normalized");
	Config.TestSettings.RunMode = EPhysicsRunMode::Viz;
	FPhysSceneInitParams Init;
	Init.DebugName = "BulletElementIndex";
	Init.BackendName = "Bullet";
	Init.Config = &Config;
	FPhysScene Scene(Init);
	SpawnGround(Scene, Scene.GetDefaultPhysicalMaterial());
	FBodySetup TwoElements;
	FKSphereElem Sphere(25.0);
	Sphere.Center = FVector(0.0, 0.0, 150.0);
	TwoElements.AggGeom.SphereElems.push_back(Sphere);
	TwoElements.AggGeom.BoxElems.emplace_back(60.0);
	const FBodySetup* TwoPtr = Scene.CreateBodySetup(std::move(TwoElements));
	FBodyInstance Dynamic;
	Dynamic.bSimulatePhysics = true;
	FBodyInstance* Body = Scene.SpawnBody(Dynamic, TwoPtr, FTransform(FVector(0.0, 0.0, 60.0)));
	Scene.FlushDeferredActors();
	Scene.PrepareForSimulation();
	Tick(Scene, 90);
	FPhysicsDebugSnapshot Snapshot;
	REQUIRE(Scene.CaptureDebugSnapshot(Snapshot));
	REQUIRE_FALSE(Snapshot.Contacts.empty());
	for (const PtContactPoint& Contact : Snapshot.Contacts)
	{
		CHECK(Contact.Body0 == Body->ActorHandle.Index);
		CHECK(Contact.Shape0 == 1u);
		CHECK(Contact.Shape1 == 0u);
	}
	MESSAGE("Bullet: " << Snapshot.Contacts.size() << " contact points on element 1 (the box) of the two-element body");
}

// UE's damping order through the pre-tick (DampingModel=UE): free-flight bodies with strong damping match the Null reference
// (v = (v + g dt)(1 - c dt) per step). DampingModel=Converted (02 8.1's coefficient, Bullet's own order) differs by the
// predicted g dt (1 - m^N) - the reason the pre-tick exists.
TEST_CASE("PhysicsTest.BulletBackend.DampingOrder")
{
	struct FResult
	{
		FVector Linear;
		FVector Angular;
	};
	const auto Run = [](const std::string& Backend, const char* DampingModel) -> FResult
	{
		FResolvedPhysicsConfig Config = LoadBulletConfig("UEParity", Backend);
		if (DampingModel != nullptr)
		{
			Config.SceneOptions.emplace_back("DampingModel", DampingModel);
		}
		FPhysSceneInitParams Init;
		Init.DebugName = "BulletDamping";
		Init.BackendName = Backend;
		Init.Config = &Config;
		FPhysScene Scene(Init);
		FBodySetup Box;
		Box.AggGeom.BoxElems.emplace_back(40.0, 30.0, 20.0);
		const FBodySetup* BoxSetup = Scene.CreateBodySetup(std::move(Box));
		FBodyInstance Template;
		FProfileResolver::ApplyBodyInstanceDefaults(Config, Template);
		Template.bSimulatePhysics = true;
		Template.LinearDamping = 2.0f;
		Template.AngularDamping = 1.0f;
		Template.InitialLinearVelocity = FVector(300.0, 0.0, 100.0);
		Template.InitialAngularVelocityInRadians = FVector(0.0, 0.0, 4.0);
		FBodyInstance* Body = Scene.SpawnBody(Template, BoxSetup, FTransform(FVector(0.0, 0.0, 5000.0)));
		Scene.FlushDeferredActors();
		Tick(Scene, 30);
		return FResult{ Body->State.LinearVelocity, Body->State.AngularVelocityInRadians };
	};
	const FResult Null = Run("Null", nullptr);
	const FResult Ue = Run("Bullet", nullptr);
	const FResult Converted = Run("Bullet", "Converted");
	MESSAGE("Null v (" << Null.Linear.X << ", " << Null.Linear.Z << ") w.z " << Null.Angular.Z << "; Bullet UE order v (" << Ue.Linear.X << ", " << Ue.Linear.Z
		<< ") w.z " << Ue.Angular.Z << "; Bullet converted v (" << Converted.Linear.X << ", " << Converted.Linear.Z << ")");
	CHECK((Ue.Linear - Null.Linear).Size() < 0.05);
	CHECK((Ue.Angular - Null.Angular).Size() < 1e-3);
	// Converted: same multiplier per step, Bullet's order. Without forces (X, the spin) it matches; along gravity it differs by
	// g dt (1 - m^N), m = 1 - c dt.
	const double Dt = 1.0 / 60.0;
	const double M = 1.0 - 2.0 * Dt;
	CHECK(Converted.Linear.X == doctest::Approx(Null.Linear.X).epsilon(1e-4));
	CHECK(Converted.Angular.Z == doctest::Approx(Null.Angular.Z).epsilon(1e-4));
	CHECK(Converted.Linear.Z - Null.Linear.Z == doctest::Approx(-980.0 * Dt * (1.0 - std::pow(M, 30))).epsilon(0.01));
}

// UEParity's 62.83 rad/s (DL16) through the pre-tick, and Bullet's own limit: above pi/4 per step (47.1 rad/s at 60 Hz)
// btTransformUtil::integrateTransform turns a body by 2 atan((|w| dt / (pi/4)) tan(pi/8)) instead of |w| dt, without
// clamping the velocity (btTransformUtil.h:44-72 [V]); the post-tick counts those body-steps.
TEST_CASE("PhysicsTest.BulletBackend.AngularVelocityLimits")
{
	for (const char* Profile : { "UEParity", "Normalized" })
	{
		const std::string ProfileText(Profile);
		CAPTURE(ProfileText);
		const FResolvedPhysicsConfig Config = LoadBulletConfig(Profile);
		FPhysSceneInitParams Init;
		Init.DebugName = "BulletAngularLimit";
		Init.BackendName = "Bullet";
		Init.Config = &Config;
		FPhysScene Scene(Init);
		FBodySetup Box;
		Box.AggGeom.BoxElems.emplace_back(50.0);
		const FBodySetup* BoxSetup = Scene.CreateBodySetup(std::move(Box));
		FBodyInstance Template;
		FProfileResolver::ApplyBodyInstanceDefaults(Config, Template);
		Template.bSimulatePhysics = true;
		Template.bEnableGravity = false;
		Template.AngularDamping = 0.0f;
		FBodyInstance* Body = Scene.SpawnBody(Template, BoxSetup, FTransform(FVector(0.0, 0.0, 1000.0)));
		Scene.FlushDeferredActors();
		Body->SetAngularVelocityInRadians(FVector(0.0, 0.0, 100.0), false);
		Tick(Scene, 1);
		const double Spin = Body->State.AngularVelocityInRadians.Size();
		const bool bParity = ProfileText == "UEParity";
		MESSAGE("Bullet " << ProfileText << ": 100 rad/s -> " << Spin << " rad/s after one step");
		CHECK(Spin == doctest::Approx(bParity ? 3600.0 * 3.14159265358979 / 180.0 : 100.0).epsilon(1e-5));
		// Both speeds are above pi/4 per step: Bullet under-rotated the step in both profiles (UEParity 57.9 deg instead of
		// 60, Normalized 82.7 deg instead of 95.5).
		const std::vector<FEffectiveSetting> Settings = ReadEffective(Scene.DumpEffectiveSettings());
		CHECK(RequireSetting(Settings, "bullet.angular_motion_limit_hits").Canonical == 1.0);
		const double Pi = 3.14159265358979;
		const double Dt = static_cast<double>(Config.TestSettings.FixedDeltaTime);
		const double Expected = 2.0 * std::atan(Spin * Dt / (0.25 * Pi) * std::tan(Pi / 8.0)) * 180.0 / Pi;
		const double Yaw = Body->State.BodyTM.GetRotation().Rotator().Yaw;
		MESSAGE("Bullet " << ProfileText << ": the step turned the box " << Yaw << " deg (|w| dt = " << Spin * Dt * 180.0 / Pi << " deg)");
		CHECK(std::abs(Yaw) == doctest::Approx(Expected).epsilon(1e-4));
		CHECK(std::abs(Yaw) < Spin * Dt * 180.0 / Pi);
	}
}

// Kinematic targets: the body moves to the target in its step with the velocity that reaches it (double, exact), and pushes
// what rests on it. A box on a kinematic platform rising 1 cm per frame rises with it.
TEST_CASE("PhysicsTest.BulletBackend.KinematicPlatformLifts")
{
	const FResolvedPhysicsConfig Config = LoadBulletConfig("Normalized");
	FPhysSceneInitParams Init;
	Init.DebugName = "BulletKinematicLift";
	Init.BackendName = "Bullet";
	Init.Config = &Config;
	FPhysScene Scene(Init);
	FBodySetup Plate;
	Plate.AggGeom.BoxElems.emplace_back(400.0, 400.0, 20.0);
	const FBodySetup* PlateSetup = Scene.CreateBodySetup(std::move(Plate));
	FBodySetup Box;
	Box.AggGeom.BoxElems.emplace_back(50.0);
	const FBodySetup* BoxSetup = Scene.CreateBodySetup(std::move(Box));
	FBodyInstance Kinematic;
	FProfileResolver::ApplyBodyInstanceDefaults(Config, Kinematic);
	Kinematic.bSimulatePhysics = false;
	FBodyInstance* Platform = Scene.SpawnBody(Kinematic, PlateSetup, FTransform(FVector(0.0, 0.0, 0.0)));
	FBodyInstance Dynamic;
	FProfileResolver::ApplyBodyInstanceDefaults(Config, Dynamic);
	Dynamic.bSimulatePhysics = true;
	FBodyInstance* Rider = Scene.SpawnBody(Dynamic, BoxSetup, FTransform(FVector(0.0, 0.0, 35.0)));   // resting on the plate top (z 10)
	Scene.FlushDeferredActors();
	Scene.PrepareForSimulation();
	Tick(Scene, 30);   // settle
	const double RiderStart = Rider->State.BodyTM.GetTranslation().Z;
	const double Dt = static_cast<double>(Config.TestSettings.FixedDeltaTime);
	for (int32 Frame = 1; Frame <= 60; ++Frame)
	{
		Scene.SetKinematicTarget_AssumesLocked(Platform, FTransform(FVector(0.0, 0.0, static_cast<double>(Frame))), false);
		Tick(Scene, 1);
	}
	MESSAGE("Bullet: the plate rose 60 cm, the box " << Rider->State.BodyTM.GetTranslation().Z - RiderStart << " cm; plate v " << Platform->State.LinearVelocity.Z
		<< " cm/s, box v " << Rider->State.LinearVelocity.Z << " cm/s");
	CHECK(Platform->State.LinearVelocity.Z == doctest::Approx(1.0 / Dt).epsilon(1e-6));
	CHECK(Platform->State.BodyTM.GetTranslation().Z == doctest::Approx(60.0).epsilon(1e-5));
	CHECK(Rider->State.BodyTM.GetTranslation().Z - RiderStart == doctest::Approx(60.0).epsilon(0.02));
	CHECK(Rider->State.LinearVelocity.Z == doctest::Approx(1.0 / Dt).epsilon(0.05));
	// No target: the plate stops (Chaos' Reset) and the box settles on it.
	Tick(Scene, 30);
	CHECK(Platform->State.LinearVelocity.Size() < 1e-9);
	CHECK(std::abs(Rider->State.LinearVelocity.Z) < 1.0);
}

// btDiscreteDynamicsWorld::setGravity updates the awake bodies only (btDiscreteDynamicsWorld.cpp:499-510 [V]): the backend gives
// the sleeping ones the new gravity too, so a body woken after a gravity change follows it.
TEST_CASE("PhysicsTest.BulletBackend.GravityChangeReachesSleepingBodies")
{
	const FResolvedPhysicsConfig Config = LoadBulletConfig("UEParity");
	FPhysSceneInitParams Init;
	Init.DebugName = "BulletGravityChange";
	Init.BackendName = "Bullet";
	Init.Config = &Config;
	FPhysScene Scene(Init);
	FBodySetup Box;
	Box.AggGeom.BoxElems.emplace_back(50.0);
	const FBodySetup* BoxSetup = Scene.CreateBodySetup(std::move(Box));
	FBodyInstance Template;
	FProfileResolver::ApplyBodyInstanceDefaults(Config, Template);
	Template.bSimulatePhysics = true;
	Template.LinearDamping = 0.0f;
	FBodyInstance* Body = Scene.SpawnBody(Template, BoxSetup, FTransform(FVector(0.0, 0.0, 5000.0)));
	Scene.FlushDeferredActors();
	Body->PutInstanceToSleep();
	Tick(Scene, 2);
	REQUIRE(Body->State.ObjectState == EObjectStateType::Sleeping);
	// A frame with a new gravity (up) while the body sleeps, then wake it and fall "up" for 10 frames. FPhysScene::Tick sends
	// the project gravity every frame (as UWorld), so these frames pass the new one explicitly.
	const FVector Up(0.0, 0.0, 980.0);
	const float Dt = Config.TestSettings.FixedDeltaTime;
	const auto FrameWithGravity = [&Scene, &Up, Dt]()
	{
		Scene.SetUpForFrame(&Up, Dt, 0.0f, 0.1f, Dt, 1, false);
		Scene.StartFrame();
		Scene.WaitPhysScenes();
		Scene.EndFrame();
	};
	FrameWithGravity();
	CHECK(Scene.GetGravity().Z == 980.0);
	CHECK(Body->State.ObjectState == EObjectStateType::Sleeping);
	Body->WakeInstance();
	for (int32 Frame = 0; Frame < 10; ++Frame)
	{
		FrameWithGravity();
	}
	MESSAGE("Bullet: woken after the gravity change, v.z = " << Body->State.LinearVelocity.Z << " cm/s");
	CHECK(Body->State.LinearVelocity.Z == doctest::Approx(980.0 * 10.0 * static_cast<double>(Dt)).epsilon(1e-3));
}

// Pools sized below the pile's needs overflow to the heap: capacity error bits 0x1 (manifolds) and 0x2 (algorithms), which
// errors.capacity_bits declares (bench exit code 5, G12). The scenario's own hints never overflow (SdkBackend.BoxPileCapacity).
TEST_CASE("PhysicsTest.BulletBackend.PoolOverflowIsACapacityError")
{
	FResolvedPhysicsConfig Config = LoadBulletConfig("Normalized");
	Config.SceneOptions.emplace_back("ManifoldPoolSize", "64");
	Config.SceneOptions.emplace_back("AlgorithmPoolSize", "64");
	std::unique_ptr<FPhysScene> Scene = MakeBulletScenario("M3.BoxPile", { { "N", "1000" } }, Config);
	uint32 Bits = 0;
	for (int32 Frame = 0; Frame < 60; ++Frame)
	{
		Tick(*Scene, 1);
		Bits |= Scene->GetLastFrameTiming().EngineErrorBits;
	}
	const std::vector<FEffectiveSetting> Settings = ReadEffective(Scene->DumpEffectiveSettings());
	MESSAGE("Bullet BoxPile 1k with 64-element pools: error bits " << Bits << ", " << RequireSetting(Settings, "bullet.pool_heap_fallbacks").Native);
	CHECK(Bits == 3u);
	CHECK(RequireSetting(Settings, "errors.capacity_bits").Canonical == 3.0);
	CHECK(RequireSetting(Settings, "bullet.pool_heap_fallbacks").Canonical > 0.0);
	CHECK(RequireSetting(Settings, "bullet.engine_error_bits").Canonical == 3.0);
	PtStepStats Stats = {};
	Stats.StructSize = sizeof(PtStepStats);
	REQUIRE(Scene->GetBackendScene().GetStepStats(Stats) == PT_OK);
	CHECK(Stats.EngineErrorBits == 3u);
}

// Native islands and the step statistics that need no listener (a bench run): islands, contact pairs, sleeping counts.
TEST_CASE("PhysicsTest.BulletBackend.NativeIslandsAndStats")
{
	const FResolvedPhysicsConfig Config = LoadBulletConfig("UEParity");
	std::unique_ptr<FPhysScene> Scene = MakeBulletScenario("M3.BoxPile", { { "N", "1000" } }, Config);
	Tick(*Scene, 90);
	PtStepStats Stats = {};
	Stats.StructSize = sizeof(PtStepStats);
	REQUIRE(Scene->GetBackendScene().GetStepStats(Stats) == PT_OK);
	MESSAGE("Bullet BoxPile 1k, 90 frames: " << Stats.NumIslands << " islands, " << Stats.NumContactPairs << " pairs, " << Stats.NumContactPoints << " points, "
		<< Stats.NumActiveBodies << " awake / " << Stats.NumSleepingBodies << " asleep");
	CHECK(Stats.NumIslands > 0);
	CHECK(Stats.NumContactPairs > 0);
	CHECK(Stats.NumContactPoints >= Stats.NumContactPairs);
	CHECK(Stats.NumActiveBodies + Stats.NumSleepingBodies == 1000);
	FPhysicsDebugSnapshot Snapshot;
	REQUIRE(Scene->CaptureDebugSnapshot(Snapshot, PT_SNAP_PARTICLES));
	REQUIRE(Snapshot.Particles.size() == 1001u);
	CHECK(Snapshot.Particles[0].NativeIslandId == -1);
	int32 MaxTag = -1;
	for (size_t Index = 1; Index < Snapshot.Particles.size(); ++Index)
	{
		CHECK(Snapshot.Particles[Index].NativeIslandId >= 0);
		MaxTag = std::max(MaxTag, Snapshot.Particles[Index].NativeIslandId);
	}
	CHECK(MaxTag <= 1001);
	// The native hash is deterministic for the same state.
	CHECK(Scene->ComputeNativeStateHash() == Scene->ComputeNativeStateHash());
}

// PT_MODULE_GLOBAL_SCENE_SETTINGS: the combine rule is a module option. The registry refuses a second set of init options in a
// process (the viewer's "restart required"); a process initialised with CombineRule=Bullet combines with Bullet's own product
// instead of the UE mode. A box sliding on the ground decelerates by mu g: UE Average of 0.2 and 0.6 = 0.4, Max 0.6, Bullet's
// product 0.12.
TEST_CASE("PhysicsTest.BulletBackend.GlobalCombineRule")
{
	const auto MeasureFriction = [](const FResolvedPhysicsConfig& Config, const EFrictionCombineMode BoxMode) -> double
	{
		FPhysSceneInitParams Init;
		Init.DebugName = "BulletFriction";
		Init.BackendName = "Bullet";
		Init.Config = &Config;
		FPhysScene Scene(Init);
		FPhysicalMaterial GroundMaterial = *Scene.GetDefaultPhysicalMaterial();
		GroundMaterial.Friction = 0.6f;
		GroundMaterial.Restitution = 0.0f;
		FPhysicalMaterial BoxMaterial = GroundMaterial;
		BoxMaterial.Friction = 0.2f;
		BoxMaterial.FrictionCombineMode = BoxMode;
		BoxMaterial.bOverrideFrictionCombineMode = true;
		SpawnGround(Scene, Scene.CreatePhysicalMaterial(GroundMaterial));
		FBodySetup Box;
		Box.AggGeom.BoxElems.emplace_back(50.0);
		Box.PhysMaterial = Scene.CreatePhysicalMaterial(BoxMaterial);
		const FBodySetup* BoxSetup = Scene.CreateBodySetup(std::move(Box));
		FBodyInstance Template;
		FProfileResolver::ApplyBodyInstanceDefaults(Config, Template);
		Template.bSimulatePhysics = true;
		Template.InitialLinearVelocity = FVector(500.0, 0.0, 0.0);
		FBodyInstance* Body = Scene.SpawnBody(Template, BoxSetup, FTransform(FVector(0.0, 0.0, 25.0)));
		Scene.FlushDeferredActors();
		Scene.PrepareForSimulation();
		Tick(Scene, 5);
		const double V5 = Body->State.LinearVelocity.X;
		Tick(Scene, 20);
		const double V25 = Body->State.LinearVelocity.X;
		return (V5 - V25) / (980.0 * 20.0 * static_cast<double>(Config.TestSettings.FixedDeltaTime));
	};
	const FResolvedPhysicsConfig Config = LoadBulletConfig("Normalized");
	REQUIRE(Config.InitOptions.empty());   // Bullet.Default sets no module option: it shares the process with config-less scenes
	const double Average = MeasureFriction(Config, EFrictionCombineMode::Average);
	const double Max = MeasureFriction(Config, EFrictionCombineMode::Max);
	MESSAGE("Bullet, UE rule: mu Average " << Average << ", Max " << Max);
	CHECK(Average == doctest::Approx(0.4).epsilon(0.03));
	CHECK(Max == doctest::Approx(0.6).epsilon(0.03));

	// Another rule in the same process: refused while the module is initialised with the default one.
	std::string Error;
	FBackendInitParams Other;
	Other.InitOptions = { { "CombineRule", "Bullet" } };
	CHECK(FPhysicsBackendRegistry::Get().FindOrLoad("Bullet", Other, Error) == nullptr);
	CHECK(Error.find("GLOBAL_SCENE_SETTINGS") != std::string::npos);
	MESSAGE("registry: " << Error);
	// A module option the backend does not know is refused by InitGamePhys itself.
	FPhysicsBackendModule* Module = FPhysicsBackendRegistry::Get().FindOrLoadModule("Bullet", Error);
	REQUIRE(Module != nullptr);
	REQUIRE(Module->GetNumLiveScenes() == 0);
	REQUIRE(Module->TermGamePhys() == PT_OK);
	FBackendInitParams Unknown;
	Unknown.InitOptions = { { "NoSuchModuleOption", "1" } };
	CHECK(Module->InitGamePhys(Unknown) == PT_E_INVALID_ARG);
	CHECK(Module->GetLastErrorMessage().find("unknown module option") != std::string::npos);
	// M5 round 0: DeactivationTime 0 would turn sleep off silently (btRigidBody::wantsSleeping), and the contact breaking
	// threshold is Bullet's factor, > 0.
	for (const auto& [Key, Value] : { std::pair<const char*, const char*>{ "DeactivationTime", "0" }, { "DeactivationTime", "-1" },
			 { "ContactBreakingThreshold", "0" }, { "CombineRule", "Max" } })
	{
		FBackendInitParams Bad;
		Bad.InitOptions = { { Key, Value } };
		const std::string Text = std::string(Key) + "=" + Value;
		CAPTURE(Text);
		CHECK(Module->InitGamePhys(Bad) == PT_E_INVALID_ARG);
		CHECK_FALSE(Module->IsInitialized());
		MESSAGE(Module->GetLastErrorMessage());
	}
	{
		FBackendInitParams Factor;
		Factor.InitOptions = { { "ContactBreakingThreshold", "0.03" }, { "DeactivationTime", "0.5" } };
		REQUIRE(Module->InitGamePhys(Factor) == PT_OK);
		const std::vector<FEffectiveSetting> ModuleSettings = ReadEffective(DumpModuleSettings(*Module));
		CHECK(RequireSetting(ModuleSettings, "bullet.contact_breaking_threshold").Canonical == doctest::Approx(0.03).epsilon(1e-6));
		CHECK(RequireSetting(ModuleSettings, "bullet.deactivation_time_global").Canonical == doctest::Approx(0.5).epsilon(1e-6));
		REQUIRE(Module->TermGamePhys() == PT_OK);
	}
	// A process of its own (here: the module re-initialised) with Bullet's rule: F0 * F1, whatever the UE modes say.
	FResolvedPhysicsConfig BulletRule = Config;
	BulletRule.InitOptions = Other.InitOptions;
	const double Product = MeasureFriction(BulletRule, EFrictionCombineMode::Average);
	MESSAGE("Bullet, Bullet rule: mu " << Product);
	CHECK(Product == doctest::Approx(0.12).epsilon(0.03));
	{
		const std::vector<FEffectiveSetting> ModuleSettings = ReadEffective(DumpModuleSettings(*Module));
		CHECK(RequireSetting(ModuleSettings, "bullet.combine_rule").Canonical == 1.0);
	}
	// Leave the module uninitialised: the next user initialises it with its own (default) options.
	REQUIRE(Module->GetNumLiveScenes() == 0);
	CHECK(Module->TermGamePhys() == PT_OK);
}

// 03 M3 criterion for Bullet: two variants that differ only in [PhysicsTest.Backend.Bullet] keys build the same desc_sha256
// (engine keys go to options_sha256), and the variant reaches the engine (solver.iterations reads 20 back).
TEST_CASE("PhysicsTest.BulletBackend.DescShaEqualAcrossEngineKeyVariants")
{
	FConfigFile Base;
	FPhysicsProfile Profile;
	std::string Error;
	REQUIRE(FConfigFile::LoadFile(StringConv::PathToUtf8(BulletTestConfigDir() / "BasePhysicsTest.ini"), Base, Error));
	REQUIRE(FPhysicsProfile::LoadIni(StringConv::PathToUtf8(BulletTestConfigDir() / "Profiles" / "UEParity.ini"), Profile, Error));
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
	const FResolvedPhysicsConfig VariantA = MakeConfig("[PhysicsTest.Variant]\nEngine=Bullet\nName=TestA\n[PhysicsTest.Backend.Bullet]\nNumIterations=10\n");
	const FResolvedPhysicsConfig VariantB = MakeConfig("[PhysicsTest.Variant]\nEngine=Bullet\nName=TestB\n[PhysicsTest.Backend.Bullet]\nNumIterations=20\n"
		"SolverMode=0x114\nSplitImpulse=false\nDampingModel=Converted\n");
	for (const auto& [Name, Params] : { std::pair<const char*, FScenarioParams>{ "M1.SingleBoxDrop", { { "Case", "b" } } }, { "M3.BoxPile", { { "N", "1000" } } } })
	{
		const std::string SceneText(Name);
		CAPTURE(SceneText);
		std::unique_ptr<FPhysScene> A = MakeBulletScenario(Name, Params, VariantA);
		std::unique_ptr<FPhysScene> B = MakeBulletScenario(Name, Params, VariantB);
		const std::vector<float> Dts(60, VariantA.TestSettings.FixedDeltaTime);
		CHECK(A->ComputeDescSha256(Dts) == B->ComputeDescSha256(Dts));
		CHECK(A->ComputeOptionsSha256() != B->ComputeOptionsSha256());
		const std::vector<FEffectiveSetting> Settings = ReadEffective(B->DumpEffectiveSettings());
		CHECK(RequireSetting(Settings, "solver.iterations").Canonical == 20.0);
		CHECK(RequireSetting(Settings, "bullet.solver_mode").Canonical == static_cast<double>(0x114));
		CHECK(RequireSetting(Settings, "bullet.damping_model").Native == "Converted");
		CHECK(RequireSetting(Settings, "body.damping").Source == "READBACK");   // Bullet's own coefficient, read back as UE's
		CHECK(RequireSetting(Settings, "body.damping").Canonical == doctest::Approx(0.01).epsilon(1e-4));
	}
	// An unknown engine key is the variant's error, rejected by CreateScene (the bench's preflight, exit code 2).
	std::string CreateError;
	FPhysicsBackendModule* Module = FPhysicsBackendRegistry::Get().FindOrLoad("Bullet", FBackendInitParams{}, CreateError);
	REQUIRE_MESSAGE(Module != nullptr, CreateError);
	const PtKeyValue Option{ "NoSuchKey", "1" };
	PtSceneDesc Desc = {};
	Desc.StructSize = sizeof(PtSceneDesc);
	Desc.Gravity = PtVec3{ 0.0, 0.0, -980.0 };
	Desc.NominalDeltaTime = 1.0 / 60.0;
	Desc.AsyncFixedTimeStepSize = PT_ASYNC_DT_SYNC;
	Desc.RestitutionThreshold = -1.0;
	Desc.NumThreads = 1;
	Desc.DebugName = "UnknownBulletKey";
	Desc.Options = &Option;
	Desc.NumOptions = 1;
	CHECK(FPhysicsBackendScene::Create(*Module, Desc, CreateError) == nullptr);
	CHECK(CreateError.find("unknown scene option 'NoSuchKey'") != std::string::npos);
}