// PhysSceneTests.cpp - FPhysScene skeleton (03_WorkPlan M1 task 5, 02_Architecture 6, A.9): pointer-stable storage,
// deferred actors in creation order, the start-asleep step, delegates and the frame state machine (stub backend).
#include "CoreMinimal.h"

#include "Physics/AnalyticReference.h"
#include "Physics/PhysicsProfile.h"
#include "PhysScene.h"

#include "Misc/PtThirdPartyIncludesStart.h"
#include <doctest/doctest.h>
#include "Misc/PtThirdPartyIncludesEnd.h"

#include <cmath>
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

	FPhysScene MakeScene(const FResolvedPhysicsConfig* Config = nullptr)
	{
		FPhysSceneInitParams Params;
		Params.DebugName = "UnitTest";
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
	// Substepping (UE5): dt 0.05 -> 3 OnPhysSceneStep calls of 1/60.
	{
		FResolvedPhysicsConfig Config;
		Config.TestSettings.TickMode = EPhysicsTickMode::Substepping;
		FPhysScene Scene = MakeScene(&Config);
		std::vector<float> Steps;
		Scene.OnPhysSceneStep = [&Steps](FPhysScene*, const float Dt) { Steps.push_back(Dt); };
		Scene.Tick(0.05f);
		REQUIRE(Steps.size() == 3u);
		CHECK(Steps[0] == 1.0f / 60.0f);
		CHECK(Scene.GetLastPlan().Params.bSubstepping);
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
