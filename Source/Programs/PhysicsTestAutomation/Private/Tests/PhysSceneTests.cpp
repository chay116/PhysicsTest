// PhysSceneTests.cpp - FPhysScene (03_WorkPlan M1 task 5, M2; 02_Architecture 6, 9.1, A.9): pointer-stable storage,
// deferred actors in creation order, the start-asleep step, delegates, the frame state machine, and from M2 the scene
// driving the Null backend through the C ABI (static and DLL, Native and Harness driver): the free-fall fixture, body
// commands, timing buckets, the harness console-variable guard, frames that run no step, the force / sleep order and the
// kinematic-target entry points.
#include "CoreMinimal.h"

#include "Fixtures/FreeFallFixture.h"
#include "HAL/IConsoleManager.h"
#include "Physics/AnalyticReference.h"
#include "Physics/PhysicsBackend.h"
#include "Physics/PhysicsConsoleVariables.h"
#include "Physics/PhysicsInterface.h"
#include "Physics/PhysicsProfile.h"
#include "PhysScene.h"

#include "Misc/PtThirdPartyIncludesStart.h"
#include <doctest/doctest.h>
#include "Misc/PtThirdPartyIncludesEnd.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace PhysicsTest;

namespace
{
	FBodySetup MakeCubeSetup(const double Size)
	{
		FBodySetup Setup;
		Setup.BodySetupName = "Cube" + std::to_string(static_cast<int32>(Size));
		Setup.AggGeom.BoxElems.emplace_back(Size);
		return Setup;
	}

	FPhysScene MakeScene(const FResolvedPhysicsConfig* Config = nullptr, const std::string& Backend = std::string())
	{
		FPhysSceneInitParams Params;
		Params.DebugName = "UnitTest";
		Params.BackendName = Backend;
		Params.Config = Config;
		return FPhysScene(Params);
	}
}

TEST_CASE("PhysicsTest.PhysScene.StablePointersAndDeferredBatch")
{
	FPhysSceneInitParams Params;
	Params.DebugName = "Storage";
	FPhysScene Scene(Params);
	const FBodySetup* Cube = Scene.CreateBodySetup(MakeCubeSetup(10.0));
	FPhysicalMaterial Ice;
	Ice.Name = "Ice";
	Ice.Friction = 0.05f;
	const FPhysicalMaterial* IcePtr = Scene.CreatePhysicalMaterial(Ice);

	FBodyInstance Template;
	Template.bSimulatePhysics = true;
	Template.PhysMaterialOverride = IcePtr;
	FBodyInstance* First = Scene.SpawnBody(Template, Cube, FTransform(FVector(0.0, 0.0, 100.0)));
	const FBodySetup* FirstSetup = Cube;
	std::vector<FBodyInstance*> Spawned{ First };
	for (int32 Index = 1; Index < 2000; ++Index)
	{
		Spawned.push_back(Scene.SpawnBody(Template, Scene.CreateBodySetup(MakeCubeSetup(10.0)), FTransform(FVector(0.0, 0.0, 100.0 + Index))));
	}
	// std::deque storage: earlier addresses stay valid while the scene grows.
	CHECK(Scene.GetBodies()[0] == First);
	CHECK(First->BodySetup == FirstSetup);
	CHECK(First->GetUnrealWorldTransform().GetTranslation() == FVector(0.0, 0.0, 100.0));
	CHECK(First->PhysMaterialOverride == IcePtr);
	CHECK(IcePtr->Friction == 0.05f);
	CHECK(Scene.GetNumDeferredActors() == 2000);
	CHECK_FALSE(First->ActorHandle.IsValid());
	CHECK(First->State.ObjectState == EObjectStateType::Uninitialized);

	Scene.FlushDeferredActors();
	CHECK(Scene.GetNumDeferredActors() == 0);
	for (uint32 Index = 0; Index < Spawned.size(); ++Index)
	{
		// Index = creation order = insertion order (02 5.1).
		REQUIRE(Spawned[Index]->ActorHandle.IsValid());
		CHECK(Spawned[Index]->ActorHandle.Index == Index);
		CHECK(Spawned[Index]->InstanceBodyIndex == INDEX_NONE);   // UE meaning (instanced mesh / physics asset): unused
		CHECK(Spawned[Index]->State.ObjectState == EObjectStateType::Dynamic);
	}
	CHECK(Scene.GetStats().NumBodies == 2000);
	CHECK(Scene.GetStats().NumDynamicBodies == 2000);

	// A second batch continues the indices.
	FBodyInstance* Late = Scene.SpawnBody(Template, Cube, FTransform::Identity);
	Scene.FlushDeferredActors();
	CHECK(Late->ActorHandle.Index == 2000u);
	CHECK(Late->ActorHandle.Serial != First->ActorHandle.Serial);
}

TEST_CASE("PhysicsTest.PhysScene.InitialStatesAndStartAsleep")
{
	// 02 6 step 5: a dynamic body with bStartAwake = false is put to sleep after AddActorsToScene, unless the profile
	// disables sleeping (Normalized), in which case the step is skipped and counted. Kinematic bodies never sleep.
	for (const bool bAllowSleeping : { true, false })
	{
		FResolvedPhysicsConfig Config;
		Config.TestSettings.bAllowSleeping = bAllowSleeping;
		FPhysScene Scene = MakeScene(&Config);
		const FBodySetup* Cube = Scene.CreateBodySetup(MakeCubeSetup(50.0));
		FBodyInstance Static;
		Static.bStatic = true;
		FBodyInstance Kinematic;
		Kinematic.bStartAwake = false;
		FBodyInstance Dynamic;
		Dynamic.bSimulatePhysics = true;
		FBodyInstance Asleep;
		Asleep.bSimulatePhysics = true;
		Asleep.bStartAwake = false;
		FBodyInstance* Bodies[] = {
			Scene.SpawnBody(Static, Cube, FTransform::Identity), Scene.SpawnBody(Kinematic, Cube, FTransform::Identity),
			Scene.SpawnBody(Dynamic, Cube, FTransform::Identity), Scene.SpawnBody(Asleep, Cube, FTransform::Identity) };
		Scene.FlushDeferredActors();
		CAPTURE(bAllowSleeping);
		CHECK(Bodies[0]->State.ObjectState == EObjectStateType::Static);
		CHECK(Bodies[1]->State.ObjectState == EObjectStateType::Kinematic);
		CHECK(Bodies[2]->State.ObjectState == EObjectStateType::Dynamic);
		CHECK(Bodies[3]->State.ObjectState == (bAllowSleeping ? EObjectStateType::Sleeping : EObjectStateType::Dynamic));
		CHECK(Scene.GetStats().NumStartAsleepSkipped == (bAllowSleeping ? 0 : 1));
		CHECK(Scene.GetStats().NumStaticBodies == 1);
		CHECK(Scene.GetStats().NumKinematicBodies == 1);
		CHECK(Scene.GetStats().NumDynamicBodies == 2);
		// Awake = dynamic and not asleep; the kinematic and the static body are never counted.
		Scene.Tick(1.0f / 60.0f);
		CHECK(Scene.GetStats().NumAwakeBodies == (bAllowSleeping ? 1 : 2));
	}
}

TEST_CASE("PhysicsTest.PhysScene.MassUsesSceneDefaults")
{
	// A body without any material falls back to the scene default material (UE: GEngine->DefaultPhysMaterial):
	// 1 g/cm^3 -> a 100 cm cube weighs 1000^0.75 kg. The scene's mass rule applies (Physical: 1000 kg).
	FPhysScene Scene = MakeScene();
	const FBodySetup* Cube = Scene.CreateBodySetup(MakeCubeSetup(100.0));
	FBodyInstance Template;
	Template.bSimulatePhysics = true;
	const FBodyInstance* Body = Scene.SpawnBody(Template, Cube, FTransform::Identity);
	CHECK(Body->GetSimplePhysicalMaterial() == Scene.GetDefaultPhysicalMaterial());
	CHECK(Body->GetBodyMass() == doctest::Approx(std::pow(1000.0, 0.75)).epsilon(1e-12));

	FResolvedPhysicsConfig Config;
	Config.TestSettings.MassRule = EMassRule::Physical;
	Config.MaterialDefaults.Density = 2.0f;
	FPhysScene Physical = MakeScene(&Config);
	const FBodyInstance* Heavy = Physical.SpawnBody(Template, Physical.CreateBodySetup(MakeCubeSetup(100.0)), FTransform::Identity);
	CHECK(Heavy->GetBodyMass() == doctest::Approx(2000.0).epsilon(1e-12));
	// Body scale from the spawn transform.
	const FBodyInstance* Wide = Physical.SpawnBody(Template, Physical.CreateBodySetup(MakeCubeSetup(100.0)),
		FTransform(FQuat::Identity, FVector::ZeroVector, FVector(2.0, 1.0, 1.0)));
	CHECK(Wide->Scale3D == FVector(2.0, 1.0, 1.0));
	CHECK(Wide->GetBodyMass() == doctest::Approx(4000.0).epsilon(1e-12));
	// The body pose is rigid (UE GetUnrealWorldTransform = actor pose); the scale lives in Scale3D only.
	const FQuat Yaw = FRotator(0.0, 30.0, 0.0).Quaternion();
	const FBodyInstance* Posed = Physical.SpawnBody(Template, Physical.CreateBodySetup(MakeCubeSetup(100.0)),
		FTransform(Yaw, FVector(1.0, 2.0, 3.0), FVector(3.0, 3.0, 3.0)));
	CHECK(Posed->Scale3D == FVector(3.0));
	CHECK(Posed->GetUnrealWorldTransform().GetScale3D() == FVector(1.0));
	CHECK(Posed->GetUnrealWorldTransform().GetTranslation() == FVector(1.0, 2.0, 3.0));
	CHECK(Posed->GetUnrealWorldTransform().GetRotation().Equals(Yaw, 0.0));
}

TEST_CASE("PhysicsTest.PhysScene.FrameStateMachineAndDelegates")
{
	FPhysScene Scene = MakeScene();
	FBodyInstance Template;
	Template.bSimulatePhysics = true;
	Scene.SpawnBody(Template, Scene.CreateBodySetup(MakeCubeSetup(10.0)), FTransform::Identity);
	Scene.FlushDeferredActors();
	Scene.PrepareForSimulation();

	std::vector<std::string> Events;
	// UE5 order: SetUpForFrame first, then StartFrame broadcasts PreTick with the clamped frame dt (MDeltaTime,
	// ChaosScene.cpp:363-374, PhysScene_Chaos.cpp:2272-2316) - phase SetUp, not simulating yet.
	Scene.OnPhysScenePreTick = [&Events](FPhysScene* InScene, const float Dt)
	{
		Events.push_back("Pre " + std::to_string(Dt) + (InScene->IsSimulating() ? " sim" : " idle"));
		CHECK(InScene->GetFramePhase() == EPhysSceneFramePhase::SetUp);
		CHECK(InScene->GetLastPlan().FrameDeltaTime == Dt);
	};
	Scene.OnPhysSceneStep = [&Events](FPhysScene* InScene, const float Dt)
	{
		Events.push_back("Step " + std::to_string(Dt) + (InScene->IsSimulating() ? " sim" : " idle"));
		CHECK(InScene->GetFramePhase() == EPhysSceneFramePhase::Started);
	};
	Scene.OnPhysScenePostTick = [&Events](FPhysScene* InScene, const float Dt)
	{
		Events.push_back("Post " + std::to_string(Dt) + (InScene->IsSimulating() ? " sim" : " idle"));
		CHECK(InScene->GetFramePhase() == EPhysSceneFramePhase::Idle);
	};

	// FixedStep (default): one step of FixedDeltaTime whatever the frame dt; PreTick / PostTick see FixedDeltaTime too.
	Scene.Tick(0.1f);
	const std::vector<std::string> Expected = { "Pre 0.016667 idle", "Step 0.016667 sim", "Post 0.016667 idle" };
	CHECK(Events == Expected);
	CHECK(Scene.GetFrameCount() == 1u);
	CHECK(Scene.GetStepCount() == 1u);
	CHECK_FALSE(Scene.IsSimulating());
	CHECK(Scene.GetLastPlan().NumSteps == 1);
	CHECK(Scene.GetLastFrameTiming().NumSteps == 1u);

	// The verbs one by one, as a backend driver would call them.
	const FVector Gravity(0.0, 0.0, -490.0);
	Scene.SetUpForFrame(&Gravity, 1.0f / 60.0f, 0.0f, 0.0f, 0.0f, 1, false);
	CHECK(Scene.GetFramePhase() == EPhysSceneFramePhase::SetUp);
	CHECK(Scene.GetGravity() == Gravity);
	Scene.StartFrame();
	CHECK(Scene.IsSimulating());
	CHECK(Scene.GetFramePhase() == EPhysSceneFramePhase::Started);
	Scene.WaitPhysScenes();
	CHECK(Scene.GetFramePhase() == EPhysSceneFramePhase::Waited);
	Scene.EndFrame();
	CHECK(Scene.GetFramePhase() == EPhysSceneFramePhase::Idle);
	CHECK_FALSE(Scene.IsSimulating());
	CHECK(Scene.GetFrameCount() == 2u);

	// Sync: PreTick gets min(dt, MaxPhysicsDeltaTime), not the raw Tick dt.
	FResolvedPhysicsConfig SyncConfig;
	SyncConfig.TestSettings.TickMode = EPhysicsTickMode::Sync;
	FPhysScene Sync = MakeScene(&SyncConfig);
	std::vector<float> PreTickDts;
	Sync.OnPhysScenePreTick = [&PreTickDts](FPhysScene*, const float Dt) { PreTickDts.push_back(Dt); };
	Sync.Tick(0.05f);
	Sync.Tick(0.01f);
	REQUIRE(PreTickDts.size() == 2u);
	CHECK(PreTickDts[0] == SyncConfig.Settings.MaxPhysicsDeltaTime);
	CHECK(PreTickDts[1] == 0.01f);
}

TEST_CASE("PhysicsTest.PhysScene.TickModesThroughThePlanner")
{
	// SimTime accumulates dt_engine per step: 60 FixedStep frames = 60 * (double)(float)(1/60).
	{
		FPhysScene Scene = MakeScene();
		for (int32 Frame = 0; Frame < 60; ++Frame)
		{
			Scene.Tick(1.0f / 60.0f);
		}
		CHECK(Scene.GetStepCount() == 60u);
		CHECK(Scene.GetSimTime() == doctest::Approx(60.0 * Analytic::EngineDeltaTime(1.0f / 60.0f)).epsilon(1e-15));
	}
	// Substepping (UE5): dt 0.05 -> 3 engine steps of 1/60. Both drivers call OnPhysSceneStep once per frame with the
	// frame dt, as UE5 FPhysScene_Chaos::OnStartFrame does (PhysScene_Chaos.cpp:2311-2312), also on a frame that runs no
	// step (dt 0).
	for (const EFrameDriver Driver : { EFrameDriver::Harness, EFrameDriver::Native })
	{
		FResolvedPhysicsConfig Config;
		Config.TestSettings.TickMode = EPhysicsTickMode::Substepping;
		Config.TestSettings.FrameDriver = Driver;
		FPhysScene Scene = MakeScene(&Config);
		const std::string DriverText = ToString(Driver);   // a const char* CAPTURE logs the pointer as 1
		CAPTURE(DriverText);
		CHECK(Scene.GetFrameDriver() == Driver);
		std::vector<float> Steps;
		Scene.OnPhysSceneStep = [&Steps](FPhysScene*, const float Dt) { Steps.push_back(Dt); };
		Scene.Tick(0.05f);
		CHECK(Scene.GetStepCount() == 3u);
		CHECK(Scene.GetLastPlan().Params.bSubstepping);
		REQUIRE(Steps.size() == 1u);
		CHECK(Steps[0] == Scene.GetLastPlan().FrameDeltaTime);
		Scene.Tick(0.0f);
		CHECK(Scene.GetStepCount() == 3u);
		REQUIRE(Steps.size() == 2u);
		CHECK(Steps[1] == 0.0f);
	}
	// The UE4 substep rule is harness-only: Auto picks the Harness driver even on a native backend (dt 0.05 -> 2 x 1/60),
	// and OnPhysSceneStep runs before every substep with the substep dt (UE4 FPhysSubstepTask).
	{
		FResolvedPhysicsConfig Config;
		Config.TestSettings.TickMode = EPhysicsTickMode::Substepping;
		Config.TestSettings.SubstepClampRule = ESubstepClampRule::UE4PhysSubstep;
		FPhysScene Scene = MakeScene(&Config);
		CHECK(Scene.GetFrameDriver() == EFrameDriver::Harness);
		std::vector<float> Steps;
		Scene.OnPhysSceneStep = [&Steps](FPhysScene*, const float Dt) { Steps.push_back(Dt); };
		Scene.Tick(0.05f);
		CHECK(Scene.GetStepCount() == 2u);
		REQUIRE(Steps.size() == 2u);
		CHECK(Steps[0] == Scene.GetLastPlan().StepDeltaTime);
	}
	// AsyncFixed 1/30 at 1/60 frames: 0, 1, 0, 1.
	{
		FResolvedPhysicsConfig Config;
		Config.TestSettings.TickMode = EPhysicsTickMode::AsyncFixed;
		FPhysScene Scene = MakeScene(&Config);
		std::vector<uint64> StepCounts;
		for (int32 Frame = 0; Frame < 4; ++Frame)
		{
			Scene.Tick(1.0f / 60.0f);
			StepCounts.push_back(Scene.GetStepCount());
		}
		CHECK(StepCounts == std::vector<uint64>{ 0u, 1u, 1u, 2u });
		CHECK(Scene.GetLastPlan().bAsyncInterpolated);
	}
}

TEST_CASE("PhysicsTest.PhysScene.ConstraintsAreDataOnly")
{
	FPhysScene Scene = MakeScene();
	FBodyInstance Template;
	Template.bSimulatePhysics = true;
	const FBodySetup* Cube = Scene.CreateBodySetup(MakeCubeSetup(10.0));
	FBodyInstance* Child = Scene.SpawnBody(Template, Cube, FTransform::Identity);
	FBodyInstance* Parent = Scene.SpawnBody(Template, Cube, FTransform(FVector(0.0, 0.0, 20.0)));
	FConstraintInstance Joint;
	Joint.JointName = "Hinge";
	FConstraintInstance* Spawned = Scene.SpawnConstraint(Joint, Child, Parent);
	CHECK(Spawned->Body1 == Child);
	CHECK(Spawned->Body2 == Parent);
	CHECK(Scene.GetConstraints().size() == 1u);
	CHECK(Scene.GetStats().NumConstraints == 1);
	CHECK_FALSE(Spawned->ConstraintHandle.IsValid());   // M9
}

TEST_CASE("PhysicsTest.PhysScene.FreeFallFixture")
{
	// 03 M2: sphere r 50, 1 kg, z0 1000 cm, damping 0, 60 frames, built through the FPhysScene API: z60 = 501.8332813705 cm
	// within 1e-9 against dt_engine, through the static and the DLL Null backend, with either frame driver.
	for (const char* Backend : { "Null", "NullDll" })
	{
		for (const EFrameDriver Driver : { EFrameDriver::Native, EFrameDriver::Harness })
		{
			FResolvedPhysicsConfig Config;
			Config.TestSettings.FrameDriver = Driver;
			FPhysScene Scene = MakeScene(&Config, Backend);
			const std::string BackendText(Backend);
			CAPTURE(BackendText);
			const std::string DriverText = ToString(Driver);   // a const char* CAPTURE logs the pointer as 1
			CAPTURE(DriverText);
			FBodyInstance* Sphere = Fixtures::FFreeFall::Build(Scene);
			Scene.PrepareForSimulation();
			CHECK(Sphere->GetBodyMass() == doctest::Approx(1.0).epsilon(1e-12));
			CHECK(Sphere->LinearDamping == 0.0f);
			for (int32 Frame = 0; Frame < Fixtures::FFreeFall::Frames; ++Frame)
			{
				Scene.Tick(1.0f / 60.0f);
			}
			const double Z60 = Sphere->GetUnrealWorldTransform().GetTranslation().Z;
			const double Reference = Fixtures::FFreeFall::ExpectedZ(Fixtures::FFreeFall::Frames, Scene.GetLastPlan().StepDeltaTime);
			char Line[160];
			std::snprintf(Line, sizeof(Line), "%s / %s: z60 = %.10f cm, |z60 - 501.8332813705| = %.3g cm", Backend, ToString(Driver), Z60,
				std::abs(Z60 - Fixtures::FFreeFall::ExpectedZ60));
			MESSAGE(Line);
			CHECK(std::abs(Z60 - Fixtures::FFreeFall::ExpectedZ60) <= Fixtures::FFreeFall::Tolerance);
			CHECK(std::abs(Z60 - Reference) <= Fixtures::FFreeFall::Tolerance);
			// v60 = -980 * 60 * dt_engine = -980.0000511 cm/s (G1's companion value).
			CHECK(Sphere->GetUnrealWorldVelocity().Z == doctest::Approx(-980.0 * 60.0 * Analytic::EngineDeltaTime(1.0f / 60.0f)).epsilon(1e-12));
			CHECK(Scene.GetStepCount() == 60u);
			CHECK(Scene.GetSimTime() == doctest::Approx(60.0 * Analytic::EngineDeltaTime(1.0f / 60.0f)).epsilon(1e-15));
			CHECK(Scene.GetBackendInfo().Name == "Null");
		}
	}
}

TEST_CASE("PhysicsTest.PhysScene.BodyCommandsReachTheBackend")
{
	// AddImpulse (10 kg, J = 1000 kg*cm/s -> dv = 100 cm/s, the S01 numbers), AddImpulse(bVelChange), SetLinearVelocity,
	// a kinematic target, start asleep + WakeInstance, and FPhysicsInterface reads.
	FResolvedPhysicsConfig Config;
	FPhysScene Scene = MakeScene(&Config);
	const FBodySetup* Cube = Scene.CreateBodySetup(MakeCubeSetup(20.0));
	FBodyInstance Template;
	Template.bSimulatePhysics = true;
	Template.bEnableGravity = false;
	Template.LinearDamping = 0.0f;
	Template.bOverrideMass = true;
	Template.MassInKgOverride = 10.0f;
	FBodyInstance* Pushed = Scene.SpawnBody(Template, Cube, FTransform::Identity);
	FBodyInstance* VelChange = Scene.SpawnBody(Template, Cube, FTransform(FVector(100.0, 0.0, 0.0)));
	FBodyInstance Sleeping = Template;
	Sleeping.bStartAwake = false;
	Sleeping.bEnableGravity = true;
	FBodyInstance* Sleeper = Scene.SpawnBody(Sleeping, Cube, FTransform(FVector(0.0, 100.0, 0.0)));
	FBodyInstance KinematicTemplate;
	FBodyInstance* Kinematic = Scene.SpawnBody(KinematicTemplate, Cube, FTransform(FVector(0.0, 0.0, 300.0)));
	Scene.FlushDeferredActors();
	CHECK(Sleeper->State.ObjectState == EObjectStateType::Sleeping);
	CHECK(FPhysicsInterface::IsSleeping(Sleeper->ActorHandle));

	Pushed->AddImpulse(FVector(1000.0, 0.0, 0.0), false);
	VelChange->AddImpulse(FVector(0.0, 0.0, 25.0), true);
	Scene.SetKinematicTarget_AssumesLocked(Kinematic, FTransform(FVector(60.0, 0.0, 300.0)), true);
	Scene.Tick(1.0f / 60.0f);
	CHECK(Pushed->GetUnrealWorldVelocity().X == doctest::Approx(100.0).epsilon(1e-12));
	CHECK(VelChange->GetUnrealWorldVelocity().Z == doctest::Approx(25.0).epsilon(1e-12));
	CHECK(Kinematic->GetUnrealWorldTransform().GetTranslation().X == doctest::Approx(60.0).epsilon(1e-12));
	CHECK(Kinematic->GetUnrealWorldVelocity().X == doctest::Approx(60.0 / Analytic::EngineDeltaTime(1.0f / 60.0f)).epsilon(1e-9));
	CHECK(Sleeper->State.ObjectState == EObjectStateType::Sleeping);
	CHECK(Sleeper->GetUnrealWorldTransform().GetTranslation().Z == 0.0);

	Sleeper->WakeInstance();
	Pushed->SetLinearVelocity(FVector(0.0, 5.0, 0.0), true);   // UE: added to the game-thread velocity, then set
	Scene.Tick(1.0f / 60.0f);
	CHECK(Sleeper->State.ObjectState == EObjectStateType::Dynamic);
	CHECK(Sleeper->GetUnrealWorldVelocity().Z < 0.0);
	CHECK(Pushed->GetUnrealWorldVelocity().Y == doctest::Approx(5.0));
	CHECK(Pushed->GetUnrealWorldVelocity().X == doctest::Approx(100.0).epsilon(1e-12));
	// Reads flush and pull: a teleport is visible immediately.
	FPhysicsInterface::SetGlobalPose_AssumesLocked(Pushed->ActorHandle, FTransform(FVector(0.0, 0.0, 777.0)));
	CHECK(FPhysicsInterface::GetGlobalPose_AssumesLocked(Pushed->ActorHandle).GetTranslation().Z == 777.0);
	CHECK(Pushed->GetUnrealWorldTransform().GetTranslation().Z == 777.0);
	// Release: the body keeps its storage, loses its actor.
	Pushed->TermBody();
	CHECK_FALSE(Pushed->ActorHandle.IsValid());
	CHECK(Scene.GetStats().NumBodies == 3);
	Scene.Tick(1.0f / 60.0f);
}

TEST_CASE("PhysicsTest.PhysScene.TimingBucketsAndStats")
{
	FPhysScene Scene = MakeScene();
	FBodyInstance Template;
	Template.bSimulatePhysics = true;
	for (int32 Index = 0; Index < 100; ++Index)
	{
		Scene.SpawnBody(Template, Scene.CreateBodySetup(MakeCubeSetup(10.0)), FTransform(FVector(0.0, 0.0, 20.0 * Index)));
	}
	Scene.FlushDeferredActors();
	Scene.Tick(1.0f / 60.0f);
	const FFrameTiming& Timing = Scene.GetLastFrameTiming();
	CHECK(Timing.NumSteps == 1u);
	CHECK(Timing.PushNs == 0u);   // no command: ApplyBodyCommands is not called (02 9.3)
	CHECK(Timing.StepNs > 0u);
	CHECK(Timing.FrameNs == Timing.PushNs + Timing.StepNs + Timing.PullNs);
	const uint64 SubBuckets = Timing.SetupNs + Timing.StartNs + Timing.WaitNs + Timing.EndNs;
	CHECK((SubBuckets > Timing.StepNs ? SubBuckets - Timing.StepNs : Timing.StepNs - SubBuckets) <= 4u);   // per-bucket rounding
	// M10: the buckets read the TSC; the raw ticks add up like the ns values (no rounding), and the scene leaves the QPC field
	// to the bench (the QPC ticks of the whole Tick, read outside the buckets).
	CHECK(Timing.FrameTsc > 0u);
	CHECK(Timing.FrameTsc == Timing.PushTsc + Timing.StepTsc + Timing.PullTsc);
	CHECK(Timing.StepTsc == Timing.SetupTsc + Timing.StartTsc + Timing.WaitTsc + Timing.EndTsc);
	CHECK(Timing.FrameQpcTicks == 0u);
	CHECK(Scene.GetStats().NumAwakeBodies == 100);
	// With a command the push runs (one ApplyBodyCommands call). Its duration can be below one QPC tick (100 ns), so the
	// check is on the effect, not on PushNs > 0.
	Scene.GetBodies()[0]->AddImpulse(FVector(1.0, 0.0, 0.0), false);
	Scene.Tick(1.0f / 60.0f);
	CHECK(Scene.GetBodies()[0]->GetUnrealWorldVelocity().X > 0.0);
	CHECK(Scene.GetLastFrameTiming().FrameNs == Scene.GetLastFrameTiming().PushNs + Scene.GetLastFrameTiming().StepNs + Scene.GetLastFrameTiming().PullNs);
	// Effective settings and the native hash are readable between frames.
	CHECK(Scene.DumpEffectiveSettings().find("\"world.gravity\"") != std::string::npos);
	CHECK(Scene.ComputeNativeStateHash() != 0u);
}

TEST_CASE("PhysicsTest.PhysScene.HarnessConsoleVariablesFollowTheConfig")
{
	// M1 review: p.ComNudgeAffectsInertia / p.SensitiveSleepThresholdMultiplier are process-global, but every resolved
	// config records (and hashes) a value. A scene sets them to its config's values, over a stale higher priority.
	IConsoleVariable* Nudge = PhysicsCVars::FindHarnessConsoleVariable("p.ComNudgeAffectsInertia");
	REQUIRE(Nudge != nullptr);
	Nudge->ResetToDefault();
	REQUIRE(Nudge->Set("0", ECVF_SetByConsole));   // a stale -ExecCmds value from "an earlier run"
	{
		FPhysScene Scene = MakeScene();   // no config: the built-in default
		CHECK(Nudge->GetBool());
		CHECK(Nudge->GetSetBy() == ECVF_SetByConstructor);
	}
	FResolvedPhysicsConfig Config;
	FResolvedConfigValue Value;
	Value.Value = "false";
	Value.Origin = EConfigOrigin::CommandLine;
	Value.OriginName = "-ExecCmds:1";
	Config.Values["ConsoleVariables.p.ComNudgeAffectsInertia"] = Value;
	{
		FPhysScene Scene = MakeScene(&Config);
		CHECK_FALSE(Nudge->GetBool());
		CHECK(Nudge->GetSetBy() == ECVF_SetByConsole);
		// The inertia of a nudged body follows the scene's value: with the CVar off there is no COMNudge term.
		FBodyInstance Template;
		Template.bSimulatePhysics = true;
		Template.COMNudge = FVector(10.0, 0.0, 0.0);
		const FBodyInstance* Body = Scene.SpawnBody(Template, Scene.CreateBodySetup(MakeCubeSetup(100.0)), FTransform::Identity);
		const double Mass = Body->GetBodyMass();
		CHECK(Body->GetBodyInertiaTensor().X == doctest::Approx(Mass * 100.0 * 100.0 / 6.0).epsilon(1e-9));
	}
	// An ini value is applied with ini priority.
	Value.Value = "true";
	Value.Origin = EConfigOrigin::PhysicsProfile;
	Value.OriginName = "Config/Profiles/Test.ini:3";
	Config.Values["ConsoleVariables.p.ComNudgeAffectsInertia"] = Value;
	{
		FPhysScene Scene = MakeScene(&Config);
		CHECK(Nudge->GetBool());
		CHECK(Nudge->GetSetBy() == ECVF_SetByConsoleVariablesIni);
	}
	Nudge->ResetToDefault();
}

TEST_CASE("PhysicsTest.PhysScene.SpawnedBodiesOnly")
{
	// Only scene-owned bodies are deferred (M1 review): SpawnBody marks them; a stack FBodyInstance is refused by
	// DeferAddActor's checkf (not exercised here: it ends the process). Templates hand over no actor handle.
	FPhysScene Scene = MakeScene();
	FBodyInstance Template;
	Template.bSimulatePhysics = true;
	Template.ActorHandle.Index = 7;
	Template.ActorHandle.Serial = 7;
	FBodyInstance* Body = Scene.SpawnBody(Template, Scene.CreateBodySetup(MakeCubeSetup(10.0)), FTransform::Identity);
	CHECK_FALSE(Body->ActorHandle.IsValid());
	Scene.FlushDeferredActors();
	CHECK(Body->ActorHandle.Index == 0u);
	CHECK(Body->ActorHandle.Scene == &Scene.GetBackendScene());
}

TEST_CASE("PhysicsTest.PhysScene.SubstepDeltaTimePerDriver")
{
	// M1 review (carry-over to M2): in UE5 substepping Chaos steps with the FReal D / N, not with its float. The Native
	// driver hands the UE arguments to the backend, which steps with D / N in double (FStepPlan::InternalStepDeltaTime);
	// the Harness driver sends the float StepDeltaTime through the ABI. Each matches its own analytic reference to 1e-9,
	// and the two differ (dt 0.05 -> 3 steps: 0.016666666915 vs 0.016666667536).
	double Z[2] = {};
	int32 Slot = 0;
	for (const EFrameDriver Driver : { EFrameDriver::Native, EFrameDriver::Harness })
	{
		FResolvedPhysicsConfig Config;
		Config.TestSettings.TickMode = EPhysicsTickMode::Substepping;
		Config.TestSettings.FrameDriver = Driver;
		FPhysScene Scene = MakeScene(&Config);
		FBodyInstance* Sphere = Fixtures::FFreeFall::Build(Scene);
		for (int32 Frame = 0; Frame < 10; ++Frame)
		{
			Scene.Tick(0.05f);
		}
		const FStepPlan& Plan = Scene.GetLastPlan();
		REQUIRE(Plan.NumSteps == 3);
		REQUIRE(Scene.GetStepCount() == 30u);
		const double Dt = Driver == EFrameDriver::Native ? Plan.InternalStepDeltaTime : Analytic::EngineDeltaTime(Plan.StepDeltaTime);
		const double Reference = Analytic::SemiImplicitEulerFreeFall(Fixtures::FFreeFall::Z0, 0.0, -980.0, Dt, 30).Z;
		Z[Slot] = Sphere->GetUnrealWorldTransform().GetTranslation().Z;
		const std::string DriverText = ToString(Driver);   // a const char* CAPTURE logs the pointer as 1
		CAPTURE(DriverText);
		CHECK(std::abs(Z[Slot] - Reference) <= 1e-9);
		CHECK(Scene.GetSimTime() == doctest::Approx(30.0 * Dt).epsilon(1e-14));
		++Slot;
	}
	CHECK(std::abs(Z[0] - Z[1]) > 1e-7);   // about 9e-6 cm apart after 30 substeps
}

namespace
{
	// A 2 kg body without gravity or damping (a force of 60 kg*cm/s^2 accelerates it by 30 cm/s^2).
	FBodyInstance* SpawnForceBody(FPhysScene& Scene, const FVector& Location)
	{
		FBodyInstance Template;
		Template.bSimulatePhysics = true;
		Template.bEnableGravity = false;
		Template.LinearDamping = 0.0f;
		Template.AngularDamping = 0.0f;
		Template.bOverrideMass = true;
		Template.MassInKgOverride = 2.0f;
		return Scene.SpawnBody(Template, Scene.CreateBodySetup(MakeCubeSetup(10.0)), FTransform(Location));
	}

	struct FForceRun
	{
		double VelocityX = 0.0;
		FStepPlan Plan;
		EFrameDriver Driver = EFrameDriver::Auto;
	};

	// One body; Push(Body, Frame) issues the frame's forces before each Tick.
	template <class FPush>
	FForceRun RunForceFrames(const EPhysicsTickMode Mode, const ESubstepClampRule Rule, const EFrameDriver Driver, const std::vector<float>& Dts, FPush&& Push)
	{
		FResolvedPhysicsConfig Config;
		Config.TestSettings.TickMode = Mode;
		Config.TestSettings.SubstepClampRule = Rule;
		Config.TestSettings.FrameDriver = Driver;
		FPhysScene Scene = MakeScene(&Config);
		FBodyInstance* Body = SpawnForceBody(Scene, FVector::ZeroVector);
		Scene.FlushDeferredActors();
		FForceRun Run;
		for (size_t Frame = 0; Frame < Dts.size(); ++Frame)
		{
			Push(Body, static_cast<int32>(Frame));
			Scene.Tick(Dts[Frame]);
		}
		Run.VelocityX = Body->GetUnrealWorldVelocity().X;
		Run.Plan = Scene.GetLastPlan();
		Run.Driver = Scene.GetFrameDriver();
		return Run;
	}
}

TEST_CASE("PhysicsTest.PhysScene.ForceRulesPerDriver")
{
	// UE5 ignores bAllowSubstepping: Chaos copies the first pushed step's accelerations into every later pushed step of the
	// frame (ChaosMarshallingManager.cpp:105-111, :325-342) and averages forces over the AsyncFixed frames that push no step
	// (PhysicsSolverBase.cpp:623-646, PBDRigidsSolver.cpp:1582). Native (Null) and Harness driver follow that rule;
	// the Harness driver's UE4PhysSubstep rule (PhysX UE4Compat) keeps UE4's split: bAllowSubstepping forces on every
	// substep, the others on the first only. FBodyInstance::AddForce and FPhysicsInterface::AddForce_AssumesLocked share
	// the rule.
	const auto Constant = [](const bool bAllowSubstepping, const bool bInterface)
	{
		return [bAllowSubstepping, bInterface](FBodyInstance* Body, int32)
		{
			if (bInterface)
			{
				FPhysicsInterface::AddForce_AssumesLocked(Body->ActorHandle, FVector(60.0, 0.0, 0.0), bAllowSubstepping, false);
			}
			else
			{
				Body->AddForce(FVector(60.0, 0.0, 0.0), bAllowSubstepping);
			}
		};
	};
	const std::vector<float> OneFrame = { 0.05f };
	SUBCASE("UE5 substepping: every substep, flag ignored, both drivers and both entry points")
	{
		for (const EFrameDriver Driver : { EFrameDriver::Native, EFrameDriver::Harness })
		{
			for (const bool bAllowSubstepping : { false, true })
			{
				for (const bool bInterface : { false, true })
				{
					const std::string DriverText = ToString(Driver);   // a const char* CAPTURE logs the pointer as 1
					CAPTURE(DriverText);
					CAPTURE(bAllowSubstepping);
					CAPTURE(bInterface);
					const FForceRun Run = RunForceFrames(EPhysicsTickMode::Substepping, ESubstepClampRule::UE5ChaosScene, Driver, OneFrame,
						Constant(bAllowSubstepping, bInterface));
					REQUIRE(Run.Driver == Driver);
					REQUIRE(Run.Plan.NumSteps == 3);
					// 30 cm/s^2 over the frame's three steps (D in double for Native, 3 x float(D / 3) for Harness).
					CHECK(Run.VelocityX == doctest::Approx(30.0 * 0.05).epsilon(1e-6));
				}
			}
		}
	}
	SUBCASE("UE4PhysSubstep (Harness): bAllowSubstepping decides, both entry points")
	{
		for (const bool bInterface : { false, true })
		{
			CAPTURE(bInterface);
			const FForceRun Split = RunForceFrames(EPhysicsTickMode::Substepping, ESubstepClampRule::UE4PhysSubstep, EFrameDriver::Auto, OneFrame,
				Constant(false, bInterface));
			const FForceRun Every = RunForceFrames(EPhysicsTickMode::Substepping, ESubstepClampRule::UE4PhysSubstep, EFrameDriver::Auto, OneFrame,
				Constant(true, bInterface));
			REQUIRE(Split.Driver == EFrameDriver::Harness);
			REQUIRE(Split.Plan.ClampRule == ESubstepClampRule::UE4PhysSubstep);
			REQUIRE(Split.Plan.NumSteps == 2);   // UseDelta = min(0.05, 1/30), 2 x 1/60
			const double StepDt = Analytic::EngineDeltaTime(Split.Plan.StepDeltaTime);
			CHECK(Split.VelocityX == doctest::Approx(30.0 * StepDt).epsilon(1e-12));        // first substep only
			CHECK(Every.VelocityX == doctest::Approx(30.0 * 2.0 * StepDt).epsilon(1e-12));  // every substep
		}
	}
	SUBCASE("A force added in OnPhysSceneStep: once per frame under UE5, per substep under UE4PhysSubstep")
	{
		// UE5 broadcasts OnPhysSceneStep once per frame: the force acts on the frame's 3 substeps once (1.5 cm/s), in both
		// drivers. (A per-step broadcast would re-send the forces of earlier calls and accumulate 1 + 2 + 3 force-steps.)
		for (const EFrameDriver Driver : { EFrameDriver::Native, EFrameDriver::Harness })
		{
			const std::string DriverText = ToString(Driver);   // a const char* CAPTURE logs the pointer as 1
			CAPTURE(DriverText);
			FResolvedPhysicsConfig Config;
			Config.TestSettings.TickMode = EPhysicsTickMode::Substepping;
			Config.TestSettings.FrameDriver = Driver;
			FPhysScene Scene = MakeScene(&Config);
			FBodyInstance* Body = SpawnForceBody(Scene, FVector::ZeroVector);
			Scene.FlushDeferredActors();
			int32 Calls = 0;
			Scene.OnPhysSceneStep = [&Calls, Body](FPhysScene*, float)
			{
				++Calls;
				Body->AddForce(FVector(60.0, 0.0, 0.0), false);
			};
			Scene.Tick(0.05f);
			REQUIRE(Scene.GetLastPlan().NumSteps == 3);
			CHECK(Calls == 1);
			CHECK(Body->GetUnrealWorldVelocity().X == doctest::Approx(30.0 * 0.05).epsilon(1e-6));
		}
		// UE4: a force without bAllowSubstepping added before substep k acts on substep k only (2 substeps, 2 calls).
		FResolvedPhysicsConfig Config;
		Config.TestSettings.TickMode = EPhysicsTickMode::Substepping;
		Config.TestSettings.SubstepClampRule = ESubstepClampRule::UE4PhysSubstep;
		FPhysScene Scene = MakeScene(&Config);
		FBodyInstance* Body = SpawnForceBody(Scene, FVector::ZeroVector);
		Scene.FlushDeferredActors();
		int32 Calls = 0;
		Scene.OnPhysSceneStep = [&Calls, Body](FPhysScene*, float)
		{
			++Calls;
			Body->AddForce(FVector(60.0, 0.0, 0.0), false);
		};
		Scene.Tick(0.05f);
		REQUIRE(Scene.GetLastPlan().NumSteps == 2);
		CHECK(Calls == 2);
		CHECK(Body->GetUnrealWorldVelocity().X == doctest::Approx(30.0 * 2.0 * Analytic::EngineDeltaTime(Scene.GetLastPlan().StepDeltaTime)).epsilon(1e-12));
	}
	SUBCASE("AsyncFixed: frames without a step carry the force, the push averages it")
	{
		// AsyncFixedTimeStepSize 1/30 at 1/60 frames: frame 0 pushes nothing, frame 1 one step of 1/30 with the mean of
		// 30 and 60 cm/s^2.
		const auto Ramp = [](FBodyInstance* Body, const int32 Frame) { Body->AddForce(FVector(Frame == 0 ? 60.0 : 120.0, 0.0, 0.0), false); };
		const std::vector<float> TwoFrames = { 1.0f / 60.0f, 1.0f / 60.0f };
		for (const EFrameDriver Driver : { EFrameDriver::Native, EFrameDriver::Harness })
		{
			const std::string DriverText = ToString(Driver);   // a const char* CAPTURE logs the pointer as 1
			CAPTURE(DriverText);
			const FForceRun Run = RunForceFrames(EPhysicsTickMode::AsyncFixed, ESubstepClampRule::UE5ChaosScene, Driver, TwoFrames, Ramp);
			REQUIRE(Run.Plan.NumSteps == 1);
			// The step dt from the plan (the float AsyncFixedTimeStepSize as double; a literal 1.0f / 30.0f could be folded
			// in double under /fp:fast).
			CHECK(Run.VelocityX == doctest::Approx(45.0 * Run.Plan.InternalStepDeltaTime).epsilon(1e-12));
		}
	}
}

TEST_CASE("PhysicsTest.PhysScene.KinematicTargetRulesPerDriver")
{
	// A kinematic body told to reach 90 cm and 90 degrees about Z in one frame. UE5 substepping (Chaos PseudoFraction
	// 1 / (N - k), PhysicsSolverBase.cpp:250, PBDRigidsEvolutionGBF.cpp:1203-1220): 3 equal steps of 30 cm / 30 degrees in
	// both drivers. UE4PhysSubstep (Harness): Lerp / Slerp at (k + 1) / N from the start pose, 2 steps of 45. AsyncFixed
	// with two steps (not solver-substepped): the target on the first step, then the velocity is zeroed.
	// Every entry point follows the scene's rule (UE5 has one path, BodyInstance.cpp:2804-2827 -> PhysScene_Chaos.cpp:
	// 1708-1713): the scene verb, FBodyInstance::SetBodyTransform(ETeleportType::None) and
	// FPhysicsInterface::SetKinematicTarget_AssumesLocked. The interface call has bAllowSubstepping false, which only the
	// UE4 rule reads (UE4's direct PhysX target: reached on the first substep).
	const double Pi = 3.14159265358979323846;
	const FTransform Goal(FQuat(FVector(0.0, 0.0, 1.0), 0.5 * Pi), FVector(90.0, 0.0, 0.0));
	enum class EEntry : uint8
	{
		Scene,
		BodyInstance,
		Interface,
	};
	struct FCase
	{
		const char* Name;
		EPhysicsTickMode Mode;
		ESubstepClampRule Rule;
		EFrameDriver Driver;
		EEntry Entry;
		float FrameDt;
		int32 Steps;
		double StepFraction;   // of the whole move covered by the last step; 0 = the last step does not move
	};
	const FCase Cases[] = {
		{ "UE5 substepping, Native", EPhysicsTickMode::Substepping, ESubstepClampRule::UE5ChaosScene, EFrameDriver::Native, EEntry::Scene, 0.05f, 3, 1.0 / 3.0 },
		{ "UE5 substepping, Harness", EPhysicsTickMode::Substepping, ESubstepClampRule::UE5ChaosScene, EFrameDriver::Harness, EEntry::Scene, 0.05f, 3, 1.0 / 3.0 },
		{ "UE5 substepping, Native, SetBodyTransform", EPhysicsTickMode::Substepping, ESubstepClampRule::UE5ChaosScene, EFrameDriver::Native, EEntry::BodyInstance,
			0.05f, 3, 1.0 / 3.0 },
		{ "UE5 substepping, Harness, SetBodyTransform", EPhysicsTickMode::Substepping, ESubstepClampRule::UE5ChaosScene, EFrameDriver::Harness, EEntry::BodyInstance,
			0.05f, 3, 1.0 / 3.0 },
		{ "UE5 substepping, Harness, FPhysicsInterface", EPhysicsTickMode::Substepping, ESubstepClampRule::UE5ChaosScene, EFrameDriver::Harness, EEntry::Interface,
			0.05f, 3, 1.0 / 3.0 },
		{ "UE4 substepping, Harness", EPhysicsTickMode::Substepping, ESubstepClampRule::UE4PhysSubstep, EFrameDriver::Harness, EEntry::Scene, 0.05f, 2, 0.5 },
		{ "UE4 substepping, Harness, SetBodyTransform", EPhysicsTickMode::Substepping, ESubstepClampRule::UE4PhysSubstep, EFrameDriver::Harness, EEntry::BodyInstance,
			0.05f, 2, 0.5 },
		{ "UE4 substepping, Harness, FPhysicsInterface", EPhysicsTickMode::Substepping, ESubstepClampRule::UE4PhysSubstep, EFrameDriver::Harness, EEntry::Interface,
			0.05f, 2, 0.0 },
		{ "AsyncFixed 2 steps, Native", EPhysicsTickMode::AsyncFixed, ESubstepClampRule::UE5ChaosScene, EFrameDriver::Native, EEntry::Scene, 1.0f / 30.0f, 2, 0.0 },
		{ "AsyncFixed 2 steps, Harness", EPhysicsTickMode::AsyncFixed, ESubstepClampRule::UE5ChaosScene, EFrameDriver::Harness, EEntry::Scene, 1.0f / 30.0f, 2, 0.0 },
	};
	for (const FCase& Case : Cases)
	{
		const std::string CaseText(Case.Name);
		CAPTURE(CaseText);
		FResolvedPhysicsConfig Config;
		Config.TestSettings.TickMode = Case.Mode;
		Config.TestSettings.SubstepClampRule = Case.Rule;
		Config.TestSettings.FrameDriver = Case.Driver;
		Config.Settings.AsyncFixedTimeStepSize = 1.0f / 60.0f;
		FPhysScene Scene = MakeScene(&Config);
		FBodyInstance KinematicTemplate;
		FBodyInstance* Kinematic = Scene.SpawnBody(KinematicTemplate, Scene.CreateBodySetup(MakeCubeSetup(10.0)), FTransform::Identity);
		Scene.FlushDeferredActors();
		switch (Case.Entry)
		{
		case EEntry::Scene: Scene.SetKinematicTarget_AssumesLocked(Kinematic, Goal, true); break;
		case EEntry::BodyInstance: Kinematic->SetBodyTransform(Goal, ETeleportType::None); break;
		case EEntry::Interface: FPhysicsInterface::SetKinematicTarget_AssumesLocked(Kinematic->ActorHandle, Goal); break;
		}
		Scene.Tick(Case.FrameDt);
		const FStepPlan& Plan = Scene.GetLastPlan();
		REQUIRE(Scene.GetFrameDriver() == Case.Driver);
		REQUIRE(Plan.NumSteps == Case.Steps);
		CHECK(Kinematic->GetUnrealWorldTransform().GetTranslation().X == doctest::Approx(90.0).epsilon(1e-12));
		CHECK(Kinematic->GetUnrealWorldTransform().GetRotation().Equals(Goal.GetRotation(), 1e-12));
		const double StepDt = Case.Driver == EFrameDriver::Native ? Plan.InternalStepDeltaTime : Analytic::EngineDeltaTime(Plan.StepDeltaTime);
		CHECK(Kinematic->GetUnrealWorldVelocity().X == doctest::Approx(90.0 * Case.StepFraction / StepDt).epsilon(1e-9));
		CHECK(Kinematic->GetUnrealWorldAngularVelocityInRadians().Z == doctest::Approx(0.5 * Pi * Case.StepFraction / StepDt).epsilon(1e-9));
	}
}

TEST_CASE("PhysicsTest.PhysScene.KinematicTeleportThenTarget")
{
	// UE5's teleport of a kinematic body also makes the pose its target and zeroes its velocity at once
	// (ChaosEngineInterface.cpp:2550-2557). A teleport and then a target in the same UE5-substepped frame: the steps start
	// from the teleported pose, not from the last pulled one, in both drivers (190 cm in 3 steps). A teleport alone reads
	// back zero velocity before the next step.
	const double Pi = 3.14159265358979323846;
	const FTransform Goal(FQuat(FVector(0.0, 0.0, 1.0), 0.5 * Pi), FVector(90.0, 0.0, 0.0));
	FTransform EndPose[2];
	FVector EndVelocity[2];
	int32 Slot = 0;
	for (const EFrameDriver Driver : { EFrameDriver::Native, EFrameDriver::Harness })
	{
		const std::string DriverText = ToString(Driver);   // a const char* CAPTURE logs the pointer as 1
		CAPTURE(DriverText);
		FResolvedPhysicsConfig Config;
		Config.TestSettings.TickMode = EPhysicsTickMode::Substepping;
		Config.TestSettings.FrameDriver = Driver;
		FPhysScene Scene = MakeScene(&Config);
		FBodyInstance KinematicTemplate;
		FBodyInstance* Kinematic = Scene.SpawnBody(KinematicTemplate, Scene.CreateBodySetup(MakeCubeSetup(10.0)), FTransform::Identity);
		Scene.FlushDeferredActors();
		Kinematic->SetBodyTransform(FTransform(FVector(-100.0, 0.0, 0.0)), ETeleportType::TeleportPhysics);
		Kinematic->SetBodyTransform(Goal, ETeleportType::None);
		Scene.Tick(0.05f);
		const FStepPlan& Plan = Scene.GetLastPlan();
		REQUIRE(Plan.NumSteps == 3);
		const double StepDt = Driver == EFrameDriver::Native ? Plan.InternalStepDeltaTime : Analytic::EngineDeltaTime(Plan.StepDeltaTime);
		CHECK(Kinematic->GetUnrealWorldTransform().GetTranslation().X == doctest::Approx(90.0).epsilon(1e-12));
		CHECK(Kinematic->GetUnrealWorldVelocity().X == doctest::Approx(190.0 / 3.0 / StepDt).epsilon(1e-9));
		EndPose[Slot] = Kinematic->GetUnrealWorldTransform();
		EndVelocity[Slot] = Kinematic->GetUnrealWorldVelocity();
		// A teleport alone: zero velocity at once (read back through the interface, which flushes and pulls).
		Kinematic->SetBodyTransform(FTransform(FVector(0.0, 50.0, 0.0)), ETeleportType::TeleportPhysics);
		CHECK(FPhysicsInterface::GetLinearVelocity_AssumesLocked(Kinematic->ActorHandle) == FVector::ZeroVector);
		CHECK(FPhysicsInterface::GetAngularVelocity_AssumesLocked(Kinematic->ActorHandle) == FVector::ZeroVector);
		Scene.Tick(0.05f);
		CHECK(Kinematic->GetUnrealWorldTransform().GetTranslation().Y == 50.0);
		CHECK(Kinematic->GetUnrealWorldVelocity() == FVector::ZeroVector);
		++Slot;
	}
	CHECK(EndPose[0].GetTranslation().Equals(EndPose[1].GetTranslation(), 1e-9));
	CHECK(EndPose[0].GetRotation().Equals(EndPose[1].GetRotation(), 1e-9));
	CHECK(EndVelocity[0].Equals(EndVelocity[1], 1e-3));   // D / N in double vs float(D / N)
}

TEST_CASE("PhysicsTest.PhysScene.ZeroStepFramesReachTheBackend")
{
	// A frame that runs no step (Sync dt 0 or below MinPhysicsDeltaTime, an AsyncFixed frame that pushes nothing) still
	// pushes its commands in both drivers, as UE5's game-thread writes take effect when they are made (M2 fix round 1):
	// an AddForce on a sleeping body wakes it in that frame, and a velocity set there is what a later bAddToCurrent adds
	// to. The two drivers end bit-identical.
	FBodyInstanceState End[2][3];
	int32 Slot = 0;
	for (const EFrameDriver Driver : { EFrameDriver::Native, EFrameDriver::Harness })
	{
		const std::string DriverText = ToString(Driver);   // a const char* CAPTURE logs the pointer as 1
		CAPTURE(DriverText);
		FResolvedPhysicsConfig Config;
		Config.TestSettings.TickMode = EPhysicsTickMode::Sync;
		Config.TestSettings.FrameDriver = Driver;
		FPhysScene Scene = MakeScene(&Config);
		FBodyInstance* Moving = SpawnForceBody(Scene, FVector::ZeroVector);
		FBodyInstance* Summed = SpawnForceBody(Scene, FVector(100.0, 0.0, 0.0));
		FBodyInstance Asleep;
		Asleep.bSimulatePhysics = true;
		Asleep.bStartAwake = false;
		FBodyInstance* Sleeper = Scene.SpawnBody(Asleep, Scene.CreateBodySetup(MakeCubeSetup(10.0)), FTransform(FVector(0.0, 100.0, 500.0)));
		Scene.FlushDeferredActors();
		REQUIRE(Sleeper->State.ObjectState == EObjectStateType::Sleeping);
		Scene.Tick(1.0f / 60.0f);

		Sleeper->AddForce(FVector::ZeroVector);                         // UE: the game-thread AddForce wakes it
		Moving->SetLinearVelocity(FVector(100.0, 0.0, 0.0), false);
		Scene.Tick(0.0f);
		CHECK(Scene.GetLastPlan().NumSteps == 0);
		CHECK(Sleeper->State.ObjectState == EObjectStateType::Dynamic);   // pulled at the end of the zero-step frame
		CHECK(Moving->State.LinearVelocity.X == 100.0);
		Moving->SetLinearVelocity(FVector(0.0, 5.0, 0.0), true);           // adds the velocity set in the zero-step frame
		// Within one frame too: bAddToCurrent sees the write issued just before (UE reads the game-thread particle).
		Summed->SetLinearVelocity(FVector(10.0, 0.0, 0.0), false);
		Summed->SetLinearVelocity(FVector(0.0, 0.0, 7.0), true);
		Scene.Tick(1.0f / 60.0f);
		CHECK(Moving->GetUnrealWorldVelocity().X == 100.0);
		CHECK(Moving->GetUnrealWorldVelocity().Y == 5.0);
		CHECK(Summed->GetUnrealWorldVelocity().X == 10.0);
		CHECK(Summed->GetUnrealWorldVelocity().Z == 7.0);
		CHECK(Sleeper->GetUnrealWorldVelocity().Z < 0.0);                    // awake and falling
		End[Slot][0] = Moving->State;
		End[Slot][1] = Summed->State;
		End[Slot][2] = Sleeper->State;
		++Slot;
	}
	for (int32 Index = 0; Index < 3; ++Index)
	{
		CAPTURE(Index);
		CHECK(std::memcmp(&End[0][Index].BodyTM, &End[1][Index].BodyTM, sizeof(FTransform)) == 0);
		CHECK(End[0][Index].LinearVelocity == End[1][Index].LinearVelocity);
		CHECK(End[0][Index].ObjectState == End[1][Index].ObjectState);
	}

	// AsyncFixed 1/30 at 1/60 frames: the first frame pushes nothing, yet its velocity set is in the pulled state.
	for (const EFrameDriver Driver : { EFrameDriver::Native, EFrameDriver::Harness })
	{
		const std::string DriverText = ToString(Driver);   // a const char* CAPTURE logs the pointer as 1
		CAPTURE(DriverText);
		FResolvedPhysicsConfig Config;
		Config.TestSettings.TickMode = EPhysicsTickMode::AsyncFixed;
		Config.TestSettings.FrameDriver = Driver;
		FPhysScene Scene = MakeScene(&Config);
		FBodyInstance* Body = SpawnForceBody(Scene, FVector::ZeroVector);
		Scene.FlushDeferredActors();
		Body->SetLinearVelocity(FVector(50.0, 0.0, 0.0), false);
		Scene.Tick(1.0f / 60.0f);
		REQUIRE(Scene.GetLastPlan().NumPushedSteps == 0);
		CHECK(Body->State.LinearVelocity.X == 50.0);
	}
}

TEST_CASE("PhysicsTest.PhysScene.ForceAndSleepFollowIssueOrder")
{
	// UE's AddForce wakes the body when it is called, and PutToSleep marks the game-thread dynamics clean, so forces
	// issued before it are not pushed unless a later force re-dirties them (SingleParticlePhysicsProxy.h:706-720,
	// ParticleHandle.h:3755-3766). Both drivers send the wake in issue order and the forces with PT_CMDF_NO_WAKE.
	for (const EFrameDriver Driver : { EFrameDriver::Native, EFrameDriver::Harness })
	{
		for (const EPhysicsTickMode Mode : { EPhysicsTickMode::Sync, EPhysicsTickMode::Substepping })
		{
			const std::string DriverText = ToString(Driver);   // a const char* CAPTURE logs the pointer as 1
			CAPTURE(DriverText);
			const std::string ModeText = ToString(Mode);   // a const char* CAPTURE logs the pointer as 1
			CAPTURE(ModeText);
			FResolvedPhysicsConfig Config;
			Config.TestSettings.TickMode = Mode;
			Config.TestSettings.FrameDriver = Driver;
			FPhysScene Scene = MakeScene(&Config);
			FBodyInstance* Dropped = SpawnForceBody(Scene, FVector::ZeroVector);
			FBodyInstance* Redirtied = SpawnForceBody(Scene, FVector(100.0, 0.0, 0.0));
			Scene.FlushDeferredActors();
			Dropped->AddForce(FVector(600.0, 0.0, 0.0));
			Dropped->PutInstanceToSleep();                         // after the force: it stays asleep and does not move
			Redirtied->AddForce(FVector(60.0, 0.0, 0.0));
			Redirtied->PutInstanceToSleep();
			Redirtied->AddForce(FVector(60.0, 0.0, 0.0));          // wakes it; both forces act (2 x 30 cm/s^2)
			Scene.Tick(0.05f);
			const double FrameDt = Mode == EPhysicsTickMode::Sync ? static_cast<double>(Scene.GetLastPlan().FrameDeltaTime) : 0.05;
			CHECK(Dropped->State.ObjectState == EObjectStateType::Sleeping);
			CHECK(Dropped->GetUnrealWorldVelocity() == FVector::ZeroVector);
			CHECK(Dropped->GetUnrealWorldTransform().GetTranslation() == FVector::ZeroVector);
			CHECK(Redirtied->State.ObjectState == EObjectStateType::Dynamic);
			CHECK(Redirtied->GetUnrealWorldVelocity().X == doctest::Approx(60.0 * FrameDt).epsilon(1e-6));
		}
	}
	// AsyncFixed: a force and a PutToSleep in a frame that pushes nothing, a WakeUp in the pushing frame: the force was
	// dropped with the clean dynamics, in both drivers.
	for (const EFrameDriver Driver : { EFrameDriver::Native, EFrameDriver::Harness })
	{
		const std::string DriverText = ToString(Driver);   // a const char* CAPTURE logs the pointer as 1
		CAPTURE(DriverText);
		FResolvedPhysicsConfig Config;
		Config.TestSettings.TickMode = EPhysicsTickMode::AsyncFixed;
		Config.TestSettings.FrameDriver = Driver;
		FPhysScene Scene = MakeScene(&Config);
		FBodyInstance* Body = SpawnForceBody(Scene, FVector::ZeroVector);
		Scene.FlushDeferredActors();
		Body->AddForce(FVector(600.0, 0.0, 0.0));
		Body->PutInstanceToSleep();
		Scene.Tick(1.0f / 60.0f);
		REQUIRE(Scene.GetLastPlan().NumPushedSteps == 0);
		CHECK(Body->State.ObjectState == EObjectStateType::Sleeping);
		Body->WakeInstance();
		Scene.Tick(1.0f / 60.0f);
		REQUIRE(Scene.GetLastPlan().NumSteps == 1);
		CHECK(Body->State.ObjectState == EObjectStateType::Dynamic);
		CHECK(Body->GetUnrealWorldVelocity() == FVector::ZeroVector);
	}
}

TEST_CASE("PhysicsTest.PhysScene.CapacityHintsReachTheBackend")
{
	// FPhysSceneInitParams carries the PtSceneDesc capacity hints (02 9.4; Jolt sizes its pools from them in M3). Null
	// reserves its body array to MaxBodiesHint and reports it.
	FPhysSceneInitParams Params;
	Params.DebugName = "Hints";
	Params.MaxBodiesHint = 123;
	Params.MaxBodyPairsHint = 456;
	Params.MaxContactConstraintsHint = 789;
	FPhysScene Scene(Params);
	const std::string Settings = Scene.DumpEffectiveSettings();
	CHECK(Settings.find("\"key\":\"null.max_bodies_hint\",\"native\":\"123\"") != std::string::npos);
}

TEST_CASE("PhysicsTest.PhysScene.ReleasedBodyDropsPendingCommands")
{
	// UE drops what was queued for a released body. Releasing a body with a pending force, kinematic target and queued
	// FPhysicsInterface commands must neither stop the frame (Fatal) nor discard another body's commands.
	for (const EFrameDriver Driver : { EFrameDriver::Native, EFrameDriver::Harness })
	{
		const std::string DriverText = ToString(Driver);   // a const char* CAPTURE logs the pointer as 1
		CAPTURE(DriverText);
		FResolvedPhysicsConfig Config;
		Config.TestSettings.FrameDriver = Driver;
		FPhysScene Scene = MakeScene(&Config);
		FBodyInstance* Released = SpawnForceBody(Scene, FVector(0.0, 0.0, 0.0));
		FBodyInstance* Kept = SpawnForceBody(Scene, FVector(100.0, 0.0, 0.0));
		FBodyInstance KinematicTemplate;
		FBodyInstance* Kinematic = Scene.SpawnBody(KinematicTemplate, Scene.CreateBodySetup(MakeCubeSetup(10.0)), FTransform(FVector(0.0, 0.0, 200.0)));
		Scene.FlushDeferredActors();

		Kept->AddImpulse(FVector(0.0, 40.0, 0.0), false);                 // queued before the release: 20 cm/s
		Released->AddForce(FVector(100.0, 0.0, 0.0));
		Released->AddImpulse(FVector(10.0, 0.0, 0.0), false);
		FPhysicsInterface::SetLinearVelocity_AssumesLocked(Released->ActorHandle, FVector(0.0, 0.0, 5.0));
		Scene.SetKinematicTarget_AssumesLocked(Kinematic, FTransform(FVector(50.0, 0.0, 200.0)), true);
		Kept->AddForce(FVector(60.0, 0.0, 0.0));                           // 30 cm/s^2
		Released->TermBody();
		Kinematic->TermBody();
		Scene.Tick(1.0f / 60.0f);

		CHECK_FALSE(Released->ActorHandle.IsValid());
		CHECK(Scene.GetStats().NumBodies == 1);
		CHECK(Kept->GetUnrealWorldVelocity().Y == doctest::Approx(20.0).epsilon(1e-12));
		CHECK(Kept->GetUnrealWorldVelocity().X == doctest::Approx(30.0 * Analytic::EngineDeltaTime(1.0f / 60.0f)).epsilon(1e-12));
		Scene.Tick(1.0f / 60.0f);
	}
}
