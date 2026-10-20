// PhysScene.cpp - FPhysScene skeleton with the M1 stub backend (see PhysScene.h).
#include "PhysScene.h"

#include "Logging/LogMacros.h"
#include "Misc/AssertionMacros.h"
#include "Physics/AnalyticReference.h"
#include "Physics/PhysicsProfile.h"

namespace PhysicsTest
{
	namespace
	{
		// M1 stand-in for the backend scene: what a backend would do at the batch boundaries, minus any physics.
		struct FStubBackendScene
		{
			uint32 NextSerial = 1;

			// One batch (02 6 steps 1-5): handles in creation order, then the initial object state; the put-to-sleep
			// step for !bStartAwake dynamic bodies is skipped when the profile disables sleeping.
			void CreateActors(std::span<FBodyInstance* const> Bodies, const uint32 FirstIndex, const bool bAllowSleeping, FPhysSceneStats& Stats)
			{
				uint32 Index = FirstIndex;
				for (FBodyInstance* Body : Bodies)
				{
					Body->ActorHandle.Scene = nullptr;   // M2: the FPhysicsBackendScene proxy
					Body->ActorHandle.Index = Index++;
					Body->ActorHandle.Serial = NextSerial++;
					Body->State.ObjectState = Body->GetInitialObjectState();
				}
				for (FBodyInstance* Body : Bodies)
				{
					if (Body->IsDynamic() && !Body->bStartAwake)
					{
						if (bAllowSleeping)
						{
							Body->State.ObjectState = EObjectStateType::Sleeping;
						}
						else
						{
							++Stats.NumStartAsleepSkipped;
						}
					}
				}
			}

			void Step(const float /*StepDeltaTime*/)
			{
			}
		};
	}

	struct FPhysScene::FImpl
	{
		std::string DebugName;
		std::string BackendName;
		FPhysicsSettings Settings;
		FPhysicsTestSettings TestSettings;
		FPhysicalMaterial DefaultMaterial;
		double AsyncDeltaTime = -1.0;

		std::deque<FPhysicalMaterial> Materials;
		std::deque<FBodySetup> BodySetups;
		std::deque<FBodyInstance> BodyStorage;
		std::deque<FConstraintInstance> ConstraintStorage;
		std::vector<FBodyInstance*> Bodies;               // creation order
		std::vector<FConstraintInstance*> Constraints;
		std::vector<FBodyInstance*> DeferredBodies;

		FStubBackendScene Backend;
		EPhysSceneFramePhase Phase = EPhysSceneFramePhase::Idle;
		bool bIsSimulating = false;
		FVector Gravity;
		FStepPlan LastPlan;
		double AccumulatedTime = 0.0;   // AsyncFixed accumulator (Chaos AccumulatedTime)
		double SimTime = 0.0;
		uint64 FrameCount = 0;
		uint64 StepCount = 0;
		FPhysSceneStats Stats;
		FFrameTiming LastTiming;

		// 02 7.1: creation, flushes and preparation happen only in Idle (not between SetUpForFrame and EndFrame).
		[[nodiscard]] bool IsIdle() const { return Phase == EPhysSceneFramePhase::Idle; }
	};

	FPhysScene::FPhysScene(const FPhysSceneInitParams& Params)
		: Impl(std::make_unique<FImpl>())
	{
		Impl->DebugName = Params.DebugName;
		Impl->BackendName = Params.BackendName;
		checkf(Impl->BackendName == "Stub", "FPhysScene '%s': backend '%s' is not available before M2 (only the M1 stub exists)",
			Impl->DebugName.c_str(), Impl->BackendName.c_str());
		if (Params.Config != nullptr)
		{
			Impl->Settings = Params.Config->Settings;
			Impl->TestSettings = Params.Config->TestSettings;
			FProfileResolver::ApplyMaterialDefaults(*Params.Config, Impl->DefaultMaterial);
		}
		Impl->DefaultMaterial.Name = "DefaultPhysicalMaterial";
		Impl->Gravity = FVector(0.0, 0.0, Impl->Settings.DefaultGravityZ);
		// The scene's async dt (FChaosScene(nullptr, AsyncDt)): -1 = sync (DL40).
		Impl->AsyncDeltaTime = FStepPlanner::GetAsyncDeltaTime(Impl->Settings, Impl->TestSettings);
	}

	FPhysScene::~FPhysScene() = default;

	FPhysicalMaterial* FPhysScene::CreatePhysicalMaterial(const FPhysicalMaterial& Template)
	{
		checkf(Impl->IsIdle(), "FPhysScene::CreatePhysicalMaterial outside the Idle phase");
		return &Impl->Materials.emplace_back(Template);
	}

	FBodySetup* FPhysScene::CreateBodySetup(FBodySetup&& Setup)
	{
		checkf(Impl->IsIdle(), "FPhysScene::CreateBodySetup outside the Idle phase");
		return &Impl->BodySetups.emplace_back(std::move(Setup));
	}

	const FPhysicalMaterial* FPhysScene::GetDefaultPhysicalMaterial() const
	{
		return &Impl->DefaultMaterial;
	}

	FBodyInstance* FPhysScene::SpawnBody(const FBodyInstance& Template, const FBodySetup* Setup, const FTransform& Transform)
	{
		checkf(Impl->IsIdle(), "FPhysScene::SpawnBody outside the Idle phase");
		checkf(Setup != nullptr, "FPhysScene::SpawnBody needs a body setup");
		checkf(Template.CollisionFilter.IsValid(), "FPhysScene::SpawnBody: collision channel %d of '%s' is not below NumCollisionChannels (%u)",
			static_cast<int32>(Template.CollisionFilter.ObjectType), Template.BodyName.c_str(), NumCollisionChannels);
		// Creation order is ActorHandle.Index (assigned by the flush); InstanceBodyIndex keeps UE's meaning (INDEX_NONE).
		FBodyInstance& Body = Impl->BodyStorage.emplace_back(Template);
		Impl->Bodies.push_back(&Body);
		Body.InitBody(Setup, Transform, this);
		return &Body;
	}

	FConstraintInstance* FPhysScene::SpawnConstraint(const FConstraintInstance& Template, FBodyInstance* Body1, FBodyInstance* Body2)
	{
		checkf(Impl->IsIdle(), "FPhysScene::SpawnConstraint outside the Idle phase");
		FConstraintInstance& Constraint = Impl->ConstraintStorage.emplace_back(Template);
		Constraint.Body1 = Body1;
		Constraint.Body2 = Body2;
		Impl->Constraints.push_back(&Constraint);
		Impl->Stats.NumConstraints = static_cast<int32>(Impl->Constraints.size());
		return &Constraint;
	}

	void FPhysScene::DeferAddActor(FBodyInstance* Body)
	{
		checkf(Impl->IsIdle(), "FPhysScene::DeferAddActor outside the Idle phase");
		checkf(Body != nullptr && Body->GetPhysicsScene() == this, "FPhysScene::DeferAddActor: the body belongs to another scene");
		Impl->DeferredBodies.push_back(Body);
	}

	void FPhysScene::FlushDeferredActors()
	{
		checkf(Impl->IsIdle(), "FPhysScene::FlushDeferredActors outside the Idle phase");
		if (Impl->DeferredBodies.empty())
		{
			return;
		}
		const uint32 FirstIndex = static_cast<uint32>(Impl->Stats.NumBodies);
		Impl->Backend.CreateActors(Impl->DeferredBodies, FirstIndex, Impl->TestSettings.bAllowSleeping, Impl->Stats);
		for (const FBodyInstance* Body : Impl->DeferredBodies)
		{
			++Impl->Stats.NumBodies;
			if (Body->IsStatic())
			{
				++Impl->Stats.NumStaticBodies;
			}
			else if (Body->IsKinematic())
			{
				++Impl->Stats.NumKinematicBodies;
			}
			else
			{
				++Impl->Stats.NumDynamicBodies;
			}
		}
		Impl->DeferredBodies.clear();
	}

	void FPhysScene::PrepareForSimulation()
	{
		checkf(Impl->IsIdle(), "FPhysScene::PrepareForSimulation outside the Idle phase");
		checkf(Impl->DeferredBodies.empty(), "FPhysScene::PrepareForSimulation with %d deferred bodies: call FlushDeferredActors first",
			static_cast<int32>(Impl->DeferredBodies.size()));
	}

	void FPhysScene::Tick(const float DeltaSeconds)
	{
		// UWorld::Tick physics part (LevelTick.cpp:1740-1778, PhysLevel.cpp:194-212 [V]): SetUpForFrame first, then
		// StartFrame, which broadcasts OnPhysScenePreTick (UE5 FPhysScene_Chaos::OnStartFrame).
		const FSetUpForFrameParams Params = FStepPlanner::MakeSetUpForFrameParams(DeltaSeconds, Impl->Settings, Impl->TestSettings);
		const FVector Gravity(0.0, 0.0, Impl->Settings.DefaultGravityZ);
		SetUpForFrame(&Gravity, Params.DeltaSeconds, Params.MinPhysicsDeltaTime, Params.MaxPhysicsDeltaTime, Params.MaxSubstepDeltaTime,
			Params.MaxSubsteps, Params.bSubstepping);
		StartFrame();
		WaitPhysScenes();
		EndFrame();
	}

	void FPhysScene::SetUpForFrame(const FVector* NewGrav, const float InDeltaSeconds, const float InMinPhysicsDeltaTime,
		const float InMaxPhysicsDeltaTime, const float InMaxSubstepDeltaTime, const int32 InMaxSubsteps, const bool bSubstepping)
	{
		checkf(Impl->Phase == EPhysSceneFramePhase::Idle, "FPhysScene::SetUpForFrame outside the Idle phase");
		checkf(Impl->DeferredBodies.empty(), "FPhysScene::SetUpForFrame with deferred bodies: call FlushDeferredActors first");
		if (NewGrav != nullptr)
		{
			Impl->Gravity = *NewGrav;
		}
		FSetUpForFrameParams Params;
		Params.DeltaSeconds = InDeltaSeconds;
		Params.MinPhysicsDeltaTime = InMinPhysicsDeltaTime;
		Params.MaxPhysicsDeltaTime = InMaxPhysicsDeltaTime;
		Params.MaxSubstepDeltaTime = InMaxSubstepDeltaTime;
		Params.MaxSubsteps = InMaxSubsteps;
		Params.bSubstepping = bSubstepping;
		Impl->LastPlan = FStepPlanner::PlanFrame(Params, Impl->AsyncDeltaTime, Impl->TestSettings.SubstepClampRule, Impl->AccumulatedTime);
		Impl->Phase = EPhysSceneFramePhase::SetUp;
	}

	void FPhysScene::StartFrame()
	{
		checkf(Impl->Phase == EPhysSceneFramePhase::SetUp, "FPhysScene::StartFrame without SetUpForFrame");
		const FStepPlan& Plan = Impl->LastPlan;
		// UE5: FChaosScene::StartFrame -> FPhysScene_Chaos::OnStartFrame(MDeltaTime) broadcasts OnPhysScenePreTick with
		// the clamped frame dt before the solver advances (ChaosScene.cpp:363-374, PhysScene_Chaos.cpp:2272-2316 [V]).
		// The phase is still SetUp here: per-frame work issued from PreTick applies to this frame's steps.
		if (OnPhysScenePreTick)
		{
			OnPhysScenePreTick(this, Plan.FrameDeltaTime);
		}
		Impl->bIsSimulating = true;
		Impl->Phase = EPhysSceneFramePhase::Started;
		// Harness driver (02 9.1): one engine step per planned step, OnPhysSceneStep before each (the UE4 PhysX
		// substepping behaviour; UE5 broadcasts OnPhysSceneStep once per frame, right after PreTick, with the frame dt).
		// The Native driver (one backend SetUpForFrame per frame) needs the M2 ABI.
		const double StepDtEngine = Analytic::EngineDeltaTime(Plan.StepDeltaTime);
		for (int32 Step = 0; Step < Plan.NumSteps; ++Step)
		{
			if (OnPhysSceneStep)
			{
				OnPhysSceneStep(this, Plan.StepDeltaTime);
			}
			Impl->Backend.Step(Plan.StepDeltaTime);
			++Impl->StepCount;
			Impl->SimTime += StepDtEngine;
		}
	}

	void FPhysScene::WaitPhysScenes()
	{
		checkf(Impl->Phase == EPhysSceneFramePhase::Started, "FPhysScene::WaitPhysScenes without StartFrame");
		Impl->Phase = EPhysSceneFramePhase::Waited;   // no-op wait, kept for API parity
	}

	void FPhysScene::EndFrame()
	{
		checkf(Impl->Phase == EPhysSceneFramePhase::Waited, "FPhysScene::EndFrame without WaitPhysScenes");
		Impl->Phase = EPhysSceneFramePhase::Idle;
		Impl->bIsSimulating = false;
		Impl->LastTiming = FFrameTiming();
		Impl->LastTiming.NumSteps = static_cast<uint32>(Impl->LastPlan.NumSteps);
		Impl->Stats.Last = Impl->LastTiming;
		int32 Awake = 0;
		for (const FBodyInstance* Body : Impl->Bodies)
		{
			// Simulated bodies that are not asleep (kinematic and static bodies are never counted).
			Awake += Body->State.ObjectState == EObjectStateType::Dynamic ? 1 : 0;
		}
		Impl->Stats.NumAwakeBodies = Awake;
		++Impl->FrameCount;
		if (OnPhysScenePostTick)
		{
			// UE5 broadcasts PostTick without a dt (ChaosScene.cpp:576 [V]); the harness passes the same frame dt as PreTick.
			OnPhysScenePostTick(this, Impl->LastPlan.FrameDeltaTime);
		}
	}

	bool FPhysScene::IsSimulating() const
	{
		return Impl->bIsSimulating;
	}

	EPhysSceneFramePhase FPhysScene::GetFramePhase() const
	{
		return Impl->Phase;
	}

	std::span<FBodyInstance* const> FPhysScene::GetBodies() const
	{
		return Impl->Bodies;
	}

	std::span<FConstraintInstance* const> FPhysScene::GetConstraints() const
	{
		return Impl->Constraints;
	}

	int32 FPhysScene::GetNumDeferredActors() const
	{
		return static_cast<int32>(Impl->DeferredBodies.size());
	}

	const FPhysicsSettings& FPhysScene::GetSettings() const
	{
		return Impl->Settings;
	}

	const FPhysicsTestSettings& FPhysScene::GetTestSettings() const
	{
		return Impl->TestSettings;
	}

	FVector FPhysScene::GetGravity() const
	{
		return Impl->Gravity;
	}

	double FPhysScene::GetSimTime() const
	{
		return Impl->SimTime;
	}

	uint64 FPhysScene::GetFrameCount() const
	{
		return Impl->FrameCount;
	}

	uint64 FPhysScene::GetStepCount() const
	{
		return Impl->StepCount;
	}

	const FPhysSceneStats& FPhysScene::GetStats() const
	{
		return Impl->Stats;
	}

	const FFrameTiming& FPhysScene::GetLastFrameTiming() const
	{
		return Impl->LastTiming;
	}

	const FStepPlan& FPhysScene::GetLastPlan() const
	{
		return Impl->LastPlan;
	}

	const std::string& FPhysScene::GetDebugName() const
	{
		return Impl->DebugName;
	}

	const std::string& FPhysScene::GetBackendName() const
	{
		return Impl->BackendName;
	}
}
